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
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlonnx.h"

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_ml_onnx_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_onnx_debug);

#define gst_ml_onnx_parent_class parent_class
G_DEFINE_TYPE (GstMLOnnx, gst_ml_onnx, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MODEL                NULL
#define DEFAULT_PROP_EXECUTION_PROVIDER   GST_ML_ONNX_EXECUTION_PROVIDER_CPU
#define DEFAULT_PROP_OPTIMIZATION_LEVEL   GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED
#define DEFAULT_PROP_QNN_BACKEND_PATH     NULL
#define DEFAULT_PROP_THREADS              1
#define DEFAULT_PROP_MIN_BUFFERS 2
#define DEFAULT_PROP_MAX_BUFFERS 10

#define GST_ML_ONNX_TENSOR_TYPES "{ INT8, UINT8, INT16, UINT16, INT32, UINT32, INT64, UINT64, FLOAT16, FLOAT32 }"

#define GST_ML_ONNX_CAPS                          \
    "neural-network/tensors, "                    \
    "type = (string) " GST_ML_ONNX_TENSOR_TYPES

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_EXECUTION_PROVIDER,
  PROP_QNN_BACKEND_PATH,
  PROP_OPTIMIZATION_LEVEL,
  PROP_THREADS,
};

static GstStaticCaps gst_ml_onnx_static_caps =
    GST_STATIC_CAPS (GST_ML_ONNX_CAPS);

static GstCaps *
gst_ml_onnx_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_onnx_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_onnx_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_onnx_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_onnx_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_onnx_src_caps ());
}

static GstPadTemplate *
gst_ml_onnx_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_onnx_sink_caps ());
}

static GstBufferPool *
gst_ml_onnx_create_pool (GstMLOnnx * onnx, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (onnx, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (onnx, "Uses DMA memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_DMA);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (onnx, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_onnx_propose_allocation (GstBaseTransform * base,
    GstQuery * inquery, GstQuery * outquery)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstMLInfo info;
  guint size = 0;
  gboolean needpool = FALSE;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (
        base, inquery, outquery))
    return FALSE;

  // No input query, nothing to do.
  if (NULL == inquery)
    return TRUE;

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (onnx, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (onnx, "Failed to get ML info!");
    return FALSE;
  }

  // Get the size from ML info.
  size = gst_ml_info_size (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    if ((pool = gst_ml_onnx_create_pool (onnx, caps)) == NULL) {
      GST_ERROR_OBJECT (onnx, "Failed to create buffer pool!");
      return FALSE;
    }

    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (onnx, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, needpool ? pool : NULL, size, 0, 0);

  if (pool != NULL)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (outquery, GST_ML_TENSOR_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_ml_onnx_decide_allocation (GstBaseTransform * base, GstQuery * query)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (onnx, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool only if caps have changed
  if (onnx->outpool) {
    GstStructure *config = gst_buffer_pool_get_config (onnx->outpool);
    GstCaps *pool_caps = NULL;
    gst_buffer_pool_config_get_params (config, &pool_caps, NULL, NULL, NULL);

    if (!gst_caps_is_equal (caps, pool_caps)) {
      gst_object_unref (onnx->outpool);
      onnx->outpool = NULL;
    }
    gst_structure_free (config);
  }

  // Create a new buffer pool only if needed
  if (!onnx->outpool) {
    if ((pool = gst_ml_onnx_create_pool (onnx, caps)) == NULL) {
      GST_ERROR_OBJECT (onnx, "Failed to create buffer pool!");
      return FALSE;
    }
    onnx->outpool = pool;
  } else {
    pool = onnx->outpool;
  }

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

  gst_query_add_allocation_meta (query, GST_ML_TENSOR_META_API_TYPE, NULL);
  return TRUE;
}

static GstFlowReturn
gst_ml_onnx_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);
  GstBufferPool *pool = onnx->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (onnx, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (!onnx->engine) {
    GST_WARNING_OBJECT (onnx, "Engine not created!");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (onnx, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (onnx, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_FLAGS |
      GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Copy the offset field as it may contain channels data for batched buffers.
  GST_BUFFER_OFFSET (*outbuffer) = GST_BUFFER_OFFSET (inbuffer);

  // Transfer GstProtectionMeta entries from input to the output buffer.
  gst_buffer_copy_protection_meta (*outbuffer, inbuffer);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_onnx_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);
  GstCaps *result = NULL;
  const GValue *value = NULL;

  if ((NULL == onnx->engine) && (filter != NULL))
    return gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  else if (NULL == onnx->engine)
    return gst_caps_ref (caps);

  GST_DEBUG_OBJECT (onnx, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (onnx, "Filter caps: %" GST_PTR_FORMAT, filter);

  switch (direction) {
    case GST_PAD_SRC:
      result = gst_ml_onnx_engine_get_input_caps (onnx->engine);
      break;
    case GST_PAD_SINK:
      result = gst_ml_onnx_engine_get_output_caps (onnx->engine);
      break;
    default:
      GST_ERROR_OBJECT (onnx, "Invalid pad direction!");
      return NULL;
  }

  // Extract the rate.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

  // Propagate rate to the ML caps if it exists.
  if (value != NULL)
    gst_caps_set_value (result, "rate", value);

  GST_DEBUG_OBJECT (onnx, "ML caps: %" GST_PTR_FORMAT, result);

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (onnx, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_ml_onnx_accept_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);
  GstCaps *mlcaps = NULL;

  GST_DEBUG_OBJECT (onnx, "Accept caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  if ((NULL == onnx->engine) && (direction == GST_PAD_SINK)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));
  } else if ((NULL == onnx->engine) && (direction == GST_PAD_SRC)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (base));
  } else if (direction == GST_PAD_SINK) {
    mlcaps = gst_ml_onnx_engine_get_input_caps (onnx->engine);
  } else if (direction == GST_PAD_SRC) {
    mlcaps = gst_ml_onnx_engine_get_output_caps (onnx->engine);
  }

  if (NULL == mlcaps) {
    GST_ERROR_OBJECT (base, "Failed to get ML caps!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (onnx, "ML caps: %" GST_PTR_FORMAT, mlcaps);

  if (!gst_caps_can_intersect (caps, mlcaps)) {
    GST_WARNING_OBJECT (base, "Caps can't intersect!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ml_onnx_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, incaps)) {
    GST_ERROR_OBJECT (onnx, "Failed to get input ML info from caps!");
    return FALSE;
  }

  if (onnx->ininfo != NULL)
    gst_ml_info_free (onnx->ininfo);

  onnx->ininfo = gst_ml_info_copy (&info);
  GST_DEBUG_OBJECT (onnx, "Input caps: %" GST_PTR_FORMAT, incaps);

  if (!gst_ml_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (onnx, "Failed to get input ML info from caps!");
    return FALSE;
  }

  if (onnx->outinfo != NULL)
    gst_ml_info_free (onnx->outinfo);

  onnx->outinfo = gst_ml_info_copy (&info);
  GST_DEBUG_OBJECT (onnx, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstStateChangeReturn
gst_ml_onnx_change_state (GstElement * element, GstStateChange transition)
{
  GstMLOnnx *onnx = GST_ML_ONNX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GstStructure *settings = gst_structure_new ("ml-engine-settings",
          GST_ML_ONNX_ENGINE_OPT_MODEL, G_TYPE_STRING,
          onnx->model,
          GST_ML_ONNX_ENGINE_OPT_EXECUTION_PROVIDER,
          GST_TYPE_ML_ONNX_EXECUTION_PROVIDER,
          onnx->execution_provider,
          GST_ML_ONNX_ENGINE_OPT_QNN_BACKEND_PATH, G_TYPE_STRING,
          onnx->backend_path,
          GST_ML_ONNX_ENGINE_OPT_OPTIMIZATION_LEVEL,
          GST_TYPE_ML_ONNX_OPTIMIZATION_LEVEL,
          onnx->optimization_level,
          GST_ML_ONNX_ENGINE_OPT_THREADS, G_TYPE_UINT,
          onnx->n_threads,
          NULL);

      if (settings == NULL) {
        GST_ERROR_OBJECT (onnx, "Failed to populate engine settings!");
        return GST_STATE_CHANGE_FAILURE;
      }

      gst_ml_onnx_engine_free (onnx->engine);

      onnx->engine = gst_ml_onnx_engine_new (settings);
      if (NULL == onnx->engine) {
        GST_ERROR_OBJECT (onnx, "Failed to create engine!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ml_onnx_engine_free (onnx->engine);
      onnx->engine = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ml_onnx_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLOnnx *onnx = GST_ML_ONNX (base);
  GstMLFrame inframe, outframe;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Create ML frame from input buffer.
  if (!gst_ml_frame_map (&inframe, onnx->ininfo, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (onnx, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  // Create ML frame from output buffer.
  if (!gst_ml_frame_map (&outframe, onnx->outinfo, outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (onnx, "Failed to map output buffer!");
    gst_ml_frame_unmap (&inframe);
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  success = gst_ml_onnx_engine_execute (onnx->engine, &inframe, &outframe);

  ts_end = gst_util_get_timestamp ();

  gst_ml_frame_unmap (&outframe);
  gst_ml_frame_unmap (&inframe);

  if (!success) {
    GST_ERROR_OBJECT (onnx, "Failed to execute!");
    return GST_FLOW_ERROR;
  }

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (onnx, "Execute took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_onnx_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLOnnx *onnx = GST_ML_ONNX (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_free (onnx->model);
      onnx->model = g_strdup (g_value_get_string (value));
      break;
    case PROP_EXECUTION_PROVIDER:
      onnx->execution_provider = (GstMLOnnxExecutionProvider) g_value_get_enum (value);
      break;
    case PROP_QNN_BACKEND_PATH:
      g_free (onnx->backend_path);
      onnx->backend_path = g_strdup (g_value_get_string (value));
      break;
    case PROP_OPTIMIZATION_LEVEL:
      onnx->optimization_level = (GstMLOnnxOptimizationLevel) g_value_get_enum (value);
      break;
    case PROP_THREADS:
      onnx->n_threads = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_onnx_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMLOnnx *onnx = GST_ML_ONNX (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_value_set_string (value, onnx->model);
      break;
    case PROP_EXECUTION_PROVIDER:
      g_value_set_enum (value, onnx->execution_provider);
      break;
    case PROP_QNN_BACKEND_PATH:
      g_value_set_string (value, onnx->backend_path);
      break;
    case PROP_OPTIMIZATION_LEVEL:
      g_value_set_enum (value, onnx->optimization_level);
      break;
    case PROP_THREADS:
      g_value_set_uint (value, onnx->n_threads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_onnx_finalize (GObject * object)
{
  GstMLOnnx *onnx = GST_ML_ONNX (object);

  if (onnx->backend_path != NULL) {
    g_free (onnx->backend_path);
    onnx->backend_path = NULL;
  }

  if (onnx->outinfo != NULL)
    gst_ml_info_free (onnx->outinfo);

  if (onnx->ininfo != NULL)
    gst_ml_info_free (onnx->ininfo);

  gst_ml_onnx_engine_free (onnx->engine);

  if (onnx->outpool != NULL)
    gst_object_unref (onnx->outpool);

  g_free (onnx->model);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (onnx));
}

static void
gst_ml_onnx_class_init (GstMLOnnxClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_onnx_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_onnx_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_onnx_finalize);

  g_object_class_install_property (gobject, PROP_MODEL,
      g_param_spec_string ("model", "Model",
          "Model filename", DEFAULT_PROP_MODEL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_EXECUTION_PROVIDER,
      g_param_spec_enum ("execution-provider", "Execution Provider",
          "ONNX Runtime execution provider",
          GST_TYPE_ML_ONNX_EXECUTION_PROVIDER, DEFAULT_PROP_EXECUTION_PROVIDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_QNN_BACKEND_PATH,
      g_param_spec_string ("backend-path", "QNN Backend Library Path",
          "Absolute file path to QNN backend library. "
          "Provide the QNN backend library path for execution-provider 'qnn'.",
          DEFAULT_PROP_QNN_BACKEND_PATH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_OPTIMIZATION_LEVEL,
      g_param_spec_enum ("optimization-level", "Optimization Level",
          "ONNX Runtime graph optimization level",
          GST_TYPE_ML_ONNX_OPTIMIZATION_LEVEL, DEFAULT_PROP_OPTIMIZATION_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_THREADS,
      g_param_spec_uint ("threads", "Threads",
          "Number of threads", 1, 16, DEFAULT_PROP_THREADS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "ONNX Machine Learning", "Filter/Effect/Converter",
      "ONNX Runtime based Machine Learning plugin", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_onnx_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_onnx_src_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_onnx_change_state);

  base->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_onnx_propose_allocation);
  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_onnx_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_onnx_prepare_output_buffer);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_onnx_transform_caps);
  base->accept_caps = GST_DEBUG_FUNCPTR (gst_ml_onnx_accept_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_onnx_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_onnx_transform);
}

static void
gst_ml_onnx_init (GstMLOnnx * onnx)
{
  onnx->outpool = NULL;
  onnx->engine = NULL;
  onnx->ininfo = NULL;
  onnx->outinfo = NULL;

  onnx->model = DEFAULT_PROP_MODEL;
  onnx->execution_provider = DEFAULT_PROP_EXECUTION_PROVIDER;
  onnx->backend_path = DEFAULT_PROP_QNN_BACKEND_PATH;
  onnx->optimization_level = DEFAULT_PROP_OPTIMIZATION_LEVEL;
  onnx->n_threads = DEFAULT_PROP_THREADS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (onnx), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_onnx_debug, "qtimlonnx", 0,
      "QTI ONNX ML plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlonnx", GST_RANK_NONE,
      GST_TYPE_ML_ONNX);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlonnx,
    "QTI ONNX Runtime based Machine Learning plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
