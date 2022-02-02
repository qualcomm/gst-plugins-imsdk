/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "mlaic.h"

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>


#define DEFAULT_PROP_MODEL         NULL
#define DEFAULT_PROP_N_ACTIVATIONS 1

#define DEFAULT_PROP_MIN_BUFFERS   48
#define DEFAULT_PROP_MAX_BUFFERS   48

#define GST_ML_AIC_TENSOR_TYPES "{ UINT8, INT32, FLOAT32 }"

#define GST_ML_AIC_CAPS                        \
    "neural-network/tensors, "                    \
    "type = (string) " GST_ML_AIC_TENSOR_TYPES

#define GST_CAT_DEFAULT gst_ml_aic_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_aic_debug);

#define gst_ml_aic_parent_class parent_class
G_DEFINE_TYPE (GstMLAic, gst_ml_aic, GST_TYPE_ELEMENT);

static GType gst_engine_request_get_type(void);
#define GST_TYPE_ENGINE_REQUEST  (gst_engine_request_get_type())
#define GST_ENGINE_REQUEST(obj) ((GstEngineRequest *) obj)

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_DEVICES,
  PROP_N_ACTIVATIONS,
};

typedef struct _GstEngineRequest GstEngineRequest;

struct _GstEngineRequest {
  GstMiniObject parent;

  // Request ID.
  gint          id;

  // Input frame submitted with provided ID.
  GstMLFrame    inframe;
  // Output frame submitted with provided ID.
  GstMLFrame    outframe;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstEngineRequest, gst_engine_request);

static void
gst_engine_request_free (GstEngineRequest * request)
{
  GstBuffer *buffer = NULL;

  buffer = request->inframe.buffer;
  if (buffer != NULL) {
    gst_ml_frame_unmap (&(request)->inframe);
    gst_buffer_unref (buffer);
  }

  buffer = request->outframe.buffer;
  if (buffer != NULL) {
    gst_ml_frame_unmap (&(request)->outframe);
    gst_buffer_unref (buffer);
  }

  g_free (request);
}

static GstEngineRequest *
gst_engine_request_new ()
{
  GstEngineRequest *request = g_new0 (GstEngineRequest, 1);

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_ENGINE_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_engine_request_free);

  request->id = -1;
  request->time = GST_CLOCK_TIME_NONE;

  return request;
}

static inline void
gst_engine_request_unref (GstEngineRequest * request)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (request));
}

static void
gst_ml_aic_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = data;
  gst_engine_request_unref (GST_ENGINE_REQUEST (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static GstStaticCaps gst_ml_aic_static_caps =
    GST_STATIC_CAPS (GST_ML_AIC_CAPS);

static GstCaps *
gst_ml_aic_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_aic_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_aic_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_aic_static_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_aic_src_template (void)
{
  return gst_pad_template_new ("src_%u", GST_PAD_SRC, GST_PAD_REQUEST,
      gst_ml_aic_src_caps ());
}

static GstPadTemplate *
gst_ml_aic_sink_template (void)
{
  return gst_pad_template_new ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      gst_ml_aic_sink_caps ());
}

static void
gst_buffer_copy_protection_meta (GstBuffer * outbuffer, GstBuffer * inbuffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (inbuffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    gst_buffer_add_protection_meta (outbuffer,
        gst_structure_copy (((GstProtectionMeta *) meta)->info));
  }
}

static GstPad *
gst_ml_aic_other_pad (GstPad * pad)
{
  GstElement *element = GST_ELEMENT (gst_pad_get_parent (pad));
  GstPad *otherpad = NULL;

  GST_OBJECT_LOCK (element);

  // Get the index of this pad, corresponding other pad has the same index.
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    guint index = g_list_index (element->srcpads, pad);
    otherpad = g_list_nth_data (element->sinkpads, index);
  } else if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    guint index = g_list_index (element->sinkpads, pad);
    otherpad = g_list_nth_data (element->srcpads, index);
  }

  if (otherpad != NULL)
    otherpad = GST_PAD (gst_object_ref (otherpad));

  GST_OBJECT_UNLOCK (element);

  gst_object_unref (element);
  return otherpad;
}

static GstBufferPool *
gst_ml_aic_create_pool (GstPad * pad, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

   GST_INFO_OBJECT (pad, "Uses ION memory");
   pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_ION);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_CONTINUOUS);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (pad, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  g_object_unref (allocator);
  return pool;
}


static gboolean
gst_ml_aic_propose_allocation (GstPad * pad, GstQuery * query)
{
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstMLInfo info;
  guint size = 0;
  gboolean needpool = FALSE;

  // Extract caps from the query.
  gst_query_parse_allocation (query, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (pad, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (pad, "Failed to get ML info!");
    return FALSE;
  }

  // Get the size from ML info.
  size = gst_ml_info_size (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    if ((pool = gst_ml_aic_create_pool (pad, caps)) == NULL) {
      GST_ERROR_OBJECT (pad, "Failed to create buffer pool!");
      return FALSE;
    }

    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size,
        DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (pad, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (query, needpool ? pool : NULL, size, 0, 0);

  // Invalidate the cached pool.
  if (pool != NULL)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_ML_TENSOR_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_ml_aic_decide_allocation (GstPad * pad, GstQuery * query)
{
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  guint size, minbuffers, maxbuffers;

  gst_query_parse_allocation (query, &caps, NULL);

  if (NULL == caps) {
    GST_ERROR_OBJECT (pad, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (GST_ML_AIC_SINKPAD (pad)->pool != NULL) {
    gst_buffer_pool_set_active (GST_ML_AIC_SINKPAD (pad)->pool, FALSE);
    gst_object_unref (GST_ML_AIC_SINKPAD (pad)->pool);
  }

  // Create a new buffer pool.
  if ((pool = gst_ml_aic_create_pool (pad, caps)) == NULL) {
    GST_ERROR_OBJECT (pad, "Failed to create buffer pool!");
    return FALSE;
  }

  GST_ML_AIC_SINKPAD (pad)->pool = pool;
  gst_buffer_pool_set_active (GST_ML_AIC_SINKPAD (pad)->pool, TRUE);

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

static gboolean
gst_ml_aic_prepare_output_buffer (GstPad * pad, GstBuffer * inbuffer,
    GstBuffer ** outbuffer)
{
  GstBufferPool *pool = GST_ML_AIC_SINKPAD (pad)->pool;
  GHashTable *bufpairs = GST_ML_AIC_SINKPAD (pad)->bufpairs;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (pad, "Failed to activate output video buffer pool!");
    return FALSE;
  }

  if (!g_hash_table_contains (bufpairs, inbuffer)) {
    // Retrieve new output buffer from the pool.
    if (gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (pad, "Failed to acquire output buffer!");
      return FALSE;
    }

    g_hash_table_insert (bufpairs, inbuffer, *outbuffer);
  } else {
    GstBufferList *blist = gst_buffer_list_new ();
    GstBuffer *buffer = NULL;

    // Get the corresponding output buffer from the buffer pair hash table.
    buffer =  g_hash_table_lookup (bufpairs, inbuffer);

    // Retrieve output buffer from the pool until the right one is found.
    while (buffer != *outbuffer) {
      if (gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
        GST_ERROR_OBJECT (pad, "Failed to acquire output buffer!");

        gst_buffer_list_unref (blist);
        return FALSE;
      }

      gst_buffer_list_insert (blist, -1, *outbuffer);
    }

    // Increase the reference count of the found buffer.
    *outbuffer = gst_buffer_ref (buffer);
    gst_buffer_list_unref (blist);
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Transfer GstProtectionMeta entries from input to the output buffer.
  gst_buffer_copy_protection_meta (*outbuffer, inbuffer);

  return TRUE;
}

static void
gst_ml_aic_src_worker_task (gpointer userdata)
{
  GstPad *pad = GST_PAD (userdata);
  GstMLAic *mlaic = GST_ML_AIC (gst_pad_get_parent (pad));
  GstDataQueueItem *item = NULL;
  GstEngineRequest *request = NULL;
  GstBuffer *buffer = NULL, *outbuffer = NULL;

  if (gst_data_queue_pop (GST_ML_AIC_SRCPAD (pad)->requests, &item)) {
    const GstMLInfo *mlinfo = NULL;
    GstMemory *memory = NULL;
    guint idx = 0, offset = 0, size = 0;

    // Increase the request reference count to indicate that it is in use.
    request = GST_ENGINE_REQUEST (gst_mini_object_ref (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (pad, "Waiting request %d", request->id);

    if (!gst_ml_aic_engine_wait_request (mlaic->engine, request->id)) {
      GST_DEBUG_OBJECT (pad, " Waiting request %d failed!", request->id);
      gst_engine_request_unref (request);
      return;
    }

    // Get time difference between current time and start.
    request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

    GST_LOG_OBJECT (pad, "Request %d took %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", request->id, GST_TIME_AS_MSECONDS (request->time),
        (GST_TIME_AS_USECONDS (request->time) % 1000));

    // Take a reference to the processed buffer for later use.
    buffer = gst_buffer_ref (request->outframe.buffer);
    // Decrease the request reference count as it is no longer needed.
    gst_engine_request_unref (request);

    // Create a new buffer wrapper to hold a reference to processed buffer.
    outbuffer = gst_buffer_new ();

    mlinfo = gst_ml_aic_engine_get_output_info (mlaic->engine);
    memory = gst_buffer_peek_memory (buffer, 0);

    // Share memory blocks from processed buffer with the new buffer.
    for (idx = 0; idx < GST_ML_INFO_N_TENSORS (mlinfo); idx++) {
      GstMLTensorMeta *mlmeta = NULL;

      // Set the size of memory that needs to be shared.
      size = gst_ml_info_tensor_size (mlinfo, idx);

      gst_buffer_append_memory (outbuffer,
          gst_memory_copy (memory, offset, size));

      // Set the offset to the next piece of memory that needs to be shared.
      offset += size;

      mlmeta = gst_buffer_add_ml_tensor_meta (outbuffer, mlinfo->type,
          mlinfo->n_dimensions[idx], mlinfo->tensors[idx]);
      mlmeta->id = idx;
    }

    // Copy the flags and timestamps from the processed buffer.
    gst_buffer_copy_into (outbuffer, buffer, GST_BUFFER_COPY_FLAGS |
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    // Transfer the GstProtectionMeta into the new buffer.
    gst_buffer_copy_protection_meta (outbuffer, buffer);

    // Reduce the reference count of the main buffer, it is no longer needed.
    gst_buffer_unref (buffer);

    GST_TRACE_OBJECT (pad, "Send buffer %p of size %" G_GSIZE_FORMAT,
        outbuffer, gst_buffer_get_size (outbuffer));

    gst_pad_push (pad, outbuffer);
  } else {
    GST_DEBUG_OBJECT (pad, "Paused worker thread");
    gst_pad_pause_task (pad);
  }
}

static GstCaps *
gst_ml_aic_query_caps (GstMLAic * mlaic, GstPad * pad, GstCaps * filter)
{
  GstPad *otherpad = NULL;
  GstCaps *caps = NULL, *othercaps = NULL;

  if (GST_PAD_DIRECTION (pad) == GST_PAD_UNKNOWN) {
    GST_ERROR_OBJECT (pad, "Unknown pad direction!");
    return NULL;
  }

  GST_DEBUG_OBJECT (pad, "Query caps: %" GST_PTR_FORMAT " in direction %s",
      caps, (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, filter);

  otherpad = gst_ml_aic_other_pad (pad);

  // Fetch the caps for current pad.
  if (mlaic->engine != NULL && GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    const GstMLInfo *mlinfo = gst_ml_aic_engine_get_input_info (mlaic->engine);
    caps = gst_ml_info_to_caps (mlinfo);
  } else if (mlaic->engine != NULL && GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    const GstMLInfo *mlinfo = gst_ml_aic_engine_get_output_info (mlaic->engine);
    caps = gst_ml_info_to_caps (mlinfo);
  } else if (!(caps = gst_pad_get_current_caps (pad))) {
    caps = gst_pad_get_pad_template_caps (pad);
  }

  // Fetch the caps from the other pad only if there is other pad.
  if (otherpad && !(othercaps = gst_pad_get_current_caps (otherpad)))
    othercaps = gst_pad_get_pad_template_caps (otherpad);

  // Propagate certain fields from the other pad caps.
  if (othercaps != NULL) {
    const GValue *value = NULL;

    // Extract the aspect ratio.
    value = gst_structure_get_value (gst_caps_get_structure (othercaps, 0),
        "aspect-ratio");

    // Propagate aspect ratio to the result caps if it exists.
    if (value != NULL)
      gst_caps_set_value (caps, "aspect-ratio", value);

    // Extract the rate.
    value = gst_structure_get_value (gst_caps_get_structure (othercaps, 0),
        "rate");

    // Propagate rate to the result caps if it exists.
    if (value != NULL)
      gst_caps_set_value (caps, "rate", value);

    gst_caps_unref (othercaps);
  }

  GST_DEBUG_OBJECT (pad, "ML caps: %" GST_PTR_FORMAT, caps);

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  if (otherpad != NULL)
    gst_object_unref (otherpad);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_ml_aic_accept_caps (GstMLAic * mlaic, GstPad * pad, GstCaps * caps)
{
  GstCaps *mlcaps = NULL;
  const GstMLInfo *mlinfo = NULL;

  GST_DEBUG_OBJECT (pad, "Accept caps: %" GST_PTR_FORMAT " in direction %s",
      caps, (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) ? "sink" : "src");

  if (NULL == mlaic->engine) {
    mlcaps = gst_pad_get_pad_template_caps (pad);
  } else if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    mlinfo = gst_ml_aic_engine_get_input_info (mlaic->engine);
  } else if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    mlinfo = gst_ml_aic_engine_get_output_info (mlaic->engine);
  }

  if ((mlinfo != NULL) && (NULL == mlcaps))
    mlcaps = gst_ml_info_to_caps (mlinfo);

  if (NULL == mlcaps) {
    GST_ERROR_OBJECT (pad, "Failed to get ML caps!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (pad, "ML caps: %" GST_PTR_FORMAT, mlcaps);

  if (!gst_caps_can_intersect (caps, mlcaps)) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect!");

    gst_caps_unref (mlcaps);
    return FALSE;
  }

  gst_caps_unref (mlcaps);
  return TRUE;
}

static gboolean
gst_ml_aic_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMLAic *mlaic = GST_ML_AIC (parent);

  GST_LOG_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      return gst_ml_aic_propose_allocation (pad, query);
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_ml_aic_query_caps (mlaic, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_ml_aic_accept_caps (mlaic, pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    case GST_QUERY_POSITION:
      {
        GstSegment *segment = NULL;
        GstFormat format = GST_FORMAT_UNDEFINED;
        gboolean success = TRUE;

        gst_query_parse_position (query, &format, NULL);

        GST_ML_AIC_SINKPAD_LOCK (pad);
        segment = &(GST_ML_AIC_SINKPAD (pad))->segment;

        if (format == GST_FORMAT_TIME && segment->format == GST_FORMAT_TIME) {
          gint64 position = gst_segment_to_stream_time (segment,
              GST_FORMAT_TIME, segment->position);
          gst_query_set_position (query, format, position);
        } else {
          GstPad *srcpad = gst_ml_aic_other_pad (pad);

          success = gst_pad_peer_query (srcpad, query);
          gst_object_unref (srcpad);
        }

        GST_ML_AIC_SINKPAD_UNLOCK (pad);
        return success;
      }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_ml_aic_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMLAic *mlaic = GST_ML_AIC (parent);
  GstPad *srcpad = NULL;

  GST_LOG_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  // Retrieve the corresponding source pad.
  srcpad = gst_ml_aic_other_pad (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *incaps = NULL, *outcaps = NULL, *peercaps = NULL, *intersect = NULL;
      GstQuery *query = NULL;
      const GValue *value = NULL;

      gst_event_parse_caps (event, &incaps);

      // Clear any pending reconfigure flag on corresponding source pad.
      gst_pad_check_reconfigure (srcpad);

      // Fetch the caps for current pad.
      if (mlaic->engine != NULL) {
        const GstMLInfo *mlinfo =
            gst_ml_aic_engine_get_output_info (mlaic->engine);
        outcaps = gst_ml_info_to_caps (mlinfo);
      } else if (!(outcaps = gst_pad_get_current_caps (srcpad))) {
        outcaps = gst_pad_get_pad_template_caps (srcpad);
      }

      // Extract the rate.
      value = gst_structure_get_value (gst_caps_get_structure (incaps, 0),
          "rate");

      // Propagate rate to the result caps if it exists.
      if (value != NULL)
        gst_caps_set_value (outcaps, "rate", value);

      // Query the source pad peer with its out caps as filter.
      peercaps = gst_pad_get_allowed_caps (srcpad);
      GST_DEBUG_OBJECT (pad, "Peer caps: %" GST_PTR_FORMAT, peercaps);

      intersect = gst_caps_intersect (peercaps, outcaps);
      GST_DEBUG_OBJECT (pad, "Intersected caps: %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (peercaps);
      gst_caps_unref (outcaps);

      if (gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Source and peer caps do not intersect!");
        gst_caps_unref (intersect);
        return FALSE;
      }

      outcaps = intersect;

      // Extract the aspect ratio.
      value = gst_structure_get_value (gst_caps_get_structure (incaps, 0),
          "aspect-ratio");

      // Propagate aspect ratio to the result caps if it exists.
      if (value != NULL)
        gst_caps_set_value (outcaps, "aspect-ratio", value);

      GST_DEBUG_OBJECT (pad, "Setting caps: %" GST_PTR_FORMAT, outcaps);

      if (!gst_pad_set_caps (srcpad, outcaps))
        gst_pad_mark_reconfigure (srcpad);

      // Query and decide buffer pool allocation.
      query = gst_query_new_allocation (outcaps, TRUE);

      if (!gst_pad_peer_query (srcpad, query))
        GST_DEBUG_OBJECT (pad, "Failed to query peer allocation!");

      gst_object_unref (srcpad);

      if (!gst_ml_aic_decide_allocation (pad, query)) {
        GST_ERROR_OBJECT (pad, "Failed to decide allocation!");

        gst_query_unref (query);
        return FALSE;
      }

      gst_query_unref (query);
      return TRUE;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;
      gboolean success = FALSE;

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(GST_ML_AIC_SINKPAD (pad))->segment);
        gst_segment_copy_into (&segment, &(GST_ML_AIC_SINKPAD (pad))->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }

      success = gst_pad_push_event (srcpad, event);
      gst_object_unref (srcpad);

      return success;
    }
    case GST_EVENT_FLUSH_START:
    {
      gboolean success = FALSE;

      gst_data_queue_set_flushing (GST_ML_AIC_SRCPAD (srcpad)->requests, TRUE);
      // TODO wait for all requests.

      success = gst_pad_push_event (srcpad, event);
      gst_object_unref (srcpad);

      return success;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      gboolean success = FALSE;

      gst_data_queue_set_flushing (GST_ML_AIC_SRCPAD (srcpad)->requests, FALSE);
      gst_segment_init (&(GST_ML_AIC_SINKPAD (pad))->segment,
          GST_FORMAT_UNDEFINED);

      success = gst_pad_push_event (srcpad, event);
      gst_object_unref (srcpad);

      return success;
    }
    case GST_EVENT_EOS:
    {
      gboolean success = FALSE;

      gst_data_queue_set_flushing (GST_ML_AIC_SRCPAD (srcpad)->requests, TRUE);
      // TODO wait for all requests.

      gst_segment_init (&(GST_ML_AIC_SINKPAD (pad))->segment,
          GST_FORMAT_UNDEFINED);

      success = gst_pad_push_event (srcpad, event);
      gst_object_unref (srcpad);

      return success;
    }
    case GST_EVENT_TAG:
    {
      gboolean success = FALSE;

      success = gst_pad_push_event (srcpad, event);
      gst_object_unref (srcpad);

      return success;
    }
    default:
      break;
  }

  // Release the reference to the corresponding source pad.
  gst_object_unref (srcpad);

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_ml_aic_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuffer)
{
  GstMLAic *mlaic = GST_ML_AIC (parent);
  GstEngineRequest *request = NULL;
  GstBuffer *outbuffer = NULL;
  const GstMLInfo * info = NULL;

  if (!gst_ml_aic_prepare_output_buffer (pad, inbuffer, &outbuffer)) {
    GST_ERROR_OBJECT (pad, "Failed to prepare output buffer!");
    return GST_FLOW_ERROR;
  }

  // Create new engine request.
  request = gst_engine_request_new ();

  info = gst_ml_aic_engine_get_input_info (mlaic->engine);

  // Create ML frame from input buffer.
  if (!gst_ml_frame_map (&(request)->inframe, info, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (pad, "Failed to map input buffer!");

    gst_engine_request_unref (request);
    return GST_FLOW_ERROR;
  }

  info = gst_ml_aic_engine_get_output_info (mlaic->engine);

  // Create ML frame from output buffer.
  if (!gst_ml_frame_map (&(request)->outframe, info, outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (pad, "Failed to map output buffer!");

    gst_engine_request_unref (request);
    return GST_FLOW_ERROR;
  }

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  request->id = gst_ml_aic_engine_submit_request (mlaic->engine,
      &(request)->inframe, &(request)->outframe);

  if (request->id == (-1)) {
    GST_WARNING_OBJECT (pad, "Failed to submit request to engine!");

    gst_engine_request_unref (request);
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (pad, "Submitted request %d", request->id);

  GST_ML_AIC_SINKPAD_LOCK (pad);
  GST_ML_AIC_SINKPAD (pad)->segment.position = GST_BUFFER_TIMESTAMP (outbuffer);
  GST_ML_AIC_SINKPAD_UNLOCK (pad);

  {
    GstPad *srcpad = NULL;
    GstDataQueueItem *item = NULL;

    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (request);
    item->visible = TRUE;
    item->destroy = gst_ml_aic_free_queue_item;

    // Retrieve the corresponding source pad.
    srcpad = gst_ml_aic_other_pad (pad);

    // Push the request into the queue or free it on failure.
    if (!gst_data_queue_push (GST_ML_AIC_SRCPAD (srcpad)->requests, item))
      item->destroy (item);

    gst_object_unref (srcpad);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_ml_aic_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = FALSE;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (GST_ML_AIC_SRCPAD (pad)->requests, FALSE);
        gst_data_queue_flush (GST_ML_AIC_SRCPAD (pad)->requests);

        success = gst_pad_start_task (pad, gst_ml_aic_src_worker_task, pad,
            NULL);
      } else {
        gst_data_queue_set_flushing (GST_ML_AIC_SRCPAD (pad)->requests, TRUE);
        // TODO wait for all requests.
        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to activate worker task!");
    return success;
  }

  GST_DEBUG_OBJECT (pad, "Mode: %s", active ? "ACTIVE" : "STOPED");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static gboolean
gst_ml_aic_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMLAic *mlaic = GST_ML_AIC (parent);

  GST_LOG_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_ml_aic_query_caps (mlaic, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_ml_aic_accept_caps (mlaic, pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_ml_aic_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_LOG_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

static GstPad*
gst_ml_aic_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstPad *newpad = NULL;
  gchar *name = NULL;
  GType type;

  GST_OBJECT_LOCK (element);

  if ((NULL == reqname) && (GST_PAD_SRC == templ->direction)) {
    guint index = element->numsrcpads;
    name = g_strdup_printf ("src_%u", index);
    type = GST_TYPE_ML_AIC_SRCPAD;
  } else if ((NULL == reqname) && (GST_PAD_SINK == templ->direction)) {
    guint index = element->numsinkpads;
    name = g_strdup_printf ("sink_%u", index);
    type = GST_TYPE_ML_AIC_SINKPAD;
  } else {
    // Use the requested pad name.
    name = g_strdup (reqname);
  }

  GST_OBJECT_UNLOCK (element);

  newpad = GST_PAD (g_object_new (
      type, "name", name,
      "direction", templ->direction,
      "template", templ,
      NULL
  ));

  g_free (name);

  if (newpad == NULL) {
    GST_ERROR_OBJECT (element, "Failed to create %s pad!",
        templ->direction == GST_PAD_SRC ? "src" : "sink");
    return NULL;
  }

  // Set pad functions.
  if (GST_PAD_SINK == templ->direction) {
    gst_pad_set_query_function (newpad,
        GST_DEBUG_FUNCPTR (gst_ml_aic_sink_query));
    gst_pad_set_event_function (newpad,
        GST_DEBUG_FUNCPTR (gst_ml_aic_sink_event));
    gst_pad_set_chain_function (newpad,
        GST_DEBUG_FUNCPTR (gst_ml_aic_sink_chain));
  } else if (GST_PAD_SRC == templ->direction) {
    gst_pad_set_activatemode_function (newpad,
        GST_DEBUG_FUNCPTR (gst_ml_aic_src_activate_mode));
    gst_pad_set_query_function (newpad,
        GST_DEBUG_FUNCPTR (gst_ml_aic_src_query));
    gst_pad_set_event_function (newpad,
        GST_DEBUG_FUNCPTR (gst_ml_aic_src_event));
  }

  GST_DEBUG_OBJECT (element, "Created %s:%s pad", GST_DEBUG_PAD_NAME (newpad));

  if (!gst_element_add_pad (element, newpad)) {
    GST_ERROR_OBJECT (element, "Failed to add %s:%s pad!",
        GST_DEBUG_PAD_NAME (newpad));

    gst_object_unref (newpad);
    return NULL;
  }

  return newpad;
}

static void
gst_ml_aic_release_pad (GstElement * element, GstPad * pad)
{
  GST_DEBUG_OBJECT (element, "Release %s:%s pad", GST_DEBUG_PAD_NAME (pad));
  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_ml_aic_change_state (GstElement * element, GstStateChange transition)
{
  GstMLAic *mlaic = GST_ML_AIC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GstStructure *settings = gst_structure_new_empty ("ml-engine-settings");
      GValue devices = G_VALUE_INIT;
      guint idx = 0;

      gst_structure_set (settings,
          GST_ML_AIC_ENGINE_OPT_MODEL, G_TYPE_STRING,
          mlaic->model,
          GST_ML_AIC_ENGINE_OPT_NUM_ACTIVATIONS, G_TYPE_UINT,
          mlaic->n_activations,
          NULL);

      g_value_init (&devices, GST_TYPE_ARRAY);

      for (idx = 0; idx < mlaic->devices->len; idx++) {
        GValue value = G_VALUE_INIT;

        g_value_init (&value, G_TYPE_UINT);
        g_value_set_uint (&value, g_array_index (mlaic->devices, guint, idx));

        gst_value_array_append_value (&devices, &value);
        g_value_unset (&value);
      }

      gst_structure_set_value (settings, GST_ML_AIC_ENGINE_OPT_DEVICES,
          &devices);
      g_value_unset (&devices);

      gst_ml_aic_engine_free (mlaic->engine);

      if ((mlaic->engine = gst_ml_aic_engine_new (settings)) == NULL) {
        GST_ERROR_OBJECT (mlaic, "Failed to create engine!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_ERROR_OBJECT (mlaic, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ml_aic_engine_free (mlaic->engine);
      mlaic->engine = NULL;
      break;
    default:
      // This is to catch PAUSED->PAUSED and PLAYING->PLAYING transitions.
      ret = (GST_STATE_TRANSITION_NEXT (transition) == GST_STATE_PAUSED) ?
          GST_STATE_CHANGE_NO_PREROLL : GST_STATE_CHANGE_SUCCESS;
      break;
  }

  return ret;
}

static void
gst_ml_aic_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLAic *mlaic = GST_ML_AIC (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_free (mlaic->model);
      mlaic->model = g_strdup (g_value_get_string (value));
      break;
    case PROP_DEVICES:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        guint val = g_value_get_int (gst_value_array_get_value (value, idx));
        g_array_append_val (mlaic->devices, val);
      }
      break;
    }
    case PROP_N_ACTIVATIONS:
      mlaic->n_activations = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_aic_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMLAic *mlaic = GST_ML_AIC (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_value_set_string (value, mlaic->model);
      break;
    case PROP_DEVICES:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_UINT);

      for (idx = 0; idx < mlaic->devices->len; idx++) {
        g_value_set_int (&val, g_array_index (mlaic->devices, guint, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    case PROP_N_ACTIVATIONS:
      g_value_set_uint (value, mlaic->n_activations);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_aic_finalize (GObject * object)
{
  GstMLAic *mlaic = GST_ML_AIC (object);

  if (mlaic->devices != NULL)
    g_array_free (mlaic->devices, TRUE);

  g_free (mlaic->model);

  gst_ml_aic_engine_free (mlaic->engine);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (mlaic));
}

static void
gst_ml_aic_class_init (GstMLAicClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_aic_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_aic_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_aic_finalize);

  g_object_class_install_property (gobject, PROP_MODEL,
      g_param_spec_string ("model", "Model",
          "Model filename", DEFAULT_PROP_MODEL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_DEVICES,
     gst_param_spec_array ("devices", "Devices",
          "List of AIC device IDs. ('<ID, ID, ID, ...>')",
          g_param_spec_int ("id", "Device ID",
              "AIC device ID.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_N_ACTIVATIONS,
      g_param_spec_uint ("activations", "Activations",
          "Number of activations (AIC programs and queues).",
          1, 10, DEFAULT_PROP_N_ACTIVATIONS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "AIC Machine Learning", "Filter/Effect/Converter",
      "AIC based Machine Learning plugin", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_aic_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_aic_src_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_aic_change_state);
  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_ml_aic_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_ml_aic_release_pad);
}

static void
gst_ml_aic_init (GstMLAic * mlaic)
{
  mlaic->engine = NULL;

  mlaic->model = DEFAULT_PROP_MODEL;
  mlaic->devices = g_array_new (FALSE, FALSE, sizeof (guint));
  mlaic->n_activations = DEFAULT_PROP_N_ACTIVATIONS;

  GST_DEBUG_CATEGORY_INIT (gst_ml_aic_debug, "qtimlaic", 0,
      "QTI AIC ML plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlaic", GST_RANK_NONE,
      GST_TYPE_ML_AIC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlaic,
    "QTI AIC based Machine Learnig plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
