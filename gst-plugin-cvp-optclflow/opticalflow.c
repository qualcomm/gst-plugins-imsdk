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

#include "opticalflow.h"

#include <gst/memory/gstionpool.h>

#include <gbm.h>
#include <gbm_priv.h>

#define GST_CAT_DEFAULT cvp_optclflow_debug
GST_DEBUG_CATEGORY_STATIC (cvp_optclflow_debug);

#define gst_cvp_optclflow_parent_class parent_class
G_DEFINE_TYPE (GstCvpOptclFlow, gst_cvp_optclflow, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_ENABLE_STATS  TRUE

#define DEFAULT_MIN_BUFFERS        2
#define DEFAULT_MAX_BUFFERS        10

#define GST_VIDEO_FORMATS "{ GRAY8, NV12 }"

#ifndef GST_CAPS_FEATURE_MEMORY_GMB
#define GST_CAPS_FEATURE_MEMORY_GMB "memory:GBM"
#endif

enum
{
  PROP_0,
  PROP_ENABLE_STATS,
};


static GstStaticCaps gst_cvp_optclflow_static_sink_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));


static GstStaticCaps gst_cvp_optclflow_static_src_caps =
    GST_STATIC_CAPS ("cvp/x-optical-flow");


static GstCaps *
gst_cvp_optclflow_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_cvp_optclflow_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_cvp_optclflow_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_cvp_optclflow_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_cvp_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_cvp_optclflow_sink_caps ());
}

static GstPadTemplate *
gst_cvp_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_cvp_optclflow_src_caps ());
}

static gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;
  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    // Skip ANY caps and return immediately if feature is present
    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
}

static GstBufferPool *
gst_cvp_optclflow_create_pool (GstCvpOptclFlow * optclflow)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GValue memblocks = G_VALUE_INIT, value = G_VALUE_INIT;
  guint mvsize = 0, statsize = 0;

  gst_cvp_optclflow_engine_sizes (optclflow->engine, &mvsize, &statsize);

  GST_INFO_OBJECT (optclflow, "Uses ION memory");
  pool = gst_ion_buffer_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, (mvsize + statsize),
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  g_value_init (&memblocks, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_UINT);

  // Set memory block 1
  g_value_set_uint (&value, mvsize);
  gst_value_array_append_value (&memblocks, &value);

  // Set memory block 2
  g_value_set_uint (&value, statsize);
  gst_value_array_append_value (&memblocks, &value);

  gst_structure_set_value (config, "memory-blocks", &memblocks);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (optclflow, "Failed to set pool configuration!");
    g_object_unref (pool);
  }

  g_object_unref (allocator);
  return pool;
}

static gboolean
gst_cvp_optclflow_decide_allocation (GstBaseTransform * base, GstQuery * query)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (optclflow, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (optclflow->outpool) {
    gst_buffer_pool_set_active (optclflow->outpool, FALSE);
    gst_object_unref (optclflow->outpool);
  }

  // Create a new buffer pool.
  pool = gst_cvp_optclflow_create_pool (optclflow);
  optclflow->outpool = pool;

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

  return TRUE;
}

static GstFlowReturn
gst_cvp_optclflow_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);
  GstBufferPool *pool = optclflow->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (base)) {
    GST_LOG_OBJECT (optclflow, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (optclflow, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (optclflow, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static gboolean
gst_cvp_optclflow_query (GstBaseTransform * base, GstPadDirection direction,
    GstQuery * query)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DRAIN:
      // Only drain queries to the sink pad can be processed.
      if (direction != GST_PAD_SINK)
        break;

      GST_DEBUG_OBJECT (optclflow, "Draining buffers queue");

      // Drain any unprocessed buffers.
      if (optclflow->buffers != NULL)
        g_list_free_full (optclflow->buffers, (GDestroyNotify) gst_buffer_unref);

      optclflow->buffers = NULL;
      return TRUE;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (base, direction, query);
}

static GstCaps *
gst_cvp_optclflow_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);
  GstCaps *result = NULL;

  GST_DEBUG_OBJECT (optclflow, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (optclflow, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (optclflow, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static gboolean
gst_cvp_optclflow_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);
  GstStructure *settings = NULL;
  GstVideoInfo ininfo;
  guint stride = 0, scanline = 0, size = 0;

  GST_DEBUG_OBJECT (optclflow, "Input caps: %" GST_PTR_FORMAT, incaps);

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (optclflow, "Failed to get input video info from caps!");
    return FALSE;
  }

  gst_base_transform_set_passthrough (base, FALSE);

  if (gst_caps_has_feature (incaps, GST_CAPS_FEATURE_MEMORY_GMB)) {
    GST_LOG_OBJECT (optclflow, "Using stride and scanline from GBM");

    struct gbm_buf_info bufinfo;
    bufinfo.width = GST_VIDEO_INFO_WIDTH (&ininfo);
    bufinfo.height = GST_VIDEO_INFO_HEIGHT (&ininfo);

    switch (GST_VIDEO_INFO_FORMAT (&ininfo)) {
      case GST_VIDEO_FORMAT_NV12:
        bufinfo.format = GBM_FORMAT_NV12;
        break;
      case GST_VIDEO_FORMAT_GRAY8:
      default:
        GST_ERROR_OBJECT (optclflow, "Invalid video type for GBM");
        return FALSE;
    }

    gbm_perform (GBM_PERFORM_GET_BUFFER_SIZE_DIMENSIONS,
        &bufinfo, 0, &stride, &scanline, &size);
  } else {
    GST_LOG_OBJECT (optclflow, "Using stride and scanline from GstVideoInfo");

    stride = GST_VIDEO_INFO_PLANE_STRIDE (&ininfo, 0);
    scanline = (GST_VIDEO_INFO_N_PLANES (&ininfo) == 2) ?
        (GST_VIDEO_INFO_PLANE_OFFSET (&ininfo, 1) / stride) :
        GST_VIDEO_INFO_SIZE (&ininfo);
  }

  GST_LOG_OBJECT (optclflow, "stride %d, scanline %d", stride, scanline);

  if (optclflow->engine != NULL)
    gst_cvp_optclflow_engine_free (optclflow->engine);

  // Fill the converter input options structure.
  settings = gst_structure_new ("qtioptclflow",
      GST_CVP_OPTCLFLOW_ENGINE_OPT_VIDEO_WIDTH, G_TYPE_UINT,
      GST_VIDEO_INFO_WIDTH (&ininfo),
      GST_CVP_OPTCLFLOW_ENGINE_OPT_VIDEO_HEIGHT, G_TYPE_UINT,
      GST_VIDEO_INFO_HEIGHT (&ininfo),
      GST_CVP_OPTCLFLOW_ENGINE_OPT_VIDEO_STRIDE, G_TYPE_UINT,
      stride,
      GST_CVP_OPTCLFLOW_ENGINE_OPT_VIDEO_SCANLINE, G_TYPE_UINT,
      scanline,
      GST_CVP_OPTCLFLOW_ENGINE_OPT_VIDEO_FORMAT, GST_TYPE_VIDEO_FORMAT,
      GST_VIDEO_INFO_FORMAT (&ininfo),
      GST_CVP_OPTCLFLOW_ENGINE_OPT_VIDEO_FPS, G_TYPE_UINT,
      GST_VIDEO_INFO_FPS_N (&ininfo) / GST_VIDEO_INFO_FPS_D (&ininfo),
      GST_CVP_OPTCLFLOW_ENGINE_OPT_ENABLE_STATS, G_TYPE_BOOLEAN,
      optclflow->stats,
      NULL);

  optclflow->engine = gst_cvp_optclflow_engine_new (settings);

  if (optclflow->ininfo != NULL)
    gst_video_info_free (optclflow->ininfo);

  optclflow->ininfo = gst_video_info_copy (&ininfo);

  GST_DEBUG_OBJECT (optclflow, "Output caps: %" GST_PTR_FORMAT, outcaps);
  return TRUE;
}

static GstFlowReturn
gst_cvp_optclflow_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);
  GstBuffer *buffer = NULL;
  GstVideoFrame inframes[2];
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;
  gboolean success = FALSE;

  inbuffer = gst_buffer_ref (inbuffer);
  optclflow->buffers = g_list_append (optclflow->buffers, inbuffer);

  if (g_list_length (optclflow->buffers) != 2) {
    GST_TRACE_OBJECT (optclflow, "Need 1 buffer history, currently have %u "
        "buffers!", g_list_length (optclflow->buffers));
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  }

  // Create a frame from previous buffer.
  buffer = g_list_nth_data (optclflow->buffers, 0);
  optclflow->buffers = g_list_remove (optclflow->buffers, buffer);

  success = gst_video_frame_map (&inframes[0], optclflow->ininfo, buffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (optclflow, "Failed to map previous input buffer!");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  // Create a frame from current buffer.
  buffer = g_list_nth_data (optclflow->buffers, 0);

  success = gst_video_frame_map (&inframes[1], optclflow->ininfo, buffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (optclflow, "Failed to map current input buffer!");

    buffer = inframes[0].buffer;
    gst_video_frame_unmap (&inframes[0]);
    gst_buffer_unref (buffer);

    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  success = gst_cvp_optclflow_engine_execute (optclflow->engine, inframes, 2,
      outbuffer);

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  if (!success)
    GST_ERROR_OBJECT (optclflow, "Failed to process buffers!");

  GST_LOG_OBJECT (optclflow, "Execution took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  // Ummap current frame without releasing the buffer reference.
  gst_video_frame_unmap (&inframes[1]);

  // Previous buffer for which the optical flow result applies to.
  buffer = inframes[0].buffer;

  // Copy the flags and timestamps from the previous buffer buffer.
  gst_buffer_copy_into (outbuffer, buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Unmap previous buffer frame and release the held reference.
  gst_video_frame_unmap (&inframes[0]);
  gst_buffer_unref (buffer);

  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static gboolean
gst_cvp_optclflow_stop (GstBaseTransform * base)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (base);

  if (optclflow->buffers != NULL)
    g_list_free_full (optclflow->buffers, (GDestroyNotify) gst_buffer_unref);

  optclflow->buffers = NULL;
  return TRUE;
}

static void
gst_cvp_optclflow_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec *pspec)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (object);

  switch (property_id) {
    case PROP_ENABLE_STATS:
      optclflow->stats = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_cvp_optclflow_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (object);

  switch (property_id) {
    case PROP_ENABLE_STATS:
      g_value_set_boolean (value, optclflow->stats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_cvp_optclflow_finalize (GObject * object)
{
  GstCvpOptclFlow *optclflow = GST_CVP_OPTCLFLOW (object);

  if (optclflow->engine != NULL)
    gst_cvp_optclflow_engine_free (optclflow->engine);

  if (optclflow->buffers != NULL)
    g_list_free_full (optclflow->buffers, (GDestroyNotify) gst_buffer_unref);

  if (optclflow->ininfo != NULL)
    gst_video_info_free (optclflow->ininfo);

  if (optclflow->outpool != NULL)
    gst_object_unref (optclflow->outpool);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (optclflow));
}

static void
gst_cvp_optclflow_class_init (GstCvpOptclFlowClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_finalize);

  g_object_class_install_property (gobject, PROP_ENABLE_STATS,
      g_param_spec_boolean ("stats", "Stats",
          "Enable statistics for additional motion vector info",
          DEFAULT_PROP_ENABLE_STATS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (
      element, "CVP Optical Flow", "Runs optical flow from CVP",
      "Calculate motion vector from current image and previous image", "QTI");

  gst_element_class_add_pad_template (element, gst_cvp_sink_template ());
  gst_element_class_add_pad_template (element, gst_cvp_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_prepare_output_buffer);
  base->query = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_query);
  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_transform_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_set_caps);
  base->transform = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_transform);
  base->stop = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_stop);
}

static void
gst_cvp_optclflow_init (GstCvpOptclFlow * optclflow)
{
  optclflow->ininfo = NULL;
  optclflow->outpool = NULL;
  optclflow->engine = NULL;

  optclflow->stats = DEFAULT_PROP_ENABLE_STATS;

  GST_DEBUG_CATEGORY_INIT (cvp_optclflow_debug, "qticvpoptclflow", 0,
      "QTI Computer Vision Processor Optical Flow");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qticvpoptclflow", GST_RANK_PRIMARY,
      GST_TYPE_CVP_OPTCLFLOW);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  qticvpoptclflow,
  "Computer Vision Processor Optical Flow",
  plugin_init,
  PACKAGE_VERSION,
  PACKAGE_LICENSE,
  PACKAGE_SUMMARY,
  PACKAGE_ORIGIN
)
