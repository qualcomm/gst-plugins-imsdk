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

#include "mlvdetection.h"

#include <stdio.h>
#include <stdlib.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/video/gstimagepool.h>
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

#define DEFAULT_PROP_MODULE      0
#define DEFAULT_PROP_LABELS      NULL
#define DEFAULT_PROP_NUM_RESULTS 5
#define DEFAULT_PROP_THRESHOLD   10.0F

#define DEFAULT_MIN_BUFFERS      2
#define DEFAULT_MAX_BUFFERS      10
#define DEFAULT_TEXT_BUFFER_SIZE 4096
#define DEFAULT_VIDEO_WIDTH      320
#define DEFAULT_VIDEO_HEIGHT     240

#define MIN_FONT_SIZE            15.0F
#define MAX_FONT_SIZE            30.0F

#define EXTRACT_RED_COLOR(color)   (((color >> 24) & 0xFF) / 255.0)
#define EXTRACT_GREEN_COLOR(color) (((color >> 16) & 0xFF) / 255.0)
#define EXTRACT_BLUE_COLOR(color)  (((color >> 8) & 0xFF) / 255.0)
#define EXTRACT_ALPHA_COLOR(color) (((color) & 0xFF) / 255.0)

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_LABELS,
  PROP_NUM_RESULTS,
  PROP_THRESHOLD,
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
  static volatile gsize inited = 0;

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
  static volatile gsize inited = 0;

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
gst_ml_prediction_free (GstMLPrediction * prediction)
{
  if (prediction->label != NULL)
    g_free (prediction->label);
}

static gboolean
caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    // Skip ANY caps and return immediately if feature is present.
    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
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
    if (caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
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

    if (NULL == (pool = gst_buffer_pool_new ())) {
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

static gboolean
gst_ml_video_detection_fill_video_output (GstMLVideoDetection * detection,
    GArray * predictions, GstBuffer * buffer)
{
  GstVideoMeta *vmeta = NULL;
  GstMapInfo memmap;
  gdouble x = 0.0, y = 0.0, width = 0.0, height = 0.0;
  gdouble fontsize = 0.0, borderwidth = 0.0;
  guint idx = 0, n_predictions = 0;

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

  for (idx = 0; idx < predictions->len; idx++) {
    GstMLPrediction *prediction = NULL;
    gchar *string = NULL;

    // Break immediately if we reach the number of results limit.
    if (n_predictions >= detection->n_results)
      break;

    prediction = &(g_array_index (predictions, GstMLPrediction, idx));

    // Break immediately if sorted prediction confidence is below the threshold.
    if (prediction->confidence < detection->threshold)
      break;

    // Concat the prediction data to the output string.
    string = g_strdup_printf ("%s: %.1f%%", prediction->label,
        prediction->confidence);

    // Set the bounding box parameters based on the output buffer dimensions.
    x      = ABS (prediction->left) * vmeta->width;
    y      = ABS (prediction->top) * vmeta->height;
    width  = ABS (prediction->right - prediction->left) * vmeta->width;
    height = ABS (prediction->bottom - prediction->top) * vmeta->height;

    // Clip width and height if it outside the frame limits.
    width = ((x + width) > vmeta->width) ? (vmeta->width - x) : width;
    height = ((y + height) > vmeta->height) ? (vmeta->height - y) : height;

    // TODO: Set the most appropriate border size based on the bbox dimensions.
    borderwidth = 3.0;

    // Set the most appropriate font size based on the bounding box dimensions.
    fontsize = (width / 20) * (5.0F / 3.0F);
    fontsize = MIN (fontsize, MAX_FONT_SIZE);
    fontsize = MAX (fontsize, MIN_FONT_SIZE);
    cairo_set_font_size (context, fontsize);

    // Set color.
    cairo_set_source_rgba (context,
        EXTRACT_RED_COLOR (prediction->color),
        EXTRACT_GREEN_COLOR (prediction->color),
        EXTRACT_BLUE_COLOR (prediction->color),
        EXTRACT_ALPHA_COLOR (prediction->color));

    // Set the starting position of the bounding box text.
    cairo_move_to (context, (x + 3), (y + fontsize / 2 + 3));

    // Draw text string.
    cairo_show_text (context, string);
    g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

    GST_TRACE_OBJECT (detection, "label: %s, confidence: %.1f%%, "
        "[%.2f %.2f %.2f %.2f]", prediction->label, prediction->confidence,
        prediction->top, prediction->left, prediction->bottom, prediction->right);

    // Set rectangle borders width.
    cairo_set_line_width (context, borderwidth);

    // Draw rectangle
    cairo_rectangle (context, x, y, width, height);
    cairo_stroke (context);
    g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

    // Flush to ensure all writing to the surface has been done.
    cairo_surface_flush (surface);

    g_free (string);
    n_predictions++;
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
    GArray * predictions, GstBuffer * buffer)
{
  GstMapInfo memmap = {};
  GValue entries = G_VALUE_INIT;
  gchar *string = NULL;
  guint idx = 0, n_predictions = 0;
  gsize length = 0;

  g_value_init (&entries, GST_TYPE_LIST);

  for (idx = 0; idx < predictions->len; idx++) {
    GstMLPrediction *prediction = NULL;
    GstStructure *entry = NULL;
    GValue value = G_VALUE_INIT, rectangle = G_VALUE_INIT;

    // Break immediately if we reach the number of results limit.
    if (n_predictions >= detection->n_results)
      break;

    prediction = &(g_array_index (predictions, GstMLPrediction, idx));

    // Break immediately if sorted prediction confidence is below the threshold.
    if (prediction->confidence < detection->threshold)
      continue;

    GST_TRACE_OBJECT (detection, "label: %s, confidence: %.1f%%, "
        "[%.2f %.2f %.2f %.2f]", prediction->label, prediction->confidence,
        prediction->top, prediction->left, prediction->bottom, prediction->right);

    prediction->label = g_strdelimit (prediction->label, " ", '-');

    entry = gst_structure_new ("ObjectDetection",
        "label", G_TYPE_STRING, prediction->label,
        "confidence", G_TYPE_FLOAT, prediction->confidence,
        "color", G_TYPE_UINT, prediction->color,
        NULL);

    prediction->label = g_strdelimit (prediction->label, "-", ' ');

    g_value_init (&value, G_TYPE_FLOAT);
    g_value_init (&rectangle, GST_TYPE_ARRAY);

    g_value_set_float (&value, prediction->top);
    gst_value_array_append_value (&rectangle, &value);

    g_value_set_float (&value, prediction->left);
    gst_value_array_append_value (&rectangle, &value);

    g_value_set_float (&value, prediction->bottom);
    gst_value_array_append_value (&rectangle, &value);

    g_value_set_float (&value, prediction->right);
    gst_value_array_append_value (&rectangle, &value);

    gst_structure_set_value (entry, "rectangle", &rectangle);
    g_value_unset (&rectangle);

    g_value_unset (&value);
    g_value_init (&value, GST_TYPE_STRUCTURE);

    gst_value_set_structure (&value, entry);
    gst_structure_free (entry);

    gst_value_list_append_value (&entries, &value);
    g_value_unset (&value);

    n_predictions++;
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (detection, "Failed to map buffer memory block!");
    return FALSE;
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&entries);
  g_value_unset (&entries);

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
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (detection, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
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

  GST_DEBUG_OBJECT (detection, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static gboolean
gst_ml_video_detection_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GstCaps *modulecaps = NULL;
  GstStructure *structure = NULL;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstMLInfo ininfo;

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
        ("Module caps do not intersect with the negotiated caps!"));
    return FALSE;
  }

  if (!gst_ml_module_init (detection->module)) {
    GST_ELEMENT_ERROR (detection, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));
    return FALSE;
  }

  structure = gst_structure_new ("options",
      GST_ML_MODULE_OPT_LABELS, G_TYPE_STRING, detection->labels, NULL);

  if (!gst_ml_module_set_opts (detection->module, structure)) {
    GST_ELEMENT_ERROR (detection, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (detection, "Failed to get input ML info from caps %"
        GST_PTR_FORMAT "!", incaps);
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

  gst_base_transform_set_passthrough (base, FALSE);

  GST_DEBUG_OBJECT (detection, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (detection, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_detection_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (base);
  GArray *predictions = NULL;
  GstMLFrame mlframe = { 0, };
  gboolean success = FALSE;

  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  g_return_val_if_fail (detection->module != NULL, GST_FLOW_ERROR);

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Initialize the array which will contain the predictions, must not fail.
  predictions = g_array_new (FALSE, FALSE, sizeof (GstMLPrediction));
  g_return_val_if_fail (predictions != NULL, GST_FLOW_ERROR);

  // Set element clearing function.
  g_array_set_clear_func (predictions, (GDestroyNotify) gst_ml_prediction_free);

  ts_begin = gst_util_get_timestamp ();

  if (!gst_ml_frame_map (&mlframe, detection->mlinfo, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (detection, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  // Call the submodule process funtion.
  success = gst_ml_video_detection_module_execute (detection->module, &mlframe,
      predictions);

  gst_ml_frame_unmap (&mlframe);

  if (!success) {
    GST_ERROR_OBJECT (detection, "Failed to process tensors!");
    g_array_free (predictions, TRUE);
    return GST_FLOW_ERROR;
  }

  if (detection->mode == OUTPUT_MODE_VIDEO)
    success = gst_ml_video_detection_fill_video_output (detection,
        predictions, outbuffer);
  else if (detection->mode == OUTPUT_MODE_TEXT)
    success = gst_ml_video_detection_fill_text_output (detection,
        predictions, outbuffer);
  else
    success = FALSE;

  g_array_free (predictions, TRUE);

  if (!success) {
    GST_ERROR_OBJECT (detection, "Failed to fill output buffer!");
    return GST_FLOW_ERROR;
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (detection, "Object detection took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_detection_finalize (GObject * object)
{
  GstMLVideoDetection *detection = GST_ML_VIDEO_DETECTION (object);

  gst_ml_module_free (detection->module);

  if (detection->mlinfo != NULL)
    gst_ml_info_free (detection->mlinfo);

  if (detection->outpool != NULL)
    gst_object_unref (detection->outpool);

  g_free (detection->labels);

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
          "Confidence threshold", 10.0F, 100.0F, DEFAULT_PROP_THRESHOLD,
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
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_detection_prepare_output_buffer);

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

  detection->mdlenum = DEFAULT_PROP_MODULE;
  detection->labels = DEFAULT_PROP_LABELS;
  detection->n_results = DEFAULT_PROP_NUM_RESULTS;
  detection->threshold = DEFAULT_PROP_THRESHOLD;

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
