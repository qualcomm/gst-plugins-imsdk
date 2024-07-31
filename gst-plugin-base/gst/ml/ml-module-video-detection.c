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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "ml-module-video-detection.h"

// Non-maximum Suppression (NMS) threshold (50%), corresponding to 2/3 overlap.
#define NMS_INTERSECTION_THRESHOLD 0.5F

void
gst_ml_box_entry_cleanup (GstMLBoxEntry * entry)
{
  if (entry->landmarks != NULL)
    g_array_free (entry->landmarks, TRUE);
}

void
gst_ml_box_prediction_cleanup (GstMLBoxPrediction * prediction)
{
  g_array_set_clear_func (prediction->entries,
      (GDestroyNotify) gst_ml_box_entry_cleanup);

  if (prediction->entries != NULL)
    g_array_free (prediction->entries, TRUE);
}

gint
gst_ml_box_compare_entries (const GstMLBoxEntry * l_entry,
    const GstMLBoxEntry * r_entry)
{
  if (l_entry->confidence > r_entry->confidence)
    return -1;
  else if (l_entry->confidence < r_entry->confidence)
    return 1;

  return 0;
}

void
gst_ml_box_relative_translation (GstMLBoxEntry * box, gint width, gint height)
{
  box->top /= height;
  box->bottom /= height;
  box->left /= width;
  box->right /= width;
}

void
gst_ml_box_transform_dimensions (GstMLBoxEntry * box, GstVideoRectangle * region)
{
  box->top = (box->top - region->y) / region->h;
  box->bottom = (box->bottom - region->y) / region->h;
  box->left = (box->left - region->x) / region->w;
  box->right = (box->right - region->x) / region->w;
}

gfloat
gst_ml_boxes_intersection_score (GstMLBoxEntry * l_box, GstMLBoxEntry * r_box)
{
  gfloat width = 0, height = 0, intersection = 0, l_area = 0, r_area = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN (l_box->right, r_box->right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_box->left, r_box->left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_box->bottom, r_box->bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_box->top, r_box->top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  intersection = width * height;

  // Calculate the area of the 2 objects.
  l_area = (l_box->right - l_box->left) * (l_box->bottom - l_box->top);
  r_area = (r_box->right - r_box->left) * (r_box->bottom - r_box->top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

gint
gst_ml_box_non_max_suppression (GstMLBoxEntry * l_box, GArray * boxes)
{
  GstMLBoxEntry *r_box = NULL;
  gdouble score = 0.0;
  guint idx = 0;

  for (idx = 0; idx < boxes->len;  idx++) {
    r_box = &(g_array_index (boxes, GstMLBoxEntry, idx));

    // If labels do not match, continue with next list entry.
    if (l_box->name != r_box->name)
      continue;

    score = gst_ml_boxes_intersection_score (l_box, r_box);

    // If the score is below the threshold, continue with next list entry.
    if (score <= NMS_INTERSECTION_THRESHOLD)
      continue;

    // If confidence of current box is higher, remove the old entry.
    if (l_box->confidence > r_box->confidence)
      return idx;

    // If confidence of current box is lower, don't add it to the list.
    if (l_box->confidence <= r_box->confidence)
      return -2;
  }

  // If this point is reached then add current box to the list;
  return -1;
}

gboolean
gst_ml_module_video_detection_execute (GstMLModule * module,
    GstMLFrame * mlframe, GArray * predictions)
{
  return gst_ml_module_execute (module, mlframe, (gpointer) predictions);
}
