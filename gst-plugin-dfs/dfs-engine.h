/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __DFS_ENGINE_H__
#define __DFS_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef struct _GstDfsEngine GstDfsEngine;
typedef struct _DfsInitSettings DfsInitSettings;

typedef enum {
  OUTPUT_MODE_VIDEO,
  OUTPUT_MODE_DISPARITY,
  OUTPUT_MODE_POINT_CLOUD,
} OutputMode;

typedef enum {
  MODE_CVP = 0,         //CVP hardware mode
  MODE_COVERAGE,        //CPU solution, speed mode
  MODE_SPEED,           //OpenCL solution, speed mode, fastest mode
  MODE_ACCURACY,        //CPU solution, accuracy mode
} DFSMode;

typedef struct{
  // Image:
  guint32 pixelWidth, pixelHeight;
  // Image Memory:
  guint32 memoryStride;
  guint32 uvOffset;
  // Calibration:
  gdouble principalPoint[2];
  gdouble focalLength[2];
  gdouble distortion[8];
  gint32   distortionModel;
} cameraConfiguration;

typedef struct{
  gfloat translation[3], rotation[3];  // Relative between cameras
  cameraConfiguration camera[2];        // Left/right camera calibrations
  gfloat correctionFactors[4];         // Distance correction
} stereoConfiguration;



struct _DfsInitSettings {
  guint                 stereo_frame_width;
  guint                 stereo_frame_height;
  guint                 stride;
  GstVideoFormat        format;
  gint                  mode;
  DFSMode               dfs_mode;
  gint                  min_disparity;
  guint                 num_disparity_levels;
  gint                  filter_width;
  gint                  filter_height;
  gboolean              rectification;
  gboolean              gpu_rect;
  stereoConfiguration   stereo_parameter;
};

GST_API GstDfsEngine *
gst_dfs_engine_new          (DfsInitSettings  *settings);

GST_API void
gst_dfs_engine_free         (GstDfsEngine *engine);

GST_API gboolean
gst_dfs_engine_execute (GstDfsEngine *engine,
    const GstVideoFrame *inframe, gpointer disparity_map, gsize size);

G_END_DECLS

#endif // __DFS_ENGINE_H_
