/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>

#include "mnetssdengine.h"

static inline int max(int a, int b) { return ((a > b) ? a : b); }
static inline int min(int a, int b) { return ((a < b) ? a : b); }

const std::string MnetSSDEngine::kModelLib = "/usr/lib/libmssdv1_nn.so";

int32_t MnetSSDEngine::Init(const NNSourceInfo* source_info)
{
  int32_t out_sizes[kNumOutputs];
  out_sizes[0] = kOutSize0;
  out_sizes[1] = kOutSize1;

  EngineInit(source_info, out_sizes);

  std::string labels_file_name(source_info->data_folder);
  labels_file_name += "/coco_labels.txt";
  uint8_t ret = ReadLabelsFiles(labels_file_name, labels_);
  if (ret) {
      ALOGE("Failed to read labels file");
      return ret;
  }

  scale_back_x_ = (float)in_width_ / scale_width_;
  scale_back_y_ = (float)in_height_ / scale_height_;

  ALOGD("%s:%d: scale_back: %.2fx%.2f in: %dx%d scaled: %dx%d", __func__,
      __LINE__, scale_back_x_, scale_back_y_, in_width_, in_height_,
      scale_width_, scale_height_);

  return NN_OK;
}

void MnetSSDEngine::DeInit()
{
  EngineDeInit();
}

static float GetIoU(
    int                     idxRect1,
    int                     idxRect2,
    float*                  pRects,
    struct DetectPPConfig*  pConfig)
{
    float* pBox1     = &pRects[idxRect1 * pConfig->numCoordBox];
    float* pBox2     = &pRects[idxRect2 * pConfig->numCoordBox];
    float  areaRect1 = (pBox1[2] - pBox1[0]) * (pBox1[3] - pBox1[1]);
    float  areaRect2 = (pBox2[2] - pBox2[0]) * (pBox2[3] - pBox2[1]);

    if ((areaRect1 < 0) || (areaRect2 < 0))
    {
        return 0.0;
    }

    float y1 = fmax(pBox1[0], pBox2[0]);
    float x1 = fmax(pBox1[1], pBox2[1]);
    float y2 = fmin(pBox1[2], pBox2[2]);
    float x2 = fmin(pBox1[3], pBox2[3]);

    float width  = fmax(x2 - x1, 0.0f);
    float height = fmax(y2 - y1, 0.0f);
    float inter  = width * height;
    float IoU    = inter / (areaRect1 + areaRect2 - inter);

    return IoU;
}

static void TransformBBoxCore(
    const float* pIn,
    float*       pOut,
    const float* pAnchor,
    struct DetectPPConfig*  pConfig)
{
    float yCenter = pIn[0] / pConfig->YScale * pAnchor[2] + pAnchor[0];
    float xCenter = pIn[1] / pConfig->XScale * pAnchor[3] + pAnchor[1];
    float halfH   = 0.5f * (float)(exp(pIn[2] / pConfig->heightScale)) * pAnchor[2];
    float halfW   = 0.5f * (float)(exp(pIn[3] / pConfig->widthScale)) * pAnchor[3];

    // Lower left corner  = (0, 1)
    // Upper right corner = (2, 3)
    pOut[0] = yCenter - halfH;  // ymin
    pOut[1] = xCenter - halfW;  // xmin
    pOut[2] = yCenter + halfH;  // ymax
    pOut[3] = xCenter + halfW;  // xmax
}

static int FindMaxClassPred(
    float*  pClassPred,
    int*    pValidObjects,
    int     numValid,
    int     classIdx,
    int     numClasses)
{
    float maxPred = 0.f;
    int   maxIdx  = -1;

    for (int i = 0; i < numValid; i++)
    {
        if (-1 != pValidObjects[i])
        {
            int tempIdx = classIdx + (pValidObjects[i] * numClasses);
            if (pClassPred[tempIdx] > maxPred)
            {
                maxPred = pClassPred[tempIdx];
                maxIdx  = i;
            }
        }
    }
    return maxIdx;
}

static void* DoNMSCore(
    void* pArg)
{
    struct NMSThreadData* pNMSData    = (struct NMSThreadData*)pArg;
    int                   numCoordBox = pNMSData->pConfig->numCoordBox;

    for (int classIdx = pNMSData->beginIndex; classIdx < pNMSData->endIndex; classIdx++)
    {
        int   validObjects[MaxNumDetect] = { 0 };
        int   numValid                   = 0;

        for (int outputIdx = 0; outputIdx < pNMSData->pConfig->numOutputs; outputIdx++)
        {
            int tempIdx = classIdx + (outputIdx * pNMSData->pConfig->numClasses);
            if (pNMSData->pClassPred[tempIdx] >= pNMSData->pConfig->appThreshold)
            {
                validObjects[numValid++] = outputIdx;

                const float* pIn     = &pNMSData->pRawBoxEnc[outputIdx * numCoordBox];
                float*       pOut    = &pNMSData->pBoxEnc[outputIdx * numCoordBox];
                const float* pAnchor = &pNMSData->pAnchors[outputIdx * numCoordBox];

                if ((0.f == pOut[0]) && (0.f == pOut[1]) && (0.f == pOut[2]) && (0.f == pOut[3]))
                {
                    TransformBBoxCore(pIn, pOut, pAnchor, pNMSData->pConfig);
                }
            }
        }

        if (numValid > 0)
        {
            int remaining = numValid;
            do
            {
                int maxIdx = FindMaxClassPred(pNMSData->pClassPred,
                                              validObjects,
                                              numValid,
                                              classIdx,
                                              pNMSData->pConfig->numClasses);
                if (-1 == maxIdx)
                {
                    break;
                }

                for (int validIdx = 0; validIdx < numValid; validIdx++)
                {
                    if ((-1 != validObjects[validIdx]) && (validIdx != maxIdx))
                    {
                        float IoU = GetIoU(validObjects[validIdx], validObjects[maxIdx], pNMSData->pBoxEnc, pNMSData->pConfig);
                        if (IoU > pNMSData->pConfig->IOUThresh)
                        {
                            validObjects[validIdx] = -1; // invalidate
                            remaining--;
                        }
                    }
                }

                pNMSData->candidates[pNMSData->numCandidates].classIdx = classIdx;
                pNMSData->candidates[pNMSData->numCandidates].score    =
                    pNMSData->pClassPred[pNMSData->pConfig->numClasses * validObjects[maxIdx] + classIdx];

                int squareShape = pNMSData->pConfig->squareShape;

                pNMSData->candidates[pNMSData->numCandidates].bbox[0]  =
                    max(0, (int)(squareShape * pNMSData->pBoxEnc[numCoordBox * validObjects[maxIdx]]));
                pNMSData->candidates[pNMSData->numCandidates].bbox[1]  =
                    max(0, (int)(squareShape * pNMSData->pBoxEnc[numCoordBox * validObjects[maxIdx] + 1]));
                pNMSData->candidates[pNMSData->numCandidates].bbox[2]  =
                    min(squareShape, (int)(squareShape * pNMSData->pBoxEnc[numCoordBox * validObjects[maxIdx] + 2]));
                pNMSData->candidates[pNMSData->numCandidates].bbox[3]  =
                    min(squareShape, (int)(squareShape * pNMSData->pBoxEnc[numCoordBox * validObjects[maxIdx] + 3]));

                pNMSData->numCandidates++;

                validObjects[maxIdx] = -1; // invalidate
                remaining--;
            } while (remaining > 0);
        }
    }

    pthread_exit(NULL);
}

static float FindMaxScoreLessThan(
    float                currMaxScore,
    struct DetectResult* pCandidates,
    int                  numCandidates,
    int*                 pOutIdx)
{
    float localMax    = 0.f;
    int   localMaxIdx = -1;

    for (int i = 0; i < numCandidates; i++)
    {
        if ((pCandidates[i].score > localMax) && (pCandidates[i].score < currMaxScore))
        {
            localMax    = pCandidates[i].score;
            localMaxIdx = i;
        }
    }

    *pOutIdx = localMaxIdx;
    return localMax;
}

int DoNMSMultiThread(
    float*                  pClassPred,
    float*                  pRawBoxEnc,
    float*                  pBoxEnc,
    const  float*           pAnchors,
    struct DetectResult*    pFinalDetections,
    struct DetectPPConfig*  pConfig,
    struct NMSThreadData*   pNMSDataA,
    struct NMSThreadData*   pNMSDataB)
{
    int finalCandidateIdx = 0;

    /*
    For each class
        Discard all boxes with p(c) <= appSensitivity
        While there are any remaining boxes
            Pick the box with the largest p(c) and output that as a prection
            Discard any remaining box with IoU >= IoUThresh, with the box output in the previous step
        end While
    end For
    */

    pNMSDataA->beginIndex     = 1;
    pNMSDataA->endIndex       = pConfig->numClasses / 2;
    pNMSDataA->pClassPred     = pClassPred;
    pNMSDataA->pRawBoxEnc     = pRawBoxEnc;
    pNMSDataA->pBoxEnc        = pBoxEnc;
    pNMSDataA->pAnchors       = pAnchors;
    pNMSDataA->pConfig        = pConfig;
    pNMSDataA->numCandidates  = 0;

    pNMSDataB->beginIndex     = pConfig->numClasses / 2;
    pNMSDataB->endIndex       = pConfig->numClasses;
    pNMSDataB->pClassPred     = pClassPred;
    pNMSDataB->pRawBoxEnc     = pRawBoxEnc;
    pNMSDataB->pBoxEnc        = pBoxEnc;
    pNMSDataB->pAnchors       = pAnchors;
    pNMSDataB->pConfig        = pConfig;
    pNMSDataB->numCandidates  = 0;

    pthread_t tIdA;
    pthread_t tIdB;

    int retA = pthread_create(&tIdA, 0, DoNMSCore, pNMSDataA);
    int retB = pthread_create(&tIdB, 0, DoNMSCore, pNMSDataB);

    if ((0 == retA) && (0 == retB))
    {
        pthread_join(tIdA, NULL);
        pthread_join(tIdB, NULL);

        int   numCandidates = pNMSDataA->numCandidates + pNMSDataB->numCandidates;
        float currMaxScore  = 100.f;

        while ((finalCandidateIdx < pConfig->maxDetections) && (finalCandidateIdx < numCandidates))
        {
            int   idxA   = 0;
            int   idxB   = 0;

            float scoreA = FindMaxScoreLessThan(currMaxScore, &pNMSDataA->candidates[0], pNMSDataA->numCandidates, &idxA);
            float scoreB = FindMaxScoreLessThan(currMaxScore, &pNMSDataB->candidates[0], pNMSDataB->numCandidates, &idxB);

            if ((-1 == idxA) && (-1 == idxB))
            {
                break;
            }

            currMaxScore = fmax(scoreA, scoreB);

            pFinalDetections[finalCandidateIdx++] =
                (scoreA == currMaxScore) ? pNMSDataA->candidates[idxA] : pNMSDataB->candidates[idxB];
        }
    }
    else
    {
        ALOGE(" Failed in DoNMSMultiThread\n");
    }

    return finalCandidateIdx;
}

int32_t MnetSSDEngine::PostProcess(void* outputs[], GstBuffer* gst_buffer)
{
  std::unique_lock<std::mutex> lock(lock_);

  float* pBoxPred   = static_cast<float*>(outputs[0]);
  float* pClassPred = static_cast<float*>(outputs[1]);

  NMSThreadData   NMSDataA;
  NMSThreadData   NMSDataB;
  DetectPPConfig  detectPPConfig;

  detectPPConfig.numClasses          = 91;
  detectPPConfig.numOutputs          = 1917;
  detectPPConfig.numCoordBox         = 4;
  detectPPConfig.YScale              = 10.0f;
  detectPPConfig.XScale              = 10.0f;
  detectPPConfig.heightScale         = 5.0f;
  detectPPConfig.widthScale          = 5.0f;
  detectPPConfig.IOUThresh           = 0.6f;
  detectPPConfig.appThreshold        = 0.5f;
  detectPPConfig.maxClassesPerDetect = 1;
  detectPPConfig.maxDetections       = 10;
  detectPPConfig.squareShape         = 300;

  float* pTempBoxEncBuf =
      static_cast<float*>(malloc(detectPPConfig.numOutputs * detectPPConfig.numCoordBox * sizeof(float)));
  if (!pTempBoxEncBuf) {
    ALOGE("%s: Failed to allocate memory", __func__);
    return NN_FAIL;
  }

  memset(pTempBoxEncBuf, 0, detectPPConfig.numOutputs * detectPPConfig.numCoordBox * sizeof(float));

  num_detect_ = DoNMSMultiThread(pClassPred,
                                 pBoxPred,
                                 pTempBoxEncBuf,
                                 &gAnchors[0],
                                 &detections_[0],
                                 &detectPPConfig,
                                 &NMSDataA,
                                 &NMSDataB);

  free(pTempBoxEncBuf);

  if (gst_buffer) {
    for (int i = 0; i < num_detect_; i++) {
      if (detections_[i].score < 0.6) continue;
      ALOGI("Object class %s prob %f box %d %d %d %d",
            labels_[detections_[i].classIdx].c_str(),
            detections_[i].score,
            detections_[i].bbox[0],
            detections_[i].bbox[1],
            detections_[i].bbox[2],
            detections_[i].bbox[3]);

      GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(gst_buffer);
      if (!meta) {
        ALOGE("%s: Failed to create metadata", __func__);
        return NN_FAIL;
      }

      GstMLClassificationResult *box_info =
          (GstMLClassificationResult*)malloc(sizeof(GstMLClassificationResult));

      guint label_size = labels_[detections_[i].classIdx].size() + 1;
      box_info->name = (gchar *)malloc(label_size);
      snprintf(box_info->name, label_size, "%s",
              labels_[detections_[i].classIdx].c_str());
      if (!box_info->name) {
        ALOGE("%s: Failed to create metadata", __func__);
        return NN_FAIL;
      }

      box_info->confidence = detections_[i].score;
      meta->box_info = g_slist_append (meta->box_info, box_info);
      meta->bounding_box.x = detections_[i].bbox[1] * scale_back_x_;
      meta->bounding_box.y = detections_[i].bbox[0] * scale_back_y_;
      meta->bounding_box.width =
          detections_[i].bbox[3] * scale_back_x_ - meta->bounding_box.x;
      meta->bounding_box.height =
          detections_[i].bbox[2] * scale_back_y_ - meta->bounding_box.y;
    }
  }

  return NN_OK;
}

int32_t MnetSSDEngine::FillMLMeta(GstBuffer * gst_buffer)
{
  std::unique_lock<std::mutex> lock(lock_);

  for (int i = 0; i < num_detect_; i++) {
    if (detections_[i].score < 0.6) continue;

    GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(gst_buffer);
    if (!meta) {
      ALOGE("%s: Failed to create metadata", __func__);
      return NN_FAIL;
    }

    GstMLClassificationResult *box_info =
        (GstMLClassificationResult*)malloc(sizeof(GstMLClassificationResult));

    guint label_size = labels_[detections_[i].classIdx].size() + 1;
    box_info->name = (gchar *)malloc(label_size);
    if (!box_info->name) {
      ALOGE("%s: Failed to create metadata", __func__);
      return NN_FAIL;
    }

    snprintf(box_info->name, label_size, "%s",
            labels_[detections_[i].classIdx].c_str());
    box_info->confidence = detections_[i].score;
    meta->box_info = g_slist_append (meta->box_info, box_info);
    meta->bounding_box.x = detections_[i].bbox[1] * scale_back_x_;
    meta->bounding_box.y = detections_[i].bbox[0] * scale_back_y_;
    meta->bounding_box.width =
        detections_[i].bbox[3] * scale_back_x_ - meta->bounding_box.x;
    meta->bounding_box.height =
        detections_[i].bbox[2] * scale_back_y_ - meta->bounding_box.y;
  }

  return NN_OK;
}
