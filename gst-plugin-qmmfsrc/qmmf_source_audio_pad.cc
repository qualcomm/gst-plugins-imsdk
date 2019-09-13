/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#include "qmmf_source_audio_pad.h"

#include <gst/gstplugin.h>
#include <gst/gstelementfactory.h>
#include <gst/gstpadtemplate.h>

// Declare qmmfsrc_audio_pad_class_init() and qmmfsrc_audio_pad_init()
// functions, implement qmmfsrc_audio_pad_get_type() function and set
// qmmfsrc_audio_pad_parent_class variable.
G_DEFINE_TYPE(GstQmmfSrcAudioPad, qmmfsrc_audio_pad, GST_TYPE_PAD);

void
audio_pad_worker_task(GstPad *pad)
{
  GstDataQueue *buffers;
  GstDataQueueItem *item;
  GstBuffer *gstbuffer;

  buffers = GST_QMMFSRC_AUDIO_PAD(pad)->buffers;

  if (gst_data_queue_pop(buffers, &item)) {
    gstbuffer = GST_BUFFER(item->object);
    gst_pad_push(pad, gstbuffer);
    item->destroy(item);
  } else {
    GST_INFO_OBJECT(gst_pad_get_parent(pad), "Pause audio pad worker thread");
    gst_pad_pause_task(pad);
  }
}

static gboolean
audio_pad_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT(parent, "Received QUERY %s", GST_QUERY_TYPE_NAME(query));

  switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;

      // Minimum latency is the time to capture one audio frame.
      min_latency = GST_QMMFSRC_AUDIO_PAD(pad)->duration;

      // TODO This will change once GstBufferPool is implemented.
      max_latency = GST_CLOCK_TIME_NONE;

      GST_DEBUG_OBJECT(parent, "Latency %" GST_TIME_FORMAT "/%" GST_TIME_FORMAT,
          GST_TIME_ARGS(min_latency), GST_TIME_ARGS(max_latency));

      // We are always live, the minimum latency is 1 frame and
      // the maximum latency is the complete buffer of frames.
      gst_query_set_latency(query, TRUE, min_latency, max_latency);
      break;
    }
    default:
      success = gst_pad_query_default(pad, parent, query);
      break;
  }

  return success;
}

static gboolean
audio_pad_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT(parent, "Received EVENT %s", GST_EVENT_TYPE_NAME(event));

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_FLUSH_START:
      qmmfsrc_audio_pad_flush_buffers_queue(pad, TRUE);
      success = gst_pad_pause_task(pad);
      gst_event_unref(event);
      break;
    case GST_EVENT_FLUSH_STOP:
      qmmfsrc_audio_pad_flush_buffers_queue(pad, FALSE);
      success = gst_pad_start_task(
          pad, (GstTaskFunction)audio_pad_worker_task, pad, nullptr
      );
      gst_event_unref(event);
      break;
    case GST_EVENT_EOS:
      // After EOS, we should not send any more buffers, even if there are
      // more requests coming in.
      qmmfsrc_audio_pad_flush_buffers_queue(pad, TRUE);
      gst_event_unref(event);
      break;
    default:
      success = gst_pad_event_default(pad, parent, event);
      break;
  }
  return success;
}

static gboolean
audio_pad_activate_mode(GstPad *pad, GstObject *parent,
                        GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  GstSegment segment;
  GstEvent *event;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        qmmfsrc_audio_pad_flush_buffers_queue(pad, FALSE);
        success = gst_pad_start_task(
            pad, (GstTaskFunction)audio_pad_worker_task, pad, nullptr
        );
      } else {
        qmmfsrc_audio_pad_flush_buffers_queue(pad, TRUE);
        success = gst_pad_stop_task(pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT(parent, "Failed to activate audio pad task!");
    return success;
  }

  GST_DEBUG_OBJECT(parent, "Audio Pad (%u) mode: %s",
      GST_QMMFSRC_AUDIO_PAD(pad)->index, active ? "ACTIVE" : "STOPED");

  // Ensure segment (format) is properly setup.
  gst_segment_init(&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment(&segment);
  gst_pad_push_event(pad, event);

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode(pad, mode, active);
}

GstPad*
qmmfsrc_request_audio_pad(GstElement *element, GstPadTemplate *templ,
                          const gchar *name, const guint index)
{
  GstPad *srcpad = nullptr;

  GST_DEBUG_OBJECT(element, "Requesting audio pad %s (%d)", name, index);

  srcpad = GST_PAD(g_object_new(
      GST_TYPE_QMMFSRC_AUDIO_PAD,
      "name", name,
      "direction", templ->direction,
      "template", templ,
      nullptr
  ));

  if (srcpad == nullptr) {
    GST_ERROR_OBJECT(element, "Failed to create audio pad!");
    return nullptr;
  }
  GST_QMMFSRC_AUDIO_PAD(srcpad)->index = index;

  gst_pad_set_query_function(srcpad, GST_DEBUG_FUNCPTR(audio_pad_query));
  gst_pad_set_event_function(srcpad, GST_DEBUG_FUNCPTR(audio_pad_event));
  gst_pad_set_activatemode_function(
      srcpad, GST_DEBUG_FUNCPTR(audio_pad_activate_mode));

  gst_pad_use_fixed_caps(srcpad);
  gst_pad_set_active(srcpad, TRUE);
  gst_element_add_pad(element, srcpad);

  return srcpad;
}

void
qmmfsrc_release_audio_pad(GstElement *element, GstPad *pad)
{
  gchar *padname = GST_PAD_NAME(pad);
  guint index = GST_QMMFSRC_AUDIO_PAD(pad)->index;

  GST_DEBUG_OBJECT(element, "Releasing audio pad %s (%d)", padname, index);

  gst_object_ref(pad);
  gst_element_remove_pad(element, pad);
  gst_pad_set_active(pad, FALSE);
  gst_object_unref(pad);
}

void
qmmfsrc_audio_pad_flush_buffers_queue(GstPad *pad, gboolean flush)
{
  GST_INFO_OBJECT(gst_pad_get_parent(pad), "Flushing buffer queue: %s",
      flush ? "TRUE" : "FALSE");

  gst_data_queue_set_flushing(GST_QMMFSRC_AUDIO_PAD(pad)->buffers, flush);
  gst_data_queue_flush(GST_QMMFSRC_AUDIO_PAD(pad)->buffers);
}

static void
audio_pad_set_property(GObject *object, guint property_id,
                       const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
audio_pad_get_property(GObject *object, guint property_id,
                       GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
audio_pad_finalize(GObject *object)
{
  GstQmmfSrcAudioPad *pad = GST_QMMFSRC_AUDIO_PAD(object);

  if (pad->buffers != nullptr) {
    gst_data_queue_set_flushing(pad->buffers, TRUE);
    gst_data_queue_flush(pad->buffers);
    gst_object_unref(GST_OBJECT_CAST(pad->buffers));
    pad->buffers = nullptr;
  }

  G_OBJECT_CLASS(qmmfsrc_audio_pad_parent_class)->finalize(object);
}

static gboolean
queue_is_full_cb(GstDataQueue *queue, guint visible, guint bytes,
                 guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

// QMMF Source audio pad class initialization.
static void
qmmfsrc_audio_pad_class_init(GstQmmfSrcAudioPadClass* klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS(klass);

  gobject->get_property = GST_DEBUG_FUNCPTR(audio_pad_get_property);
  gobject->set_property = GST_DEBUG_FUNCPTR(audio_pad_set_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR(audio_pad_finalize);
}

// QMMF Source audio pad initialization.
static void
qmmfsrc_audio_pad_init(GstQmmfSrcAudioPad *pad)
{
  pad->index      = 0;

  pad->device     = -1;
  pad->channels   = -1;
  pad->samplerate = -1;
  pad->bitdepth   = -1;
  pad->format     = GST_AUDIO_FORMAT_UNKNOWN;

  pad->duration   = 0;
  pad->tsbase     = 0;

  pad->buffers    = gst_data_queue_new(queue_is_full_cb, nullptr, nullptr, pad);
}
