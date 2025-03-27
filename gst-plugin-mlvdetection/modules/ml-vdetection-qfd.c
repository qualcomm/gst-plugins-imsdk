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
#define BBOX_SIZE_THRESHOLD    100 // 10 x 10 pixels

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, 60, 80, 1 >, < 1, 60, 80, 1 >, < 1, 60, 80, 10 >, < 1, 60, 80, 4 > >; " \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, 120, 160, 1>, <1, 120, 160, 10>, <1, 120, 160, 4> >; " \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < < 1, 60, 80, 4 >, < 1, 60, 80, 10 >, < 1, 60, 80, 1 > >;" \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < < 1, 60, 80, 1 >, < 1, 60, 80, 4 >, < 1, 60, 80, 10 > >;"

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

  if (GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_UINT8) {
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
  GArray *predictions = (GArray *)output;
  GstProtectionMeta *pmeta = NULL;
  GstMLBoxPrediction *prediction = NULL;
  gpointer scores = NULL, landmarks = NULL, bboxes = NULL, hm_pool = NULL;
  GstVideoRectangle region = { 0, };
  gfloat confidence = 0.0;
  guint idx = 0, num = 0, id = 0, class_idx = 0, size = 0.0;
  guint n_classes = 0, n_landmarks = 0, n_paxels = 0, paxelsize = 0;
  gint nms = -1, cx = 0, cy = 0;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint scores_idx = 0, bboxes_idx = 0, landmarks_idx = 0;

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

  mltype = GST_ML_FRAME_TYPE (mlframe);

  if (GST_ML_FRAME_N_TENSORS (mlframe) == 4) {
    // First tensor represents confidence scores.
    scores_idx = 0;
    // TODO: Second tensor represents some kind of confidence scores.
    hm_pool = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
    // Third tensor represents the landmarks (left eye, right ear, etc.).
    landmarks_idx = 2;
    // Fourh tensor represents the coordinates of the bounding boxes.
    bboxes_idx = 3;
  } else if (GST_ML_FRAME_N_TENSORS (mlframe) == 3) {
    // Check whether the first tensor contains the bounding boxes.
    if (GST_ML_FRAME_DIM (mlframe, 0, 3) == 4) {
      // Thrid tensor represents confidence scores.
      scores_idx = 2;
      // First tensor represents the coordinates of the bounding boxes.
      bboxes_idx = 0;
      // Second tensor represents the landmarks (left eye, right ear, etc.).
      landmarks_idx = 1;
    } else if (GST_ML_FRAME_DIM (mlframe, 1, 3) == 4) {
      // First tensor represents confidence scores.
      scores_idx = 0;
      // 2nd tensor represents the coordinates of the bounding boxes.
      bboxes_idx = 1;
      // third tensor represents the landmarks (left eye, right ear, etc.).
      landmarks_idx = 2;
    } else {
      // First tensor represents confidence scores.
      scores_idx = 0;
      // Thrid tensor represents the coordinates of the bounding boxes.
      bboxes_idx = 2;
      // Second tensor represents the landmarks (left eye, right ear, etc.).
      landmarks_idx = 1;
    }
  }

  scores = GST_ML_FRAME_BLOCK_DATA (mlframe, scores_idx);
  landmarks = GST_ML_FRAME_BLOCK_DATA (mlframe, landmarks_idx);
  bboxes = GST_ML_FRAME_BLOCK_DATA (mlframe, bboxes_idx);
  n_classes = GST_ML_FRAME_DIM (mlframe, scores_idx, 3);
  n_landmarks = GST_ML_FRAME_DIM (mlframe, landmarks_idx, 3) / 2;

  // Calculate the number of macroblocks (paxels).
  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 1) * GST_ML_FRAME_DIM (mlframe, 0, 2);
  // Calculate the dimension of the square macro block.
  paxelsize = submodule->inwidth / GST_ML_FRAME_DIM (mlframe, 0, 2);

  // TODO: This is currently processing only class with index 0 (face).
  for (idx = 0; idx < n_paxels; idx += n_classes) {
    GstMLLabel *label = NULL;
    GstMLBoxEntry entry = { 0, };
    gfloat left = G_MAXFLOAT, right = 0.0, top = G_MAXFLOAT, bottom = 0.0;
    gfloat x = 0.0, y = 0.0, tx = 0.0, ty = 0.0, width = 0.0, height = 0.0;
    gfloat bbox_x = 0.0, bbox_y = 0.0, bbox_w = 0.0, bbox_h = 0.0, hm_pool_val = 0.0;

    confidence = gst_ml_tensor_extract_value (mltype, scores, idx,
        submodule->qoffsets[scores_idx], submodule->qscales[scores_idx]);

    // Discard invalid results.
    hm_pool_val = gst_ml_tensor_extract_value (mltype, hm_pool, idx,
        submodule->qoffsets[1], submodule->qscales[1]);
    if ((hm_pool != NULL) && confidence != hm_pool_val)
      continue;

    // Discard results below the minimum score threshold.
    if (confidence < submodule->threshold)
      continue;

    class_idx = idx % n_classes;

    // Calculate the centre coordinates.
    cx = (idx / n_classes) % GST_ML_FRAME_DIM (mlframe, 0, 2);
    cy = (idx / n_classes) / GST_ML_FRAME_DIM (mlframe, 0, 2);

    bbox_x = gst_ml_tensor_extract_value (mltype, bboxes, (idx * 4),
        submodule->qoffsets[bboxes_idx], submodule->qscales[bboxes_idx]);
    bbox_y = gst_ml_tensor_extract_value (mltype, bboxes, (idx * 4) + 1,
        submodule->qoffsets[bboxes_idx], submodule->qscales[bboxes_idx]);
    bbox_w = gst_ml_tensor_extract_value (mltype, bboxes, (idx * 4) + 2,
        submodule->qoffsets[bboxes_idx], submodule->qscales[bboxes_idx]);
    bbox_h = gst_ml_tensor_extract_value (mltype, bboxes, (idx * 4) + 3,
        submodule->qoffsets[bboxes_idx], submodule->qscales[bboxes_idx]);

    entry.left = (cx - bbox_x) * paxelsize;
    entry.top = (cy - bbox_y) * paxelsize;
    entry.right = (cx + bbox_w) * paxelsize;
    entry.bottom = (cy + bbox_h) * paxelsize;

    size = (entry.right - entry.left) * (entry.bottom - entry.top);

    // Discard results below the minimum bounding box size.
    if (size < BBOX_SIZE_THRESHOLD)
      continue;

    for (num = 0; num < n_landmarks; ++num) {
      gfloat ld_x = 0, ld_y = 0;

      id = (idx / n_classes) * (n_landmarks * 2) + num;

      ld_x = gst_ml_tensor_extract_value (mltype, landmarks, id,
          submodule->qoffsets[landmarks_idx], submodule->qscales[landmarks_idx]);
      ld_y = gst_ml_tensor_extract_value (mltype, landmarks, (id + n_landmarks),
          submodule->qoffsets[landmarks_idx], submodule->qscales[landmarks_idx]);

      x = (cx + ld_x) * paxelsize;
      y = (cy + ld_y) * paxelsize;

      // Normalize landmark X and Y within bbox coordinates
      x -= region.x + entry.left;
      y -= region.y + entry.top;

      // Find the region in which the landmarks reside.
      left = MIN (left, x);
      top = MIN (top, y);
      right = MAX (right, x);
      bottom = MAX (bottom, y);

      GST_TRACE ("Ladnmark: [ %f %f ] ", x, y);
    }

    // Translate the bbox centre based on the landmarks region centre.
    tx = left + ((right - left) / 2) - ((entry.right - entry.left) / 2);
    ty = top + ((bottom - top) / 2) - ((entry.bottom - entry.top) / 2);

    entry.left += tx;
    entry.top += ty;
    entry.right += tx;
    entry.bottom += ty;

    GST_LOG ("Class: %u Confidence: %.2f Box[%f, %f, %f, %f]", class_idx,
        confidence, entry.top, entry.left, entry.bottom, entry.right);

    // Adjust bounding box dimensions in order to make it a square with margins.
    width = entry.right - entry.left;
    height = entry.bottom - entry.top;

    if (width > height) {
      entry.top -= ((width - height) / 2);
      entry.bottom = entry.top + width;
    } else if (width < height) {
      entry.left -= ((height - width) / 2);
      entry.right = entry.left + height;
    }

    GST_LOG ("Class: %u Confidence: %.2f Adjusted Box[%f, %f, %f, %f]",
        class_idx, confidence, entry.top, entry.left, entry.bottom, entry.right);

    // Adjust bounding box dimensions with SAR and input tensor resolution.
    gst_ml_box_transform_dimensions (&entry, &region);

    label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (class_idx));

    entry.confidence = confidence * 100.0;
    entry.name = g_quark_from_string (label ? label->name : "unknown");
    entry.color = label ? label->color : 0x000000FF;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_box_non_max_suppression (&entry, prediction->entries);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    GST_TRACE ("Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        g_quark_to_string (entry.name), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      predictions = g_array_remove_index (prediction->entries, nms);

    prediction->entries = g_array_append_val (prediction->entries, entry);
  }

  g_array_sort (prediction->entries, (GCompareFunc) gst_ml_box_compare_entries);
  return TRUE;
}
