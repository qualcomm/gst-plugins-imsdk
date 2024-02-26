/*
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

#include <stdio.h>
#include <math.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-detection.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Layer index at which the object score resides.
#define SCORE_IDX              4
// Layer index from which the class labels begin.
#define CLASSES_IDX            5

// Bounding box weights for each of the 3 tensors used for normalization.
static const gint32 weights[3][2] = { {8, 8}, {16, 16}, {32, 32} };
// Bounding box gains for each of the 3 tensors used for normalization.
static const gint32 gains[3][3][2] = {
    { {10,  13}, {16,   30}, {33,   23} },
    { {30,  61}, {62,   45}, {59,  119} },
    { {116, 90}, {156, 198}, {373, 326} },
};

// Output dimensions depends on input[w, h], weights index and n_classes.
//
// First set of module capabilities have the following format:
// <<1, w/8, h/8, C>, <1, w/16, h/16, C>, <1, w/32, h/32, C>>
// C = ((n_classes + CLASSES_IDX) * 3) [where 3 is number of anchors].
//
// Second set of module capabilities have the following format:
// <<1, 3, w/8, h/8, C>, <1, 3, w/16, h/16, C>, <1, 3, w/32, h/32, C>>
// C = (n_classes + CLASSES_IDX)
//
// Third set of module capabilities have the following format:
// <<1, D, C>>
// C = (n_classes + CLASSES_IDX)
// D = ((w/8 * h/8) + (w/16 * h/16) + (w/32* h/32)) * 3
//
// 8, 16, 32 are coresponding weights[0][0], weights[1][0], weights[2][0]
// The maximum supported input[w, h] is [1088, 1088]
#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, [1, 136], [1, 136], [18, 3018]>, <1, [1, 136], [1, 136], [18, 3018]>, <1, [1, 136], [1, 136], [18, 3018]> >; " \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8 }, " \
    "dimensions = (int) < <1, 3, [1, 136], [1, 136], [6, 85]>, <1, 3, [1, 136], [1, 136], [6, 85]>, <1, 3, [1, 136], [1, 136], [6, 85]> >; " \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8 }, " \
    "dimensions = (int) < <1, [21, 72828], [6, 85]> >;"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // The width of the model input tensor.
  guint      inwidth;
  // The height of the model input tensor.
  guint      inheight;

  // List of prediction labels.
  GHashTable *labels;
  // Confidence threshold value.
  gfloat     threshold;

  // Offset values for each of the tensors for dequantization of some tensors.
  gdouble    qoffsets[GST_ML_MAX_TENSORS];
  // Scale values for each of the tensors for dequantization of some tensors.
  gdouble    qscales[GST_ML_MAX_TENSORS];
};

static inline gfloat
gst_ml_module_get_threshold_value (GstMLType mltype, gfloat threshold)
{
  // Adjust threshold value depending on the tensors type.
  switch (mltype) {
    case GST_ML_TYPE_UINT8:
      // Confidence threshold represented as the exponent of sigmoid.
      return log (threshold / (1 - threshold));
    case GST_ML_TYPE_FLOAT32:
      // Confidence threshold is represent normally.
      return threshold;
    default:
      break;
  }
  return 0.0;
}

static void
gst_ml_module_parse_tripleblock_frame (GstMLSubModule * submodule,
    GArray * predictions, GstMLFrame * mlframe)
{
  GstProtectionMeta *pmeta = NULL;
  GstMLBoxPrediction *prediction = NULL;
  GstVideoRectangle region = { 0, };
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint idx = 0, num = 0, x = 0, y = 0, m = 0, id = 0, w_idx = 0, tile_idx = 0;
  guint width = 0, height = 0;
  guint anchor = 0, n_layers = 0, n_anchors = 0, n_tiles = 0;
  gdouble confidence = 0.0, score = 0.0, threshold = 0.0, bbox[4] = { 0, };
  gint nms = -1;

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLBoxPrediction, 0));

  prediction->batch_idx = 0;
  prediction->info = pmeta->info;

  // Extract the source tensor region with actual data.
  gst_ml_protecton_meta_get_source_region (pmeta, &region);

  mltype = GST_ML_FRAME_TYPE (mlframe);
  threshold = gst_ml_module_get_threshold_value (mltype, submodule->threshold);

  for (idx = 0; idx < GST_ML_FRAME_N_BLOCKS (mlframe); idx++, num = 0) {
    void *data = GST_ML_FRAME_BLOCK_DATA (mlframe, idx);

    if (GST_ML_FRAME_N_DIMENSIONS (mlframe, idx) == 5) {
      // The 2nd dimension represents number of anchors.
      n_anchors = GST_ML_FRAME_DIM (mlframe, idx, 1);
      // The 3rd dimension represents the object matrix height.
      height = GST_ML_FRAME_DIM (mlframe, idx, 2);
      // The 4th dimension represents the object matrix width.
      width = GST_ML_FRAME_DIM (mlframe, idx, 3);
      // The 5th dimension represents number of layers.
      n_layers = GST_ML_FRAME_DIM (mlframe, idx, 4);
    } else { // A 4 dimensional tensor which has exactly 3 anchors.
      // Number of anchors.
      n_anchors = 3;
      // The 1rd dimension represents the object matrix height.
      height = GST_ML_FRAME_DIM (mlframe, idx, 1);
      // The 2th dimension represents the object matrix width.
      width = GST_ML_FRAME_DIM (mlframe, idx, 2);
      // Layers(85) = CLASSES_IDX(5) + n_class(80)
      n_layers = GST_ML_FRAME_DIM (mlframe, idx, 3) / n_anchors;
    }

    // Total number of tiles in the matrix.
    n_tiles = width * height;

    // Find weight/gain idx in case tensor order sometimes is changed unexpected.
    // Ex: "< <1, 20, 20, 255>, <1, 40, 40, 255>, <1, 80, 80, 255> > "
    // TODO: optimize
    for (w_idx = 0; w_idx < 3 ; w_idx++)
      if (weights[w_idx][0] == (gint32)(submodule->inwidth / width)) break;

    for (tile_idx = 0; tile_idx < n_tiles; tile_idx++) {
      for (anchor = 0; anchor < n_anchors; anchor++, num += n_layers) {
        GstMLBoxEntry entry = { 0, };
        GstMLLabel *label = NULL;

        // Dequantize the object score.
        // Represented as an exponent 'x' in sigmoid function: 1 / (1 + exp(x)).
        score = gst_ml_tensor_extract_value (mltype, data, num + SCORE_IDX,
            submodule->qoffsets[idx], submodule->qscales[idx]);

        // Discard results below the minimum score threshold.
        if (score < threshold)
          continue;

        // Initialize the class index.
        id = num + CLASSES_IDX;

        // Find the class index with the highest score in current paxel.
        for (m = (num + CLASSES_IDX + 1); m < (num + n_layers); m++)
          id = (gst_ml_tensor_compare_values (mltype, data, m, id) > 0) ? m : id;

        // Dequantize the class confidence.
        confidence = gst_ml_tensor_extract_value (mltype, data, id,
            submodule->qoffsets[idx], submodule->qscales[idx]);

        // Discard results below the minimum confidence threshold.
        if (confidence < threshold)
          continue;

        // Apply a sigmoid function in order to normalize the confidence.
        confidence = 1 / (1 + expf (- confidence));
        // Normalize the end confidence with the object score value.
        confidence *= 1 / (1 + expf (- score));

        // Dequantize the bounding box parameters.
        bbox[0] = gst_ml_tensor_extract_value (mltype, data, num,
            submodule->qoffsets[idx], submodule->qscales[idx]);
        bbox[1] = gst_ml_tensor_extract_value (mltype, data, num + 1,
            submodule->qoffsets[idx], submodule->qscales[idx]);
        bbox[2] = gst_ml_tensor_extract_value (mltype, data, num + 2,
            submodule->qoffsets[idx], submodule->qscales[idx]);
        bbox[3] = gst_ml_tensor_extract_value (mltype, data, num + 3,
            submodule->qoffsets[idx], submodule->qscales[idx]);

        // Apply a sigmoid function in order to normalize the parameters.
        bbox[0] = 1 / (1 + expf (- bbox[0]));
        bbox[1] = 1 / (1 + expf (- bbox[1]));
        bbox[2] = 1 / (1 + expf (- bbox[2]));
        bbox[3] = 1 / (1 + expf (- bbox[3]));

        y = (tile_idx % n_tiles) / height;
        x = (tile_idx % n_tiles) % width;

        // Special calculations for the bounding box parameters.
        bbox[0] = (bbox[0] * 2 - 0.5F + x) * weights[w_idx][0];
        bbox[1] = (bbox[1] * 2 - 0.5F + y) * weights[w_idx][1];
        bbox[2] = pow ((bbox[2] * 2), 2) * gains[w_idx][anchor][0];
        bbox[3] = pow ((bbox[3] * 2), 2) * gains[w_idx][anchor][1];

        entry.top = bbox[1] - (bbox[3] / 2);
        entry.left = bbox[0] - (bbox[2] / 2);
        entry.bottom = bbox[1] + (bbox[3] / 2);
        entry.right = bbox[0] + (bbox[2] / 2);

        // Adjust bounding box dimensions with extracted source tensor region.
        gst_ml_box_transform_dimensions (&entry, &region);

        // Discard results with out of region coordinates.
        if ((entry.top > 1.0) || (entry.left > 1.0) ||
            (entry.bottom > 1.0) || (entry.right > 1.0))
          continue;

        label = g_hash_table_lookup (submodule->labels,
            GUINT_TO_POINTER (id - (num + CLASSES_IDX)));

        entry.confidence = confidence * 100.0F;
        entry.name = g_quark_from_string (label ? label->name : "unknown");
        entry.color = label ? label->color : 0x000000FF;

        // Non-Max Suppression (NMS) algorithm.
        nms = gst_ml_box_non_max_suppression (&entry, prediction->entries);

        // If the NMS result is -2 don't add the prediction to the list.
        if (nms == (-2))
          continue;

        // If the NMS result is above -1 remove the entry with the nms index.
        if (nms >= 0)
          prediction->entries = g_array_remove_index (prediction->entries, nms);

        prediction->entries = g_array_append_val (prediction->entries, entry);
      }
    }
  }

  g_array_sort (prediction->entries, (GCompareFunc) gst_ml_box_compare_entries);
}

static void
gst_ml_module_parse_monoblock_tensors (GstMLSubModule * submodule,
    GArray * predictions, GstMLFrame * mlframe)
{
  GstProtectionMeta *pmeta = NULL;
  GstMLBoxPrediction *prediction = NULL;
  GstMLLabel *label = NULL;
  guint8 *data = NULL;
  GstVideoRectangle region = { 0, };
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint idx = 0, num = 0, m = 0, id = 0, n_layers = 0, n_tiles = 0;
  gdouble confidence = 0.0, score = 0.0, bbox[4] = { 0, };
  gint nms = -1;

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLBoxPrediction, 0));

  prediction->batch_idx = 0;
  prediction->info = pmeta->info;

  // Extract the dimensions of the input tensor that produced the output tensors.
  if (submodule->inwidth == 0 || submodule->inheight == 0) {
    gst_ml_protecton_meta_get_source_dimensions (pmeta, &(submodule->inwidth),
        &(submodule->inheight));
  }

  // Extract the source tensor region with actual data.
  gst_ml_protecton_meta_get_source_region (pmeta, &region);

  data = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  mltype = GST_ML_FRAME_TYPE (mlframe);

  // The 2nd dimension represents ((w/8 * h/8) + (w/16 * h/16) + (w/32* h/32)) * 3
  n_tiles = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension represents number of layers.
  n_layers = GST_ML_FRAME_DIM (mlframe, 0, 2);

  for (num = 0; num < n_tiles; num++, idx += n_layers) {
    GstMLBoxEntry entry = { 0, };

    // Dequantize the object score.
    // Represented as an exponent 'x' in sigmoid function: 1 / (1 + exp(x)).
    score = gst_ml_tensor_extract_value (mltype, data, idx + SCORE_IDX,
        submodule->qoffsets[0], submodule->qscales[0]);

    // Discard results below the minimum score threshold.
    if (score < submodule->threshold)
      continue;

    // Initialize the class ID value.
    id = idx + CLASSES_IDX;

    // Find the class ID with the highest confidence.
    for (m = (idx + CLASSES_IDX + 1); m < (idx + n_layers); m++)
      id = (gst_ml_tensor_compare_values (mltype, data, m, id) > 0) ? m : id;

    // Dequantize the class confidence.
    confidence = gst_ml_tensor_extract_value (mltype, data, id,
        submodule->qoffsets[0], submodule->qscales[0]);

    // Normalize the end confidence with the object score value.
    confidence *= score;

    // Discard results below the minimum confidence threshold.
    if (confidence < submodule->threshold)
      continue;

    // Dequantize the bounding box parameters.
    bbox[0] = gst_ml_tensor_extract_value (mltype, data, idx,
        submodule->qoffsets[0], submodule->qscales[0]);
    bbox[1] = gst_ml_tensor_extract_value (mltype, data, idx + 1,
        submodule->qoffsets[0], submodule->qscales[0]);
    bbox[2] = gst_ml_tensor_extract_value (mltype, data, idx + 2,
        submodule->qoffsets[0], submodule->qscales[0]);
    bbox[3] = gst_ml_tensor_extract_value (mltype, data, idx + 3,
        submodule->qoffsets[0], submodule->qscales[0]);

    // Translate box coordinates to absolute as the tensor region is in absolute.
    entry.top = (bbox[1] - (bbox[3] / 2)) * submodule->inheight;
    entry.left = (bbox[0] - (bbox[2] / 2)) * submodule->inwidth;
    entry.bottom = (bbox[1] + (bbox[3] / 2)) * submodule->inheight;
    entry.right = (bbox[0] + (bbox[2] / 2)) * submodule->inwidth;

    // Adjust bounding box dimensions with extracted source tensor region.
    gst_ml_box_transform_dimensions (&entry, &region);

    // Discard results with out of region coordinates.
    if ((entry.top > 1.0) || (entry.left > 1.0) ||
        (entry.bottom > 1.0) || (entry.right > 1.0))
      continue;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (id - (idx + CLASSES_IDX)));

    entry.confidence = confidence * 100.0F;
    entry.name = g_quark_from_string (label ? label->name : "unknown");
    entry.color = label ? label->color : 0x000000FF;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_box_non_max_suppression (&entry, prediction->entries);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      prediction->entries = g_array_remove_index (prediction->entries, nms);

    prediction->entries = g_array_append_val (prediction->entries, entry);
  }

  g_array_sort (prediction->entries, (GCompareFunc) gst_ml_box_compare_entries);
}

gpointer
gst_ml_module_open (void)
{
  GstMLSubModule *submodule = NULL;
  guint idx = 0;

  submodule = g_slice_new0 (GstMLSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  // Initialize the quantization offsets and scales.
  for (idx = 0; idx < GST_ML_MAX_TENSORS; idx++) {
    submodule->qoffsets[idx] = 0.0;
    submodule->qscales[idx] = 1.0;
  }

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

  if ((GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_INT8) ||
      (GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_UINT8)) {
    GstStructure *constants = NULL;
    const GValue *qoffsets = NULL, *qscales = NULL;
    guint idx = 0, n_tensors = 0;

    success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_CONSTANTS);
    if (!success) {
      GST_ERROR ("Settings stucture does not contain constants value!");
      goto cleanup;
    }

    constants = GST_STRUCTURE (g_value_get_boxed (
        gst_structure_get_value (settings, GST_ML_MODULE_OPT_CONSTANTS)));

    if (!(success = gst_structure_has_field (constants, "q-offsets"))) {
      GST_ERROR ("Missing quantization offsets coefficients!");
      goto cleanup;
    } else if (!(success = gst_structure_has_field (constants, "q-scales"))) {
      GST_ERROR ("Missing quantization scales coefficients!");
      goto cleanup;
    }

    qoffsets = gst_structure_get_value (constants, "q-offsets");
    qscales = gst_structure_get_value (constants, "q-scales");
    n_tensors = GST_ML_INFO_N_TENSORS (&(submodule->mlinfo));

    if (!(success = (gst_value_array_get_size (qoffsets) == n_tensors))) {
      GST_ERROR ("Expecting %u dequantization offsets entries but received "
          "only %u!", n_tensors, gst_value_array_get_size (qoffsets));
      goto cleanup;
    } else if (!(success = (gst_value_array_get_size (qscales) == n_tensors))) {
      GST_ERROR ("Expecting %u dequantization scales entries but received "
          "only %u!", n_tensors, gst_value_array_get_size (qscales));
      goto cleanup;
    }

    for (idx = 0; idx < n_tensors; idx++) {
      submodule->qoffsets[idx] =
          g_value_get_double (gst_value_array_get_value (qoffsets, idx));
      submodule->qscales[idx] =
          g_value_get_double (gst_value_array_get_value (qscales, idx));
    }
  }

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

  if (GST_ML_INFO_N_TENSORS (&(submodule->mlinfo)) == 3) {
    gst_ml_module_parse_tripleblock_frame (submodule, predictions, mlframe);
  } else if (GST_ML_INFO_N_TENSORS (&(submodule->mlinfo)) == 1) {
    gst_ml_module_parse_monoblock_tensors (submodule, predictions, mlframe);
  } else {
    GST_ERROR ("Ml frame with unsupported post-processing procedure!");
    return FALSE;
  }

  return TRUE;
}
