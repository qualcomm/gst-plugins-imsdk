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

#include "mltflite.h"

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_ml_tflite_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_tflite_debug);

#define gst_ml_tflite_parent_class parent_class
G_DEFINE_TYPE (GstMLTFLite, gst_ml_tflite, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MODEL       NULL
#define DEFAULT_PROP_DELEGATE    GST_ML_TFLITE_DELEGATE_NONE
#define DEFAULT_PROP_THREADS     1

#ifdef HAVE_EXTERNAL_DELEGATE_H
#define DEFAULT_PROP_EXT_DELEGATE_PATH    NULL
#define DEFAULT_PROP_EXT_DELEGATE_OPTS    NULL
#endif // HAVE_EXTERNAL_DELEGATE_H

#define DEFAULT_PROP_MIN_BUFFERS 2
#define DEFAULT_PROP_MAX_BUFFERS 10

#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
#define GST_ML_TFLITE_TENSOR_TYPES "{ INT8, UINT8, INT32, UINT32, FLOAT16, FLOAT32 }"
#else
#define GST_ML_TFLITE_TENSOR_TYPES "{ INT8, UINT8, INT32, FLOAT16, FLOAT32 }"
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)

#define GST_ML_TFLITE_CAPS                        \
    "neural-network/tensors, "                    \
    "type = (string) " GST_ML_TFLITE_TENSOR_TYPES

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_DELEGATE,
  PROP_THREADS,
#ifdef HAVE_EXTERNAL_DELEGATE_H
  PROP_EXT_DELEGATE_PATH,
  PROP_EXT_DELEGATE_OPTS,
#endif // HAVE_EXTERNAL_DELEGATE_H
};

static GstStaticCaps gst_ml_tflite_static_caps =
    GST_STATIC_CAPS (GST_ML_TFLITE_CAPS);

static GstCaps *
gst_ml_tflite_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_tflite_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_tflite_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_tflite_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_tflite_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_tflite_src_caps ());
}

static GstPadTemplate *
gst_ml_tflite_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_tflite_sink_caps ());
}

static GstBufferPool *
gst_ml_tflite_create_pool (GstMLTFLite * tflite, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (tflite, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (tflite, "Uses ION memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_ION);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (tflite, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_tflite_propose_allocation (GstBaseTransform * base,
    GstQuery * inquery, GstQuery * outquery)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (base);

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
    GST_ERROR_OBJECT (tflite, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (tflite, "Failed to get ML info!");
    return FALSE;
  }

  // Get the size from ML info.
  size = gst_ml_info_size (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    if ((pool = gst_ml_tflite_create_pool (tflite, caps)) == NULL) {
      GST_ERROR_OBJECT (tflite, "Failed to create buffer pool!");
      return FALSE;
    }

    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (tflite, "Failed to set buffer pool configuration!");
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
gst_ml_tflite_decide_allocation (GstBaseTransform * base, GstQuery * query)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (tflite, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (tflite->outpool)
    gst_object_unref (tflite->outpool);

  // Create a new buffer pool.
  if ((pool = gst_ml_tflite_create_pool (tflite, caps)) == NULL) {
    GST_ERROR_OBJECT (tflite, "Failed to create buffer pool!");
    return FALSE;
  }

  tflite->outpool = pool;

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
gst_ml_tflite_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (base);
  GstBufferPool *pool = tflite->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (tflite, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (!tflite->engine) {
    GST_WARNING_OBJECT (tflite, "Engine not created!");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (tflite, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (tflite, "Failed to create output buffer!");
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
gst_ml_tflite_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (base);
  GstCaps *result = NULL;
  const GstMLInfo *mlinfo = NULL;
  const GValue *value = NULL;

  if ((NULL == tflite->engine) && (filter != NULL))
    return gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  else if (NULL == tflite->engine)
    return gst_caps_ref (caps);

  GST_DEBUG_OBJECT (tflite, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (tflite, "Filter caps: %" GST_PTR_FORMAT, filter);

  switch (direction) {
    case GST_PAD_SRC:
      mlinfo = gst_ml_tflite_engine_get_input_info (tflite->engine);
      break;
    case GST_PAD_SINK:
      mlinfo = gst_ml_tflite_engine_get_output_info (tflite->engine);
      break;
    default:
      GST_ERROR_OBJECT (tflite, "Invalid pad direction!");
      return NULL;
  }

  // The source and sink pads caps do not depend on each other so directly take
  // the ML caps from the engine for the corresponding pad and apply filter.
  result = gst_ml_info_to_caps (mlinfo);

  // Extract the rate.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

  // Propagate rate to the ML caps if it exists.
  if (value != NULL)
    gst_caps_set_value (result, "rate", value);

  GST_DEBUG_OBJECT (tflite, "ML caps: %" GST_PTR_FORMAT, result);

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (tflite, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_ml_tflite_accept_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (base);
  GstCaps *mlcaps = NULL;
  const GstMLInfo *mlinfo = NULL;

  GST_DEBUG_OBJECT (tflite, "Accept caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  if ((NULL == tflite->engine) && (direction == GST_PAD_SINK)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));
  } else if ((NULL == tflite->engine) && (direction == GST_PAD_SRC)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (base));
  } else if (direction == GST_PAD_SINK) {
    mlinfo = gst_ml_tflite_engine_get_input_info (tflite->engine);
  } else if (direction == GST_PAD_SRC) {
    mlinfo = gst_ml_tflite_engine_get_output_info (tflite->engine);
  }

  if ((mlinfo != NULL) && (NULL == mlcaps))
    mlcaps = gst_ml_info_to_caps (mlinfo);

  if (NULL == mlcaps) {
    GST_ERROR_OBJECT (base, "Failed to get ML caps!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (tflite, "ML caps: %" GST_PTR_FORMAT, mlcaps);

  if (!gst_caps_can_intersect (caps, mlcaps)) {
    GST_WARNING_OBJECT (base, "Caps can't intersect!");
    return FALSE;
  }

  return TRUE;
}

static GstStateChangeReturn
gst_ml_tflite_change_state (GstElement * element, GstStateChange transition)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GstStructure *settings = gst_structure_new ("ml-engine-settings",
          GST_ML_TFLITE_ENGINE_OPT_MODEL, G_TYPE_STRING,
          tflite->model,
          GST_ML_TFLITE_ENGINE_OPT_DELEGATE, GST_TYPE_ML_TFLITE_DELEGATE,
          tflite->delegate,
          GST_ML_TFLITE_ENGINE_OPT_THREADS, G_TYPE_UINT,
          tflite->n_threads,
          NULL);

      if (settings == NULL) {
        GST_ERROR_OBJECT (tflite, "Failed to populate engine settings!");
        return GST_STATE_CHANGE_FAILURE;
      }
#ifdef HAVE_EXTERNAL_DELEGATE_H
      if (tflite->delegate == GST_ML_TFLITE_DELEGATE_EXTERNAL) {
        gst_structure_set(settings,
            GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_PATH, G_TYPE_STRING,
            tflite->ext_delegate_path,
            GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_OPTS, GST_TYPE_STRUCTURE,
            tflite->ext_delegate_opts,
            NULL);
      }
#endif // HAVE_EXTERNAL_DELEGATE_H
      gst_ml_tflite_engine_free (tflite->engine);

      tflite->engine = gst_ml_tflite_engine_new (settings);
      if (NULL == tflite->engine) {
        GST_ERROR_OBJECT (tflite, "Failed to create engine!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_ERROR_OBJECT (tflite, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ml_tflite_engine_free (tflite->engine);
      tflite->engine = NULL;
      break;
    default:
      // This is to catch PAUSED->PAUSED and PLAYING->PLAYING transitions.
      ret = (GST_STATE_TRANSITION_NEXT (transition) == GST_STATE_PAUSED) ?
          GST_STATE_CHANGE_NO_PREROLL : GST_STATE_CHANGE_SUCCESS;
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ml_tflite_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (base);
  GstMLFrame inframe, outframe;
  const GstMLInfo * info = NULL;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  info = gst_ml_tflite_engine_get_input_info (tflite->engine);

  // Create ML frame from input buffer.
  if (!gst_ml_frame_map (&inframe, info, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (tflite, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  info = gst_ml_tflite_engine_get_output_info (tflite->engine);

  // Create ML frame from output buffer.
  if (!gst_ml_frame_map (&outframe, info, outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (tflite, "Failed to map output buffer!");
    gst_ml_frame_unmap (&inframe);
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  gst_ml_tflite_engine_execute (tflite->engine, &inframe, &outframe);

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (tflite, "Execute took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  gst_ml_frame_unmap (&outframe);
  gst_ml_frame_unmap (&inframe);

  return GST_FLOW_OK;
}

static void
gst_ml_tflite_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_free (tflite->model);
      tflite->model = g_strdup (g_value_get_string (value));
      break;
    case PROP_DELEGATE:
      tflite->delegate = g_value_get_enum (value);
      break;
    case PROP_THREADS:
      tflite->n_threads = g_value_get_uint (value);
      break;
#ifdef HAVE_EXTERNAL_DELEGATE_H
    case PROP_EXT_DELEGATE_PATH:
      g_free (tflite->ext_delegate_path);
      tflite->ext_delegate_path = g_strdup (g_value_get_string (value));
      break;
    case PROP_EXT_DELEGATE_OPTS:

      if (tflite->ext_delegate_opts)
        gst_structure_free (tflite->ext_delegate_opts);

      tflite->ext_delegate_opts =
          GST_STRUCTURE_CAST (g_value_dup_boxed (value));
      break;
#endif // HAVE_EXTERNAL_DELEGATE_H
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_tflite_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_value_set_string (value, tflite->model);
      break;
    case PROP_DELEGATE:
      g_value_set_enum (value, tflite->delegate);
      break;
    case PROP_THREADS:
      g_value_set_uint (value, tflite->n_threads);
      break;
#ifdef HAVE_EXTERNAL_DELEGATE_H
    case PROP_EXT_DELEGATE_PATH:
      g_value_set_string (value, tflite->ext_delegate_path);
      break;
    case PROP_EXT_DELEGATE_OPTS:

      if (tflite->ext_delegate_opts)
        g_value_set_boxed (value, tflite->ext_delegate_opts);

      break;
#endif // HAVE_EXTERNAL_DELEGATE_H
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_tflite_finalize (GObject * object)
{
  GstMLTFLite *tflite = GST_ML_TFLITE (object);

#ifdef HAVE_EXTERNAL_DELEGATE_H
  if (tflite->ext_delegate_path != NULL) {
    g_free (tflite->ext_delegate_path);
    tflite->ext_delegate_path = NULL;
  }

  if (tflite->ext_delegate_opts != NULL) {
    gst_structure_free (tflite->ext_delegate_opts);
    tflite->ext_delegate_opts = NULL;
  }
#endif // HAVE_EXTERNAL_DELEGATE_H

  gst_ml_tflite_engine_free (tflite->engine);

  if (tflite->outpool != NULL)
    gst_object_unref (tflite->outpool);

  g_free (tflite->model);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (tflite));
}

static void
gst_ml_tflite_class_init (GstMLTFLiteClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_tflite_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_tflite_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_tflite_finalize);

  g_object_class_install_property (gobject, PROP_MODEL,
      g_param_spec_string ("model", "Model",
          "Model filename", DEFAULT_PROP_MODEL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_DELEGATE,
      g_param_spec_enum ("delegate", "Delegate",
          "Delegate part or all of graph execution to another executor",
          GST_TYPE_ML_TFLITE_DELEGATE, DEFAULT_PROP_DELEGATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_THREADS,
      g_param_spec_uint ("threads", "Threads",
          "Number of threads", 1, 4, DEFAULT_PROP_THREADS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#ifdef HAVE_EXTERNAL_DELEGATE_H
  g_object_class_install_property (gobject, PROP_EXT_DELEGATE_PATH,
      g_param_spec_string ("external-delegate-path", "External Delegate Path",
          "External delegate's absolute path. "
          "This takes effect when the 'delegate' property is 'external'.",
          DEFAULT_PROP_EXT_DELEGATE_PATH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_EXT_DELEGATE_OPTS,
      g_param_spec_boxed ("external-delegate-options",
          "External Delegate Options",
          "External delegate's options, "
          "that includes backend type and backend library path. "
          "This takes effect when the 'delegate' property is 'external'.",
          GST_TYPE_STRUCTURE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif // HAVE_EXTERNAL_DELEGATE_H

  gst_element_class_set_static_metadata (element,
      "TFLite Machine Learning", "Filter/Effect/Converter",
      "TFLite based Machine Learning plugin", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_tflite_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_tflite_src_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_tflite_change_state);

  base->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_tflite_propose_allocation);
  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_tflite_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_tflite_prepare_output_buffer);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_tflite_transform_caps);
  base->accept_caps = GST_DEBUG_FUNCPTR (gst_ml_tflite_accept_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_tflite_transform);
}

static void
gst_ml_tflite_init (GstMLTFLite * tflite)
{
  tflite->outpool = NULL;
  tflite->engine = NULL;

  tflite->model = DEFAULT_PROP_MODEL;
  tflite->delegate = DEFAULT_PROP_DELEGATE;
#ifdef HAVE_EXTERNAL_DELEGATE_H
  tflite->ext_delegate_path = DEFAULT_PROP_EXT_DELEGATE_PATH;
  tflite->ext_delegate_opts = DEFAULT_PROP_EXT_DELEGATE_OPTS;
#endif // HAVE_EXTERNAL_DELEGATE_H
  tflite->n_threads = DEFAULT_PROP_THREADS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (tflite), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_tflite_debug, "qtimltflite", 0,
      "QTI TFLite ML plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimltflite", GST_RANK_NONE,
      GST_TYPE_ML_TFLITE);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimltflite,
    "QTI TFLite based Machine Learnig plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
