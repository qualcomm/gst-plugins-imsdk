/*
* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-3-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlaclassification.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/video/gstimagepool.h>
#include <gst/memory/gstmempool.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <cairo/cairo.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_ml_audio_classification_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_audio_classification_debug);

#define gst_ml_audio_classification_parent_class parent_class
G_DEFINE_TYPE (GstMLAudioClassification, gst_ml_audio_classification,
    GST_TYPE_BASE_TRANSFORM);

#define GST_TYPE_ML_MODULES (gst_ml_modules_get_type())

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_ML_AUDIO_CLASSIFICATION_VIDEO_FORMATS \
    "{ BGRA, BGRx, BGR16 }"

#define GST_ML_AUDIO_CLASSIFICATION_TEXT_FORMATS \
    "{ utf8 }"

#define GST_ML_AUDIO_CLASSIFICATION_SRC_CAPS                            \
    "video/x-raw, "                                                     \
    "format = (string) " GST_ML_AUDIO_CLASSIFICATION_VIDEO_FORMATS "; " \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "                    \
    "format = (string) " GST_ML_AUDIO_CLASSIFICATION_VIDEO_FORMATS "; " \
    "text/x-raw, "                                                      \
    "format = (string) " GST_ML_AUDIO_CLASSIFICATION_TEXT_FORMATS

#define GST_ML_AUDIO_CLASSIFICATION_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE           0
#define DEFAULT_PROP_LABELS           NULL
#define DEFAULT_PROP_NUM_RESULTS      5
#define DEFAULT_PROP_THRESHOLD        10.0F

#define DEFAULT_MIN_BUFFERS        2
#define DEFAULT_MAX_BUFFERS        10
#define DEFAULT_TEXT_BUFFER_SIZE   8192
#define DEFAULT_FONT_SIZE          20

#define MAX_TEXT_LENGTH            25

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

static GstStaticCaps gst_ml_audio_classification_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_AUDIO_CLASSIFICATION_SINK_CAPS);

static GstStaticCaps gst_ml_audio_classification_static_src_caps =
    GST_STATIC_CAPS (GST_ML_AUDIO_CLASSIFICATION_SRC_CAPS);

static GstCaps *
gst_ml_audio_classification_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_audio_classification_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_audio_classification_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_audio_classification_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_audio_classification_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_audio_classification_sink_caps ());
}

static GstPadTemplate *
gst_ml_audio_classification_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_audio_classification_src_caps ());
}

static GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_enumarate_modules ("ml-aclassification-");
  gtype = g_enum_register_static ("GstMLAudioClassificationModules", variants);

  return gtype;
}

static GstBufferPool *
gst_ml_audio_classification_create_pool (
    GstMLAudioClassification * classification, GstCaps * caps)
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
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
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
    pool = gst_mem_buffer_pool_new (GST_MEMORY_BUFFER_POOL_TYPE_SYSTEM);

    if (NULL == pool) {
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
gst_ml_audio_classification_fill_video_output (
    GstMLAudioClassification * classification, GstBuffer * buffer)
{
  GstVideoMeta *vmeta = NULL;
  GstMapInfo memmap;
  guint idx = 0, num = 0, n_entries = 0;
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
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_BGRx:
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

  // Fill a semi-transperant black background.
  cairo_set_source_rgba (context, 0.0, 0.0, 0.0, 0.5);
  cairo_paint (context);

  // Select font.
  cairo_select_font_face (context, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_antialias (context, CAIRO_ANTIALIAS_BEST);

  // Set the most appropriate font size based on number of results.
  fontsize = ((gdouble) vmeta->width / MAX_TEXT_LENGTH) * 9.0 / 5.0;
  fontsize = MIN (fontsize, vmeta->height / classification->n_results);
  cairo_set_font_size (context, fontsize);

  {
    // Set font options.
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);
    cairo_set_font_options (context, options);
    cairo_font_options_destroy (options);
  }

  for (idx = 0; idx < classification->predictions->len; idx++) {
    GstMLClassPrediction *prediction = NULL;
    GstMLClassEntry *entry = NULL;
    gchar *string = NULL;

    prediction =
        &(g_array_index (classification->predictions, GstMLClassPrediction, idx));

    n_entries = (prediction->entries->len < classification->n_results) ?
        prediction->entries->len : classification->n_results;

    for (num = 0; num < n_entries; num++) {
      entry = &(g_array_index (prediction->entries, GstMLClassEntry, num));

      // Concat the prediction data to the output string.
      string = g_strdup_printf ("%s: %.1f%%",
            g_quark_to_string (entry->name), entry->confidence);

      GST_TRACE_OBJECT (classification, "Batch: %u, label: %s, confidence: "
            "%.1f%%", prediction->batch_idx, g_quark_to_string (entry->name),
            entry->confidence);

      // Set text color.
      cairo_set_source_rgba (context,
            EXTRACT_RED_COLOR (entry->color), EXTRACT_GREEN_COLOR (entry->color),
            EXTRACT_BLUE_COLOR (entry->color), EXTRACT_ALPHA_COLOR (entry->color));

      // (0,0) is at top left corner of the buffer.
      cairo_move_to (context, 0.0, (fontsize * (num + 1)) - 6);

      // Draw text string.
      cairo_show_text (context, string);
      g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      // Flush to ensure all writing to the surface has been done.
      cairo_surface_flush (surface);

      g_free (string);
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
      GST_WARNING_OBJECT (classification, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  // Unmap buffer memory blocks.
  gst_buffer_unmap (buffer, &memmap);

  return TRUE;
}

static gboolean
gst_ml_audio_classification_fill_text_output (
    GstMLAudioClassification * classification, GstBuffer *buffer)
{
  GstStructure *structure = NULL;
  gchar *string = NULL, *name = NULL;
  GstMapInfo memmap = {};
  GValue list = G_VALUE_INIT, labels = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0, num = 0, n_entries = 0;
  gsize length = 0;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&labels, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_STRUCTURE);

  for (idx = 0; idx < classification->predictions->len; idx++) {
    GstMLClassPrediction *prediction = NULL;
    GstMLClassEntry *entry = NULL;

    prediction =
        &(g_array_index (classification->predictions, GstMLClassPrediction, idx));

    n_entries = (prediction->entries->len < classification->n_results) ?
        prediction->entries->len : classification->n_results;

    for (num = 0; num < n_entries; num++) {
      entry = &(g_array_index (prediction->entries, GstMLClassEntry, num));

      GST_TRACE_OBJECT (classification, "Batch: %u, label: %s, confidence: "
            "%.1f%%", prediction->batch_idx, g_quark_to_string (entry->name),
            entry->confidence);

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (g_quark_to_string (entry->name));
      name = g_strdelimit (name, " ", '.');

      structure = gst_structure_new (name, "id", G_TYPE_UINT, num,
          "confidence", G_TYPE_DOUBLE,  entry->confidence, "color", G_TYPE_UINT,
          entry->color, NULL);
      g_free (name);

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&labels, &value);
      g_value_reset (&value);
    }

    structure = gst_structure_new ("AudioClassification",
        "batch-index", G_TYPE_UINT, prediction->batch_idx,
        "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (buffer), NULL);

    gst_structure_set_value (structure, "labels", &labels);
    g_value_reset (&labels);

    g_value_take_boxed (&value, structure);
    gst_value_list_append_value (&list, &value);
    g_value_reset (&value);
  }

  g_value_unset (&labels);
  g_value_unset (&value);

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (classification, "Failed to map buffer memory block!");
    return FALSE;
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

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
gst_ml_audio_classification_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);

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
  pool = gst_ml_audio_classification_create_pool (classification, caps);
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
gst_ml_audio_classification_submit_input_buffer (GstBaseTransform * base,
    gboolean is_discont, GstBuffer * buffer)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);
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

  // Check if the baseclass set the plugin in passthrough mode.
  if (gst_base_transform_is_passthrough (base))
    return ret;

  // GAP input buffer, cleanup the entries and set the protection meta info.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    GstMLClassPrediction *prediction = NULL;

    for (idx = 0; idx < classification->predictions->len; ++idx) {
      prediction =
          &(g_array_index (classification->predictions, GstMLClassPrediction, idx));

      g_array_remove_range (prediction->entries, 0, prediction->entries->len);
    }

    return GST_FLOW_OK;
  }

  // Perform pre-processing on the input buffer.
  time = gst_util_get_timestamp ();

  if (!gst_ml_frame_map (&mlframe, classification->mlinfo, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (classification, "Failed to map buffer!");
    return GST_FLOW_ERROR;
  }

  // Clear previously stored values.
  for (idx = 0; idx < classification->predictions->len; ++idx) {
    GstMLClassPrediction *prediction =
        &(g_array_index (classification->predictions, GstMLClassPrediction, idx));

    g_array_remove_range (prediction->entries, 0, prediction->entries->len);
    prediction->info = NULL;
  }

  // Call the submodule process funtion.
  success = gst_ml_module_audio_classification_execute (classification->module,
      &mlframe, classification->predictions);

  gst_ml_frame_unmap (&mlframe);

  if (!success) {
    GST_ERROR_OBJECT (classification, "Failed to process tensors!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (classification, "Processing took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ml_audio_classification_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);
  GstBufferPool *pool = classification->outpool;

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

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if ((classification->mode == OUTPUT_MODE_VIDEO) &&
      (gst_buffer_get_size (inbuffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    *outbuffer = gst_buffer_new ();
    GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
  }

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (classification, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_audio_classification_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);
  GstCaps *tmplcaps = NULL, *result = NULL;
  guint idx = 0, num = 0, length = 0;

  GST_DEBUG_OBJECT (classification, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (classification, "Filter caps: %" GST_PTR_FORMAT, filter);

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

  GST_DEBUG_OBJECT (classification, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_audio_classification_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);
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

  // Fixate any remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (classification, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_audio_classification_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);
  GstCaps *modulecaps = NULL;
  GstStructure *structure = NULL;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstMLInfo ininfo;
  guint idx = 0;

  if (NULL == classification->labels) {
    GST_ELEMENT_ERROR (classification, RESOURCE, NOT_FOUND, (NULL),
        ("Labels not set!"));
    return FALSE;
  } else if (DEFAULT_PROP_MODULE == classification->mdlenum) {
    GST_ELEMENT_ERROR (classification, RESOURCE, NOT_FOUND, (NULL),
        ("Module name not set, automatic module pick up not supported!"));
    return FALSE;
  }

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_ML_MODULES));
  evalue = g_enum_get_value (eclass, classification->mdlenum);

  gst_ml_module_free (classification->module);
  classification->module = gst_ml_module_new (evalue->value_name);

  if (NULL == classification->module) {
    GST_ELEMENT_ERROR (classification, RESOURCE, FAILED, (NULL),
        ("Module creation failed!"));
    return FALSE;
  }

  modulecaps = gst_ml_module_get_caps (classification->module);

  if (!gst_caps_can_intersect (incaps, modulecaps)) {
    GST_ELEMENT_ERROR (classification, RESOURCE, FAILED, (NULL),
        ("Module caps %" GST_PTR_FORMAT " do not intersect with the "
         "negotiated caps %" GST_PTR_FORMAT "!", modulecaps, incaps));
    return FALSE;
  }

  if (!gst_ml_module_init (classification->module)) {
    GST_ELEMENT_ERROR (classification, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));
    return FALSE;
  }

  structure = gst_structure_new ("options",
      GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, incaps,
      GST_ML_MODULE_OPT_LABELS, G_TYPE_STRING, classification->labels,
      GST_ML_MODULE_OPT_THRESHOLD, G_TYPE_DOUBLE, classification->threshold,
      NULL);

  if (!gst_ml_module_set_opts (classification->module, structure)) {
    GST_ELEMENT_ERROR (classification, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (classification, "Failed to get input ML info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (classification->mlinfo != NULL)
    gst_ml_info_free (classification->mlinfo);

  classification->mlinfo = gst_ml_info_copy (&ininfo);

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw"))
    classification->mode = OUTPUT_MODE_VIDEO;
  else if (gst_structure_has_name (structure, "text/x-raw"))
    classification->mode = OUTPUT_MODE_TEXT;

  if ((classification->mode == OUTPUT_MODE_VIDEO) &&
      (GST_ML_INFO_TENSOR_DIM (classification->mlinfo, 0, 0) > 1)) {
    GST_ELEMENT_ERROR (classification, CORE, FAILED, (NULL),
        ("Batched input tensors with video output is not supported!"));
    return FALSE;
  }

  // Allocate the maximum number of predictions based on the batch size.
  g_array_set_size (classification->predictions,
      GST_ML_INFO_TENSOR_DIM (classification->mlinfo, 0, 0));

  for (idx = 0; idx < classification->predictions->len; ++idx) {
    GstMLClassPrediction *prediction =
        &(g_array_index (classification->predictions, GstMLClassPrediction, idx));

    prediction->entries = g_array_new (FALSE, FALSE, sizeof (GstMLClassEntry));
    prediction->batch_idx = idx;
  }

  GST_DEBUG_OBJECT (classification, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (classification, "Output caps: %" GST_PTR_FORMAT, outcaps);

  gst_base_transform_set_passthrough (base, FALSE);
  return TRUE;
}

static GstFlowReturn
gst_ml_audio_classification_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  g_return_val_if_fail (classification->module != NULL, GST_FLOW_ERROR);

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  time = gst_util_get_timestamp ();

  if (classification->mode == OUTPUT_MODE_VIDEO) {
    success = gst_ml_audio_classification_fill_video_output (classification,
        outbuffer);
  } else if (classification->mode == OUTPUT_MODE_TEXT) {
    success = gst_ml_audio_classification_fill_text_output (classification,
        outbuffer);
  }

  if (!success) {
    GST_ERROR_OBJECT (classification, "Failed to fill output buffer!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (classification, "Categorization took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_audio_classification_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (object);

  switch (prop_id) {
    case PROP_MODULE:
      classification->mdlenum = g_value_get_enum (value);
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
gst_ml_audio_classification_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_value_set_enum (value, classification->mdlenum);
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
gst_ml_audio_classification_finalize (GObject * object)
{
  GstMLAudioClassification *classification = GST_ML_AUDIO_CLASSIFICATION (object);

  g_array_free (classification->predictions, TRUE);
  gst_ml_module_free (classification->module);

  if (classification->mlinfo != NULL)
    gst_ml_info_free (classification->mlinfo);

  if (classification->outpool != NULL)
    gst_object_unref (classification->outpool);

  g_free (classification->labels);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (classification));
}

static void
gst_ml_audio_classification_class_init (GstMLAudioClassificationClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_audio_classification_finalize);

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

  gst_element_class_set_static_metadata (element,
      "Machine Learning audio classification", "Filter/Effect/Converter",
      "Machine Learning plugin for audio classification processing", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_audio_classification_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_audio_classification_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_decide_allocation);
  base->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_submit_input_buffer);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_transform_caps);
  base->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_ml_audio_classification_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_audio_classification_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_audio_classification_transform);
}

static void
gst_ml_audio_classification_init (GstMLAudioClassification * classification)
{
  classification->outpool = NULL;
  classification->module = NULL;

  classification->predictions =
      g_array_new (FALSE, FALSE, sizeof (GstMLClassPrediction));
  g_return_if_fail (classification->predictions != NULL);

  g_array_set_clear_func (classification->predictions,
      (GDestroyNotify) gst_ml_class_audio_prediction_cleanup);

  classification->mdlenum = DEFAULT_PROP_MODULE;
  classification->labels = DEFAULT_PROP_LABELS;
  classification->n_results = DEFAULT_PROP_NUM_RESULTS;
  classification->threshold = DEFAULT_PROP_THRESHOLD;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (classification), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_audio_classification_debug,
      "qtimlaclassification", 0, "QTI ML audio classification plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlaclassification", GST_RANK_NONE,
      GST_TYPE_ML_AUDIO_CLASSIFICATION);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlaclassification,
    "QTI Machine Learning plugin for audio classification post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
