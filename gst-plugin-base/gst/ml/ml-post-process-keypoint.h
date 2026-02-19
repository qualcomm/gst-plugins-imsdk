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

#ifndef __GST_QTI_ML_POST_PROCESS_KEYPOINT_H__
#define __GST_QTI_ML_POST_PROCESS_KEYPOINT_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstMLKeypoint GstMLKeypoint;
typedef struct _GstMLKeypointLink GstMLKeypointLink;

#define GST_TYPE_ML_KEYPOINT            (gst_ml_keypoint_get_type ())
GST_API GType gst_ml_keypoint_get_type  (void);

#define GST_TYPE_ML_KEYPOINT_LINK            (gst_ml_keypoint_link_get_type ())
GST_API GType gst_ml_keypoint_link_get_type  (void);

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
 * GstMLKeypointLink:
 * @l_kp: Left (or First) #GstMLKeypoint entry.
 * @r_kp: Right (or Second) #GstMLKeypoint entry.
 * @color: Optional color that is associated with this link.
 *
 * Information describing a link between two keypoints.
 */
struct _GstMLKeypointLink {
  GstMLKeypoint l_kp;
  GstMLKeypoint r_kp;

  guint         color;
};

/**
 * gst_ml_keypoint_copy:
 * @keypoint: A #GstMLKeypoint
 *
 * Copy a GstMLKeypoint structure.
 *
 * Returns: (transfer full): a new #GstMLKeypoint.
 */
GST_API GstMLKeypoint *
gst_ml_keypoint_copy (const GstMLKeypoint * keypoint);

/**
 * gst_ml_keypoint_free:
 * @keypoint: A #GstMLKeypoint
 *
 * Free a GstMLKeypoint structure previously allocated with
 * gst_ml_keypoint_copy().
 */
GST_API void
gst_ml_keypoint_free (GstMLKeypoint * keypoint);

/**
 * gst_ml_keypoint_relative_transform:
 * @keypoint: A #GstMLKeypoint
 * @region: Region in the tensor containg the actual data.
 * @clamp: Whether to clamp values outside the region dimensions.
 *
 * Helper function for adjusting ML keypoint dimensions to within the region
 * which actually contains data and transforming them to relative.
 */
GST_API void
gst_ml_keypoint_relative_transform (GstMLKeypoint * keypoint,
                                    const GstVideoRectangle * region,
                                    const gboolean clamp);

/**
 * gst_ml_keypoint_affine_transform:
 * @keypoint: A #GstMLKeypoint
 * @matrix: (array fixed-size=9) (element-type gdouble):
 *          A 3x3 affine transformation matrix.
 *
 * Helper function for adjusting coordinates of a ML keypoint with affine matrix.
 */
GST_API void
gst_ml_keypoint_affine_transform (GstMLKeypoint * keypoint, gdouble matrix[3][3]);

/**
 * gst_ml_keypoint_link_copy:
 * @link: A #GstMLKeypointLink
 *
 * Copy a GstMLKeypointLink structure.
 *
 * Returns: (transfer full): a new #GstMLKeypointLink.
 */
GST_API GstMLKeypointLink *
gst_ml_keypoint_link_copy (const GstMLKeypointLink * link);

/**
 * gst_ml_keypoint_link_free:
 * @link: A #GstMLKeypointLink
 *
 * Free a GstMLKeypointLink structure previously allocated with
 * gst_ml_keypoint_link_copy().
 */
GST_API void
gst_ml_keypoint_link_free (GstMLKeypointLink * link);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_KEYPOINT_H__
