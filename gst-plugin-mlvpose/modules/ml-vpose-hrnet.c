/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
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
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 *
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <math.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-pose.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, [1, 256], [1, 256], [1, 17]> >"

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

  // List of keypoint labels.
  GHashTable *labels;
  // Chain/Tree comprised of keypoint pairs that describe the skeleton.
  GArray     *links;
  // List of keypoint pairs that are connected together.
  GArray     *connections;

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

  if (submodule->connections != NULL)
    g_array_free (submodule->connections, TRUE);

  if (submodule->links != NULL)
    g_array_free (submodule->links, TRUE);

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

  // Tensor keypoints count and number of labels need to match.
  if (g_hash_table_size (submodule->labels) !=
          GST_ML_INFO_TENSOR_DIM (&(submodule->mlinfo), 0, 3)) {
    GST_ERROR ("Invalid number of loaded labels!");
    goto cleanup;
  }

  // Fill the keypoints chain/tree.
  submodule->links = g_array_new (FALSE, FALSE, sizeof (GstMLKeypointsLink));
  submodule->connections = g_array_new (FALSE, FALSE, sizeof (GstMLKeypointsLink));

  // Recursiveli fill the skeleton chain/tree starting from label 0 as seed.
  if (!(success = gst_ml_load_links (&list, 0, submodule->links))) {
    GST_ERROR ("Failed to load the skeleton chain/tree!");
    goto cleanup;
  }

  // Recursiveli fill the keypoint connections starting from label 0 as seed.
  if (!(success = gst_ml_load_connections (&list, submodule->connections))) {
    GST_ERROR ("Failed to load the keypoint interconnections!");
    goto cleanup;
  }

  success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_THRESHOLD);
  if (!success) {
    GST_ERROR ("Settings stucture does not contain threshold value!");
    goto cleanup;
  }

  gst_structure_get_double (settings, GST_ML_MODULE_OPT_THRESHOLD, &threshold);
  submodule->threshold = threshold;

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
  GArray *predictions = ((GArray *) output);
  GstProtectionMeta *pmeta = NULL;
  GstMLPosePrediction *prediction = NULL;
  GstMLPoseEntry *entry = NULL;
  gpointer heatmap = NULL;
  GstVideoRectangle region = { 0, };
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint idx = 0, num = 0, id = 0, x = 0, y = 0;
  guint width = 0, height = 0, n_keypoints = 0, n_blocks = 0;
  gfloat confidence = 0.0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLPosePrediction, 0));

  prediction->batch_idx = 0;
  prediction->info = pmeta->info;

  // Extract the dimensions of the input tensor that produced the output tensors.
  if (submodule->inwidth == 0 || submodule->inheight == 0) {
    gst_ml_protecton_meta_get_source_dimensions (pmeta, &(submodule->inwidth),
        &(submodule->inheight));
  }

  // Extract the source tensor region with actual data.
  gst_ml_protecton_meta_get_source_region (pmeta, &region);

  // The 2nd dimension of each tensor represents the matrix height.
  height = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension of each tensor represents the matrix width.
  width = GST_ML_FRAME_DIM (mlframe, 0, 2);
  // The 4th dimension of 1st tensor represents the number of keypoints.
  n_keypoints = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Convenient pointer to the keypoints heatmap inside the 1st tensor.
  heatmap = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  mltype = GST_ML_FRAME_TYPE (mlframe);

  // The total number of macro blocks in the matrix.
  n_blocks = width * height * n_keypoints;

  // Allocate only single prediction result.
  g_array_set_size (prediction->entries, 1);
  entry = &(g_array_index (prediction->entries, GstMLPoseEntry, 0));

  // Allocate memory for the keypoiints.
  entry->keypoints = g_array_sized_new (FALSE, TRUE,
      sizeof (GstMLKeypoint), g_hash_table_size (submodule->labels));

  g_array_set_size (entry->keypoints, n_keypoints);

  // Iterate the heatmap and find the block with highest score for each keypoint.
  for (idx = 0; idx < n_keypoints; idx++) {
    GstMLKeypoint *kp = NULL;
    GstMLLabel *label = NULL;
    gint dx = 0, dy = 0;

    // Initial position ID of this type of keypoint.
    id = idx;

    // Find the position of the keypoint with the highest score in current paxel.
    for (num = (idx + n_keypoints); num < n_blocks; num += n_keypoints)
      id = (gst_ml_tensor_compare_values (mltype, heatmap, num, id) > 0) ? num : id;

    // Dequantize the keypoint confidence.
    confidence = gst_ml_tensor_extract_value (mltype, heatmap, id,
        submodule->qoffsets[0], submodule->qscales[0]);

    x = (id / n_keypoints) % width;
    y = (id / n_keypoints) / width;

    GST_TRACE ("Keypoint: %u [%u x %u], confidence %.2f", idx, x, y, confidence);

    // Refine coordinates by moving from the maximum towards the second maximum.
    //         (Y - 1)
    // (X - 1) Keypoint (X + 1)
    //         (Y + 1)
    if ((x > 1) && (x < (width - 1)) && (y > 0) && (y < height)) {
      dx = gst_ml_tensor_compare_values (mltype, heatmap,
          (y * (x + 1) * n_keypoints) + idx, (y * (x - 1) * n_keypoints) + idx);
    }

    if ((y > 1) && (y < (height - 1)) && (x > 0) && (x < width)) {
      dy = gst_ml_tensor_compare_values (mltype, heatmap,
          ((y + 1) * x * n_keypoints) + idx, ((y - 1) * x * n_keypoints) + idx);
    }

    GST_TRACE ("Refined Keypoint: %u [%.2f x %.2f], confidence %.2f", idx,
        (x + dx * 0.25), (y + dy * 0.25), confidence);

    kp = &(g_array_index (entry->keypoints, GstMLKeypoint, idx));

    // Multiply by the dimensions of the paxel.
    kp->x = ((x + dx * 0.25) / width) * submodule->inwidth;
    kp->y = ((y + dy * 0.25) / height) * submodule->inheight;

    // Extract info from labels and populate the coresponding keypoint params.
    label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (idx));

    kp->name = g_quark_from_string (label ? label->name : "unknown");
    kp->color = label->color;

    kp->confidence = confidence * 100;
    entry->confidence += kp->confidence;

    gst_ml_keypoint_transform_coordinates (kp, &region);
  }

  // The final confidence score for the whole prediction entry.
  entry->confidence /= n_keypoints;

  // TODO: For now set the same connections.
  entry->connections = submodule->connections;

  if (entry->confidence < submodule->threshold)
    prediction->entries = g_array_remove_index (prediction->entries, 0);

  return TRUE;
}
