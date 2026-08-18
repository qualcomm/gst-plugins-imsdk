/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlqairt.h"

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_ml_qairt_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_qairt_debug);

#define gst_ml_qairt_parent_class parent_class
G_DEFINE_TYPE (GstMLQairt, gst_ml_qairt, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MODEL           NULL
#define DEFAULT_PROP_BACKEND         "libQairtCpu.so"
#define DEFAULT_PROP_EXEC_PRIORITY   GST_ML_QAIRT_EXEC_PRIORITY_NORMAL
#define DEFAULT_PROP_OUTPUTS         NULL

#define DEFAULT_PROP_MIN_BUFFERS 2
#define DEFAULT_PROP_MAX_BUFFERS 10

#define GST_ML_QAIRT_TENSOR_TYPES "{ INT8, UINT8, INT16, UINT16, INT32, UINT32, INT64, UINT64, FLOAT16, FLOAT32 }"

#define GST_ML_QAIRT_CAPS                       \
    "neural-network/tensors, "                  \
    "type = (string) " GST_ML_QAIRT_TENSOR_TYPES

#define RETRY_ON_FAILURE_CNT 3

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_BACKEND,
  PROP_EXEC_PRIORITY,
  PROP_LAYERS,
  PROP_TENSORS,
};

static GstStaticCaps gst_ml_qairt_static_caps =
    GST_STATIC_CAPS (GST_ML_QAIRT_CAPS);

static GstCaps *
gst_ml_qairt_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_qairt_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_qairt_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_qairt_caps ());
}

static GstPadTemplate *
gst_ml_qairt_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_qairt_caps ());
}

static GstBufferPool *
gst_ml_qairt_create_pool (GstMLQairt * qairt, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (qairt, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (qairt, "Uses DMA memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_DMA);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (qairt, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_qairt_propose_allocation (GstBaseTransform * base,
    GstQuery * inquery, GstQuery * outquery)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);

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
    GST_ERROR_OBJECT (qairt, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (qairt, "Failed to get ML info!");
    return FALSE;
  }

  // Get the size from ML info.
  size = gst_ml_info_size (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    if ((pool = gst_ml_qairt_create_pool (qairt, caps)) == NULL) {
      GST_ERROR_OBJECT (qairt, "Failed to create buffer pool!");
      return FALSE;
    }

    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (qairt, "Failed to set buffer pool configuration!");
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
gst_ml_qairt_decide_allocation (GstBaseTransform * base, GstQuery * query)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (qairt, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (qairt->outpool)
    gst_object_unref (qairt->outpool);

  // Create a new buffer pool.
  if ((pool = gst_ml_qairt_create_pool (qairt, caps)) == NULL) {
    GST_ERROR_OBJECT (qairt, "Failed to create buffer pool!");
    return FALSE;
  }

  qairt->outpool = pool;

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
gst_ml_qairt_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);
  GstBufferPool *pool = qairt->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (qairt, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (!qairt->engine) {
    GST_WARNING_OBJECT (qairt, "Engine not created!");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (qairt, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (qairt, "Failed to create output buffer!");
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
gst_ml_qairt_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);
  GstCaps *result = NULL;
  const GstMLInfo *mlinfo = NULL;
  const GValue *value = NULL;

  if ((NULL == qairt->engine) && (filter != NULL))
    return gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  else if (NULL == qairt->engine)
    return gst_caps_ref (caps);

  GST_DEBUG_OBJECT (qairt, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (qairt, "Filter caps: %" GST_PTR_FORMAT, filter);

  // The source and sink pads caps do not depend on each other so directly take
  // the ML caps from the engine for the corresponding pad and apply filter.
  switch (direction) {
    case GST_PAD_SRC:
      mlinfo = gst_ml_qairt_engine_get_input_info (qairt->engine);
      break;
    case GST_PAD_SINK:
      mlinfo = gst_ml_qairt_engine_get_output_info (qairt->engine);
      break;
    default:
      GST_ERROR_OBJECT (qairt, "Invalid pad direction!");
      return NULL;
  }

  result = gst_ml_info_to_caps (mlinfo);

  // Extract the rate.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

  // Propagate rate to the ML caps if it exists.
  if (value != NULL)
    gst_caps_set_value (result, "rate", value);

  GST_DEBUG_OBJECT (qairt, "ML caps: %" GST_PTR_FORMAT, result);

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (qairt, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_ml_qairt_accept_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);
  GstCaps *mlcaps = NULL;
  const GstMLInfo *mlinfo = NULL;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (qairt, "Accept caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  if ((NULL == qairt->engine) && (direction == GST_PAD_SINK)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));
  } else if ((NULL == qairt->engine) && (direction == GST_PAD_SRC)) {
    mlcaps = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (base));
  } else if (direction == GST_PAD_SINK) {
    mlinfo = gst_ml_qairt_engine_get_input_info (qairt->engine);
    mlcaps = gst_ml_info_to_caps (mlinfo);
  } else if (direction == GST_PAD_SRC) {
    mlinfo = gst_ml_qairt_engine_get_output_info (qairt->engine);
    mlcaps = gst_ml_info_to_caps (mlinfo);
  }

  if (NULL == mlcaps) {
    GST_ERROR_OBJECT (base, "Failed to get ML caps!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (qairt, "ML caps: %" GST_PTR_FORMAT, mlcaps);

  success = gst_caps_can_intersect (caps, mlcaps);
  gst_caps_unref (mlcaps);

  if (!success)
    GST_WARNING_OBJECT (base, "Caps can't intersect!");

  return success;
}

static gboolean
gst_ml_qairt_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, incaps)) {
    GST_ERROR_OBJECT (qairt, "Failed to get input ML info from caps!");
    return FALSE;
  }

  if (qairt->ininfo != NULL)
    gst_ml_info_free (qairt->ininfo);

  qairt->ininfo = gst_ml_info_copy (&info);
  GST_DEBUG_OBJECT (qairt, "Input caps: %" GST_PTR_FORMAT, incaps);

  if (!gst_ml_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (qairt, "Failed to get output ML info from caps!");
    return FALSE;
  }

  if (qairt->outinfo != NULL)
    gst_ml_info_free (qairt->outinfo);

  qairt->outinfo = gst_ml_info_copy (&info);
  GST_DEBUG_OBJECT (qairt, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstStateChangeReturn
gst_ml_qairt_change_state (GstElement * element, GstStateChange transition)
{
  GstMLQairt *qairt = GST_ML_QAIRT (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      qairt->engine = gst_ml_qairt_engine_new (&qairt->settings);
      if (NULL == qairt->engine) {
        GST_ERROR_OBJECT (qairt, "Failed to create engine!");
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
      gst_ml_qairt_engine_free (qairt->engine);
      qairt->engine = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ml_qairt_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLQairt *qairt = GST_ML_QAIRT (base);
  GstMLFrame inframe, outframe;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Create ML frame from input buffer.
  if (!gst_ml_frame_map (&inframe, qairt->ininfo, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (qairt, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  // Create ML frame from output buffer.
  if (!gst_ml_frame_map (&outframe, qairt->outinfo, outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (qairt, "Failed to map output buffer!");
    gst_ml_frame_unmap (&inframe);
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  for (guint i = 0; i < RETRY_ON_FAILURE_CNT && success == FALSE; i++) {
    success = gst_ml_qairt_engine_execute (qairt->engine, &inframe, &outframe);

    if (!success) {
      GST_ERROR_OBJECT (qairt, "Failed to execute inference, retrying %d/%d!",
        i + 1, RETRY_ON_FAILURE_CNT);
    }
  }

  ts_end = gst_util_get_timestamp ();

  gst_ml_frame_unmap (&outframe);
  gst_ml_frame_unmap (&inframe);

  if (!success) {
    GST_ERROR_OBJECT (qairt, "Failed to execute!");
    return GST_FLOW_ERROR;
  }

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (qairt, "Execute took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_qairt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLQairt *qairt = GST_ML_QAIRT (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_free (qairt->settings.modelfile);
      qairt->settings.modelfile = g_strdup (g_value_get_string (value));
      break;
    case PROP_BACKEND:
      g_free (qairt->settings.backend);
      qairt->settings.backend = g_strdup (g_value_get_string (value));
      break;
    case PROP_EXEC_PRIORITY:
      qairt->settings.priority = g_value_get_enum (value);
      break;
    case PROP_LAYERS:
    case PROP_TENSORS:
    {
      guint idx = 0;

      g_list_free_full (qairt->settings.outputs, (GDestroyNotify) g_free);
      qairt->settings.outputs = NULL;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        const gchar *name = g_value_get_string (
            gst_value_array_get_value (value, idx));
        qairt->settings.outputs = g_list_append (
            qairt->settings.outputs, g_strdup (name));
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_qairt_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMLQairt *qairt = GST_ML_QAIRT (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_value_set_string (value, qairt->settings.modelfile);
      break;
    case PROP_BACKEND:
      g_value_set_string (value, qairt->settings.backend);
      break;
    case PROP_EXEC_PRIORITY:
      g_value_set_enum (value, qairt->settings.priority);
      break;
    case PROP_LAYERS:
    case PROP_TENSORS:
    {
      GList *list = NULL;
      GValue val = G_VALUE_INIT;

      for (list = qairt->settings.outputs; list != NULL; list = list->next) {
        const gchar *name = list->data;

        g_value_init (&val, G_TYPE_STRING);
        g_value_set_string (&val, name);

        gst_value_array_append_value (value, &val);
        g_value_unset (&val);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_qairt_finalize (GObject * object)
{
  GstMLQairt *qairt = GST_ML_QAIRT (object);

  if (qairt->outinfo != NULL)
    gst_ml_info_free (qairt->outinfo);

  if (qairt->ininfo != NULL)
    gst_ml_info_free (qairt->ininfo);

  if (qairt->outpool != NULL)
    gst_object_unref (qairt->outpool);

  gst_ml_qairt_engine_free (qairt->engine);

  g_free (qairt->settings.modelfile);
  g_free (qairt->settings.backend);

  g_list_free_full (qairt->settings.outputs, (GDestroyNotify) g_free);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (qairt));
}

static void
gst_ml_qairt_class_init (GstMLQairtClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_qairt_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_qairt_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_qairt_finalize);

  g_object_class_install_property (gobject, PROP_MODEL,
      g_param_spec_string ("model", "Model",
          "Model file name. Either a QAIRT/SNPE '.dlc' container or a cached "
          "context '.bin' binary.", DEFAULT_PROP_MODEL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_BACKEND,
      g_param_spec_string ("backend", "Backend",
          "Backend lib name (e.g. libQairtHtp.so).",
          DEFAULT_PROP_BACKEND,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_EXEC_PRIORITY,
      g_param_spec_enum ("priority", "Execution Priority",
          "Sets a preference for execution priority. This allows the caller to "
          "give a coarse hint to the QAIRT runtime about the priority of the "
          "network.",
          GST_TYPE_ML_QAIRT_EXEC_PRIORITY, DEFAULT_PROP_EXEC_PRIORITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_LAYERS,
     gst_param_spec_array ("layers", "Layers",
          "List of output layers. Should be set if model has more than one output",
          g_param_spec_string ("name", "Layer Name",
              "Name of the output layer.", NULL,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_TENSORS,
     gst_param_spec_array ("tensors", "Tensors",
          "List of output tensors. Alternative to output layer list. "
          "The outputs will be generated in the order defined in this list.",
          g_param_spec_string ("name", "Tensor Name",
              "Name of the output tensor.", NULL,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "QAIRT Machine Learning", "Filter/Effect/Converter",
      "Qualcomm AI Runtime (QAIRT) based Machine Learning plugin", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_qairt_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_qairt_src_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_qairt_change_state);

  base->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_qairt_propose_allocation);
  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_qairt_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_qairt_prepare_output_buffer);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_qairt_transform_caps);
  base->accept_caps = GST_DEBUG_FUNCPTR (gst_ml_qairt_accept_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_qairt_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_qairt_transform);
}

static void
gst_ml_qairt_init (GstMLQairt * qairt)
{
  qairt->outpool = NULL;
  qairt->engine = NULL;

  qairt->settings.modelfile = DEFAULT_PROP_MODEL;
  qairt->settings.backend = NULL;
  qairt->settings.priority = DEFAULT_PROP_EXEC_PRIORITY;
  qairt->settings.outputs = DEFAULT_PROP_OUTPUTS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (qairt), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_qairt_debug, "qtimlqairt", 0,
      "QTI QAIRT ML plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlqairt", GST_RANK_NONE,
      GST_TYPE_ML_QAIRT);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlqairt,
    "QTI QAIRT based Machine Learning plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
