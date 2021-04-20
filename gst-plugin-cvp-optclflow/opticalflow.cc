/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

#include "opticalflow.h"

namespace cvp {

OFEngine::OFEngine(CVPConfig &config) : config_(config) {
  is_start = true;
  frameid = 0;
  Init_Handle = NULL;
  pSessH = NULL;
}

int32_t OFEngine::Init() {
  int res = CVP_OK;
  CVP_LOGI("%s: Enter", __func__);

  // init CVP session
  pSessH = cvpCreateSession (NULL, NULL);
  if (pSessH == nullptr) {
    CVP_LOGE("%s: Create session failed, NULL session handle returned",
             __func__);
  }
  CVP_LOGI("%s: CVP Session %p", __func__, pSessH);

  // Init call config Parameters
  pConfig.nActualFps         = config_.fps;
  pConfig.nOperationalFps    = config_.fps;
  pConfig.sImageInfo.nWidth  = config_.source_info.width;
  pConfig.sImageInfo.nHeight = config_.source_info.height;
  pConfig.eMode              = (cvpOpticalFlowMode)1; // seven-pass
  pConfig.bStatsEnable       = config_.stats_enable;
  switch (config_.source_info.format) {
    case CVPImageFormat::cvp_format_nv12:
      pConfig.sImageInfo.eFormat = CVP_COLORFORMAT_NV12;
      break;
    case CVPImageFormat::cvp_format_nv21:
    case CVPImageFormat::cvp_format_gray8bit:
      pConfig.sImageInfo.eFormat = CVP_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      CVP_LOGE("%s: ERROR, invalid video format", __func__);
      return CVP_FAIL;
  }

  CVP_LOGI("%s: Query image info", __func__);
  cvpImageInfo pQueryImageInfo;
  cvpStatus query = cvpQueryImageInfo(pConfig.sImageInfo.eFormat,
                                      pConfig.sImageInfo.nWidth,
                                      pConfig.sImageInfo.nHeight,
                                      &pQueryImageInfo);
  if (query != CVP_SUCCESS) {
    CVP_LOGE("%s: Error! Query image info failed", __func__);
    return query;
  }

  CVP_LOGI("%s:      Width      %d", __func__, pQueryImageInfo.nWidth);
  CVP_LOGI("%s:      Height     %d", __func__, pQueryImageInfo.nHeight);
  CVP_LOGI("%s:      Format     %d", __func__, pQueryImageInfo.eFormat);

  pConfig.sImageInfo.nPlane = pQueryImageInfo.nPlane;
  pConfig.sImageInfo.nTotalSize = pQueryImageInfo.nTotalSize;
  CVP_LOGI("%s:      Plane      %d", __func__, pConfig.sImageInfo.nPlane);
  CVP_LOGI("%s:      Total size %d", __func__, pConfig.sImageInfo.nTotalSize);

  for (unsigned int i = 0; i < pConfig.sImageInfo.nPlane; i++) {
    pConfig.sImageInfo.nWidthStride[i] = pQueryImageInfo.nWidthStride[i];
    pConfig.sImageInfo.nAlignedSize[i] = pQueryImageInfo.nAlignedSize[i];
    CVP_LOGI("%s:      Plane %d width stride %d, aligned size %d",
              __func__, i,
              pConfig.sImageInfo.nWidthStride[i],
              pConfig.sImageInfo.nAlignedSize[i]);
  }

  CVP_LOGI("%s: Optical flow Config", __func__);
  CVP_LOGI("%s:  actual fps:      %d", __func__, pConfig.nActualFps);
  CVP_LOGI("%s:  operational fps: %d", __func__, pConfig.nOperationalFps);
  CVP_LOGI("%s:  format:          %d", __func__, pConfig.sImageInfo.eFormat);
  CVP_LOGI("%s:  width:           %d", __func__, pConfig.sImageInfo.nWidth);
  CVP_LOGI("%s:  height:          %d", __func__, pConfig.sImageInfo.nHeight);
  CVP_LOGI("%s:  total size:      %d", __func__, pConfig.sImageInfo.nTotalSize);
  CVP_LOGI("%sl  plane:           %d", __func__, pConfig.sImageInfo.nPlane);
  CVP_LOGI("%s:  pass type:       %d", __func__, pConfig.eMode);
  CVP_LOGI("%s:  stats enable:    %d", __func__, pConfig.bStatsEnable);

  CVP_LOGI("%s: Optical flow Adv Config", __func__);
  CVP_LOGI("%s:  MV Dist:                 %d", __func__, pAdvConfig.nMvDist);
  CVP_LOGI("%s:  median filter type:      %d", __func__,
                                               pAdvConfig.nMedianFiltType);
  CVP_LOGI("%s:  threshold median filter: %d", __func__,
                                               pAdvConfig.nThresholdMedFilt);
  CVP_LOGI("%s:  smoothness penalty:      %d",
           __func__, pAdvConfig.nSmoothnessPenaltyThresh);
  CVP_LOGI("%s:  search range X:          %d", __func__,
                                               pAdvConfig.nSearchRangeX);
  CVP_LOGI("%s:  search range Y:          %d", __func__,
                                               pAdvConfig.nSearchRangeY);
  CVP_LOGI("%s:  enable EIC:              %d", __func__, pAdvConfig.bEnableEic);

  //Init Call
  Init_Handle = cvpInitOpticalFlow(pSessH, &pConfig, &pAdvConfig, &pOutMemReq,
                                   NULL, NULL);
  if (Init_Handle == NULL) {
    CVP_LOGE("%s: ERROR cvpInitOpticalFlow call failed. Init_Handle is NULL",
             __func__);
    return CVP_FAIL;
  }

  CVP_LOGI("%s: CVP optical flow initialized", __func__);
  CVP_LOGI("%s: CVP Output MV bytes:    %d", __func__,
                                             pOutMemReq.nMotionVectorBytes);
  CVP_LOGI("%s: CVP Output Stats bytes: %d", __func__,
                                             pOutMemReq.nStatsBytes);

  // allocate image and output buffer
  res = AllocateBuffer();
  if (res != CVP_SUCCESS) {
    CVP_LOGE("%s: Buffer allocation failed", __func__);
  }

  // CVP Start Session
  res = cvpStartSession (pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGE("%s: CVP session fail to start", __func__);
    return CVP_FAIL;
  }
  CVP_LOGI("%s: CVP session start", __func__);

  CVP_LOGI("%s: Exit", __func__);
  return res;
}

void OFEngine::Deinit() {
  int res = CVP_OK;
  CVP_LOGI("%s: Enter", __func__);

  // CVP Stop Session
  res = cvpStopSession(pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGI("%s: Stop CVP session Failed", __func__);
  }
  CVP_LOGI("%s: CVP session stopped", __func__);

  // Free image and output buffer
  FreeBuffer();

  // OF De-Initialization
  res = cvpDeInitOpticalFlow(Init_Handle);
  if (res != CVP_SUCCESS) {
    CVP_LOGI("%s: DeInit Optical flow Failed", __func__);
    return;
  }
  else {
    CVP_LOGI("%s: Optical flow deinit", __func__);
  }

  // CVP Delete session
  res = cvpDeleteSession(pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGI("%s: cvpDeleteSession Failed", __func__);
    return;
  }
  else {
    CVP_LOGI("%s: CVP session deleted", __func__);
  }
  CVP_LOGI("%s: Exit", __func__);
  return;
}

/* Process
 * Check input frame from GST
 * Assign image to cur image buffer
 *
 * If ref buffer does not exist
 * Skip to next iteration
 *
 * If ref buffer exist
 * Register both image buffer to optical flow
 * Run optical flow sync
 * Process output
 *
 * Swap ref and cur image buffer
 * Clean old ref image buffer
 */
int32_t OFEngine::Process(GstVideoFrame *frame) {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  if (!frame || !frame->buffer) {
    CVP_LOGE("%s Null pointer!", __func__);
    return CVP_FAIL;
  }

  uint8_t *frame_data_plane0 = (uint8_t*) GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  // always assign frame to current buffer
  uint8_t *curimage = (uint8_t*) pCurImage.pBuffer->pAddress;
  // copy frame pixel to image buffer
  memcpy(curimage, frame_data_plane0, pConfig.sImageInfo.nAlignedSize[0]);
  CVP_LOGI("%s: Copy %d to cur image buffer", __func__,
           pConfig.sImageInfo.nAlignedSize[0]);

  if (!is_start) {
    // register current image
    res = cvpRegisterOpticalFlowImageBuf(Init_Handle, &pRefImage);
    if (res != CVP_SUCCESS) {
      CVP_LOGE("%s: Unable to register ref image buffer", __func__);
      return res;
    }
    res = cvpRegisterOpticalFlowImageBuf(Init_Handle, &pCurImage);
    if (res != CVP_SUCCESS) {
      CVP_LOGE("%s: Unable to register cur image buffer", __func__);
      return res;
    }
    CVP_LOGI("%s: Register image buffers to optical flow", __func__);

    // optical flow sync
    {
      Timer t("Process time");
      CVP_LOGI("%s: CPU OF_Sync Call", __func__);
      res = cvpOpticalFlow_Sync(Init_Handle, &pRefImage, &pCurImage,
                                pNewRef, pNewCur, pOutput);// CVP Sync call
      if (res != CVP_SUCCESS) {
        CVP_LOGE("%s: ERROR! cvpOpticalFlow_Sync Failed", __func__);
        if (res == CVP_EFAIL) {
          CVP_LOGE("%s: General failure", __func__);
        }
        else if (res == CVP_EUNALIGNPARAM) {
          CVP_LOGE("%s: Unaligned pointer parameter", __func__);
        }
        else if (res == CVP_EBADPARAM) {
          CVP_LOGE("%s: Bad parameters", __func__);
        }
        else if (res == CVP_ENORES) {
          CVP_LOGE("%s: Insufficient resources, memory, etc", __func__);
        }
        else if (res == CVP_EFATAL) {
          CVP_LOGE("%s: Fatal error", __func__);
        }
        return res;
      }
      else {
        CVP_LOGI("%s: Optical flow sync successful", __func__);
      }
    }

    // save output motion vector
    if (OutputProcess(frame->buffer) != CVP_SUCCESS) {
      CVP_LOGE("%s: Output motion vector process failed", __func__);
    }

    // DeRegister Reference Image
    cvpDeregisterOpticalFlowImageBuf(Init_Handle, &pRefImage);
    cvpDeregisterOpticalFlowImageBuf(Init_Handle, &pCurImage);

    memset (pRefImage.pBuffer->pAddress, 0, pRefImage.pBuffer->nSize);
  }

  // swap ref and cur pointer, clean old ref pointer
  is_start = false;

  CVP_LOGI("%s: Before switch, ref image address %p, cur image address %p",
           __func__, pRefImage.pBuffer->pAddress, pCurImage.pBuffer->pAddress);
  cvpImage tmpimage;
  tmpimage.pBuffer = pRefImage.pBuffer;
  pRefImage.pBuffer = pCurImage.pBuffer;
  pCurImage.pBuffer = tmpimage.pBuffer;
  CVP_LOGI("%s: After switch, ref image address %p, cur image address %p",
           __func__, pRefImage.pBuffer->pAddress, pCurImage.pBuffer->pAddress);

  CVP_LOGI("%s: Exit", __func__);
  return res;
}

/* Allocate buffer
 * 1. Output buffer and stats buffer
 * 2. Image buffer - only allocate ref is there is not already one
 */
int32_t OFEngine::AllocateBuffer() {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  cvpMemSecureType allocMemType = CVP_MEM_NON_SECURE;

  if (pSessH == nullptr) {
    CVP_LOGE("%s: Session handle", __func__);
  }

  // allocate output buffer
  if (CVP_SUCCESS != cvpMemAlloc(pSessH, pOutMemReq.nMotionVectorBytes,
      allocMemType, &pOutput[0].pMotionVector)) {
    CVP_LOGE("%s: Failed to allocate output buffer", __func__);
    return CVP_FAIL;
  }
  CVP_LOGI("%s: Allocate output buffer", __func__);

  if (pConfig.bStatsEnable)
  {
    if (CVP_SUCCESS != cvpMemAlloc(pSessH, pOutMemReq.nStatsBytes,
        allocMemType, &pOutput[0].pStats)) {
      CVP_LOGE("%s: Failed to allocate stats buffer", __func__);
      return CVP_FAIL;
    }
    CVP_LOGI("%s: Allocate stats buffer", __func__);
  }

  // allocate image buffer
  int numbytes = 0;

  //Ref Image
  pRefImage.sImageInfo.nWidth = pConfig.sImageInfo.nWidth;
  pRefImage.sImageInfo.nHeight = pConfig.sImageInfo.nHeight;
  pRefImage.sImageInfo.eFormat = pConfig.sImageInfo.eFormat;
  pRefImage.sImageInfo.nPlane = pConfig.sImageInfo.nPlane;
  pRefImage.sImageInfo.nTotalSize = pConfig.sImageInfo.nTotalSize;
  for (unsigned int i = 0; i < pConfig.sImageInfo.nPlane; i++) {
    pRefImage.sImageInfo.nWidthStride[i] = pConfig.sImageInfo.nWidthStride[i];
    pRefImage.sImageInfo.nAlignedSize[i] = pConfig.sImageInfo.nAlignedSize[i];
    numbytes += pRefImage.sImageInfo.nAlignedSize[i];
  }
  cvpMemAlloc(pSessH, numbytes, allocMemType, &pRefImage.pBuffer);
  CVP_LOGI("%s: Allocate %d of bytes for ref image buffer",
           __func__, numbytes);
  buffersize = numbytes;

  //Cur Image
  numbytes = 0;
  pCurImage.sImageInfo.nWidth = pConfig.sImageInfo.nWidth;
  pCurImage.sImageInfo.nHeight = pConfig.sImageInfo.nHeight;
  pCurImage.sImageInfo.eFormat = pConfig.sImageInfo.eFormat;
  pCurImage.sImageInfo.nPlane = pConfig.sImageInfo.nPlane;
  pCurImage.sImageInfo.nTotalSize = pConfig.sImageInfo.nTotalSize;
  for (unsigned int i = 0; i < pConfig.sImageInfo.nPlane; i++) {
    pCurImage.sImageInfo.nWidthStride[i] = pConfig.sImageInfo.nWidthStride[i];
    pCurImage.sImageInfo.nAlignedSize[i] = pConfig.sImageInfo.nAlignedSize[i];
    numbytes += pCurImage.sImageInfo.nAlignedSize[i];
  }
  cvpMemAlloc(pSessH, numbytes, allocMemType, &pCurImage.pBuffer);
  CVP_LOGI("%s: Allocate %d of bytes for cur image buffer",
           __func__, numbytes);
  if (numbytes != buffersize) {
    CVP_LOGE("%s: RefImage buffer size %d is different from CurImage buffer size %d",
             __func__, buffersize, numbytes);
    return CVP_FAIL;
  }

  return res;
}

/* Free buffer
 * 1. Free output and stats buffer
 * 2. Free image buffer
 *      if keep_ref is set, keep a ref image buffer
 *      set cur image buffer to ref
 *      free original ref image buffer
 *      else, free both
 */
int32_t OFEngine::FreeBuffer() {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  // set output buffer to 0
  memset(pOutput[0].pMotionVector->pAddress, 0, pOutMemReq.nMotionVectorBytes);
  if (pConfig.bStatsEnable == true) {
    memset(pOutput[0].pStats->pAddress, 0, pOutMemReq.nStatsBytes);
  }

  // Free output Buffers
  cvpMemFree(Init_Handle, pOutput[0].pMotionVector);
  CVP_LOGI("%s: Free output buffer", __func__);
  if (pConfig.bStatsEnable == true) {
    cvpMemFree(Init_Handle, pOutput[0].pStats);
    CVP_LOGI("%s: Free stats buffer", __func__);
  }

  // free image buffer
  cvpMemFree(pSessH, pRefImage.pBuffer);
  cvpMemFree(pSessH, pCurImage.pBuffer);

  CVP_LOGI("%s: Exit", __func__);
  return res;
}

/* Output Process
 * If output location is provided, save output MV to file
 * Save in format of x, y
 *
 * Save motion vector to gst buffer
 */
int32_t OFEngine::OutputProcess(GstBuffer* buffer) {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  // save to file
  FILE * mv_output;
  if (config_.output_location != nullptr) {
    CVP_LOGI("%s: Output motion vector printed to %s", __func__,
             config_.output_location);
    char output_filename [200];
    int n = sprintf_s (output_filename, "%s/mvframe%d.txt",
                     config_.output_location, frameid);
    if (n == 0) {
      CVP_LOGE("%s: Error in opening output file", __func__);
      return CVP_FAIL;
    }
    frameid++;
    CVP_LOGI("%s: Output file name %s", __func__, output_filename);
    mv_output = fopen(output_filename, "w");
  }

  int mv_size = pOutput[0].nMVSize;
  cvpMotionVector* pVector =
      (cvpMotionVector*)(pOutput[0].pMotionVector->pAddress);
  cvpOFStats* pStats = NULL;
  if (config_.stats_enable) {
    pStats = (cvpOFStats*)(pOutput[0].pStats->pAddress);
    if (pOutput[0].nMVSize != pOutput[0].nStatsSize) {
      CVP_LOGE("%s: Size of MV buffer and stats buffer mismatch", __func__);
      return CVP_FAIL;
    }
  }
  CVP_LOGI("%s: Output[0] mv size: %d, stats size: %d", __func__,
           mv_size, pOutput[0].nStatsSize);

  for (int i = 0; i < mv_size; i++) {
    if (config_.output_location != nullptr) {
      // print to file
      fprintf(mv_output, "%d %d\n", pVector[i].nMVX_L0, pVector[i].nMVY_L0);
    }

    // save to gst output buffer
    GstCVPOFMeta *meta = gst_buffer_add_optclflow_meta(buffer);
    if (!meta) {
      CVP_LOGE("Failed to create metadata");
      return CVP_NULLPTR;
    }

    meta->mv.x = pVector[i].nMVX_L0;
    meta->mv.y = pVector[i].nMVY_L0;
    meta->mv.conf = pVector[i].nConf;

    // if stats is enable
    if (config_.stats_enable) {
      meta->mv.variance = pStats[i].nVariance;
      meta->mv.mean     = pStats[i].nMean;
      meta->mv.bestSAD  = pStats[i].nBestMVSad;
      meta->mv.SAD      = pStats[i].nSad;
    }
  }

  if (config_.output_location != nullptr) {
    fclose(mv_output);
  }
  else {
    CVP_LOGI("%s: Output motion vector not saved", __func__);
  }

  // clean output buffer
  memset(pOutput[0].pMotionVector->pAddress, 0, pOutMemReq.nMotionVectorBytes);
  if (pConfig.bStatsEnable == true) {
    memset(pOutput[0].pStats->pAddress, 0, pOutMemReq.nStatsBytes);
  }

  CVP_LOGI("%s: Exit", __func__);
  return res;
}

}; // namespace cvp
