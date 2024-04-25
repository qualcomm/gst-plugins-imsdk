/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc All rights reserved.
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
 *     * Neither the name of Qualcomm Innovation Center, Inc nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
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

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GINT8_PTR_CAST(data)        ((gint8*) data)
#define GUINT8_PTR_CAST(data)       ((guint8*) data)

// MODULE_CAPS support input dim [32, 32] -> [1920, 1088] Number class 1 -> 1001
#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, [21, 42840], 4>, <1, [21, 42840]>, <1, [21, 42840]> >; " \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, 4, [21, 42840]>, <1, [1, 1001], [21, 42840]> >; " \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, [5, 1005], [21, 42840]> > "

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

  // Offset values for each of the tensors for dequantization of some tensors.
  gdouble    qoffsets[GST_ML_MAX_TENSORS];
  // Scale values for each of the tensors for dequantization of some tensors.
  gdouble    qscales[GST_ML_MAX_TENSORS];
};

static inline gdouble
gst_ml_module_get_dequant_value (void * pdata, GstMLType mltype, guint idx,
    gdouble offset, gdouble scale)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      return ((GINT8_PTR_CAST (pdata))[idx] - offset) * scale;
    case GST_ML_TYPE_UINT8:
      return ((GUINT8_PTR_CAST (pdata))[idx] - offset) * scale;
    case GST_ML_TYPE_FLOAT32:
      return (GFLOAT_PTR_CAST (pdata))[idx];
    default:
      break;
  }
  return 0.0;
}

static inline gint
gst_ml_module_compare_values (void * data, GstMLType mltype,
    guint l_idx, guint r_idx)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      return GINT8_PTR_CAST (data)[l_idx] > GINT8_PTR_CAST (data)[r_idx] ? 1 : 0;
    case GST_ML_TYPE_UINT8:
      return GUINT8_PTR_CAST (data)[l_idx] > GUINT8_PTR_CAST (data)[r_idx] ? 1 : 0;
    case GST_ML_TYPE_FLOAT32:
      return GFLOAT_PTR_CAST (data)[l_idx] > GFLOAT_PTR_CAST (data)[r_idx] ? 1 : 0;
    default:
      break;
  }
  return 0;
}

static void
gst_ml_module_parse_monoblock_frame (GstMLSubModule * submodule,
    GArray * predictions, GstMLFrame * mlframe)
{
  GstProtectionMeta *pmeta = NULL;
  GstLabel *label = NULL;
  gpointer bboxes = NULL, scores = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gint sar_n = 1, sar_d = 1, nms = -1;
  guint n_classes = 0, n_paxels = 0, height = 0, width = 0, idx = 0, num = 0;
  gdouble cx = 0, cy = 0, w = 0, h = 0, confidence = 0.0;

  pmeta = gst_buffer_get_protection_meta (mlframe->buffer);

  // Extract the SAR (Source Aspect Ratio).
  gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);

  // Extract the dimensions of the input tensor that produced the output tensors.
  gst_structure_get_uint (pmeta->info, "input-tensor-height", &height);
  gst_structure_get_uint (pmeta->info, "input-tensor-width", &width);

  mltype = GST_ML_FRAME_TYPE (mlframe);
  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 2);

  // We subtract 4 because the last 4 are the bbox coordinates.
  n_classes = GST_ML_FRAME_DIM (mlframe, 0, 1) - 4;

  bboxes = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);

  if (mltype == GST_ML_TYPE_FLOAT32)
    scores = GFLOAT_PTR_CAST (bboxes) + 4 * n_paxels;
  else if (mltype == GST_ML_TYPE_UINT8)
    scores = GUINT8_PTR_CAST (bboxes) + 4 * n_paxels;
  else if (mltype == GST_ML_TYPE_INT8)
    scores = GINT8_PTR_CAST (bboxes) + 4 * n_paxels;

  for (idx = 0; idx < n_paxels; idx++) {
    GstMLPrediction prediction = { 0, };
    guint class_idx = 0, id = 0;

    // Initial position ID of the class index.
    id = idx;

    // Find the position of the class index with the highest score in current paxel.
    for (num = (idx + n_paxels); num < (n_classes * n_paxels); num += n_paxels)
      id = (gst_ml_module_compare_values (scores, mltype, num, id) > 0) ? num : id;

    // Get the class index from the position ID.
    class_idx = id / n_paxels;

    // Dequantize the class confidence.
    confidence = gst_ml_module_get_dequant_value (scores, mltype, id,
        submodule->qoffsets[0], submodule->qscales[0]);

    // Discard results below the minimum score threshold.
    if (confidence < submodule->threshold)
      continue;

    // Get bounding box centre X, centre Y, width, height coordinates parameters.
    cx = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (0 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);
    cy = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (1 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);
    w  = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (2 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);
    h  = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (3 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);

    GST_LOG ("CX x CY[%f, %f] W x H: [%f, %f] Class: %u Confidence: %f",
        cx, cy, w, h, class_idx, confidence);

    prediction.top =  cy - h / 2.0f;
    prediction.left = cx - w / 2.0f;
    prediction.bottom = prediction.top + h;
    prediction.right = prediction.left + w;

    GST_LOG ("Box[%f, %f, %f, %f] Class: %u Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        class_idx, confidence);

    // Adjust bounding box dimensions with extracted source aspect ratio.
    gst_ml_prediction_transform_dimensions (&prediction, sar_n, sar_d,
        width, height);

    // Discard results with out of region coordinates.
    if ((prediction.top > 1.0)   || (prediction.left > 1.0) ||
        (prediction.bottom > 1.0) || (prediction.right > 1.0) ||
        (prediction.top < 0.0)    || (prediction.left < 0.0) ||
        (prediction.bottom < 0.0) || (prediction.right < 0.0))
      continue;

    label = g_hash_table_lookup (
        submodule->labels, GUINT_TO_POINTER (class_idx));

    prediction.confidence = confidence * 100.0F;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000F;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_non_max_suppression (&prediction, predictions);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)) {
      g_free (prediction.label);
      continue;
    }

    GST_TRACE ("Box[%f, %f, %f, %f] Label: %s Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        prediction.label, prediction.confidence);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (predictions, nms);

    predictions = g_array_append_val (predictions, prediction);
  }
}

static void
gst_ml_module_parse_dualblock_frame (GstMLSubModule * submodule,
    GArray * predictions, GstMLFrame * mlframe)
{
  GstProtectionMeta *pmeta = NULL;
  GstLabel *label = NULL;
  gpointer bboxes = NULL, scores = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gint sar_n = 1, sar_d = 1, nms = -1;
  guint n_classes = 0, n_paxels = 0, height = 0, width = 0, idx = 0, num = 0;
  gdouble cx = 0, cy = 0, w = 0, h = 0, confidence = 0.0;

  pmeta = gst_buffer_get_protection_meta (mlframe->buffer);

  // Extract the SAR (Source Aspect Ratio).
  gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);

  // Extract the dimensions of the input tensor that produced the output tensors.
  gst_structure_get_uint (pmeta->info, "input-tensor-height", &height);
  gst_structure_get_uint (pmeta->info, "input-tensor-width", &width);

  mltype = GST_ML_FRAME_TYPE (mlframe);
  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 2);
  n_classes = GST_ML_FRAME_DIM (mlframe, 1, 1);

  bboxes = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  scores = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);

  for (idx = 0; idx < n_paxels; idx++) {
    GstMLPrediction prediction = { 0, };
    guint class_idx = 0, id = 0;

    // Initial position ID of the class index.
    id = idx;

    // Find the position of the class index with the highest score in current paxel.
    for (num = (idx + n_paxels); num < (n_classes * n_paxels); num += n_paxels)
      id = (gst_ml_module_compare_values (scores, mltype, num, id) > 0) ? num : id;

    // Get the class index from the position ID.
    class_idx = id / n_paxels;

    // Dequantize the class confidence.
    confidence = gst_ml_module_get_dequant_value (scores, mltype, id,
        submodule->qoffsets[1], submodule->qscales[1]);

    // Discard results below the minimum score threshold.
    if (confidence < submodule->threshold)
      continue;

    // Get bounding box centre X, centre Y, width, height coordinates parameters.
    cx = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (0 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);
    cy = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (1 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);
    w  = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (2 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);
    h  = gst_ml_module_get_dequant_value (bboxes, mltype, idx + (3 * n_paxels),
        submodule->qoffsets[0], submodule->qscales[0]);

    GST_LOG ("CX x CY[%f, %f] W x H: [%f, %f] Class: %u Confidence: %f",
        cx, cy, w, h, class_idx, confidence);

    prediction.top =  cy - h / 2.0f;
    prediction.left = cx - w / 2.0f;
    prediction.bottom = prediction.top + h;
    prediction.right = prediction.left + w;

    GST_LOG ("Box[%f, %f, %f, %f] Class: %u Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        class_idx, confidence);

    // Adjust bounding box dimensions with extracted source aspect ratio.
    gst_ml_prediction_transform_dimensions (&prediction, sar_n, sar_d,
        width, height);

    // Discard results with out of region coordinates.
    if ((prediction.top > 1.0)   || (prediction.left > 1.0) ||
        (prediction.bottom > 1.0) || (prediction.right > 1.0) ||
        (prediction.top < 0.0)    || (prediction.left < 0.0) ||
        (prediction.bottom < 0.0) || (prediction.right < 0.0))
      continue;

    label = g_hash_table_lookup (
        submodule->labels, GUINT_TO_POINTER (class_idx));

    prediction.confidence = confidence * 100.0F;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000F;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_non_max_suppression (&prediction, predictions);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)) {
      g_free (prediction.label);
      continue;
    }

    GST_TRACE ("Box[%f, %f, %f, %f] Label: %s Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        prediction.label, prediction.confidence);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (predictions, nms);

    predictions = g_array_append_val (predictions, prediction);
  }
}

static void
gst_ml_module_parse_tripleblock_frame (GstMLSubModule * submodule,
    GArray * predictions, GstMLFrame * mlframe)
{
  GstProtectionMeta *pmeta = NULL;
  GstLabel *label = NULL;
  gpointer bboxes = NULL, scores = NULL, classes = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gint sar_n = 1, sar_d = 1, nms = -1;
  guint n_paxels = 0, height = 0, width = 0, idx = 0, class_idx = 0;
  gfloat confidence = 0;

  pmeta = gst_buffer_get_protection_meta (mlframe->buffer);

  // Extract the SAR (Source Aspect Ratio).
  gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);

  // Extract the dimensions of the input tensor that produced the output tensors.
  gst_structure_get_uint (pmeta->info, "input-tensor-height", &height);
  gst_structure_get_uint (pmeta->info, "input-tensor-width", &width);

  mltype = GST_ML_FRAME_TYPE (mlframe);
  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 1);

  bboxes = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  scores = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
  classes = GST_ML_FRAME_BLOCK_DATA (mlframe, 2);

  for (idx = 0; idx < n_paxels; idx++) {
    GstMLPrediction prediction = { 0, };

    confidence = gst_ml_module_get_dequant_value (scores, mltype, idx,
        submodule->qoffsets[1], submodule->qscales[1]);
    class_idx = gst_ml_module_get_dequant_value (classes, mltype, idx,
        submodule->qoffsets[2], submodule->qscales[2]);

    // Discard results below the minimum score threshold.
    if (confidence < submodule->threshold)
      continue;

    prediction.left = gst_ml_module_get_dequant_value (bboxes, mltype,
        (idx * 4) + 0, submodule->qoffsets[0], submodule->qscales[0]);
    prediction.top = gst_ml_module_get_dequant_value (bboxes, mltype,
        (idx * 4) + 1, submodule->qoffsets[0], submodule->qscales[0]);
    prediction.right  = gst_ml_module_get_dequant_value (bboxes, mltype,
        (idx * 4) + 2, submodule->qoffsets[0], submodule->qscales[0]);
    prediction.bottom  = gst_ml_module_get_dequant_value (bboxes, mltype,
        (idx * 4) + 3, submodule->qoffsets[0], submodule->qscales[0]);

    GST_LOG ("Box[%f, %f, %f, %f] Class: %u Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        class_idx, confidence);

    // Adjust bounding box dimensions with extracted source aspect ratio.
    gst_ml_prediction_transform_dimensions (&prediction, sar_n, sar_d,
        width, height);

    // Discard results with out of region coordinates.
    if ((prediction.top > 1.0)   || (prediction.left > 1.0) ||
        (prediction.bottom > 1.0) || (prediction.right > 1.0) ||
        (prediction.top < 0.0)    || (prediction.left < 0.0) ||
        (prediction.bottom < 0.0) || (prediction.right < 0.0))
      continue;

    label = g_hash_table_lookup (
        submodule->labels, GUINT_TO_POINTER (class_idx));

    prediction.confidence = confidence * 100.0F;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000F;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_non_max_suppression (&prediction, predictions);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)) {
      g_free (prediction.label);
      continue;
    }

    GST_TRACE ("Box[%f, %f, %f, %f] Label: %s Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        prediction.label, prediction.confidence);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (predictions, nms);

    predictions = g_array_append_val (predictions, prediction);
  }
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

  if (!gst_ml_info_is_equal (&(mlframe->info), &(submodule->mlinfo))) {
    GST_ERROR ("ML frame with unsupported layout!");
    return FALSE;
  }

  if (GST_ML_INFO_N_TENSORS (&(submodule->mlinfo)) == 3) {
    gst_ml_module_parse_tripleblock_frame (submodule, predictions, mlframe);
  } else if (GST_ML_INFO_N_TENSORS (&(submodule->mlinfo)) == 2) {
    gst_ml_module_parse_dualblock_frame (submodule, predictions, mlframe);
  } else if (GST_ML_INFO_N_TENSORS (&(submodule->mlinfo)) == 1) {
    gst_ml_module_parse_monoblock_frame (submodule, predictions, mlframe);
  } else {
    GST_ERROR ("Ml frame with unsupported post-processing procedure!");
    return FALSE;
  }

  return TRUE;
}
