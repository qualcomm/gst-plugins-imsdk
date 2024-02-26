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

#include <math.h>
#include <stdio.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-detection.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

// Minimum relative size of the bounding box must occupy in the image.
#define BBOX_SIZE_THRESHOLD    0.01F

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < <1, 60, 80, 1 >, < 1, 60, 80, 1 >, < 1, 60, 80, 10 >, < 1, 60, 80, 4 > >; " \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < <1, 120, 160, 1 >, < 1, 120, 160, 10 >, < 1, 120, 160, 4 > >; "

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
  GArray *predictions = (GArray *)output;
  GstProtectionMeta *pmeta = NULL;
  GstMLBoxPrediction *prediction = NULL;
  gfloat *scores = NULL, *hm_pool = NULL, *landmarks = NULL, *bboxes = NULL;
  GstVideoRectangle region = { 0, };
  gfloat size = 0;
  guint idx = 0, num = 0, n_classes = 0, n_blocks = 0, n_tensor = 0, block_size = 0;
  gint nms = -1, cx = 0, cy = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

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

  n_tensor = GST_ML_FRAME_N_TENSORS (mlframe);

  // TODO: First tensor represents some kind of confidence scores.
  scores = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));

  if (n_tensor == 4) {
    // TODO: Second tensor represents some kind of confidence scores.
    hm_pool = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
    // Third tensor represents the landmarks (left eye, right ear, etc.).
    landmarks = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 2));
    // Fourh tensor represents the coordinates of the bounding boxes.
    bboxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 3));
  } else if (n_tensor == 3) {
    // Second tensor represents the landmarks (left eye, right ear, etc.).
    landmarks = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
    // Thrid tensor represents the coordinates of the bounding boxes.
    bboxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 2));
  }

  // The 4th tensor dimension represents the number of detection classes.
  n_classes = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Calculate the number of macroblocks.
  n_blocks = GST_ML_FRAME_DIM (mlframe, 0, 1) * GST_ML_FRAME_DIM (mlframe, 0, 2);

  block_size = submodule->inwidth / GST_ML_FRAME_DIM (mlframe, 0, 2);

  for (idx = 0; idx < n_blocks; ++idx) {
    GstMLLabel *label = NULL;
    GstMLBoxEntry entry = { 0, };

    // Discard invalid results.
    if (n_tensor == 4 && hm_pool != NULL && scores[idx] != hm_pool[idx])
      continue;

    // Discard results below the minimum score threshold.
    if (scores[idx] < submodule->threshold)
      continue;

    // Calculate the centre coordinates.
    cx = (idx / n_classes) % GST_ML_FRAME_DIM (mlframe, 0, 2);
    cy = (idx / n_classes) / GST_ML_FRAME_DIM (mlframe, 0, 2);

    entry.left = (cx - bboxes[(idx * 4)]) * block_size;
    entry.top = (cy - bboxes[(idx * 4) + 1]) * block_size;
    entry.right = (cx + bboxes[(idx * 4) + 2]) * block_size;
    entry.bottom = (cy + bboxes[(idx * 4) + 3]) * block_size;

    // Adjust bounding box dimensions with SAR and input tensor resolution.
    gst_ml_box_transform_dimensions (&entry, &region);

    size = (entry.right - entry.left) *
        (entry.bottom - entry.top);

    // Discard results below the minimum bounding box size.
    if (size < BBOX_SIZE_THRESHOLD)
      continue;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (idx % n_classes));

    entry.confidence = scores[idx] * 100.0;
    entry.name = g_quark_from_string (label ? label->name : "unknown");
    entry.color = label ? label->color : 0x000000FF;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_box_non_max_suppression (&entry, prediction->entries);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (prediction->entries, nms);

    // TODO: Enchance predictions to support landmarks.
    for (num = 0; num < 5; ++num) {
      gfloat lx = 0.0, ly = 0.0;

      if ((idx % n_classes) != 0)
        continue;

      lx = (cx + landmarks[idx / n_classes * 10 + num]) * block_size;
      ly = (cy + landmarks[idx / n_classes * 10 + num + 5]) * block_size;
      GST_INFO ("Ladnmark: [ %.2f %.2f ] ", lx, ly);
    }

    prediction->entries = g_array_append_val (prediction->entries, entry);
  }

  g_array_sort (prediction->entries, (GCompareFunc) gst_ml_box_compare_entries);

  return TRUE;
}
