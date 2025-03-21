/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <math.h>
#include <stdio.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-pose.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, 512>, <1, 265> >; " \
    "neural-network/tensors, " \
    "type = (string) { UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, 265> >"

#define ALPHA_ID_SIZE          219
#define ALPHA_EXP_SIZE         39

// List of for the true index for each supported landmark.
static const guint lmk_idx[] = {
    662, 660, 659, 669, 750, 700, 583, 560, 561, 608, 966, 712, 708, 707, 557,
    554, 880, 2278, 2275, 2276, 2284, 2360, 2314, 2203, 2181, 2180, 2227, 2553,
    2325, 2321, 2322, 2176, 2175, 1852, 1867, 1877, 1869, 1870, 1848, 1851,
    1846, 1842, 219, 218, 226, 216, 201, 191, 195, 198, 197, 148, 150, 299, 281,
    1796, 1935, 2580, 2003, 1974, 331, 138, 290, 993, 366, 333, 2532, 2498,
    2489, 2519, 3189, 2515, 2517, 2805, 0, 1615, 932, 900, 911, 945, 1229, 930,
    926, 0, 2073, 2104, 398, 470, 443, 1627, 2119, 487, 393, 2030, 2080, 448,
    2130, 506, 498, 2163, 540, 536, 2161, 534, 0, 256
};

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

  // Face related databases.
  GArray     *meanface;
  GArray     *shapebasis;
  GArray     *blendshape;

  // Rotational 3x3 matrices.
  gfloat     roll_matrix[9];
  gfloat     yaw_matrix[9];
  gfloat     pitch_matrix[9];
};

static gboolean
gst_maxtrix_multiplication (gfloat * outmatrix, const gfloat * l_matrix,
    const guint l_rows, const guint l_columns, const gfloat * r_matrix,
    const guint r_rows, const guint r_columns)
{
  guint row = 0, col = 0, idx = 0;

  if (l_columns != r_rows)
    return FALSE;

  for (row = 0; row < l_rows; row++) {
    for (col = 0; col < r_columns; col++) {
      gfloat sum = 0.0;

      for (idx = 0; idx < l_columns; idx++)
        sum += l_matrix[(row * l_columns) + idx] * r_matrix[idx * r_columns + col];

      outmatrix[row * r_columns + col] = sum;
    }
  }

  return TRUE;
}

static GArray *
gst_load_binary_database (const gchar * filename, const guint n_values)
{
  FILE *fp = NULL;
  GArray *contents = NULL, *database = NULL;
  guint size = 0, idx = 0, num = 0;

  if ((fp = fopen (filename, "rb")) == NULL) {
    GST_ERROR ("Failed to open '%s'", filename);
    return NULL;
  }

  fseek (fp, 0, SEEK_END);
  size = ftell (fp);
  fseek (fp, 0, SEEK_SET);

  contents = g_array_sized_new (FALSE, FALSE, sizeof (guint8), size);
  g_array_set_size (contents, size);

  size = fread (contents->data, sizeof (guint8), size, fp);
  fclose (fp);

  if (size != contents->len) {
    GST_ERROR ("Failed to read '%s'", filename);
    goto cleanup;
  }

  // Calculate the size of the local database base on the number of supported
  // landmarks, number of axis and number of values for each landmark.
  size = G_N_ELEMENTS (lmk_idx) * 3 * n_values;

  if (size > contents->len) {
    GST_ERROR ("Invalid database size, expected %u but actual size is %u !",
        size, contents->len);
    goto cleanup;
  }

  database = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), size);
  g_array_set_size (database, size);

  // Etract the necessary values based on the actual landmark index.
  for (idx = 0; idx < G_N_ELEMENTS (lmk_idx); idx++) {
    for (num = 0; num < n_values; num++) {
      g_array_index (database, gfloat, (idx * 3 + 0) * n_values + num) =
          g_array_index (contents, gfloat, (lmk_idx[idx] * 3 + 0) * n_values + num);

      g_array_index (database, gfloat, (idx * 3 + 1) * n_values + num) =
          g_array_index (contents, gfloat, (lmk_idx[idx] * 3 + 1) * n_values + num);

      g_array_index (database, gfloat, (idx * 3 + 2) * n_values + num) =
          g_array_index (contents, gfloat, (lmk_idx[idx] * 3 + 2) * n_values + num);
    }
  }

cleanup:
  g_array_free (contents, TRUE);
  return database;
}

static gboolean
gst_ml_module_load_databases (GstMLSubModule * submodule, const GValue * list)
{
  GstStructure *structure = NULL;
  const gchar *location = NULL;
  guint size = 0;

  if ((size = gst_value_list_get_size (list)) != 3) {
    GST_ERROR ("Expecting 3 values in the labels list but git %u!", size);
    return FALSE;
  }

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, 0)));

  if (!gst_structure_has_name (structure, "mean-face")) {
    GST_ERROR ("Missing entry for mean-face !");
    return FALSE;
  } else if (!gst_structure_has_field (structure, "location")) {
    GST_ERROR ("Missing location for entry for mean-face !");
    return FALSE;
  }

  location = gst_structure_get_string (structure, "location");
  submodule->meanface = gst_load_binary_database (location, 1);

  if (submodule->meanface == NULL)
    return FALSE;

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, 1)));

  if (!gst_structure_has_name (structure, "shape-basis")) {
    GST_ERROR ("Missing entry for shape-basis !");
    return FALSE;
  } else if (!gst_structure_has_field (structure, "location")) {
    GST_ERROR ("Missing location for entry for shape-basis !");
    return FALSE;
  }

  location = gst_structure_get_string (structure, "location");
  submodule->shapebasis = gst_load_binary_database (location, ALPHA_ID_SIZE);

  if (submodule->shapebasis == NULL)
    return FALSE;

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, 2)));

  if (!gst_structure_has_name (structure, "blend-shape")) {
    GST_ERROR ("Missing entry for blend-shape !");
    return FALSE;
  } else if (!gst_structure_has_field (structure, "location")) {
    GST_ERROR ("Missing location for entry for blend-shape !");
    return FALSE;
  }

  location = gst_structure_get_string (structure, "location");
  submodule->blendshape = gst_load_binary_database (location, ALPHA_EXP_SIZE);

  if (submodule->blendshape == NULL)
    return FALSE;

  return TRUE;
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

  if (submodule->meanface != NULL)
    g_array_free (submodule->meanface, TRUE);

  if (submodule->shapebasis != NULL)
    g_array_free (submodule->shapebasis, TRUE);

  if (submodule->blendshape != NULL)
    g_array_free (submodule->blendshape, TRUE);


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

  if (!(success = gst_ml_module_load_databases (submodule, &list)))
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
  GstMLPosePrediction *prediction = NULL;
  GstMLPoseEntry *entry = NULL;
  GstVideoRectangle region = { 0, };
  gpointer vertices = NULL;
  gfloat matrix[9] = { 0, };
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gfloat confidence = 0.0, roll = 0.0, yaw = 0.0, pitch = 0.0, value = 0.0;
  gfloat tx = 0.0, ty = 0.0, tf = 0.0, x = 0.0, y = 0.0, z = 0.0;
  gfloat tmp_x = 0.0, tmp_y = 0.0, tmp_z = 0.0;
  guint n_vertices = 0, idx = 0, num = 0, id = 0;
  guint vertices_idx = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLPosePrediction, 0));
  prediction->info = pmeta->info;

  // Extract the dimensions of the input tensor that produced the output tensors.
  if (submodule->inwidth == 0 || submodule->inheight == 0) {
    gst_ml_structure_get_source_dimensions (pmeta->info, &(submodule->inwidth),
        &(submodule->inheight));
  }

  // Extract the source tensor region with actual data.
  gst_ml_structure_get_source_region (pmeta->info, &region);

  mltype = GST_ML_FRAME_TYPE (mlframe);

  if (GST_ML_INFO_N_TENSORS (&(submodule->mlinfo)) == 2)
    vertices_idx = 1;

  vertices = GST_ML_FRAME_BLOCK_DATA (mlframe, vertices_idx);
  n_vertices = GST_ML_FRAME_DIM (mlframe, vertices_idx, 1);

  confidence = gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 1,
        submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx]);

  GST_LOG ("Confidence[%f]", confidence);

  if (confidence < submodule->threshold)
    return TRUE;

  // Translation values on the Z, Y and X axis respectively.
  // TODO: What is tf ?? What are those coefficients ??
  tf = (gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 2,
      submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
      * 150.0F) + 450.0F;
  ty = gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 3,
      submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
      * 60.0F;
  tx = gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 4,
      submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
      * 60.0F;

  GST_LOG ("Translation coordinates X[%f] Y[%f] F[%f]", tx, ty, tf);

  // The rotational angles along the 3 axis in radians.
  // TODO: What are those coefficients ??
  roll = gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 5,
      submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
      * M_PI / 2;
  yaw = gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 6,
      submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
      * M_PI / 2;
  pitch = (gst_ml_tensor_extract_value (mltype, vertices, n_vertices - 7,
      submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
      * M_PI / 2) + M_PI;

  GST_LOG ("Roll[%f] Yaw[%f] Pitch[%f]", roll, yaw, pitch);

  submodule->roll_matrix[0] = cos(-roll);
  submodule->roll_matrix[1] = -sin(-roll);
  submodule->roll_matrix[2] = 0.0;
  submodule->roll_matrix[3] = sin(-roll);
  submodule->roll_matrix[4] = cos(-roll);
  submodule->roll_matrix[5] = 0.0;
  submodule->roll_matrix[6] = 0.0;
  submodule->roll_matrix[7] = 0.0;
  submodule->roll_matrix[8] = 1.0;

  submodule->yaw_matrix[0] = cos(-yaw);
  submodule->yaw_matrix[1] = 0.0;
  submodule->yaw_matrix[2] = sin(-yaw);
  submodule->yaw_matrix[3] = 0.0;
  submodule->yaw_matrix[4] = 1.0;
  submodule->yaw_matrix[5] = 0.0;
  submodule->yaw_matrix[6] = -sin(-yaw);
  submodule->yaw_matrix[7] = 0.0;
  submodule->yaw_matrix[8] = cos(-yaw);

  submodule->pitch_matrix[0] = 1.0;
  submodule->pitch_matrix[1] = 0.0;
  submodule->pitch_matrix[2] = 0.0;
  submodule->pitch_matrix[3] = 0.0;
  submodule->pitch_matrix[4] = cos(-pitch);
  submodule->pitch_matrix[5] = -sin(-pitch);
  submodule->pitch_matrix[6] = 0.0;
  submodule->pitch_matrix[7] = sin(-pitch);
  submodule->pitch_matrix[8] = cos(-pitch);

  gst_maxtrix_multiplication (matrix, submodule->pitch_matrix, 3, 3,
      submodule->roll_matrix, 3, 3);
  gst_maxtrix_multiplication (submodule->roll_matrix,
      submodule->yaw_matrix, 3, 3, matrix, 3, 3);

  // Allocate only single prediction result.
  g_array_set_size (prediction->entries, 1);
  entry = &(g_array_index (prediction->entries, GstMLPoseEntry, 0));

  // Allocate memory for the keypoints.
  entry->keypoints = g_array_sized_new (FALSE, TRUE, sizeof (GstMLKeypoint),
      G_N_ELEMENTS (lmk_idx) / 2);
  g_array_set_size (entry->keypoints, G_N_ELEMENTS (lmk_idx) / 2);

  entry->confidence = confidence * 100.0;
  entry->connections = NULL;

  for (idx = 0; idx < G_N_ELEMENTS (lmk_idx); idx += 2) {
    GstMLKeypoint *kp = &(g_array_index (entry->keypoints, GstMLKeypoint, idx / 2));

    id = idx * 3;

    x = g_array_index (submodule->meanface, gfloat, id + 0);
    y = g_array_index (submodule->meanface, gfloat, id + 1);
    z = g_array_index (submodule->meanface, gfloat, id + 2);

    for (num = 0; num < ALPHA_ID_SIZE; num++) {
      value = gst_ml_tensor_extract_value (mltype, vertices, num,
          submodule->qoffsets[1], submodule->qscales[1]) * 3.0F;

      id = idx * 3 * ALPHA_ID_SIZE + num;
      x += value * g_array_index (submodule->shapebasis, gfloat, id);

      id = (idx * 3 + 1) * ALPHA_ID_SIZE + num;
      y += value * g_array_index (submodule->shapebasis, gfloat, id);

      id = (idx * 3 + 2) * ALPHA_ID_SIZE + num;
      z += value * g_array_index (submodule->shapebasis, gfloat, id);
    }

    for (num = 0; num < ALPHA_EXP_SIZE; num++) {
      value = (gst_ml_tensor_extract_value (mltype, vertices, ALPHA_ID_SIZE + num,
          submodule->qoffsets[vertices_idx], submodule->qscales[vertices_idx])
          * 0.5F) + 0.5F;

      id = idx * 3 * ALPHA_EXP_SIZE + num;
      x += value * g_array_index (submodule->blendshape, gfloat, id);

      id = (idx * 3 + 1) * ALPHA_EXP_SIZE + num;
      y += value * g_array_index (submodule->blendshape, gfloat, id);

      id = (idx * 3 + 2) * ALPHA_EXP_SIZE + num;
      z += value * g_array_index (submodule->blendshape, gfloat, id);
    }

    tmp_x = (x * submodule->roll_matrix[0]) +
        (y * submodule->roll_matrix[1]) + (z * submodule->roll_matrix[2]);
    tmp_y = (x * submodule->roll_matrix[3]) +
        (y * submodule->roll_matrix[4]) + (z * submodule->roll_matrix[5]);
    tmp_z = (x * submodule->roll_matrix[6]) +
        (y * submodule->roll_matrix[7]) + (z * submodule->roll_matrix[8]);

    x = tmp_x + tx;
    y = tmp_y + ty;
    z = tmp_z + 500.0F;

    kp->x = (x * tf / 500.0F) + (submodule->inwidth / 2);
    kp->y = (y * tf / 500.0F) + (submodule->inheight / 2);

    gst_ml_keypoint_transform_coordinates (kp, &region);

    kp->name = g_quark_from_static_string ("unknown");
    kp->color = 0xFF0000FF;
    kp->confidence = confidence * 100.0;

    GST_TRACE ("Keypoint: %u [%f x %f], confidence %f", idx / 2, kp->x, kp->y,
        kp->confidence);
  }

  entry->xtraparams = gst_structure_new ("ExtraParams", "roll", G_TYPE_FLOAT,
      roll, "yaw", G_TYPE_FLOAT, yaw, "pitch", G_TYPE_FLOAT, pitch, NULL);

  return TRUE;
}
