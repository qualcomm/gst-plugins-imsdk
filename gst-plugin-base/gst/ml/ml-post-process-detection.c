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

#include "ml-post-process-detection.h"

#include <gst/video/video-utils.h>

/**
 * GstMLDetections:
 * @refcount: thread safe reference counter
 * @entries: (element-type GstMLDetection): A #GArray of #GstMLDetection
 *
 * Information describing a group of prediction results beloging to the same batch.
 */
struct _GstMLDetections {
  gatomicrefcount refcount;
  GArray          *entries;
};

G_DEFINE_BOXED_TYPE (GstMLDetection, gst_ml_detection,
    (GBoxedCopyFunc) gst_ml_detection_copy,
    (GBoxedFreeFunc) gst_ml_detection_free);

G_DEFINE_BOXED_TYPE (GstMLDetections, gst_ml_detections,
    (GBoxedCopyFunc) gst_ml_detections_ref,
    (GBoxedFreeFunc) gst_ml_detections_unref);

static gint
gst_ml_detections_compare (const GstMLDetection * l_detection,
    const GstMLDetection * r_detection)
{
  if (l_detection->confidence > r_detection->confidence)
    return -1;
  else if (l_detection->confidence < r_detection->confidence)
    return 1;

  return 0;
}

void
gst_ml_detection_reset (GstMLDetection * detection)
{
  g_return_if_fail (detection != NULL);

  detection->name = 0;
  detection->confidence = 0.0;
  detection->color = 0;

  detection->top = detection->bottom = 0;
  detection->left = detection->right = 0;

  g_clear_pointer (&detection->landmarks, g_array_unref);
  g_clear_pointer (&detection->xtraparams, gst_structure_free);
}

GstMLDetection*
gst_ml_detection_copy (const GstMLDetection * detection)
{
  GstMLDetection *newdetection = NULL;

  g_return_val_if_fail (detection != NULL, NULL);

  newdetection = g_slice_new (GstMLDetection);

  newdetection->name = detection->name;
  newdetection->confidence = detection->confidence;
  newdetection->color = detection->color;

  newdetection->top = detection->top;
  newdetection->bottom = detection->bottom;
  newdetection->left = detection->left;
  newdetection->right = detection->right;

  newdetection->landmarks = g_array_copy (detection->landmarks);
  newdetection->xtraparams = gst_structure_copy (detection->xtraparams);

  return newdetection;
}

void
gst_ml_detection_free (GstMLDetection * detection)
{
  if (detection == NULL)
    return;

  gst_ml_detection_reset (detection);
  g_slice_free (GstMLDetection, detection);
}

void
gst_ml_detection_relative_transform (GstMLDetection * detection,
    const GstVideoRectangle * region, const gboolean clamp)
{
  guint idx = 0, n_landmarks = 0;

  g_return_if_fail (detection != NULL);
  g_return_if_fail (region != NULL);

  n_landmarks = detection->landmarks ? detection->landmarks->len : 0;

  for (idx = 0; idx < n_landmarks; idx++) {
    GstMLKeypoint *kp = &(g_array_index (detection->landmarks, GstMLKeypoint, idx));

    kp->x = (kp->x - detection->left) / (detection->right - detection->left);
    kp->y = (kp->y - detection->top) / (detection->bottom - detection->top);

    if (!clamp)
      continue;

    kp->x = CLAMP (kp->x, 0.0f, 1.0f);
    kp->y = CLAMP (kp->y, 0.0f, 1.0f);
  }

  detection->top = (detection->top - region->y) / region->h;
  detection->bottom = (detection->bottom - region->y) / region->h;
  detection->left = (detection->left - region->x) / region->w;
  detection->right = (detection->right - region->x) / region->w;

  if (!clamp)
    return;

  detection->left = CLAMP (detection->left, 0.0f, 1.0f);
  detection->top = CLAMP (detection->top, 0.0f, 1.0f);
  detection->right = CLAMP (detection->right, 0.0f, 1.0f);
  detection->bottom = CLAMP (detection->bottom, 0.0f, 1.0f);
}

void
gst_ml_detection_affine_transform (GstMLDetection * detection, gdouble matrix[3][3])
{
  GstVideoPoint a = { 0, }, b = { 0, }, c = { 0, }, d = { 0, };
  guint idx = 0, n_landmarks = 0;

  g_return_if_fail (detection != NULL);

  n_landmarks = detection->landmarks ? detection->landmarks->len : 0;

  for (idx = 0; idx < n_landmarks; idx++) {
    GstMLKeypoint *kp = &(g_array_index (detection->landmarks, GstMLKeypoint, idx));
    gst_ml_keypoint_affine_transform (kp, matrix);
  }

  a = (GstVideoPoint) {detection->left, detection->top};
  b = (GstVideoPoint) {detection->left, detection->bottom};
  c = (GstVideoPoint) {detection->right, detection->top};
  d = (GstVideoPoint) {detection->right, detection->bottom};

  gst_video_point_affine_transform (&a, matrix);
  gst_video_point_affine_transform (&b, matrix);
  gst_video_point_affine_transform (&c, matrix);
  gst_video_point_affine_transform (&d, matrix);

  // Adjusted bounding box so that incoporates the new ABCD quadrilateral.
  detection->left = MIN (MIN (a.x, b.x), MIN (c.x, d.x));
  detection->top = MIN (MIN (a.y, b.y), MIN (c.y, d.y));
  detection->right = MAX (MAX (a.x, b.x), MAX (c.x, d.x));
  detection->bottom = MAX (MAX (a.y, b.y), MAX (c.y, d.y));
}

gfloat
gst_ml_detection_intersection_score (const GstMLDetection * l_detection,
    const GstMLDetection * r_detection)
{
  gfloat width = 0, height = 0, intersection = 0, l_area = 0, r_area = 0;

  g_return_val_if_fail (l_detection != NULL, 0.0f);
  g_return_val_if_fail (r_detection != NULL, 0.0f);

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN (l_detection->right, r_detection->right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_detection->left, r_detection->left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_detection->bottom, r_detection->bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_detection->top, r_detection->top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  intersection = width * height;

  // Calculate the area of the 2 objects.
  l_area = (l_detection->right - l_detection->left) *
      (l_detection->bottom - l_detection->top);
  r_area = (r_detection->right - r_detection->left) *
      (r_detection->bottom - r_detection->top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

GstStructure *
gst_ml_detection_to_structure (GstMLDetection * detection)
{
  GstStructure *structure = NULL;
  gchar *name = NULL;
  GValue array = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0;

  // Replace empty spaces otherwise subsequent stream parse call will fail.
  name = g_strdup (g_quark_to_string (detection->name));
  name = g_strdelimit (name, " ", '.');

  structure = gst_structure_new (name, "confidence", G_TYPE_DOUBLE,
      detection->confidence, "color", G_TYPE_UINT, detection->color, NULL);

  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_FLOAT);

  g_value_set_float (&value, detection->left);
  gst_value_array_append_value (&array, &value);

  g_value_set_float (&value, detection->top);
  gst_value_array_append_value (&array, &value);

  g_value_set_float (&value, (detection->right - detection->left));
  gst_value_array_append_value (&array, &value);

  g_value_set_float (&value, (detection->bottom - detection->top));
  gst_value_array_append_value (&array, &value);

  gst_structure_take_value (structure, "rectangle", &array);
  g_value_unset (&value);

  if ((detection->landmarks != NULL) && (detection->landmarks->len != 0)) {
    g_value_init (&array, GST_TYPE_ARRAY);

    for (idx = 0; idx < detection->landmarks->len; idx++) {
      GstMLKeypoint *keypoint =
          &(g_array_index (detection->landmarks, GstMLKeypoint, idx));

      g_value_init (&value, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&value, gst_ml_keypoint_to_structure (keypoint));

      gst_value_array_append_and_take_value (&array, &value);
    }

    gst_structure_take_value (structure, "landmarks", &array);
  }

  if (detection->xtraparams != NULL) {
    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, g_steal_pointer (&detection->xtraparams));
    gst_structure_take_value (structure, "xtraparams", &value);
  }

  g_free (name);
  gst_ml_detection_reset (detection);

  return structure;
}

GstMLDetections*
gst_ml_detections_new (void)
{
  GstMLDetections *detections = g_slice_new (GstMLDetections);

  g_atomic_ref_count_init (&detections->refcount);
  detections->entries = g_array_new (FALSE, TRUE, sizeof (GstMLDetection));

  g_array_set_clear_func (detections->entries,
      (GDestroyNotify) gst_ml_detection_reset);

  return detections;
}

GstMLDetections*
gst_ml_detections_new_sized (guint size)
{
  GstMLDetections *detections = g_slice_new (GstMLDetections);

  g_atomic_ref_count_init (&detections->refcount);
  detections->entries =
      g_array_sized_new (FALSE, TRUE, sizeof (GstMLDetection), size);

  g_array_set_clear_func (detections->entries,
      (GDestroyNotify) gst_ml_detection_reset);
  g_array_set_size (detections->entries, size);

  return detections;
}

GstMLDetections*
gst_ml_detections_ref (GstMLDetections * detections)
{
  g_return_val_if_fail (detections != NULL, NULL);
  g_atomic_ref_count_inc (&detections->refcount);

  return detections;
}

void
gst_ml_detections_unref (GstMLDetections * detections)
{
  g_return_if_fail (detections != NULL);

  if (g_atomic_ref_count_dec (&detections->refcount)) {
    g_array_free (detections->entries, TRUE);
    g_slice_free (GstMLDetections, detections);
  }
}

GstMLDetections *
gst_ml_detections_copy (const GstMLDetections * detections)
{
  GstMLDetections *newdetections = NULL;

  g_return_val_if_fail (detections != NULL, NULL);

  newdetections = g_slice_new (GstMLDetections);
  newdetections->entries = g_array_copy (detections->entries);
  g_atomic_ref_count_init (&newdetections->refcount);

  return newdetections;
}

void
gst_ml_detections_append (GstMLDetections * detections,
    const GstMLDetection * detection)
{
  g_return_if_fail (detections != NULL);
  g_array_append_vals (detections->entries, detection, 1);
}

void
gst_ml_detections_insert (GstMLDetections * detections, guint index,
    const GstMLDetection * detection)
{
  g_return_if_fail (detections != NULL);
  g_array_insert_vals (detections->entries, index, detection, 1);
}

void
gst_ml_detections_remove (GstMLDetections * detections, guint index)
{
  g_return_if_fail (detections != NULL);
  g_array_remove_index (detections->entries, index);
}

GstMLDetection*
gst_ml_detections_entry (GstMLDetections * detections, guint index)
{
   g_return_val_if_fail (detections != NULL, NULL);
  return &(g_array_index (detections->entries, GstMLDetection, index));
}

guint
gst_ml_detections_size (GstMLDetections * detections)
{
  g_return_val_if_fail (detections != NULL, 0);
  return detections->entries->len;
}

void
gst_ml_detections_resize (GstMLDetections * detections, guint size)
{
  g_return_if_fail (detections != NULL);
  g_array_set_size (detections->entries, size);
}

void
gst_ml_detections_sort (GstMLDetections * detections)
{
  g_return_if_fail (detections != NULL);
  g_array_sort (detections->entries, (GCompareFunc) gst_ml_detections_compare);
}

gint
gst_ml_detections_non_max_suppression (GstMLDetections * detections,
    const GstMLDetection * detection, const gfloat threshold)
{
  GstMLDetection *l_detection = NULL;
  gdouble score = 0.0;
  guint idx = 0;

  g_return_val_if_fail (detections != NULL, -1);
  g_return_val_if_fail (detections != NULL, -1);

  for (idx = 0; idx < detections->entries->len;  idx++) {
    l_detection = &(g_array_index (detections->entries, GstMLDetection, idx));

    // If labels do not match, continue with next list entry.
    if (detection->name != l_detection->name)
      continue;

    score = gst_ml_detection_intersection_score (detection, l_detection);

    // If the score is below the threshold, continue with next list entry.
    if (score <= threshold)
      continue;

    // If confidence of current detection is higher, remove the old entry.
    if (detection->confidence > l_detection->confidence) {
      gst_ml_detection_reset (l_detection);
      memcpy (l_detection, detection, sizeof (GstMLDetection));
      return idx;
    }

    // If confidence of current detection is lower, don't add it to the list.
    if (detection->confidence <= l_detection->confidence)
      return -1;
  }

  // If this point is reached then add current detection to the list;
  g_array_append_vals (detections->entries, detection, 1);

  return (detections->entries->len - 1);
}
