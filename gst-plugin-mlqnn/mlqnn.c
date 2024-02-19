/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include "mlqnn.h"

#include <gst/ml/gstmlmeta.h>
#include <gst/ml/gstmlpool.h>
#include <gst/ml/ml-frame.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_ml_qnn_debug
GST_DEBUG_CATEGORY (gst_ml_qnn_debug);

#define gst_ml_qnn_parent_class parent_class
G_DEFINE_TYPE (GstMLQnn, gst_ml_qnn, GST_TYPE_BASE_TRANSFORM);

#define PROP_QNN_BACKEND_DEFAULT  NULL
#define PROP_QNN_MODEL_DEFAULT    NULL

#define DEFAULT_PROP_MIN_BUFFERS  2
#define DEFAULT_PROP_MAX_BUFFERS  10

#define GST_ML_QNN_TENSOR_TYPES "{ INT8, UINT8, INT32, UINT32, FLOAT16, FLOAT32 }"

#define GST_ML_QNN_CAPS                           \
    "neural-network/tensors, "                    \
    "type = (string) " GST_ML_QNN_TENSOR_TYPES

enum
{
  PROP_0,
  PROP_QNN_BACKEND,
  PROP_QNN_MODEL,
};

static GstStaticCaps gst_ml_qnn_static_caps = GST_STATIC_CAPS (GST_ML_QNN_CAPS);

static GstCaps *
gst_ml_qnn_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_qnn_static_caps);
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstCaps *
gst_ml_qnn_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_qnn_static_caps);
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstPadTemplate *
gst_ml_qnn_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_qnn_src_caps ());
}

static GstPadTemplate *
gst_ml_qnn_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_qnn_sink_caps ());
}

static GstCaps *
gst_ml_qnn_transform_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * filter)
{
  GstMLQnn *mlqnn = GST_ML_QNN (base);
  GstCaps *mlcaps = NULL;
  const GstMLInfo *mlinfo = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (mlqnn, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (mlqnn, "Filter caps: %" GST_PTR_FORMAT, filter);

  if ((NULL == mlqnn->engine) && (filter != NULL))
    return gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  else if (NULL == mlqnn->engine)
    return gst_caps_ref (caps);

  switch (direction) {
    case GST_PAD_SRC:
      mlinfo = gst_ml_qnn_engine_get_input_info (mlqnn->engine);
      break;
    case GST_PAD_SINK:
      mlinfo = gst_ml_qnn_engine_get_output_info (mlqnn->engine);
      break;
    default:
      GST_ERROR_OBJECT (mlqnn, "Invalid pad direction!");
      return NULL;
  }

  // The source and sink pads caps do not depend on each other so directly take
  // the ML caps from the engine for the corresponding pad and apply filter.
  mlcaps = gst_ml_info_to_caps (mlinfo);

  // Extract the rate.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

  // Propagate rate to the ML caps if it exists.
  if (value != NULL)
    gst_caps_set_value (mlcaps, "rate", value);

  if (mlcaps)
    GST_DEBUG_OBJECT (mlqnn, "ML caps: %" GST_PTR_FORMAT, mlcaps);
  else
    GST_DEBUG_OBJECT (mlqnn, "ML caps: NULL");

  if (filter) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, mlcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (mlcaps);
    mlcaps = intersection;
  }

  GST_DEBUG_OBJECT (mlqnn, "Returning caps: %" GST_PTR_FORMAT, mlcaps);

  return mlcaps;
}

static gboolean
gst_ml_qnn_accept_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps)
{
  GstMLQnn *mlqnn = GST_ML_QNN (base);
  GstCaps *mlcaps = NULL;
  const GstMLInfo *mlinfo = NULL;

  GST_DEBUG_OBJECT (mlqnn, "Accept caps: %" GST_PTR_FORMAT " in direction %s",
      caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  if ((NULL == mlqnn->engine) && (direction == GST_PAD_SINK)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));
  } else if ((NULL == mlqnn->engine) && (direction == GST_PAD_SRC)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (base));
  } else if (direction == GST_PAD_SINK) {
    mlinfo = gst_ml_qnn_engine_get_input_info (mlqnn->engine);
    mlcaps = gst_ml_info_to_caps (mlinfo);
  } else if (direction == GST_PAD_SRC) {
    mlinfo = gst_ml_qnn_engine_get_output_info (mlqnn->engine);
    mlcaps = gst_ml_info_to_caps (mlinfo);
  }

  if (NULL == mlcaps) {
    GST_ERROR_OBJECT (base, "Failed to get ML caps!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (mlqnn, "ML caps: %" GST_PTR_FORMAT, mlcaps);

  if (!gst_caps_can_intersect (caps, mlcaps)) {
    GST_WARNING_OBJECT (base, "Caps can't intersect!");
    return FALSE;
  }

  return TRUE;
}

static GstBufferPool *
gst_ml_qnn_create_pool (GstMLQnn * mlqnn, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlqnn, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (mlqnn, "Uses ION memory");

  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_ION);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config,
      GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (mlqnn, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_qnn_decide_allocation (GstBaseTransform * base, GstQuery * query)
{
  GstMLQnn *mlqnn = GST_ML_QNN (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  GST_DEBUG_OBJECT (mlqnn, "decide_allocation");

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (mlqnn, "Failed to parse the allocation caps!");
    return FALSE;
  }
  // Invalidate the cached pool if there is an allocation_query.
  if (mlqnn->outpool)
    gst_object_unref (mlqnn->outpool);

  // Create a new buffer pool.
  if ((pool = gst_ml_qnn_create_pool (mlqnn, caps)) == NULL) {
    GST_ERROR_OBJECT (mlqnn, "Failed to create buffer pool!");
    return FALSE;
  }

  mlqnn->outpool = pool;

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

static gboolean
gst_ml_qnn_propose_allocation (GstBaseTransform * base,
    GstQuery * inquery, GstQuery * outquery)
{
  GstMLQnn *mlqnn = GST_ML_QNN (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstMLInfo info;
  guint size = 0;
  gboolean needpool = FALSE;

  GST_DEBUG_OBJECT (mlqnn, "propose_allocation");

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (base,
      inquery, outquery))
    return FALSE;

  // No input query, nothing to do.
  if (NULL == inquery)
    return TRUE;

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (mlqnn, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlqnn, "Failed to get ML info!");
    return FALSE;
  }
  // Get the size from ML info.
  size = gst_ml_info_size (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    if ((pool = gst_ml_qnn_create_pool (mlqnn, caps)) == NULL) {
      GST_ERROR_OBJECT (mlqnn, "Failed to create buffer pool!");
      return FALSE;
    }

    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (mlqnn, "Failed to set buffer pool configuration!");
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


static GstFlowReturn
gst_ml_qnn_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLQnn *mlqnn = GST_ML_QNN (base);
  GstBufferPool *pool = mlqnn->outpool;
  GstProtectionMeta *pmeta = NULL;

  GST_DEBUG_OBJECT (mlqnn, "prepare_output_buffer");

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (mlqnn, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (mlqnn, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }
  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (mlqnn, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }
  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Copy the offset field as it may contain channels data for batched buffers.
  GST_BUFFER_OFFSET (*outbuffer) = GST_BUFFER_OFFSET (inbuffer);

  // Transfer GstProtectionMeta entries from input to the output buffer.
  gst_buffer_copy_protection_meta (*outbuffer, inbuffer);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_ml_qnn_change_state (GstElement * element, GstStateChange transition)
{
  GstMLQnn *mlqnn = GST_ML_QNN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GstStructure *settings = NULL;

      if (mlqnn->engine)
        gst_ml_qnn_engine_free (mlqnn->engine);

      settings = gst_structure_new ("ml-engine-settings",
          GST_ML_QNN_ENGINE_OPT_BACKEND, G_TYPE_STRING, mlqnn->backend,
          GST_ML_QNN_ENGINE_OPT_MODEL, G_TYPE_STRING, mlqnn->model,
          NULL);

      mlqnn->engine = gst_ml_qnn_engine_new (settings);

      if (mlqnn->engine == NULL) {
        GST_ERROR_OBJECT (mlqnn, "Failed to create engine!");
        return GST_STATE_CHANGE_FAILURE;
      }

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ml_qnn_engine_free (mlqnn->engine);
      mlqnn->engine = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ml_qnn_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLQnn *mlqnn = GST_ML_QNN (base);
  GstMLFrame inframe, outframe;
  const GstMLInfo *info = NULL;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  GST_DEBUG_OBJECT (mlqnn, "Transform Inbuf %ld  Outbuf %ld",
      gst_buffer_get_size (inbuffer), gst_buffer_get_size (outbuffer));

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  info = gst_ml_qnn_engine_get_input_info (mlqnn->engine);

  // Create ML frame from input buffer.
  if (!gst_ml_frame_map (&inframe, info, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (mlqnn, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  info = gst_ml_qnn_engine_get_output_info (mlqnn->engine);

  // Create ML frame from output buffer.
  if (!gst_ml_frame_map (&outframe, info, outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (mlqnn, "Failed to map output buffer!");
    gst_ml_frame_unmap (&inframe);
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  gst_ml_qnn_engine_execute (mlqnn->engine, &inframe, &outframe);

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (mlqnn, "Execute took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT
      " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  gst_ml_frame_unmap (&outframe);
  gst_ml_frame_unmap (&inframe);

  return GST_FLOW_OK;
}

void
gst_ml_qnn_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLQnn *mlqnn = GST_ML_QNN (object);

  switch (property_id) {
    case PROP_QNN_BACKEND:
      mlqnn->backend = g_strdup (g_value_get_string (value));
      break;
    case PROP_QNN_MODEL:
      mlqnn->model = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_ml_qnn_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLQnn *mlqnn = GST_ML_QNN (object);

  switch (property_id) {
    case PROP_QNN_BACKEND:
      g_value_set_string (value, mlqnn->backend);
      break;
    case PROP_QNN_MODEL:
      g_value_set_string (value, mlqnn->model);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_ml_qnn_finalize (GObject * object)
{
  GstMLQnn *mlqnn = GST_ML_QNN (object);

  if (mlqnn->outpool != NULL)
    gst_object_unref (mlqnn->outpool);

  gst_ml_qnn_engine_free (mlqnn->engine);
  mlqnn->engine = NULL;

  g_free (mlqnn->model);
  mlqnn->model = NULL;

  g_free(mlqnn->backend);
  mlqnn->backend = NULL;

  G_OBJECT_CLASS (gst_ml_qnn_parent_class)->finalize (object);
}

static void
gst_ml_qnn_class_init (GstMLQnnClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_ml_qnn_set_property;
  gobject_class->get_property = gst_ml_qnn_get_property;
  gobject_class->finalize = gst_ml_qnn_finalize;

  g_object_class_install_property (gobject_class, PROP_QNN_BACKEND,
      g_param_spec_string ("backend", "Backend", "Backend lib path",
          PROP_QNN_BACKEND_DEFAULT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QNN_MODEL,
      g_param_spec_string ("model", "Model", "Model file path",
          PROP_QNN_MODEL_DEFAULT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "QNN based ML plugin", "QNN", "QNN based ML plugin", "QTI");

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_ml_qnn_src_template ());
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_ml_qnn_sink_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_qnn_change_state);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_qnn_transform_caps);
  base->accept_caps = GST_DEBUG_FUNCPTR (gst_ml_qnn_accept_caps);

  base->decide_allocation = GST_DEBUG_FUNCPTR (gst_ml_qnn_decide_allocation);
  base->propose_allocation = GST_DEBUG_FUNCPTR (gst_ml_qnn_propose_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_qnn_prepare_output_buffer);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_qnn_transform);
}

static void
gst_ml_qnn_init (GstMLQnn * mlqnn)
{
  mlqnn->outpool = NULL;
  mlqnn->engine = NULL;

  mlqnn->model = PROP_QNN_MODEL_DEFAULT;
  mlqnn->backend = PROP_QNN_BACKEND_DEFAULT;

  GST_DEBUG_CATEGORY_INIT (gst_ml_qnn_debug, "qtimlqnn", 0,
      "QTI QNN ML plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlqnn", GST_RANK_NONE,
      GST_TYPE_ML_QNN);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlqnn,
    "QTI QNN based Machine Learnig plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
