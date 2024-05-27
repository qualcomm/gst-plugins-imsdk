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
 * ​​​​​Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <stdio.h>
#include <math.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-pose.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Minimum distance in pixels between keypoints of poses.
#define NMS_THRESHOLD_RADIUS  20.0F
// Radius in which to search for highest root keypoint of given type.
#define LOCAL_MAXIMUM_RADIUS  1
// Number of refinement steps to apply when traversing skeleton links.
#define NUM_REFINEMENT_STEPS  2

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8 }, " \
    "dimensions = (int) < <1, [5, 251], [5, 251], [1, 17]>, <1, [5, 251], [5, 251], [2, 34]>, <1, [5, 251], [5, 251], [4, 64]> >"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstRootPoint GstRootPoint;
typedef struct _GstMLSubModule GstMLSubModule;

struct _GstRootPoint {
  guint  id;
  gfloat confidence;
  gfloat x;
  gfloat y;
};

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

static inline gint
gst_ml_compare_rootpoints (gconstpointer a, gconstpointer b)
{
  const GstRootPoint *l_kp, *r_kp;

  l_kp = (const GstRootPoint*)a;
  r_kp = (const GstRootPoint*)b;

  if (l_kp->confidence > r_kp->confidence)
    return -1;
  else if (l_kp->confidence < r_kp->confidence)
    return 1;

  return 0;
}

static inline gint
gst_ml_pose_non_max_suppression (GstMLPoseEntry * l_entry, GArray * entries)
{
  GstMLPoseEntry *r_entry = NULL;
  GstMLKeypoint *l_kp = NULL, *r_kp = NULL;
  guint idx = 0, num = 0, n_keypoints = 0, n_overlaps = 0;
  gdouble distance = 0.0, threshold = 0.0;

  n_keypoints = l_entry->keypoints->len;

  // The threhold distance between 2 keypoints.
  threshold = NMS_THRESHOLD_RADIUS * NMS_THRESHOLD_RADIUS;

  for (idx = 0; idx < entries->len;  idx++, n_overlaps = 0) {
    r_entry = &(g_array_index (entries, GstMLPoseEntry, idx));

    // Find out how much overlap is between the keypoints of the predictons.
    for (num = 0; num < n_keypoints; num++) {
      l_kp = &(g_array_index (l_entry->keypoints, GstMLKeypoint, num));
      r_kp = &(g_array_index (r_entry->keypoints, GstMLKeypoint, num));

      distance = pow (l_kp->x - r_kp->x, 2) + pow (l_kp->y - r_kp->y, 2);

      // If the distance is below the threshold, increase the overlap score.
      if (distance <= threshold)
        n_overlaps += 1;
    }

    // If only half of the keypoints overlap then it's probably another pose.
    if (n_overlaps < (n_keypoints / 2))
      continue;

    // If confidence of current prediction is higher, remove the old entry.
    if (l_entry->confidence > r_entry->confidence)
      return idx;

    // If confidence of current prediction is lower, don't add it to the list.
    if (l_entry->confidence <= r_entry->confidence)
      return -2;
  }

  // If this point is reached then add current prediction to the list;
  return -1;
}

static GArray*
gst_ml_module_extract_rootpoints (GstMLSubModule * submodule,
    GstMLFrame * mlframe)
{
  GArray *rootpoints = NULL;
  gpointer heatmap = NULL, offsets = NULL;;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint idx = 0, num = 0, row = 0, column = 0;
  guint n_rows = 0, n_columns = 0, n_parts = 0, paxelsize[2] = {0, 0};
  gfloat threshold = 0.0, confidence = 0.0;

  mltype = GST_ML_FRAME_TYPE (mlframe);

  // The 2nd dimension of each tensor represents the matrix height.
  n_rows = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension of each tensor represents the matrix width.
  n_columns = GST_ML_FRAME_DIM (mlframe, 0, 2);
  // The 4th dimension of 1st tensor represents the number of parts in the pose.
  n_parts = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Convenient pointer to the keypoints heatmap inside the 1st tensor.
  heatmap = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  // Pointer to the keypoints coordinate offsets inside the 2nd tensor.
  offsets = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);

  // The width (position 0) and height (position 1) of the paxel block.
  paxelsize[0] = (submodule->inwidth - 1) / (n_columns - 1);
  paxelsize[1] = (submodule->inheight - 1) / (n_rows - 1);

  // Initial allocation for the Hough score map for each block of the matrix.
  rootpoints = g_array_new (FALSE, FALSE, sizeof (GstRootPoint));

  // Confidence threshold represented as the exponent of sigmoid.
  threshold = log (submodule->threshold / (1 - submodule->threshold));

  // Iterate the heatmap and find the keypoint with highest score for each block.
  for (row = 0; row < n_rows; row++) {
    for (column = 0; column < n_columns; column++) {
      for (num = 0; num < n_parts; num++) {
        GstRootPoint rootpoint;
        guint x = 0, y = 0, xmin = 0, xmax = 0, ymin = 0, ymax = 0;
        gfloat score = G_MINFLOAT;

        idx = (((row * n_columns) + column) * n_parts) + num;

        // Dequantize the keypoint heatmap confidence.
        confidence = gst_ml_tensor_extract_value (mltype, heatmap, idx,
            submodule->qoffsets[0], submodule->qscales[0]);

        // Discard results below the minimum confidence threshold.
        if (confidence < threshold)
          continue;

        // Calculate the X and Y range of the local window.
        ymin = MAX (row - LOCAL_MAXIMUM_RADIUS, 0);
        ymax = MIN (row + LOCAL_MAXIMUM_RADIUS + 1, n_rows);

        xmin = MAX (column - LOCAL_MAXIMUM_RADIUS, 0);
        xmax = MIN (column + LOCAL_MAXIMUM_RADIUS + 1, n_columns);

        // Check if this root point is the mexaimum in the local window.
        for (y = ymin; (confidence >= score) && (y < ymax); y++) {
          for (x = xmin; (confidence >= score) && (x < xmax); x++) {
            idx = (((y * n_columns) + x) * n_parts) + num;

            score = gst_ml_tensor_extract_value (mltype, heatmap, idx,
                submodule->qoffsets[0], submodule->qscales[0]);
          }
        }

        // Dicard keypoint if it is not the maximum in the local window.
        if (confidence < score)
          continue;

        // Apply a sigmoid function in order to normalize the heatmap confidence.
        confidence = 1.0 / (1.0 + expf (- confidence));

        rootpoint.id = num;
        rootpoint.confidence = confidence * 100.0;

        rootpoint.x = column * paxelsize[0];
        rootpoint.y = row * paxelsize[1];

        idx = (((y * n_columns) + x) * n_parts * 2) + num;

        // Dequantize the keypoint Y axis offset and add it ot the end coordinate.
        rootpoint.y += gst_ml_tensor_extract_value (mltype, offsets, idx,
            submodule->qoffsets[1], submodule->qscales[1]);

        // Dequantize the keypoint X axis offset and add it ot the end coordinate.
        rootpoint.x += gst_ml_tensor_extract_value (mltype, offsets,
            idx + n_parts, submodule->qoffsets[1], submodule->qscales[1]);

        GST_TRACE ("Root Keypoint %u [%.2f x %.2f], confidence %.2f",
            rootpoint.id, rootpoint.x, rootpoint.y, rootpoint.confidence);

        g_array_append_val (rootpoints, rootpoint);
      }
    }
  }

  // Sort the hough keypoint scores map by the their confidences.
  g_array_sort (rootpoints, gst_ml_compare_rootpoints);

  return rootpoints;
}

static void
gst_ml_module_traverse_skeleton_links (GstMLSubModule * submodule,
    GstMLFrame * mlframe, GstMLPoseEntry * entry, gboolean backwards)
{
  GstMLKeypointsLink *link =NULL;
  GstMLKeypoint *s_kp = NULL, *d_kp = NULL;
  GstMLLabel *label = NULL;
  gpointer heatmap = NULL, offsets = NULL, displacements = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gfloat displacement = 0.0, offset = 0.0, confidence = 0.0;
  guint idx = 0, num = 0, id = 0, n_rows = 0, n_columns = 0, n_parts = 0;
  guint row = 0, column = 0, s_kp_id = 0, d_kp_id = 0, paxelsize[2] = {0, 0};
  gint edge = 0, n_edges = 0, base = 0;

  mltype = GST_ML_FRAME_TYPE (mlframe);

  // The 2nd dimension of each tensor represents the matrix height.
  n_rows = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension of each tensor represents the matrix width.
  n_columns = GST_ML_FRAME_DIM (mlframe, 0, 2);
  // The 4th dimension of 1st tensor represents the number of keypoints.
  n_parts = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // The 4th dimension of 3rd tensor represents the number of keypoint links.
  // Division by 4 due to X and Y coordinates and backwards and forward values.
  n_edges = GST_ML_FRAME_DIM (mlframe, 2, 3) / 4;

  // Pointer to the keypoints heatmap inside the 1st tensor.
  heatmap = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  // Pointer to the keypoints coordinate offsets inside the 2nd tensor.
  offsets = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
  // Pointer to the displacement data inside the 3rd tensor.
  displacements = GST_ML_FRAME_BLOCK_DATA (mlframe, 2);

  // The width (position 0) and height (position 1) of the paxel block.
  paxelsize[0] = (submodule->inwidth - 1) / (n_columns - 1);
  paxelsize[1] = (submodule->inheight - 1) / (n_rows - 1);

  base = backwards ? (n_edges - 1) : 0;

  for (edge = 0; edge < n_edges; edge++, num = 0) {
    id = ABS (base - edge);
    link = &(g_array_index (submodule->links, GstMLKeypointsLink, id));

    s_kp_id = backwards ? link->d_kp_id : link->s_kp_id;
    d_kp_id = backwards ? link->s_kp_id : link->d_kp_id;

    s_kp = &(g_array_index (entry->keypoints, GstMLKeypoint, s_kp_id));
    d_kp = &(g_array_index (entry->keypoints, GstMLKeypoint, d_kp_id));

    // Skip if source is not present or destination is already populated.
    if ((s_kp->confidence == 0.0) || (d_kp->confidence != 0.0))
      continue;

    // Calculate original X & Y axis values in the matrix coordinate system.
    row = CLAMP (round (s_kp->y / paxelsize[1]), 0, (n_rows - 1));
    column = CLAMP (round (s_kp->x / paxelsize[0]), 0, (n_columns - 1));

    // Calculate the position of source keypoint inside the displacements tensor.
    idx = (((row * n_columns) + column) * (n_edges * 4)) + id;
    // For reverse iteration an additional offset by half of the edges is needed.
    idx += backwards ? (n_edges * 2) : 0;

    // Calculate the displaced Y axis value in the matrix coordinate system.
    displacement = gst_ml_tensor_extract_value (mltype, displacements, idx,
        submodule->qoffsets[2], submodule->qscales[2]);
    d_kp->y = s_kp->y + displacement;

    // Calculate the displaced X axis value in the matrix coordinate system.
    displacement = gst_ml_tensor_extract_value (mltype, displacements,
        idx + n_edges, submodule->qoffsets[2], submodule->qscales[2]);
    d_kp->x = s_kp->x + displacement;

    // Refine the destination keypoint coordinates.
    do {
      // Calculate original X & Y axis values in the matrix coordinate system.
      row = CLAMP (round (d_kp->y / paxelsize[1]), 0, (n_rows - 1));
      column = CLAMP (round (d_kp->x / paxelsize[0]), 0, (n_columns - 1));

      // Calculate the position of target keypoint inside the offsets tensor.
      idx = (((row * n_columns) + column) * n_parts * 2) + d_kp_id;

      // Dequantize the keypoint Y axis offset and add it ot the end coordinate.
      offset = gst_ml_tensor_extract_value (mltype, offsets, idx,
          submodule->qoffsets[1], submodule->qscales[1]);
      d_kp->y = row * paxelsize[1] + offset;

      // Dequantize the keypoint X axis offset and add it ot the end coordinate.
      offset = gst_ml_tensor_extract_value (mltype, offsets, idx + n_parts,
          submodule->qoffsets[1], submodule->qscales[1]);
      d_kp->x = column * paxelsize[0] + offset;
    } while (++num < NUM_REFINEMENT_STEPS);

    // Clamp values outside the range.
    d_kp->y = CLAMP (d_kp->y, 0, submodule->inheight - 1);
    d_kp->x = CLAMP (d_kp->x, 0, submodule->inwidth - 1);

    // Calculate original X & Y axis values in the matrix coordinate system.
    row = CLAMP (round (d_kp->y / paxelsize[1]), 0, (n_rows - 1));
    column = CLAMP (round (d_kp->x / paxelsize[0]), 0, (n_columns - 1));

    // Calculate the position of target keypoint inside the heatmap tensor.
    idx = (((row * n_columns) + column) * n_parts) + d_kp_id;

    // Dequantize the keypoint heatmap confidence.
    confidence = gst_ml_tensor_extract_value (mltype, heatmap, idx,
        submodule->qoffsets[0], submodule->qscales[0]);
    // Apply a sigmoid function in order to normalize the heatmap confidence.
    confidence = 1.0 / (1.0 + expf (- confidence));

    d_kp->confidence = confidence * 100;

    // Extract info from labels and populate the coresponding keypoint params.
    label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (d_kp_id));

    d_kp->name = g_quark_from_string (label ? label->name : "unknown");
    d_kp->color = label->color;

    GST_TRACE ("Link[%d]: '%s' [%f x %f], %.2f <---> '%s' [%f x %f], %.2f", id,
        g_quark_to_string (s_kp->name), s_kp->x, s_kp->y, s_kp->confidence,
        g_quark_to_string (d_kp->name), d_kp->x, d_kp->y, d_kp->confidence);

    // Increase the overall prediction confidence with the found keypoint.
    entry->confidence += d_kp->confidence / n_parts;
  }
}

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

  // The 4th dimension of 3rd tensor represents the number of keypoint pairs
  // that make up the skeleton and their X & Y axis displacements values.
  if (submodule->links->len !=
          (GST_ML_INFO_TENSOR_DIM (&(submodule->mlinfo), 2, 3) / 4)) {
    GST_ERROR ("Invalid number of loaded skeleton links!");
    goto cleanup;
  }

  success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_THRESHOLD);
  if (!success) {
    GST_ERROR ("Settings stucture does not contain threshold value!");
    goto cleanup;
  }

  gst_structure_get_double (settings, GST_ML_MODULE_OPT_THRESHOLD, &threshold);
  submodule->threshold = threshold / 100.0;

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
  GArray *predictions = ((GArray *) output), *rootpoints = NULL;
  GstProtectionMeta *pmeta = NULL;
  GstMLPosePrediction *prediction = NULL;
  GstMLLabel *label = NULL;
  GstVideoRectangle region = { 0, };
  guint idx = 0, num = 0, n_parts = 0;
  gint nms = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLPosePrediction, 0));
  prediction->info = pmeta->info;

  // Extract the dimensions of the input tensor that produced the output tensors.
  if (submodule->inwidth == 0 || submodule->inheight == 0) {
    gst_ml_protecton_meta_get_source_dimensions (pmeta, &(submodule->inwidth),
        &(submodule->inheight));
  }

  // Extract the source tensor region with actual data.
  gst_ml_protecton_meta_get_source_region (pmeta, &region);

  // The 4th dimension of 1st tensor represents the number of parts in the pose.
  n_parts = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Find the keypoints with highest score for each block inside the heatmap.
  rootpoints = gst_ml_module_extract_rootpoints (submodule, mlframe);

  // Iterate over the root keypoints and build up pose predictions.
  for (idx = 0; idx < rootpoints->len; idx++) {
    GstRootPoint *rootpoint = NULL;
    GstMLKeypoint keypoint = { 0, }, *kp = NULL;
    GstMLPoseEntry entry = { 0, };

    rootpoint = &(g_array_index (rootpoints, GstRootPoint, idx));

    keypoint.x = rootpoint->x;
    keypoint.y = rootpoint->y;
    keypoint.confidence = rootpoint->confidence;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (rootpoint->id));

    keypoint.name = g_quark_from_string (label ? label->name : "unknown");
    keypoint.color = label->color;

    entry.keypoints =
        g_array_sized_new (FALSE, TRUE, sizeof (GstMLKeypoint), n_parts);

    g_array_set_size (entry.keypoints, n_parts);

    // Copy the new seed inside the pose prediction struct.
    kp = &(g_array_index (entry.keypoints, GstMLKeypoint, rootpoint->id));
    memcpy (kp, &keypoint, sizeof (GstMLKeypoint));

    entry.confidence = kp->confidence / n_parts;

    GST_TRACE ("Seed Keypoint: '%s' [%.2f x %.2f], confidence %.2f",
        g_quark_to_string (kp->name), kp->x, kp->y, kp->confidence);

    // Iterate backwards over the skeleton links to find the seed keypoint.
    gst_ml_module_traverse_skeleton_links (submodule, mlframe, &entry, TRUE);
    // Iterate forward over the skeleton links to find all other keypoints.
    gst_ml_module_traverse_skeleton_links (submodule, mlframe, &entry, FALSE);

    // Non-Max Suppression (NMS) algorithm.
    // If the NMS result is below 0 don't create new pose prediction.
    nms = gst_ml_pose_non_max_suppression (&entry, prediction->entries);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)) {
      g_array_free (entry.keypoints, TRUE);
      continue;
    }

    // TODO: For now set the same connections.
    entry.connections = submodule->connections;

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      prediction->entries = g_array_remove_index (prediction->entries, nms);

    prediction->entries = g_array_append_val (prediction->entries, entry);
  }

  g_array_free (rootpoints, TRUE);
  g_array_sort (prediction->entries, (GCompareFunc) gst_ml_pose_compare_entries);

  // TODO Optimize?
  // Transform coordinates to relative with extracted source aspect ratio.
  for (idx = 0; idx < prediction->entries->len; idx++) {
    GstMLPoseEntry *entry =
        &(g_array_index (prediction->entries, GstMLPoseEntry, idx));

    for (num = 0; num < entry->keypoints->len; num++) {
      GstMLKeypoint *keypoint =
          &(g_array_index (entry->keypoints, GstMLKeypoint, num));

      gst_ml_keypoint_transform_coordinates (keypoint, &region);
    }
  }

  return TRUE;
}
