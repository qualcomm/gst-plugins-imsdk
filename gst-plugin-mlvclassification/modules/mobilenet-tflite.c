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

#include "ml-video-classification-module.h"

#include <gst/ml/gstmlmeta.h>


#define CAST_TO_GINT32(data) ((gint32*) data)
#define CAST_TO_GFLOAT(data) ((gfloat*) data)

typedef struct _GstPrivateModule GstPrivateModule;
typedef struct _GstLabel GstLabel;

struct _GstPrivateModule {
  GHashTable *labels;
};

struct _GstLabel {
  gchar *name;
  guint color;
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

gpointer
gst_ml_video_classification_module_init (const gchar * labels)
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
gst_ml_video_classification_module_deinit (gpointer instance)
{
  GstPrivateModule *module = instance;

  if (NULL == module)
    return;

  g_hash_table_destroy (module->labels);
  g_slice_free (GstPrivateModule, module);
}

gboolean
gst_ml_video_classification_module_process (gpointer instance,
    GstMLFrame * frame, GList ** predictions)
{
  GstPrivateModule *module = instance;
  guint8 *data = NULL;
  guint idx = 0, n_inferences = 0;
  gdouble value = 0.0;

  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  n_inferences = GST_ML_FRAME_DIM (frame, 0, 1);
  data = GST_ML_FRAME_BLOCK_DATA (frame, 0);

  // Fill the prediction table.
  for (idx = 0; idx < n_inferences; ++idx) {
    GstMLPrediction *prediction = NULL;
    GstLabel *label = NULL;

    switch (GST_ML_FRAME_TYPE (frame)) {
      case GST_ML_TYPE_UINT8:
        value = data[idx] * (100.0 / G_MAXUINT8);
        break;
      case GST_ML_TYPE_INT32:
        value = CAST_TO_GINT32 (data)[idx];
        break;
      case GST_ML_TYPE_FLOAT32:
        value = CAST_TO_GFLOAT (data)[idx] * 100;
        break;
      default:
        GST_ERROR ("Unsupported tensor type!");
        return FALSE;
    }

    // Discard results below 10% confidence.
    if (value <= 10.0)
      continue;

    label = g_hash_table_lookup (module->labels, GUINT_TO_POINTER (idx + 1));

    prediction = gst_ml_prediction_new ();

    prediction->confidence = value;
    prediction->label = g_strdup (label ? label->name : "unknown");
    prediction->color = label ? label->color : 0x000000FF;

    *predictions = g_list_insert_sorted (
        *predictions, prediction, gst_ml_compare_predictions);
  }

  return TRUE;
}
