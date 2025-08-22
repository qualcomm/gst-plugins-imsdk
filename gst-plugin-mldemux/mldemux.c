/*
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "mldemux.h"

#include <stdio.h>

#include <gst/ml/gstmlmeta.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>

#include "mldemuxpads.h"


#define GST_CAT_DEFAULT gst_ml_demux_debug
GST_DEBUG_CATEGORY (gst_ml_demux_debug);

#define gst_ml_demux_parent_class parent_class
G_DEFINE_TYPE (GstMLDemux, gst_ml_demux, GST_TYPE_ELEMENT);

#define GST_ML_DEMUX_TENSOR_TYPES \
  "{ INT8, UINT8, INT32, UINT32, FLOAT16, FLOAT32 }"

#define GST_ML_DEMUX_SINK_CAPS                   \
    "neural-network/tensors, "                   \
    "type = (string) " GST_ML_DEMUX_TENSOR_TYPES

#define GST_ML_DEMUX_SRC_CAPS                    \
    "neural-network/tensors, "                   \
    "type = (string) " GST_ML_DEMUX_TENSOR_TYPES

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_ml_demux_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_ML_DEMUX_SINK_CAPS)
    );

static GstStaticPadTemplate gst_ml_demux_src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_ML_DEMUX_SRC_CAPS)
    );


static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_ml_demux_src_pad_push_event (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstMLDemux *demux = GST_ML_DEMUX (element);
  GstEvent *event = GST_EVENT (userdata);

  // On EOS wait until all queued buffers have been pushed before propagating it.
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    GST_ML_DEMUX_PAD_WAIT_IDLE (GST_ML_DEMUX_SRCPAD_CAST (pad));

  GST_TRACE_OBJECT (demux, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_ref (event));
}

static GstCaps *
gst_ml_demux_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = NULL, *intersect = NULL;

  if (!(caps = gst_pad_get_current_caps (pad)))
    caps = gst_pad_get_pad_template_caps (pad);

  GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);
    intersect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersect;
  }


  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_ml_demux_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *tmplcaps = NULL;
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success &= gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with template!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ml_demux_sink_setcaps (GstMLDemux * demux, GstPad * pad, GstCaps * caps)
{
  GstCaps *srccaps = NULL, *filter = NULL, *intersect = NULL;
  GList *list = NULL;
  const GValue *value = NULL;
  GstMLInfo mlinfo;
  guint idx = 0, n_batch = 0;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  if (!gst_ml_info_from_caps (&mlinfo, caps)) {
    GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (GST_ML_DEMUX_SINKPAD_CAST (pad)->mlinfo != NULL)
    gst_ml_info_free (GST_ML_DEMUX_SINKPAD_CAST (pad)->mlinfo);

  GST_ML_DEMUX_SINKPAD_CAST (pad)->mlinfo = gst_ml_info_copy (&mlinfo);

  // Initialize batch size variable with the value of the 1st tensor.
  n_batch = GST_ML_INFO_TENSOR_DIM(&mlinfo, 0, 0);

  // Parsing happens by batch size, so all tensors must have the same batch size.
  for (idx = 0; idx < GST_ML_INFO_N_TENSORS (&mlinfo); idx++) {
    if (n_batch != GST_ML_INFO_TENSOR_DIM (&mlinfo, idx, 0)) {
      GST_ELEMENT_ERROR (demux, CORE, NEGOTIATION, (NULL),
          ("Mismatch between the tensor batch sizes!"));
      return FALSE;
    }

    // Set the batch size of the output tensors to 1, will be used later for caps.
    GST_ML_INFO_TENSOR_DIM (&mlinfo, idx, 0) = 1;
  }

  GST_ML_DEMUX_LOCK (demux);

  // Create new filter caps for source pads from the modified ML info.
  filter = gst_ml_info_to_caps (&mlinfo);

  // Extract the rate.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      "rate");

  // Propagate rate to the result caps if it exists.
  if (value != NULL)
    gst_caps_set_value (filter, "rate", value);

  for (list = demux->srcpads; list != NULL; list = g_list_next (list)) {
    GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD (list->data);

    // Get the negotiated caps between the srcpad and its peer.
    srccaps = gst_pad_get_allowed_caps (GST_PAD (srcpad));
    GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

    intersect = gst_caps_intersect (srccaps, filter);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

    gst_caps_unref (srccaps);
    srccaps = intersect;

    if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
      GST_ELEMENT_ERROR (demux, CORE, NEGOTIATION, (NULL),
          ("Source %s and sink caps do not intersect!", GST_PAD_NAME (srcpad)));

      if (intersect != NULL)
        gst_caps_unref (intersect);

      GST_ML_DEMUX_UNLOCK (demux);
      return FALSE;
    }

    if (!gst_pad_set_caps (GST_PAD (srcpad), srccaps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (demux), CORE, NEGOTIATION, (NULL),
          ("Failed to set caps to %s!", GST_PAD_NAME (srcpad)));
      gst_caps_unref (filter);

      GST_ML_DEMUX_UNLOCK (demux);
      return FALSE;
    }

    if (srcpad->mlinfo != NULL)
      gst_ml_info_free (srcpad->mlinfo);

    srcpad->mlinfo = gst_ml_info_copy (&mlinfo);

    GST_DEBUG_OBJECT (pad, "Negotiated caps at source pad %s: %" GST_PTR_FORMAT,
        GST_PAD_NAME (srcpad), srccaps);
  }

  gst_caps_unref (filter);

  GST_ML_DEMUX_UNLOCK (demux);

  return TRUE;
}

static GstMLDemuxSrcPad *
gst_ml_demux_find_srcpad (GstMLDemux * demux, const guint stream_id)
{
  GstMLDemuxSrcPad *srcpad = NULL;
  GList *list = NULL;

  for (list = demux->srcpads; list != NULL; list = g_list_next (list)) {
    srcpad = GST_ML_DEMUX_SRCPAD_CAST (list->data);

    if (srcpad->id == stream_id)
      return srcpad;
  }

  return NULL;
}

static void
gst_ml_demux_src_pad_worker_task (gpointer userdata)
{
  GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting %" GST_PTR_FORMAT, buffer);
    gst_pad_push (GST_PAD (srcpad), buffer);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

static GstFlowReturn
gst_ml_demux_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuffer)
{
  GstMLDemux *demux = GST_ML_DEMUX (parent);
  GstMLDemuxSrcPad *srcpad = NULL;
  GstProtectionMeta *pmeta = NULL;
  const GValue *value = NULL;
  guint batch_idx = 0, num = 0, n_batch = 0, n_memory = 0;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, inbuffer);

  n_batch = GST_ML_INFO_TENSOR_DIM (
      GST_ML_DEMUX_SINKPAD (demux->sinkpad)->mlinfo, 0, 0);
  n_memory = gst_buffer_n_memory (inbuffer);

  GST_ML_DEMUX_LOCK (demux);

  for (batch_idx = 0; batch_idx < n_batch; ++batch_idx) {
    GstStructure *structure = NULL;
    GstBuffer *outbuffer = NULL;
    GstDataQueueItem *item = NULL;

    pmeta = gst_buffer_get_protection_meta_id (inbuffer,
        gst_batch_channel_name (batch_idx));

    // No protection meta for this batch number, continue with next one.
    if (pmeta == NULL)
      continue;

    // No muxed stream ID (probably not a muxed stream tensor), continue.
    if ((value = gst_structure_get_value (pmeta->info, "stream-id")) == NULL)
      continue;

    // Get the stream ID for this batch and check if there is corresponding pad.
    if (!(srcpad = gst_ml_demux_find_srcpad (demux, g_value_get_int (value))))
      continue;

    // Create a new buffer wrapper to hold a reference to input buffer.
    outbuffer = gst_buffer_new ();

    // Extract the original stream timestamp.
    gst_structure_get_uint64 (pmeta->info, "stream-timestamp",
        &GST_BUFFER_TIMESTAMP (outbuffer));

    structure = gst_structure_new (gst_batch_channel_name (0),
        "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (outbuffer), NULL);

    value = gst_structure_get_value (pmeta->info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", value);

    value = gst_structure_get_value (pmeta->info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", value);

    if ((value = gst_structure_get_value (pmeta->info, "parent-id"))) {
      // Remove the stream ID prefix from the muxed ROI ID.
      gint id = g_value_get_int (value) && (~GST_MUX_STREAM_ID_MASK);
      gst_structure_set (structure, "parent-id", G_TYPE_INT, id, NULL);
    }

    if ((value = gst_structure_get_value (pmeta->info, "input-tensor-width")))
      gst_structure_set_value (structure, "input-tensor-width", value);

    if ((value = gst_structure_get_value (pmeta->info, "input-tensor-height")))
      gst_structure_set_value (structure, "input-tensor-height", value);

    if ((value = gst_structure_get_value (pmeta->info, "input-region-x")))
      gst_structure_set_value (structure, "input-region-x", value);

    if ((value = gst_structure_get_value (pmeta->info, "input-region-y")))
      gst_structure_set_value (structure, "input-region-y", value);

    if ((value = gst_structure_get_value (pmeta->info, "input-region-width")))
      gst_structure_set_value (structure, "input-region-width", value);

    if ((value = gst_structure_get_value (pmeta->info, "input-region-height")))
      gst_structure_set_value (structure, "input-region-height", value);

    // Transfer the batch protection meta into the buffer for this stream.
    pmeta = gst_buffer_add_protection_meta (outbuffer, structure);

      // Transfer the memory block for this batch number.
    for (num = 0; num < n_memory; ++num) {
      GstMemory *memory = gst_buffer_peek_memory (inbuffer, num);
      GstMLTensorMeta *mlmeta = NULL, *inpmlmeta = NULL;
      guint offset = 0, size = 0;

      // Set the size of memory that needs to be shared.
      size = gst_ml_info_tensor_size (srcpad->mlinfo, num);
      // Set the offset to the piece of memory that needs to be shared.
      offset = size * batch_idx;

      GST_TRACE_OBJECT (srcpad, "Transfering memory region %u with offset %u "
          "and size %u", num, offset, size);

      gst_buffer_append_memory (outbuffer, gst_memory_copy (memory, offset, size));

      mlmeta = gst_buffer_add_ml_tensor_meta (outbuffer, srcpad->mlinfo->type,
          srcpad->mlinfo->n_dimensions[num], srcpad->mlinfo->tensors[num]);
      mlmeta->id = num;

      // Get tensor name from mlmeta
      inpmlmeta = gst_buffer_get_ml_tensor_meta_id (inbuffer, num);
      if (inpmlmeta != NULL)
        mlmeta->name = inpmlmeta->name;
    }

    // If input is a GAP buffer set the GAP flag for the output buffer.
    if (gst_buffer_get_size (inbuffer) == 0 &&
        GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
      GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_GAP);

    // Initialize and send the source segment for synchronization.
    if (GST_FORMAT_UNDEFINED == srcpad->segment.format) {
      gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);

      srcpad->segment.start = 0;
      srcpad->segment.position = GST_BUFFER_TIMESTAMP (outbuffer);

      gst_pad_push_event (GST_PAD (srcpad),
          gst_event_new_segment (&(srcpad)->segment));
    }

    // Adjust the source pad segment position.
    srcpad->segment.position = GST_BUFFER_TIMESTAMP (outbuffer) +
        GST_BUFFER_DURATION (outbuffer);

    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (outbuffer);
    item->size = gst_buffer_get_size (outbuffer);
    item->duration = GST_BUFFER_DURATION (outbuffer);
    item->visible = TRUE;
    item->destroy = gst_data_queue_free_item;

    // Push the buffer into the queue or free it on failure.
    if (!gst_data_queue_push (srcpad->buffers, item))
      item->destroy (item);
  }

  GST_ML_DEMUX_UNLOCK (demux);

  // Reduce the reference count of the input buffer, it is no longer needed.
  gst_buffer_unref (inbuffer);

  return GST_FLOW_OK;
}

static gboolean
gst_ml_demux_sink_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_ml_demux_sink_getcaps (pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_ml_demux_sink_acceptcaps (pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_ml_demux_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMLDemux *demux = GST_ML_DEMUX (parent);
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_ml_demux_sink_setcaps (demux, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstMLDemuxSinkPad *sinkpad = GST_ML_DEMUX_SINKPAD (pad);
      GstSegment segment;

      gst_event_copy_segment (event, &segment);
      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(sinkpad)->segment, GST_FORMAT_TIME);
        sinkpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
        gst_segment_copy_into (&segment, &(sinkpad)->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }

      return TRUE;
    }
    case GST_EVENT_STREAM_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (demux),
          gst_ml_demux_src_pad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (demux),
          gst_ml_demux_src_pad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_STOP:
    {
      GstMLDemuxSinkPad *sinkpad = GST_ML_DEMUX_SINKPAD (pad);
      GList *list = NULL;

      GST_OBJECT_LOCK (demux);

      for (list = GST_ELEMENT (demux)->srcpads; list; list = list->next) {
        GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD (list->data);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);
      }

      GST_OBJECT_UNLOCK (demux);

      gst_segment_init (&(sinkpad)->segment, GST_FORMAT_UNDEFINED);

      success = gst_element_foreach_src_pad (GST_ELEMENT (demux),
          gst_ml_demux_src_pad_push_event, event);
      return success;
    }
    case GST_EVENT_EOS:
      success = gst_element_foreach_src_pad (GST_ELEMENT (demux),
          gst_ml_demux_src_pad_push_event, event);
      return success;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}


gboolean
gst_ml_demux_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_ml_demux_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (srcpad, "Current caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (srcpad, "Filter caps: %" GST_PTR_FORMAT, filter);

      if (filter != NULL) {
        GstCaps *intersection  =
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (caps);
        caps = intersection;
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &(srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (srcpad, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_SEGMENT:
    {
      GstSegment *segment = &(srcpad)->segment;
      gint64 start = 0, stop = 0;

      start = gst_segment_to_stream_time (segment, segment->format,
          segment->start);

      stop = (segment->stop == GST_CLOCK_TIME_NONE) ? segment->duration :
          gst_segment_to_stream_time (segment, segment->format, segment->stop);

      gst_query_set_segment (query, segment->rate, segment->format, start, stop);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_ml_demux_src_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  GST_INFO_OBJECT (pad, "%s worker task", active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (GST_ML_DEMUX_SRCPAD (pad)->buffers, FALSE);
        gst_data_queue_flush (GST_ML_DEMUX_SRCPAD (pad)->buffers);

        success = gst_pad_start_task (pad, gst_ml_demux_src_pad_worker_task,
            pad, NULL);
      } else {
        gst_data_queue_set_flushing (GST_ML_DEMUX_SRCPAD (pad)->buffers, TRUE);
        // TODO wait for all requests.
        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to %s worker task!",
        active ? "activate" : "deactivate");
    return FALSE;
  }

  GST_INFO_OBJECT (pad, "Worker task %s", active ? "activated" : "deactivated");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static GstPad*
gst_ml_demux_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstMLDemux *demux = GST_ML_DEMUX (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_ML_DEMUX_LOCK (demux);

  if (reqname && sscanf (reqname, "src_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= demux->nextidx) ? index + 1 : demux->nextidx;
  } else {
    index = demux->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  GST_ML_DEMUX_UNLOCK (demux);

  name = g_strdup_printf ("src_%u", index);

  pad = g_object_new (GST_TYPE_ML_DEMUX_SRCPAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (demux, "Failed to create source pad!");
    return NULL;
  }

  GST_ML_DEMUX_SRCPAD_CAST (pad)->id = index;

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_ml_demux_src_pad_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_ml_demux_src_pad_event));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR (gst_ml_demux_src_pad_activate_mode));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (demux, "Failed to add source pad!");
    gst_object_unref (pad);
    return NULL;
  }

  GST_ML_DEMUX_LOCK (demux);

  demux->srcpads = g_list_append (demux->srcpads, pad);
  demux->nextidx = nextindex;

  GST_ML_DEMUX_UNLOCK (demux);

  GST_DEBUG_OBJECT (demux, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_ml_demux_release_pad (GstElement * element, GstPad * pad)
{
  GstMLDemux *demux = GST_ML_DEMUX (element);

  GST_DEBUG_OBJECT (demux, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_ML_DEMUX_LOCK (demux);
  demux->srcpads = g_list_remove (demux->srcpads, pad);
  GST_ML_DEMUX_UNLOCK (demux);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_ml_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_ml_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_demux_finalize (GObject * object)
{
  GstMLDemux *demux = GST_ML_DEMUX (object);

  g_mutex_clear (&(demux)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (demux));
}

static void
gst_ml_demux_class_init (GstMLDemuxClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_ml_demux_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_ml_demux_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_ml_demux_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_ml_demux_sink_template, GST_TYPE_ML_DEMUX_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_ml_demux_src_template, GST_TYPE_ML_DEMUX_SRCPAD);

  gst_element_class_set_static_metadata (element,
      "Batching stream buffers", "Video/Audio/Muxer",
      "Batch buffers from multiple streams into one output buffer", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_ml_demux_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_ml_demux_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_demux_change_state);

  // Initializes a new ML demux GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_ml_demux_debug, "qtimldemux", 0, "QTI ML Demux");
}

static void
gst_ml_demux_init (GstMLDemux * demux)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&(demux)->lock);

  demux->nextidx = 0;
  demux->srcpads = NULL;

  template = gst_static_pad_template_get (&gst_ml_demux_sink_template);
  demux->sinkpad = g_object_new (GST_TYPE_ML_DEMUX_SINKPAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ml_demux_sink_chain));
  gst_pad_set_query_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ml_demux_sink_pad_query));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ml_demux_sink_pad_event));

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimldemux", GST_RANK_NONE,
      GST_TYPE_ML_DEMUX);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimldemux,
    "QTI ML Demux",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
