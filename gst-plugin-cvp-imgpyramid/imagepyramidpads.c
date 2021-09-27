/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "imagepyramidpads.h"

G_DEFINE_TYPE (GstCvpImgPyramidSinkPad, gst_cvp_imgpyramid_sinkpad, GST_TYPE_PAD);
G_DEFINE_TYPE (GstCvpImgPyramidSrcPad, gst_cvp_imgpyramid_srcpad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (gst_cvp_imgpyramid_debug);
#define GST_CAT_DEFAULT gst_cvp_imgpyramid_debug

#define DEFAULT_VIDEO_STREAM_FPS_NUM  30
#define DEFAULT_VIDEO_STREAM_FPS_DEN  1
#define DEFAULT_VIDEO_RAW_FORMAT      "GRAY8"

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

// Source pad implementation
static void
gst_cvp_imgpyramid_srcpad_worker_task (gpointer userdata)
{
  GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting %" GST_PTR_FORMAT, buffer);

    // Adjust the source pad segment position.
    srcpad->segment.position = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);

    gst_pad_push (GST_PAD (srcpad), buffer);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

gboolean
gst_cvp_imgpyramid_srcpad_push_event (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstEvent *event = GST_EVENT (userdata);

  GST_TRACE_OBJECT (pad, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_ref (event));
}

gboolean
gst_cvp_imgpyramid_srcpad_setcaps (GstCvpImgPyramidSrcPad * srcpad)
{
  GstCaps *outcaps = NULL;
  GstStructure *structure;
  gint width = 0, height = 0;
  const GValue *framerate;

  // Get the negotiated caps between the pad and its peer.
  outcaps = gst_pad_get_allowed_caps (GST_PAD (srcpad));
  g_return_val_if_fail (outcaps != NULL, FALSE);

  // Immediately return the fetched caps if they are fixed.
  if (gst_caps_is_fixed (outcaps)) {
    gst_pad_set_caps (GST_PAD (srcpad), outcaps);
    GST_DEBUG_OBJECT (srcpad, "Caps fixated to: %" GST_PTR_FORMAT, outcaps);
    return TRUE;
  }

  // Capabilities are not fixated, fixate them.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);
  structure = gst_caps_get_structure (outcaps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  framerate = gst_structure_get_value (structure, "framerate");

  // Set width and height to range if not present
  if (!width) {
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE,
        1, G_MAXINT, NULL);
  }

  if (!height) {
    gst_structure_set (structure, "height", GST_TYPE_INT_RANGE,
        1, G_MAXINT, NULL);
  }

  if (!gst_value_is_fixed (framerate)) {
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_VIDEO_STREAM_FPS_NUM, DEFAULT_VIDEO_STREAM_FPS_DEN);
  }

  if (gst_structure_has_field (structure, "format")) {
    const gchar *format = gst_structure_get_string (structure, "format");

    if (!format) {
      gst_structure_fixate_field_string (structure, "format",
          DEFAULT_VIDEO_RAW_FORMAT);
      GST_DEBUG_OBJECT (srcpad, "Format not set, using default value: %s",
          DEFAULT_VIDEO_RAW_FORMAT);
    }
  }

  outcaps = gst_caps_fixate (outcaps);
  gst_pad_set_caps (GST_PAD (srcpad), outcaps);

  GST_DEBUG_OBJECT (srcpad, "Caps fixated to: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

gboolean
gst_cvp_imgpyramid_srcpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
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

      gst_query_set_segment (query, segment->rate, segment->format, start,
          stop);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_cvp_imgpyramid_srcpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_cvp_imgpyramid_srcpad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  GST_INFO_OBJECT (pad, "%s worker task",
      active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (GST_CVP_IMGPYRAMID_SRCPAD (pad)->buffers,
            FALSE);
        gst_data_queue_flush (GST_CVP_IMGPYRAMID_SRCPAD (pad)->buffers);

        success = gst_pad_start_task (pad, gst_cvp_imgpyramid_srcpad_worker_task,
            pad, NULL);
      } else {
        gst_data_queue_set_flushing (GST_CVP_IMGPYRAMID_SRCPAD (pad)->buffers,
            TRUE);
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

static void
gst_cvp_imgpyramid_srcpad_finalize (GObject * object)
{
  GstCvpImgPyramidSrcPad *pad = GST_CVP_IMGPYRAMID_SRCPAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);

  gst_object_unref (GST_OBJECT_CAST (pad->buffers));

  G_OBJECT_CLASS (gst_cvp_imgpyramid_srcpad_parent_class)->finalize (object);
}

void
gst_cvp_imgpyramid_srcpad_class_init (GstCvpImgPyramidSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_srcpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_cvp_imgpyramid_debug, "qticvpimgpyramid", 0,
      "QTI CVP pyramid scaler src pad");
}

void
gst_cvp_imgpyramid_srcpad_init (GstCvpImgPyramidSrcPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
}

// Sink pad
static void
gst_cvp_imgpyramid_sinkpad_finalize (GObject * object)
{
  GstCvpImgPyramidSinkPad *pad = GST_CVP_IMGPYRAMID_SINKPAD (object);

  gst_data_queue_set_flushing (pad->requests, TRUE);
  gst_data_queue_flush (pad->requests);

  gst_object_unref (GST_OBJECT_CAST (pad->requests));

  G_OBJECT_CLASS (gst_cvp_imgpyramid_sinkpad_parent_class)->finalize (object);
}

void
gst_cvp_imgpyramid_sinkpad_class_init (GstCvpImgPyramidSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_sinkpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_cvp_imgpyramid_debug, "qticvpimgpyramid", 0,
      "QTI CVP/EVA sink pad");
}

void
gst_cvp_imgpyramid_sinkpad_init (GstCvpImgPyramidSinkPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->info = NULL;

  pad->requests = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  gst_data_queue_set_flushing (pad->requests, FALSE);
}