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

#ifndef POSENETENGINE_H
#define POSENETENGINE_H

#include "nnengine.h"

static const int TotalKeypointNum   = 17;
static const int MaxKeypointNameLen = 20;

static const int          MaxNumDetect = 64;
static const int      PoseMaxNumDetect = 20;
static const int PoseMaxNumScoredParts = 250;
static const int    PoseFeatureMapSize = 1271;

static inline int max(int a, int b) { return ((a > b) ? a : b); }
static inline int min(int a, int b) { return ((a < b) ? a : b); }

static char PartNames[TotalKeypointNum][MaxKeypointNameLen] =
{
  "nose", "leftEye", "rightEye", "leftEar", "rightEar", "leftShoulder",
  "rightShoulder", "leftElbow", "rightElbow", "leftWrist", "rightWrist",
  "leftHip", "rightHip", "leftKnee", "rightKnee", "leftAnkle", "rightAnkle"
};

static char PoseChain[32][MaxKeypointNameLen] =
{
  "nose", "leftEye", "leftEye", "leftEar", "nose", "rightEye", "rightEye",
  "rightEar", "nose", "leftShoulder","leftShoulder", "leftElbow",
  "leftElbow", "leftWrist", "leftShoulder", "leftHip", "leftHip", "leftKnee",
  "leftKnee", "leftAnkle", "nose", "rightShoulder", "rightShoulder",
  "rightElbow", "rightElbow", "rightWrist", "rightShoulder", "rightHip",
  "rightHip", "rightKnee", "rightKnee", "rightAnkle"
};

typedef enum {
  Float,
  Int32,
  Uint8,
  Invalid
} TensorDataType;

typedef struct
{
  float val;
  int   index;
} Score;

typedef struct
{
  int   outputStride;
  int   maxPoseDetections;
  float minPoseScore;
  float heatmapScoreThreshold;
  int   nmsRadius;
  int   featureHeight;
  int   featureWidth;
  int   numKeypoint;
  int   localMaximumRadius;
} PosePPConfig;

typedef struct
{
  float poseScore;
  float keypointScore[TotalKeypointNum];
  float keypointCoord[TotalKeypointNum * 2];
} PoseResult;

typedef struct
{
  float partScore;
  int   keypointId;
  int   coord[2];
} Part;

typedef struct
{
  float partScore;
  int   keypointId;
  float coord[2];
} PartWithFloatCoord;

typedef struct
{
  int  pId;
  char pName[MaxKeypointNameLen];
} PartId;

typedef struct
{
  int parent;
  int child;
} ParentChildTurple;

typedef struct
{
  TensorDataType outputType;
  void* pOuts;
  float min = 0.0;
  float max = 0.0;
} PPOutputElement;

class PoseNetEngine : public NNEngine {
public:

  PoseNetEngine ()
          : NNEngine(kModelLib, kPadWidth, kPadHeight, kNumOutputs, kInFormat),
            scale_back_x_(1.0),
            scale_back_y_(1.0),
            pose_count_(0)
            {};

  int32_t Init(const NNSourceInfo* pSourceInfo) override;

  void DeInit() override;

private:

  int32_t PostProcess(void* outputs[], GstBuffer * gst_buffer) override;

  int32_t FillMLMeta(GstBuffer * gst_buffer) override;

  static int SortPartScore(
      const void* pVal1,
      const void* pVal2);

  void DoHeatmapNormalize(
      float* pRawScores,
      int    rawScoresSize);

  void FindMaximumFilterForVector(
      PosePPConfig* pPosePPConfig,
      float*        pKpScoresRow,
      float*        pMaxValsRow);

  void FindMaximumFilterForMatrix(
      PosePPConfig* pPosePPConfig,
      float*        pKpScores,
      float*        pFilteredKpScores);

  int SelectKeypointWithScore(
      PosePPConfig* pPosePPConfig,
      float*        pRawScores,
      Part*         pParts);

  void ReshapeLastTwoDimensions(
      PosePPConfig* pPosePPConfig,
      float*        pRawOffsets,
      float*        pReshapedOffsets);

  void ReshapeDisplacements(
      PosePPConfig* pPosePPConfig,
      float*        pRawDisplacements,
      float*        pReshapedDisplacementsBwd,
      float*        pReshapedDisplacementsFwd);

  int DoNMSPose(
      PoseResult* pPoseResults,
      int         poseCount,
      int         rootId,
      int         squaredNmsRadius,
      float       curPoint[2],
      int         numKeypoints);

  void GeneratePartIds(
      char*   PartNames,
      PartId* pPartIds,
      int     numKeypoint,
      int     partNameLen);

  void GenerateParentChildTuples(
      char*              PoseChain,
      ParentChildTurple* pParentChildTurples,
      PartId*            pPartIds,
      int                numKeypoint,
      int                partNameLen);

  void PropagateFromSourceToTargetKeypoint(
      int                 edgeId,
      float*              pKeypointCoords,
      int                 sourceKeypointId,
      int                 targetKeypointId,
      float*              pScores,
      float*              pOffsets,
      float*              pDisplacements,
      PosePPConfig*       pPosePPConfig,
      PartWithFloatCoord* pCurPart);

  void DecodePose(
      float              rootScore,
      int                rootId,
      float              rootImageCoords[2],
      float*             pRawHeatmaps,
      float*             pOffsets,
      float*             pDisplacementsBwd,
      float*             pDisplacementsFwd,
      PosePPConfig*      pPosePPConfig,
      PoseResult*        pPoseResult,
      ParentChildTurple* pParentChildTurples);

  int32_t CalculatePoseInstanceScore(
      PoseResult*   pPoseResults,
      int           poseCount,
      int           squaredNmsRadius,
      PoseResult*   pCurPoseResult,
      PosePPConfig* pPosePPConfig,
      float         &notOverlappedScores
      );

  static const std::string    kModelLib;

  // input
  static const NNImgFormat    kInFormat         = NN_FORMAT_RGB24;
  static const uint32_t       kPadWidth         = 641;
  static const uint32_t       kPadHeight        = 481;

  // output
  static const uint32_t       kNumOutputs       = 3;
  static const uint32_t       kOutBytesPerPixel = 4;
  static const uint32_t       kHeatMapSize      = 41 * 31 * 17;
  static const uint32_t       kOffsetSize       = 41 * 31 * 34;
  static const uint32_t       kDisplacementSize = 41 * 31 * 64;

  // post-process and local copy of inference result
  float                       scale_back_x_;
  float                       scale_back_y_;
  PosePPConfig                pose_pp_config_;
  PoseResult                  pose_results_[PoseMaxNumDetect];
  int                         pose_count_;
  std::mutex                  lock_;

  float* pOffsets;
  float* pDisplacementsFwd;
  float* pDisplacementsBwd;
};

#endif // POSENETENGINE_H
