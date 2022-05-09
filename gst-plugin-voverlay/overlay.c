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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
  "{ NV12, NV21, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR }"

#define GST_OVERLAY_SRC_CAPS                            \
    "video/x-raw, "                                     \
    "format = (string) " GST_OVERLAY_VIDEO_FORMATS "; " \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "    \
    "format = (string) " GST_OVERLAY_VIDEO_FORMATS

#define GST_OVERLAY_SINK_CAPS                           \
    "video/x-raw, "                                     \
    "format = (string) " GST_OVERLAY_VIDEO_FORMATS "; " \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "    \
    "format = (string) " GST_OVERLAY_VIDEO_FORMATS

#define DEFAULT_MIN_BUFFERS      1
#define DEFAULT_MAX_BUFFERS      30

#define MAX_TEXT_LENGTH          25.0F

#define EXTRACT_RED_COLOR(color)   (((color >> 24) & 0xFF) / 255.0)
#define EXTRACT_GREEN_COLOR(color) (((color >> 16) & 0xFF) / 255.0)
#define EXTRACT_BLUE_COLOR(color)  (((color >> 8) & 0xFF) / 255.0)
#define EXTRACT_ALPHA_COLOR(color) (((color) & 0xFF) / 255.0)

enum
{
  PROP_0,
  PROP_BBOXES,
  PROP_TIMESTAMPS,
  PROP_STRINGS,
  PROP_PRIVACY_MASKS,
  PROP_STATIC_IMAGES,
};

static GstStaticCaps gst_overlay_static_sink_caps =
    GST_STATIC_CAPS (GST_OVERLAY_SINK_CAPS);

static GstStaticCaps gst_overlay_static_src_caps =
    GST_STATIC_CAPS (GST_OVERLAY_SRC_CAPS);


static GstCaps *
gst_overlay_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_overlay_static_sink_caps);
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
    caps = gst_static_caps_get (&gst_overlay_static_src_caps);
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


static gboolean
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

static gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
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
  cairo_set_source_rgba (context, EXTRACT_RED_COLOR (color),
      EXTRACT_GREEN_COLOR (color), EXTRACT_BLUE_COLOR (color),
      EXTRACT_ALPHA_COLOR (color));

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
  cairo_set_source_rgba (context, EXTRACT_RED_COLOR (color),
      EXTRACT_GREEN_COLOR (color), EXTRACT_BLUE_COLOR (color),
      EXTRACT_ALPHA_COLOR (color));

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
  cairo_set_source_rgba (context, EXTRACT_RED_COLOR (color),
      EXTRACT_GREEN_COLOR (color), EXTRACT_BLUE_COLOR (color),
      EXTRACT_ALPHA_COLOR (color));

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
  cairo_set_source_rgba (context, EXTRACT_RED_COLOR (color),
      EXTRACT_GREEN_COLOR (color), EXTRACT_BLUE_COLOR (color),
      EXTRACT_ALPHA_COLOR (color));

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
  cairo_set_source_rgba (context, EXTRACT_RED_COLOR (color),
      EXTRACT_GREEN_COLOR (color), EXTRACT_BLUE_COLOR (color),
      EXTRACT_ALPHA_COLOR (color));
  cairo_fill (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

static inline void
gst_overlay_release_frame (GstVideoFrame * frame)
{
  GstBuffer *buffer = frame->buffer;

  gst_video_frame_unmap (frame);
  gst_buffer_unref (buffer);
}

static inline void
gst_overlay_release_frames (GstVideoFrame * frames, guint n_frames)
{
  guint idx = 0;

  for (idx = 0; idx < n_frames; idx++)
    gst_overlay_release_frame (&frames[idx]);

  g_free (frames);
}

static gboolean
gst_overlay_acquire_frame (GstVOverlay * overlay, guint type,
    GstVideoFrame * frame)
{
  GstBufferPool *pool = NULL;
  GstVideoInfo *info = NULL;
  GstBuffer *buffer = NULL;

  pool = overlay->ovlpools[type];
  info = overlay->ovlinfos[type];

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (overlay, "Failed to activate overlay buffer pool!");
    return FALSE;
  }

  if (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (overlay, "Failed to acquire overlay buffer!");
    return FALSE;
  }

  if (!gst_video_frame_map (frame, info, buffer,
          GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (overlay, "Failed to map overlay buffer!");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  return TRUE;
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

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    GST_INFO_OBJECT (overlay, "Uses GBM memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
  } else {
    GST_INFO_OBJECT (overlay, "Uses ION memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (gst_caps_has_compression (caps, "ubwc")) {
    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_UBWC_MODE);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (overlay, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  g_object_unref (allocator);
  return pool;
}

static void
gst_overlay_update_converter_params (GstVOverlay * overlay, guint idx,
    GstVideoRectangle * source, GstVideoRectangle * destination)
{
  GstStructure *opts = gst_structure_new_empty ("options");
  GValue rects = G_VALUE_INIT, entry = G_VALUE_INIT, value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_value_init (&entry, GST_TYPE_ARRAY);
  g_value_init (&rects, GST_TYPE_ARRAY);

  g_value_set_int (&value, source->x);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, source->y);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, source->w);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, source->h);
  gst_value_array_append_value (&entry, &value);

  gst_value_array_append_value (&rects, &entry);
  g_value_reset (&entry);

#ifdef USE_C2D_CONVERTER
  gst_structure_set_value (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &rects);
  g_value_reset (&rects);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  gst_structure_set_value (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &rects);
  g_value_reset (&rects);
#endif // USE_GLES_CONVERTER

  g_value_set_int (&value, destination->x);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, destination->y);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, destination->w);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, destination->h);
  gst_value_array_append_value (&entry, &value);
  g_value_unset (&value);

  gst_value_array_append_value (&rects, &entry);
  g_value_unset (&entry);

  GST_TRACE_OBJECT (overlay, "Source/Destination Rectangles[%u]: [%d %d %d %d]"
      " -> [%d %d %d %d]", idx, source->x, source->y, source->w, source->h,
      destination->x, destination->y, destination->w, destination->h);

#ifdef USE_C2D_CONVERTER
  gst_structure_set_value (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &rects);
  g_value_unset (&rects);

  gst_c2d_video_converter_set_input_opts (overlay->c2dconvert, idx, opts);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  gst_structure_set_value (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &rects);
  g_value_unset (&rects);

  gst_gles_video_converter_set_input_opts (overlay->glesconvert, idx, opts);
#endif // USE_GLES_CONVERTER
}

static void
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

static gboolean
gst_overlay_handle_detection_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstVideoRegionOfInterestMeta * roimeta)
{
  GstStructure *param = NULL;
  gchar *label = NULL;
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gdouble confidence = 0.0, scale = 0.0, linewidth = 0.0;
  gdouble x = 0.0, y = 0.0, fontsize = 0.0;
  guint color = 0x000000FF;
  gboolean success = FALSE;

  // Extract the structure containing ROI parameters.
  param = gst_video_region_of_interest_meta_get_param (roimeta,
      g_quark_to_string (roimeta->roi_type));

  gst_structure_get_double (param, "confidence", &confidence);
  gst_structure_get_uint (param, "color", &color);

  destbox.x = roimeta->x;
  destbox.y = roimeta->y;

  srcbox.w = destbox.w = roimeta->w;
  srcbox.h = destbox.h = roimeta->h;

  // Adjust bbox dimensions so that it fits inside the overlay frame.
  gst_overlay_update_rectangle_dimensions (overlay, vframe, &srcbox);
  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  // Set the most appropriate box line width based on frame and box dimensions.
  gst_util_fraction_to_double (srcbox.w, destbox.w, &scale);
  linewidth = (scale > 1.0F) ? (2.0F / scale) : 2.0F;

  label = g_strdup_printf ("%s: %.1f%%",
      gst_structure_get_string (param, "label"), confidence);

  // Remove white space delimeter.
  label = g_strdelimit (label, "-", ' ');

  // Set the most appropriate font size based on the bounding box dimensions.
  fontsize = MIN (((srcbox.w / MAX_TEXT_LENGTH) * 9.0 / 5.0), 16.0F);

  x = y = linewidth;

  GST_TRACE_OBJECT (overlay, "Label: %s, Color: 0x%X, Position: [%.2f %.2f]",
      label, color, x, y);

  success = gst_cairo_draw_text (context, color, x, y, label, fontsize);
  g_free (label);

  GST_TRACE_OBJECT (overlay, "Rectangle: [%d %d %d %d], Color: 0x%X",
      srcbox.x, srcbox.y, srcbox.w, srcbox.h, color);

  success &= gst_cairo_draw_rectangle (context, color, srcbox.x, srcbox.y,
      srcbox.w, srcbox.h, linewidth, FALSE);

  return success;
}

static gboolean
gst_overlay_handle_classification_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstVideoRegionOfInterestMeta * roimeta)
{
  GstStructure *param = NULL;
  gchar *label = NULL;
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gdouble confidence = 0.0, x = 0.0, y = 0.0, fontsize = 0.0;
  guint color = 0x000000FF;
  gboolean success = FALSE;

  // Extract the structure containing ROI parameters.
  param = gst_video_region_of_interest_meta_get_param (roimeta,
      g_quark_to_string (roimeta->roi_type));

  gst_structure_get_double (param, "confidence", &confidence);
  gst_structure_get_uint (param, "color", &color);

  srcbox.w = destbox.w = GST_VIDEO_FRAME_WIDTH (vframe);
  srcbox.h = destbox.h = GST_VIDEO_FRAME_HEIGHT (vframe);

  destbox.x = 0;
  destbox.y = roimeta->id * GST_VIDEO_FRAME_HEIGHT (vframe);

  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  label = g_strdup_printf ("%s: %.1f%%",
      gst_structure_get_string (param, "label"), confidence);

  // Remove white space delimeter.
  label = g_strdelimit (label, "-", ' ');

  // Set the most appropriate font size based on the overlay frame dimensions.
  fontsize = (GST_VIDEO_FRAME_WIDTH (vframe) / MAX_TEXT_LENGTH) * 9.0 / 5.0;

  if ((GST_VIDEO_FRAME_HEIGHT (vframe) / fontsize) < 1.0)
    fontsize = GST_VIDEO_FRAME_HEIGHT (vframe);

  GST_TRACE_OBJECT (overlay, "Label: %s, Color: 0x%X, Position: [%.2f %.2f]",
      label, color, x, y);

  success = gst_cairo_draw_text (context, color, x, y, label, fontsize);
  g_free (label);

  return success;
}

static gboolean
gst_overlay_handle_pose_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstVideoRegionOfInterestMeta * roimeta)
{
  GstStructure *param = NULL, *keypoint = NULL;
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  GValue connections = G_VALUE_INIT;
  gdouble score = 0.0, confidence = 0.0, x = 0.0, y = 0.0;
  guint num = 0, size = 0, color = 0x000000FF;
  gboolean success = TRUE;

  // Extract the structure containing ROI parameters.
  param = gst_video_region_of_interest_meta_get_param (roimeta,
      g_quark_to_string (roimeta->roi_type));

  g_value_init (&connections, GST_TYPE_ARRAY);

  // First extract and remoeve the 'connections' field.
  g_value_copy (gst_structure_get_value (param, "connections"), &connections);
  gst_structure_remove_field (param, "connections");

  // Next extract and remove the overall pose confidence score.
  gst_structure_get_double (param, "confidence", &score);
  gst_structure_remove_field (param, "confidence");

  // Get the number of remaining fields, this the number of keypoints.
  size = gst_structure_n_fields (param);

  for (num = 0; num < size; num++) {
    const gchar *name = gst_structure_nth_field_name (param, num);

    keypoint = GST_STRUCTURE (
        g_value_get_boxed (gst_structure_get_value (param, name)));

    gst_structure_get_double (keypoint, "x", &x);
    gst_structure_get_double (keypoint, "y", &y);
    gst_structure_get_double (keypoint, "confidence", &confidence);
    gst_structure_get_uint (keypoint, "color", &color);

    // Translate relative X & Y axis coordinates to absolute.
    x *= GST_VIDEO_FRAME_WIDTH (vframe);
    y *= GST_VIDEO_FRAME_HEIGHT (vframe);

    GST_TRACE_OBJECT (overlay, "Keypoint: %s, Position: [%.2f %.2f], "
        "Confidence: %.2f, Color: 0x%X", name, x, y, confidence, color);

    success &= gst_cairo_draw_circle (context, color, x, y, 2.0, 1.0, TRUE);
  }

  // Get the number of keypoint connections.
  size = gst_value_array_get_size (&connections);

  for (num = 0; num < size; num++) {
    const GValue *connection = gst_value_array_get_value (&connections, num);
    const gchar *s_name = NULL, *d_name = NULL;
    gdouble x = 0.0, y = 0.0, dx = 0.0, dy = 0.0;

    // Extract the names of the source and destination keypoints.
    s_name = g_value_get_string (gst_value_array_get_value (connection, 0));
    d_name = g_value_get_string (gst_value_array_get_value (connection, 1));

    keypoint = GST_STRUCTURE (
        g_value_get_boxed (gst_structure_get_value (param, s_name)));

    gst_structure_get_double (keypoint, "x", &x);
    gst_structure_get_double (keypoint, "y", &y);

    x *= GST_VIDEO_FRAME_WIDTH (vframe);
    y *= GST_VIDEO_FRAME_HEIGHT (vframe);

    keypoint = GST_STRUCTURE (
        g_value_get_boxed (gst_structure_get_value (param, d_name)));

    gst_structure_get_double (keypoint, "x", &dx);
    gst_structure_get_double (keypoint, "y", &dy);

    dx *= GST_VIDEO_FRAME_WIDTH (vframe);
    dy *= GST_VIDEO_FRAME_HEIGHT (vframe);

    GST_TRACE_OBJECT (overlay, "Link: %s [%.2f %.2f] <---> %s [%.2f %.2f]",
        s_name, x, y, d_name, dx, dy);

    success &= gst_cairo_draw_line (context, color, x, y, dx, dy, 1.0);
  }

  g_value_unset (&connections);

  srcbox.w = GST_VIDEO_FRAME_WIDTH (vframe);
  srcbox.h = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  return success;
}

static gboolean
gst_overlay_handle_optclflow_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstCvpOptclFlowMeta * cvpmeta)
{
  GstCvpMotionVector *mvector = NULL;
  GstCvpOptclFlowStats *stats = NULL;
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  guint num = 0, color = 0xFFFFFFFF;
  gdouble x = 0.0, y = 0.0, dx = 0.0, dy = 0.0, xscale = 0.0, yscale = 0.0;

  srcbox.w = GST_VIDEO_FRAME_WIDTH (vframe);
  srcbox.h = GST_VIDEO_FRAME_HEIGHT (vframe);

  gst_util_fraction_to_double (GST_VIDEO_INFO_WIDTH (overlay->vinfo),
      GST_VIDEO_FRAME_WIDTH (vframe), &xscale);
  gst_util_fraction_to_double (GST_VIDEO_INFO_HEIGHT (overlay->vinfo),
      GST_VIDEO_FRAME_HEIGHT (vframe), &yscale);

  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  // Read every 6th 4x16 motion vector paxel due arrows density.
  for (num = 0; num < cvpmeta->mvectors->len; num += 6) {
    mvector = &g_array_index (cvpmeta->mvectors, GstCvpMotionVector, num);

    if ((mvector->dx == 0) && (mvector->dy == 0))
      continue;

    if ((cvpmeta->stats != NULL) && (cvpmeta->stats->len != 0))
      stats = &g_array_index (cvpmeta->stats, GstCvpOptclFlowStats, num);

    if ((stats != NULL) && (stats->sad == 0) && (stats->variance == 0))
      continue;

    x = (mvector->x / xscale) + mvector->dx;
    y = (mvector->y / yscale) + mvector->dy;

    dx = (-1.0F) * mvector->dx;
    dy = (-1.0F) * mvector->dy;

    gst_cairo_draw_arrow (context, color, x, y, dx, dy, 1.0);
  }

  return TRUE;
}

static gboolean
gst_overlay_handle_bbox_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstOverlayBBox * bbox)
{
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gdouble scale = 0.0, linewidth = 0.0;
  guint color = 0;

  destbox.x = bbox->destination.x;
  destbox.y = bbox->destination.y;

  srcbox.w = destbox.w = bbox->destination.w;
  srcbox.h = destbox.h = bbox->destination.h;

  color = bbox->color;

  // Adjust bbox dimensions so that it fits inside the overlay frame.
  gst_overlay_update_rectangle_dimensions (overlay, vframe, &srcbox);
  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  // Set the most appropriate box line width based on frame and box dimensions.
  gst_util_fraction_to_double (srcbox.w, destbox.w, &scale);
  linewidth = (scale > 1.0F) ? (6.0F / scale) : 6.0F;

  GST_TRACE_OBJECT (overlay, "Rectangle: [%d %d %d %d], Color: 0x%X",
      srcbox.x, srcbox.y, srcbox.w, srcbox.h, color);

  return gst_cairo_draw_rectangle (context, color, srcbox.x, srcbox.y,
      srcbox.w, srcbox.h, linewidth, FALSE);
}

static gboolean
gst_overlay_handle_timestamp_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstOverlayTimestamp * timestamp)
{
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gchar *text = NULL;
  gdouble fontsize = 0.0, n_chars = 0.0, scale = 0.0;
  guint color = 0;
  gboolean success = FALSE;

  destbox.x = timestamp->position.x;
  destbox.y = timestamp->position.y;

  destbox.w = srcbox.w = GST_VIDEO_FRAME_WIDTH (vframe);
  destbox.h = srcbox.h = GST_VIDEO_FRAME_HEIGHT (vframe);

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
      GstClockTime time = GST_CLOCK_TIME_NONE;
      guint hours = 0, mins = 0, secs = 0, usecs = 0;

      time = GST_BUFFER_DTS_IS_VALID (vframe->buffer) ?
          GST_BUFFER_DTS (vframe->buffer) : GST_BUFFER_PTS (vframe->buffer);

      hours = (guint) (time / (GST_SECOND * 60 * 60));
      mins = (guint) ((time / (GST_SECOND * 60)) % 60);
      secs = (guint) ((time / GST_SECOND) % 60);
      usecs = (guint) ((time % GST_SECOND) / 1000);

      text = g_strdup_printf ("%u:%02u:%02u.%06u", hours, mins, secs, usecs);
      break;
    }
    default:
      GST_ERROR_OBJECT (overlay, "Unknown timestamp type %d!", timestamp->type);
      return FALSE;
  }

  n_chars = strlen (text);

  // Limit the fontsize if it is not possible to put the text in the buffer.
  fontsize =
      MIN ((GST_VIDEO_FRAME_WIDTH (vframe) / n_chars) * 9.0 / 5.0, fontsize);

  if ((GST_VIDEO_FRAME_HEIGHT (vframe) / fontsize) < 1.0)
    fontsize = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Calculate the scale factor, will be use to update destination rectangle.
  scale = timestamp->fontsize / fontsize;

  // Scale destination rectangle dimensions in order to match the set fontsize.
  destbox.w *= (scale > 1.0) ? scale : 1;
  destbox.h *= (scale > 1.0) ? scale : 1;

  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  GST_TRACE_OBJECT (overlay, "String: '%s', Color: 0x%X, Position: [%d %d]",
      text, timestamp->color, timestamp->position.x, timestamp->position.y);

  success = gst_cairo_draw_text (context, color, 0.0, 0.0, text, fontsize);
  g_free (text);

  return success;
}

static gboolean
gst_overlay_handle_string_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstOverlayString * string)
{
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gchar *text = NULL;
  gdouble fontsize = 0.0, n_chars = 0.0, scale = 0.0;
  guint color = 0;

  destbox.x = string->position.x;
  destbox.y = string->position.y;

  destbox.w = srcbox.w = GST_VIDEO_FRAME_WIDTH (vframe);
  destbox.h = srcbox.h = GST_VIDEO_FRAME_HEIGHT (vframe);

  fontsize = string->fontsize;
  color = string->color;

  text = string->contents;
  n_chars = strlen (text);

  // Limit the fontsize if it is not possible to put the text in the buffer.
  fontsize =
      MIN ((GST_VIDEO_FRAME_WIDTH (vframe) / n_chars) * 9.0 / 5.0, fontsize);

  if ((GST_VIDEO_FRAME_HEIGHT (vframe) / fontsize) < 1.0)
    fontsize = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Calculate the scale factor, will be use to update destination rectangle.
  scale = string->fontsize / fontsize;

  // Scale destination rectangle dimensions in order to match the set fontsize.
  destbox.w *= (scale > 1.0) ? scale : 1;
  destbox.h *= (scale > 1.0) ? scale : 1;

  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  GST_TRACE_OBJECT (overlay, "String: '%s', Color: 0x%X, Position: [%d %d]",
      string->contents, string->color, string->position.x,
      string->position.y);

  return gst_cairo_draw_text (context, color, 0.0, 0.0, text, fontsize);
}

static gboolean
gst_overlay_handle_mask_entry (GstVOverlay * overlay, cairo_t * context,
    guint idx, GstVideoFrame * vframe, GstOverlayMask * mask)
{
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gdouble x = 0.0, y = 0.0, linewidth = 0.0, scale = 0.0;
  guint color = 0;
  gboolean success = FALSE;

  switch (mask->type) {
    case GST_OVERLAY_MASK_RECTANGLE:
      srcbox.w = destbox.w = mask->dims.wh[0];
      srcbox.h = destbox.h = mask->dims.wh[1];

      destbox.x = mask->position.x;
      destbox.y = mask->position.y;
      break;
    case GST_OVERLAY_MASK_CIRCLE:
      srcbox.w = destbox.w = mask->dims.radius * 2;
      srcbox.h = destbox.h = mask->dims.radius * 2;

      destbox.x = mask->position.x - mask->dims.radius;
      destbox.y = mask->position.y - mask->dims.radius;
      break;
    default:
      GST_ERROR_OBJECT (overlay, "Unknown privacy mask type %d!", mask->type);
      return FALSE;
  }

  color = mask->color;

  // Adjust mask srcbox dimensions so that it fits inside the overlay frame.
  gst_overlay_update_rectangle_dimensions (overlay, vframe, &srcbox);
  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  // Set the most appropriate box line width based on frame and box dimensions.
  gst_util_fraction_to_double (srcbox.w, destbox.w, &scale);
  linewidth = (scale > 1.0F) ? (6.0F / scale) : 6.0F;

  if (GST_OVERLAY_MASK_RECTANGLE == mask->type) {
    gdouble width = 0.0, height = 0.0;

    width = srcbox.w;
    height = srcbox.h;

    GST_TRACE_OBJECT (overlay, "Rectangle: [%.2f %.2f %.2f %.2f], Color: 0x%X",
        x, y, width, height, color);

    success = gst_cairo_draw_rectangle (context, color, x, y, width, height,
        linewidth, TRUE);
  } else if (GST_OVERLAY_MASK_CIRCLE == mask->type) {
    gdouble radius = 0.0;

    radius = srcbox.w / 2.0;
    x = y = radius;

    GST_TRACE_OBJECT (overlay, "Circle: [%.2f %.2f %.2f], Color: 0x%X", x, y,
        radius, color);

    success = gst_cairo_draw_circle (context, color, x, y, radius, linewidth,
        TRUE);
  }

  return success;
}

static gboolean
gst_overlay_handle_image_entry (GstVOverlay * overlay,
    guint idx, GstVideoFrame * vframe, GstOverlayImage * simage)
{
  GError *error = NULL;
  gchar *data = NULL;
  GstVideoRectangle srcbox = { 0 }, destbox = { 0 };
  gint x = 0, num = 0, id = 0;

  srcbox.w = simage->width;
  srcbox.h = simage->height;

  destbox = simage->destination;

  // Update the converter source and destination rectangle for current entry.
  gst_overlay_update_converter_params (overlay, idx, &srcbox, &destbox);

  // Load static image file contents in case it was not already loaded.
  if (simage->contents == NULL) {
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

    if (!g_file_get_contents (simage->path, &(simage)->contents, NULL, &error)) {
      GST_WARNING_OBJECT (overlay, "Failed to laod static image file '%s', "
          "error: %s!", simage->path, GST_STR_NULL (error->message));

      g_clear_error (&error);
      return FALSE;
    }
  }

  data = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  for (x = 0; x < simage->height; x++, num += (simage->width * 4)) {
    id = x * GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0);
    memcpy (&data[id], &(simage)->contents[num], (simage->width * 4));
  }

  return TRUE;
}

static gboolean
gst_overlay_fill_overlay_frame (GstVOverlay * overlay, guint idx,
    GstVideoFrame * vframe, guint ovltype, gpointer ovldata)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  cairo_format_t format;
  gboolean success = FALSE;

  GST_TRACE_OBJECT (overlay, "Processing overlay frame at idx[%u]", idx);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (vframe->buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (vframe->buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (overlay, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  switch (GST_VIDEO_FRAME_FORMAT (vframe)) {
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
      GST_ERROR_OBJECT (overlay, "Unsupported format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (vframe)));

      gst_overlay_release_frame (vframe);
      return FALSE;
  }

  surface = cairo_image_surface_create_for_data (
      GST_VIDEO_FRAME_PLANE_DATA (vframe, 0), format,
      GST_VIDEO_FRAME_WIDTH (vframe), GST_VIDEO_FRAME_HEIGHT (vframe),
      GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0));
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

  if (GST_OVERLAY_TYPE_DETECTION == ovltype)
    success = gst_overlay_handle_detection_entry (overlay, context, idx,
        vframe, GST_VIDEO_ROI_META_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_CLASSIFICATION == ovltype)
    success = gst_overlay_handle_classification_entry (overlay, context, idx,
        vframe, GST_VIDEO_ROI_META_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_POSE_ESTIMATION == ovltype)
    success = gst_overlay_handle_pose_entry (overlay, context, idx,
        vframe, GST_VIDEO_ROI_META_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_OPTCLFLOW == ovltype)
    success = gst_overlay_handle_optclflow_entry (overlay, context, idx,
        vframe, GST_CVP_OPTCLFLOW_META_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_BBOX == ovltype)
    success = gst_overlay_handle_bbox_entry (overlay, context, idx, vframe,
        GST_OVERLAY_BBOX_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_TIMESTAMP == ovltype)
    success = gst_overlay_handle_timestamp_entry (overlay, context, idx, vframe,
        GST_OVERLAY_TIMESTAMP_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_STRING == ovltype)
    success = gst_overlay_handle_string_entry (overlay, context, idx, vframe,
        GST_OVERLAY_STRING_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_IMAGE == ovltype)
    success = gst_overlay_handle_image_entry (overlay, idx, vframe,
        GST_OVERLAY_IMAGE_CAST (ovldata));
  else if (GST_OVERLAY_TYPE_MASK == ovltype)
    success = gst_overlay_handle_mask_entry (overlay, context, idx, vframe,
        GST_OVERLAY_MASK_CAST (ovldata));
  else
    GST_ERROR_OBJECT (overlay, "Unhandled overlay type: %u!", ovltype);

  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (surface);

  cairo_destroy (context);
  cairo_surface_destroy (surface);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (vframe->buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (vframe->buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (overlay, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  return success;
}

static gboolean
gst_overlay_draw_overlays (GstVOverlay * overlay, GstBuffer * buffer,
    GstVideoFrame ** ovlframes, guint * n_overlays)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;
  GstVideoFrame *frames = NULL;
  guint idx = 0, num = 0, n_entries = 0, length = 0;
  gboolean success = FALSE;

  // Add the total number of meta entries that needs to be processed.
  n_entries += gst_buffer_get_n_meta (buffer,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
  n_entries += gst_buffer_get_n_meta (buffer, GST_CVP_OPTCLFLOW_META_API_TYPE);

  // Add the number of manually set bounding boxes.
  n_entries += (overlay->bboxes != NULL) ? overlay->bboxes->len : 0;
  // Add the number of manually set timestamps.
  n_entries += (overlay->timestamps != NULL) ? overlay->timestamps->len : 0;
  // Add the number of manually set strings.
  n_entries += (overlay->strings != NULL) ? overlay->strings->len : 0;
  // Add the number of manually set privacy masks.
  n_entries += (overlay->masks != NULL) ? overlay->masks->len : 0;
  // Add the number of manually set static images.
  n_entries += (overlay->simages != NULL) ? overlay->simages->len : 0;

  // Allocate video frame structure for each of the entries.
  frames = g_new (GstVideoFrame, n_entries);

  // Iterate over the buffer meta and process the supported entries.
  while ((meta = gst_buffer_iterate_meta (buffer, &state)) != NULL) {
    guint ovltype = gst_meta_overlay_type (meta);

    // Skip meta entries that are not among the supported overlay types.
    if (GST_OVERLAY_TYPE_MAX == ovltype)
      continue;

    if (!gst_overlay_acquire_frame (overlay, ovltype, &frames[num])) {
      GST_ERROR_OBJECT (overlay, "Failed to acquire overlay frame!");
      gst_overlay_release_frames (frames, num);
      return FALSE;
    }

    // Copy the timestamps from the main buffer.
    gst_buffer_copy_into (frames[num].buffer, buffer,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    success = gst_overlay_fill_overlay_frame (overlay, num, &frames[num],
        ovltype, GST_GPOINTER_CAST (meta));

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process meta %u!", num);
      gst_overlay_release_frames (frames, num + 1);
      return FALSE;
    }

    num++;
  }

  length = (overlay->bboxes != NULL) ? overlay->bboxes->len : 0;

  // Process manually set bounding boxes.
  for (idx = 0; idx < length; idx++, idx++) {
    GstOverlayBBox *bbox = &g_array_index (overlay->bboxes, GstOverlayBBox, idx);

    success = gst_overlay_acquire_frame (overlay, GST_OVERLAY_TYPE_BBOX,
        &frames[num]);

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to acquire overlay frame!");
      gst_overlay_release_frames (frames, num);
      return FALSE;
    }

    // Copy the timestamps from the main buffer.
    gst_buffer_copy_into (frames[num].buffer, buffer,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    success = gst_overlay_fill_overlay_frame (overlay, num, &frames[num],
        GST_OVERLAY_TYPE_BBOX, GST_GPOINTER_CAST (bbox));

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process bounding box %u!", idx);
      gst_overlay_release_frames (frames, num + 1);
      return FALSE;
    }
  }

  length = (overlay->timestamps != NULL) ? overlay->timestamps->len : 0;

  // Process manually set timestamps.
  for (idx = 0; idx < length; idx++, num++) {
    GstOverlayTimestamp *timestamp =
        &g_array_index (overlay->timestamps, GstOverlayTimestamp, idx);

    success = gst_overlay_acquire_frame (overlay, GST_OVERLAY_TYPE_TIMESTAMP,
        &frames[num]);

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to acquire overlay frame!");
      gst_overlay_release_frames (frames, num);
      return FALSE;
    }

    // Copy the timestamps from the main buffer.
    gst_buffer_copy_into (frames[num].buffer, buffer,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    success = gst_overlay_fill_overlay_frame (overlay, num, &frames[num],
        GST_OVERLAY_TYPE_TIMESTAMP, GST_GPOINTER_CAST (timestamp));

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process timestamp %u!", idx);
      gst_overlay_release_frames (frames, num + 1);
      return FALSE;
    }
  }

  length = (overlay->strings != NULL) ? overlay->strings->len : 0;

  // Process manually set strings.
  for (idx = 0; idx < length; idx++, num++) {
    GstOverlayString *string =
        &g_array_index (overlay->strings, GstOverlayString, idx);

    success = gst_overlay_acquire_frame (overlay, GST_OVERLAY_TYPE_STRING,
        &frames[num]);

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to acquire overlay frame!");
      gst_overlay_release_frames (frames, num);
      return FALSE;
    }

    // Copy the timestamps from the main buffer.
    gst_buffer_copy_into (frames[num].buffer, buffer,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    success = gst_overlay_fill_overlay_frame (overlay, num, &frames[num],
        GST_OVERLAY_TYPE_STRING, GST_GPOINTER_CAST (string));

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process string %u!", idx);
      gst_overlay_release_frames (frames, num + 1);
      return FALSE;
    }
  }

  length = (overlay->masks != NULL) ? overlay->masks->len : 0;

  // Process manually set privacy masks.
  for (idx = 0; idx < length; idx++, num++) {
    GstOverlayMask *mask = &g_array_index (overlay->masks, GstOverlayMask, idx);

    success = gst_overlay_acquire_frame (overlay, GST_OVERLAY_TYPE_MASK,
        &frames[num]);

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to acquire overlay frame!");
      gst_overlay_release_frames (frames, num);
      return FALSE;
    }

    // Copy the timestamps from the main buffer.
    gst_buffer_copy_into (frames[num].buffer, buffer,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    success = gst_overlay_fill_overlay_frame (overlay, num, &frames[num],
        GST_OVERLAY_TYPE_MASK, GST_GPOINTER_CAST (mask));

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process privacy mask %u!", idx);
      gst_overlay_release_frames (frames, num + 1);
      return FALSE;
    }
  }

  length = (overlay->simages != NULL) ? overlay->simages->len : 0;

  // Process manually set static images.
  for (idx = 0; idx < length; idx++, num++) {
    GstOverlayImage *simage =
        &g_array_index (overlay->simages, GstOverlayImage, idx);

    success = gst_overlay_acquire_frame (overlay, GST_OVERLAY_TYPE_IMAGE,
        &frames[num]);

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to acquire overlay frame!");
      gst_overlay_release_frames (frames, num);
      return FALSE;
    }

    // Copy the timestamps from the main buffer.
    gst_buffer_copy_into (frames[num].buffer, buffer,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    success = gst_overlay_fill_overlay_frame (overlay, num, &frames[num],
        GST_OVERLAY_TYPE_IMAGE, GST_GPOINTER_CAST (simage));

    if (!success) {
      GST_ERROR_OBJECT (overlay, "Failed to process static image %u!", idx);
      gst_overlay_release_frames (frames, num + 1);
      return FALSE;
    }
  }

  *n_overlays = n_entries;
  *ovlframes = frames;

  return TRUE;
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
  GstStructure *opts = NULL;
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
      // Square resolution (128 to 256 pixels) is most optimal.
      width = height = GST_ROUND_UP_128 (MIN (MAX (width, height) / 10, 256));
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
      // For classification overlay resolution with aspect ratio 12:1 is optimal.
      width = GST_ROUND_UP_128 (MAX (width / 6, 256));
      height = GST_ROUND_UP_4 (width / 16);
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

  // Fill the converter output options structure.
#ifdef USE_C2D_CONVERTER
  opts = gst_structure_new ("options",
      GST_C2D_VIDEO_CONVERTER_OPT_BACKGROUND, G_TYPE_UINT, 0x00000000,
      GST_C2D_VIDEO_CONVERTER_OPT_CLEAR, G_TYPE_BOOLEAN, FALSE,
      GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          gst_caps_has_compression (outcaps, "ubwc"),
      NULL);
  gst_c2d_video_converter_set_output_opts (overlay->c2dconvert, 0, opts);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  opts = gst_structure_new ("options",
      GST_GLES_VIDEO_CONVERTER_OPT_BACKGROUND, G_TYPE_UINT, 0x00000000,
      GST_GLES_VIDEO_CONVERTER_OPT_CLEAR, G_TYPE_BOOLEAN, FALSE,
      GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          gst_caps_has_compression (outcaps, "ubwc"),
      NULL);
  gst_gles_video_converter_set_output_opts (overlay->glesconvert, 0, opts);
#endif // USE_GLES_CONVERTER

  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_in_place (base, TRUE);

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
  GstVideoFrame *ovlframes = NULL, vframe = { 0 };
  GstClockTime time = GST_CLOCK_TIME_NONE;
  guint n_overlays = 0;
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

  // Extract metadata entries from the buffer and create overlay frames.
  success = gst_overlay_draw_overlays (overlay, buffer, &ovlframes, &n_overlays);

  if (!success) {
    GST_ERROR_OBJECT (overlay, "Failed to draw overlay frames!");
    return GST_FLOW_ERROR;
  }

  // Check if there is need for applying any overlay frames.
  if ((ovlframes == NULL) || (n_overlays == 0)) {
    gst_overlay_release_frames (ovlframes, n_overlays);
    return GST_FLOW_OK;
  }

  if (!gst_video_frame_map (&vframe, overlay->vinfo, buffer,
          GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (overlay, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

#ifdef USE_C2D_CONVERTER
  {
    gpointer request_id = NULL;
    GstC2dComposition composition = { ovlframes, n_overlays, &vframe };

    request_id = gst_c2d_video_converter_submit_request (overlay->c2dconvert,
        &composition, 1);
    success = gst_c2d_video_converter_wait_request (overlay->c2dconvert,
        request_id);
  }
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  {
    gpointer request_id = NULL;
    GstGlesComposition composition = { ovlframes, n_overlays, &vframe };

    request_id = gst_gles_video_converter_submit_request (overlay->glesconvert,
        &composition, 1);
    success = gst_gles_video_converter_wait_request (overlay->glesconvert,
        request_id);
  }
#endif // USE_GLES_CONVERTER

  gst_video_frame_unmap (&vframe);
  gst_overlay_release_frames (ovlframes, n_overlays);

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

  switch (prop_id) {
    case PROP_BBOXES:
      if (overlay->bboxes != NULL)
        g_array_free (overlay->bboxes, TRUE);

      if ((overlay->bboxes = gst_extract_bboxes (value)) == NULL) {
        GST_ERROR_OBJECT (overlay, "Failed to exract bboxes!");
        break;
      }
      break;
    case PROP_TIMESTAMPS:
    {
      const gchar *input = NULL;
      GValue list = G_VALUE_INIT;

      g_value_init (&list, GST_TYPE_LIST);
      input = g_value_get_string (value);

      if (!gst_parse_property_value (input, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for timestamps!");
        break;
      }

      if (overlay->timestamps != NULL)
        g_array_free (overlay->timestamps, TRUE);

      if ((overlay->timestamps = gst_extract_timestamps (&list)) == NULL) {
        GST_ERROR_OBJECT (overlay, "Failed to exract timestamps!");
        break;
      }

      g_value_unset (&list);
      break;
    }
    case PROP_STRINGS:
    {
      const gchar *input = NULL;
      GValue list = G_VALUE_INIT;

      g_value_init (&list, GST_TYPE_LIST);
      input = g_value_get_string (value);

      if (!gst_parse_property_value (input, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for strings!");
        break;
      }

      if (overlay->strings != NULL)
        g_array_free (overlay->strings, TRUE);

      if ((overlay->strings = gst_extract_strings (&list)) == NULL) {
        GST_ERROR_OBJECT (overlay, "Failed to exract strings!");
        break;
      }

      g_value_unset (&list);
      break;
    }
    case PROP_PRIVACY_MASKS:
    {
      const gchar *input = NULL;
      GValue list = G_VALUE_INIT;

      g_value_init (&list, GST_TYPE_LIST);
      input = g_value_get_string (value);

      if (!gst_parse_property_value (input, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for masks!");
        break;
      }

      if (overlay->masks != NULL)
        g_array_free (overlay->masks, TRUE);

      if ((overlay->masks = gst_extract_masks (&list)) == NULL) {
        GST_ERROR_OBJECT (overlay, "Failed to exract privacy masks!");
        break;
      }

      g_value_unset (&list);
      break;
    }
    case PROP_STATIC_IMAGES:
    {
      const gchar *input = NULL;
      GValue list = G_VALUE_INIT;

      g_value_init (&list, GST_TYPE_LIST);
      input = g_value_get_string (value);

      if (!gst_parse_property_value (input, &list)) {
        GST_ERROR_OBJECT (overlay, "Failed to parse input for images!");
        break;
      }

      if (overlay->simages != NULL)
        g_array_free (overlay->simages, TRUE);

      if ((overlay->simages = gst_extract_static_images (&list)) == NULL) {
        GST_ERROR_OBJECT (overlay, "Failed to exract static images!");
        break;
      }

      g_value_unset (&list);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVOverlay *overlay = GST_OVERLAY (object);

  switch (prop_id) {
    case PROP_BBOXES:
    {
      GstOverlayBBox *bbox = NULL;
      guint idx = 0, length = 0;

      length = (overlay->bboxes != NULL) ? overlay->bboxes->len : 0;

      for (idx = 0; idx < length; idx++) {
        GValue entry = G_VALUE_INIT, val = G_VALUE_INIT;

        g_value_init (&entry, GST_TYPE_ARRAY);
        g_value_init (&val, G_TYPE_INT);

        bbox = &g_array_index (overlay->bboxes, GstOverlayBBox, idx);

        g_value_set_int (&val, bbox->destination.x);
        gst_value_array_append_value (&entry, &val);

        g_value_set_int (&val, bbox->destination.y);
        gst_value_array_append_value (&entry, &val);

        g_value_set_int (&val, bbox->destination.w);
        gst_value_array_append_value (&entry, &val);

        g_value_set_int (&val, bbox->destination.h);
        gst_value_array_append_value (&entry, &val);

        g_value_set_int (&val, bbox->color);
        gst_value_array_append_value (&entry, &val);

        gst_value_array_append_value (value, &entry);

        g_value_unset (&val);
        g_value_unset (&entry);
      }
      break;
    }
    case PROP_TIMESTAMPS:
    {
      gchar *string = gst_serialize_strings (overlay->timestamps);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PROP_STRINGS:
    {
      gchar *string = gst_serialize_strings (overlay->strings);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PROP_PRIVACY_MASKS:
    {
      gchar *string = gst_serialize_masks (overlay->masks);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PROP_STATIC_IMAGES:
    {
      gchar *string = gst_serialize_static_images (overlay->simages);

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

#ifdef USE_C2D_CONVERTER
  gst_c2d_video_converter_free (overlay->c2dconvert);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  gst_gles_video_converter_free (overlay->glesconvert);
#endif // USE_GLES_CONVERTER

  for (idx = 0; idx < GST_OVERLAY_TYPE_MAX; idx++) {
    if (overlay->ovlpools[idx] != NULL)
      gst_object_unref (overlay->ovlpools[idx]);

    if (overlay->ovlinfos[idx] != NULL)
      gst_video_info_free (overlay->ovlinfos[idx]);
  }

  if (overlay->vinfo != NULL)
    gst_video_info_free (overlay->vinfo);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (overlay));
}

static void
gst_overlay_class_init (GstVOverlayClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_overlay_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_overlay_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_overlay_finalize);

  g_object_class_install_property (gobject, PROP_BBOXES,
      gst_param_spec_array ("bboxes", "BBoxes",
          "Manually set list of bounding boxes with 4 mandatory parameters "
          "X, Y, WIDTH and HEIGHT and a 5th optional parameter for COLOR "
          "(e.g. '<<X, Y, W, H, C>, <X, Y, W, H>, ...>')",
          gst_param_spec_array ("rectangle", "Rectangle", "Rectangle",
              g_param_spec_int ("value", "Rectangle Value",
                  "One of X, Y, WIDTH or HEIGHT value.", G_MININT, G_MAXINT, 0,
                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_TIMESTAMPS,
      g_param_spec_string ("timestamps", "Timestamps",
          "Manually set various timestamps as GstStructures with 'Date/Time' as"
          " keyword for displaying date and/or time with 4 mandatory parameters"
          " 'format', 'fontsize', 'position', and 'color'. And use 'PTS/DTS' "
          "as keyword dispalying buffer timestamp with 3 mandatory parameters "
          "'fontsize', 'position', and 'color' e.g. \"{(structure)\\\"Date/Time"
          ",format=\\\\\\\"%d/%m/%Y\\ %H:%M:%S\\\\\\\",fontsize=12,"
          "position=<0,0>,color=0xRRGGBBAA;\\\", (structure)\\\"PTS/DTS,"
          "fontsize=12,position=<0,0>,color=0xRRGGBBAA;\\\"}\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_STRINGS,
      g_param_spec_string ("strings", "Strings",
          "Manually set multiple custom strings in list of GstStructures with "
          "'Text' as keyword and 4 mandatory parameters 'contents', 'fontsize', "
          "'position', and 'color' e.g. \"{(structure)\\\"Text,contents="
          "\\\\\\\"Example\\ 1\\\\\\\",fontsize=12,position=<0,0>,color="
          "0xRRGGBBAA;\\\"}\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_STATIC_IMAGES,
      g_param_spec_string ("images", "Images",
          "Manually set multiple custom BGRA images in list of GstStructures with "
          "'Image' as keyword and 3 mandatory parameters 'path', 'resolution',"
          " 'destination' e.g. \"{(structure)\\\"Image,path=/data/image1.bgra,"
          "resolution=<480,360>,destination=<0,0,640,480>;\\\", (structure)"
          "\\\"Image,path=/data/image2.bgra,resolution=<240,180>,destination="
          "<100,100,480,360>;\\\"}\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_PRIVACY_MASKS,
      g_param_spec_string ("masks", "Masks",
          "Manually set multiple privacy masks in list of GstStructures with "
          "'Mask' as keyword and 2 mandatory parameters 'color' and either "
          "'circle=<X, Y, RADIUS>' or 'rectangle=<X, Y, WIDTH, HEIGHT>' e.g. "
          "\"{(structure)\\\"Mask,color=0xRRGGBBAA,circle=<400,400,200>;\\\","
          "(structure)\\\"Mask,color=0xRRGGBBAA,rectangle=<0,0,20,10>;\\\"}\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  overlay->latency = 0;
  overlay->vinfo = NULL;

  for (idx = 0; idx < GST_OVERLAY_TYPE_MAX; idx++) {
    overlay->ovlpools[idx] = NULL;
    overlay->ovlinfos[idx] = NULL;
  }

#ifdef USE_C2D_CONVERTER
  overlay->c2dconvert = gst_c2d_video_converter_new ();
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  overlay->glesconvert = gst_gles_video_converter_new ();
#endif // USE_GLES_CONVERTER

  overlay->bboxes = NULL;
  overlay->timestamps = NULL;
  overlay->strings = NULL;
  overlay->simages = NULL;
  overlay->masks = NULL;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (overlay), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_overlay_debug, "qtivoverlay", 0,
      "QTI video overlay plugin");
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
