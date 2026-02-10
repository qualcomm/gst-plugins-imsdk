/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-module-utils.h"

#include <gst/utils/common-utils.h>

// Global table for storing registered indeces of ML stages.
static GHashTable *ml_stage_table = NULL;
// Mutex for protecting access to the global table for ML stage indeces.
G_LOCK_DEFINE_STATIC (ml_stage_mutex);


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
