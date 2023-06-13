/*
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

#include "ml-video-detection-module.h"

#include <math.h>
#include <stdio.h>


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

// The size in pixels of a macro block.
#define MACRO_BLOCK_SIZE       8
// Non-maximum Suppression (NMS) threshold (50%).
#define INTERSECTION_THRESHOLD 0.5F
// Minimum relative size of the bounding box must occupy in the image.
#define BBOX_SIZE_THRESHOLD    0.01F

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 60, 80, 1 >, < 1, 60, 80, 1 >, < 1, 60, 80, 10 >, < 1, 60, 80, 4 > >; "

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // List of prediction labels.
  GHashTable *labels;
  // Confidence threshold value.
  gfloat     threshold;
};

static inline void
gst_ml_prediction_transform_dimensions (GstMLPrediction * prediction,
    gint num, gint denum, guint width, guint height)
{
  gdouble coeficient = 0.0;

  if (num > denum) {
    gst_util_fraction_to_double (num, denum, &coeficient);

    prediction->top /= width / coeficient;
    prediction->bottom /= width / coeficient;
    prediction->left /= width;
    prediction->right /= width;

    return;
  } else if (num < denum) {
    gst_util_fraction_to_double (denum, num, &coeficient);

    prediction->top /= height;
    prediction->bottom /= height;
    prediction->left /= height / coeficient;
    prediction->right /= height / coeficient;

    return;
  }

  // There is no need for AR adjustments, just translate to relative coords.
  prediction->top /= height;
  prediction->bottom /= height;
  prediction->left /= width;
  prediction->right /= width;
}

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
  if (width <= 0.0F) return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_prediction->bottom, r_prediction->bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_prediction->top, r_prediction->top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F) return 0.0F;

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
    if (score <= INTERSECTION_THRESHOLD)
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

gpointer
gst_ml_module_open (void)
{
  GstMLSubModule *submodule = NULL;

  submodule = g_slice_new0 (GstMLSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  return (gpointer) submodule;
}

void
gst_ml_module_close (gpointer instance)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  if (submodule->labels != NULL)
    g_hash_table_destroy (submodule->labels);

  g_slice_free (GstMLSubModule, submodule);
}

GstCaps *
gst_ml_module_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&modulecaps);
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

gboolean
gst_ml_module_configure (gpointer instance, GstStructure * settings)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  const gchar *input = NULL;
  GValue list = G_VALUE_INIT;
  gdouble threshold = 0.0;
  gboolean success = FALSE;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  input = gst_structure_get_string (settings, GST_ML_MODULE_OPT_LABELS);

  // Parse funtion will print error message if it fails, simply goto cleanup.
  if (!(success = gst_ml_parse_labels (input, &list)))
    goto cleanup;

  submodule->labels = gst_ml_load_labels (&list);

  // Labels funtion will print error message if it fails, simply goto cleanup.
  if (!(success = (submodule->labels != NULL)))
    goto cleanup;

  success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_THRESHOLD);
  if (!success) {
    GST_ERROR ("Settings stucture does not contain threshold value!");
    goto cleanup;
  }

  gst_structure_get_double (settings, GST_ML_MODULE_OPT_THRESHOLD, &threshold);
  submodule->threshold = threshold / 100.0;

cleanup:
  g_value_unset (&list);
  gst_structure_free (settings);

  return success;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GArray *predictions = (GArray *)output;
  GstProtectionMeta *pmeta = NULL;
  gfloat *scores = NULL, *hm_pool = NULL, *landmarks = NULL, *bboxes = NULL;
  gfloat size = 0;
  guint idx = 0, num = 0, n_classes = 0, n_blocks = 0, in_width = 0, in_height = 0;
  gint sar_n = 1, sar_d = 1, nms = -1, cx = 0, cy = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  // Extract the SAR (Source Aspect Ratio) and input tensor resolution.
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);
    gst_structure_get_uint (pmeta->info, "input-tensor-width", &in_width);
    gst_structure_get_uint (pmeta->info, "input-tensor-height", &in_height);
  }

  // TODO: First tensor represents some kind of confidence scores.
  scores = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));
  // TODO: Second tensor represents some kind of confidence scores.
  hm_pool = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
  // Third tensor represents the landmarks (left eye, right ear, etc.).
  landmarks = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 2));
  // Fourh tensor represents the coordinates of the bounding boxes.
  bboxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 3));

  // The 4th tensor dimension represents the number of detection classes.
  n_classes = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Calculate the number of macroblocks.
  n_blocks = GST_ML_FRAME_DIM (mlframe, 0, 1) * GST_ML_FRAME_DIM (mlframe, 0, 2);

  for (idx = 0; idx < n_blocks; ++idx) {
    GstLabel *label = NULL;
    GstMLPrediction prediction = { 0, };

    // Discard invalid results.
    if (scores[idx] != hm_pool[idx])
      continue;

    // Discard results below the minimum score threshold.
    if (scores[idx] < submodule->threshold)
      continue;

    // Calculate the centre coordinates.
    cx = (idx / n_classes) % GST_ML_FRAME_DIM (mlframe, 0, 2);
    cy = (idx / n_classes) / GST_ML_FRAME_DIM (mlframe, 0, 2);

    prediction.left = (cx - bboxes[(idx * 4)]) * MACRO_BLOCK_SIZE;
    prediction.top = (cy - bboxes[(idx * 4) + 1]) * MACRO_BLOCK_SIZE;
    prediction.right = (cx + bboxes[(idx * 4) + 2]) * MACRO_BLOCK_SIZE;
    prediction.bottom = (cy + bboxes[(idx * 4) + 3]) * MACRO_BLOCK_SIZE;

    // Adjust bounding box dimensions with SAR and input tensor resolution.
    gst_ml_prediction_transform_dimensions (&prediction, sar_n, sar_d,
        in_width, in_height);

    size = (prediction.right - prediction.left) *
        (prediction.bottom - prediction.top);

    // Discard results below the minimum bounding box size.
    if (size < BBOX_SIZE_THRESHOLD)
      continue;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (idx % n_classes));

    prediction.confidence = scores[idx] * 100.0;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000FF;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_non_max_suppression (&prediction, predictions);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)){
      g_free (prediction.label);
      continue;
    }

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (predictions, nms);

    // TODO: Enchance predictions to support landmarks.
    for (num = 0; num < 5; ++num) {
      gfloat lx = 0.0, ly = 0.0;

      if ((idx % n_classes) != 0)
        continue;

      lx = (cx + landmarks[idx / n_classes * 10 + num]) * MACRO_BLOCK_SIZE;
      ly = (cy + landmarks[idx / n_classes * 10 + num + 5]) * MACRO_BLOCK_SIZE;
      GST_INFO ("Ladnmark: [ %.2f %.2f ] ", lx, ly);
    }

    predictions = g_array_append_val (predictions, prediction);
  }

  return TRUE;
}
