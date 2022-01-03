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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlvclassification.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/video/gstimagepool.h>
#include <cairo/cairo.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#include "modules/ml-video-classification-module.h"

#define GST_CAT_DEFAULT gst_ml_video_classification_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_classification_debug);

#define gst_ml_video_classification_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoClassification, gst_ml_video_classification,
               GST_TYPE_BASE_TRANSFORM);

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_ML_VIDEO_CLASSIFICATION_VIDEO_FORMATS \
    "{ BGRA, RGBA, BGRx, xRGB, BGR16 }"

#define GST_ML_VIDEO_CLASSIFICATION_TEXT_FORMATS \
    "{ utf8 }"

#define GST_ML_VIDEO_CLASSIFICATION_SRC_CAPS                            \
    "video/x-raw, "                                                 \
    "format = (string) " GST_ML_VIDEO_CLASSIFICATION_VIDEO_FORMATS "; " \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "                \
    "format = (string) " GST_ML_VIDEO_CLASSIFICATION_VIDEO_FORMATS "; " \
    "text/x-raw, "                                                  \
    "format = (string) " GST_ML_VIDEO_CLASSIFICATION_TEXT_FORMATS

#define GST_ML_VIDEO_CLASSIFICATION_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE        NULL
#define DEFAULT_PROP_LABELS        NULL
#define DEFAULT_PROP_NUM_RESULTS   5
#define DEFAULT_PROP_THRESHOLD     10.0F


#define DEFAULT_MIN_BUFFERS        2
#define DEFAULT_MAX_BUFFERS        10
#define DEFAULT_TEXT_BUFFER_SIZE   4096
#define DEFAULT_FONT_SIZE          20

#define MAX_TEXT_LENGTH            25

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

static GstStaticCaps gst_ml_video_classification_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CLASSIFICATION_SINK_CAPS);

static GstStaticCaps gst_ml_video_classification_static_src_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CLASSIFICATION_SRC_CAPS);


/**
 * GstMLModule:
 * @libhandle: the library handle
 * @instance: instance of the tensor processing module
 *
 * @init: Initilizes an instance of the module.
 * @deinit: Deinitilizes the instance of the module.
 * @process: Decode the tensors inside the buffer into prediction results.
 *
 * Machine learning interface for post-processing module.
 */
struct _GstMLModule
{
  gpointer libhandle;
  gpointer instance;

  /// Virtual functions.
  gpointer (*init)    (const gchar * labels);
  void     (*deinit)  (gpointer instance);

  gboolean (*process) (gpointer instance, GstBuffer * buffer,
                       GList ** predictions);
};

static void
gst_ml_module_free (GstMLModule * module)
{
  if (NULL == module)
    return;

  if (module->instance != NULL)
    module->deinit (module->instance);

  if (module->libhandle != NULL)
    dlclose (module->libhandle);

  g_slice_free (GstMLModule, module);
}

static GstMLModule *
gst_ml_module_new (const gchar * libname, const gchar * labels)
{
  GstMLModule *module = NULL;
  gchar *location = NULL;

  location = g_strdup_printf ("%s/lib%s.so", GST_ML_MODULES_DIR, libname);

  module = g_slice_new0 (GstMLModule);
  g_return_val_if_fail (module != NULL, NULL);

  if ((module->libhandle = dlopen (location, RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open %s module library, error: %s!",
        libname, dlerror());

    g_free (location);
    gst_ml_module_free (module);

    return NULL;
  }

  g_free (location);

  module->init = dlsym (module->libhandle,
      "gst_ml_video_classification_module_init");
  module->deinit = dlsym (module->libhandle,
      "gst_ml_video_classification_module_deinit");
  module->process = dlsym (module->libhandle,
      "gst_ml_video_classification_module_process");

  if (!module->init || !module->deinit || !module->process) {
    GST_ERROR ("Failed to load %s library symbols, error: %s!",
        libname, dlerror());
    gst_ml_module_free (module);
    return NULL;
  }

  if ((module->instance = module->init (labels)) == NULL) {
    GST_ERROR ("Failed to initilize %s module library!", libname);
    gst_ml_module_free (module);
    return NULL;
  }

  return module;
}

static void
gst_ml_prediction_free (GstMLPrediction * prediction)
{
  if (prediction->label != NULL)
    g_free (prediction->label);

  g_free (prediction);
}

static GstCaps *
gst_ml_video_classification_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_classification_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_classification_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_classification_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_classification_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_classification_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_classification_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_classification_src_caps ());
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
gst_ml_video_classification_create_pool (
    GstMLVideoClassification * classification, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBufferPool *pool = NULL;
  guint size = 0;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (classification, "Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }

    // If downstream allocation query supports GBM, allocate gbm memory.
    if (caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (classification, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (classification, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    if (NULL == pool) {
      GST_ERROR_OBJECT (classification, "Failed to create buffer pool!");
      return NULL;
    }

    size = GST_VIDEO_INFO_SIZE (&info);
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    GST_INFO_OBJECT (classification, "Uses SYSTEM memory");

    if (NULL == (pool = gst_buffer_pool_new ())) {
      GST_ERROR_OBJECT (classification, "Failed to create buffer pool!");
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
    GST_WARNING_OBJECT (classification, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  return pool;
}

static gboolean
gst_ml_video_classification_fill_video_output (
    GstMLVideoClassification * classification, GList * predictions,
    GstBuffer *buffer)
{
  GstVideoMeta *vmeta = NULL;
  GstMapInfo memmap;
  guint idx = 0, n_predictions = 0;
  gdouble fontsize = 0.0;

  cairo_format_t format;
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;

  if (!(vmeta = gst_buffer_get_video_meta (buffer))) {
    GST_ERROR_OBJECT (classification, "Output buffer has no meta!");
    return FALSE;
  }

  switch (vmeta->format) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
      format = CAIRO_FORMAT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      format = CAIRO_FORMAT_RGB16_565;
      break;
    default:
      GST_ERROR_OBJECT (classification, "Unsupported format: %s!",
          gst_video_format_to_string (vmeta->format));
      return FALSE;
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (classification, "Failed to map buffer memory block!");
    return FALSE;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (classification, "DMA IOCTL SYNC START failed!");
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

  // Set the most appropriate font size based on number of results.
  fontsize = ((gdouble) vmeta->width / MAX_TEXT_LENGTH) * (5.0F / 3.0F);
  fontsize = MIN (fontsize, vmeta->height / classification->n_results);
  cairo_set_font_size (context, fontsize);

  {
    // Set font options.
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);
    cairo_set_font_options (context, options);
    cairo_font_options_destroy (options);
  }

  for (idx = 0; idx < g_list_length (predictions); ++idx) {
    const GstMLPrediction *prediction = NULL;
    gchar *string = NULL;

    // Break immediately if we reach the number of results limit.
    if (n_predictions >= classification->n_results)
      break;

    // Extract the prediction data.
    prediction = g_list_nth_data (predictions, idx);

    // Concat the prediction data to the output string.
    string = g_strdup_printf ("%s: %.1f%%", prediction->label,
        prediction->confidence);

    GST_TRACE_OBJECT (classification, "idx: %u, label: %s, confidence: %.1f%%",
        idx, prediction->label, prediction->confidence);

    // Set text color.
    cairo_set_source_rgba (context,
        EXTRACT_RED_COLOR (prediction->color),
        EXTRACT_GREEN_COLOR (prediction->color),
        EXTRACT_BLUE_COLOR (prediction->color),
        EXTRACT_ALPHA_COLOR (prediction->color));

    // (0,0) is at top left corner of the buffer.
    cairo_move_to (context, 0.0, fontsize * (idx + 1));

    // Draw text string.
    cairo_show_text (context, string);
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
      GST_WARNING_OBJECT (classification, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  // Unmap buffer memory blocks.
  gst_buffer_unmap (buffer, &memmap);

  return TRUE;
}

static gboolean
gst_ml_video_classification_fill_text_output (
    GstMLVideoClassification * classification, GList * predictions,
    GstBuffer *buffer)
{
  GstMapInfo memmap = {};
  GValue entries = G_VALUE_INIT;
  gchar *string = NULL;
  guint idx = 0, n_predictions = 0;
  gsize length = 0;

  g_value_init (&entries, GST_TYPE_LIST);

  for (idx = 0; idx < g_list_length (predictions); ++idx) {
    GstMLPrediction *prediction = NULL;
    GstStructure *entry = NULL;
    GValue value = G_VALUE_INIT;

    // Break immediately if we reach the number of results limit.
    if (n_predictions >= classification->n_results)
      break;

    // Extract the prediction data.
    prediction = g_list_nth_data (predictions, idx);

    GST_TRACE_OBJECT (classification, "idx: %u, label: %s, confidence: %.1f%%",
        idx, prediction->label, prediction->confidence);

    prediction->label = g_strdelimit (prediction->label, " ", '-');

    entry = gst_structure_new ("ImageClassification",
        "label", G_TYPE_STRING, prediction->label,
        "confidence", G_TYPE_FLOAT, prediction->confidence,
        "color", G_TYPE_UINT, prediction->color,
        NULL);

    prediction->label = g_strdelimit (prediction->label, "-", ' ');

    g_value_init (&value, GST_TYPE_STRUCTURE);

    gst_value_set_structure (&value, entry);
    gst_structure_free (entry);

    gst_value_list_append_value (&entries, &value);
    g_value_unset (&value);

    n_predictions++;
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (classification, "Failed to map buffer memory block!");
    return FALSE;
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&entries);
  g_value_unset (&entries);

  if (string == NULL) {
    GST_ERROR_OBJECT (classification, "Failed serialize predictions structure!");
    gst_buffer_unmap (buffer, &memmap);
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  // Check whether the length +1 byte for the additional '\n' is within maxsize.
  if ((length + 1) > memmap.maxsize) {
    GST_ERROR_OBJECT (classification, "String size exceeds max buffer size!");

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
gst_ml_video_classification_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (classification, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (classification->outpool)
    gst_object_unref (classification->outpool);

  // Create a new buffer pool.
  pool = gst_ml_video_classification_create_pool (classification, caps);
  if (pool == NULL) {
    GST_ERROR_OBJECT (classification, "Failed to create buffer pool!");
    return FALSE;
  }

  classification->outpool = pool;

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
gst_ml_video_classification_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (base);
  GstBufferPool *pool = classification->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (classification, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (classification, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (classification, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_video_classification_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (base);
  GstCaps *result = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (classification, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (classification, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  }

  // Extract the rate and propagate it to result caps.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      (direction == GST_PAD_SRC) ? "framerate" : "rate");

  if (value != NULL) {
    gint idx = 0, length = 0;

    result = gst_caps_make_writable (result);
    length = gst_caps_get_size (result);

    for (idx = 0; idx < length; idx++) {
      GstStructure *structure = gst_caps_get_structure (result, idx);
      gst_structure_set_value (structure,
          (direction == GST_PAD_SRC) ? "rate" : "framerate", value);
    }
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (classification, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_ml_video_classification_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (base);
  GstStructure *output = NULL;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (classification, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Fixate the output format.
  value = gst_structure_get_value (output, "format");

  if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field (output, "format");
    value = gst_structure_get_value (output, "format");
  }

  GST_DEBUG_OBJECT (classification, "Output format fixed to: %s",
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

    GST_DEBUG_OBJECT (classification, "Output PAR fixed to: %d/%d", par_n, par_d);

    // Retrieve the output width and height.
    value = gst_structure_get_value (output, "width");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      width = GST_ROUND_UP_4 (DEFAULT_FONT_SIZE * MAX_TEXT_LENGTH * 3 / 5);
      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }

    width = g_value_get_int (value);
    value = gst_structure_get_value (output, "height");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      height = GST_ROUND_UP_4 (DEFAULT_FONT_SIZE * classification->n_results);
      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }

    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (classification, "Output width and height fixated to: %dx%d",
        width, height);
  }

  GST_DEBUG_OBJECT (classification, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static gboolean
gst_ml_video_classification_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (base);
  GstMLModule *module = NULL;
  GstStructure *structure = NULL;
  GstMLInfo ininfo;

  if (NULL == classification->labels) {
    GST_ERROR_OBJECT (classification, "Labels not set!");
    return FALSE;
  } else if (NULL == classification->modname) {
    GST_ERROR_OBJECT (classification, "Module not set!");
    return FALSE;
  }

  module = gst_ml_module_new (classification->modname, classification->labels);
  if (NULL == module) {
    GST_ERROR_OBJECT (classification, "Failed to create processing module!");
    return FALSE;
  }

  gst_ml_module_free (classification->module);
  classification->module = module;

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (classification, "Failed to get input ML info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (classification->mlinfo != NULL)
    gst_ml_info_free (classification->mlinfo);

  classification->mlinfo = gst_ml_info_copy (&ininfo);
  gst_base_transform_set_passthrough (base, FALSE);

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw"))
    classification->mode = OUTPUT_MODE_VIDEO;
  else if (gst_structure_has_name (structure, "text/x-raw"))
    classification->mode = OUTPUT_MODE_TEXT;

  GST_DEBUG_OBJECT (classification, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (classification, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_classification_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (base);
  GList *predictions = NULL;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;
  gboolean success = FALSE;
  guint n_blocks = 0;

  g_return_val_if_fail (classification->module != NULL, GST_FLOW_ERROR);

  n_blocks = gst_buffer_n_memory (inbuffer);

  if (gst_buffer_get_size (inbuffer) != gst_ml_info_size (classification->mlinfo)) {
    GST_ERROR_OBJECT (classification, "Mismatch, expected buffer size %"
        G_GSIZE_FORMAT " but actual size is %" G_GSIZE_FORMAT "!",
        gst_ml_info_size (classification->mlinfo), gst_buffer_get_size (inbuffer));
    return FALSE;
  } else if ((n_blocks > 1) && n_blocks != classification->mlinfo->n_tensors) {
    GST_ERROR_OBJECT (classification, "Mismatch, expected %u memory blocks "
        "but buffer has %u!", classification->mlinfo->n_tensors, n_blocks);
    return FALSE;
  }

  n_blocks = gst_buffer_get_n_meta (inbuffer, GST_ML_TENSOR_META_API_TYPE);
  if (n_blocks != classification->mlinfo->n_tensors) {
    GST_ERROR_OBJECT (classification, "Input buffer has %u tensor metas but "
        "negotiated caps require %u!", n_blocks, classification->mlinfo->n_tensors);
    return GST_FLOW_ERROR;
  }

  if (gst_buffer_n_memory (outbuffer) == 0) {
    GST_ERROR_OBJECT (classification, "Output buffer has no memory blocks!");
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  // Call the submodule process funtion.
  success = classification->module->process (classification->module->instance,
      inbuffer, &predictions);

  if (!success) {
    GST_ERROR_OBJECT (classification, "Failed to process tensors!");
    g_list_free_full (predictions, (GDestroyNotify) gst_ml_prediction_free);
    return GST_FLOW_ERROR;
  }

  if (classification->mode == OUTPUT_MODE_VIDEO)
    success = gst_ml_video_classification_fill_video_output (classification,
        predictions, outbuffer);
  else if (classification->mode == OUTPUT_MODE_TEXT)
    success = gst_ml_video_classification_fill_text_output (classification,
        predictions, outbuffer);
  else
    success = FALSE;

  g_list_free_full (predictions, (GDestroyNotify) gst_ml_prediction_free);

  if (!success) {
    GST_ERROR_OBJECT (classification, "Failed to fill output buffer!");
    return GST_FLOW_ERROR;
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (classification, "Categorization took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_classification_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_free (classification->modname);
      classification->modname = g_strdup (g_value_get_string (value));
      break;
    case PROP_LABELS:
      g_free (classification->labels);
      classification->labels = g_strdup (g_value_get_string (value));
      break;
    case PROP_NUM_RESULTS:
      classification->n_results = g_value_get_uint (value);
      break;
    case PROP_THRESHOLD:
      classification->threshold = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_classification_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_value_set_string (value, classification->modname);
      break;
    case PROP_LABELS:
      g_value_set_string (value, classification->labels);
      break;
    case PROP_NUM_RESULTS:
      g_value_set_uint (value, classification->n_results);
      break;
    case PROP_THRESHOLD:
      g_value_set_double (value, classification->threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_classification_finalize (GObject * object)
{
  GstMLVideoClassification *classification = GST_ML_VIDEO_CLASSIFICATION (object);

  gst_ml_module_free (classification->module);

  if (classification->mlinfo != NULL)
    gst_ml_info_free (classification->mlinfo);

  if (classification->outpool != NULL)
    gst_object_unref (classification->outpool);

  g_free (classification->modname);
  g_free (classification->labels);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (classification));
}

static void
gst_ml_video_classification_class_init (GstMLVideoClassificationClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_classification_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_classification_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_classification_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_string ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          DEFAULT_PROP_MODULE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
          "Confidence threshold in %", 1.0F, 100.0F, DEFAULT_PROP_THRESHOLD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning image classification", "Filter/Effect/Converter",
      "Machine Learning plugin for image classification processing", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_classification_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_classification_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_classification_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_classification_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_classification_transform_caps);
  base->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_classification_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_classification_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_classification_transform);
}

static void
gst_ml_video_classification_init (GstMLVideoClassification * classification)
{
  classification->outpool = NULL;
  classification->module = NULL;

  classification->modname = DEFAULT_PROP_MODULE;
  classification->labels = DEFAULT_PROP_LABELS;
  classification->n_results = DEFAULT_PROP_NUM_RESULTS;
  classification->threshold = DEFAULT_PROP_THRESHOLD;

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_classification_debug,
      "qtimlvclassification", 0, "QTI ML image categorization plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvclassification", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_CLASSIFICATION);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvclassification,
    "QTI Machine Learning plugin for image classification post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
