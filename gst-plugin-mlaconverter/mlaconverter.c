/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/audio/audio.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/ml-frame.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#include "mlaconverter-engine.h"
#include "mlaconverter.h"

#define GST_CAT_DEFAULT gst_ml_audio_converter_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_audio_converter_debug);

#define gst_ml_audio_converter_parent_class parent_class
G_DEFINE_TYPE (GstMLAudioConverter, gst_ml_audio_converter,
    GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MIN_BUFFERS     2
#define DEFAULT_PROP_MAX_BUFFERS     24
#define DEFAULT_PROP_SAMPLE_RATE     16000

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS_ALL)
        ", layout = (string) interleaved, channels = (int) 1")
    );

#define GST_ML_TFLITE_TENSOR_TYPES "{ INT8, UINT8, INT32, FLOAT16, FLOAT32 }"

#define GST_SRC_PAD_CAPS                        \
    "neural-network/tensors, "                    \
    "type = (string) " GST_ML_TFLITE_TENSOR_TYPES

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_SRC_PAD_CAPS)
    );

enum {
  PROP_0,
  PROP_SAMPLE_RATE,
};

static GstBufferPool *
gst_ml_audio_converter_create_pool (GstMLAudioConverter * mlconverter,
    GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR ("Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_DEBUG_OBJECT (mlconverter, "create buffer pool based on caps: %"
      GST_PTR_FORMAT, caps);

  GST_INFO ("Uses ION memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_ION);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING ("Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_audio_converter_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR ("Failed to parse the allocation caps!");
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);

  // Create a new pool in case none was proposed in the query.
  if (!pool && !(pool = gst_ml_audio_converter_create_pool (mlconverter, caps))) {
    GST_ERROR("Failed to create buffer pool!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (mlconverter->outpool)
    gst_object_unref (mlconverter->outpool);

  mlconverter->outpool = pool;

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
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);

  gst_query_add_allocation_meta (query, GST_ML_TENSOR_META_API_TYPE, NULL);

  return TRUE;
}

static GstCaps *
gst_ml_audio_converter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (base);
  GstCaps *res = NULL;
  GstPad *trans_pad = NULL;
  //const GValue *value = NULL;

  GST_DEBUG_OBJECT (mlconverter, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (mlconverter, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SINK)
    trans_pad = GST_BASE_TRANSFORM_SRC_PAD (base);
  else
    trans_pad = GST_BASE_TRANSFORM_SINK_PAD (base);

  // caps result should be intersected from pad static caps and
  // filter caps and adjusted based on caps event direction

  res = gst_pad_get_pad_template_caps (trans_pad);

  GST_DEBUG_OBJECT (mlconverter, "pad caps %" GST_PTR_FORMAT, res);

  // Extract the rate and set it to property value in result sink caps.
  if (!gst_caps_is_empty (caps)) {

    gint idx = 0, length = 0;

    res = gst_caps_make_writable (res);
    length = gst_caps_get_size (res);

    if (direction == GST_PAD_SRC)
      for (idx = 0; idx < length; idx++) {
        GstStructure *structure = gst_caps_get_structure (res, idx);
        gst_structure_set (structure, "rate", G_TYPE_INT,
            mlconverter->sample_rate, NULL);
      }
  }

  if (filter != NULL) {
    GstCaps *intersection;

    intersection =
      gst_caps_intersect_full (res, filter, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (res);
    res = intersection;
  }

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, res);

  return res;
}

static GstCaps *
gst_ml_audio_converter_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (base);
  const GValue *val = NULL;
  GstStructure *caps_struct = NULL;

  g_return_val_if_fail (mlconverter != NULL, NULL);
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (othercaps != NULL, NULL);

  GST_DEBUG_OBJECT (mlconverter, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT " in direction %s",
      othercaps, caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  if (direction == GST_PAD_SRC) {
  caps_struct = gst_caps_get_structure (othercaps, 0);

  val = gst_structure_get_value (caps_struct, "rate");

  if (val != NULL || !gst_value_is_fixed (val))
    gst_structure_set (caps_struct, "rate",
        G_TYPE_INT, mlconverter->sample_rate, NULL);
  }

  return gst_caps_fixate (othercaps);
}

static gboolean
gst_ml_audio_converter_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (base);
  GstAudioInfo ininfo;
  GstMLInfo mlinfo;
  GstStructure *structure = NULL;

  GST_LOG_OBJECT (mlconverter, "incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);


  if (!gst_audio_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (mlconverter, "invalid incaps");
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&mlinfo, outcaps)) {
    GST_ERROR_OBJECT (mlconverter, "invalid outcaps");
    return FALSE;
  }

  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_in_place (base, FALSE);

  if (mlconverter->audio_info != NULL)
    gst_audio_info_free (mlconverter->audio_info);

  if (mlconverter->ml_info != NULL)
    gst_ml_info_free (mlconverter->ml_info);

  mlconverter->audio_info = gst_audio_info_copy (&ininfo);
  mlconverter->ml_info = gst_ml_info_copy (&mlinfo);

  if (mlconverter->engine != NULL) {
    gst_mlaconverter_engine_free (mlconverter->engine);
    mlconverter->engine = NULL;
  }

  structure = gst_structure_new ("options",
      GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE, G_TYPE_INT,
      GST_AUDIO_INFO_RATE(&ininfo),
      GST_ML_AUDIO_CONVERTER_OPT_BPS, G_TYPE_INT,
      GST_AUDIO_INFO_BPS(&ininfo),
      GST_ML_AUDIO_CONVERTER_OPT_FORMAT, G_TYPE_STRING,
      gst_audio_format_to_string (GST_AUDIO_INFO_FORMAT(&ininfo)),
      GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE, G_TYPE_STRING,
      gst_ml_type_to_string (GST_ML_INFO_TYPE(&mlinfo)),
      NULL);

  if (GST_ML_INFO_N_TENSORS(&mlinfo) == 1 &&
      GST_ML_INFO_N_DIMENSIONS(&mlinfo, 0) == 1) {
    gst_structure_set (structure, GST_ML_AUDIO_CONVERTER_OPT_MODE, G_TYPE_INT,
        GST_AUDIO_CONV_MODE_RAW, NULL);
    gst_structure_set (structure, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER,
        G_TYPE_INT, GST_ML_INFO_TENSOR_DIM(&mlinfo, 0, 0), NULL);
  }

  mlconverter->engine = gst_mlaconverter_engine_new ((const GstStructure *)structure);

  return TRUE;
}

static GstFlowReturn
gst_ml_audio_converter_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (base);
  GstBufferPool *pool = mlconverter->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_TRACE ("Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR ("Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    *outbuffer = gst_buffer_new ();
    GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
  }

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR ("Failed to acquire output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_FLAGS |
      GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Copy the offset field as it may contain channels data for batched buffers.
  GST_BUFFER_OFFSET (*outbuffer) = GST_BUFFER_OFFSET (inbuffer);

  //TBD: copy meta information if any

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ml_audio_converter_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (base);
  GstMLFrame outframe;
  GstAudioBuffer inframe;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;
  gboolean success = TRUE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  if (!gst_audio_buffer_map (&inframe, mlconverter->audio_info,
        inbuffer, GST_MAP_READ)) {
    GST_ERROR ("audio frame map failure");
    return GST_FLOW_ERROR;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0) {
      GST_ERROR ("DMA IOCTL SYNC START failed!");
      ret = GST_FLOW_ERROR;
      goto unmap_audio;
    }
  }
#endif // HAVE_LINUX_DMA_BUF_H

  if (!gst_ml_frame_map (&outframe, mlconverter->ml_info,
        outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR ("ml frame map failure");
    ret = GST_FLOW_ERROR;
    goto unmap_audio;
  }

  ts_begin = gst_util_get_timestamp ();

  success = gst_mlaconverter_engine_process (mlconverter->engine, &inframe, &outframe);

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0) {
      GST_ERROR ("DMA IOCTL SYNC END failed!");
      ret = GST_FLOW_ERROR;
      goto unmap_ml;
    }
  }
#endif // HAVE_LINUX_DMA_BUF_H

  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to process buffers");
    ret = GST_FLOW_ERROR;
    goto unmap_ml;
  }

  GST_LOG ("Execute took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  ret = GST_FLOW_OK;

unmap_ml:
  gst_ml_frame_unmap (&outframe);
unmap_audio:
  gst_audio_buffer_unmap (&inframe);

  return ret;
}

static void
gst_ml_audio_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (object);

  switch (prop_id) {
    case PROP_SAMPLE_RATE:
    {
      mlconverter->sample_rate = g_value_get_int (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_audio_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (object);

  switch (prop_id) {
    case PROP_SAMPLE_RATE:
    {
      g_value_set_int (value, mlconverter->sample_rate);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_audio_converter_finalize (GObject * object)
{
  GstMLAudioConverter *mlconverter = GST_ML_AUDIO_CONVERTER (object);

  if (mlconverter->engine != NULL)
    gst_mlaconverter_engine_free (mlconverter->engine);

  if (mlconverter->outpool != NULL)
    gst_object_unref (mlconverter->outpool);

  if (mlconverter->ml_info != NULL)
    gst_ml_info_free (mlconverter->ml_info);

  if (mlconverter->audio_info != NULL)
    gst_audio_info_free (mlconverter->audio_info);
}

static void
gst_ml_audio_converter_class_init (GstMLAudioConverterClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ml_audio_converter_debug, "qtimlaconverter", 0,
      "QTI ML audio converter plugin");

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_audio_converter_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_audio_converter_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_audio_converter_finalize);

  g_object_class_install_property (gobject, PROP_SAMPLE_RATE,
      g_param_spec_int ("sample-rate", "Sample-Rate",
          "Audio sample rate converter expects", G_MININT, G_MAXINT,
          DEFAULT_PROP_SAMPLE_RATE, G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning Audio Converter", "Audio",
      "Parse an Audio stream into a ML stream", "QTI");

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&src_template));

  base->decide_allocation =
    GST_DEBUG_FUNCPTR (gst_ml_audio_converter_decide_allocation);
  base->transform_caps =
    GST_DEBUG_FUNCPTR (gst_ml_audio_converter_transform_caps);
  base->fixate_caps =
    GST_DEBUG_FUNCPTR (gst_ml_audio_converter_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_audio_converter_set_caps);
  base->prepare_output_buffer =
    GST_DEBUG_FUNCPTR (gst_ml_audio_converter_prepare_output_buffer);
  base->transform = GST_DEBUG_FUNCPTR (gst_ml_audio_converter_transform);
}

static void
gst_ml_audio_converter_init (GstMLAudioConverter * mlconverter)
{
  mlconverter->engine  = NULL;
  mlconverter->outpool = NULL;
  mlconverter->ml_info = NULL;
  mlconverter->audio_info = NULL;
  mlconverter->sample_rate = DEFAULT_AUDIO_SAMPLE_RATE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlaconverter", GST_RANK_NONE,
      GST_TYPE_ML_AUDIO_CONVERTER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlaconverter,
    "QTI Machine Learning plugin for converting audio stream into ML stream",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
