/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_POST_PROCESS_SEGMENTATION_H__
#define __GST_QTI_ML_POST_PROCESS_SEGMENTATION_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_ML_SEGMENTATIONS_CAST(obj)      ((GstMLSegmentations*)(obj))

typedef struct _GstMLSegmentation GstMLSegmentation;
typedef struct _GstMLSegmentations GstMLSegmentations;

#define GST_TYPE_ML_SEGMENTATION            (gst_ml_segmentation_get_type ())
GST_API GType gst_ml_segmentation_get_type  (void);

#define GST_TYPE_ML_SEGMENTATIONS           (gst_ml_segmentations_get_type ())
GST_API GType gst_ml_segmentations_get_type (void);

/**
 * GstMLSegmentation:
 * @labels: (element-type GQuark):
 *          A #GArray of #GQuark label for each index in the segmentation mask.
 * @colors: (optional) (element-type guint32):
 *          A #GArray of #guint32 color for each index in the segmentation mask.
 * @n_rows: Number of rows in the segmentation mask.
 * @n_columns: Number of columns in the segmentation mask.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from image segmentation models.
 */
struct _GstMLSegmentation {
  GArray       *labels;
  GArray       *colors;

  guint        n_rows;
  guint        n_columns;

  GstStructure *xtraparams;
};

/**
 * gst_ml_segmentation_copy:
 * @segmentation: A #GstMLSegmentation
 *
 * Copy a GstMLSegmentation structure.
 *
 * Returns: (transfer full): A new #GstMLSegmentation.
 */
GST_API GstMLSegmentation *
gst_ml_segmentation_copy (const GstMLSegmentation * segmentation);

/**
 * gst_ml_segmentation_free:
 * @segmentation: A #GstMLSegmentation
 *
 * Free a GstMLSegmentation structure previously allocated with
 * gst_ml_segmentation_copy().
 */
GST_API void
gst_ml_segmentation_free (GstMLSegmentation * segmentation);

/**
 * gst_ml_segmentation_to_structure:
 * @segmentation: A #GstMLSegmentation
 *
 * Converts GstMLSegmentation to a GstStructure representation.
 */
GST_API GstStructure *
gst_ml_segmentation_to_structure (GstMLSegmentation * segmentation);

/**
 * gst_ml_segmentations_new: (constructor)
 *
 * Allocate a new #GstMLSegmentations that is also initialized.
 *
 * Returns: (transfer full): A new #GstMLSegmentations.
 */
GST_API GstMLSegmentations*
gst_ml_segmentations_new (void);

/**
 * gst_ml_segmentations_new_sized: (constructor)
 * @size: number of elements preallocated
 *
 * Allocate a new #GstMLSegmentations with @size elements preallocated.
 *
 * Returns: (transfer full): A new #GstMLSegmentations.
 */
GST_API GstMLSegmentations*
gst_ml_segmentations_new_sized (guint size);

/**
 * gst_ml_segmentations_ref: (skip)
 * @segmentations: (transfer none): A #GstMLSegmentations
 *
 * Atomically increments the reference count of @segmentations by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer none): A pointer to the object passed in @segmentations
 */
GST_API GstMLSegmentations*
gst_ml_segmentations_ref (GstMLSegmentations * segmentations);

/**
 * gst_ml_segmentations_unref: (skip)
 * @segmentations: (transfer none): A #GstMLSegmentations
 *
 * Atomically decrements the reference count of @segmentations by one. If the
 * reference count drops to 0, the GstMLSegmentations apreviously allocated with
 * gst_ml_segmentations_new() or gst_ml_segmentations_copy() will be deallocated.
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_segmentations_unref (GstMLSegmentations * segmentations);

/**
 * gst_ml_segmentations_copy:
 * @segmentations: A #GstMLSegmentations
 *
 * Copy a GstMLSegmentations structure.
 *
 * Returns: (transfer full): A new #GstMLSegmentations.
 */
GST_API GstMLSegmentations *
gst_ml_segmentations_copy (const GstMLSegmentations * segmentations);

/**
 * gst_ml_segmentations_append:
 * @segmentations: A #GstMLSegmentations
 * @segmentation: A #GstMLSegmentation
 *
 * Adds the value on to the end of the GstMLSegmentations list.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_segmentations_append (GstMLSegmentations * segmentations,
                             const GstMLSegmentation * segmentation);

/**
 * gst_ml_segmentations_insert:
 * @segmentations: A #GstMLSegmentations
 * @index: the index at which to insert the new element
 * @segmentation: A #GstMLSegmentation
 *
 * Insert element into a GstMLSegmentations at the given index.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_segmentations_insert (GstMLSegmentations * segmentations, guint index,
                             const GstMLSegmentation * segmentation);

/**
 * gst_ml_segmentations_remove:
 * @segmentations: A #GstMLSegmentations
 * @index: the index of the element to remove
 *
 * Removes the element at the given index from the segmentations list.
 * The following elements are moved down one place.
 */
GST_API void
gst_ml_segmentations_remove (GstMLSegmentations * segmentations, guint index);

/**
 * gst_ml_segmentations_entry:
 * @segmentations: A #GstMLSegmentations
 * @index: the index of the element to return
 *
 * Returns: (transfer none): the #GstMLSegmentation at the given index.
 */
GST_API GstMLSegmentation*
gst_ml_segmentations_entry (GstMLSegmentations * segmentations, guint index);

/**
 * gst_ml_segmentations_size:
 * @segmentations: A #GstMLSegmentations
 *
 * Returns: number of elements in A #GstMLSegmentations
 */
GST_API guint
gst_ml_segmentations_size (GstMLSegmentations * segmentations);

/**
 * gst_ml_segmentations_resize:
 * @segmentations: A #GstMLSegmentations
 * @size: the new size of the GstMLSegmentations list
 *
 * Sets the size of the array, expanding it if necessary.
 */
GST_API void
gst_ml_segmentations_resize (GstMLSegmentations * segmentations, guint size);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_SEGMENTATION_H__
