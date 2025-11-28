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

  if (gst_buffer_get_size (buffer) != gst_ml_info_size (info)) {
    GST_ERROR ("Mismatch, expected buffer size %" G_GSIZE_FORMAT " but "
        "actual size is %" G_GSIZE_FORMAT "!", gst_ml_info_size (info),
        gst_buffer_get_size (buffer));
    return FALSE;
  } else if ((n_memory > 1) && n_memory != info->n_tensors) {
    GST_ERROR ("Mismatch, expected %u memory blocks but buffer has %u!",
        info->n_tensors, n_memory);
    return FALSE;
  }

  // Copy the ML info into the frame.
  frame->info = *info;

  for (idx = 0; idx < n_memory; idx++) {
    gsize size = (n_memory == 1) ? gst_ml_info_size (&(frame)->info) :
        gst_ml_info_tensor_size (&(frame)->info, idx);

    success = gst_buffer_map_range (buffer, idx, 1, &(frame)->map[idx], flags);

    if (!success) {
      GST_ERROR ("Failed to map buffer %p with memory at idx %u!", buffer, idx);

      for (num = 0; num < idx; ++num)
        gst_buffer_unmap (buffer, &(frame)->map[num]);

      return FALSE;
    } else if (frame->map[idx].size != size) {
      GST_ERROR ("Size mismatch for buffer %p with memory at idx %u! "
          "Expected %" G_GSIZE_FORMAT " but received %" G_GSIZE_FORMAT "!",
          buffer, idx, size, frame->map[idx].size);

      for (num = 0; num <= idx; ++num)
        gst_buffer_unmap (buffer, &(frame)->map[num]);

      return FALSE;
    }
  }

  frame->buffer = buffer;

  return TRUE;
}

void
gst_ml_frame_unmap (GstMLFrame * frame)
{
  GstBuffer *buffer = NULL;
  guint idx = 0;

  g_return_if_fail (frame != NULL);

  buffer = frame->buffer;

  for (idx = 0; idx < gst_buffer_n_memory (buffer); idx++)
    gst_buffer_unmap (buffer, &(frame)->map[idx]);
}
