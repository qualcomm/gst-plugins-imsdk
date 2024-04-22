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
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QTI_ML_MODULE_VIDEO_POSE_H__
#define __GST_QTI_ML_MODULE_VIDEO_POSE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/gstmlmodule.h>

G_BEGIN_DECLS

typedef struct _GstMLKeypoint GstMLKeypoint;
typedef struct _GstMLKeypointsLink GstMLKeypointsLink;
typedef struct _GstMLPosePrediction GstMLPosePrediction;

/**
 * GstMLKeypoint:
 * @name: Name of the keypoint.
 * @confidence: Confidence score for this keypoint.
 * @color: Optional color of the keypoint.
 * @x: X axis coordinate of the keypoint.
 * @y: Y axis coordinate of the keypoint.
 *
 * Information describing keypoint location and confidence score.
 *
 * The fields x and y must be set in (0.0 to 1.0) relative coordinate system.
 */
struct _GstMLKeypoint {
  GQuark name;
  gfloat confidence;

  guint  color;

  gfloat x;
  gfloat y;
};

/**
 * GstMLKeypointsLink:
 * @s_kp_idx: ID of the source keypoint.
 * @d_kp_idx: ID of the destination keypoint.
 *
 * Information describing a link between two keypoints.
 */
struct _GstMLKeypointsLink {
  guint s_kp_id;
  guint d_kp_id;
};

/**
 * GstMLPosePrediction:
 * @confidence: The overall confidence for the estimated pose.
 * @keypoints: List of #GstMLKeypoint.
 * @connections: List of #GstMLKeypointsLink.
 *
 * Information describing prediction result from pose estimation models.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct _GstMLPosePrediction {
  gdouble confidence;

  GArray  *keypoints;
  GArray  *connections;
};

/**
 * gst_ml_module_video_pose_execute:
 * @module: Pointer to ML post-processing module.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @predictions: GArray of #GstMLPosePrediction.
 *
 * Convenient wrapper function used on plugin level to call the module
 * 'gst_ml_module_process' API via 'gst_ml_module_execute' wrapper in order
 * to process input tensors.
 *
 * Post-processing module must define the 3rd argument of the implemented
 * 'gst_ml_module_process' API as 'GArray *'.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_video_pose_execute (GstMLModule * module, GstMLFrame * mlframe,
                                  GArray * predictions);

/**
 * gst_ml_load_links:
 * @list: GValue list containing link information.
 * @idx: Seed index from which to start.
 * @links: Array to be filled.
 *
 * Helper recursive function to load the skeleton chain/tree starting from
 * GValue list with seed index provided by user into array comprised by
 * #GstMLKeypointsLink.
 *
 * return: TRUE on success or FALSE on failure
 */
gboolean
gst_ml_load_links (const GValue * list, const guint idx, GArray * links);

/**
 * gst_ml_load_connections:
 * @list: GValue list containing label information.
 * @connections: Array to be filled.
 *
 * Helper function to load the keypoint pairs/links from GValue list into
 * array comprised by #GstMLKeypointsLink.
 *
 * return: TRUE on success or FALSE on failure
 */
gboolean
gst_ml_load_connections (const GValue * list, GArray * connections);

/**
 * gst_ml_keypoint_transform_coordinates:
 * @keypoint: Pointer to the ML keypoint which will be modified.
 * @region: Region in the tensor containg the actual data.
 *
 * Helper function for adjusting ML keypoint dimensions to within the region
 * which actually contains data and transforming them to relative.
 *
 * return: None
 */
void
gst_ml_keypoint_transform_coordinates (GstMLKeypoint * keypoint,
                                      GstVideoRectangle * region);

G_END_DECLS

#endif // __GST_QTI_ML_MODULE_VIDEO_POSE_H__
