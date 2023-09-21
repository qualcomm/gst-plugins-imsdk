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

// Non-maximum Suppression (NMS) threshold (50%).
#define INTERSECTION_THRESHOLD 0.5F

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GUINT8_PTR_CAST(data)       ((guint8*) data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Offset/Scale value for dequantizing lite-model_east-text-detector_int8_2.tflite
#define DEFAULT_SCORE_OFFSET     0
#define DEFAULT_SCORE_SCALE      0.00390625
#define DEFAULT_BOX_OFFSET       0
#define DEFAULT_BOX_SCALE        1.577793836593628

// MODULE_CAPS support input dim [32, 32] -> [1920, 1088]
#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < < 1, [8, 480], [8, 480], [1, 5] >, < 1, [8, 480], [8, 480], [1, 5] > > ;"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

// Offset/Score offset value for dequantizing.
// TODO: optimize
static gfloat    s_offset = DEFAULT_SCORE_OFFSET;
static gfloat    s_scale = DEFAULT_SCORE_SCALE;
static gfloat    b_offset = DEFAULT_BOX_OFFSET;
static gfloat    b_scale = DEFAULT_BOX_SCALE;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // List of prediction labels.
  GHashTable *labels;
  // Confidence threshold value.
  gfloat     threshold;
};

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

static inline gfloat
gst_ml_module_get_dequant_value (void * pdata, GstMLType mltype, guint idx,
    gfloat offset, gfloat scale)
{
  switch (mltype) {
    case GST_ML_TYPE_UINT8:
      return ((GUINT8_PTR_CAST (pdata))[idx] - offset) * scale;
    case GST_ML_TYPE_FLOAT32:
      return (GFLOAT_PTR_CAST (pdata))[idx];
    default:
      break;
  }
  return 0.0;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstProtectionMeta *pmeta = NULL;
  gint sar_n = 1, sar_d = 1, nms = -1;
  guint s_idx = 0, b_idx = 0, n_rows = 0, n_cols = 0, x = 0, y = 0;
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GArray *predictions = (GArray *) output;
  gpointer scores = NULL, geometry = NULL;
  gfloat x0 = 0, x1 = 0, x2 = 0, x3 = 0, angle = 0, cos_angle = 0, sin_angle = 0;
  gfloat confidence = 0, h = 0, w = 0;
  GstMLType mltype = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  if (!gst_ml_info_is_equal (&(mlframe->info), &(submodule->mlinfo))) {
    GST_ERROR ("ML frame with unsupported layout!");
    return FALSE;
  }

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);
  }

  mltype = GST_ML_INFO_TYPE (&mlframe->info);
  n_rows = GST_ML_FRAME_DIM (mlframe, 0, 1);
  n_cols = GST_ML_FRAME_DIM (mlframe, 0, 2);
  if (GST_ML_FRAME_DIM(mlframe,0,3) == 1) {
    scores = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
    geometry = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
  } else {
    scores = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
    geometry = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  }

  for (y = 0; y < n_rows; y++) {
    for (x = 0; x < n_cols; x++, s_idx++, b_idx += 5) {
      GstLabel *label = NULL;
      GstMLPrediction prediction = { 0, };
      confidence = gst_ml_module_get_dequant_value (scores, mltype,
          s_idx, s_offset, s_scale);

      // Discard results below the minimum score threshold.
      if (confidence < submodule->threshold)
        continue;

      // Extracting the derive rotated boxes surround text
      x0 = gst_ml_module_get_dequant_value (geometry, mltype, b_idx,
          b_offset, b_scale);
      x1 = gst_ml_module_get_dequant_value (geometry, mltype, b_idx + 1,
          b_offset, b_scale);
      x2 = gst_ml_module_get_dequant_value (geometry, mltype, b_idx + 2,
          b_offset, b_scale);
      x3 = gst_ml_module_get_dequant_value (geometry, mltype, b_idx + 3,
          b_offset, b_scale);

      // Extracting the rotation angle then computing the sine and cosine
      angle = gst_ml_module_get_dequant_value (geometry, mltype, b_idx + 4,
          b_offset, b_scale);
      cos_angle = cos (angle);
      sin_angle = sin (angle);

      // Using the geo volume to get the width and height bounding box
      h = x0 + x2;
      w = x1 + x3;

      // Compute coordinates of text prediction bounding box
      prediction.right = (x * 4 + (cos_angle * x1) + (sin_angle * x2));
      prediction.bottom = (y * 4 - (sin_angle * x1) + (cos_angle * x2));
      prediction.left = (prediction.right - w);
      prediction.top = (prediction.bottom - h);

      prediction.confidence = confidence * 100.0F;
      // Adjust bounding box dimensions with extracted source aspect ratio.
      gst_ml_prediction_transform_dimensions (
          &prediction, sar_n, sar_d, n_cols * 4, n_rows * 4);

      // Discard results with out of region coordinates.
      if ((prediction.top > 1.0)   || (prediction.left > 1.0) ||
         (prediction.bottom > 1.0) || (prediction.right > 1.0))
        continue;

      label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (0));
      prediction.label = g_strdup (label ? label->name : "Text");
      prediction.color = label ? label->color : 0x00FF00FF;

      // Non-Max Suppression (NMS) algorithm.
      nms = gst_ml_non_max_suppression (&prediction, predictions);

      // If the NMS result is -2 don't add the prediction to the list.
      if (nms == (-2)) {
        g_free (prediction.label);
        continue;
      }

      GST_LOG ("Box[y1,x1,y2,x2]=[%f, %f, %f, %f]. Label: %s. Confidence: %f",
          prediction.top, prediction.left, prediction.bottom, prediction.right,
          prediction.label, prediction.confidence);

      // If the NMS result is above -1 remove the entry with the nms index.
      if (nms >= 0)
        predictions = g_array_remove_index (predictions, nms);

      predictions = g_array_append_val (predictions, prediction);
    }
  }
  return TRUE;
}
