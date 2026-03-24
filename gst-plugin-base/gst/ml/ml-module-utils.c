/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-module-utils.h"

#include <gst/utils/common-utils.h>

#define SUPPORTED_TENSORS_IDENTATION "                                "
#define CAPS_IDENTATION              "                                  "

// Global table for storing registered indeces of ML stages.
static GHashTable *ml_stage_table = NULL;
// Mutex for protecting access to the global table for ML stage indeces.
G_LOCK_DEFINE_STATIC (ml_stage_mutex);


static void
gst_ml_module_get_type (GstStructure * structure, GString * result)
{
  const GValue *list = NULL;
  guint length = 0, idx = 0;

  if (!gst_structure_has_field (structure, "type")) {
    GST_WARNING ("No field named 'type' in ml module caps!");
    return;
  }

  list = gst_structure_get_value (structure, "type");
  length = gst_value_list_get_size (list);

  g_string_append_printf (result, "%sType: ", CAPS_IDENTATION);

  for (idx = 0; idx < length; idx++) {
    const GValue *value = gst_value_list_get_value (list, idx);

    g_string_append (result, g_value_get_string (value));

    if ((idx + 1) < length)
      g_string_append (result, ", ");
  }

  g_string_append (result, "\n");
}

static void
gst_ml_module_get_dimensions (GstStructure * structure, GString * result)
{
  const GValue *dimensions = NULL;
  guint length = 0, idx = 0;

  if (!gst_structure_has_field (structure, "dimensions")) {
    GST_WARNING ("No field named 'dimensions' in ml module caps!");
    return;
  }

  dimensions = gst_structure_get_value (structure, "dimensions");
  length = gst_value_array_get_size (dimensions);

  for (idx = 0; idx < length; idx++) {
    const GValue *array = NULL;
    guint size = 0, num = 0;

    array = gst_value_array_get_value (dimensions, idx);

    if (array == NULL || !G_VALUE_HOLDS (array, GST_TYPE_ARRAY))
      continue;

    g_string_append_printf (result, "%sTensor %d: ", CAPS_IDENTATION, idx);
    size = gst_value_array_get_size (array);

    for (num = 0; num < size; num++) {
      const GValue *value = gst_value_array_get_value (array, num);

      if (value == NULL)
        continue;

      if (G_VALUE_HOLDS (value, GST_TYPE_INT_RANGE)) {
        gint min_value = gst_value_get_int_range_min (value);
        gint max_value = gst_value_get_int_range_max (value);

        g_string_append_printf (result, "%d-%d", min_value, max_value);
      } else {
        g_string_append_printf (result, "%d", g_value_get_int (value));
      }

      if ((num + 1) < size)
        g_string_append (result, ", ");
    }

    g_string_append (result, "\n");
  }
}

gchar *
gst_ml_caps_to_string (const GstCaps *caps)
{
  GstStructure *structure = NULL;
  GString *result = g_string_new ("");
  guint size = gst_caps_get_size (caps);
  guint idx = 0;

  g_string_append_printf (result, "\n%sSupported tensors:\n",
      SUPPORTED_TENSORS_IDENTATION);

  for (idx = 0; idx < size; idx++) {
    structure = gst_caps_get_structure (caps, idx);

    gst_ml_module_get_type (structure, result);
    gst_ml_module_get_dimensions (structure, result);
  }

  return g_string_free (result, FALSE);
}

gint8
gst_ml_stage_get_unique_index (void)
{
  gint8 index = 0;

  G_LOCK (ml_stage_mutex);

  if (ml_stage_table == NULL)
    ml_stage_table = g_hash_table_new (NULL, NULL);

  while (g_hash_table_contains (ml_stage_table, GINT_TO_POINTER (index)))
    index++;

  if (index >= 0)
    g_hash_table_insert (ml_stage_table, GINT_TO_POINTER (index), NULL);

  G_UNLOCK (ml_stage_mutex);

  return (index >= 0) ? index : (-1);
}

gboolean
gst_ml_stage_register_unique_index (gint8 index)
{
  gboolean exists = FALSE;

  if (index < 0)
    return FALSE;

  G_LOCK (ml_stage_mutex);

  if (ml_stage_table == NULL)
    ml_stage_table = g_hash_table_new (NULL, NULL);

  exists = g_hash_table_insert (ml_stage_table, GINT_TO_POINTER (index), NULL);

  G_UNLOCK (ml_stage_mutex);

  return !exists ? TRUE : FALSE;
}

void
gst_ml_stage_unregister_unique_index (gint8 index)
{
  G_LOCK (ml_stage_mutex);

  if (ml_stage_table == NULL)
    ml_stage_table = g_hash_table_new (NULL, NULL);

  g_hash_table_remove (ml_stage_table, GINT_TO_POINTER (index));

  if (g_hash_table_size (ml_stage_table) == 0)
    g_clear_pointer (&ml_stage_table, g_hash_table_unref);

  G_UNLOCK (ml_stage_mutex);
}

void
gst_ml_tensor_assign_value (GstMLType mltype, gpointer data, guint index,
    gdouble value)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      GST_INT8_PTR_CAST (data)[index] = (gint8) value;
      break;
    case GST_ML_TYPE_UINT8:
      GST_UINT8_PTR_CAST (data)[index] = (guint8) value;
      break;
    case GST_ML_TYPE_INT16:
      GST_INT16_PTR_CAST (data)[index] = (gint16) value;
      break;
    case GST_ML_TYPE_UINT16:
      GST_UINT16_PTR_CAST (data)[index] = (guint16) value;
      break;
    case GST_ML_TYPE_INT32:
      GST_INT32_PTR_CAST (data)[index] = (gint32) value;
      break;
    case GST_ML_TYPE_UINT32:
      GST_UINT32_PTR_CAST (data)[index] = (guint32) value;
      break;
    case GST_ML_TYPE_INT64:
      GST_INT64_PTR_CAST (data)[index] = (gint64) value;
      break;
    case GST_ML_TYPE_UINT64:
      GST_UINT64_PTR_CAST (data)[index] = (guint64) value;
      break;
#if defined(__ARM_FP16_FORMAT_IEEE)
    case GST_ML_TYPE_FLOAT16:
      GST_FLOAT16_PTR_CAST (data)[index] = (__fp16) value;
      break;
#endif //__ARM_FP16_FORMAT_IEEE
    case GST_ML_TYPE_FLOAT32:
      GST_FLOAT_PTR_CAST (data)[index] = (gfloat) value;
      break;
    default:
      break;
  }
}

gint
gst_ml_tensor_compare_values (GstMLType mltype, gpointer data, guint l_idx,
    guint r_idx)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      return GST_INT8_PTR_CAST (data)[l_idx] > GST_INT8_PTR_CAST (data)[r_idx] ? 1 :
          GST_INT8_PTR_CAST (data)[l_idx] < GST_INT8_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_UINT8:
      return GST_UINT8_PTR_CAST (data)[l_idx] > GST_UINT8_PTR_CAST (data)[r_idx] ? 1 :
          GST_UINT8_PTR_CAST (data)[l_idx] < GST_UINT8_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_INT16:
      return GST_INT16_PTR_CAST (data)[l_idx] > GST_INT16_PTR_CAST (data)[r_idx] ? 1 :
          GST_INT16_PTR_CAST (data)[l_idx] < GST_INT16_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_UINT16:
      return GST_UINT16_PTR_CAST (data)[l_idx] > GST_UINT16_PTR_CAST (data)[r_idx] ? 1 :
          GST_UINT16_PTR_CAST (data)[l_idx] < GST_UINT16_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_INT32:
      return GST_INT32_PTR_CAST (data)[l_idx] > GST_INT32_PTR_CAST (data)[r_idx] ? 1 :
          GST_INT32_PTR_CAST (data)[l_idx] < GST_INT32_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_UINT32:
      return GST_UINT32_PTR_CAST (data)[l_idx] > GST_UINT32_PTR_CAST (data)[r_idx] ? 1 :
          GST_UINT32_PTR_CAST (data)[l_idx] < GST_UINT32_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_INT64:
      return GST_INT64_PTR_CAST (data)[l_idx] > GST_INT64_PTR_CAST (data)[r_idx] ? 1 :
          GST_INT64_PTR_CAST (data)[l_idx] < GST_INT64_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_UINT64:
      return GST_UINT64_PTR_CAST (data)[l_idx] > GST_UINT64_PTR_CAST (data)[r_idx] ? 1 :
          GST_UINT64_PTR_CAST (data)[l_idx] < GST_UINT64_PTR_CAST (data)[r_idx] ? -1 : 0;
#if defined(__ARM_FP16_FORMAT_IEEE)
    case GST_ML_TYPE_FLOAT16:
      return GST_FLOAT16_PTR_CAST (data)[l_idx] > GST_FLOAT16_PTR_CAST (data)[r_idx] ? 1 :
          GST_FLOAT16_PTR_CAST (data)[l_idx] < GST_FLOAT16_PTR_CAST (data)[r_idx] ? -1 : 0;
#endif //__ARM_FP16_FORMAT_IEEE
    case GST_ML_TYPE_FLOAT32:
      return GST_FLOAT_PTR_CAST (data)[l_idx] > GST_FLOAT_PTR_CAST (data)[r_idx] ? 1 :
          GST_FLOAT_PTR_CAST (data)[l_idx] < GST_FLOAT_PTR_CAST (data)[r_idx] ? -1 : 0;
    default:
      break;
  }
  return 0;
}

gboolean
gst_ml_structure_has_source_dimensions (const GstStructure * structure)
{
  if (gst_structure_has_field (structure, "input-tensor-width") &&
      gst_structure_has_field (structure, "input-tensor-height"))
    return TRUE;

  return FALSE;
}

void
gst_ml_structure_set_source_dimensions (GstStructure * structure,
    guint width, guint height)
{
  gst_structure_set (structure, "input-tensor-width", G_TYPE_UINT, width,
      "input-tensor-height", G_TYPE_UINT, height, NULL);
}

void
gst_ml_structure_get_source_dimensions (const GstStructure * structure,
    guint * width, guint * height)
{
  gst_structure_get_uint (structure, "input-tensor-width", width);
  gst_structure_get_uint (structure, "input-tensor-height", height);
}

gboolean
gst_ml_structure_has_source_region (const GstStructure * structure)
{
  if (gst_structure_has_field (structure, "input-region-x") &&
      gst_structure_has_field (structure, "input-region-y") &&
      gst_structure_has_field (structure, "input-region-width") &&
      gst_structure_has_field (structure, "input-region-height"))
    return TRUE;

  return FALSE;
}

void
gst_ml_structure_set_source_region (GstStructure * structure,
    GstVideoRectangle * region)
{
  gst_structure_set (structure,
      "input-region-x", G_TYPE_INT, region->x,
      "input-region-y", G_TYPE_INT, region->y,
      "input-region-width", G_TYPE_INT, region->w,
      "input-region-height", G_TYPE_INT, region->h,
      NULL);
}

void
gst_ml_structure_get_source_region (const GstStructure * structure,
    GstVideoRectangle * region)
{
  gst_structure_get_int (structure, "input-region-x", &(region->x));
  gst_structure_get_int (structure, "input-region-y", &(region->y));
  gst_structure_get_int (structure, "input-region-width", &(region->w));
  gst_structure_get_int (structure, "input-region-height", &(region->h));
}
