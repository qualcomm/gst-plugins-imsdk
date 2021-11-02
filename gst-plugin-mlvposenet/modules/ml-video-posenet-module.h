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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYright OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ​​​​​Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QTI_ML_VIDEO_POSENET_MODULE_H__
#define __GST_QTI_ML_VIDEO_POSENET_MODULE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

// Must match the keypoints of the posename models
enum PosenetKeypointId
{
  POSENET_KP_NOSE = 0,
  POSENET_KP_LEFT_EYE,
  POSENET_KP_RIGHT_EYE,
  POSENET_KP_LEFT_EAR,
  POSENET_KP_RIGHT_EAR,
  POSENET_KP_LEFT_SHOULDER,
  POSENET_KP_RIGHT_SHOULDER,
  POSENET_KP_LEFT_ELBOW,
  POSENET_KP_RIGHT_ELBOW,
  POSENET_KP_LEFT_WRIST,
  POSENET_KP_RIGHT_WRIST,
  POSENET_KP_LEFT_HIP,
  POSENET_KP_RIGHT_HIP,
  POSENET_KP_LEFT_KNEE,
  POSENET_KP_RIGHT_KNEE,
  POSENET_KP_LEFT_ANKLE,
  POSENET_KP_RIGHT_ANKLE,

  POSENET_KP_COUNT
};

typedef struct _GstPoseKeypoint GstPoseKeypoint;
typedef struct _GstPose GstPose;

/**
 * GstPoseKeypoint:
 * @score: score for the keypoint
 * @x: X coordinate of the keypoint
 * @y: Y coordinate of the keypoint
 */
struct _GstPoseKeypoint
{
  gfloat score;
  gfloat x;
  gfloat y;
};

/**
 * GstPose:
 * @pose_score: the overall score for the pose
 * @keypoint: the coordinates and score for each keypoints of the pose
 */
struct _GstPose
{
  float pose_score;
  GstPoseKeypoint keypoint[POSENET_KP_COUNT];
};

/**
 * gst_ml_video_posenet_module_init:
 *
 * Initialize instance of the Posenet module.
 *
 * return: pointer to a private module struct on success or NULL on failure
 */
GST_API gpointer gst_ml_video_posenet_module_init ();

/**
 * gst_ml_video_posenet_module_deinit:
 * @instance: pointer to the private module structure
 *
 * Deinitialize the instance of the Posenet module.
 *
 * return: NONE
 */
GST_API void gst_ml_video_posenet_module_deinit (gpointer instance);

/**
 * gst_ml_video_posenet_module_process:
 * @instance: pointer to the private module structure
 * @buffer: buffer containing tensor memory blocks that need processing
 * @poses: linked list of #GstPose
 *
 * Parses incoming buffer containing result tensors from a Posenet model and
 * converts that information into a list of poses.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_video_posenet_module_process (gpointer instance, GstBuffer * buffer,
    GList ** poses);

G_END_DECLS
#endif // __GST_QTI_ML_VIDEO_POSENET_MODULE_H__
