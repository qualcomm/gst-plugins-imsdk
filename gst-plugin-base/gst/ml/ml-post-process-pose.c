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

#include "ml-post-process-pose.h"

/**
 * GstMLPoses:
 * @refcount: thread safe reference counter
 * @entries: (element-type GstMLPose): A #GArray of #GstMLPose
 *
 * Information describing a group of prediction results beloging to the same batch.
 */
struct _GstMLPoses {
  gatomicrefcount refcount;
  GArray          *entries;
};

G_DEFINE_BOXED_TYPE (GstMLPose, gst_ml_pose,
    (GBoxedCopyFunc) gst_ml_pose_copy, (GBoxedFreeFunc) gst_ml_pose_free);

G_DEFINE_BOXED_TYPE (GstMLPoses, gst_ml_poses,
    (GBoxedCopyFunc) gst_ml_poses_ref, (GBoxedFreeFunc) gst_ml_poses_unref);

static gint
gst_ml_poses_compare (const GstMLPose * l_entry, const GstMLPose * r_entry)
{
  if (l_entry->confidence > r_entry->confidence)
    return -1;
  else if (l_entry->confidence < r_entry->confidence)
    return 1;

  return 0;
}

void
gst_ml_pose_reset (GstMLPose * pose)
{
  g_return_if_fail (pose != NULL);

  pose->name = 0;
  pose->confidence = 0.0;

  g_clear_pointer (&pose->keypoints, g_array_unref);
  g_clear_pointer (&pose->links, g_array_unref);
  g_clear_pointer (&pose->xtraparams, gst_structure_free);
}

GstMLPose*
gst_ml_pose_copy (const GstMLPose * pose)
{
  GstMLPose *newpose = NULL;

  g_return_val_if_fail (pose != NULL, NULL);

  newpose = g_slice_new (GstMLPose);

  newpose->name = pose->name;
  newpose->confidence = pose->confidence;

  newpose->keypoints = g_array_copy (pose->keypoints);
  newpose->links = g_array_copy (pose->links);
  newpose->xtraparams = gst_structure_copy (pose->xtraparams);

  return newpose;
}

void
gst_ml_pose_free (GstMLPose * pose)
{
  if (pose == NULL)
    return;

  gst_ml_pose_reset (pose);
  g_slice_free (GstMLPose, pose);
}

void
gst_ml_pose_relative_transform (GstMLPose * pose,
    const GstVideoRectangle * region, const gboolean clamp)
{
  guint idx = 0, length = 0;

  g_return_if_fail (pose != NULL);
  g_return_if_fail (region != NULL);

  length = pose->keypoints ? pose->keypoints->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstMLKeypoint *kp = &(g_array_index (pose->keypoints, GstMLKeypoint, idx));
    gst_ml_keypoint_relative_transform (kp, region, clamp);
  }

  length = pose->links ? pose->links->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstMLKeypointLink *link = &(g_array_index (pose->links, GstMLKeypointLink, idx));

    gst_ml_keypoint_relative_transform (&(link->l_kp), region, clamp);
    gst_ml_keypoint_relative_transform (&(link->r_kp), region, clamp);
  }
}

void
gst_ml_pose_affine_transform (GstMLPose * pose, gdouble matrix[3][3])
{
  guint idx = 0, length = 0;

  g_return_if_fail (pose != NULL);

  length = pose->keypoints ? pose->keypoints->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstMLKeypoint *kp = &(g_array_index (pose->keypoints, GstMLKeypoint, idx));
    gst_ml_keypoint_affine_transform (kp, matrix);
  }

  length = pose->links ? pose->links->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstMLKeypointLink *link =
        &(g_array_index (pose->links, GstMLKeypointLink, idx));

    gst_ml_keypoint_affine_transform (&(link->l_kp), matrix);
    gst_ml_keypoint_affine_transform (&(link->r_kp), matrix);
  }
}

GstMLPoses*
gst_ml_poses_new (void)
{
  GstMLPoses *poses = g_slice_new (GstMLPoses);

  g_atomic_ref_count_init (&poses->refcount);
  poses->entries = g_array_new (FALSE, TRUE, sizeof (GstMLPose));

  g_array_set_clear_func (poses->entries, (GDestroyNotify) gst_ml_pose_reset);

  return poses;
}

GstMLPoses*
gst_ml_poses_new_sized (guint size)
{
  GstMLPoses *poses = g_slice_new (GstMLPoses);

  g_atomic_ref_count_init (&poses->refcount);
  poses->entries = g_array_sized_new (FALSE, TRUE, sizeof (GstMLPose), size);

  g_array_set_clear_func (poses->entries, (GDestroyNotify) gst_ml_pose_reset);
  g_array_set_size (poses->entries, size);

  return poses;
}

GstMLPoses*
gst_ml_poses_ref (GstMLPoses * poses)
{
  g_return_val_if_fail (poses != NULL, NULL);

  g_atomic_ref_count_inc (&poses->refcount);
  return poses;
}

void
gst_ml_poses_unref (GstMLPoses * poses)
{
  g_return_if_fail (poses != NULL);

  if (g_atomic_ref_count_dec (&poses->refcount)) {
    g_array_free (poses->entries, TRUE);
    g_slice_free (GstMLPoses, poses);
  }
}

GstMLPoses *
gst_ml_poses_copy (const GstMLPoses * poses)
{
  GstMLPoses *newposes = NULL;

  g_return_val_if_fail (poses != NULL, NULL);

  newposes = g_slice_new (GstMLPoses);
  newposes->entries = g_array_copy (poses->entries);
  g_atomic_ref_count_init (&newposes->refcount);

  return newposes;
}

void
gst_ml_poses_append (GstMLPoses * poses, const GstMLPose * pose)
{
  g_return_if_fail (poses != NULL);
  g_array_append_vals (poses->entries, pose, 1);
}

void
gst_ml_poses_insert (GstMLPoses * poses, guint index, const GstMLPose * pose)
{
  g_return_if_fail (poses != NULL);
  g_array_insert_vals (poses->entries, index, pose, 1);
}

void
gst_ml_poses_remove (GstMLPoses * poses, guint index)
{
  g_return_if_fail (poses != NULL);
  g_array_remove_index (poses->entries, index);
}

GstMLPose*
gst_ml_poses_entry (GstMLPoses * poses, guint index)
{
  g_return_val_if_fail (poses != NULL, NULL);
  return &(g_array_index (poses->entries, GstMLPose, index));
}

guint
gst_ml_poses_size (GstMLPoses * poses)
{
  g_return_val_if_fail (poses != NULL, 0);
  return poses->entries->len;
}

void
gst_ml_poses_resize (GstMLPoses * poses, guint size)
{
  g_return_if_fail (poses != NULL);
  g_array_set_size (poses->entries, size);
}

void
gst_ml_poses_sort (GstMLPoses * poses)
{
  g_return_if_fail (poses != NULL);
  g_array_sort (poses->entries, (GCompareFunc) gst_ml_poses_compare);
}
