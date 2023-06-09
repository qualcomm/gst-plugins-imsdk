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

#include "ml-video-classification-module.h"


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GINT32_PTR_CAST(data)       ((gint32*) data)
#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { UINT8, INT32, FLOAT32 }, " \
    "dimensions = (int) < < 1, [ 1000, 1001 ] > >"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // List of prediction labels.
  GHashTable *labels;
  // Confidence threshold value.
  gdouble    threshold;
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
  submodule->threshold = threshold;

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
  guint8 *data = NULL;
  guint idx = 0, n_inferences = 0;
  gdouble confidence = 0.0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  if (!gst_ml_info_is_equal (&(mlframe->info), &(submodule->mlinfo))) {
    GST_ERROR ("ML frame with unsupported layout!");
    return FALSE;
  }

  n_inferences = GST_ML_FRAME_DIM (mlframe, 0, 1);
  data = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);

  // Fill the prediction table.
  for (idx = 0; idx < n_inferences; ++idx) {
    GstLabel *label = NULL;
    GstMLPrediction prediction = { 0 };

    switch (GST_ML_FRAME_TYPE (mlframe)) {
      case GST_ML_TYPE_UINT8:
        confidence = data[idx] * (100.0 / G_MAXUINT8);
        break;
      case GST_ML_TYPE_INT32:
        confidence = GINT32_PTR_CAST (data)[idx];
        break;
      case GST_ML_TYPE_FLOAT32:
        confidence = GFLOAT_PTR_CAST (data)[idx] * 100;
        break;
      default:
        GST_ERROR ("Unsupported tensor type!");
        return FALSE;
    }

    // Discard results with confidence below the set threshold.
    if (confidence < submodule->threshold)
      continue;

    label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (idx));

    prediction.confidence = confidence;
    prediction.label = g_strdup (label ? label->name : "unknown");
    prediction.color = label ? label->color : 0x000000FF;

    predictions = g_array_append_val (predictions, prediction);
  }

  return TRUE;
}
