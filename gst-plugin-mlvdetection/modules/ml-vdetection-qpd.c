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

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Minimum relative size of the bounding box must occupy in the image.
#define BBOX_SIZE_THRESHOLD         400 // 20 x 20 pixels

//Person detection model output parameters
#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 120, 160, 3 >, < 1, 120, 160, 12 >, < 1, 120, 160, 34 >, < 1, 120, 160, 17 > >; "

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
  // List of of the names for the landmarks.
  GHashTable *landmarks;
  // Confidence threshold value.
  gfloat     threshold;
};

static GHashTable *
gst_ml_box_load_landmarks (const GValue * list)
{
  GHashTable *landmarks = NULL, *names = NULL;;
  GstStructure *structure = NULL, *lmkparams = NULL;
  const GValue *value = NULL;
  gchar *name = NULL;
  guint idx = 0, num = 0, id = 0, size = 0;

  g_return_val_if_fail (list != NULL, NULL);

  landmarks = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) g_hash_table_destroy);

  for (idx = 0; idx < gst_value_list_get_size (list); idx++) {
    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (list, idx)));

    if (!gst_structure_has_field (structure, "id")) {
      GST_DEBUG ("Structure does not contain 'id' field!");
      continue;
    }

    if ((value = gst_structure_get_value (structure, "landmarks")) == NULL) {
      GST_DEBUG ("Structure does not contain 'landmarks' field!");
      continue;
    }

    size = gst_value_array_get_size (value);
    names = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) g_free);

    for (num = 0; num < size; num++) {
      lmkparams = GST_STRUCTURE (
          g_value_get_boxed (gst_value_array_get_value (value, num)));

      name = g_strdup (gst_structure_get_name (lmkparams));
      name = g_strdelimit (name, "-", ' ');

      gst_structure_get_uint (lmkparams, "id", &id);
      g_hash_table_insert (names, GUINT_TO_POINTER (id), name);
    }

    gst_structure_get_uint (structure, "id", &id);
    g_hash_table_insert (landmarks, GUINT_TO_POINTER (id), names);
  }

  return landmarks;
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

  if (submodule->landmarks != NULL)
    g_hash_table_destroy (submodule->landmarks);

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

  submodule->landmarks = gst_ml_box_load_landmarks (&list);

  // Fill the landmarks for each label.
  if (!(success = (submodule->landmarks != NULL)))
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
  gfloat *scores = NULL, *landmarks = NULL, *bboxes = NULL, *lmkscores = NULL;
  GstVideoRectangle region = { 0, };
  gfloat confidence = 0.0;
  guint idx = 0, num = 0, id = 0, class_idx = 0, size = 0.0;
  guint n_classes = 0, n_landmarks = 0, n_paxels = 0, paxelsize = 0;
  gint nms = -1, cx = 0, cy = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLBoxPrediction, 0));
  prediction->info = pmeta->info;

  // Extract the dimensions of the input tensor that produced the output tensors.
  if (submodule->inwidth == 0 || submodule->inheight == 0) {
    gst_ml_structure_get_source_dimensions (pmeta->info, &(submodule->inwidth),
        &(submodule->inheight));
  }

  // Extract the source tensor region with actual data.
  gst_ml_structure_get_source_region (pmeta->info, &region);

  // First tensor represents confidence scores.
  scores = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));
  // Second tensor represents the coordinates of the bounding boxes.
  bboxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
  // Third tensor represents the landmarks coordinates.
  landmarks = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 2));
  // Fourth tensor represents landmark scores.
  lmkscores = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 3));

  // The 1st tensor dimension represents the number of detection classes.
  n_classes = GST_ML_FRAME_DIM (mlframe, 0, 3);
  // The 3rd tensor dimension represents the number of landmark X & Y pairs.
  n_landmarks = GST_ML_FRAME_DIM (mlframe, 2, 3) / 2;

  // Calculate the number of macroblocks (paxels).
  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 1) * GST_ML_FRAME_DIM (mlframe, 0, 2);
  // Calculate the dimension of the square macro block.
  paxelsize = submodule->inwidth / GST_ML_FRAME_DIM (mlframe, 2, 2);

  for (idx = 0; idx < (n_paxels * n_classes); idx++) {
    GstMLLabel *label = NULL;
    GstMLBoxEntry entry = { 0, };
    gfloat bbox[4] = { 0, }, x = 0.0, y = 0.0;
    guint lmk_idx = 0;

    confidence = scores[idx];

    // Discard results below the minimum score threshold.
    if (confidence < submodule->threshold)
      continue;

    class_idx = idx % n_classes;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (class_idx));

    if (label == NULL) {
      GST_TRACE ("Unknown label, skipping this entry.");
      continue;
    }

    // Calculate the centre coordinates.
    cx = (idx / n_classes) % GST_ML_FRAME_DIM (mlframe, 2, 2);
    cy = (idx / n_classes) / GST_ML_FRAME_DIM (mlframe, 2, 2);

    bbox[0] = bboxes[idx * 4];
    bbox[1] = bboxes[idx * 4 + 1];
    bbox[2] = bboxes[idx * 4 + 2];
    bbox[3] = bboxes[idx * 4 + 3];

    entry.left = (cx - bbox[0]) * paxelsize;
    entry.top = (cy - bbox[1]) * paxelsize;
    entry.right = (cx + bbox[2]) * paxelsize;
    entry.bottom = (cy + bbox[3]) * paxelsize;

    size = (entry.right - entry.left) * (entry.bottom - entry.top);

    // Discard results below the minimum bounding box size.
    if (size < BBOX_SIZE_THRESHOLD)
      continue;

    // Keep dimensions within the region.
    entry.left = MAX (entry.left, (gfloat) region.x);
    entry.top = MAX (entry.top, (gfloat) region.y);
    entry.right = MIN (entry.right, (gfloat) (region.x + region.w));
    entry.bottom = MIN (entry.bottom, (gfloat) (region.y + region.h));

    GST_TRACE ("Class: %u Confidence: %.2f Box[%f, %f, %f, %f]", class_idx,
        confidence, entry.top, entry.left, entry.bottom, entry.right);

    // Adjust bounding box dimensions with SAR and input tensor resolution.
    gst_ml_box_transform_dimensions (&entry, &region);

    entry.confidence = confidence * 100.0;
    entry.name = g_quark_from_string (label->name);
    entry.color = label->color;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_box_non_max_suppression (&entry, prediction->entries);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    GST_LOG ("Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        g_quark_to_string (entry.name), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (prediction->entries, nms);

    entry.landmarks = g_array_sized_new (FALSE, FALSE,
        sizeof (GstMLBoxLandmark), n_landmarks);
    g_array_set_size (entry.landmarks, n_landmarks);

    // Process the landmarks for this bounding box entry.
    for (num = 0; num < n_landmarks; ++num) {
      GstMLBoxLandmark *lmk = NULL;
      GHashTable *names = NULL;
      const gchar *name = NULL;

      // Check whether the landmark is above the set threshold.
      id = (idx / n_classes) * n_landmarks + num;

      confidence = lmkscores[id];

      if (confidence < 0.5)
        continue;

      names = g_hash_table_lookup (submodule->landmarks,
          GUINT_TO_POINTER (class_idx));

      // No landmarks label for this class, skip it.
      if (names == NULL)
        continue;

      name = g_hash_table_lookup (names, GUINT_TO_POINTER (num));

      // Landmark label with taht ID hasn't been added, skip it.
      if (name == NULL)
        continue;

      lmk = &(g_array_index (entry.landmarks, GstMLBoxLandmark, lmk_idx++));
      lmk->name = g_quark_from_string (name);

      id = (idx / n_classes) * (n_landmarks * 2) + num;

      x = landmarks[id];
      y = landmarks[id + n_landmarks];

      lmk->x = (cx + x) * paxelsize;
      lmk->y = (cy + y) * paxelsize;

      // Normalize landmark X and Y within bbox coordinates
      lmk->x -= region.x + (entry.left * region.w);
      lmk->y -= region.y + (entry.top * region.h);

      // Convert to relative coordinates.
      lmk->x /= (entry.right - entry.left) * region.w;
      lmk->y /= (entry.bottom - entry.top) * region.h;

      lmk->x = MIN (MAX (lmk->x, 0.0), 1.0);
      lmk->y = MIN (MAX (lmk->y, 0.0), 1.0);

      GST_LOG ("Landmark: %s [%f %f] ", g_quark_to_string (lmk->name),
          lmk->x, lmk->y);
    }

    // Resize the landmarks array to the actual number filled.
    g_array_set_size (entry.landmarks, lmk_idx);

    prediction->entries = g_array_append_val (prediction->entries, entry);
  }

  g_array_sort (prediction->entries, (GCompareFunc) gst_ml_box_compare_entries);
  return TRUE;
}
