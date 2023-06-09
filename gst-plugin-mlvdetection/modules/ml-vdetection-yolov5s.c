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

#include <stdio.h>
#include <math.h>


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

// Layer index at which the object score resides.
#define SCORE_IDX              4
// Layer index from which the class labels begin.
#define CLASSES_IDX            5
// Non-maximum Suppression (NMS) threshold (50%).
#define INTERSECTION_THRESHOLD 0.5F

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Bounding box weights for each of the 3 tensors used for normalization.
static const gint32 weights[3][2] = { {8, 8}, {16, 16}, {32, 32}  };
// Bounding box gains for each of the 3 tensors used for normalization.
static const gint32 gains[3][3][2] = {
    { {10,  13}, {16,   30}, {33,   23} },
    { {30,  61}, {62,   45}, {59,  119} },
    { {116, 90}, {156, 198}, {373, 326} },
};

// Output dimensions depends on input[w, h], weights_idex and n_classes.
// Dimensions format: <<1, w/8, h/8, D>, <1, w/16, h/16, D>, <1, w/32, h/32, D>>
// 8, 16, 32 are coresponding weights[0][0], weights[1][0], weights[2][0]
// D = (n_classes + CLASSES_IDX) * 3
// MODULE_CAPS support input[w, h]: [32, 32] -> [1920, 1088]. n_class: 1 -> 1001
#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < <1, [1, 136], [1, 136], [18, 3018]>," \
    "<1, [1, 136], [1, 136], [18, 3018]>, <1, [1, 136], [1, 136], [18, 3018]> > "

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

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

static void
gst_ml_module_parse_split_tensors (GstMLSubModule * submodule,
    GArray * predictions, GstMLFrame * mlframe)
{
  GstProtectionMeta *pmeta = NULL;
  guint idx = 0, num = 0, anchor = 0, n_anchors = 0, x = 0, y = 0, m = 0, id = 0;
  guint n_detections = 0, width = 0, height = 0, in_width = 0, in_height = 0;
  gfloat confidence = 0.0, score = 0.0, bbox[4] = { 0, };
  gint nms = -1, sar_n = 1, sar_d = 1;

  // Extract the SAR (Source Aspect Ratio) and input tensor resolution.
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);
    gst_structure_get_uint (pmeta->info, "input-tensor-width", &in_width);
    gst_structure_get_uint (pmeta->info, "input-tensor-height", &in_height);
  }

  for (idx = 0; idx < GST_ML_FRAME_N_BLOCKS (mlframe); idx++, num = 0) {
    GstLabel *label = NULL;
    gfloat *data = NULL;
    guint w_idx = 0;

    data = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, idx));

    // Number of anchors.
    n_anchors = 3;
    // Detection size(85) = CLASSES_IDX(5) + n_class(80)
    n_detections = GST_ML_FRAME_DIM (mlframe, idx, 3) / n_anchors;

    // The 1rd dimension represents the object matrix height.
    height = GST_ML_FRAME_DIM (mlframe, idx, 1);
    // The 2th dimension represents the object matrix width.
    width = GST_ML_FRAME_DIM (mlframe, idx, 2);

    // Find weight/gain idx in case tensor order sometimes is changed unexpected.
    // Ex: "< <1, 20, 20, 255>, <1, 40, 40, 255>, <1, 80, 80, 255> > "
    // TODO: optimize
    for (w_idx = 0; w_idx < 3 ; w_idx++)
      if (weights[w_idx][0] == (gint32)(in_width / width)) break;

    GST_DEBUG ("height: %d, width: %d, threshold: %f n_classes: %d",
        height, width, submodule->threshold, n_detections -  CLASSES_IDX);

    for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
        for (anchor = 0; anchor < n_anchors; anchor++, num += n_detections) {
          GstMLPrediction prediction = { 0, };

          // Get the object score.
          score = data[num + SCORE_IDX];

          // Discard results below the minimum score threshold.
          if (score < submodule->threshold)
            continue;

          // Initialize the class ID value.
          id = num + CLASSES_IDX;

          // Find the class ID with the highest confidence.
          for (m = (num + CLASSES_IDX + 1); m < (num + n_detections); m++)
            id = (data[m] > data[id]) ? m : id;

          // Class confidence.
          confidence = data[id];

          // Apply a sigmoid function in order to normalize the confidence.
          confidence = 1 / (1 + expf (- confidence));
          // Normalize the end confidence with the object score value.
          confidence *= 1 / (1 + expf (- score));

          // Discard results below the minimum confidence threshold.
          if (confidence < submodule->threshold)
            continue;

          // Bounding box parameters.
          bbox[0] = data[num];
          bbox[1] = data[num + 1];
          bbox[2] = data[num + 2];
          bbox[3] = data[num + 3];

          // Apply a sigmoid function in order to normalize the parameters.
          bbox[0] = 1 / (1 + expf (- bbox[0]));
          bbox[1] = 1 / (1 + expf (- bbox[1]));
          bbox[2] = 1 / (1 + expf (- bbox[2]));
          bbox[3] = 1 / (1 + expf (- bbox[3]));

          // Special calculations for the bounding box parameters.
          bbox[0] = (bbox[0] * 2 - 0.5F + x) * weights[w_idx][0];
          bbox[1] = (bbox[1] * 2 - 0.5F + y) * weights[w_idx][1];
          bbox[2] = pow ((bbox[2] * 2), 2) * gains[w_idx][anchor][0];
          bbox[3] = pow ((bbox[3] * 2), 2) * gains[w_idx][anchor][1];

          label = g_hash_table_lookup (submodule->labels,
              GUINT_TO_POINTER (id - (num + CLASSES_IDX)));

          prediction.confidence = confidence * 100.0F;
          prediction.label = g_strdup (label ? label->name : "unknown");
          prediction.color = label ? label->color : 0x000000FF;

          prediction.top = bbox[1] - (bbox[3] / 2);
          prediction.left = bbox[0] - (bbox[2] / 2);
          prediction.bottom = bbox[1] + (bbox[3] / 2);
          prediction.right = bbox[0] + (bbox[2] / 2);

          // Adjust bounding box dimensions with extracted source aspect ratio.
          gst_ml_prediction_transform_dimensions (&prediction, sar_n, sar_d,
              in_width, in_height);

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

          predictions = g_array_append_val (predictions, prediction);
        }
      }
    }
  }
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
  GstCaps *caps = NULL, *mlcaps = NULL;
  const gchar *input = NULL;
  GValue list = G_VALUE_INIT;
  gdouble threshold = 0.0;
  gboolean success = FALSE;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  if (!(success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_CAPS))) {
    GST_ERROR ("Settings stucture does not contain configuration caps!");
    goto cleanup;
  }

  // Fetch the configuration capabilities.
  gst_structure_get (settings, GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, &caps, NULL);
  // Get the set of supported capabilities.
  mlcaps = gst_ml_module_caps ();

  // Make sure that the configuration capabilities are fixated and supported.
  if (!(success = gst_caps_is_fixed (caps))) {
    GST_ERROR ("Configuration caps are not fixated!");
    goto cleanup;
  } else if (!(success = gst_caps_can_intersect (caps, mlcaps))) {
    GST_ERROR ("Configuration caps are not supported!");
    goto cleanup;
  }

  if (!(success = gst_ml_info_from_caps (&(submodule->mlinfo), caps))) {
    GST_ERROR ("Failed to get ML info from confguration caps!");
    goto cleanup;
  }

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
  if (caps != NULL)
    gst_caps_unref (caps);

  g_value_unset (&list);
  gst_structure_free (settings);

  return success;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GArray *predictions = (GArray *) output;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  if (!gst_ml_info_is_equal (&(mlframe->info), &(submodule->mlinfo))) {
    GST_ERROR ("ML frame with unsupported layout!");
    return FALSE;
  }

  gst_ml_module_parse_split_tensors (submodule, predictions, mlframe);

  return TRUE;
}
