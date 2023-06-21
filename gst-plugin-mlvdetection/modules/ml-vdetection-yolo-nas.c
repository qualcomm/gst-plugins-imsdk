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

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Output dimensions depends on input[w, h] and n_classes.
// Dimensions format: <<1, D, n_classes>, <1, D, 4>>
// D = w/32 * h/32 + w/16 * h/16 + w/8 * h/8
// MODULE_CAPS support input dim [32, 32] -> [1920, 1088]. Number class 1 -> 1001
#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < <1, [21, 42840], [1, 1001]>, <1, [21, 42840], 4> >; "

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
  GstProtectionMeta *pmeta = NULL;
  GstLabel *label = NULL;
  gint sar_n = 1, sar_d = 1, nms = -1;
  gfloat *bbox_data = NULL, *class_data = NULL;
  guint n_classes = 0, n_rows = 0, in_height = 0, in_width = 0, idx = 0;

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
    gst_structure_get_uint (pmeta->info, "input-tensor-height", &in_height);
    gst_structure_get_uint (pmeta->info, "input-tensor-width", &in_width);
  }

  // The 2nd dimension represents the number of rows.
  n_rows = GST_ML_FRAME_DIM (mlframe, 0, 1);

  if (GST_ML_FRAME_DIM (mlframe, 0, 2) == 4) {
    //Tensor dimensions looks like: <1, 8400, 4>, <1, 8400, 80>
    bbox_data = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));
    class_data = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
    n_classes = GST_ML_FRAME_DIM (mlframe, 1, 2);
  } else {
    //Tensor dimensions looks like: <1, 8400, 80>, <1, 8400, 4>
    bbox_data = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
    class_data = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));
    n_classes = GST_ML_FRAME_DIM (mlframe, 0, 2);
  }

  GST_LOG ("Input size[%d:%d] SAR[%d/%d]. n_rows: %d. n_classes: %d"
      ". threshold: %f", in_height, in_width, sar_n, sar_d, n_rows,
      n_classes, submodule->threshold);

  for (idx = 0; idx < n_rows; idx++) {
    GstMLPrediction prediction = { 0, };
    gfloat confidence = 0;
    guint num = 0, class_idx = 0;
    gfloat *pbbox = bbox_data + idx * 4;
    gfloat *pclass = class_data + idx * n_classes;

    // Find the class ID with the highest confidence.
    for (num = 0; num < n_classes; num++) {
      if (pclass[num] <= confidence)
        continue;

      confidence = pclass[num];
      class_idx = num;
    }

    // Discard results below the minimum score threshold.
    if (confidence < submodule->threshold)
      continue;

    label = g_hash_table_lookup (
        submodule->labels, GUINT_TO_POINTER (class_idx));

    prediction.confidence = confidence * 100.0F;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000F;

    prediction.top = pbbox[1];
    prediction.left = pbbox[0];
    prediction.bottom = pbbox[3];
    prediction.right = pbbox[2];

    GST_LOG ("Box[y1,x1,y2,x2]=[%f, %f, %f, %f]. Label: %s. Confidence: %f",
        prediction.top, prediction.left, prediction.bottom, prediction.right,
        prediction.label, prediction.confidence);

    // Adjust bounding box dimensions with extracted source aspect ratio.
    gst_ml_prediction_transform_dimensions (
        &prediction, sar_n, sar_d, in_width, in_height);

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_non_max_suppression (&prediction, predictions);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)) {
      g_free (prediction.label);
      continue;
    }

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (predictions, nms);

    predictions = g_array_append_val (predictions, prediction);
  }

  return TRUE;
}
