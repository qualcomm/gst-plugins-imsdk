/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "heifmuxpads.h"

GST_DEBUG_CATEGORY_EXTERN (gst_heifmux_debug);
#define GST_CAT_DEFAULT gst_heifmux_debug

G_DEFINE_TYPE(GstHeifMuxSinkPad, gst_heifmux_sink_pad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstHeifMuxSrcPad, gst_heifmux_src_pad, GST_TYPE_PAD);

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstPad *pad = GST_PAD (checkdata);

  if (GST_IS_HEIFMUX_SRC_PAD (pad)) {
    GstHeifMuxSrcPad *srcpad = GST_HEIFMUX_SRC_PAD_CAST (pad);
    GST_HEIFMUX_PAD_SIGNAL_IDLE (srcpad, FALSE);

    // Limiting the output queue
    if (visible >= srcpad->buffers_limit) {
      GST_DEBUG_OBJECT (pad, "Queue limit reached of %d buffers!",
          srcpad->buffers_limit);
      return TRUE;
    }
  } else if (GST_IS_HEIFMUX_SINK_PAD (pad)) {
    GstHeifMuxSinkPad *sinkpad = GST_HEIFMUX_SINK_PAD_CAST (pad);
    GST_HEIFMUX_PAD_SIGNAL_IDLE (sinkpad, FALSE);

    // Limiting the input queue
    if (visible >= sinkpad->buffers_limit) {
      GST_DEBUG_OBJECT (pad, "Queue limit reached of %d buffers!",
          sinkpad->buffers_limit);
      return TRUE;
    }
  }

  return FALSE;
}

static void
queue_empty_cb (GstDataQueue * queue, gpointer checkdata)
{
  GstPad *pad = GST_PAD (checkdata);

  if (GST_IS_HEIFMUX_SRC_PAD (pad)) {
    GstHeifMuxSrcPad *srcpad = GST_HEIFMUX_SRC_PAD_CAST (pad);
    GST_HEIFMUX_PAD_SIGNAL_IDLE (srcpad, TRUE);
  } else if (GST_IS_HEIFMUX_SINK_PAD (pad)) {
    GstHeifMuxSinkPad *sinkpad = GST_HEIFMUX_SINK_PAD_CAST (pad);
    GST_HEIFMUX_PAD_SIGNAL_IDLE (sinkpad, TRUE);
  }
}

static void
gst_heifmux_src_pad_worker_task (gpointer userdata)
{
  GstHeifMuxSrcPad *srcpad = GST_HEIFMUX_SRC_PAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_peek (srcpad->buffers, &item)) {
    GstBuffer *buffer = NULL;

    // Take the buffer from the queue item and null the object pointer.
    buffer = GST_BUFFER (item->object);
    item->object = NULL;

    GST_TRACE_OBJECT (srcpad, "Pushing %" GST_PTR_FORMAT, buffer);
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
gst_heifmux_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_LOG_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_heifmux_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstHeifMuxSrcPad *srcpad = GST_HEIFMUX_SRC_PAD (pad);

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
      GstSegment *segment = NULL;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (srcpad, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      GST_HEIFMUX_SRC_LOCK (srcpad);

      segment = &(srcpad)->segment;

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));

      GST_HEIFMUX_SRC_UNLOCK (srcpad);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_heifmux_src_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstHeifMuxSrcPad *srcpad = GST_HEIFMUX_SRC_PAD (pad);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
    {
      GstTaskState state = gst_pad_get_task_state (pad);
      gboolean success = FALSE;

      GST_INFO_OBJECT (srcpad, "%s task", active ? "Activating" : "Deactivating");

      if (active && (state != GST_TASK_STARTED)) {
        gst_data_queue_set_flushing (srcpad->buffers, FALSE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_start_task (GST_PAD (srcpad),
            gst_heifmux_src_pad_worker_task, srcpad, NULL);
      } else if (!active  && (state != GST_TASK_STOPPED)) {
        gst_data_queue_set_flushing (srcpad->buffers, TRUE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_stop_task (GST_PAD (srcpad));

        GST_HEIFMUX_SRC_LOCK (srcpad);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_UNDEFINED);
        GST_HEIFMUX_SRC_UNLOCK (srcpad);
      }

      if (!success) {
        GST_ERROR_OBJECT (pad, "Failed to %s task!",
            active ? "activate" : "deactivate");
        return FALSE;
      }

      GST_INFO_OBJECT (srcpad, "Task %s", active ? "activated" : "deactivated");
      break;
    }
    default:
      break;
  }

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
gst_heifmux_sink_pad_finalize (GObject * object)
{
  GstHeifMuxSinkPad *pad = GST_HEIFMUX_SINK_PAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);
  gst_object_unref (GST_OBJECT_CAST (pad->buffers));

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_heifmux_sink_pad_parent_class)->finalize(object);
}

void
gst_heifmux_sink_pad_class_init (GstHeifMuxSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_heifmux_sink_pad_finalize);
}

void
gst_heifmux_sink_pad_init (GstHeifMuxSinkPad * pad)
{
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  pad->vinfo = NULL;
  pad->buffers =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
  pad->buffers_limit = 0;
}

static void
gst_heifmux_src_pad_finalize (GObject * object)
{
  GstHeifMuxSrcPad *pad = GST_HEIFMUX_SRC_PAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);
  gst_object_unref (GST_OBJECT_CAST (pad->buffers));

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_heifmux_src_pad_parent_class)->finalize(object);
}

void
gst_heifmux_src_pad_class_init (GstHeifMuxSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_heifmux_src_pad_finalize);
}

void
gst_heifmux_src_pad_init (GstHeifMuxSrcPad * pad)
{
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->buffers =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
  pad->buffers_limit = 0;
}
