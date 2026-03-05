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

#ifndef __GST_QTI_ML_POST_PROCESS_POSE_H__
#define __GST_QTI_ML_POST_PROCESS_POSE_H__

#include <gst/gst.h>
#include <gst/ml/ml-post-process-keypoint.h>

G_BEGIN_DECLS

typedef struct _GstMLPose GstMLPose;
typedef struct _GstMLPoses GstMLPoses;

#define GST_TYPE_ML_POSE            (gst_ml_pose_get_type ())
GST_API GType gst_ml_pose_get_type  (void);

#define GST_TYPE_ML_POSES           (gst_ml_poses_get_type ())
GST_API GType gst_ml_poses_get_type (void);

/**
 * GstMLPose:
 * @name: Name of the prediction.
 * @confidence: The overall confidence for the estimated pose.
 * @keypoints: (element-type GstMLKeypoint) (transfer full):
 *             A #GArray of GstMLKeypoint
 * @links: (optional) (element-type GstMLKeypoint) (transfer full):
 *         A #GArray of GstMLKeypointLink
 * @xtraparams: (optional): A #GstStructure with custom parameters. The names
 *              for the those parameters inside the #GstStructure must be lower
 *              case with dash ('-') for whitepace e.g. "param-example-name".
 *              The following parameter names are forbidden: 'confidence',
 *              'keypoints' and 'links'. The name given to the structure
 *              on creation must use the reserved naming "ExtraParams".
 *
 * Information describing prediction result from pose estimation models.
 */
struct _GstMLPose {
  GQuark       name;
  gfloat       confidence;

  GArray       *keypoints;
  GArray       *links;

  GstStructure *xtraparams;
};

/**
 * gst_ml_pose_pose_cleanup:
 * @pose: Pointer to the ML pose pose.
 *
 * Helper function for freeing any allocated resources owned by the entry.
 */
GST_API void
gst_ml_pose_reset (GstMLPose * pose);

/**
 * gst_ml_pose_copy:
 * @pose: A #GstMLPose
 *
 * Copy a GstMLPose structure.
 *
 * Returns: (transfer full): a new #GstMLPose.
 */
GST_API GstMLPose *
gst_ml_pose_copy (const GstMLPose * pose);

/**
 * gst_ml_pose_free:
 * @pose: A #GstMLPose
 *
 * Free a GstMLPose structure previously allocated with gst_ml_pose_copy().
 */
GST_API void
gst_ml_pose_free (GstMLPose * pose);

/**
 * gst_ml_pose_relative_transform:
 * @pose: A #GstMLPose
 * @region: Region in the tensor containg the actual data.
 * @clamp: Whether to clamp values outside the region dimensions.
 *
 * Helper function for adjusting ML pose coordinates to within the region
 * which actually contains data and transforming them to relative.
 */
GST_API void
gst_ml_pose_relative_transform (GstMLPose * pose,
                                const GstVideoRectangle * region,
                                const gboolean clamp);

/**
 * gst_ml_pose_affine_transform:
 * @pose: A #GstMLPose
 * @matrix: (array fixed-size=9) (element-type gdouble):
 *          A 3x3 affine transformation matrix.
 *
 * Helper function for adjusting coordinates of a ML pose with affine matrix.
 */
GST_API void
gst_ml_pose_affine_transform (GstMLPose * pose, gdouble matrix[3][3]);

/**
 * gst_ml_poses_new: (constructor)
 *
 * Allocate a new #GstMLPoses that is also initialized.
 *
 * Returns: (transfer full): a new #GstMLPoses.
 */
GST_API GstMLPoses*
gst_ml_poses_new (void);

/**
 * gst_ml_poses_new_sized: (constructor)
 * @size: number of elements preallocated
 *
 * Allocate a new #GstMLPoses with @size elements preallocated.
 *
 * Returns: (transfer full): a new #GstMLPoses.
 */

GST_API GstMLPoses*
gst_ml_poses_new_sized (guint size);

/**
 * gst_ml_poses_ref: (skip)
 * @poses: (transfer none): A #GstMLPoses
 *
 * Atomically increments the reference count of @poses by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer none): A pointer to the object passed in @poses
 */
GST_API GstMLPoses*
gst_ml_poses_ref (GstMLPoses * poses);

/**
 * gst_ml_poses_unref: (skip)
 * @poses: (transfer none):  A #GstMLPoses
 *
 * Atomically decrements the reference count of @poses by one. If the
 * reference count drops to 0, the GstMLPoses apreviously allocated with
 * gst_ml_poses_new() or gst_ml_poses_copy() will be deallocated.
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_poses_unref (GstMLPoses * poses);

/**
 * gst_ml_poses_copy:
 * @poses: A #GstMLPoses
 *
 * Copy a GstMLPoses structure.
 *
 * Returns: (transfer full): a new #GstMLPoses.
 */
GST_API GstMLPoses *
gst_ml_poses_copy (const GstMLPoses * poses);

/**
 * gst_ml_poses_append:
 * @poses: A #GstMLPoses
 * @pose: A #GstMLPose
 *
 * Adds the value on to the end of the GstMLPoses list.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_poses_append (GstMLPoses * poses, const GstMLPose * pose);

/**
 * gst_ml_poses_insert:
 * @poses: A #GstMLPoses
 * @index: the index at which to insert the new element
 * @pose: A #GstMLPose
 *
 * Insert element into a GstMLPoses at the given index.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_poses_insert (GstMLPoses * poses, guint index, const GstMLPose * pose);

/**
 * gst_ml_poses_remove:
 * @poses: A #GstMLPoses
 * @index: the index of the element to remove
 *
 * Removes the element at the given index from the poses list.
 * The following elements are moved down one place.
 */
GST_API void
gst_ml_poses_remove (GstMLPoses * poses, guint index);

/**
 * gst_ml_poses_entry:
 * @poses: A #GstMLPoses
 * @index: the index of the element to return
 *
 * Returns: (transfer none): the #GstMLPose at the given index.
 */
GST_API GstMLPose*
gst_ml_poses_entry (GstMLPoses * poses, guint index);

/**
 * gst_ml_poses_size:
 * @poses: A #GstMLPoses
 *
 * Returns: number of elements in A #GstMLPoses
 */
GST_API guint
gst_ml_poses_size (GstMLPoses * poses);

/**
 * gst_ml_poses_resize:
 * @poses: A #GstMLPoses
 * @size: the new size of the GstMLPoses list
 *
 * Sets the size of the array, expanding it if necessary.
 */
GST_API void
gst_ml_poses_resize (GstMLPoses * poses, guint size);

/**
 * gst_ml_poses_sort:
 * @poses: A #GstMLPoses
 *
 * Sort elements in a GstMLPoses list by confidence score.
 */
GST_API void
gst_ml_poses_sort (GstMLPoses * poses);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_POSE_H__
