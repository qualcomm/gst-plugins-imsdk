/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-frame.h"

#define CAT_PERFORMANCE gst_ml_frame_get_category()

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

static GstMLFrame*
gst_ml_frame_copy (GstMLFrame * frame)
{
  GstMLFrame *newframe = NULL;
  guint idx = 0;

  g_return_val_if_fail (frame != NULL, NULL);

  newframe = g_slice_new (GstMLFrame);

  for (idx = 0; idx < GST_ML_MAX_TENSORS; idx++)
    memcpy (&(newframe->mapinfo[idx]), &(frame->mapinfo[idx]), sizeof (GstMapInfo));

  newframe->info = frame->info;
  newframe->buffer = gst_buffer_ref (frame->buffer);

  return newframe;
}

static void
gst_ml_frame_free (GstMLFrame * frame)
{
  g_return_if_fail (frame != NULL);

  gst_buffer_unref (frame->buffer);
  g_slice_free (GstMLFrame, frame);
}

G_DEFINE_BOXED_TYPE (GstMLFrame, gst_ml_frame,
    (GBoxedCopyFunc) gst_ml_frame_copy, (GBoxedFreeFunc) gst_ml_frame_free);

static gboolean
gst_ml_info_from_tensor_meta (GstMLInfo * info, GstBuffer * buffer)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;
  guint index = 0;
  gboolean success = TRUE;

  while (success && (meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_ML_TENSOR_META_API_TYPE))) {
    GstMLTensorMeta *mlmeta = GST_ML_TENSOR_META_CAST (meta);

    success = gst_ml_info_set_tensor (info, index++, mlmeta->type,
        mlmeta->n_dimensions, mlmeta->dimensions);
  }

  return success;
}

gboolean
gst_ml_frame_map (GstMLFrame * frame, const GstMLInfo * info,
    GstBuffer * buffer, GstMapFlags flags)
{
  gboolean success = FALSE;
  guint idx = 0, num = 0, n_memory = 0;

  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  // If ML info is not given extract the information from the ML tensor meta.
  if (info != NULL) {
    frame->info = *info;
  } else if (!gst_ml_info_from_tensor_meta (&(frame->info), buffer)) {
    GST_ERROR ("Failed to populate ML info!");
    return FALSE;
  }

  n_memory = gst_buffer_n_memory (buffer);

  if (gst_buffer_get_size (buffer) < gst_ml_info_size (&(frame->info))) {
    GST_ERROR ("Mismatch, expected buffer size %" G_GSIZE_FORMAT " but "
        "actual size is %" G_GSIZE_FORMAT "!", gst_ml_info_size (&(frame->info)),
        gst_buffer_get_size (buffer));
    return FALSE;
  } else if ((n_memory > 1) && n_memory != frame->info.n_tensors) {
    GST_ERROR ("Mismatch, expected %u memory blocks but buffer has %u!",
        frame->info.n_tensors, n_memory);
    return FALSE;
  }

  for (idx = 0; idx < n_memory; idx++) {
    gsize size = (n_memory == 1) ? gst_ml_info_size (&(frame->info)) :
        gst_ml_info_tensor_size (&(frame->info), idx);

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

  if (gst_buffer_n_memory (frame->buffer) == 1) {
    tensor.data = frame->mapinfo[0].data;
    tensor.size = gst_ml_info_tensor_size (&frame->info, index);

    // Offset the data pointer with the size of previous tensors in the block.
    for (num = 0; num < index; num++)
      tensor.data += gst_ml_info_tensor_size (&frame->info, num);
  } else {
    tensor.data = frame->mapinfo[index].data;
    tensor.size = frame->mapinfo[index].size;
  }

  tensor.type = frame->info.type;
  tensor.n_dimensions = frame->info.n_dimensions[index];

  for (num = 0; num < tensor.n_dimensions; num++)
    tensor.dimensions[num] = frame->info.tensors[index][num];

  return tensor;
}
