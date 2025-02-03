/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "common-utils.h"

#include <gst/allocators/allocators.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

static const gchar* mux_stream_names[] = {
    "mux-stream-00", "mux-stream-01", "mux-stream-02", "mux-stream-03",
    "mux-stream-04", "mux-stream-05", "mux-stream-06", "mux-stream-07",
    "mux-stream-08", "mux-stream-09", "mux-stream-10", "mux-stream-11",
    "mux-stream-12", "mux-stream-13", "mux-stream-14", "mux-stream-15",
    "mux-stream-16", "mux-stream-17", "mux-stream-18", "mux-stream-19",
    "mux-stream-20", "mux-stream-21", "mux-stream-22", "mux-stream-23",
    "mux-stream-24", "mux-stream-25", "mux-stream-26", "mux-stream-27",
    "mux-stream-28", "mux-stream-29", "mux-stream-30", "mux-stream-31",
};

void
gst_buffer_dma_sync_start (GstBuffer * buffer)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  if (!gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0)))
    return;

  struct dma_buf_sync bufsync;
  gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

  bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

  if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
    GST_WARNING ("DMA IOCTL SYNC Start failed!");
#endif // HAVE_LINUX_DMA_BUF_H
}

void
gst_buffer_dma_sync_end (GstBuffer * buffer)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  if (!gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0)))
    return;

  struct dma_buf_sync bufsync;
  gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

  bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

  if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
    GST_WARNING ("DMA IOCTL SYNC End failed!");
#endif // HAVE_LINUX_DMA_BUF_H
}

const gchar *
gst_mux_stream_name (guint index)
{
  g_return_val_if_fail ((G_N_ELEMENTS (mux_stream_names) > index), NULL);
  return mux_stream_names[index];
}

gint
gst_mux_buffer_get_memory_stream_id (GstBuffer * buffer, gint mem_idx)
{
  gint num = -1;

  if (GST_BUFFER_OFFSET (buffer) == GST_BUFFER_OFFSET_NONE)
    return -1;

  // Find the set bit index corresponding to the given memory index.
  while (mem_idx >= 0)
    mem_idx -= ((GST_BUFFER_OFFSET (buffer) >> (++num)) & 0b01) ? 1 : 0;

  return num;
}

gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    if (feature == NULL && ((gst_caps_features_get_size (features) == 0) ||
            gst_caps_features_is_any (features)))
      return TRUE;

    // Skip ANY caps and return immediately if feature is present.
    if ((feature != NULL) && !gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
}

gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

gboolean
gst_parse_string_property_value (const GValue * value, GValue * output)
{
  const gchar *input = g_value_get_string (value);

  if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (input, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get file contents, error: '%s'!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    // Add opening and closing brackets if output value is of type list.
    if (G_VALUE_HOLDS (output, GST_TYPE_LIST)) {
      GString *string = g_string_new (contents);

      string = g_string_prepend (string, "{ ");
      string = g_string_append (string, " }");

      g_free (contents);

      // Get the raw character data.
      contents = g_string_free (string, FALSE);
    }

    success = gst_value_deserialize (output, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize file contents!");
      return FALSE;
    }
  } else if (!gst_value_deserialize (output, input)) {
    GST_ERROR ("Failed to deserialize string!");
    return FALSE;
  }

  return TRUE;
}

GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    if (gst_structure_has_name (GST_PROTECTION_META_CAST (meta)->info, name))
      return GST_PROTECTION_META_CAST (meta);
  }

  return NULL;
}

void
gst_buffer_copy_protection_meta (GstBuffer * destination, GstBuffer * source)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (source, &state,
              GST_PROTECTION_META_API_TYPE))) {
    gst_buffer_add_protection_meta (destination,
        gst_structure_copy (GST_PROTECTION_META_CAST (meta)->info));
  }
}

#if GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)
GArray *
g_array_copy (GArray * array)
{
  GArray *newarray = NULL:
  guint size = 0;

  size = g_array_get_element_size (array);
  newarray = g_array_sized_new (FALSE, FALSE, size, array->len);

  newarray = g_array_set_size (newarray, array->len);
  memcpy (newarray->data, array->data, array->len * size);

  return newarray;
}
#endif // GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)
