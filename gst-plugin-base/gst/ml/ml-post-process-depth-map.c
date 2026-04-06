/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-post-process-depth-map.h"

/**
 * GstMLDepthMaps:
 * @refcount: thread safe reference counter
 * @entries: (element-type GstMLDepthMap): A #GArray of #GstMLDepthMap
 *
 * Information describing a group of prediction results beloging to the same batch.
 */
struct _GstMLDepthMaps {
  gatomicrefcount refcount;
  GArray          *entries;
};

G_DEFINE_BOXED_TYPE (GstMLDepthMap, gst_ml_depth_map,
    (GBoxedCopyFunc) gst_ml_depth_map_copy,
    (GBoxedFreeFunc) gst_ml_depth_map_free);

G_DEFINE_BOXED_TYPE (GstMLDepthMaps, gst_ml_depth_maps,
    (GBoxedCopyFunc) gst_ml_depth_maps_ref,
    (GBoxedFreeFunc) gst_ml_depth_maps_unref);

void
gst_ml_depth_map_reset (GstMLDepthMap * depthmap)
{
  g_return_if_fail (depthmap != NULL);

  depthmap->n_rows = 0;
  depthmap->n_columns = 0.0;

  g_clear_pointer (&depthmap->values, g_array_unref);
  g_clear_pointer (&depthmap->colors, g_array_unref);
  g_clear_pointer (&depthmap->xtraparams, gst_structure_free);
}

GstMLDepthMap*
gst_ml_depth_map_copy (const GstMLDepthMap * depthmap)
{
  GstMLDepthMap *newdepth_map = NULL;

  g_return_val_if_fail (depthmap != NULL, NULL);

  newdepth_map = g_slice_new (GstMLDepthMap);

  newdepth_map->n_rows = depthmap->n_rows;
  newdepth_map->n_columns = depthmap->n_columns;

  newdepth_map->values = g_array_copy (depthmap->values);
  newdepth_map->colors = g_array_copy (depthmap->colors);
  newdepth_map->xtraparams = gst_structure_copy (depthmap->xtraparams);

  return newdepth_map;
}

void
gst_ml_depth_map_free (GstMLDepthMap * depthmap)
{
  if (depthmap == NULL)
    return;

  gst_ml_depth_map_reset (depthmap);
  g_slice_free (GstMLDepthMap, depthmap);
}

GstStructure *
gst_ml_depth_map_to_structure (GstMLDepthMap * depthmap)
{
  GstStructure *structure = NULL;
  const guchar *data = NULL;
  GValue value = G_VALUE_INIT;
  gsize size = 0;

  structure = gst_structure_new ("depth-map", "rows", G_TYPE_UINT,
      depthmap->n_rows, "columns", G_TYPE_UINT, depthmap->n_columns,
      NULL);

  g_value_init (&value, G_TYPE_STRING);

  if (depthmap->values != NULL && (depthmap->values->len > 0)) {
    data = (const guchar*) depthmap->values->data;
    size = depthmap->values->len * sizeof (gdouble);
    g_value_take_string (&value, g_base64_encode (data, size));
  }

  gst_structure_take_value (structure, "values", &value);
  g_value_init (&value, G_TYPE_STRING);

  if (depthmap->colors != NULL && (depthmap->colors->len > 0)) {
    data = (const guchar*) depthmap->colors->data;
    size = depthmap->colors->len * sizeof (guint32);
    g_value_take_string (&value, g_base64_encode (data, size));
  }

  gst_structure_take_value (structure, "colors", &value);

  if (depthmap->xtraparams != NULL) {
    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, g_steal_pointer (&depthmap->xtraparams));
    gst_structure_take_value (structure, "xtraparams", &value);
  }

  gst_ml_depth_map_reset (depthmap);
  return structure;
}

GstMLDepthMaps*
gst_ml_depth_maps_new (void)
{
  GstMLDepthMaps *depthmaps = g_slice_new (GstMLDepthMaps);

  g_atomic_ref_count_init (&depthmaps->refcount);
  depthmaps->entries = g_array_new (FALSE, TRUE, sizeof (GstMLDepthMap));

  g_array_set_clear_func (depthmaps->entries,
      (GDestroyNotify) gst_ml_depth_map_reset);

  return depthmaps;
}

GstMLDepthMaps*
gst_ml_depth_maps_new_sized (guint size)
{
  GstMLDepthMaps *depthmaps = g_slice_new (GstMLDepthMaps);

  g_atomic_ref_count_init (&depthmaps->refcount);
  depthmaps->entries =
      g_array_sized_new (FALSE, TRUE, sizeof (GstMLDepthMap), size);

  g_array_set_clear_func (depthmaps->entries,
      (GDestroyNotify) gst_ml_depth_map_reset);

  g_array_set_size (depthmaps->entries, size);
  return depthmaps;
}

GstMLDepthMaps*
gst_ml_depth_maps_ref (GstMLDepthMaps * depthmaps)
{
  g_return_val_if_fail (depthmaps != NULL, NULL);

  g_atomic_ref_count_inc (&depthmaps->refcount);
  return depthmaps;
}

void
gst_ml_depth_maps_unref (GstMLDepthMaps * depthmaps)
{
  g_return_if_fail (depthmaps != NULL);

  if (g_atomic_ref_count_dec (&depthmaps->refcount)) {
    g_array_free (depthmaps->entries, TRUE);
    g_slice_free (GstMLDepthMaps, depthmaps);
  }
}

GstMLDepthMaps *
gst_ml_depth_maps_copy (const GstMLDepthMaps * depthmaps)
{
  GstMLDepthMaps *newdepth_maps = NULL;

  g_return_val_if_fail (depthmaps != NULL, NULL);

  newdepth_maps = g_slice_new (GstMLDepthMaps);
  newdepth_maps->entries = g_array_copy (depthmaps->entries);
  g_atomic_ref_count_init (&newdepth_maps->refcount);

  return newdepth_maps;
}

void
gst_ml_depth_maps_append (GstMLDepthMaps * depthmaps,
    const GstMLDepthMap * depthmap)
{
  g_return_if_fail (depthmaps != NULL);
  g_array_append_vals (depthmaps->entries, depthmap, 1);
}

void
gst_ml_depth_maps_insert (GstMLDepthMaps * depthmaps, guint index,
    const GstMLDepthMap * depthmap)
{
  g_return_if_fail (depthmaps != NULL);
  g_array_insert_vals (depthmaps->entries, index, depthmap, 1);
}

void
gst_ml_depth_maps_remove (GstMLDepthMaps * depthmaps, guint index)
{
  g_return_if_fail (depthmaps != NULL);
  g_array_remove_index (depthmaps->entries, index);
}

GstMLDepthMap*
gst_ml_depth_maps_entry (GstMLDepthMaps * depthmaps, guint index)
{
  g_return_val_if_fail (depthmaps != NULL, NULL);
  return &(g_array_index (depthmaps->entries, GstMLDepthMap, index));
}

guint
gst_ml_depth_maps_size (GstMLDepthMaps * depthmaps)
{
  g_return_val_if_fail (depthmaps != NULL, 0);
  return depthmaps->entries->len;
}

void
gst_ml_depth_maps_resize (GstMLDepthMaps * depthmaps, guint size)
{
  g_return_if_fail (depthmaps != NULL);
  g_array_set_size (depthmaps->entries, size);
}
