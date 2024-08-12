/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <math.h>
#include <stdio.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-classification.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_OP_IS_SOFTMAX(op) \
    (op == GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX)

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define FACE_PID_SIZE           20
#define MAX_STRING_SIZE         64

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < <1, 512>, <1, 32>, <1, 2>, <1, 2>, <1, 2>, <1, 2> > "

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstFaceFeatures GstFaceFeatures;
typedef struct _GstFaceTemplate GstFaceTemplate;
typedef struct _GstMLSubModule GstMLSubModule;

struct _GstFaceFeatures {
  gfloat *half;
  gfloat *whole;
};

struct _GstFaceTemplate {
  gchar  name[FACE_PID_SIZE];
  gfloat *liveliness;
  GArray *features;
};

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // List of prediction labels.
  GHashTable *labels;
  // Confidence threshold value.
  gdouble    threshold;

  // Offset values for each of the tensors for dequantization of some tensors.
  gdouble    qoffsets[GST_ML_MAX_TENSORS];
  // Scale values for each of the tensors for dequantization of some tensors.
  gdouble    qscales[GST_ML_MAX_TENSORS];

  // Extra operations that need to apply
  gint       operation;

  // Loaded database with faces.
  GArray     *database;
};

static gboolean
gst_ml_module_load_face_database (GstMLSubModule * submodule, const guint idx,
    const gchar * filename)
{
  FILE *fp = NULL;
  GstFaceTemplate *face = NULL;
  GstMLLabel *label = NULL;
  guint32 num = 0, version = 0, n_features = 0, n_lvns_features = 0;
  guint32 size = 0, n_feature_templates = 0;
  gboolean success = FALSE;

  if ((fp = fopen (filename, "rb")) == NULL) {
    GST_ERROR ("Failed to open '%s'", filename);
    return FALSE;
  }

  size += fread (&version, sizeof (guint32), 1, fp);
  size += fread (&n_features, sizeof (guint32), 1, fp);
  size += fread (&n_lvns_features, sizeof (guint32), 1, fp);

  if (!(success = (size == 3))) {
    GST_ERROR ("Failed to read version, size and/or number of features!");
    goto cleanup;
  }

  GST_DEBUG ("Loaded database version %u with %u face features and %u "
      "liveliness features", version, n_features, n_lvns_features);

  success = (n_features == GST_ML_INFO_TENSOR_DIM (&(submodule->mlinfo), 0, 1));

  if (!success) {
    GST_ERROR ("Inavlid number of features, expected %u but loaded size is %u!",
        GST_ML_INFO_TENSOR_DIM (&(submodule->mlinfo), 0, 1), n_features);
    goto cleanup;
  }

  success = (n_lvns_features ==
      GST_ML_INFO_TENSOR_DIM (&(submodule->mlinfo), 1, 1));

  if (!success) {
    GST_ERROR ("Inavlid number of liveliness features, expected %u but loaded "
        "size is %u!", GST_ML_INFO_TENSOR_DIM (&(submodule->mlinfo), 1, 1),
        n_lvns_features);
    goto cleanup;
  }

  face = &(g_array_index (submodule->database, GstFaceTemplate, idx));
  face->liveliness = g_new (gfloat, n_lvns_features);

  size = fread (face->name, sizeof (gchar), FACE_PID_SIZE, fp);
  size += fread (face->liveliness, sizeof (gfloat), n_lvns_features, fp);
  size += fread (&n_feature_templates, sizeof (guint32), 1, fp);

  if (!(success = (size == (FACE_PID_SIZE + n_lvns_features + 1)))) {
    GST_ERROR ("Failed to read face header!");
    goto cleanup;
  }

  label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (idx));

  if (!(success = (g_strcmp0 (face->name, label->name) == 0))) {
    GST_ERROR ("Face name and label name do not match!");
    goto cleanup;
  }

  GST_DEBUG ("Face %u [%s] has %u features templates", idx, face->name,
      n_feature_templates);

  face->features = g_array_sized_new (FALSE, FALSE, sizeof (GstFaceFeatures),
      n_feature_templates);
  g_array_set_size (face->features, n_feature_templates);

  for (num = 0; num < n_feature_templates; num++) {
    GstFaceFeatures *features =
        &(g_array_index (face->features, GstFaceFeatures, num));

    features->half = g_new (gfloat, n_features);
    features->whole = g_new (gfloat, n_features);

    size = fread (features->half, sizeof (gfloat), n_features, fp);
    size += fread (features->whole, sizeof (gfloat), n_features, fp);

    if (!(success = (size == (n_features * 2)))) {
      GST_ERROR ("Failed to read face features!");
      goto cleanup;
    }
  }

cleanup:
  fclose (fp);

  return success;
}

static gboolean
gst_ml_module_load_databases (GstMLSubModule * submodule, const GValue * list)
{
  GstStructure *structure = NULL;
  const gchar *filename = NULL;
  guint idx = 0, size = 0;

  size = gst_value_list_get_size (list);

  submodule->database = g_array_sized_new (FALSE, FALSE,
      sizeof (GstFaceTemplate), size);
  g_array_set_size (submodule->database, size);

  for (idx = 0; idx < size; idx++) {
    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (list, idx)));

    if (!gst_structure_has_field (structure, "database")) {
      GST_ERROR ("Missing database for label at index %u !", idx);
      return FALSE;
    }

    filename = gst_structure_get_string (structure, "database");

    if (!gst_ml_module_load_face_database (submodule, idx, filename))
      return FALSE;
  }

  return TRUE;
}

static gdouble
gst_ml_tensor_softmax_sum (GstMLType mltype, gpointer data, guint n_entries,
    gdouble qoffset, gdouble qscale)
{
  guint idx = 0;
  gdouble sum = 0, value = 0;

  for (idx = 0; idx < n_entries; ++idx) {
    value = gst_ml_tensor_extract_value (mltype, data, idx, qoffset, qscale);
    sum += exp(value);
  }

  return sum;
}

static gfloat
gst_ml_cosine_similarity_score (GstMLType mltype, gpointer data,
    gfloat * database, guint n_entries, gdouble qoffset, gdouble qscale)
{
  guint idx = 0;
  gdouble value = 0.0, v1_pow2_sum = 0.0, v2_pow2_sum = 0.0, product = 0.0;

  for (idx = 0; idx < n_entries; ++idx) {
    // Extract the tensor value in float format.
    value = gst_ml_tensor_extract_value (mltype, data, idx, qoffset, qscale);

    // Calculate the vectors power of 2 sum and sum of the dot products.
    v1_pow2_sum += value * value;
    v2_pow2_sum += database[idx] * database[idx];
    product += value * database[idx];
  }

  if ((v1_pow2_sum < 0.1) || (v2_pow2_sum < 0.1))
    return 0.0;

  // Range -1 (opposite) to 1 (exactly the same), 0 indicating orthogonality.
  return product / sqrt (v1_pow2_sum) / sqrt (v2_pow2_sum);
}

static gfloat
gst_ml_cosine_distance_score (GstMLType mltype, gpointer data,
    gfloat * database, guint n_entries, gdouble qoffset, gdouble qscale)
{
  guint idx = 0;
  gfloat value = 0.0, v1_pow2_sum = 0.0, v2_pow2_sum = 0.0, product = 0.0;

  for (idx = 0; idx < n_entries; ++idx) {
    // Extract the tensor value in float format.
    value = gst_ml_tensor_extract_value (mltype, data, idx, qoffset, qscale);

    // Calculate the vectors power of 2 sum and sum of the dot products.
    v1_pow2_sum += value * value;
    v2_pow2_sum += database[idx] * database[idx];
    product += value * database[idx];
  }

  if ((v1_pow2_sum < 0.1) || (v2_pow2_sum < 0.1))
    return 0.0;

  value = product / (sqrt (v1_pow2_sum) * sqrt (v2_pow2_sum));
  return sqrtf (2 * (1 - value));
}

static void
gst_ml_module_face_recognition (GstMLSubModule * submodule,
    gint * person_id, gdouble * confidence, GstMLFrame * mlframe, guint index)
{
  GstFaceTemplate *face = NULL;
  GstFaceFeatures *features = NULL;
  gpointer data = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint num = 0, id = 0, n_features = 0;
  gfloat score = 0.0, maxscore = 0.0, maxconfidence = 0.0;
  gint pid = -1;

  mltype = GST_ML_FRAME_TYPE (mlframe);
  data = GST_ML_FRAME_BLOCK_DATA (mlframe, index);
  n_features = GST_ML_FRAME_DIM (mlframe, index, 1);

  for (id = 0; id < submodule->database->len; id++, maxscore = 0.0) {
    face = &(g_array_index (submodule->database, GstFaceTemplate, id));

    for (num = 0; num < face->features->len; num++) {
      features = &(g_array_index (face->features, GstFaceFeatures, num));

      // Calculate the similarity between the tensor data and the database.
      score = gst_ml_cosine_similarity_score (mltype, data, features->whole,
          n_features, submodule->qoffsets[index], submodule->qscales[index]);

      if (score <= maxscore)
        continue;

      maxscore = score;
    }

    GST_TRACE ("Face %u [%s] in database scored %f", id, face->name, maxscore);

    if (maxscore < maxconfidence)
      continue;

    maxconfidence = maxscore;
    pid = id;
  }

  *person_id = pid;
  *confidence = maxconfidence;
}

static gboolean
gst_ml_module_face_has_liveliness (GstMLSubModule * submodule,
    GstFaceTemplate * face, GstMLFrame * mlframe, guint index)
{
  gpointer data = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint n_features = 0;
  gdouble score = 0.0;

  mltype = GST_ML_FRAME_TYPE (mlframe);
  data = GST_ML_FRAME_BLOCK_DATA (mlframe, index);
  n_features = GST_ML_FRAME_DIM (mlframe, index, 1);

  // Liveliness score using cosine distance between tensor data and database.
  score = gst_ml_cosine_distance_score (mltype, data, face->liveliness,
      n_features, submodule->qoffsets[index], submodule->qscales[index]);

  GST_TRACE ("Face %s has liveliness score %f", face->name, score);
  return (score >= submodule->threshold) ? TRUE : FALSE;
}


static gfloat
gst_ml_module_accessory_tensor_score (GstMLSubModule * submodule,
    GstMLFrame * mlframe, guint index)
{
  gpointer data = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint n_values = 0;
  gfloat sum = 0.0, score = 0.0;

  mltype = GST_ML_FRAME_TYPE (mlframe);
  data = GST_ML_FRAME_BLOCK_DATA (mlframe, index);
  n_values = GST_ML_FRAME_DIM (mlframe, index, 1);

  // Two possible values scores, TRUE or FALSE.
  if (n_values != 2)
    return 0.0;

  if (GST_ML_OP_IS_SOFTMAX (submodule->operation)) {
    // Calculate the sum of the exponents for softmax function.
    sum = gst_ml_tensor_softmax_sum (mltype, data, n_values,
        submodule->qoffsets[index], submodule->qscales[index]);
  }

  // Second value corresponds to TRUE score.
  score = gst_ml_tensor_extract_value (mltype, data, 1,
      submodule->qoffsets[index], submodule->qscales[index]);

  // Apply softmax function on the confidence result.
  if (submodule->operation == GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX)
    score = (exp (score) / sum);

  return score;
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

  if (submodule->database != NULL) {
    guint idx = 0, num = 0;

    for (idx = 0; idx < submodule->database->len; idx++) {
      GstFaceTemplate *face =
          &(g_array_index (submodule->database, GstFaceTemplate, idx));

      for (num = 0; num < face->features->len; num++) {
        GstFaceFeatures *features =
            &(g_array_index (face->features, GstFaceFeatures, num));

        g_clear_pointer (&(features->half), g_free);
        g_clear_pointer (&(features->whole), g_free);
      }

      g_clear_pointer (&(face->liveliness), g_free);
      g_array_free (face->features, TRUE);
    }

    g_array_free (submodule->database, TRUE);
  }

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

  if (!(success = gst_ml_module_load_databases (submodule, &list)))
    goto cleanup;

  success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_THRESHOLD);
  if (!success) {
    GST_ERROR ("Settings stucture does not contain threshold value!");
    goto cleanup;
  }

  gst_structure_get_double (settings, GST_ML_MODULE_OPT_THRESHOLD, &threshold);
  submodule->threshold = threshold / 100;

  gst_structure_get_enum (settings, GST_ML_MODULE_OPT_XTRA_OPERATION, G_TYPE_ENUM,
      &submodule->operation);

  GST_INFO ("Extra operation selected: %u", submodule->operation);

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
  GArray *predictions = (GArray *) output;
  GstMLClassPrediction *prediction = NULL;
  GstProtectionMeta *pmeta = NULL;
  GstMLLabel *label = NULL;
  GstFaceTemplate *face = NULL;
  GstMLClassEntry *entry = NULL;
  gint pid = -1;
  gdouble confidence = 0.0, score = 0.0;
  gboolean has_lvns = FALSE, has_mask = FALSE, has_glasses = FALSE;
  gboolean has_open_eyes = FALSE, has_sunglasses = FALSE;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  prediction = &(g_array_index (predictions, GstMLClassPrediction, 0));
  prediction->info = pmeta->info;

  // Allocate only single prediction entry result.
  g_array_set_size (prediction->entries, 1);
  entry = &(g_array_index (prediction->entries, GstMLClassEntry, 0));

  entry->name = g_quark_from_string ("UNKNOWN");
  entry->color = 0xFF0000FF;

  // If face is not recognized there is no poit of continuing.
  gst_ml_module_face_recognition (submodule, &pid, &confidence, mlframe, 0);

  entry->confidence = (pid != -1) ? confidence : (100.0 - confidence);
  entry->confidence *= 100.0;

  if ((pid == -1) || (confidence < submodule->threshold))
    return TRUE;

  label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (pid));
  face = &(g_array_index (submodule->database, GstFaceTemplate, pid));

  entry->color = label ? label->color : 0xFF0000FF;

  GST_LOG ("Recognized face %d [%s] in the database", pid, face->name);

  // Exract the max score from the open eyes tensor and set the bool flag.
  score = gst_ml_module_accessory_tensor_score (submodule, mlframe, 2);
  has_open_eyes = (score >= submodule->threshold) ? TRUE : FALSE;

  GST_TRACE ("Face %s has open eyes score %f", face->name, score);

  // Exract the max score from the glasses tensor and set the bool flag.
  score = gst_ml_module_accessory_tensor_score (submodule, mlframe, 3);
  has_glasses = (score >= submodule->threshold) ? TRUE : FALSE;

  GST_TRACE ("Face %s has glasses score %f", face->name, score);

  // Exract the max score from the mask tensor and set the bool flag.
  score = gst_ml_module_accessory_tensor_score (submodule, mlframe, 4);
  has_mask = (score >= submodule->threshold) ? TRUE : FALSE;

  GST_TRACE ("Face %s has mask score %f", face->name, score);

  // Exract the max score from the sunglasses tensor and set the bool flag.
  score = gst_ml_module_accessory_tensor_score (submodule, mlframe, 5);
  has_sunglasses = (score >= submodule->threshold) ? TRUE : FALSE;

  GST_TRACE ("Face %s has sunglasses score %f", face->name, score);

  // Check for face liveliness only if mask wasn't detected.
  if (!has_mask)
    has_lvns = gst_ml_module_face_has_liveliness (submodule, face, mlframe, 1);

  GST_LOG ("Face %s, Lively: %s, Open Eyes: %s, Mask: %s, Glasses: %s, "
      "Sunglasses: %s", label->name, has_lvns ? "YES" : "NO",
      has_open_eyes ? "YES" : "NO", has_mask ? "YES" : "NO",
      has_glasses ? "YES" : "NO", has_sunglasses ? "YES" : "NO");

  entry->name = g_quark_from_string (label->name);

  return TRUE;
}
