/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-module-utils.h"

gdouble
gst_ml_tensor_extract_value (GstMLType mltype, gpointer data, guint idx,
    gdouble offset, gdouble scale)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      return ((GINT8_PTR_CAST (data))[idx] - offset) * scale;
    case GST_ML_TYPE_UINT8:
      return ((GUINT8_PTR_CAST (data))[idx] - offset) * scale;
    case GST_ML_TYPE_INT32:
      return (GINT32_PTR_CAST (data))[idx];
    case GST_ML_TYPE_UINT32:
      return (GUINT32_PTR_CAST (data))[idx];
    case GST_ML_TYPE_FLOAT32:
      return (GFLOAT_PTR_CAST (data))[idx];
    default:
      break;
  }
  return 0.0;
}

gint
gst_ml_tensor_compare_values (GstMLType mltype, gpointer data, guint l_idx,
    guint r_idx)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      return GINT8_PTR_CAST (data)[l_idx] > GINT8_PTR_CAST (data)[r_idx] ? 1 :
          GINT8_PTR_CAST (data)[l_idx] < GINT8_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_UINT8:
      return GUINT8_PTR_CAST (data)[l_idx] > GUINT8_PTR_CAST (data)[r_idx] ? 1 :
          GUINT8_PTR_CAST (data)[l_idx] < GUINT8_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_INT32:
      return GINT32_PTR_CAST (data)[l_idx] > GINT32_PTR_CAST (data)[r_idx] ? 1 :
          GINT32_PTR_CAST (data)[l_idx] < GINT32_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_UINT32:
      return GUINT32_PTR_CAST (data)[l_idx] > GUINT32_PTR_CAST (data)[r_idx] ? 1 :
          GUINT32_PTR_CAST (data)[l_idx] < GUINT32_PTR_CAST (data)[r_idx] ? -1 : 0;
    case GST_ML_TYPE_FLOAT32:
      return GFLOAT_PTR_CAST (data)[l_idx] > GFLOAT_PTR_CAST (data)[r_idx] ? 1 :
          GFLOAT_PTR_CAST (data)[l_idx] < GFLOAT_PTR_CAST (data)[r_idx] ? -1 : 0;
    default:
      break;
  }
  return 0;
}

void
gst_ml_protecton_meta_set_source_dimensions (GstProtectionMeta * pmeta,
    guint width, guint height)
{
  gst_structure_set (pmeta->info, "input-tensor-width", G_TYPE_UINT, width,
      "input-tensor-height", G_TYPE_UINT, height, NULL);
}

void
gst_ml_protecton_meta_get_source_dimensions (GstProtectionMeta * pmeta,
    guint * width, guint * height)
{
  gst_structure_get_uint (pmeta->info, "input-tensor-width", width);
  gst_structure_get_uint (pmeta->info, "input-tensor-height", height);
}

void
gst_ml_protecton_meta_set_source_region (GstProtectionMeta * pmeta,
    GstVideoRectangle * region)
{
  gst_structure_set (pmeta->info,
      "input-region-x", G_TYPE_INT, region->x,
      "input-region-y", G_TYPE_INT, region->y,
      "input-region-width", G_TYPE_INT, region->w,
      "input-region-height", G_TYPE_INT, region->h,
      NULL);
}

void
gst_ml_protecton_meta_get_source_region (GstProtectionMeta * pmeta,
    GstVideoRectangle * region)
{
  gst_structure_get_int (pmeta->info, "input-region-x", &(region->x));
  gst_structure_get_int (pmeta->info, "input-region-y", &(region->y));
  gst_structure_get_int (pmeta->info, "input-region-width", &(region->w));
  gst_structure_get_int (pmeta->info, "input-region-height", &(region->h));
}
