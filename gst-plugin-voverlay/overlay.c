/*
 * Copyright (c) 2022,2025 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <gst/video/gstqtibufferpool.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <cairo/cairo.h>
#include <gst/video/gstimagepool.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

GST_DEBUG_CATEGORY (gst_overlay_debug);
#define GST_CAT_DEFAULT gst_overlay_debug

#define gst_overlay_parent_class parent_class
G_DEFINE_TYPE (GstVOverlay, gst_overlay, GST_TYPE_BASE_TRANSFORM);

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_OVERLAY_VIDEO_FORMATS \
  "{ NV12, NV21, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, NV12_Q08C }"

#define DEFAULT_PROP_ENGINE_BACKEND (gst_video_converter_default_backend())

#define DEFAULT_MIN_BUFFERS         1
#define DEFAULT_MAX_BUFFERS         30

#define MAX_TEXT_LENGTH             48

enum
{
  PROP_0,
  PROP_ENGINE_BACKEND,
  PROP_BBOXES,
  PROP_TIMESTAMPS,
  PROP_STRINGS,
  PROP_PRIVACY_MASKS,
  PROP_STATIC_IMAGES,
};

static GstCaps *
gst_overlay_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_OVERLAY_VIDEO_FORMATS));

    if (gst_is_gbm_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_OVERLAY_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_overlay_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_OVERLAY_VIDEO_FORMATS));

    if (gst_is_gbm_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_OVERLAY_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_overlay_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_overlay_sink_caps ());
}

static GstPadTemplate *
gst_overlay_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_overlay_src_caps ());
}

static inline void
gst_video_blits_release (GstVideoBlit * blits, guint n_blits)
{
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < n_blits; idx++) {
    buffer = (blits[idx].frame != NULL) ? blits[idx].frame->buffer : NULL;

    // If refcount is >1 then blit object has been cached, do not free the data.
    if (buffer != NULL && (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) > 1))
      continue;

    if (buffer != NULL) {
      gst_video_frame_unmap (blits[idx].frame);
      gst_buffer_unref (buffer);
    }

    g_slice_free (GstVideoFrame, blits[idx].frame);
    g_slice_free (GstVideoRectangle, blits[idx].sources);
    g_slice_free (GstVideoRectangle, blits[idx].destinations);
  }

  g_free (blits);
}

static inline void
gst_recalculate_dimensions (guint * width, guint * height, gint num, gint denum,
    guint scale)
{
  if (num > denum) {
    *width = GST_ROUND_UP_128 (*width / scale);
    *height = gst_util_uint64_scale_int (*width, denum, num);
  } else if (num < denum) {
    *height = GST_ROUND_UP_4 (*height / scale);
    *width = GST_ROUND_UP_128 (gst_util_uint64_scale_int (*height, num, denum));
    *height = gst_util_uint64_scale_int (*width, denum, num);
  } else {
    *width = GST_ROUND_UP_128 (*width / scale);
    *height = GST_ROUND_UP_4 (*height / scale);
  }
}

static inline gboolean
gst_cairo_draw_text (cairo_t * context, guint color, gdouble x, gdouble y,
    gchar * text, gdouble fontsize)
{
  // Set color.
  cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
      EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
      EXTRACT_FLOAT_ALPHA_COLOR (color));

  // Set the starting position of the bounding box text.
  cairo_move_to (context, x, (y + (fontsize * 4.0F / 5.0F)));

  // Draw text.
  cairo_set_font_size (context, fontsize);
  cairo_show_text (context, text);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline gboolean
gst_cairo_draw_line (cairo_t * context, guint color, gdouble x, gdouble y,
     gdouble dx, gdouble dy, gdouble linewidth)
{
  // Set color.
  cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
      EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
      EXTRACT_FLOAT_ALPHA_COLOR (color));

  // Set rectangle lines width.
  cairo_set_line_width (context, linewidth);

  cairo_move_to (context, x, y);
  cairo_line_to (context, dx, dy);

  cairo_stroke (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline gboolean
gst_cairo_draw_rectangle (cairo_t * context, guint color, gdouble x, gdouble y,
    gdouble width, gdouble height, gdouble linewidth, gboolean filled)
{
  // Set color.
  cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
      EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
      EXTRACT_FLOAT_ALPHA_COLOR (color));

  // Set rectangle lines width.
  cairo_set_line_width (context, linewidth);

  // Set rectangle position and dimensions.
  cairo_rectangle (context, x, y, width, height);

  if (filled)
    cairo_fill (context);
  else
    cairo_stroke (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline gboolean
gst_cairo_draw_circle (cairo_t * context, guint color, gdouble x, gdouble y,
    gdouble radius, gdouble linewidth, gboolean filled)
{
  // Set color.
  cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
      EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
      EXTRACT_FLOAT_ALPHA_COLOR (color));

  // Set rectangle lines width.
  cairo_set_line_width (context, linewidth);

  // Set circle position and dimensions.
  cairo_arc (context, x, y, radius, 0, 2 * G_PI);

  if (filled)
    cairo_fill (context);
  else
    cairo_stroke (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline gboolean
gst_cairo_draw_polygon (cairo_t * context, guint color,
    gdouble coords[GST_VIDEO_POLYGON_MAX_POINTS * 2], guint n_coords,
    gdouble linewidth, gboolean filled)
{
  guint idx = 0;

  // Set polygon lines width.
  cairo_set_line_width (context, linewidth);

  cairo_move_to (context, coords[0], coords[1]);

  for (idx = 2; idx < n_coords; idx += 2)
    cairo_line_to (context, coords[idx], coords[idx + 1]);

  cairo_close_path (context);

  // Set color.
  cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
      EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
      EXTRACT_FLOAT_ALPHA_COLOR (color));

  if (filled) {
    cairo_stroke_preserve (context);
    cairo_fill (context);
  } else {
    cairo_stroke (context);
  }

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline gboolean
gst_cairo_draw_arrow (cairo_t * context, guint color, gdouble x, gdouble y,
    gdouble dx, gdouble dy, gdouble linewidth)
{
  gdouble a = 0.0, b = 0.0, angle = 0.0;

  // Set rectangle lines width.
  cairo_set_line_width (context, linewidth);

  // Draw arrow head with 20 degrees angles and length of 4 pixels.
  angle = atan2 (dy, dx) + G_PI;
  cairo_move_to (context, x, y);

  a = x + (linewidth / 2) * cos (angle - G_PI / 2.0);
  b = y + (linewidth / 2) * sin (angle - G_PI / 2.0);
  cairo_line_to (context, a, b);

  a = x + dx + (linewidth / 2) * cos (angle - G_PI / 2.0) + 4 * cos (angle);
  b = y + dy + (linewidth / 2) * sin (angle - G_PI / 2.0) + 4 * sin (angle);
  cairo_line_to (context, a, b);

  a = x + dx + 4 * cos (angle - G_PI / 9.0);
  b = y + dy + 4 * sin (angle - G_PI / 9.0);
  cairo_line_to (context, a, b);

  cairo_line_to (context, x + dx, y + dy);

  a = x + dx + 4 * cos (angle + G_PI / 9.0);
  b = y + dy + 4 * sin (angle + G_PI / 9.0);
  cairo_line_to (context, a, b);

  a = x + dx + (linewidth / 2) * cos (angle + G_PI / 2.0) + 4 * cos (angle);
  b = y + dy + (linewidth / 2) * sin (angle + G_PI / 2.0) + 4 * sin (angle);
  cairo_line_to (context, a, b);

  a = x + (linewidth / 2) * cos (angle + G_PI / 2.0);
  b = y + (linewidth / 2) * sin (angle + G_PI / 2.0);
  cairo_line_to (context, a, b);

  cairo_close_path (context);

  // Set black border color.
  cairo_set_source_rgba (context, 0.0, 0.0, 0.0, 1.0);
  cairo_stroke_preserve (context);

  // Set infill color.
  cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
      EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
      EXTRACT_FLOAT_ALPHA_COLOR (color));
  cairo_fill (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline gboolean
gst_cairo_draw_setup (GstVideoFrame * frame, cairo_surface_t ** surface,
    cairo_t ** context)
{
  cairo_format_t format;
  cairo_font_options_t *options = NULL;

#ifdef HAVE_LINUX_DMA_BUF_H
    if (gst_is_fd_memory (gst_buffer_peek_memory (frame->buffer, 0))) {
      struct dma_buf_sync bufsync;
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (frame->buffer, 0));

      bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

      if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
        GST_WARNING ("DMA IOCTL SYNC START failed!");
    }
#endif // HAVE_LINUX_DMA_BUF_H

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
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
      GST_ERROR ("Unsupported format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
      return FALSE;
  }

  *surface = cairo_image_surface_create_for_data (
      GST_VIDEO_FRAME_PLANE_DATA (frame, 0), format,
      GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));
  g_return_val_if_fail (*surface, FALSE);

  *context = cairo_create (*surface);
  g_return_val_if_fail (*context, FALSE);

  // Select font.
  cairo_select_font_face (*context, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_antialias (*context, CAIRO_ANTIALIAS_BEST);

  // Set font options.
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);

  cairo_set_font_options (*context, options);
  cairo_font_options_destroy (options);

  // Clear any leftovers from previous operations.
  cairo_set_operator (*context, CAIRO_OPERATOR_CLEAR);
  cairo_paint (*context);
  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (*surface);

  // Set operator to draw over the source.
  cairo_set_operator (*context, CAIRO_OPERATOR_OVER);
  // Mark the surface dirty so Cairo clears its caches.
  cairo_surface_mark_dirty (*surface);

  return TRUE;
}

static inline void
gst_cairo_draw_cleanup (GstVideoFrame * frame, cairo_surface_t * surface,
    cairo_t * context)
{
  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (surface);

  cairo_destroy (context);
  cairo_surface_destroy (surface);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (frame->buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (frame->buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING ("DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H
}

static inline void
gst_overlay_update_rectangle_dimensions (GstVOverlay * overlay,
    GstVideoFrame * vframe, GstVideoRectangle * rectangle)
{
  gint width = 0, height = 0, num = 0, denum = 0;

  // Calculate the aspect ration of the bounding box rectangle.
  gst_util_fraction_multiply (rectangle->w, rectangle->h, 1, 1, &num, &denum);

  // Initial values for bounding box width and height, used adjustment.
  width = GST_VIDEO_FRAME_WIDTH (vframe);
  height = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Adjust the rectangle width & height so it is within the buffer dimensions.
  if ((rectangle->w <= width) && (rectangle->h <= height)) {
    width = rectangle->w;
    height = rectangle->h;
  } else if ((rectangle->w > width) && (rectangle->h <= height)) {
    // Height is set to the width of the frame, adjust width with aspect ratio.
    height = gst_util_uint64_scale_int (width, denum, num);
  } else if ((rectangle->w <= width) && (rectangle->h > height)) {
    // Width is set to the width of the frame, adjust height with aspect ratio.
    width = gst_util_uint64_scale_int (height, num, denum);
  } else if ((rectangle->w > width) && (rectangle->h > height)) {
    if (num > denum)
      height = gst_util_uint64_scale_int (width, denum, num);
    else if (num < denum)
      width = gst_util_uint64_scale_int (height, num, denum);
  }

  GST_TRACE_OBJECT (overlay, "Adjusted dimensions %dx%d --> %dx%d",
      rectangle->w, rectangle->h, width, height);

  // Set the adjusted bounding box dimensions.
  rectangle->w = width;
  rectangle->h = height;

  return;
}

static GstBufferPool *
gst_overlay_create_pool (GstVOverlay * overlay, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (overlay, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if (gst_is_gbm_supported ()) {
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (overlay, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (overlay, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    config = gst_buffer_pool_get_config (pool);
    allocator = gst_fd_allocator_new ();

    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);
  } else {
    GstVideoFormat format;
    GstVideoAlignment align;
    gboolean success;
    guint width, height;
    gint stride, scanline;

    width = GST_VIDEO_INFO_WIDTH (&info);
    height = GST_VIDEO_INFO_HEIGHT (&info);
    format = GST_VIDEO_INFO_FORMAT (&info);

    success = gst_adreno_utils_compute_alignment (width, height, format,
        &stride, &scanline);
    if (!success) {
      GST_ERROR_OBJECT(overlay,"Failed to get alignment");
      return NULL;
    }

    pool = gst_qti_buffer_pool_new ();
    config = gst_buffer_pool_get_config (pool);

    gst_video_alignment_reset (&align);
    align.padding_bottom = scanline - height;
    align.padding_right = stride - width;
    gst_video_info_align (&info, &align);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &align);

    allocator = gst_qti_allocator_new ();
    if (allocator == NULL) {
      GST_ERROR_OBJECT (overlay, "Failed to create QTI allocator");
      gst_clear_object (&pool);
      return NULL;
    }
  }

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (overlay, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  g_object_unref (allocator);
  return pool;
}

static gboolean
gst_overlay_handle_classification_entry (GstVOverlay * overlay,
    GstVideoBlit * blit, GArray * labels)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  gchar text[MAX_TEXT_LENGTH] = { 0, };
  GstVideoRectangle *source = NULL, *destination = NULL;
  gdouble x = 1.0, y = 1.0, fontsize = 24.0;
  guint num = 0, length = 0, maxlength = 0, color = 0xFFFFFFFF;
  gboolean success = TRUE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  destination->w = source->w;
  destination->h = source->h;

  if ((labels == NULL) || (labels->len == 0))
    return TRUE;

  for (num = 0; num < labels->len; num++, y += fontsize) {
    GstClassLabel *label = &(g_array_index (labels, GstClassLabel, num));

    if (y > GST_VIDEO_FRAME_HEIGHT (vframe))
      break;

    length = g_snprintf (text, MAX_TEXT_LENGTH, "%s",
        g_quark_to_string (label->name));

    if (length > maxlength)
      maxlength = length;

    color = label->color;

    GST_TRACE_OBJECT (overlay, "Label: %s, Color: 0x%X, Position: [%.2f %.2f],"
        " Fontsize: %.2f", text, color, x, y, fontsize);

    cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
        EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
        EXTRACT_FLOAT_ALPHA_COLOR (color));
    cairo_paint (context);

    // Choose the best contrasting color to the background.
    color = EXTRACT_ALPHA_COLOR (color);
    color += ((EXTRACT_RED_COLOR (label->color) > 0x7F) ? 0x00 : 0xFF) << 8;
    color += ((EXTRACT_GREEN_COLOR (label->color) > 0x7F) ? 0x00 : 0xFF) << 16;
    color += ((EXTRACT_BLUE_COLOR (label->color) > 0x7F) ? 0x00 : 0xFF) << 24;

    success &= gst_cairo_draw_text (context, color, x, y, text, fontsize);
  }

  // Update the source and destination with the actual text dimensions.
  destination->w = source->w = ceil (maxlength * fontsize * 3.0F / 5.0F);
  destination->h = source->h = ceil (num * fontsize);

  // The default value is for 1080p resolution, scale up/down based on that.
  destination->w *= (GST_VIDEO_INFO_HEIGHT (overlay->vinfo) / 1080.0F);
  destination->h *= (GST_VIDEO_INFO_HEIGHT (overlay->vinfo) / 1080.0F);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_pose_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GArray * keypoints, GArray * links)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gdouble x = 0.0, y = 0.0, dx = 0.0, dy = 0.0, xscale = 1.0, yscale = 1.0;
  guint num = 0, length = 0;
  gboolean success = TRUE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  gst_util_fraction_to_double (source->w, destination->w, &xscale);
  gst_util_fraction_to_double (source->h, destination->h, &yscale);

  length = (keypoints != NULL) ? keypoints->len : 0;

  for (num = 0; num < length; num++) {
    GstVideoKeypoint *kp =
        &(g_array_index (keypoints, GstVideoKeypoint, num));

    x = kp->x * xscale;
    y = kp->y * yscale;

    GST_TRACE_OBJECT (overlay, "Keypoint: %s, Position: [%.2f %.2f], "
        "Confidence: %.2f, Color: 0x%X", g_quark_to_string (kp->name), x, y,
        kp->confidence, kp->color);

    success &=
        gst_cairo_draw_circle (context, kp->color, x, y, 2.0, 1.0, TRUE);
  }

  length = (links != NULL) ? links->len : 0;

  for (num = 0; num < length; num++) {
    GstVideoKeypointLink *link = NULL;
    GstVideoKeypoint *s_kp = NULL, *d_kp = NULL;

    link = &(g_array_index (links, GstVideoKeypointLink, num));
    s_kp = &(g_array_index (keypoints, GstVideoKeypoint, link->s_kp_idx));
    d_kp = &(g_array_index (keypoints, GstVideoKeypoint, link->d_kp_idx));

    x = s_kp->x * xscale;
    y = s_kp->y * yscale;

    dx = d_kp->x * xscale;
    dy = d_kp->y * yscale;

    GST_TRACE_OBJECT (overlay, "Link: %s [%.2f %.2f] <---> %s [%.2f %.2f]",
        g_quark_to_string (s_kp->name), x, y, g_quark_to_string (d_kp->name),
        dx, dy);

    success &= gst_cairo_draw_line (context, s_kp->color, x, y, dx, dy, 1.0);
  }

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_optclflow_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GArray * mvectors, GArray * stats)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  GstCvMotionVector *mvector = NULL;
  GstCvOptclFlowStats *cvstats = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  guint num = 0, color = 0xFFFFFFFF;
  gdouble x = 0.0, y = 0.0, dx = 0.0, dy = 0.0, xscale = 0.0, yscale = 0.0;
  gboolean success = FALSE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  gst_util_fraction_to_double (GST_VIDEO_INFO_WIDTH (overlay->vinfo),
      GST_VIDEO_FRAME_WIDTH (vframe), &xscale);
  gst_util_fraction_to_double (GST_VIDEO_INFO_HEIGHT (overlay->vinfo),
      GST_VIDEO_FRAME_HEIGHT (vframe), &yscale);

  // Read every 6th 4x16 motion vector paxel due arrows density.
  for (num = 0; num < mvectors->len; num += 6) {
    mvector = &g_array_index (mvectors, GstCvMotionVector, num);

    if ((mvector->dx == 0) && (mvector->dy == 0))
      continue;

    if ((stats != NULL) && (stats->len != 0))
      cvstats = &g_array_index (stats, GstCvOptclFlowStats, num);

    if ((cvstats != NULL) && (cvstats->sad == 0) && (cvstats->variance == 0))
      continue;

    x = (mvector->x / xscale) + mvector->dx;
    y = (mvector->y / yscale) + mvector->dy;

    dx = (-1.0F) * mvector->dx;
    dy = (-1.0F) * mvector->dy;

    gst_cairo_draw_arrow (context, color, x, y, dx, dy, 1.0);
  }

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return TRUE;
}

static gboolean
gst_overlay_handle_detection_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GstVideoBlit * auxblit, GstVideoRegionOfInterestMeta * roimeta)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  GstStructure *param = NULL, *objparam;
  GList *list = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gdouble scale = 0.0, linewidth = 0.0;
  guint idx = 0, color = 0x000000FF;
  gboolean success = TRUE, haslabel = FALSE, haslndmrks = FALSE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  destination->x = roimeta->x;
  destination->y = roimeta->y;

  source->w = destination->w = roimeta->w;
  source->h = destination->h = roimeta->h;

  // Adjust bbox dimensions so that it fits inside the overlay frame.
  gst_overlay_update_rectangle_dimensions (overlay, vframe, source);

  // Initialize the destination X/Y of the auxiliary blit for labels.
  auxblit->destinations[0].x = roimeta->x;
  auxblit->destinations[0].y = roimeta->y;

  // Process attached meta entries that were derived from this ROI.
  for (list = roimeta->params; list != NULL; list = g_list_next (list)) {
    GQuark id = 0;

    param = GST_STRUCTURE_CAST (list->data);
    id = gst_structure_get_name_id (param);

    if (id == g_quark_from_static_string ("ImageClassification")) {
      GArray *labels = NULL;

      labels = g_value_get_boxed (gst_structure_get_value (param, "labels"));
      success &= gst_overlay_handle_classification_entry (overlay, auxblit, labels);

      haslabel = ((labels != NULL) && (labels->len > 0)) ? TRUE : FALSE;
    } else if (id == g_quark_from_static_string ("VideoLandmarks")) {
      GArray *keypoints = NULL, *links = NULL;

      keypoints = g_value_get_boxed (gst_structure_get_value (param, "keypoints"));
      links = g_value_get_boxed (gst_structure_get_value (param, "links"));

      success &= gst_overlay_handle_pose_entry (overlay, blit, keypoints, links);

      haslndmrks = ((keypoints != NULL) && (keypoints->len > 0)) ? TRUE : FALSE;
    } else if (id == g_quark_from_static_string ("OpticalFlow")) {
      GArray *mvectors = NULL, *stats = NULL;

      mvectors = g_value_get_boxed (gst_structure_get_value (param, "mvectors"));
      stats = g_value_get_boxed (gst_structure_get_value (param, "stats"));

      success &= gst_overlay_handle_optclflow_entry (overlay, blit, mvectors, stats);
    }
  }

  // Extract the structure containing ROI parameters.
  objparam = gst_video_region_of_interest_meta_get_param (roimeta,
      "ObjectDetection");
  gst_structure_get_uint (objparam, "color", &color);

  // Set the most appropriate box line width based on frame and box dimensions.
  gst_util_fraction_to_double (destination->w, source->w, &scale);
  linewidth = (scale > 1.0F) ? (4.0F / scale) : 4.0F;

  GST_TRACE_OBJECT (overlay, "Rectangle: [%d %d %d %d], Color: 0x%X",
      source->x, source->y, source->w, source->h, color);

  success = gst_cairo_draw_rectangle (context, color, source->x, source->y,
      source->w, source->h, linewidth, FALSE);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  // Process any additional landmarks if present.
  if (!haslndmrks && gst_structure_has_field (objparam, "landmarks")) {
    GArray *landmarks = NULL;
    GstVideoKeypoint *kp = NULL;
    gfloat x = 0.0, y = 0.0;

    gst_structure_get (objparam, "landmarks", G_TYPE_ARRAY, &landmarks, NULL);

    for (idx = 0; idx < landmarks->len; idx++) {
      kp = &(g_array_index (landmarks, GstVideoKeypoint, idx));

      // Additionally adjust coordinates with source to destination ratio.
      x = kp->x  * (source->w / (gfloat) destination->w);
      y = kp->y * (source->h / (gfloat) destination->h);

      GST_TRACE_OBJECT (overlay, "Landmark: [%.2f %.2f]", x, y);
      success &=
          gst_cairo_draw_circle (context, color, x, y, (linewidth / 2), 1, TRUE);
    }
  }

  if (!haslabel) {
    // TODO: Optimize!
    GArray *labels = g_array_sized_new (FALSE, TRUE, sizeof (GstClassLabel), 1);
    GstClassLabel *label = NULL;

    g_array_set_size (labels, 1);
    label = &(g_array_index (labels, GstClassLabel, 0));

    label->name = roimeta->roi_type;
    label->color = color;

    gst_structure_get_double (objparam, "confidence", &(label->confidence));

    success &= gst_overlay_handle_classification_entry (overlay, auxblit, labels);
    g_array_free (labels, TRUE);
  }

  source = &(auxblit->sources[0]);
  destination = &(auxblit->destinations[0]);

  // Correct the destination of the auxiliary blit for labels.
  if ((destination->y -= destination->h) < 0)
    destination->y = roimeta->y + roimeta->h;

  if ((destination->x + destination->w) > GST_VIDEO_INFO_WIDTH (overlay->vinfo))
    destination->x = roimeta->x + roimeta->w - destination->w;

  GST_TRACE_OBJECT (overlay, "Adjusted Label Destination: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_bbox_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GstOverlayBBox * bbox)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gdouble scale = 0.0, linewidth = 0.0;
  guint color = 0;
  gboolean success = FALSE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  destination->x = bbox->destination.x;
  destination->y = bbox->destination.y;

  source->x = source->y = 0;

  source->w = destination->w = bbox->destination.w;
  source->h = destination->h = bbox->destination.h;

  color = bbox->color;

  // Adjust bbox dimensions so that it fits inside the overlay frame.
  gst_overlay_update_rectangle_dimensions (overlay, vframe, source);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  // Set the most appropriate box line width based on frame and box dimensions.
  gst_util_fraction_to_double (destination->w, source->w, &scale);
  linewidth = (scale > 1.0F) ? (4.0F / scale) : 4.0F;

  GST_TRACE_OBJECT (overlay, "Rectangle: [%d %d %d %d], Color: 0x%X",
      source->x, source->y, source->w, source->h, color);

  success = gst_cairo_draw_rectangle (context, color, source->x, source->y,
      source->w, source->h, linewidth, FALSE);

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_timestamp_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GstOverlayTimestamp * timestamp)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gchar *text = NULL;
  gdouble fontsize = 0.0, n_chars = 0.0, scale = 0.0;
  guint color = 0;
  gboolean success = FALSE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  destination->x = timestamp->position.x;
  destination->y = timestamp->position.y;

  destination->w = GST_VIDEO_FRAME_WIDTH (vframe);
  destination->h = GST_VIDEO_FRAME_HEIGHT (vframe);

  fontsize = timestamp->fontsize;
  color = timestamp->color;

  switch (timestamp->type) {
    case GST_OVERLAY_TIMESTAMP_DATE_TIME:
    {
      GDateTime *datetime = g_date_time_new_now_local ();
      text = g_date_time_format (datetime, timestamp->format);
      g_date_time_unref (datetime);
      break;
    }
    case GST_OVERLAY_TIMESTAMP_PTS_DTS:
    {
      GstClockTime time = GST_BUFFER_DTS_IS_VALID (vframe->buffer) ?
          GST_BUFFER_DTS (vframe->buffer) : GST_BUFFER_PTS (vframe->buffer);

      text = g_strdup_printf ("%" GST_TIME_FORMAT, GST_TIME_ARGS (time));
      break;
    }
    default:
      GST_ERROR_OBJECT (overlay, "Unknown timestamp type %d!", timestamp->type);
      return FALSE;
  }

  n_chars = strlen (text);

  // Limit the fontsize if it is not possible to put the text in the buffer.
  fontsize =
      MIN ((GST_VIDEO_FRAME_WIDTH (vframe) / n_chars) * 5.0 / 3.0, fontsize);

  if ((GST_VIDEO_FRAME_HEIGHT (vframe) / fontsize) < 1.0)
    fontsize = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Calculate the scale factor, will be use to update destination rectangle.
  scale = timestamp->fontsize / fontsize;

  // Scale destination rectangle dimensions in order to match the set fontsize.
  destination->w *= (scale > 1.0) ? scale : 1;
  destination->h *= (scale > 1.0) ? scale : 1;

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  GST_TRACE_OBJECT (overlay, "String: '%s', Color: 0x%X, Position: [%d %d]",
      text, timestamp->color, timestamp->position.x, timestamp->position.y);

  success = gst_cairo_draw_text (context, color, 0.0, 0.0, text, fontsize);
  g_free (text);

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_string_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GstOverlayString * string)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gchar *text = NULL;
  gdouble fontsize = 0.0, n_chars = 0.0, scale = 0.0;
  guint color = 0;
  gboolean success = FALSE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  destination->x = string->position.x;
  destination->y = string->position.y;

  destination->w = GST_VIDEO_FRAME_WIDTH (vframe);
  destination->h = GST_VIDEO_FRAME_HEIGHT (vframe);

  fontsize = string->fontsize;
  color = string->color;

  text = string->contents;
  n_chars = strlen (text);

  // Limit the fontsize if it is not possible to put the text in the buffer.
  fontsize =
      MIN ((GST_VIDEO_FRAME_WIDTH (vframe) / n_chars) * 5.0 / 3.0, fontsize);

  if ((GST_VIDEO_FRAME_HEIGHT (vframe) / fontsize) < 1.0)
    fontsize = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Calculate the scale factor, will be use to update destination rectangle.
  scale = string->fontsize / fontsize;

  // Scale destination rectangle dimensions in order to match the set fontsize.
  destination->w *= (scale > 1.0) ? scale : 1;
  destination->h *= (scale > 1.0) ? scale : 1;

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  GST_TRACE_OBJECT (overlay, "String: '%s', Color: 0x%X, Position: [%d %d]",
      string->contents, string->color, string->position.x,
      string->position.y);

  success = gst_cairo_draw_text (context, color, 0.0, 0.0, text, fontsize);

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_mask_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GstOverlayMask * mask)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoFrame *vframe = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gdouble x = 0.0, y = 0.0, linewidth = 0.0, scale = 0.0;
  guint color = 0;
  gboolean success = FALSE, infill = TRUE;

  success = gst_cairo_draw_setup (blit->frame, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  vframe = blit->frame;
  source = &(blit->sources[0]);
  destination = &(blit->destinations[0]);

  switch (mask->type) {
    case GST_OVERLAY_MASK_RECTANGLE:
      source->w = destination->w = mask->dims.rectangle.w;
      source->h = destination->h = mask->dims.rectangle.h;

      destination->x = mask->dims.rectangle.x;
      destination->y = mask->dims.rectangle.y;
      break;
    case GST_OVERLAY_MASK_CIRCLE:
      source->w = destination->w = mask->dims.circle.radius * 2;
      source->h = destination->h = mask->dims.circle.radius * 2;

      destination->x = mask->dims.circle.x - mask->dims.circle.radius;
      destination->y = mask->dims.circle.y - mask->dims.circle.radius;
      break;
    case GST_OVERLAY_MASK_POLYGON:
      source->w = destination->w = mask->dims.polygon.region.w;
      source->h = destination->h = mask->dims.polygon.region.h;

      destination->x = mask->dims.polygon.region.x;
      destination->y = mask->dims.polygon.region.y;
      break;
    default:
      GST_ERROR_OBJECT (overlay, "Unknown privacy mask type %d!", mask->type);
      return FALSE;
  }

  color = mask->color;
  infill = mask->infill;

  // Adjust mask source dimensions so that it fits inside the overlay frame.
  gst_overlay_update_rectangle_dimensions (overlay, vframe, source);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  // Set the most appropriate box line width based on frame and box dimensions.
  gst_util_fraction_to_double (destination->w, source->w, &scale);
  linewidth = (scale > 1.0F) ? (4.0F / scale) : 4.0F;

  if (GST_OVERLAY_MASK_RECTANGLE == mask->type) {
    gdouble width = 0.0, height = 0.0;

    width = source->w;
    height = source->h;

    GST_TRACE_OBJECT (overlay, "Rectangle: [%.2f %.2f %.2f %.2f], Color: 0x%X",
        x, y, width, height, color);

    success = gst_cairo_draw_rectangle (context, color, x, y, width, height,
        linewidth, infill);
  } else if (GST_OVERLAY_MASK_CIRCLE == mask->type) {
    gdouble radius = 0.0;

    radius = source->w / 2.0;
    x = y = radius;

    GST_TRACE_OBJECT (overlay, "Circle: [%.2f %.2f %.2f], Color: 0x%X", x, y,
        radius, color);

    success = gst_cairo_draw_circle (context, color, x, y, radius,
        linewidth, infill);
  } else if (GST_OVERLAY_MASK_POLYGON == mask->type) {
    gdouble coords[GST_VIDEO_POLYGON_MAX_POINTS * 2];
    guint idx = 0, num = 0, n_coords = 0;

    n_coords = mask->dims.polygon.n_points * 2;

    for (idx = 0; idx < mask->dims.polygon.n_points; idx++, num += 2) {
      coords[num] = (mask->dims.polygon.points[idx].x - destination->x) / scale;
      coords[num + 1] = (mask->dims.polygon.points[idx].y - destination->y) / scale;

      GST_TRACE_OBJECT (overlay, "Polygon: [%.2f %.2f], Color: 0x%X",
          coords[num], coords[num + 1], color);
    }

    success = gst_cairo_draw_polygon (context, color, coords, n_coords,
        linewidth, infill);
  }

  gst_cairo_draw_cleanup (blit->frame, surface, context);
  return success;
}

static gboolean
gst_overlay_handle_image_entry (GstVOverlay * overlay, GstVideoBlit * blit,
    GstOverlayImage * simage)
{
  GstVideoFrame *vframe = NULL;
  GError *error = NULL;
  gchar *contents = NULL, *data = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  gint x = 0, num = 0, id = 0;

  vframe = blit->frame;
  source = &(blit->sources[0]);

  source->w = simage->width;
  source->h = simage->height;

  blit->destinations[0] = simage->destination;
  destination = &(blit->destinations[0]);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles: [%d %d %d %d] -> "
      "[%d %d %d %d]", source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

  // Load static image file contents in case it was not already loaded.
  if (simage->width > GST_VIDEO_FRAME_WIDTH (vframe)) {
    GST_ERROR_OBJECT (overlay, "Static image width (%u) is greater than the "
        "frame width (%u)!", simage->width, GST_VIDEO_FRAME_WIDTH (vframe));
    return FALSE;
  } else if (simage->height > GST_VIDEO_FRAME_HEIGHT (vframe)) {
    GST_ERROR_OBJECT (overlay, "Static image height (%u) is greater than the "
        "frame height (%u)!", simage->height, GST_VIDEO_FRAME_HEIGHT (vframe));
    return FALSE;
  }

  if (!g_file_test (simage->path, G_FILE_TEST_IS_REGULAR)) {
    GST_ERROR_OBJECT (overlay, "Static image path '%s' is not a regular file!",
        simage->path);
    return FALSE;
  }

  if (!g_file_get_contents (simage->path, &contents, NULL, &error)) {
    GST_WARNING_OBJECT (overlay, "Failed to laod static image file '%s', "
        "error: %s!", simage->path, GST_STR_NULL (error->message));

    g_clear_error (&error);
    return FALSE;
  }

  data = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  for (x = 0; x < simage->height; x++, num += (simage->width * 4)) {
    id = x * GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0);
    memcpy (&data[id], &contents[num], (simage->width * 4));
  }

  return TRUE;
}

static gboolean
gst_overlay_populate_video_blit (GstVOverlay * overlay, guint ovltype,
    GstVideoBlit * blit)
{
  GstBufferPool *pool = NULL;
  GstVideoInfo *info = NULL;
  GstBuffer *buffer = NULL;

  pool = overlay->ovlpools[ovltype];
  info = overlay->ovlinfos[ovltype];

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (overlay, "Failed to activate overlay buffer pool!");
    return FALSE;
  }

  if (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (overlay, "Failed to acquire overlay buffer!");
    return FALSE;
  }

  blit->frame = g_slice_new0 (GstVideoFrame);

  if (!gst_video_frame_map (blit->frame, info, buffer,
          GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (overlay, "Failed to map overlay buffer!");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  blit->alpha = G_MAXUINT8;

  // Allocate and intialize source and destination rectangles.
  blit->sources = g_slice_new (GstVideoRectangle);
  blit->destinations = g_slice_new (GstVideoRectangle);
  blit->n_regions = 1;

  // Initialize the blit source rectangle.
  blit->sources[0].x = blit->sources[0].y = 0;
  blit->sources[0].w = GST_VIDEO_FRAME_WIDTH (blit->frame);
  blit->sources[0].h = GST_VIDEO_FRAME_HEIGHT (blit->frame);

  // Initialize the blit destination rectangle.
  blit->destinations[0].x = blit->destinations[0].y = 0;
  blit->destinations[0].w = GST_VIDEO_INFO_WIDTH (overlay->vinfo);
  blit->destinations[0].h = GST_VIDEO_INFO_HEIGHT (overlay->vinfo);

  return TRUE;
}

static gboolean
gst_overlay_draw_metadata_entries (GstVOverlay * overlay,
    GstVideoComposition * composition, guint * index)
{
  GstBuffer *outbuffer = composition->frame->buffer;
  GstMeta *meta = NULL;
  gpointer state = NULL;
  gboolean success = TRUE;

  // Iterate over the buffer meta and process the supported entries.
  while ((meta = gst_buffer_iterate_meta (outbuffer, &state)) != NULL) {
    guint ovltype = gst_meta_overlay_type (meta);
    guint n_blits = 1;

    switch (ovltype) {
      case GST_OVERLAY_TYPE_DETECTION:
      {
        // Two blit objects, one for bounding box and one for the labels.
        GstVideoBlit *blit = &(composition->blits[(*index)]);
        GstVideoBlit *auxblit = &(composition->blits[(*index) + 1]);

        success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
        g_return_val_if_fail (success, FALSE);

        ovltype = GST_OVERLAY_TYPE_CLASSIFICATION;
        success = gst_overlay_populate_video_blit (overlay, ovltype, auxblit);
        g_return_val_if_fail (success, FALSE);

        success = gst_overlay_handle_detection_entry (overlay, blit, auxblit,
            GST_VIDEO_ROI_META_CAST (meta));

        n_blits = 2;
        break;
      }
      case GST_OVERLAY_TYPE_CLASSIFICATION:
      {
        GstVideoBlit *blit = &(composition->blits[(*index)]);
        GArray *labels = GST_VIDEO_CLASSIFICATION_META_CAST (meta)->labels;

        success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
        g_return_val_if_fail (success, FALSE);

        success = gst_overlay_handle_classification_entry (overlay, blit, labels);
        break;
      }
      case GST_OVERLAY_TYPE_POSE_ESTIMATION:
      {
        GstVideoBlit *blit = &(composition->blits[*index]);
        GArray *keypoints = GST_VIDEO_LANDMARKS_META_CAST (meta)->keypoints;
        GArray *links = GST_VIDEO_LANDMARKS_META_CAST (meta)->links;

        success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
        g_return_val_if_fail (success, FALSE);

        success = gst_overlay_handle_pose_entry (overlay, blit, keypoints, links);
        break;
      }
      case GST_OVERLAY_TYPE_OPTCLFLOW:
      {
        GstVideoBlit *blit = &(composition->blits[(*index)]);
        GArray *mvectors = GST_CV_OPTCLFLOW_META_CAST (meta)->mvectors;
        GArray *stats = GST_CV_OPTCLFLOW_META_CAST (meta)->stats;

        success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
        g_return_val_if_fail (success, FALSE);

        success = gst_overlay_handle_optclflow_entry (overlay, blit,
            mvectors, stats);
        break;
      }
      default:
        // Skip meta entries that are not among the supported overlay types.
        continue;
    }

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process meta %u!", (*index));
      return FALSE;
    }

    // Increase the index with the number of populated blit objects.
    *index += n_blits;
  }

  return TRUE;
}

static gboolean
gst_overlay_draw_bbox_entries (GstVOverlay * overlay,
    GstVideoComposition * composition, guint * index)
{
  guint num = 0;
  gboolean success = TRUE;

  for (num = 0; num < overlay->bboxes->len; num++) {
    GstOverlayBBox *bbox = &g_array_index (overlay->bboxes, GstOverlayBBox, num);

    // Skip this bounding box entry as it has been disabled.
    if (!bbox->enable)
      continue;

    if (bbox->blit.frame != NULL) {
      // Take the blit parameters from the cached object.
      composition->blits[(*index)] = bbox->blit;
    } else {
      GstVideoBlit *blit = &(composition->blits[(*index)]);
      guint ovltype = GST_OVERLAY_TYPE_BBOX;

      success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
      g_return_val_if_fail (success, FALSE);

      success = gst_overlay_handle_bbox_entry (overlay, blit, bbox);
      if (!success) {
        GST_ERROR_OBJECT (overlay, "Failed to process bounding box %u!", num);
        return FALSE;
      }

      // Save the blit parameters for this entry until something changes.
      bbox->blit = composition->blits[(*index)];
      // Increase the buffer refcount, this will be used as indicator that
      // the blit object has been cached and its parameters won't be freed.
      gst_buffer_ref (bbox->blit.frame->buffer);
    }

    // Increase the index with the number of populated blit objects.
    *index += 1;
  }

  return TRUE;
}

static gboolean
gst_overlay_draw_timestamp_entries (GstVOverlay * overlay,
    GstVideoComposition * composition, guint * index)
{
  GstVideoBlit *blit = NULL;
  guint ovltype = GST_OVERLAY_TYPE_TIMESTAMP, num = 0;
  gboolean success = TRUE;

  for (num = 0; num < overlay->timestamps->len; num++) {
    GstOverlayTimestamp *timestamp =
        &g_array_index (overlay->timestamps, GstOverlayTimestamp, num);

    // Skip this timstamp entry as it has been disabled.
    if (!timestamp->enable)
      continue;

    blit = &(composition->blits[(*index)]);

    success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
    g_return_val_if_fail (success, FALSE);

    GST_BUFFER_DTS (blit->frame->buffer) =
        GST_BUFFER_DTS (composition->frame->buffer);
    GST_BUFFER_PTS (blit->frame->buffer) =
        GST_BUFFER_PTS (composition->frame->buffer);

    success = gst_overlay_handle_timestamp_entry (overlay, blit, timestamp);
    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process timestamp %u!", num);
      return FALSE;
    }

    // Increase the index with the number of populated blit objects.
    *index += 1;
  }

  return TRUE;
}

static gboolean
gst_overlay_draw_string_entries (GstVOverlay * overlay,
    GstVideoComposition * composition, guint * index)
{
  guint num = 0;
  gboolean success = TRUE;

  for (num = 0; num < overlay->strings->len; num++) {
    GstOverlayString *string =
        &g_array_index (overlay->strings, GstOverlayString, num);

    // Skip this text entry as it has been disabled.
    if (!string->enable)
      continue;

    if (string->blit.frame != NULL) {
      // Take the blit parameters from the cached object.
      composition->blits[(*index)] = string->blit;
    } else {
      GstVideoBlit *blit = &(composition->blits[(*index)]);
      guint ovltype = GST_OVERLAY_TYPE_STRING;

      success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
      g_return_val_if_fail (success, FALSE);

      success = gst_overlay_handle_string_entry (overlay, blit, string);
      if (!success) {
        GST_ERROR_OBJECT (overlay, "Failed to process string %u!", num);
        return FALSE;
      }

      // Save the blit parameters for this entry until something changes.
      string->blit = composition->blits[(*index)];
      // Increase the buffer refcount, this will be used as indicator that
      // the blit object has been cached and its parameters won't be freed.
      gst_buffer_ref (string->blit.frame->buffer);
    }

    // Increase the index with the number of populated blit objects.
    *index += 1;
  }

  return TRUE;
}

static gboolean
gst_overlay_draw_mask_entries (GstVOverlay * overlay,
    GstVideoComposition * composition, guint * index)
{
  guint num = 0;
  gboolean success = TRUE;

  for (num = 0; num < overlay->masks->len; num++) {
    GstOverlayMask *mask = &g_array_index (overlay->masks, GstOverlayMask, num);

    // Skip this privacy mask entry as it has been disabled.
    if (!mask->enable)
      continue;

    if (mask->blit.frame != NULL) {
      // Take the blit parameters from the cached object.
      composition->blits[(*index)] = mask->blit;
    } else {
      GstVideoBlit *blit = &(composition->blits[(*index)]);
      guint ovltype = GST_OVERLAY_TYPE_MASK;

      success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
      g_return_val_if_fail (success, FALSE);

      success = gst_overlay_handle_mask_entry (overlay, blit, mask);
      if (!success) {
        GST_ERROR_OBJECT (overlay, "Failed to process privacy mask %u!", num);
        return FALSE;
      }

      // Save the blit parameters for this entry until something changes.
      mask->blit = composition->blits[(*index)];
      // Increase the buffer refcount, this will be used as indicator that
      // the blit object has been cached and its parameters won't be freed.
      gst_buffer_ref (mask->blit.frame->buffer);
    }

    // Increase the index with the number of populated blit objects.
    *index += 1;
  }

  return TRUE;
}

static gboolean
gst_overlay_draw_static_image_entries (GstVOverlay * overlay,
    GstVideoComposition * composition, guint * index)
{
  guint num = 0;
  gboolean success = TRUE;

  for (num = 0; num < overlay->simages->len; num++) {
    GstOverlayImage *simage =
        &g_array_index (overlay->simages, GstOverlayImage, num);

    // Skip this static image entry as it has been disabled.
    if (!simage->enable)
      continue;

    if (simage->blit.frame != NULL) {
      // Take the blit parameters from the cached object.
      composition->blits[(*index)] = simage->blit;
    } else {
      GstVideoBlit *blit = &(composition->blits[(*index)]);
      guint ovltype = GST_OVERLAY_TYPE_IMAGE;

      success = gst_overlay_populate_video_blit (overlay, ovltype, blit);
      g_return_val_if_fail (success, FALSE);

      success = gst_overlay_handle_image_entry (overlay, blit, simage);
      if (!success) {
        GST_ERROR_OBJECT (overlay, "Failed to process static image %u!", num);
        return FALSE;
      }

      // Save the blit parameters for this entry until something changes.
      simage->blit = composition->blits[(*index)];
      // Increase the buffer refcount, this will be used as indicator that
      // the blit object has been cached and its parameters won't be freed.
      gst_buffer_ref (simage->blit.frame->buffer);
    }

    // Increase the index with the number of populated blit objects.
    *index += 1;
  }

  return TRUE;
}

static gboolean
gst_overlay_draw_ovelay_blits (GstVOverlay * overlay,
    GstVideoComposition * composition)
{
  GstBuffer *outbuffer = composition->frame->buffer;
  guint index = 0;
  gboolean success = TRUE;

  // Add the total number of meta entries that needs to be processed.
  composition->n_blits = 2 * gst_buffer_get_n_meta (outbuffer,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
  composition->n_blits += gst_buffer_get_n_meta (outbuffer,
      GST_VIDEO_CLASSIFICATION_META_API_TYPE);
  composition->n_blits += gst_buffer_get_n_meta (outbuffer,
      GST_VIDEO_LANDMARKS_META_API_TYPE);
  composition->n_blits += gst_buffer_get_n_meta (outbuffer,
      GST_CV_OPTCLFLOW_META_API_TYPE);

  GST_OVERLAY_LOCK (overlay);

  // Add the number of manually set bounding boxes.
  composition->n_blits += overlay->bboxes->len;
  // Add the number of manually set timestamps.
  composition->n_blits += overlay->timestamps->len;
  // Add the number of manually set strings.
  composition->n_blits += overlay->strings->len;
  // Add the number of manually set privacy masks.
  composition->n_blits += overlay->masks->len;
  // Add the number of manually set static images.
  composition->n_blits += overlay->simages->len;

  // Allocate maximum possible blit structures for each of the entries.
  composition->blits = g_new0 (GstVideoBlit, composition->n_blits);

  // Iterate over the buffer meta and process the supported entries.
  success = gst_overlay_draw_metadata_entries (overlay, composition, &index);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to process metatada blits!");
    goto cleanup;
  }

  // Process manually set bounding boxes.
  success = gst_overlay_draw_bbox_entries (overlay, composition, &index);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to process bbox blits!");
    goto cleanup;
  }

  // Process manually set timestamps.
  success = gst_overlay_draw_timestamp_entries (overlay, composition, &index);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to process timestamps blits!");
    goto cleanup;
  }

  // Process manually set strings.
  success = gst_overlay_draw_string_entries (overlay, composition, &index);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to process strings blits!");
    goto cleanup;
  }

  // Process manually set privacy masks.
  success = gst_overlay_draw_mask_entries (overlay, composition, &index);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to process masks blits!");
    goto cleanup;
  }

  // Process manually set static images.
  success = gst_overlay_draw_static_image_entries (overlay, composition, &index);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to process static image blits!");
    goto cleanup;
  }

  // Resize the blits array as actual number is less then the maximum.
  if (index < composition->n_blits) {
    composition->blits = g_renew (GstVideoBlit, composition->blits, index);
    composition->n_blits = index;
  }

cleanup:
  if (!success)
    gst_video_blits_release (composition->blits, composition->n_blits);

  GST_OVERLAY_UNLOCK (overlay);
  return success;
}

static gboolean
gst_overlay_query (GstBaseTransform * base, GstPadDirection direction,
    GstQuery * query)
{
  GstVOverlay *overlay = GST_OVERLAY (base);
  GstPad *otherpad = NULL;

  GST_TRACE_OBJECT (overlay, "Received query: %" GST_PTR_FORMAT
    " in direction %s", query, (direction == GST_PAD_SINK) ? "sink" : "src");

  otherpad = (direction == GST_PAD_SRC) ?
      GST_BASE_TRANSFORM_SINK_PAD (base) : GST_BASE_TRANSFORM_SRC_PAD (base);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min = 0, max = 0, latency = 0;
      gboolean live = FALSE;

      // If query on peer pad failed break and call the base class function.
      if (!gst_pad_peer_query (otherpad, query))
        break;

      gst_query_parse_latency (query, &live, &min, &max);

      GST_DEBUG_OBJECT (overlay, "Peer latency : min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT, GST_TIME_ARGS (min),  GST_TIME_ARGS (max));

      GST_OBJECT_LOCK (overlay);
      latency = overlay->latency;
      GST_OBJECT_UNLOCK (overlay);

      GST_DEBUG_OBJECT (overlay, "Our latency: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (latency));

      min += latency;
      max += (max != GST_CLOCK_TIME_NONE) ? latency : 0;

      GST_DEBUG_OBJECT (overlay, "Total latency : min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT, GST_TIME_ARGS (min), GST_TIME_ARGS (max));

      gst_query_set_latency (query, live, min, max);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (base, direction, query);
}

static gboolean
gst_overlay_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVOverlay *overlay = GST_OVERLAY (base);
  GstVideoInfo info = { 0 };
  guint ovltype = 0, width = 0, height = 0;
  gint num = 1, denum = 1;

  if (!gst_caps_is_equal_fixed (incaps, outcaps)) {
    GST_ELEMENT_ERROR (overlay, CORE, NEGOTIATION, (NULL),
        ("Input and output caps are not equal!"));
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, incaps)) {
    GST_ERROR_OBJECT (overlay, "Failed to get video info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (overlay->vinfo != NULL)
    gst_video_info_free (overlay->vinfo);

  overlay->vinfo = gst_video_info_copy (&info);

  if (!gst_util_fraction_multiply (info.width, info.height,
          info.par_n, info.par_d, &num, &denum))
    GST_WARNING_OBJECT (overlay, "Failed to calculate DAR!");

  // Initialize internal overlay buffer pools.
  for (ovltype = 0; ovltype < GST_OVERLAY_TYPE_MAX; ovltype++) {
    GstCaps *caps = NULL;

    width = GST_VIDEO_INFO_WIDTH (overlay->vinfo);
    height = GST_VIDEO_INFO_HEIGHT (overlay->vinfo);

    if ((ovltype == GST_OVERLAY_TYPE_BBOX) ||
        (ovltype == GST_OVERLAY_TYPE_DETECTION) ||
        (ovltype == GST_OVERLAY_TYPE_MASK)) {
      // Square resolution of atleats 256 is most optimal.
      width = height = GST_ROUND_UP_128 (MAX (MAX (width, height) / 8, 256));
    } else if (ovltype == GST_OVERLAY_TYPE_IMAGE) {
      // Square resolution 4 times smaller than the frame is most optimal.
      width = height = GST_ROUND_UP_128 (MAX (width, height) / 4);
    } else if (ovltype == GST_OVERLAY_TYPE_POSE_ESTIMATION) {
      // For pose estimation a 8 times lower resolution seems to be optimal.
      gst_recalculate_dimensions (&width, &height, num, denum, 4);
    } else if ((ovltype == GST_OVERLAY_TYPE_STRING) ||
               (ovltype == GST_OVERLAY_TYPE_TIMESTAMP)) {
      // For custom text overlay resolution with aspect ratio 4:1 is optimal.
      width = GST_ROUND_UP_128 (MAX (width / 6, 256));
      height = GST_ROUND_UP_4 (width / 4);
    } else if (ovltype == GST_OVERLAY_TYPE_CLASSIFICATION) {
      // For classification overlay resolution with aspect ratio 32:10 is optimal.
      width = GST_ROUND_UP_128 (MAX (width / 6, 512));
      height = GST_ROUND_UP_4 ((width * 10) / 32);
    } else if (ovltype == GST_OVERLAY_TYPE_OPTCLFLOW) {
      // For optical flow a 4 times lower resolution seems to be optimal.
      gst_recalculate_dimensions (&width, &height, num, denum, 2);
    } else {
      GST_ERROR_OBJECT (overlay, "Unsupported overlay type %u!", ovltype);
      return FALSE;
    }

    caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "BGRA",
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

    if (overlay->ovlpools[ovltype] != NULL) {
      gst_buffer_pool_set_active (overlay->ovlpools[ovltype], FALSE);
      gst_object_unref (overlay->ovlpools[ovltype]);
    }

    overlay->ovlpools[ovltype] = gst_overlay_create_pool (overlay, caps);

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (overlay, "Failed to get video info from caps %"
          GST_PTR_FORMAT "!", caps);

      gst_caps_unref (caps);
      return FALSE;
    }

    if (overlay->ovlinfos[ovltype] != NULL)
      gst_video_info_free (overlay->ovlinfos[ovltype]);

    overlay->ovlinfos[ovltype] = gst_video_info_copy (&info);
    gst_caps_unref (caps);
  }

  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_in_place (base, TRUE);

  if (overlay->converter != NULL)
    gst_video_converter_engine_free (overlay->converter);

  overlay->converter = gst_video_converter_engine_new (overlay->backend, NULL);

  GST_DEBUG_OBJECT (overlay, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (overlay, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_overlay_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstVOverlay *overlay = GST_OVERLAY (base);

  if (!gst_buffer_is_writable (inbuffer))
    GST_TRACE_OBJECT (overlay, "Input buffer %p is not writable!", inbuffer);

  *outbuffer = inbuffer;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_overlay_transform_ip (GstBaseTransform * base, GstBuffer * buffer)
{
  GstVOverlay *overlay = GST_OVERLAY (base);
  GstVideoFrame outframe = { 0 };
  GstVideoComposition composition = GST_VCE_COMPOSITION_INIT;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate buffer downstream.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  if (!gst_buffer_is_writable (buffer)) {
    GST_WARNING_OBJECT (overlay, "Buffer %p not writable, skipping!", buffer);
    return GST_FLOW_OK;
  }

  time = gst_util_get_timestamp ();

  if (!gst_video_frame_map (&outframe, overlay->vinfo, buffer,
          GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (overlay, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  composition.frame = &outframe;

  // Extract metadata entries from the buffer and create overlay blit objects.
  if (!gst_overlay_draw_ovelay_blits (overlay, &composition)) {
    GST_ERROR_OBJECT (overlay, "Failed to draw overlay frames!");
    return GST_FLOW_ERROR;
  }

  // Check if there is need for applying any overlay frames.
  if ((composition.blits == NULL) && (composition.n_blits == 0)) {
    gst_video_frame_unmap (&outframe);
    return GST_FLOW_OK;
  }

  success = gst_video_converter_engine_compose (overlay->converter,
      &composition, 1, NULL);

  gst_video_blits_release (composition.blits, composition.n_blits);
  gst_video_frame_unmap (&outframe);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to apply overlays!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (overlay, "Process took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  GST_OBJECT_LOCK (overlay);
  overlay->latency = (time > overlay->latency) ? time : overlay->latency;
  GST_OBJECT_UNLOCK (overlay);

  return GST_FLOW_OK;
}

static void
gst_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVOverlay *overlay = GST_OVERLAY (object);
  GValue list = G_VALUE_INIT;

  GST_OVERLAY_LOCK (overlay);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      overlay->backend = g_value_get_enum (value);
      break;
    case PROP_BBOXES:
      g_value_init (&list, GST_TYPE_LIST);

      if (!gst_parse_string_property_value (value, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for bboxes!");
        break;
      }

      if (!gst_extract_bboxes (&list, overlay->bboxes))
        GST_ERROR_OBJECT (overlay, "Failed to exract bboxes!");

      g_value_unset (&list);
      break;
    case PROP_TIMESTAMPS:
      g_value_init (&list, GST_TYPE_LIST);

      if (!gst_parse_string_property_value (value, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for timestamps!");
        break;
      }

      if (!gst_extract_timestamps (&list, overlay->timestamps))
        GST_ERROR_OBJECT (overlay, "Failed to exract timestamps!");

      g_value_unset (&list);
      break;
    case PROP_STRINGS:
      g_value_init (&list, GST_TYPE_LIST);

      if (!gst_parse_string_property_value (value, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for strings!");
        break;
      }

      if (!gst_extract_strings (&list, overlay->strings))
        GST_ERROR_OBJECT (overlay, "Failed to exract strings!");

      g_value_unset (&list);
      break;
    case PROP_PRIVACY_MASKS:
      g_value_init (&list, GST_TYPE_LIST);

      if (!gst_parse_string_property_value (value, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for masks!");
        break;
      }

      if (!gst_extract_masks (&list, overlay->masks))
        GST_ERROR_OBJECT (overlay, "Failed to exract privacy masks!");

      g_value_unset (&list);
      break;
    case PROP_STATIC_IMAGES:
      g_value_init (&list, GST_TYPE_LIST);

      if (!gst_parse_string_property_value (value, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for images!");
        break;
      }

      if (!gst_extract_static_images (&list, overlay->simages))
        GST_ERROR_OBJECT (overlay, "Failed to exract static images!");

      g_value_unset (&list);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OVERLAY_UNLOCK (overlay);
}

static void
gst_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVOverlay *overlay = GST_OVERLAY (object);
  gchar *string = NULL;

  GST_OVERLAY_LOCK (overlay);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      g_value_set_enum (value, overlay->backend);
      break;
    case PROP_BBOXES:
      string = gst_serialize_bboxes (overlay->bboxes);
      g_value_take_string (value, string);
      break;
    case PROP_TIMESTAMPS:
      string = gst_serialize_strings (overlay->timestamps);
      g_value_take_string (value, string);
      break;
    case PROP_STRINGS:
      string = gst_serialize_strings (overlay->strings);
      g_value_take_string (value, string);
      break;
    case PROP_PRIVACY_MASKS:
      string = gst_serialize_masks (overlay->masks);
      g_value_take_string (value, string);
      break;
    case PROP_STATIC_IMAGES:
      string = gst_serialize_static_images (overlay->simages);
      g_value_take_string (value, string);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OVERLAY_UNLOCK (overlay);
}

static void
gst_overlay_finalize (GObject * object)
{
  GstVOverlay *overlay = GST_OVERLAY (object);
  guint idx = 0;

  if (overlay->timestamps != NULL)
    g_array_free (overlay->timestamps, TRUE);

  if (overlay->bboxes != NULL)
    g_array_free (overlay->bboxes, TRUE);

  if (overlay->strings != NULL)
    g_array_free (overlay->strings, TRUE);

  if (overlay->simages != NULL)
    g_array_free (overlay->simages, TRUE);

  if (overlay->masks != NULL)
    g_array_free (overlay->masks, TRUE);

  gst_video_converter_engine_free (overlay->converter);

  for (idx = 0; idx < GST_OVERLAY_TYPE_MAX; idx++) {
    if (overlay->ovlpools[idx] != NULL)
      gst_object_unref (overlay->ovlpools[idx]);

    if (overlay->ovlinfos[idx] != NULL)
      gst_video_info_free (overlay->ovlinfos[idx]);
  }

  if (overlay->vinfo != NULL)
    gst_video_info_free (overlay->vinfo);

  g_mutex_clear (&(overlay)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (overlay));
}

static void
gst_overlay_class_init (GstVOverlayClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_overlay_debug, "qtivoverlay", 0,
      "QTI video overlay plugin");

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_overlay_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_overlay_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_overlay_finalize);

  g_object_class_install_property (gobject, PROP_ENGINE_BACKEND,
      g_param_spec_enum ("engine", "Engine",
          "Engine backend used for the blitting operations",
          GST_TYPE_VCE_BACKEND, DEFAULT_PROP_ENGINE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_BBOXES,
      g_param_spec_string ("bboxes", "BBoxes",
          "Manually set multiple custom bounding boxes in list of GstStructures "
          "with unique name and 3 parameters 'position', 'dimensions' and 'color'. "
          "The 'position' and 'dimensions' are mandatory if struct entry is new "
          "e.g. \"{(structure)\\\"Box1,position=<100,100>,dimensions=<640,480>;"
          "\\\", (structure)\\\"Box2,position=<1000,100>,dimensions=<300,300>,"
          "color=0xFF0000FF;\\\"}\"", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_TIMESTAMPS,
      g_param_spec_string ("timestamps", "Timestamps",
          "Manually set various timestamps as GstStructures with 'Date/Time' as"
          " keyword for displaying date and/or time with 4 optional parameters"
          " 'format', 'fontsize', 'position', and 'color'. And use 'PTS/DTS' "
          "as keyword dispalying buffer timestamp with 3 optional parameters "
          "'fontsize', 'position', and 'color' e.g. \"{(structure)\\\"Date/Time"
          ",format=\\\\\\\"%d/%m/%Y\\ %H:%M:%S\\\\\\\",fontsize=12,"
          "position=<0,0>,color=0xRRGGBBAA;\\\", (structure)\\\"PTS/DTS,"
          "fontsize=12,position=<0,0>,color=0xRRGGBBAA;\\\"}\"", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_STRINGS,
      g_param_spec_string ("strings", "Strings",
          "Manually set multiple custom strings in list of GstStructures with "
          "unique name and 4 parameters 'contents', 'fontsize', 'position', "
          "and 'color'. The 'contents' is mandatory if struct entry is new "
          "e.g. \"{(structure)\\\"Text1,contents=\\\\\\\"Example\\ 1\\\\\\\","
          "fontsize=12,position=<0,0>,color=0xRRGGBBAA;\\\"}\"", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_STATIC_IMAGES,
      g_param_spec_string ("images", "Images",
          "Manually set multiple custom BGRA images in list of GstStructures with "
          "unique name and 3 parameters 'path', 'resolution', 'destination'. "
          "All 3 are mandatory if struct entry is new e.g. \"{(structure)\\\""
          "Image1,path=/data/image1.bgra,resolution=<480,360>,destination="
          "<0,0,640,480>;\\\", (structure)\\\"Image2,path=/data/image2.bgra,"
          "resolution=<240,180>,destination=<100,100,480,360>;\\\"}\"", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_PRIVACY_MASKS,
      g_param_spec_string ("masks", "Masks",
          "Manually set multiple masks in list of GstStructures with unique "
          "name and 2 parameters 'color' and either 'circle=<X, Y, RADIUS>' or "
          "'rectangle=<X, Y, WIDTH, HEIGHT>'. Either circle or rectangle must "
          "be provided if struct entry is new e.g. \"{(structure)"
          "\\\"Mask1,color=0xRRGGBBAA,circle=<400,400,200>;\\\",(structure)"
          "\\\"Mask2,color=0xRRGGBBAA,rectangle=<0,0,20,10>;\\\",(structure)"
          "\\\"Mask3,color=0xRRGGBBAA,polygon=<<2,2>,<2,4>,<4,4>>;\\\"}\"", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));

  gst_element_class_set_static_metadata (element, "Video Overlay",
      "Filter/Effect", "Generic plugin to extract meta like ROI from image "
      "buffer and overlaying that data on top of that buffer", "QTI");

  gst_element_class_add_pad_template (element, gst_overlay_sink_template ());
  gst_element_class_add_pad_template (element, gst_overlay_src_template ());

  base->query = GST_DEBUG_FUNCPTR (gst_overlay_query);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_overlay_set_caps);

  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_overlay_prepare_output_buffer);
  base->transform_ip = GST_DEBUG_FUNCPTR (gst_overlay_transform_ip);
}

static void
gst_overlay_init (GstVOverlay * overlay)
{
  guint idx = 0;

  g_mutex_init (&(overlay)->lock);

  overlay->latency = 0;
  overlay->vinfo = NULL;

  for (idx = 0; idx < GST_OVERLAY_TYPE_MAX; idx++) {
    overlay->ovlpools[idx] = NULL;
    overlay->ovlinfos[idx] = NULL;
  }

  overlay->converter = NULL;

  overlay->backend = DEFAULT_PROP_ENGINE_BACKEND;
  overlay->bboxes = g_array_new (FALSE, TRUE, sizeof (GstOverlayBBox));
  overlay->timestamps = g_array_new (FALSE, TRUE, sizeof (GstOverlayTimestamp));
  overlay->strings = g_array_new (FALSE, TRUE, sizeof (GstOverlayString));
  overlay->simages = g_array_new (FALSE, TRUE, sizeof (GstOverlayImage));
  overlay->masks = g_array_new (FALSE, TRUE, sizeof (GstOverlayMask));

  g_array_set_clear_func (overlay->timestamps,
      (GDestroyNotify) gst_overlay_timestamp_free);
  g_array_set_clear_func (overlay->strings,
      (GDestroyNotify) gst_overlay_string_free);
  g_array_set_clear_func (overlay->simages,
      (GDestroyNotify) gst_overlay_image_free);

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (overlay), TRUE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivoverlay", GST_RANK_NONE,
      GST_TYPE_OVERLAY);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivoverlay,
    "QTI video overlay plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
