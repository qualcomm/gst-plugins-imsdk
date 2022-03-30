/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <gst/ml/gstmlmeta.h>


// Layer index at which the object score resides.
#define SCORE_IDX              4
// Layer index from which the class labels begin.
#define CLASSES_IDX            5
// Object score threshold represented as an exponent of sigmoid 0.1 (10%).
#define SCORE_THRESHOLD        (-2.197224577F)
// Class confidence threshold (10%).
#define CONFIDENCE_THRESHOLD   0.1F
// Non-maximum Suppression (NMS) threshold (50%).
#define INTERSECTION_THRESHOLD 0.5F

#define CAST_TO_GFLOAT(data)    ((gfloat*)data)

typedef struct _GstPrivateModule GstPrivateModule;
typedef struct _GstLabel GstLabel;

struct _GstPrivateModule {
  GHashTable *labels;
};

struct _GstLabel {
  gchar *name;
  guint color;
};

// Offset values for each of the 3 tensors needed for dequantization.
static const gint32 qoffsets[3] = { 128, 128, 128 };
// Scale values for each of the 3 tensors needed for dequantization.
static const gfloat qscales[3] = { 0.163093, 0.170221, 0.213311 };
// Bounding box weights for each of the 3 tensors used for normalization.
static const gint32 weights[3][2] = { {32, 32}, {16, 16}, {8, 8} };
// Bounding box gains for each of the 3 tensors used for normalization.
static const gint32 gains[3][3][2] = {
    { {116, 90}, {156, 198}, {373, 326} },
    { {30,  61}, {62,   45}, {59,  119} },
    { {10,  13}, {16,   30}, {33,   23} },
};

static GstLabel *
gst_ml_label_new ()
{
  GstLabel *label = g_new (GstLabel, 1);

  label->name = NULL;
  label->color = 0x00000000;

  return label;
}

static void
gst_ml_label_free (GstLabel * label)
{
  if (label->name != NULL)
    g_free (label->name);

  g_free (label);
}

static gint
gst_ml_compare_predictions (gconstpointer a, gconstpointer b)
{
  const GstMLPrediction *l_prediction, *r_prediction;

  l_prediction = (const GstMLPrediction*)a;
  r_prediction = (const GstMLPrediction*)b;

  if (l_prediction->confidence > r_prediction->confidence)
    return -1;
  else if (l_prediction->confidence < r_prediction->confidence)
    return 1;

  return 0;
}

static gdouble
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

static gint
gst_ml_non_max_suppression (GstMLPrediction * l_prediction, GList * predictions)
{
  GList *list = NULL;
  gdouble score = 0.0;

  for (list = predictions; list != NULL; list = g_list_next (list)) {
    GstMLPrediction *r_prediction = (GstMLPrediction *) list->data;

    score = gst_ml_predictions_intersection_score (l_prediction, r_prediction);

    // If the score is below the threshold, continue with next list entry.
    if (score <= INTERSECTION_THRESHOLD)
      continue;

    // If labels do not match, continue with next list entry.
    if (g_strcmp0 (l_prediction->label, r_prediction->label) != 0)
      continue;

    // If confidence of current prediction is higher, remove the old entry.
    if (l_prediction->confidence > r_prediction->confidence)
      return g_list_index (predictions, r_prediction);

    // If confidence of current prediction is lower, don't add it to the list.
    if (l_prediction->confidence <= r_prediction->confidence)
      return -2;
  }

  // If this point is reached then add current prediction to the list;
  return -1;
}

gpointer
gst_ml_video_detection_module_init (const gchar * labels)
{
  GstPrivateModule *module = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0;

  g_value_init (&list, GST_TYPE_LIST);

  if (g_file_test (labels, G_FILE_TEST_IS_REGULAR)) {
    GString *string = NULL;
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (labels, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get labels file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return NULL;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    string = g_string_new (contents);
    g_free (contents);

    // Add opening and closing brackets.
    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    // Get the raw character data.
    contents = g_string_free (string, FALSE);

    success = gst_value_deserialize (&list, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize labels file contents!");
      return NULL;
    }
  } else if (!gst_value_deserialize (&list, labels)) {
    GST_ERROR ("Failed to deserialize labels!");
    return NULL;
  }

  module = g_slice_new0 (GstPrivateModule);
  g_return_val_if_fail (module != NULL, NULL);

  module->labels = g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) gst_ml_label_free);

  for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
    GstStructure *structure = NULL;
    GstLabel *label = NULL;
    guint id = 0;

    structure = GST_STRUCTURE (
        g_value_dup_boxed (gst_value_list_get_value (&list, idx)));

    if (structure == NULL) {
      GST_WARNING ("Failed to extract structure!");
      continue;
    } else if (!gst_structure_has_field (structure, "id") ||
        !gst_structure_has_field (structure, "color")) {
      GST_WARNING ("Structure does not contain 'id' and/or 'color' fields!");
      gst_structure_free (structure);
      continue;
    }

    if ((label = gst_ml_label_new ()) == NULL) {
      GST_ERROR ("Failed to allocate label memory!");
      gst_structure_free (structure);
      continue;
    }

    label->name = g_strdup (gst_structure_get_name (structure));
    label->name = g_strdelimit (label->name, "-", ' ');

    gst_structure_get_uint (structure, "color", &label->color);
    gst_structure_get_uint (structure, "id", &id);

    g_hash_table_insert (module->labels, GUINT_TO_POINTER (id), label);
    gst_structure_free (structure);
  }

  g_value_unset (&list);
  return module;
}

void
gst_ml_video_detection_module_deinit (gpointer instance)
{
  GstPrivateModule *module = (GstPrivateModule*) instance;

  if (NULL == module)
    return;

  g_hash_table_destroy (module->labels);
  g_slice_free (GstPrivateModule, module);
}

gboolean
gst_ml_video_detection_module_process (gpointer instance, GstMLFrame * frame,
    GList ** predictions)
{
  GstPrivateModule *module = (GstPrivateModule*) instance;
  GstProtectionMeta *pmeta = NULL;
  gint sar_n = 1, sar_d = 1, result = -1;
  guint idx = 0, num = 0, anchor = 0, x = 0, y = 0, m = 0, id = 0;
  guint n_layers = 0, n_anchors = 0, maxwidth = 0, maxheight = 0;
  gfloat confidence = 0.0, score = 0.0, bbox[4] = { 0, };

  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (frame->buffer)) != NULL) {
    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
  }

  for (idx = 0; idx < GST_ML_FRAME_N_BLOCKS (frame); idx++, num = 0) {
    GstLabel *label = NULL;
    GstMLPrediction *prediction = NULL;
    guint8 *data = NULL;

    data = GST_ML_FRAME_BLOCK_DATA (frame, idx);

    // The 2nd dimension represents number of anchors.
    n_anchors = GST_ML_FRAME_DIM (frame, idx, 1);
    // The 3rd dimension represents the object matrix height.
    maxheight = GST_ML_FRAME_DIM (frame, idx, 2);
    // The 4th dimension represents the object matrix width.
    maxwidth = GST_ML_FRAME_DIM (frame, idx, 3);
    // The 5th dimension represents number of layers.
    n_layers = GST_ML_FRAME_DIM (frame, idx, 4);

    for (anchor = 0; anchor < n_anchors; anchor++) {
      for (y = 0; y < maxheight; y++) {
        for (x = 0; x < maxwidth; x++, num += n_layers) {
          // Dequantize the object score.
          // Represented as an exponent 'x' in sigmoid function: 1 / (1 + exp(x)).
          score = (data[num + SCORE_IDX] - qoffsets[idx]) * qscales[idx];

          // Discard results below the minimum score threshold.
          if (score <= SCORE_THRESHOLD)
            continue;

          // Initialize the class ID value.
          id = num + CLASSES_IDX;

          // Find the class ID with the highest confidence.
          for (m = (num + CLASSES_IDX + 1); m < (num + n_layers); m++)
            id = (data[m] > data[id]) ? m : id;

          // Dequantize the class confidence.
          confidence = (data[id] - qoffsets[idx]) * qscales[idx];
          // Apply a sigmoid function in order to normalize the confidence.
          confidence = 1 / (1 + expf (- confidence));
          // Normalize the end confidence with the object score value.
          confidence *= 1 / (1 + expf (- score));

          // Discard results below the minimum confidence threshold.
          if (confidence <= CONFIDENCE_THRESHOLD)
            continue;

          // Dequantize the bounding box parameters.
          bbox[0] = (data[num] - qoffsets[idx]) * qscales[idx];
          bbox[1] = (data[num + 1] - qoffsets[idx]) * qscales[idx];
          bbox[2] = (data[num + 2] - qoffsets[idx]) * qscales[idx];
          bbox[3] = (data[num + 3] - qoffsets[idx]) * qscales[idx];

          // Apply a sigmoid function in order to normalize the parameters.
          bbox[0] = 1 / (1 + expf (- bbox[0]));
          bbox[1] = 1 / (1 + expf (- bbox[1]));
          bbox[2] = 1 / (1 + expf (- bbox[2]));
          bbox[3] = 1 / (1 + expf (- bbox[3]));

          // Special calculations for the bounding box parameters.
          bbox[0] = (bbox[0] * 2 - 0.5F + x) * weights[idx][0];
          bbox[1] = (bbox[1] * 2 - 0.5F + y) * weights[idx][1];
          bbox[2] = pow ((bbox[2] * 2), 2) * gains[idx][anchor][0];
          bbox[3] = pow ((bbox[3] * 2), 2) * gains[idx][anchor][1];

          label = (GstLabel*) g_hash_table_lookup (module->labels,
              GUINT_TO_POINTER ((id - (num + CLASSES_IDX)) + 1));

          prediction = gst_ml_prediction_new ();

          prediction->confidence = confidence * 100.0F;
          prediction->label = g_strdup (label ? label->name : "unknown");
          prediction->color = label ? label->color : 0x000000FF;

          prediction->top = bbox[1] - (bbox[3] / 2);
          prediction->left = bbox[0] - (bbox[2] / 2);
          prediction->bottom = bbox[1] + (bbox[3] / 2);
          prediction->right = bbox[0] + (bbox[2] / 2);

          // Adjust bounding box dimensions with extracted source aspect ratio.
          if (sar_n > sar_d) {
            gdouble coeficient = 0.0;
            gst_util_fraction_to_double (sar_n, sar_d, &coeficient);

            prediction->top /= 384.0 / coeficient;
            prediction->bottom /= 384.0 / coeficient;
            prediction->left /= 384.0;
            prediction->right /= 384.0;
          } else if (sar_n < sar_d) {
            gdouble coeficient = 0.0;
            gst_util_fraction_to_double (sar_d, sar_n, &coeficient);

            prediction->top /= 640.0;
            prediction->bottom /= 640.0;
            prediction->left /= 640.0 / coeficient;
            prediction->right /= 640.0 / coeficient;
          }

          // Non-Max Suppression (NMS) algorithm.
          result = gst_ml_non_max_suppression (prediction, *predictions);

          // If the NMS result is -2 don't add the prediction to the list.
          if (result == (-2)) {
            gst_ml_prediction_free (prediction);
            continue;
          }

          // If the NMS result is above -1 remove the entry with the result index.
          if (result >= 0) {
            GList *link = g_list_nth (*predictions, result);
            *predictions = g_list_remove_link (*predictions, link);
            g_list_free_full (link, (GDestroyNotify) gst_ml_prediction_free);
          }

          *predictions = g_list_insert_sorted (
              *predictions, prediction, gst_ml_compare_predictions);
        }
      }
    }
  }

  return TRUE;
}
