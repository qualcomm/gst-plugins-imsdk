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

#include "ml-video-pose-module.h"

#include <stdio.h>
#include <math.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

// Size (Stride) in pixels of one block of the tensor matrix.
#define MATRIX_BLOCK_SIZE     16.0F
// Keypoint confidence threshold (10%).
#define CONFIDENCE_THRESHOLD  0.1F
// Minimum distance in pixels between keypoints of poses.
#define NMS_THRESHOLD_RADIUS  20.0F

#define GST_ML_MODULE_TENSOR_DIMS \
    "< < 1, 31, 41, 17 >, < 1, 31, 41, 34 >, < 1, 31, 41, 64 > >"

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { UINT8 }, " \
    "dimensions = (int) " GST_ML_MODULE_TENSOR_DIMS

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

// Offset values for each of the 3 tensors needed for dequantization.
static const gint32 qoffsets[3] = { 128, 128, 117 };
// Scale values for each of the 3 tensors needed for dequantization.
static const gfloat qscales[3] =
    { 0.0784313753247261, 0.0784313753247261, 1.3875764608383179 };

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstHoughScore GstHoughScore;
typedef struct _GstMLSubModule GstMLSubModule;

// Hough keypoint score.
struct _GstHoughScore {
  guint  id;
  gfloat confidence;
  gfloat x;
  gfloat y;
};

struct _GstMLSubModule {
  // List of keypoint labels.
  GHashTable *labels;
  // Chain/Tree comprised of keypoint pairs that describe the skeleton.
  GArray     *links;
  // List of keypoint pairs that are connected together.
  GArray     *connections;
};


static gboolean
gst_ml_load_links (const GValue * list, const guint idx, GArray * links)
{
  GstStructure *structure = NULL;
  const GValue *array = NULL, *value = NULL;
  GstPoseLink link = { 0, };
  guint id = 0, num = 0, size = 0;

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, idx)));

  if (structure == NULL) {
    GST_ERROR ("Failed to extract structure!");
    return FALSE;
  }

  if (!gst_structure_has_field (structure, "links"))
    return TRUE;

  // Initial ID of the source keypoint.
  gst_structure_get_uint (structure, "id", &id);
  link.s_kp_id = id;

  array = gst_structure_get_value (structure, "links");
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (array), FALSE);

  size = gst_value_array_get_size (array);
  g_return_val_if_fail (size != 0, FALSE);

  for (num = 0; num < size; num++) {
    value = gst_value_array_get_value (array, num);
    g_return_val_if_fail (G_VALUE_HOLDS_UINT (value), FALSE);

    link.d_kp_id = id = g_value_get_uint (value);
    g_array_append_val (links, link);

    // Recursively check and load the next link in teh chain/tree.
    if (!gst_ml_load_links (list, id, links))
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ml_load_connections (const GValue * list, GArray * connections)
{
  GstStructure *structure = NULL;
  GstPoseLink connection = { 0, };
  guint idx = 0, size = 0;

  size = gst_value_list_get_size (list);

  for (idx = 0; idx < size; idx++) {
    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (list, idx)));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure!");
      return FALSE;
    }

    if (!gst_structure_has_field (structure, "connection"))
      continue;

    gst_structure_get_uint (structure, "id", &(connection).s_kp_id);
    gst_structure_get_uint (structure, "connection", &(connection).d_kp_id);

    g_array_append_val (connections, connection);
  }

  return TRUE;
}

static gint
gst_ml_compare_scores (gconstpointer a, gconstpointer b)
{
  const GstHoughScore *l_score, *r_score;

  l_score = (const GstHoughScore*)a;
  r_score = (const GstHoughScore*)b;

  if (l_score->confidence > r_score->confidence)
    return -1;
  else if (l_score->confidence < r_score->confidence)
    return 1;

  return 0;
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

static void
gst_ml_keypoint_free (gpointer data)
{
  GstPoseKeypoint *keypoint = (GstPoseKeypoint*) data;

  if (keypoint->label != NULL)
    g_free (keypoint->label);
}

static inline void
gst_ml_keypoint_populate_label_params (GstPoseKeypoint * keypoint, guint id,
    GHashTable * labels)
{
  GstLabel *label = g_hash_table_lookup (labels, GUINT_TO_POINTER (id));

  keypoint->label = g_strdup (label ? label->name : "unknown");
  keypoint->color = label->color;
}

static inline void
gst_ml_keypoint_transform_coordinates (GstPoseKeypoint * keypoint,
    gint num, gint denum, guint width, guint height)
{
  gdouble coeficient = 0.0;

  if (num > denum) {
    gst_util_fraction_to_double (num, denum, &coeficient);

    keypoint->x /= width;
    keypoint->y /= width / coeficient;

    return;
  } else if (num < denum) {
    gst_util_fraction_to_double (denum, num, &coeficient);

    keypoint->x /= height / coeficient;
    keypoint->y /= height;

    return;
  }

  // There is no need for AR adjustments, just translate to relative coords.
  keypoint->x /= width;
  keypoint->y /= height;
}

static inline gint
gst_ml_non_max_suppression (GstPoseKeypoint * l_keypoint, GArray * predictions)
{
  gdouble distance = 0.0;
  guint idx = 0, num = 0;

  for (idx = 0; idx < predictions->len;  idx++) {
    GstMLPrediction *prediction =
        &(g_array_index (predictions, GstMLPrediction, idx));

    for (num = 0; num < prediction->keypoints->len;  num++) {
      GstPoseKeypoint *r_keypoint =
          &(g_array_index (prediction->keypoints, GstPoseKeypoint, num));

      distance = pow (l_keypoint->x - r_keypoint->x, 2) +
          pow (l_keypoint->y - r_keypoint->y, 2);

      // If the distance is above the threshold, continue with next list entry.
      if (distance > (NMS_THRESHOLD_RADIUS * NMS_THRESHOLD_RADIUS))
        continue;

      // If labels do not match, continue with next list entry.
      if (g_strcmp0 (l_keypoint->label, r_keypoint->label) != 0)
        continue;

      // If confidence of current prediction is higher, remove the old entry.
      if (l_keypoint->confidence > r_keypoint->confidence) {
        g_free (r_keypoint->label);
        memcpy (r_keypoint, l_keypoint, sizeof (GstPoseKeypoint));
        return -1;
      }

      // If confidence of current prediction is lower, don't add it to the list.
      if (l_keypoint->confidence <= r_keypoint->confidence)
        return -1;
    }
  }

  // If this point is reached then create new prediction entry.
  return idx;
}

static void
gst_ml_traverse_skeleton_link (GstPoseKeypoint * kp, guint id, guint edge,
    guint n_edges, gfloat x, gfloat y, guint width, guint height, guint n_keypoints,
    const guint8 * heatmap, const guint8 * offsets, const guint8 * displacements)
{
  gfloat displacement = 0.0, offset = 0.0, confidence = 0.0;
  guint n = 0, m = 0, idx = 0;

  // Calculate original X & Y axis values in the matrix coordinate system.
  n = CLAMP (round (x / MATRIX_BLOCK_SIZE), 0, (width - 1));
  m = CLAMP (round (y / MATRIX_BLOCK_SIZE), 0, (height - 1));

  // Calculate the position of source keypoint inside the displacements tensor.
  idx = ((m * width) + n) * (n_edges * 4) + edge;

  // Calculate the displaced Y axis value in the matrix coordinate system.
  displacement = (displacements[idx] - qoffsets[2]) * qscales[2];
  m = CLAMP (round ((y + displacement) / MATRIX_BLOCK_SIZE), 0, (height - 1));

  // Calculate the displaced X axis value in the matrix coordinate system.
  displacement = (displacements[idx + n_edges] - qoffsets[2]) * qscales[2];
  n = CLAMP (round ((x + displacement) / MATRIX_BLOCK_SIZE), 0, (width - 1));

  // Calculate the position of target keypoint inside the heatmap tensor.
  idx = ((m * width) + n) * n_keypoints + id;

  // Dequantize the keypoint heatmap confidence.
  confidence = (heatmap[idx] - qoffsets[0]) * qscales[0];
  // Apply a sigmoid function in order to normalize the heatmap confidence.
  confidence = 1.0 / (1.0 + expf (- confidence));

  // Calculate the position of target keypoint inside the offsets tensor.
  idx = ((m * width) + n) * n_keypoints * 2;

  // Dequantize the keypoint Y axis offset and add it ot the end coordinate.
  offset = (offsets[idx] - qoffsets[1]) * qscales[1];
  kp->y = (m * MATRIX_BLOCK_SIZE) + offset;

  // Dequantize the keypoint X axis offset and add it ot the end coordinate.
  offset = (offsets[idx + n_keypoints] - qoffsets[1]) * qscales[1];
  kp->x = (n * MATRIX_BLOCK_SIZE) + offset;

  kp->confidence = confidence * 100;
}

static void
gst_ml_extract_hough_scores (GArray * scores, GstMLFrame * mlframe)
{
  GstHoughScore score = { 0, };
  guint8 *heatmap = NULL, *offsets = NULL;
  guint idx = 0, num = 0, id = 0, x = 0, y = 0;
  guint width = 0, height = 0, n_keypoints = 0;
  gfloat confidence = 0.0, offset = 0.0;

  // The 2nd dimension of each tensor represents the matrix height.
  height = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension of each tensor represents the matrix width.
  width = GST_ML_FRAME_DIM (mlframe, 0, 2);
  // The 4th dimension of 1st tensor represents the number of keypoints.
  n_keypoints = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Convinient Ppointer to the keypoints heatmap inside the 1st tensor.
  heatmap = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  // Convinient pointer to the keypoints coordinate offsets inside the 2nd tensor.
  offsets = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);

  // Iterate the heatmap and find the keypoint with highest score for each block.
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++, num += n_keypoints) {
      // Initialize the keypoint ID value.
      id = num;

      // Find the keypoint ID in current coordinate with the highest confidence.
      for (idx = (num + 1); idx < (num + n_keypoints); idx++)
        id = (heatmap[idx] > heatmap[id]) ? idx : id;

      // Dequantize the keypoint heatmap confidence.
      confidence = (heatmap[id] - qoffsets[0]) * qscales[0];
      // Apply a sigmoid function in order to normalize the heatmap confidence.
      confidence = 1.0 / (1.0 + expf (- confidence));

      // Discard results below the minimum confidence threshold.
      if (confidence < CONFIDENCE_THRESHOLD)
        continue;

      idx = (id - num);

      // Dequantize the keypoint Y axis offset and add it ot the end coordinate.
      offset = (offsets[(num * 2) + idx] - qoffsets[1]) * qscales[1];
      score.y = (y * MATRIX_BLOCK_SIZE) + offset;

      // Dequantize the keypoint X axis offset and add it ot the end coordinate.
      offset = (offsets[(num * 2) + idx + n_keypoints] - qoffsets[1]) * qscales[1];
      score.x = (x * MATRIX_BLOCK_SIZE) + offset;

      score.confidence = confidence;
      score.id = idx;

      GST_TRACE ("Score: Keypoint %u [%.2f x %.2f], confidence %.2f",
          score.id, score.x, score.y, score.confidence);

      scores = g_array_append_val (scores, score);
    }
  }
}

static void
gst_ml_decode_pose_prediction (GstMLPrediction * prediction,
    GstMLFrame * mlframe, GHashTable * labels, GArray * links)
{
  GstPoseKeypoint *s_kp = NULL, *d_kp = NULL;
  guint8 *heatmap = NULL, *offsets = NULL, *displacements = NULL;
  guint width = 0, height = 0, n_keypoints = 0;
  gint edge = 0, n_edges = 0;

  // The 2nd dimension of each tensor represents the matrix height.
  height = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension of each tensor represents the matrix width.
  width = GST_ML_FRAME_DIM (mlframe, 0, 2);
  // The 4th dimension of 1st tensor represents the number of keypoints.
  n_keypoints = GST_ML_FRAME_DIM (mlframe, 0, 3);

  // Pointer to the keypoints heatmap inside the 1st tensor.
  heatmap = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  // Pointer to the keypoints coordinate offsets inside the 2nd tensor.
  offsets = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
  // Pointer to the displacement data inside the 3rd tensor.
  displacements = GST_ML_FRAME_BLOCK_DATA (mlframe, 2);

  n_edges = links->len;

  // Iterate backwards over the skeleton links to find the seed keypoint.
  for (edge = (n_edges - 1); edge >= 0; edge--) {
    GstPoseLink *link = &(g_array_index (links, GstPoseLink, edge));

    s_kp = &(g_array_index (prediction->keypoints, GstPoseKeypoint, link->d_kp_id));
    d_kp = &(g_array_index (prediction->keypoints, GstPoseKeypoint, link->s_kp_id));

    // Skip if source is not present or destination is already populated.
    if ((s_kp->confidence == 0.0) || (d_kp->confidence != 0.0))
      continue;

    // Extrapolate data from the source keypoint and populate the destination.
    // Increase the edge with 2x links length, because iteration is backwards.
    gst_ml_traverse_skeleton_link (d_kp, link->s_kp_id, (edge + (n_edges * 2)),
        n_edges, s_kp->x, s_kp->y, width, height, n_keypoints, heatmap,
        offsets, displacements);

    // Extract info from labels and populate the coresponding keypoint params.
    gst_ml_keypoint_populate_label_params (d_kp, link->s_kp_id, labels);

    GST_TRACE ("Keypoint: '%s' [%.2f x %.2f], confidence %.2f", d_kp->label,
        d_kp->x, d_kp->y, d_kp->confidence);
  }

  // Iterate forward over the skeleton links to find all other keypoints.
  for (edge = 0; edge < n_edges; edge++) {
    GstPoseLink *link = &(g_array_index (links, GstPoseLink, edge));

    s_kp = &(g_array_index (prediction->keypoints, GstPoseKeypoint, link->s_kp_id));
    d_kp = &(g_array_index (prediction->keypoints, GstPoseKeypoint, link->d_kp_id));

    // Skip if source is not present or destination is already populated.
    if ((s_kp->confidence == 0.0) || (d_kp->confidence != 0.0))
      continue;

    // Extrapolate data from the source keypoint and populate the destination.
    gst_ml_traverse_skeleton_link (d_kp, link->d_kp_id, edge, n_edges, s_kp->x,
        s_kp->y, width, height, n_keypoints, heatmap, offsets, displacements);

    // Extract info from labels and populate the coresponding keypoint params.
    gst_ml_keypoint_populate_label_params (d_kp, link->d_kp_id, labels);

    GST_TRACE ("Keypoint: '%s' [%.2f x %.2f], confidence %.2f", d_kp->label,
        d_kp->x, d_kp->y, d_kp->confidence);
  }
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
  const gchar *input = NULL;
  GValue list = G_VALUE_INIT;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  input = gst_structure_get_string (settings, GST_ML_MODULE_OPT_LABELS);
  g_return_val_if_fail (gst_ml_parse_labels (input, &list), FALSE);

  submodule->labels = gst_ml_load_labels (&list);
  g_return_val_if_fail (submodule->labels != NULL, FALSE);

  // Fill the keypoints chain/tree.
  submodule->links = g_array_new (FALSE, FALSE, sizeof (GstPoseLink));
  submodule->connections = g_array_new (FALSE, FALSE, sizeof (GstPoseLink));

  // Recursiveli fill the skeleton chain/tree starting from label 0 as seed.
  if (!gst_ml_load_links (&list, 0, submodule->links)) {
    GST_ERROR ("Failed to load the skeleton chain/tree!");

    g_value_unset (&list);
    gst_structure_free (settings);
    return FALSE;
  }

  // Recursiveli fill the keypoint connections starting from label 0 as seed.
  if (!gst_ml_load_connections (&list, submodule->connections)) {
    GST_ERROR ("Failed to load the keypoint interconnections!");

    g_value_unset (&list);
    gst_structure_free (settings);
    return FALSE;
  }

  g_value_unset (&list);
  gst_structure_free (settings);
  return TRUE;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GArray *predictions = ((GArray *) output), *scores = NULL;
  GstProtectionMeta *pmeta = NULL;
  guint idx = 0, num = 0, width = 0, height = 0, n_keypoints = 0;
  gint sar_n = 1, sar_d = 1;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
  }

  n_keypoints = g_hash_table_size (submodule->labels);

  // Tensor keypoints count and number of labels need to match.
  if (GST_ML_FRAME_DIM (mlframe, 0, 3) != n_keypoints) {
    GST_ERROR ("Invalid number of loaded labels!");
    return FALSE;
  }

  // The 4th dimension of 3rd tensor represents the number of keypoint pairs
  // that make up the skeleton and their X & Y axis displacements values.
  if (submodule->links->len != (GST_ML_FRAME_DIM (mlframe, 2, 3) / 4)) {
    GST_ERROR ("Invalid number of loaded skeleton links!");
    return FALSE;
  }

  // Initial allocation for the Hough score map for each block of the matrix.
  scores = g_array_new (FALSE, FALSE, sizeof (GstHoughScore));

  // Find the keypoints with highest score for each block inside the heatmap.
  gst_ml_extract_hough_scores (scores, mlframe);

  // Sort the hough keypoint scores map by the their confidences.
  g_array_sort (scores, gst_ml_compare_scores);

  // Iterate over the hough scores and build up pose predictions.
  for (idx = 0; idx < scores->len; idx++) {
    GstHoughScore *score = &(g_array_index (scores, GstHoughScore, idx));
    GstPoseKeypoint keypoint = { 0, }, *kp = NULL;
    GstMLPrediction prediction = { 0, };

    // Check if current seed keypoint is not already part of a prediction.
    keypoint.x = score->x;
    keypoint.y = score->y;
    keypoint.confidence= score->confidence * 100;

    // Extract info from labels and populate the coresponding keypoint params.
    gst_ml_keypoint_populate_label_params (&keypoint, score->id, submodule->labels);

    // Non-Max Suppression (NMS) algorithm.
    // If the NMS result is below 0 don't create new pose prediction.
    if (gst_ml_non_max_suppression (&keypoint, predictions) < 0)
      continue;

    prediction.keypoints = g_array_sized_new (FALSE, TRUE,
        sizeof (GstPoseKeypoint), g_hash_table_size (submodule->labels));

    g_array_set_size (prediction.keypoints, n_keypoints);
    g_array_set_clear_func (prediction.keypoints, gst_ml_keypoint_free);

    // Copy the new seed inside the pose prediction struct.
    kp = &(g_array_index (prediction.keypoints, GstPoseKeypoint, score->id));
    memcpy (kp, &keypoint, sizeof (GstPoseKeypoint));

    // TODO: For now set the same connections.
    prediction.connections = submodule->connections;

    GST_TRACE ("Seed Keypoint: '%s' [%.2f x %.2f], confidence %.2f",
        kp->label, kp->x, kp->y, kp->confidence);

    // Traverse the skeleton links and populate pose keypoints.
    gst_ml_decode_pose_prediction (&prediction, mlframe, submodule->labels,
        submodule->links);

    predictions = g_array_append_val (predictions, prediction);
  }

  // The 2nd dimension of each tensor represents the matrix height.
  height = GST_ML_FRAME_DIM (mlframe, 0, 1);
  // The 3rd dimension of each tensor represents the matrix width.
  width = GST_ML_FRAME_DIM (mlframe, 0, 2);

  // TODO Optimize?
  // Transform coordinates to relative with extracted source aspect ratio.
  for (idx = 0; idx < predictions->len; idx++) {
    GstMLPrediction *prediction =
        &(g_array_index (predictions, GstMLPrediction, idx));

    for (num = 0; num < prediction->keypoints->len; num++) {
      GstPoseKeypoint *keypoint =
          &(g_array_index (prediction->keypoints, GstPoseKeypoint, num));

      gst_ml_keypoint_transform_coordinates (keypoint, sar_n, sar_d,
          (width - 1) * MATRIX_BLOCK_SIZE, (height - 1) * MATRIX_BLOCK_SIZE);

      prediction->confidence += keypoint->confidence;
    }

    prediction->confidence /= n_keypoints;
  }

  // Sort the hough keypoint scores map by the their confidences.
  g_array_sort (predictions, gst_ml_compare_predictions);

  return TRUE;
}
