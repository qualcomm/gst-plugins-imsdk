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
*/

#include "videocrop-video-pad.h"

#include <gst/gstplugin.h>
#include <gst/gstelementfactory.h>
#include <gst/gstpadtemplate.h>
#include <gst/allocators/allocators.h>

// Declare videocrop_video_pad_class_init() and videocrop_video_pad_init()
// functions, implement videocrop_video_pad_get_type() function and set
// videocrop_video_pad_parent_class variable.
G_DEFINE_TYPE(GstVideoCropVideoPad, videocrop_video_pad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (videocrop_video_pad_debug);
#define GST_CAT_DEFAULT videocrop_video_pad_debug

#define DEFAULT_VIDEO_STREAM_WIDTH    640
#define DEFAULT_VIDEO_STREAM_HEIGHT   480
#define DEFAULT_VIDEO_STREAM_FPS_NUM  30
#define DEFAULT_VIDEO_STREAM_FPS_DEN  1
#define DEFAULT_VIDEO_RAW_FORMAT      "NV12"

static void
video_pad_worker_task (GstPad * pad)
{
  GstDataQueue *buffers;
  GstDataQueueItem *item;

  buffers = GST_VIDEOCROP_VIDEO_PAD (pad)->buffers;

  if (gst_data_queue_pop (buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    gst_pad_push (pad, buffer);
  } else {
    GST_INFO_OBJECT (pad, "Pause video pad worker thread");
    gst_pad_pause_task (pad);
  }
}

static gboolean
video_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Received QUERY %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;

      // Minimum latency is the time to capture one video frame.
      min_latency = GST_VIDEOCROP_VIDEO_PAD (pad)->duration;

      // TODO This will change once GstBufferPool is implemented.
      max_latency = GST_CLOCK_TIME_NONE;

      GST_DEBUG_OBJECT (pad, "Latency %" GST_TIME_FORMAT "/%" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      // We are always live, the minimum latency is 1 frame and
      // the maximum latency is the complete buffer of frames.
      gst_query_set_latency (query, TRUE, min_latency, max_latency);
      break;
    }
    default:
      success = gst_pad_query_default (pad, parent, query);
      break;
  }

  return success;
}

static gboolean
video_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Received EVENT %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      videocrop_video_pad_flush_buffers_queue (pad, TRUE);
      success = gst_pad_pause_task (pad);
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      videocrop_video_pad_flush_buffers_queue (pad, FALSE);
      success = gst_pad_start_task (
          pad, (GstTaskFunction) video_pad_worker_task, pad, NULL);
      gst_event_unref (event);

      gst_segment_init (&GST_VIDEOCROP_VIDEO_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_EOS:
      // After EOS, we should not send any more buffers, even if there are
      // more requests coming in.
      videocrop_video_pad_flush_buffers_queue (pad, TRUE);
      gst_event_unref (event);

      gst_segment_init (&GST_VIDEOCROP_VIDEO_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);
      break;
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }
  return success;
}

static gboolean
video_pad_activate_mode (GstPad * pad, GstObject * parent, GstPadMode mode,
    gboolean active)
{
  gboolean success = FALSE;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        videocrop_video_pad_flush_buffers_queue (pad, FALSE);
        success = gst_pad_start_task (
            pad, (GstTaskFunction) video_pad_worker_task, pad, NULL);
      } else {
        videocrop_video_pad_flush_buffers_queue (pad, TRUE);
        success = gst_pad_stop_task (pad);

        gst_segment_init (&GST_VIDEOCROP_VIDEO_PAD (pad)->segment,
            GST_FORMAT_UNDEFINED);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to activate video pad task!");
    return success;
  }

  GST_DEBUG_OBJECT (pad, "Video Pad (%u) mode: %s",
      GST_VIDEOCROP_VIDEO_PAD (pad)->index, active ? "ACTIVE" : "STOPED");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
video_pad_update_params (GstPad * pad, GstStructure * structure)
{
  GstVideoCropVideoPad *vpad = GST_VIDEOCROP_VIDEO_PAD (pad);
  gint fps_n = 0, fps_d = 0;

  GST_VIDEOCROP_VIDEO_PAD_LOCK (pad);

  gst_structure_get_int (structure, "width", &vpad->width);
  gst_structure_get_int (structure, "height", &vpad->height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);

  vpad->duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  vpad->framerate = 1 / GST_TIME_AS_SECONDS (
      gst_guint64_to_gdouble (vpad->duration));

  if (gst_structure_has_name (structure, "video/x-raw")) {
    vpad->format = gst_video_format_from_string (
        gst_structure_get_string (structure, "format"));
  }

  GST_VIDEOCROP_VIDEO_PAD_UNLOCK (pad);
}

GstPad *
videocrop_request_video_pad (GstPadTemplate * templ, const gchar * name,
    const guint index)
{
  GstPad *srcpad = GST_PAD (g_object_new (
      GST_TYPE_VIDEOCROP_VIDEO_PAD,
      "name", name,
      "direction", templ->direction,
      "template", templ,
      NULL
  ));
  g_return_val_if_fail (srcpad != NULL, NULL);

  GST_VIDEOCROP_VIDEO_PAD (srcpad)->index = index;

  gst_pad_set_query_function (srcpad, GST_DEBUG_FUNCPTR (video_pad_query));
  gst_pad_set_event_function (srcpad, GST_DEBUG_FUNCPTR (video_pad_event));
  gst_pad_set_activatemode_function (
      srcpad, GST_DEBUG_FUNCPTR (video_pad_activate_mode));

  gst_pad_use_fixed_caps (srcpad);
  gst_pad_set_active (srcpad, TRUE);

  return srcpad;
}

void
videocrop_release_video_pad (GstElement * element, GstPad * pad)
{
  gst_object_ref (pad);

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);

  gst_object_unref (pad);
}

gboolean
videocrop_video_pad_fixate_caps (GstPad * pad)
{
  GstCaps *caps;
  GstStructure *structure;
  gint width = 0, height = 0;
  const GValue *framerate;

  // Get the negotiated caps between the pad and its peer.
  caps = gst_pad_get_allowed_caps (pad);
  g_return_val_if_fail (caps != NULL, FALSE);

  // Immediately return the fetched caps if they are fixed.
  if (gst_caps_is_fixed (caps)) {
    gst_pad_set_caps (pad, caps);
    GST_DEBUG_OBJECT (pad, "Caps fixated to: %" GST_PTR_FORMAT, caps);
    video_pad_update_params (pad, gst_caps_get_structure (caps, 0));
    return TRUE;
  }

  // Capabilities are not fixated, fixate them.
  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  framerate = gst_structure_get_value (structure, "framerate");

  if (!width) {
    gst_structure_set (structure, "width", G_TYPE_INT,
        DEFAULT_VIDEO_STREAM_WIDTH, NULL);
    GST_DEBUG_OBJECT (pad, "Width not set, using default value: %d",
        DEFAULT_VIDEO_STREAM_WIDTH);
  }

  if (!height) {
    gst_structure_set (structure, "height", G_TYPE_INT,
        DEFAULT_VIDEO_STREAM_HEIGHT, NULL);
    GST_DEBUG_OBJECT (pad, "Height not set, using default value: %d",
        DEFAULT_VIDEO_STREAM_HEIGHT);
  }

  if (!gst_value_is_fixed (framerate)) {
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_VIDEO_STREAM_FPS_NUM, DEFAULT_VIDEO_STREAM_FPS_DEN);
    GST_DEBUG_OBJECT (pad, "Framerate not set, using default value: %d/%d",
        DEFAULT_VIDEO_STREAM_FPS_NUM, DEFAULT_VIDEO_STREAM_FPS_DEN);
  }

  if (gst_structure_has_field (structure, "format")) {
    const gchar *format = gst_structure_get_string (structure, "format");

    if (!format) {
      gst_structure_fixate_field_string (structure, "format",
          DEFAULT_VIDEO_RAW_FORMAT);
      GST_DEBUG_OBJECT (pad, "Format not set, using default value: %s",
          DEFAULT_VIDEO_RAW_FORMAT);
    }
  }

  caps = gst_caps_fixate (caps);
  gst_pad_set_caps (pad, caps);

  GST_DEBUG_OBJECT (pad, "Caps fixated to: %" GST_PTR_FORMAT, caps);
  video_pad_update_params (pad, structure);
  return TRUE;
}

void
videocrop_video_pad_flush_buffers_queue (GstPad * pad, gboolean flush)
{
  GST_INFO_OBJECT (pad, "Flushing buffer queue: %s", flush ? "TRUE" : "FALSE");

  gst_data_queue_set_flushing (GST_VIDEOCROP_VIDEO_PAD (pad)->buffers, flush);
  gst_data_queue_flush (GST_VIDEOCROP_VIDEO_PAD (pad)->buffers);
}

static void
video_pad_finalize (GObject * object)
{
  GstVideoCropVideoPad *pad = GST_VIDEOCROP_VIDEO_PAD (object);

  if (pad->buffers != NULL) {
    gst_data_queue_set_flushing (pad->buffers, TRUE);
    gst_data_queue_flush (pad->buffers);
    gst_object_unref (GST_OBJECT_CAST (pad->buffers));
    pad->buffers = NULL;
  }

  G_OBJECT_CLASS (videocrop_video_pad_parent_class)->finalize(object);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

// VIDEOCROP Source video pad class initialization.
static void
videocrop_video_pad_class_init (GstVideoCropVideoPadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);

  gobject->finalize     = GST_DEBUG_FUNCPTR (video_pad_finalize);

  GST_DEBUG_CATEGORY_INIT (videocrop_video_pad_debug, "qtivideocrop", 0,
      "QTI VIDEOCROP Source video pad");
}

// VIDEOCROP Source video pad initialization.
static void
videocrop_video_pad_init (GstVideoCropVideoPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->index        = -1;

  pad->width        = -1;
  pad->height       = -1;
  pad->framerate    = 0.0;
  pad->format       = GST_VIDEO_FORMAT_UNKNOWN;

  pad->duration  = GST_CLOCK_TIME_NONE;

  pad->buffers   = gst_data_queue_new (queue_is_full_cb, NULL, NULL, pad);
}
