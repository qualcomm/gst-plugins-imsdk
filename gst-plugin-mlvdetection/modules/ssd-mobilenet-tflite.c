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
 */

#include "ml-video-detection-module.h"

#include <gst/ml/gstmlmeta.h>


#define CAST_TO_GFLOAT(data) ((gfloat*)data)

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
  GstPrivateModule *module = instance;

  if (NULL == module)
    return;

  g_hash_table_destroy (module->labels);
  g_slice_free (GstPrivateModule, module);
}

gboolean
gst_ml_video_detection_module_process (gpointer instance, GstBuffer * buffer,
    GList ** predictions)
{
  GstPrivateModule *module = instance;
  GstProtectionMeta *pmeta = NULL;
  GstMapInfo bboxes, classes, scores, n_boxes;
  guint idx = 0, sar_n = 1, sar_d = 1;

  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  if (gst_buffer_n_memory (buffer) != 4) {
    GST_ERROR ("Expecting 4 tensor memory blocks but received %u!",
        gst_buffer_n_memory (buffer));
    return FALSE;
  }

  for (idx = 0; idx < gst_buffer_n_memory (buffer); idx++) {
    GstMLTensorMeta *mlmeta = NULL;

    if (!(mlmeta = gst_buffer_get_ml_tensor_meta_id (buffer, idx))) {
      GST_ERROR ("Buffer has no ML meta for tensor %u!", idx);
      return FALSE;
    } else if (mlmeta->type != GST_ML_TYPE_FLOAT32) {
      GST_ERROR ("Buffer has unsupported type for tensor %u!", idx);
      return FALSE;
    }
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &bboxes, GST_MAP_READ)) {
    GST_ERROR ("Failed to map bboxes memory block!");
    return FALSE;
  }

  if (!gst_buffer_map_range (buffer, 1, 1, &classes, GST_MAP_READ)) {
    GST_ERROR ("Failed to map classes memory block!");

    gst_buffer_unmap (buffer, &bboxes);
    return FALSE;
  }

  if (!gst_buffer_map_range (buffer, 2, 1, &scores, GST_MAP_READ)) {
    GST_ERROR ("Failed to map scores memory block!");

    gst_buffer_unmap (buffer, &classes);
    gst_buffer_unmap (buffer, &bboxes);
    return FALSE;
  }

  if (!gst_buffer_map_range (buffer, 3, 1, &n_boxes, GST_MAP_READ)) {
    GST_ERROR ("Failed to map n_boxes memory block!");

    gst_buffer_unmap (buffer, &scores);
    gst_buffer_unmap (buffer, &classes);
    gst_buffer_unmap (buffer, &bboxes);
    return FALSE;
  }

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (buffer)) != NULL) {
    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, g_quark_to_string (1)));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, g_quark_to_string (1)));
  }

  for (idx = 0; idx < CAST_TO_GFLOAT (n_boxes.data)[0]; idx++) {
    GstMLPrediction *prediction = NULL;
    GstLabel *label = NULL;
    gdouble value = CAST_TO_GFLOAT (scores.data)[idx] * 100;

    // Discard results below 1% confidence.
    if (value <= 1.0)
      continue;

    label = g_hash_table_lookup (module->labels,
        GUINT_TO_POINTER (CAST_TO_GFLOAT (classes.data)[idx] + 1));

    prediction = g_new0 (GstMLPrediction, 1);

    prediction->confidence = value;
    prediction->label = g_strdup (label ? label->name : "unknown");
    prediction->color = label ? label->color : 0x000000FF;

    prediction->top = CAST_TO_GFLOAT (bboxes.data)[(idx * 4)];
    prediction->left = CAST_TO_GFLOAT (bboxes.data)[(idx * 4)  + 1];
    prediction->bottom = CAST_TO_GFLOAT (bboxes.data)[(idx * 4) + 2];
    prediction->right = CAST_TO_GFLOAT (bboxes.data)[(idx * 4) + 3];

    // Adjust bounding box dimensions with extracted source aspect ratio.
    if (sar_n > sar_d) {
      gdouble coeficient = 0.0;

      gst_util_fraction_to_double (sar_n, sar_d, &coeficient);

      prediction->top *= coeficient;
      prediction->bottom *= coeficient;
    } else if (sar_n < sar_d) {
      gdouble coeficient = 0.0;

      gst_util_fraction_to_double (sar_d, sar_n, &coeficient);

      prediction->left *= coeficient;
      prediction->right *= coeficient;
    }

    *predictions = g_list_insert_sorted (
        *predictions, prediction, gst_ml_compare_predictions);
  }

  gst_buffer_unmap (buffer, &n_boxes);
  gst_buffer_unmap (buffer, &scores);
  gst_buffer_unmap (buffer, &classes);
  gst_buffer_unmap (buffer, &bboxes);

  return TRUE;
}
