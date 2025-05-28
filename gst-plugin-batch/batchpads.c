/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

GST_DEBUG_CATEGORY_EXTERN (gst_batch_debug);
#define GST_CAT_DEFAULT gst_batch_debug

G_DEFINE_TYPE(GstBatchSinkPad, gst_batch_sink_pad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstBatchSrcPad, gst_batch_src_pad, GST_TYPE_PAD);

#define GST_BINARY_8BIT_FORMAT "%c%c%c%c%c%c%c%c"
#define GST_BINARY_8BIT_STRING(x) \
  (x & 0x80 ? '1' : '0'), (x & 0x40 ? '1' : '0'), (x & 0x20 ? '1' : '0'), \
  (x & 0x10 ? '1' : '0'), (x & 0x08 ? '1' : '0'), (x & 0x04 ? '1' : '0'), \
  (x & 0x02 ? '1' : '0'), (x & 0x01 ? '1' : '0')


static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD_CAST (checkdata);

  GST_BATCH_PAD_SIGNAL_IDLE (srcpad, FALSE);

  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
queue_empty_cb (GstDataQueue * queue, gpointer checkdata)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD_CAST (checkdata);
  GST_BATCH_PAD_SIGNAL_IDLE (srcpad, TRUE);
}

static void
gst_batch_sink_pad_finalize (GObject * object)
{
  GstBatchSinkPad *pad = GST_BATCH_SINK_PAD (object);

  g_queue_free_full (pad->buffers, (GDestroyNotify) gst_buffer_unref);

  G_OBJECT_CLASS (gst_batch_sink_pad_parent_class)->finalize(object);
}

void
gst_batch_sink_pad_class_init (GstBatchSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_batch_sink_pad_finalize);
}

void
gst_batch_sink_pad_init (GstBatchSinkPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->is_idle = TRUE;
  pad->buffers = g_queue_new ();
}

static void
gst_batch_src_pad_worker_task (gpointer userdata)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_peek (srcpad->buffers, &item)) {
    GstBuffer *buffer = NULL;

    // Take the buffer from the queue item and null the object pointer.
    buffer = GST_BUFFER (item->object);
    item->object = NULL;

    GST_TRACE_OBJECT (srcpad, "Pushing buffer %p of size %" G_GSIZE_FORMAT
        " with %u memory blocks, channels mask " GST_BINARY_8BIT_FORMAT
        ", timestamp %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
        " flags 0x%X", buffer, gst_buffer_get_size (buffer),
        gst_buffer_n_memory (buffer), GST_BINARY_8BIT_STRING (buffer->offset),
        GST_TIME_ARGS (buffer->pts), GST_TIME_ARGS (buffer->duration),
        GST_BUFFER_FLAGS (buffer));

    gst_pad_push (GST_PAD (srcpad), buffer);

    // Buffer was sent downstream, remove and free the item from the queue.
    if (gst_data_queue_pop (srcpad->buffers, &item))
      item->destroy (item);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

gboolean
gst_batch_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_batch_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (srcpad, "Template caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (srcpad, "Filter caps: %" GST_PTR_FORMAT, filter);

      if (filter != NULL) {
        GstCaps *intersection =
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
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (pad);
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (srcpad, "%s task", active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        gst_data_queue_set_flushing (srcpad->buffers, FALSE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_start_task (
            GST_PAD (srcpad), gst_batch_src_pad_worker_task, srcpad, NULL);
      } else if (!active) {
        gst_data_queue_set_flushing (srcpad->buffers, TRUE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_stop_task (GST_PAD (srcpad));
      }

      GST_DEBUG_OBJECT (srcpad, "Task %s", active ? "activated" : "deactivated");
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

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_batch_src_pad_parent_class)->finalize(object);
}

void
gst_batch_src_pad_class_init (GstBatchSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_batch_src_pad_finalize);
}

void
gst_batch_src_pad_init (GstBatchSrcPad * pad)
{
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->stmstart = FALSE;

  pad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
}

