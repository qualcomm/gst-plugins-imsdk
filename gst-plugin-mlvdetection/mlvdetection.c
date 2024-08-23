/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlvdetection.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/memory/gstmempool.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <cairo/cairo.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_ml_video_detection_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_detection_debug);

#define gst_ml_video_detection_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoDetection, gst_ml_video_detection,
    GST_TYPE_BASE_TRANSFORM);

#define GST_TYPE_ML_MODULES (gst_ml_modules_get_type())

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_ML_VIDEO_DETECTION_VIDEO_FORMATS \
    "{ BGRA, BGRx, BGR16 }"

#define GST_ML_VIDEO_DETECTION_TEXT_FORMATS \
    "{ utf8 }"

#define GST_ML_VIDEO_DETECTION_SRC_CAPS                            \
    "video/x-raw, "                                                \
    "format = (string) " GST_ML_VIDEO_DETECTION_VIDEO_FORMATS "; " \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "               \
    "format = (string) " GST_ML_VIDEO_DETECTION_VIDEO_FORMATS "; " \
    "text/x-raw, "                                                 \
    "format = (string) " GST_ML_VIDEO_DETECTION_TEXT_FORMATS

#define GST_ML_VIDEO_DETECTION_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE        0
#define DEFAULT_PROP_LABELS        NULL
#define DEFAULT_PROP_NUM_RESULTS   5
#define DEFAULT_PROP_THRESHOLD     10.0F
#define DEFAULT_PROP_CONSTANTS     NULL
#define DEFAULT_PROP_STABILIZATION FALSE

#define DEFAULT_MIN_BUFFERS      2
#define DEFAULT_MAX_BUFFERS      10
#define DEFAULT_TEXT_BUFFER_SIZE 8192
#define DEFAULT_VIDEO_WIDTH      320
#define DEFAULT_VIDEO_HEIGHT     240

#define MAX_TEXT_LENGTH          48.0F

#define DISPLACEMENT_THRESHOLD   0.7F
#define POSITION_THRESHOLD       0.04F

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_LABELS,
  PROP_NUM_RESULTS,
  PROP_THRESHOLD,
  PROP_CONSTANTS,
  PROP_STABILIZATION,
};

enum {
  OUTPUT_MODE_VIDEO,
  OUTPUT_MODE_TEXT,
};

static GstStaticCaps gst_ml_video_detection_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_DETECTION_SINK_CAPS);

static GstStaticCaps gst_ml_video_detection_static_src_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_DETECTION_SRC_CAPS);


static GstCaps *
gst_ml_video_detection_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_detection_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_detection_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_detection_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_detection_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_detection_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_detection_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_detection_src_caps ());
}

static GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_enumarate_modules ("ml-vdetection-");
  gtype = g_enum_register_static ("GstMLVideoDetectionModules", variants);

  return gtype;
}

static void
gst_ml_box_displacement_correction (GstMLBoxEntry * l_box, GArray * boxes)
{
  GstMLBoxEntry *r_box = NULL;
  gdouble score = 0.0;
  guint idx = 0;

  if (boxes == NULL)
    return;

  for (idx = 0; idx < boxes->len;  idx++) {
    r_box = &(g_array_index (boxes, GstMLBoxEntry, idx));

    // If labels do not match, continue with next list entry.
    if (l_box->name != r_box->name)
      continue;

    score = gst_ml_boxes_intersection_score (l_box, r_box);

    // If the score is below the threshold, continue with next list entry.
    if (score <= DISPLACEMENT_THRESHOLD)
      continue;

    // Previously detected box overlaps at ~95 % with current one, use it.
    l_box->top = r_box->top;
    l_box->left = r_box->left;
    l_box->bottom = r_box->bottom;
    l_box->right = r_box->right;

    break;
  }

  return;
}

static gint
gst_ml_box_compare_entries_by_position (const GstMLBoxEntry * l_entry,
    const GstMLBoxEntry * r_entry)
{
  gfloat delta = l_entry->left - r_entry->left;

  if (fabs (delta) > POSITION_THRESHOLD)
    return (delta > 0) ? 1 : (-1);

  delta = l_entry->top - r_entry->top;

  if (fabs (delta) > POSITION_THRESHOLD)
    return (delta > 0) ? 1 : (-1);

  return 0;
}

static GstBufferPool *
gst_ml_video_detection_create_pool (GstMLVideoDetection * detection,
    GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBufferPool *pool = NULL;
  guint size = 0;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (detection, "Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }

    // If downstream allocation query supports GBM, allocate gbm memory.
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (detection, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (detection, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    if (NULL == pool) {
      GST_ERROR_OBJECT (detection, "Failed to create buffer pool!");
      return NULL;
    }

    size = GST_VIDEO_INFO_SIZE (&info);
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    GST_INFO_OBJECT (detection, "Uses SYSTEM memory");
    pool = gst_mem_buffer_pool_new (GST_MEMORY_BUFFER_POOL_TYPE_SYSTEM);

    if (NULL == pool) {
      GST_ERROR_OBJECT (detection, "Failed to create buffer pool!");
      return NULL;
    }

    size = DEFAULT_TEXT_BUFFER_SIZE;
  }

  structure = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (structure, caps, size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (GST_IS_IMAGE_BUFFER_POOL (pool)) {
    GstAllocator *allocator = gst_fd_allocator_new ();

    gst_buffer_pool_config_set_allocator (structure, allocator, NULL);
    g_object_unref (allocator);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  if (!gst_buffer_pool_set_config (pool, structure)) {
    GST_WARNING_OBJECT (detection, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  return pool;
}

static void
gst_ml_video_detection_stabilization (GstMLVideoDetection * detection)
{
  guint idx = 0, num = 0;
  GstMLBoxEntry *entry = NULL;
  GArray *mlboxes = NULL;
  GstMLBoxPrediction *prediction = NULL;

  for (idx = 0; idx < detection->predictions->len; idx++) {
    prediction =
        &(g_array_index (detection->predictions, GstMLBoxPrediction, idx));
    mlboxes = g_list_nth_data (detection->stashedmlboxes, idx);

    for (num = 0; num < prediction->entries->len; num++) {
      entry = &(g_array_index (prediction->entries, GstMLBoxEntry, num));

      // Overwrite current box with previously detected one if required.
      gst_ml_box_displacement_correction (entry, mlboxes);
    }

    // Stash the previous prediction results.
    if (mlboxes != NULL) {
      detection->stashedmlboxes =
          g_list_remove (detection->stashedmlboxes, mlboxes);
      g_array_free (mlboxes, TRUE);
    }

    detection->stashedmlboxes = g_list_append (detection->stashedmlboxes,
        g_array_copy (prediction->entries));

    // Clear lower confidence results before position sort.
    if (prediction->entries->len > detection->n_results) {
      guint index = detection->n_results;
      guint length = prediction->entries->len - detection->n_results;

      g_array_remove_range (prediction->entries, index, length);
    }

    // Sort bboxes by possition.
    g_array_sort (prediction->entries,
        (GCompareFunc) gst_ml_box_compare_entries_by_position);
  }
}

static gboolean
gst_ml_video_detection_fill_video_output (GstMLVideoDetection * detection,
    GstBuffer * buffer)
{
  GstVideoMeta *vmeta = NULL;
  GstMapInfo memmap;
  gdouble x = 0.0, y = 0.0, width = 0.0, height = 0.0;
  gdouble fontsize = 12.0, borderwidth = 2.0, radius = 2.0;
  guint idx = 0, num = 0, mrk = 0, n_entries = 0, color = 0, length = 0;

  cairo_format_t format;
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;

  if (!(vmeta = gst_buffer_get_video_meta (buffer))) {
    GST_ERROR_OBJECT (detection, "Output buffer has no meta!");
    return FALSE;
  }

  switch (vmeta->format) {
    case GST_VIDEO_FORMAT_BGRA:
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      format = CAIRO_FORMAT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      format = CAIRO_FORMAT_RGB16_565;
      break;
    default:
      GST_ERROR_OBJECT (detection, "Unsupported format: %s!",
          gst_video_format_to_string (vmeta->format));
      return FALSE;
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (detection, "Failed to map buffer memory block!");
    return FALSE;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (detection, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  surface = cairo_image_surface_create_for_data (memmap.data, format,
      vmeta->width, vmeta->height, vmeta->stride[0]);
  g_return_val_if_fail (surface, FALSE);

  context = cairo_create (surface);
  g_return_val_if_fail (context, FALSE);

  // Clear any leftovers from previous operations.
  cairo_set_operator (context, CAIRO_OPERATOR_CLEAR);
  cairo_paint (context);

  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (surface);

  // Set operator to draw over the source.
  cairo_set_operator (context, CAIRO_OPERATOR_OVER);

  // Mark the surface dirty so Cairo clears its caches.
  cairo_surface_mark_dirty (surface);

  // Select font.
  cairo_select_font_face (context, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_antialias (context, CAIRO_ANTIALIAS_BEST);

  {
    // Set font options.
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);
    cairo_set_font_options (context, options);
    cairo_font_options_destroy (options);
  }

  // Set rectangle borders width and label font size.
  cairo_set_line_width (context, borderwidth);
  cairo_set_font_size (context, fontsize);

  for (idx = 0; idx < detection->predictions->len; idx++) {
    GstMLBoxPrediction *prediction = NULL;
    GstMLBoxEntry *entry = NULL;
    GstVideoRectangle region = { 0, };

    prediction = &(g_array_index (detection->predictions, GstMLBoxPrediction, idx));

    n_entries = (prediction->entries->len < detection->n_results) ?
        prediction->entries->len : detection->n_results;

    // No decoded poses, nothing to do.
    if (n_entries == 0)
      continue;

    // Get the source tensor region with actual data.
    gst_ml_structure_get_source_region (prediction->info, &region);

    // Recalculate the region dimensions depending on the ratios.
    if ((region.w * vmeta->height) > (region.h * vmeta->width)) {
      region.h = gst_util_uint64_scale_int (vmeta->width, region.h, region.w);
      region.w = vmeta->width;
    } else if ((region.w * vmeta->height) < (region.h * vmeta->width)) {
      region.w = gst_util_uint64_scale_int (vmeta->height, region.w, region.h);
      region.h = vmeta->height;
    } else {
      region.w = vmeta->width;
      region.h = vmeta->height;
    }

    // Additional overwrite of X and Y axis for centred image disposition.
    region.x = (vmeta->width - region.w) / 2;
    region.y = (vmeta->height - region.h) / 2;

    for (num = 0; num < n_entries; num++) {
      entry = &(g_array_index (prediction->entries, GstMLBoxEntry, num));

      // Set the bounding box parameters based on the output buffer dimensions.
      x = region.x + (ABS (entry->left) * region.w);
      y = region.y + (ABS (entry->top) * region.h);
      width  = ABS (entry->right - entry->left) * region.w;
      height = ABS (entry->bottom - entry->top) * region.h;

      // Clip width and height if it outside the frame limits.
      width = ((x + width) > vmeta->width) ? (vmeta->width - x) : width;
      height = ((y + height) > vmeta->height) ? (vmeta->height - y) : height;

      color = entry->color;

      // Set color.
      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // Draw rectangle
      cairo_rectangle (context, x, y, width, height);
      cairo_stroke (context);
      g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      length = (entry->landmarks != NULL) ? entry->landmarks->len : 0;

      // Draw landmarks if present.
      for (mrk = 0; mrk < length; mrk++) {
        GstMLBoxLandmark *kp =
            &(g_array_index (entry->landmarks, GstMLBoxLandmark, mrk));

        GST_TRACE_OBJECT (detection, "Landmark [%.2f x %.2f]", kp->x, kp->y);

        // Adjust coordinates based on the output buffer dimensions.
        kp->x = kp->x * vmeta->width;
        kp->y = kp->y * vmeta->height;

        cairo_arc (context, kp->x, kp->y, radius, 0, 2 * G_PI);
        cairo_close_path (context);

        cairo_fill (context);
        g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);
      }

      // Set the width and height of the label background rectangle.
      width = ceil (strlen (g_quark_to_string (entry->name)) *
          fontsize * 3.0F / 5.0F);
      height = ceil (fontsize);

      // Calculate the X and Y position of the label.
      if ((y -= height) < 0.0)
        y = region.y + region.h;

      if ((x + width - 1) > (gdouble) region.w)
        x = region.x + region.w - width;

      cairo_rectangle (context, (x - 1), y, width, height);
      cairo_fill (context);

      // Choose the best contrasting color to the background.
      color = EXTRACT_ALPHA_COLOR (color);
      color += ((EXTRACT_RED_COLOR (entry->color) > 0x7F) ? 0x00 : 0xFF) << 8;
      color += ((EXTRACT_GREEN_COLOR (entry->color) > 0x7F) ? 0x00 : 0xFF) << 16;
      color += ((EXTRACT_BLUE_COLOR (entry->color) > 0x7F) ? 0x00 : 0xFF) << 24;

      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // Set the starting position of the label text.
      cairo_move_to (context, x, (y + (fontsize * 4.0F / 5.0F)));

      // Draw text string.
      cairo_show_text (context, g_quark_to_string (entry->name));
      g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      GST_TRACE_OBJECT (detection, "Batch: %u, label: %s, confidence: %.1f%%, "
          "[%.2f %.2f %.2f %.2f]", prediction->batch_idx,
          g_quark_to_string (entry->name), entry->confidence,
          entry->top, entry->left, entry->bottom, entry->right);

      // Flush to ensure all writing to the surface has been done.
      cairo_surface_flush (surface);
    }
  }

  cairo_destroy (context);
  cairo_surface_destroy (surface);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (detection, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  // Unmap buffer memory blocks.
  gst_buffer_unmap (buffer, &memmap);

  return TRUE;
}

static gboolean
gst_ml_video_detection_fill_text_output (GstMLVideoDetection * detection,
    GstBuffer * buffer)
{
  GstStructure *structure = NULL;
  gchar *string = NULL, *name = NULL;
  GstMapInfo memmap = {};
  GValue list = G_VALUE_INIT, bboxes = G_VALUE_INIT;
  GValue array = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0, num = 0, mrk = 0, n_entries = 0, sequence_idx = 0, id = 0;
  gsize length = 0;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&bboxes, GST_TYPE_ARRAY);
  g_value_init (&array, GST_TYPE_ARRAY);

  for (idx = 0; idx < detection->predictions->len; idx++) {
    GstMLBoxPrediction *prediction = NULL;
    GstMLBoxEntry *entry = NULL;
    const GValue *val = NULL;

    prediction = &(g_array_index (detection->predictions, GstMLBoxPrediction, idx));

    n_entries = (prediction->entries->len < detection->n_results) ?
        prediction->entries->len : detection->n_results;

    gst_structure_get_uint (prediction->info, "sequence-index", &sequence_idx);

    for (num = 0; num < n_entries; num++) {
      entry = &(g_array_index (prediction->entries, GstMLBoxEntry, num));

      id = GST_META_ID (detection->stage_id, sequence_idx, num);

      GST_TRACE_OBJECT (detection, "Batch: %u, ID: %X, Label: %s, Confidence: "
          "%.1f%%, Box [%.2f %.2f %.2f %.2f]", prediction->batch_idx, id,
          g_quark_to_string (entry->name), entry->confidence, entry->top,
          entry->left, entry->bottom, entry->right);

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (g_quark_to_string (entry->name));
      name = g_strdelimit (name, " ", '.');

      structure = gst_structure_new (name, "id", G_TYPE_UINT, id, "confidence",
          G_TYPE_DOUBLE, entry->confidence, "color", G_TYPE_UINT, entry->color,
          NULL);
      g_free (name);

      g_value_init (&value, G_TYPE_FLOAT);

      g_value_set_float (&value, entry->top);
      gst_value_array_append_value (&array, &value);

      g_value_set_float (&value, entry->left);
      gst_value_array_append_value (&array, &value);

      g_value_set_float (&value, entry->bottom);
      gst_value_array_append_value (&array, &value);

      g_value_set_float (&value, entry->right);
      gst_value_array_append_value (&array, &value);

      gst_structure_set_value (structure, "rectangle", &array);
      g_value_reset (&array);

      g_value_unset (&value);
      g_value_init (&value, GST_TYPE_STRUCTURE);

      if ((entry->landmarks != NULL) && (entry->landmarks->len != 0)) {
        GstMLBoxLandmark *lndmark = NULL;
        GstStructure *substructure = NULL;

        for (mrk = 0; mrk < entry->landmarks->len; mrk++) {
          lndmark = &(g_array_index (entry->landmarks, GstMLBoxLandmark, mrk));

          GST_TRACE_OBJECT (detection, "Landmark %s [%.2f x %.2f]",
              g_quark_to_string (lndmark->name), lndmark->x, lndmark->y);

          // Replace empty spaces otherwise subsequent structure call will fail.
          name = g_strdup (g_quark_to_string (lndmark->name));
          name = g_strdelimit (name, " ", '.');

          substructure = gst_structure_new (name, "x", G_TYPE_DOUBLE,
              lndmark->x, "y", G_TYPE_DOUBLE, lndmark->y, NULL);
          g_free (name);

          g_value_take_boxed (&value, substructure);
          gst_value_array_append_value (&array, &value);
          g_value_reset (&value);
        }

        gst_structure_set_value (structure, "landmarks", &array);
        g_value_reset (&array);
      }

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&bboxes, &value);
      g_value_unset (&value);
    }

    structure = gst_structure_new ("ObjectDetection",
        "batch-index", G_TYPE_UINT, prediction->batch_idx, NULL);

    gst_structure_set_value (structure, "bounding-boxes", &bboxes);
    g_value_reset (&bboxes);

    val = gst_structure_get_value (prediction->info, "timestamp");
    gst_structure_set_value (structure, "timestamp", val);

    val = gst_structure_get_value (prediction->info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", val);

    val = gst_structure_get_value (prediction->info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", val);

    if ((val = gst_structure_get_value (prediction->info, "stream-id")))
      gst_structure_set_value (structure, "stream-id", val);

    if ((val = gst_structure_get_value (prediction->info, "stream-timestamp")))
      gst_structure_set_value (structure, "stream-timestamp", val);

    if ((val = gst_structure_get_value (prediction->info, "source-region-id")))
      gst_structure_set_value (structure, "source-region-id", val);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, structure);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  g_value_unset (&array);
  g_value_unset (&bboxes);

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (detection, "Failed to map buffer memory block!");
    return FALSE;
  }

  // Serialize the predictions list into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR_OBJECT (detection, "Failed serialize predictions structure!");
    gst_buffer_unmap (buffer, &memmap);
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  // Check whether the length +1 byte for the additional '\n' is within maxsize.
  if ((length + 1) > memmap.maxsize) {
    GST_ERROR_OBJECT (detection, "String size exceeds max buffer size!");

    gst_buffer_unmap (buffer, &memmap);
    g_free (string);

    return FALSE;
  }

  // Copy the serialized GValue into the output buffer with '\n' termination.
  length = g_snprintf ((gchar *) memmap.data, (length + 1), "%s\n", string);
  g_free (string);

  gst_buffer_unmap (buffer, &memmap);
  gst_buffer_resize (buffer, 0, length);

  return TRUE;
}

static gboolean
gst_ml_video_detection_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (detection, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (detection->outpool)
    gst_object_unref (detection->outpool);

  // Create a new buffer pool.
  if ((pool = gst_ml_video_detection_create_pool (detection, caps)) == NULL) {
    GST_ERROR_OBJECT (detection, "Failed to create buffer pool!");
    return FALSE;
  }

  detection->outpool = pool;

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers,
        maxbuffers);

  if (GST_IS_IMAGE_BUFFER_POOL (pool))
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_detection_submit_input_buffer (GstBaseTransform * base,
    gboolean is_discont, GstBuffer * buffer)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstMLFrame mlframe = { 0, };
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  guint idx = 0;
  gboolean success = FALSE;

  // Let baseclass handle caps (re)negotiation and QoS.
  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->submit_input_buffer (base,
      is_discont, buffer);

  if (ret != GST_FLOW_OK)
    return ret;

  // Check if the baseclass set the plufin in passthrough mode.
  if (gst_base_transform_is_passthrough (base))
    return ret;

  GST_TRACE_OBJECT (detection, "Received %" GST_PTR_FORMAT, buffer);

  // GAP input buffer, cleanup the entries and set the protection meta info.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    GstProtectionMeta *pmeta = NULL;
    GstMLBoxPrediction *prediction = NULL;

    for (idx = 0; idx < detection->predictions->len; ++idx) {
      prediction =
          &(g_array_index (detection->predictions, GstMLBoxPrediction, idx));

      pmeta = gst_buffer_get_protection_meta_id (buffer,
          gst_batch_channel_name (idx));

      g_array_remove_range (prediction->entries, 0, prediction->entries->len);
      prediction->info = pmeta->info;
    }

    return GST_FLOW_OK;
  }

  // Perform pre-processing on the input buffer.
  time = gst_util_get_timestamp ();

  if (!gst_ml_frame_map (&mlframe, detection->mlinfo, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (detection, "Failed to map buffer!");
    return GST_FLOW_ERROR;
  }

  // Clear previously stored values.
  for (idx = 0; idx < detection->predictions->len; ++idx) {
    GstMLBoxPrediction *prediction =
        &(g_array_index (detection->predictions, GstMLBoxPrediction, idx));

    g_array_remove_range (prediction->entries, 0, prediction->entries->len);
    prediction->info = NULL;
  }

  // Call the submodule process funtion.
  success = gst_ml_module_video_detection_execute (detection->module, &mlframe,
      detection->predictions);

  gst_ml_frame_unmap (&mlframe);

  if (!success) {
    GST_ERROR_OBJECT (detection, "Failed to process tensors!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (detection, "Processing took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ml_video_detection_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstBufferPool *pool = detection->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (detection, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (detection, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if ((detection->mode == OUTPUT_MODE_VIDEO) &&
      (gst_buffer_get_size (inbuffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    *outbuffer = gst_buffer_new ();
    GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
  }

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (detection, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static gboolean
gst_ml_video_detection_sink_event (GstBaseTransform * base, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure = gst_event_get_structure (event);

      // Not a supported custom event, pass it to the default handling function.
      if ((structure == NULL) ||
          !gst_structure_has_name (structure, "ml-detection-information"))
        break;

      // Consume downstream information from previous detection stage.
      gst_event_unref (event);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (base, event);
}

static GstCaps *
gst_ml_video_detection_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstCaps *tmplcaps = NULL, *result = NULL;
  guint idx = 0, num = 0, length = 0;

  GST_DEBUG_OBJECT (detection, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (detection, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    tmplcaps = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    tmplcaps = gst_pad_get_pad_template_caps (pad);
  }

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = NULL;
    GstCapsFeatures *features = NULL;

    for (num = 0; num < gst_caps_get_size (caps); num++) {
      const GValue *value = NULL;

      structure = gst_caps_get_structure (tmplcaps, idx);
      features = gst_caps_get_features (tmplcaps, idx);

      // Make a copy that will be modified.
      structure = gst_structure_copy (structure);

      // Extract the rate from incoming caps and propagate it to result caps.
      value = gst_structure_get_value (gst_caps_get_structure (caps, num),
          (direction == GST_PAD_SRC) ? "framerate" : "rate");

      // Skip if there is no value or if current caps structure is text.
      if (value != NULL && !gst_structure_has_name (structure, "text/x-raw")) {
        gst_structure_set_value (structure,
            (direction == GST_PAD_SRC) ? "rate" : "framerate", value);
      }

      // If this is already expressed by the existing caps skip this structure.
      if (gst_caps_is_subset_structure_full (result, structure, features)) {
        gst_structure_free (structure);
        continue;
      }

      gst_caps_append_structure_full (result, structure,
          gst_caps_features_copy (features));
    }
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (detection, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_ml_video_detection_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstStructure *output = NULL;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (detection, "Trying to fixate output caps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Fixate the output format.
  value = gst_structure_get_value (output, "format");

  if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field (output, "format");
    value = gst_structure_get_value (output, "format");
  }

  GST_DEBUG_OBJECT (detection, "Output format fixed to: %s",
      g_value_get_string (value));

  if (gst_structure_has_name (output, "video/x-raw")) {
    gint width = 0, height = 0, par_n = 0, par_d = 0;

    // Fixate output PAR if not already fixated..
    value = gst_structure_get_value (output, "pixel-aspect-ratio");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, 1, 1, NULL);
      value = gst_structure_get_value (output, "pixel-aspect-ratio");
    }

    par_d = gst_value_get_fraction_denominator (value);
    par_n = gst_value_get_fraction_numerator (value);

    GST_DEBUG_OBJECT (detection, "Output PAR fixed to: %d/%d", par_n, par_d);

    // Retrieve the output width and height.
    value = gst_structure_get_value (output, "width");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      width = DEFAULT_VIDEO_WIDTH;
      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }

    width = g_value_get_int (value);
    value = gst_structure_get_value (output, "height");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      height = DEFAULT_VIDEO_HEIGHT;
      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }

    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (detection, "Output width and height fixated to: %dx%d",
        width, height);
  }

  // Fixate any remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (detection, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_video_detection_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstCaps *modulecaps = NULL;
  GstQuery *query = NULL;
  GstStructure *structure = NULL;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstMLInfo ininfo;
  gboolean success = FALSE;
  guint idx = 0;

  if (NULL == detection->labels) {
    GST_ELEMENT_ERROR (detection, RESOURCE, NOT_FOUND, (NULL),
        ("Labels file not set!"));
    return FALSE;
  } else if (DEFAULT_PROP_MODULE == detection->mdlenum) {
    GST_ELEMENT_ERROR (detection, RESOURCE, NOT_FOUND, (NULL),
        ("Module name not set, automatic module pick up not supported!"));
    return FALSE;
  }

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_ML_MODULES));
  evalue = g_enum_get_value (eclass, detection->mdlenum);

  gst_ml_module_free (detection->module);
  detection->module = gst_ml_module_new (evalue->value_name);

  if (NULL == detection->module) {
    GST_ELEMENT_ERROR (detection, RESOURCE, FAILED, (NULL),
        ("Module creation failed!"));
    return FALSE;
  }

  modulecaps = gst_ml_module_get_caps (detection->module);

  if (!gst_caps_can_intersect (incaps, modulecaps)) {
    GST_ELEMENT_ERROR (detection, RESOURCE, FAILED, (NULL),
        ("Module caps %" GST_PTR_FORMAT " do not intersect with the "
         "negotiated caps %" GST_PTR_FORMAT "!", modulecaps, incaps));
    return FALSE;
  }

  if (!gst_ml_module_init (detection->module)) {
    GST_ELEMENT_ERROR (detection, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));
    return FALSE;
  }

  // Query upstream pre-process plugin about the inference parameters.
  query = gst_query_new_custom (GST_QUERY_CUSTOM,
      gst_structure_new_empty ("ml-preprocess-information"));

  if (gst_pad_peer_query (base->sinkpad, query)) {
    const GstStructure *s = gst_query_get_structure (query);

    gst_structure_get_uint (s, "stage-id", &(detection->stage_id));
    GST_DEBUG_OBJECT (detection, "Queried stage ID: %u", detection->stage_id);
  } else {
    GST_ELEMENT_ERROR (detection, CORE, NEGOTIATION, (NULL),
        ("Failed to receive preprocess information!"));
    return FALSE;
  }

  // Free the query instance as it is no longer needed and we are the owners.
  gst_query_unref (query);

  structure = gst_structure_new ("options",
      GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, incaps,
      GST_ML_MODULE_OPT_LABELS, G_TYPE_STRING, detection->labels,
      GST_ML_MODULE_OPT_THRESHOLD, G_TYPE_DOUBLE, detection->threshold,
      NULL);

  if (detection->mlconstants != NULL) {
    gst_structure_set (structure,
        GST_ML_MODULE_OPT_CONSTANTS, GST_TYPE_STRUCTURE, detection->mlconstants,
        NULL);
  }

  if (!gst_ml_module_set_opts (detection->module, structure)) {
    GST_ELEMENT_ERROR (detection, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ELEMENT_ERROR (detection, CORE, CAPS, (NULL),
        ("Failed to get input ML info from caps %" GST_PTR_FORMAT "!", incaps));
    return FALSE;
  }

  if (detection->mlinfo != NULL)
    gst_ml_info_free (detection->mlinfo);

  detection->mlinfo = gst_ml_info_copy (&ininfo);

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw"))
    detection->mode = OUTPUT_MODE_VIDEO;
  else if (gst_structure_has_name (structure, "text/x-raw"))
    detection->mode = OUTPUT_MODE_TEXT;

  if ((detection->mode == OUTPUT_MODE_VIDEO) &&
      (GST_ML_INFO_TENSOR_DIM (detection->mlinfo, 0, 0) > 1)) {
    GST_ELEMENT_ERROR (detection, CORE, FAILED, (NULL),
        ("Batched input tensors with video output is not supported!"));
    return FALSE;
  }

  // Inform any ML pre-process downstream about it's ROI stage ID.
  structure = gst_structure_new ("ml-detection-information", "stage-id",
      G_TYPE_UINT, detection->stage_id, NULL);

  GST_DEBUG_OBJECT (detection, "Send stage ID %u", detection->stage_id);

  success = gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (detection),
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure));

  if (!success) {
    GST_ELEMENT_ERROR (detection, CORE, EVENT, (NULL),
        ("Failed to send ML info downstream!"));
    return FALSE;
  }

  // Allocate the maximum number of predictions based on the batch size.
  g_array_set_size (detection->predictions,
      GST_ML_INFO_TENSOR_DIM (detection->mlinfo, 0, 0));

  for (idx = 0; idx < detection->predictions->len; ++idx) {
    GstMLBoxPrediction *prediction =
        &(g_array_index (detection->predictions, GstMLBoxPrediction, idx));

    prediction->entries = g_array_new (FALSE, TRUE, sizeof (GstMLBoxEntry));
    prediction->batch_idx = idx;

    g_array_set_clear_func (prediction->entries,
        (GDestroyNotify) gst_ml_box_entry_cleanup);
  }

  GST_DEBUG_OBJECT (detection, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (detection, "Output caps: %" GST_PTR_FORMAT, outcaps);

  gst_base_transform_set_passthrough (base, FALSE);
  return TRUE;
}

static GstFlowReturn
gst_ml_video_detection_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  g_return_val_if_fail (detection->module != NULL, GST_FLOW_ERROR);

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Apply stabilization for fluctuating bboxes
  if (detection->stabilization)
    gst_ml_video_detection_stabilization (detection);

  time = gst_util_get_timestamp ();

  if (detection->mode == OUTPUT_MODE_VIDEO)
    success = gst_ml_video_detection_fill_video_output (detection, outbuffer);
  else if (detection->mode == OUTPUT_MODE_TEXT)
    success = gst_ml_video_detection_fill_text_output (detection, outbuffer);

  if (!success) {
    GST_ERROR_OBJECT (detection, "Failed to fill output buffer!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (detection, "Object detection took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_detection_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (object);

  switch (prop_id) {
    case PROP_MODULE:
      detection->mdlenum = g_value_get_enum (value);
      break;
    case PROP_LABELS:
      g_free (detection->labels);
      detection->labels = g_strdup (g_value_get_string (value));
      break;
    case PROP_NUM_RESULTS:
      detection->n_results = g_value_get_uint (value);
      break;
    case PROP_THRESHOLD:
      detection->threshold = g_value_get_double (value);
      break;
    case PROP_STABILIZATION:
      detection->stabilization = g_value_get_boolean (value);
      break;
    case PROP_CONSTANTS:
    {
      GValue structure = G_VALUE_INIT;

      g_value_init (&structure, GST_TYPE_STRUCTURE);

      if (!gst_parse_string_property_value (value, &structure)) {
        GST_ERROR_OBJECT (detection, "Failed to parse constants!");
        break;
      }

      if (detection->mlconstants != NULL)
        gst_structure_free (detection->mlconstants);

      detection->mlconstants = GST_STRUCTURE (g_value_dup_boxed (&structure));
      g_value_unset (&structure);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_detection_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (object);

  switch (prop_id) {
     case PROP_MODULE:
      g_value_set_enum (value, detection->mdlenum);
      break;
    case PROP_LABELS:
      g_value_set_string (value, detection->labels);
      break;
    case PROP_NUM_RESULTS:
      g_value_set_uint (value, detection->n_results);
      break;
    case PROP_THRESHOLD:
      g_value_set_double (value, detection->threshold);
      break;
    case PROP_STABILIZATION:
      g_value_set_boolean (value, detection->stabilization);
      break;
    case PROP_CONSTANTS:
    {
      gchar *string = NULL;

      if (detection->mlconstants != NULL)
        string = gst_structure_to_string (detection->mlconstants);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_detection_finalize (GObject * object)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (object);

  g_array_free (detection->predictions, TRUE);
  gst_ml_module_free (detection->module);

  if (detection->mlinfo != NULL)
    gst_ml_info_free (detection->mlinfo);

  if (detection->outpool != NULL)
    gst_object_unref (detection->outpool);

  g_free (detection->labels);

  if (detection->mlconstants != NULL)
    gst_structure_free (detection->mlconstants);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (detection));
}

static void
gst_ml_video_detection_class_init (GstMLVideoDetectionClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_video_detection_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          GST_TYPE_ML_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_LABELS,
      g_param_spec_string ("labels", "Labels",
          "Labels filename", DEFAULT_PROP_LABELS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_NUM_RESULTS,
      g_param_spec_uint ("results", "Results",
          "Number of results to display", 0, 10, DEFAULT_PROP_NUM_RESULTS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_THRESHOLD,
      g_param_spec_double ("threshold", "Threshold",
          "Confidence threshold in %", 10.0F, 100.0F, DEFAULT_PROP_THRESHOLD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CONSTANTS,
      g_param_spec_string ("constants", "Constants",
          "Constants, offsets and coefficients used by the chosen module for "
          "post-processing of incoming tensors in GstStructure string format. "
          "Applicable only for some modules.",
          DEFAULT_PROP_CONSTANTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_STABILIZATION,
      g_param_spec_boolean ("stabilization", "Stabilization enable",
          "Enable stabilization of bboxes", DEFAULT_PROP_STABILIZATION,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning image object detection", "Filter/Effect/Converter",
      "Machine Learning plugin for image object detection", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_detection_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_detection_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_decide_allocation);
  base->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_submit_input_buffer);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_prepare_output_buffer);

  base->sink_event = GST_DEBUG_FUNCPTR (gst_ml_video_detection_sink_event);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_video_detection_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_detection_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_detection_transform);
}

static void
gst_ml_video_detection_init (GstMLVideoDetection * detection)
{
  detection->mode = OUTPUT_MODE_VIDEO;

  detection->outpool = NULL;
  detection->module = NULL;

  detection->stashedmlboxes = NULL;
  detection->stage_id = 0;

  detection->predictions = g_array_new (FALSE, FALSE, sizeof (GstMLBoxPrediction));
  g_return_if_fail (detection->predictions != NULL);

  g_array_set_clear_func (detection->predictions,
      (GDestroyNotify) gst_ml_box_prediction_cleanup);

  detection->mdlenum = DEFAULT_PROP_MODULE;
  detection->labels = DEFAULT_PROP_LABELS;
  detection->n_results = DEFAULT_PROP_NUM_RESULTS;
  detection->threshold = DEFAULT_PROP_THRESHOLD;
  detection->mlconstants = DEFAULT_PROP_CONSTANTS;
  detection->stabilization = DEFAULT_PROP_STABILIZATION;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (detection), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_detection_debug, "qtimlvdetection", 0,
      "QTI ML image object detection plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvdetection", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_DETECTION);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvdetection,
    "QTI Machine Learning plugin for image object detection post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
