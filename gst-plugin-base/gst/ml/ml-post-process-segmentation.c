/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-post-process-segmentation.h"

/**
 * GstMLSegmentations:
 * @refcount: thread safe reference counter
 * @entries: (element-type GstMLSegmentation): A #GArray of #GstMLSegmentation
 *
 * Information describing a group of prediction results beloging to the same batch.
 */
struct _GstMLSegmentations {
  gatomicrefcount refcount;
  GArray          *entries;
};

G_DEFINE_BOXED_TYPE (GstMLSegmentation, gst_ml_segmentation,
    (GBoxedCopyFunc) gst_ml_segmentation_copy,
    (GBoxedFreeFunc) gst_ml_segmentation_free);

G_DEFINE_BOXED_TYPE (GstMLSegmentations, gst_ml_segmentations,
    (GBoxedCopyFunc) gst_ml_segmentations_ref,
    (GBoxedFreeFunc) gst_ml_segmentations_unref);

void
gst_ml_segmentation_reset (GstMLSegmentation * segmentation)
{
  g_return_if_fail (segmentation != NULL);

  segmentation->n_rows = 0;
  segmentation->n_columns = 0.0;

  g_clear_pointer (&segmentation->labels, g_array_unref);
  g_clear_pointer (&segmentation->colors, g_array_unref);
  g_clear_pointer (&segmentation->xtraparams, gst_structure_free);
}

GstMLSegmentation*
gst_ml_segmentation_copy (const GstMLSegmentation * segmentation)
{
  GstMLSegmentation *newsegmentation = NULL;

  g_return_val_if_fail (segmentation != NULL, NULL);

  newsegmentation = g_slice_new (GstMLSegmentation);

  newsegmentation->n_rows = segmentation->n_rows;
  newsegmentation->n_columns = segmentation->n_columns;

  newsegmentation->labels = g_array_copy (segmentation->labels);
  newsegmentation->colors = g_array_copy (segmentation->colors);
  newsegmentation->xtraparams = gst_structure_copy (segmentation->xtraparams);

  return newsegmentation;
}

void
gst_ml_segmentation_free (GstMLSegmentation * segmentation)
{
  if (segmentation == NULL)
    return;

  gst_ml_segmentation_reset (segmentation);
  g_slice_free (GstMLSegmentation, segmentation);
}

GstStructure *
gst_ml_segmentation_to_structure (GstMLSegmentation * segmentation)
{
  GstStructure *structure = NULL;
  const guchar *data = NULL;
  GValue value = G_VALUE_INIT;
  gsize size = 0;

  structure = gst_structure_new ("segmentation", "rows", G_TYPE_UINT,
      segmentation->n_rows, "columns", G_TYPE_UINT, segmentation->n_columns,
      NULL);

  g_value_init (&value, G_TYPE_STRING);

  if (segmentation->labels != NULL && (segmentation->labels->len > 0)) {
    data = (const guchar*) segmentation->labels->data;
    size = segmentation->labels->len * sizeof (GQuark);
    g_value_take_string (&value, g_base64_encode (data, size));
  }

  gst_structure_take_value (structure, "labels", &value);
  g_value_init (&value, G_TYPE_STRING);

  if (segmentation->colors != NULL && (segmentation->colors->len > 0)) {
    data = (const guchar*) segmentation->colors->data;
    size = segmentation->colors->len * sizeof (guint32);
    g_value_take_string (&value, g_base64_encode (data, size));
  }

  gst_structure_take_value (structure, "colors", &value);

  if (segmentation->xtraparams != NULL) {
    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, g_steal_pointer (&segmentation->xtraparams));
    gst_structure_take_value (structure, "xtraparams", &value);
  }

  gst_ml_segmentation_reset (segmentation);
  return structure;
}

GstMLSegmentations*
gst_ml_segmentations_new (void)
{
  GstMLSegmentations *segmentations = g_slice_new (GstMLSegmentations);

  g_atomic_ref_count_init (&segmentations->refcount);
  segmentations->entries = g_array_new (FALSE, TRUE, sizeof (GstMLSegmentation));

  g_array_set_clear_func (segmentations->entries,
      (GDestroyNotify) gst_ml_segmentation_reset);

  return segmentations;
}

GstMLSegmentations*
gst_ml_segmentations_new_sized (guint size)
{
  GstMLSegmentations *segmentations = g_slice_new (GstMLSegmentations);

  g_atomic_ref_count_init (&segmentations->refcount);
  segmentations->entries =
      g_array_sized_new (FALSE, TRUE, sizeof (GstMLSegmentation), size);

  g_array_set_clear_func (segmentations->entries,
      (GDestroyNotify) gst_ml_segmentation_reset);

  g_array_set_size (segmentations->entries, size);
  return segmentations;
}

GstMLSegmentations*
gst_ml_segmentations_ref (GstMLSegmentations * segmentations)
{
  g_return_val_if_fail (segmentations != NULL, NULL);

  g_atomic_ref_count_inc (&segmentations->refcount);
  return segmentations;
}

void
gst_ml_segmentations_unref (GstMLSegmentations * segmentations)
{
  g_return_if_fail (segmentations != NULL);

  if (g_atomic_ref_count_dec (&segmentations->refcount)) {
    g_array_free (segmentations->entries, TRUE);
    g_slice_free (GstMLSegmentations, segmentations);
  }
}

GstMLSegmentations *
gst_ml_segmentations_copy (const GstMLSegmentations * segmentations)
{
  GstMLSegmentations *newsegmentations = NULL;

  g_return_val_if_fail (segmentations != NULL, NULL);

  newsegmentations = g_slice_new (GstMLSegmentations);
  newsegmentations->entries = g_array_copy (segmentations->entries);
  g_atomic_ref_count_init (&newsegmentations->refcount);

  return newsegmentations;
}

void
gst_ml_segmentations_append (GstMLSegmentations * segmentations,
    const GstMLSegmentation * segmentation)
{
  g_return_if_fail (segmentations != NULL);
  g_array_append_vals (segmentations->entries, segmentation, 1);
}

void
gst_ml_segmentations_insert (GstMLSegmentations * segmentations, guint index,
    const GstMLSegmentation * segmentation)
{
  g_return_if_fail (segmentations != NULL);
  g_array_insert_vals (segmentations->entries, index, segmentation, 1);
}

void
gst_ml_segmentations_remove (GstMLSegmentations * segmentations, guint index)
{
  g_return_if_fail (segmentations != NULL);
  g_array_remove_index (segmentations->entries, index);
}

GstMLSegmentation*
gst_ml_segmentations_entry (GstMLSegmentations * segmentations, guint index)
{
  g_return_val_if_fail (segmentations != NULL, NULL);
  return &(g_array_index (segmentations->entries, GstMLSegmentation, index));
}

guint
gst_ml_segmentations_size (GstMLSegmentations * segmentations)
{
  g_return_val_if_fail (segmentations != NULL, 0);
  return segmentations->entries->len;
}

void
gst_ml_segmentations_resize (GstMLSegmentations * segmentations, guint size)
{
  g_return_if_fail (segmentations != NULL);
  g_array_set_size (segmentations->entries, size);
}
