/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-frame.h"

#define CAT_PERFORMANCE gst_ml_frame_get_category()

G_DEFINE_BOXED_TYPE (GstMLFrame, gst_ml_frame,
    (GBoxedCopyFunc) gst_ml_frame_ref, (GBoxedFreeFunc) gst_ml_frame_unref);

static inline GstDebugCategory *
gst_ml_frame_get_category (void)
{
  static GstDebugCategory *category = NULL;

  if (g_once_init_enter (&category)) {
    GstDebugCategory *cat = NULL;

    GST_DEBUG_CATEGORY_GET (cat, "GST_PERFORMANCE");
    g_once_init_leave (&category, cat);
  }
  return category;
}

GstMLFrame *
gst_ml_frame_new (void)
{
  GstMLFrame *frame = g_slice_new (GstMLFrame);
  guint idx = 0;

  for (idx = 0; idx < GST_ML_MAX_TENSORS; idx++)
    memset (&(frame->mapinfo[idx]), 0, sizeof (frame->mapinfo[idx]));

  gst_ml_info_init (&frame->info);
  g_atomic_ref_count_init (&frame->refcount);
  return frame;
}

GstMLFrame*
gst_ml_frame_ref (GstMLFrame * frame)
{
  g_return_val_if_fail (frame != NULL, NULL);
  g_atomic_ref_count_inc (&frame->refcount);

  return frame;
}

void
gst_ml_frame_unref (GstMLFrame * frame)
{
  g_return_if_fail (frame != NULL);

  if (g_atomic_ref_count_dec (&frame->refcount)) {
    gst_buffer_unref (frame->buffer);
    g_slice_free (GstMLFrame, frame);
  }
}

gboolean
gst_ml_frame_map (GstMLFrame * frame, const GstMLInfo * info,
    GstBuffer * buffer, GstMapFlags flags)
{
  gboolean success = FALSE;
  guint idx = 0, num = 0, n_memory = 0;

  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  n_memory = gst_buffer_n_memory (buffer);

  if (gst_buffer_get_size (buffer) < gst_ml_info_size (info)) {
    GST_ERROR ("Mismatch, expected buffer size %" G_GSIZE_FORMAT " but "
        "actual size is %" G_GSIZE_FORMAT "!", gst_ml_info_size (info),
        gst_buffer_get_size (buffer));
    return FALSE;
  } else if ((n_memory > 1) && n_memory != info->n_tensors) {
    GST_ERROR ("Mismatch, expected %u memory blocks but buffer has %u!",
        info->n_tensors, n_memory);
    return FALSE;
  }

  g_atomic_ref_count_init (&frame->refcount);

  // Copy the ML info into the frame.
  frame->info = *info;

  for (idx = 0; idx < n_memory; idx++) {
    gsize size = (n_memory == 1) ? gst_ml_info_size (&(frame)->info) :
        gst_ml_info_tensor_size (&(frame)->info, idx);

    success = gst_buffer_map_range (buffer, idx, 1, &(frame)->mapinfo[idx], flags);

    if (!success) {
      GST_ERROR ("Failed to map buffer %p with memory at idx %u!", buffer, idx);

      for (num = 0; num < idx; ++num)
        gst_buffer_unmap (buffer, &(frame)->mapinfo[num]);

      return FALSE;
    } else if (frame->mapinfo[idx].size < size) {
      GST_ERROR ("Size mismatch for buffer %p with memory at idx %u! "
          "Expected %" G_GSIZE_FORMAT " but received %" G_GSIZE_FORMAT "!",
          buffer, idx, size, frame->mapinfo[idx].size);

      for (num = 0; num <= idx; ++num)
        gst_buffer_unmap (buffer, &(frame)->mapinfo[num]);

      return FALSE;
    }
  }

  frame->buffer = buffer;
  return TRUE;
}

void
gst_ml_frame_unmap (GstMLFrame * frame)
{
  guint idx = 0, n_memory = 0;

  g_return_if_fail (frame != NULL);

  if (frame->buffer == NULL)
    return;

  n_memory = gst_buffer_n_memory (frame->buffer);

  for (idx = 0; idx < n_memory; idx++)
    gst_buffer_unmap (frame->buffer, &(frame)->mapinfo[idx]);

  frame->buffer = NULL;
}

GstMLTensor
gst_ml_frame_get_tensor (GstMLFrame * frame, guint index)
{
  GstMLTensor tensor = { 0, };
  guint num = 0;

  tensor.data = frame->mapinfo[index].data;
  tensor.size = frame->mapinfo[index].size;

  tensor.type = frame->info.type;
  tensor.n_dimensions = frame->info.n_dimensions[index];

  for (num = 0; num < tensor.n_dimensions; num++)
    tensor.dimensions[num] = frame->info.tensors[index][num];

  return tensor;
}
