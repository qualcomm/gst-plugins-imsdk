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


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GFLOAT_PTR_CAST(data)       ((gfloat*)data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 10, 4 >, < 1, 10 >, < 1, 10 >, < 1 > >; " \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 10, 4 >, < 1, 10 >, < 1, 10 > >"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  GHashTable *labels;
};

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
  static volatile gsize inited = 0;

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

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  input = gst_structure_get_string (settings, GST_ML_MODULE_OPT_LABELS);

  submodule->labels = gst_ml_load_labels (input);
  g_return_val_if_fail (submodule->labels != NULL, FALSE);

  gst_structure_free (settings);
  return TRUE;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GArray *predictions = (GArray *) output;
  GstProtectionMeta *pmeta = NULL;
  gfloat *bboxes = NULL, *classes = NULL, *scores = NULL;
  guint idx = 0, n_entries = 0, sar_n = 1, sar_d = 1;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  bboxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));
  classes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
  scores = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 2));

  if (GST_ML_FRAME_N_TENSORS (mlframe) == 4) {
    gfloat *n_boxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 3));
    n_entries = n_boxes[0];
  } else {
    gsize bytes = GST_ML_FRAME_BLOCK_SIZE (mlframe, 2);
    n_entries = bytes / gst_ml_type_get_size (GST_ML_TYPE_FLOAT32);
  }

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
  }

  for (idx = 0; idx < n_entries; idx++) {
    GstLabel *label = NULL;
    GstMLPrediction prediction = { 0, };
    gfloat confidence = scores[idx] * 100;

    // Discard results below 1% confidence.
    if (confidence <= 1.0)
      continue;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (classes[idx] + 1));

    prediction.confidence = confidence;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000FF;

    prediction.top = bboxes[(idx * 4)];
    prediction.left = bboxes[(idx * 4)  + 1];
    prediction.bottom = bboxes[(idx * 4) + 2];
    prediction.right = bboxes[(idx * 4) + 3];

    // Adjust bounding box dimensions with extracted source aspect ratio.
    if (sar_n > sar_d) {
      gdouble coeficient = 0.0;

      gst_util_fraction_to_double (sar_n, sar_d, &coeficient);

      prediction.top *= coeficient;
      prediction.bottom *= coeficient;
    } else if (sar_n < sar_d) {
      gdouble coeficient = 0.0;

      gst_util_fraction_to_double (sar_d, sar_n, &coeficient);

      prediction.left *= coeficient;
      prediction.right *= coeficient;
    }

    predictions = g_array_append_val (predictions, prediction);
  }

  g_array_sort (predictions, gst_ml_compare_predictions);
  return TRUE;
}
