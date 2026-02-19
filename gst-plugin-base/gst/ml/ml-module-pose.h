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
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_MODULE_POSE_H__
#define __GST_QTI_ML_MODULE_POSE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/gstmlmodule.h>
#include <gst/ml/ml-post-process-pose.h>

G_BEGIN_DECLS

/**
 * gst_ml_load_skeleton_links:
 * @links: Array to be filled.
 * @list: GValue list containing link information.
 * @idx: Seed index from which to start.
 *
 * Helper recursive function to load the skeleton chain/tree starting from
 * GValue list with seed index provided by user into array comprised by
 * #GstMLKeypointLink.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_load_skeleton_links (GPtrArray * links, const GValue * list,
                            const guint idx);

/**
 * gst_ml_load_connections:
 * @connections: Array to be filled.
 * @list: GValue list containing label information.
 *
 * Helper function to load the keypoint pairs/links from GValue list into
 * array comprised by #GstMLKeypointLink.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_load_connections (GPtrArray * connections, const GValue * list);

/**
 * gst_ml_module_pose_execute:
 * @module: Pointer to ML post-processing module.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @predictions: A #GPtrArray of #GstMLPoses.
 *
 * Convenient wrapper function used on plugin level to call the module
 * 'gst_ml_module_process' API via 'gst_ml_module_execute' wrapper in order
 * to process input tensors.
 *
 * Post-processing module must define the 3rd argument of the implemented
 * 'gst_ml_module_process' API as 'GArray *'.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_pose_execute (GstMLModule * module, GstMLFrame * mlframe,
                            GPtrArray * predictions);

G_END_DECLS

#endif // __GST_QTI_ML_MODULE_VIDEO_POSE_H__
