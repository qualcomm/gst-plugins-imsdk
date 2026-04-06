/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_POST_PROCESS_DEPTH_MAP_H__
#define __GST_QTI_ML_POST_PROCESS_DEPTH_MAP_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_ML_DEPTH_MAPS_CAST(obj)      ((GstMLDepthMaps*)(obj))

typedef struct _GstMLDepthMap GstMLDepthMap;
typedef struct _GstMLDepthMaps GstMLDepthMaps;

#define GST_TYPE_ML_DEPTH_MAP            (gst_ml_depth_map_get_type ())
GST_API GType gst_ml_depth_map_get_type  (void);

#define GST_TYPE_ML_DEPTH_MAPS           (gst_ml_depth_maps_get_type ())
GST_API GType gst_ml_depth_maps_get_type (void);

/**
 * GstMLDepthMap:
 * @values: (element-type gdouble):
 *          A #GArray of #gdouble depth value for each index in the map.
 * @colors: (optional) (element-type guint32):
 *          A #GArray of #guint32 color for each index in the map.
 * @n_rows: Number of rows in the depth map.
 * @n_columns: Number of columns in the depth map.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from image depth models.
 */
struct _GstMLDepthMap {
  GArray       *values;
  GArray       *colors;

  guint        n_rows;
  guint        n_columns;

  GstStructure *xtraparams;
};

/**
 * gst_ml_depth_map_copy:
 * @depthmap: A #GstMLDepthMap
 *
 * Copy a GstMLDepthMap structure.
 *
 * Returns: (transfer full): A new #GstMLDepthMap.
 */
GST_API GstMLDepthMap *
gst_ml_depth_map_copy (const GstMLDepthMap * depthmap);

/**
 * gst_ml_depth_map_free:
 * @depthmap: A #GstMLDepthMap
 *
 * Free a GstMLDepthMap structure previously allocated with
 * gst_ml_depth_map_copy().
 */
GST_API void
gst_ml_depth_map_free (GstMLDepthMap * depthmap);

/**
 * gst_ml_depth_map_to_structure:
 * @depthmap: A #GstMLDepthMap
 *
 * Converts GstMLDepthMap to a GstStructure representation.
 */
GST_API GstStructure *
gst_ml_depth_map_to_structure (GstMLDepthMap * depthmap);

/**
 * gst_ml_depth_maps_new: (constructor)
 *
 * Allocate a new #GstMLDepthMaps that is also initialized.
 *
 * Returns: (transfer full): A new #GstMLDepthMaps.
 */
GST_API GstMLDepthMaps*
gst_ml_depth_maps_new (void);

/**
 * gst_ml_depth_maps_new_sized: (constructor)
 * @size: number of elements preallocated
 *
 * Allocate a new #GstMLDepthMaps with @size elements preallocated.
 *
 * Returns: (transfer full): A new #GstMLDepthMaps.
 */

GST_API GstMLDepthMaps*
gst_ml_depth_maps_new_sized (guint size);

/**
 * gst_ml_depth_maps_ref:
 * @depthmaps: A #GstMLDepthMaps
 *
 * Atomically increments the reference count of @depthmaps by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer full): The passed in `GstMLDepthMaps`
 */
GST_API GstMLDepthMaps*
gst_ml_depth_maps_ref (GstMLDepthMaps * depthmaps);

/**
 * gst_ml_depth_maps_unref:
 * @depthmaps: (transfer full):  A #GstMLDepthMaps
 *
 * Atomically decrements the reference count of @depthmaps by one. If the
 * reference count drops to 0, the GstMLDepthMaps apreviously allocated with
 * gst_ml_depth_maps_new() or gst_ml_depth_maps_copy() will be deallocated.
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_depth_maps_unref (GstMLDepthMaps * depthmaps);

/**
 * gst_ml_depth_maps_copy:
 * @depthmaps: A #GstMLDepthMaps
 *
 * Copy a GstMLDepthMaps structure.
 *
 * Returns: (transfer full): A new #GstMLDepthMaps.
 */
GST_API GstMLDepthMaps *
gst_ml_depth_maps_copy (const GstMLDepthMaps * depthmaps);

/**
 * gst_ml_depth_maps_append:
 * @depthmaps: A #GstMLDepthMaps
 * @depthmap: A #GstMLDepthMap
 *
 * Adds the value on to the end of the GstMLDepthMaps list.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_depth_maps_append (GstMLDepthMaps * depthmaps,
                          const GstMLDepthMap * depthmap);

/**
 * gst_ml_depth_maps_insert:
 * @depthmaps: A #GstMLDepthMaps
 * @index: the index at which to insert the new element
 * @depthmap: A #GstMLDepthMap
 *
 * Insert element into a GstMLDepthMaps at the given index.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_depth_maps_insert (GstMLDepthMaps * depthmaps, guint index,
                          const GstMLDepthMap * depthmap);

/**
 * gst_ml_depth_maps_remove:
 * @depthmaps: A #GstMLDepthMaps
 * @index: the index of the element to remove
 *
 * Removes the element at the given index from the depth map list.
 * The following elements are moved down one place.
 */
GST_API void
gst_ml_depth_maps_remove (GstMLDepthMaps * depthmaps, guint index);

/**
 * gst_ml_depth_maps_entry:
 * @depthmaps: A #GstMLDepthMaps
 * @index: the index of the element to return
 *
 * Returns: (transfer none): the #GstMLDepthMap at the given index.
 */
GST_API GstMLDepthMap*
gst_ml_depth_maps_entry (GstMLDepthMaps * depthmaps, guint index);

/**
 * gst_ml_depth_maps_size:
 * @depthmaps: A #GstMLDepthMaps
 *
 * Returns: number of elements in A #GstMLDepthMaps
 */
GST_API guint
gst_ml_depth_maps_size (GstMLDepthMaps * depthmaps);

/**
 * gst_ml_depth_maps_resize:
 * @depthmaps: A #GstMLDepthMaps
 * @size: the new size of the GstMLDepthMaps list
 *
 * Sets the size of the array, expanding it if necessary.
 */
GST_API void
gst_ml_depth_maps_resize (GstMLDepthMaps * depthmaps, guint size);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_DEPTH_MAP_H__
