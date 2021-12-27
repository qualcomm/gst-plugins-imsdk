/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "batchpads.h"

G_DEFINE_TYPE(GstBatchSinkPad, gst_batch_sink_pad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstBatchSrcPad, gst_batch_src_pad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (gst_batch_debug);
#define GST_CAT_DEFAULT gst_batch_debug

#define GST_BINARY_8BIT_FORMAT "%c%c%c%c%c%c%c%c"
#define GST_BINARY_8BIT_STRING(x) \
  (x & 0x80 ? '1' : '0'), (x & 0x40 ? '1' : '0'), (x & 0x20 ? '1' : '0'), \
  (x & 0x10 ? '1' : '0'), (x & 0x08 ? '1' : '0'), (x & 0x04 ? '1' : '0'), \
  (x & 0x02 ? '1' : '0'), (x & 0x01 ? '1' : '0')


static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

void
gst_batch_sink_pad_flush_queue (GstPad * pad)
{
  GstBatchSinkPad *sinkpad = GST_BATCH_SINK_PAD (pad);

  GST_BATCH_SINK_LOCK (sinkpad);

  while (!g_queue_is_empty (sinkpad->queue)) {
    GstBuffer *buffer = g_queue_pop_head (sinkpad->queue);
    gst_buffer_unref (buffer);
  }

  GST_BATCH_SINK_UNLOCK (sinkpad);
}

static void
gst_batch_sink_pad_finalize (GObject * object)
{
  GstBatchSinkPad *pad = GST_BATCH_SINK_PAD (object);

  g_queue_free (pad->queue);

  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_batch_sink_pad_parent_class)->finalize(object);
}

void
gst_batch_sink_pad_class_init (GstBatchSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_batch_sink_pad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_batch_debug, "qtibatch", 0,
      "QTI Batch sink pads");
}

void
gst_batch_sink_pad_init (GstBatchSinkPad * pad)
{
  g_mutex_init (&pad->lock);
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->queue = g_queue_new ();
}


static void
gst_batch_src_pad_worker_task (gpointer userdata)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting buffer %p of size %" G_GSIZE_FORMAT
        " with %u memory blocks, channels mask " GST_BINARY_8BIT_FORMAT
        ", timestamp %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buffer,
        gst_buffer_get_size (buffer), gst_buffer_n_memory (buffer),
        GST_BINARY_8BIT_STRING (GST_BUFFER_OFFSET (buffer)),
        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

    gst_pad_push (GST_PAD (srcpad), buffer);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

gboolean
gst_batch_src_pad_activate_task (GstPad * pad, gboolean active)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (pad);
  GstTaskState state = GST_TASK_STOPPED;
  gboolean success = TRUE;

  state = gst_pad_get_task_state (GST_PAD (srcpad));

  GST_DEBUG_OBJECT (srcpad, "%s task", active ? "Activating" : "Deactivating");

  if (active && (state != GST_TASK_STARTED)) {
    gst_data_queue_set_flushing (srcpad->buffers, FALSE);
    gst_data_queue_flush (srcpad->buffers);

    success = gst_pad_start_task (
        GST_PAD (srcpad), gst_batch_src_pad_worker_task, srcpad, NULL);
  } else if (!active  && (state != GST_TASK_STOPPED)) {
    gst_data_queue_set_flushing (srcpad->buffers, TRUE);
    gst_data_queue_flush (srcpad->buffers);

    success = gst_pad_stop_task (GST_PAD (srcpad));

    GST_BATCH_SRC_LOCK (srcpad);
    gst_segment_init (&(srcpad)->segment, GST_FORMAT_UNDEFINED);
    srcpad->basetime = -1;
    GST_BATCH_SRC_UNLOCK (srcpad);
  }

  GST_DEBUG_OBJECT (srcpad, "%s task", active ? "Activated" : "Deactivated");
  return success;
}

gboolean
gst_batch_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (pad);

  GST_LOG_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_batch_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (pad);

  GST_LOG_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);

      GST_DEBUG_OBJECT (srcpad, "Current caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (srcpad, "Filter caps: %" GST_PTR_FORMAT, caps);

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
      GstSegment *segment = &srcpad->segment;
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
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_batch_src_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      success = gst_batch_src_pad_activate_task (pad, active);
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to %s task!",
        active ? "activate" : "deactivate");
    return FALSE;
  }

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
gst_batch_src_pad_finalize (GObject * object)
{
  GstBatchSrcPad *pad = GST_BATCH_SRC_PAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);
  gst_object_unref (GST_OBJECT_CAST (pad->buffers));

  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_batch_src_pad_parent_class)->finalize(object);
}

void
gst_batch_src_pad_class_init (GstBatchSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_batch_src_pad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_batch_debug, "qtibatch", 0,
      "QTI Batch src pads");
}

void
gst_batch_src_pad_init (GstBatchSrcPad * pad)
{
  g_mutex_init (&pad->lock);

  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->stmstart = FALSE;

  pad->duration = GST_CLOCK_TIME_NONE;
  pad->basetime = -1;

  pad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, NULL, pad);
}

