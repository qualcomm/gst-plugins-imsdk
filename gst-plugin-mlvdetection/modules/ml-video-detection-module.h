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
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QTI_ML_VIDEO_DETECTION_MODULE_H__
#define __GST_QTI_ML_VIDEO_DETECTION_MODULE_H__

#include <gst/gst.h>
#include <gst/ml/gstmlmodule.h>
#include <gst/ml/ml-module-utils.h>

G_BEGIN_DECLS

// Non-maximum Suppression (NMS) threshold (50%), corresponding to 2/3 overlap.
#define NMS_INTERSECTION_THRESHOLD 0.5F

typedef struct _GstMLPrediction GstMLPrediction;

/**
 * GstMLPrediction:
 * @label: the name of the prediction.
 * @confidence: the percentage certainty that the prediction is accurate.
 * @color: the possible color that is associated with this prediction.
 * @top: the Y axis coordinate of upper-left corner.
 * @left: the X axis coordinate of upper-left corner.
 * @bottom: the Y axis coordinate of lower-right corner.
 * @right: the X axis coordinate of lower-right corner.
 *
 * Information describing prediction result from object detection models.
 * All fields are mandatory and need to be filled by the submodule.
 *
 * The fields top, left, bottom and right must be set in (0.0 to 1.0) relative
 * coordinate system.
 */
struct _GstMLPrediction {
  gchar  *label;
  gfloat confidence;
  guint  color;
  gfloat top;
  gfloat left;
  gfloat bottom;
  gfloat right;
};

/**
 * gst_ml_video_detection_module_execute:
 * @module: Pointer to ML post-processing module.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @predictions: GArray of #GstMLPrediction.
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
gst_ml_video_detection_module_execute (GstMLModule * module,
    GstMLFrame * mlframe, GArray * predictions)
{
  return gst_ml_module_execute (module, mlframe, (gpointer) predictions);
}

/**
 * gst_ml_prediction_transform_dimensions:
 * @prediction: Pointer to ML post-processing prediction.
 * @region: Source Aspect Ratio numerator.
 *
 * Helper function for normalizing prediction coordinates based on the source
 * aspect ratio and transforming them into relative coordinates using the
 * input tensor width and height. If coordinates are already in relative
 * coordinate system them width and height must be set  to 1.
 *
 * return: NONE
 */
static inline void
gst_ml_prediction_transform_dimensions (GstMLPrediction * prediction,
    GstVideoRectangle * region)
{
  prediction->top = (prediction->top - region->y) / region->h;
  prediction->bottom = (prediction->bottom - region->y) / region->h;
  prediction->left = (prediction->left - region->x) / region->w;
  prediction->right = (prediction->right - region->x) / region->w;
}

/**
 * gst_ml_predictions_intersection_score:
 * @l_prediction: Pointer to ML post-processing prediction.
 * @r_prediction: Pointer to ML post-processing prediction.
 *
 * Helper function for scoring how much two predictions are overlapping.
 *
 * return: Score from 0.0 (no overlap) to 1.0 (fully overlapping)
 */
static inline gdouble
gst_ml_predictions_intersection_score (GstMLPrediction * l_prediction,
    GstMLPrediction * r_prediction)
{
  gdouble width = 0, height = 0, intersection = 0, l_area = 0, r_area = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN (l_prediction->right, r_prediction->right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_prediction->left, r_prediction->left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_prediction->bottom, r_prediction->bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_prediction->top, r_prediction->top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  intersection = width * height;

  // Calculate the are of the 2 objects.
  l_area = (l_prediction->right - l_prediction->left) *
      (l_prediction->bottom - l_prediction->top);
  r_area = (r_prediction->right - r_prediction->left) *
      (r_prediction->bottom - r_prediction->top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

/**
 * gst_ml_non_max_suppression:
 * @l_prediction: Pointer to ML post-processing prediction.
 * @predictions: GArray of #GstMLPrediction.
 *
 * Helper function for Non-Max Suppression (NMS) algorithm.
 *
 * return: (-2) If confidence of the prediction is lower then any in the list.
 *         (-1) If no prediction with the same label is present in the list.
 *         (>= 0) If confidence of the prediction is higher then any in the list.
 */
static inline gint
gst_ml_non_max_suppression (GstMLPrediction * l_prediction, GArray * predictions)
{
  gdouble score = 0.0;
  guint idx = 0;

  for (idx = 0; idx < predictions->len;  idx++) {
    GstMLPrediction *r_prediction =
        &(g_array_index (predictions, GstMLPrediction, idx));

    score = gst_ml_predictions_intersection_score (l_prediction, r_prediction);

    // If the score is below the threshold, continue with next list entry.
    if (score <= NMS_INTERSECTION_THRESHOLD)
      continue;

    // If labels do not match, continue with next list entry.
    if (g_strcmp0 (l_prediction->label, r_prediction->label) != 0)
      continue;

    // If confidence of current prediction is higher, remove the old entry.
    if (l_prediction->confidence > r_prediction->confidence)
      return idx;

    // If confidence of current prediction is lower, don't add it to the list.
    if (l_prediction->confidence <= r_prediction->confidence)
      return -2;
  }

  // If this point is reached then add current prediction to the list;
  return -1;
}

G_END_DECLS

#endif // __GST_QTI_ML_VIDEO_DETECTION_MODULE_H__
