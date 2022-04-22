/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ml-video-segmentation-module.h"

#include <gst/video/video.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/allocators/allocators.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H


#define CAST_TO_GFLOAT(data) ((gfloat*)data)
#define CAST_TO_GUINT32(data) ((guint32*)data)

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

typedef struct _GstPrivateModule GstPrivateModule;
typedef struct _GstLabel GstLabel;

struct _GstPrivateModule {
  GHashTable *labels;
};

struct _GstLabel {
  gchar *name;
  guint color;
};

static GstLabel *
gst_ml_label_new ()
{
  GstLabel *label = g_new (GstLabel, 1);

  label->name = NULL;
  label->color = 0x00000000;

  return label;
}

static void
gst_ml_label_free (GstLabel * label)
{
  if (label->name != NULL)
    g_free (label->name);

  g_free (label);
}

gpointer
gst_ml_video_segmentation_module_init (const gchar * labels)
{
  GstPrivateModule *module = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0;

  g_value_init (&list, GST_TYPE_LIST);

  if (g_file_test (labels, G_FILE_TEST_IS_REGULAR)) {
    GString *string = NULL;
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (labels, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get labels file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return NULL;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    string = g_string_new (contents);
    g_free (contents);

    // Add opening and closing brackets.
    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    // Get the raw character data.
    contents = g_string_free (string, FALSE);

    success = gst_value_deserialize (&list, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize labels file contents!");
      return NULL;
    }
  } else if (!gst_value_deserialize (&list, labels)) {
    GST_ERROR ("Failed to deserialize labels!");
    return NULL;
  }

  module = g_slice_new0 (GstPrivateModule);
  g_return_val_if_fail (module != NULL, NULL);

  module->labels = g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) gst_ml_label_free);

  for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
    GstStructure *structure = NULL;
    GstLabel *label = NULL;
    guint id = 0;

    structure = GST_STRUCTURE (
        g_value_dup_boxed (gst_value_list_get_value (&list, idx)));

    if (structure == NULL) {
      GST_WARNING ("Failed to extract structure!");
      continue;
    } else if (!gst_structure_has_field (structure, "id") ||
        !gst_structure_has_field (structure, "color")) {
      GST_WARNING ("Structure does not contain 'id' and/or 'color' fields!");
      gst_structure_free (structure);
      continue;
    }

    if ((label = gst_ml_label_new ()) == NULL) {
      GST_ERROR ("Failed to allocate label memory!");
      gst_structure_free (structure);
      continue;
    }

    label->name = g_strdup (gst_structure_get_name (structure));
    label->name = g_strdelimit (label->name, "-", ' ');

    gst_structure_get_uint (structure, "color", &label->color);
    gst_structure_get_uint (structure, "id", &id);

    g_hash_table_insert (module->labels, GUINT_TO_POINTER (id), label);
    gst_structure_free (structure);
  }

  g_value_unset (&list);
  return module;
}

void
gst_ml_video_segmentation_module_deinit (gpointer instance)
{
  GstPrivateModule *module = instance;

  if (NULL == module)
    return;

  g_hash_table_destroy (module->labels);
  g_slice_free (GstPrivateModule, module);
}

gboolean
gst_ml_video_segmentation_module_process (gpointer instance,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstPrivateModule *module = instance;
  GstMLTensorMeta *mlmeta = NULL;
  GstVideoMeta *vmeta = NULL;
  GstProtectionMeta *pmeta = NULL;
  GstMapInfo inmap, outmap;
  guint idx = 0, row = 0, column = 0, bpp = 0, padding = 0;
  guint width = 0, height = 0, length = 0, color = 0;
  gdouble mindepth = G_MAXDOUBLE, maxdepth = G_MINDOUBLE;

  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (inbuffer != NULL, FALSE);
  g_return_val_if_fail (outbuffer != NULL, FALSE);

  if (gst_buffer_n_memory (inbuffer) != 1) {
    GST_ERROR ("Expecting 1 tensor memory block but received %u!",
        gst_buffer_n_memory (inbuffer));
    return FALSE;
  } else if (gst_buffer_n_memory (outbuffer) != 1) {
    GST_ERROR ("Expecting 1 output memory block but received %u!",
        gst_buffer_n_memory (outbuffer));
    return FALSE;
  }

  if (!(mlmeta = gst_buffer_get_ml_tensor_meta (inbuffer))) {
    GST_ERROR ("Input buffer has no ML meta!");
    return FALSE;
  }

  if (mlmeta->type != GST_ML_TYPE_FLOAT32) {
    GST_ERROR ("Unsupported tensor type!");
    return FALSE;
  }

  if (!(vmeta = gst_buffer_get_video_meta (outbuffer))) {
    GST_ERROR ("Output buffer has no video meta!");
    return FALSE;
  }

  {
    const GstVideoFormatInfo *vfinfo = gst_video_format_get_info (vmeta->format);

    if (!GST_VIDEO_FORMAT_INFO_IS_RGB (vfinfo)) {
      GST_ERROR ("Output buffer formats other than RGB based are not supported!");
      return FALSE;
    }

    // Retrive the video frame Bytes Per Pixel for later calculations.
    bpp = GST_VIDEO_FORMAT_INFO_BITS (vfinfo) *
        GST_VIDEO_FORMAT_INFO_N_COMPONENTS (vfinfo) / CHAR_BIT;
  }

  // Map input buffer memory blocks.
  if (!gst_buffer_map_range (inbuffer, 0, 1, &inmap, GST_MAP_READ)) {
    GST_ERROR ("Failed to map input buffer memory block!");
    return FALSE;
  }

  // Map output buffer memory blocks.
  if (!gst_buffer_map_range (outbuffer, 0, 1, &outmap, GST_MAP_READWRITE)) {
    GST_ERROR ("Failed to map output buffer memory block!");
    gst_buffer_unmap (inbuffer, &inmap);
    return FALSE;
  }

  // Calculate the row padding in bytes.
  padding = vmeta->stride[0] - (vmeta->width * bpp);

  // Set the initial width and height of the source mask.
  width = mlmeta->dimensions[2];
  height = mlmeta->dimensions[1];

  // Set length as full width of the source mask.
  length = mlmeta->dimensions[2];

  // Extract the SAR (Source Aspect Ratio) and adjust mask dimensions.
  if ((pmeta = gst_buffer_get_protection_meta (inbuffer)) != NULL) {
    guint sar_n = 1, sar_d = 1;

    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));

    if (sar_n > sar_d)
      height = gst_util_uint64_scale_int (width, sar_d, sar_n);
    else if (sar_n < sar_d)
      width = gst_util_uint64_scale_int (height, sar_n, sar_d);
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING ("DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  // Find the minimum and maximum depth values in the mask.
  for (row = 0; row < height; row++) {
    for (column = 0; column < width; column++) {
      gdouble value = 0.0;

      idx = row * length + column;
      value = CAST_TO_GFLOAT (inmap.data)[idx];

      if (value > maxdepth)
        maxdepth = value;

      if (value < mindepth)
        mindepth = value;
    }
  }

  for (row = 0; row < vmeta->height; row++) {
    for (column = 0; column < vmeta->width; column++) {
      GstLabel *label = NULL;
      guint id = G_MAXUINT8;

      // Calculate the source index.
      idx = length * gst_util_uint64_scale_int (row, height, vmeta->height);
      idx += gst_util_uint64_scale_int (column, width, vmeta->width);

      id *= (CAST_TO_GFLOAT (inmap.data)[idx] - mindepth) / (maxdepth - mindepth);

      label = g_hash_table_lookup (module->labels, GUINT_TO_POINTER (id));
      color = (label != NULL) ? label->color : 0x000000FF;

      // Calculate the destination index.
      idx = (((row * vmeta->width) + column) * bpp) + (row * padding);

      outmap.data[idx] = EXTRACT_RED_COLOR (color);
      outmap.data[idx + 1] = EXTRACT_GREEN_COLOR (color);
      outmap.data[idx + 2] = EXTRACT_BLUE_COLOR (color);

      if (bpp == 4)
        outmap.data[idx + 3] = EXTRACT_ALPHA_COLOR (color);
    }
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING ("DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  gst_buffer_unmap (outbuffer, &outmap);
  gst_buffer_unmap (inbuffer, &inmap);

  return TRUE;
}
