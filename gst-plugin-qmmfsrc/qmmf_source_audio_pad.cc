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

GST_DEBUG_CATEGORY_STATIC (qmmfsrc_audio_pad_debug);
#define GST_CAT_DEFAULT qmmfsrc_audio_pad_debug

#define DEFAULT_AUDIO_STREAM_DEVICE_ID        1
#define DEFAULT_AUDIO_STREAM_CHANNELS         1
#define DEFAULT_AUDIO_STREAM_AAC_FORMAT       "adts"
#define DEFAULT_AUDIO_STREAM_AAC_SAMPLERATE   48000
#define DEFAULT_AUDIO_STREAM_AAC_BITDEPTH     16
#define DEFAULT_AUDIO_STREAM_AMR_SAMPLERATE   8000
#define DEFAULT_AUDIO_STREAM_AMR_BITDEPTH     32
#define DEFAULT_AUDIO_STREAM_AMRWB_SAMPLERATE 16000
#define DEFAULT_AUDIO_STREAM_AMRWB_BITDEPTH   32

enum
{
  PROP_0,
};

void
audio_pad_worker_task (GstPad * pad)
{
  GstDataQueue *buffers;
  GstDataQueueItem *item;
  GstBuffer *gstbuffer;

  buffers = GST_QMMFSRC_AUDIO_PAD (pad)->buffers;

  if (gst_data_queue_pop (buffers, &item)) {
    gstbuffer = GST_BUFFER (item->object);
    gst_pad_push (pad, gstbuffer);
    item->destroy (item);
  } else {
    GST_INFO_OBJECT (gst_pad_get_parent (pad), "Pause audio pad worker thread");
    gst_pad_pause_task (pad);
  }
}

static gboolean
audio_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (parent, "Received QUERY %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;

      // Minimum latency is the time to capture one audio frame.
      min_latency = GST_QMMFSRC_AUDIO_PAD (pad)->duration;

      // TODO This will change once GstBufferPool is implemented.
      max_latency = GST_CLOCK_TIME_NONE;

      GST_DEBUG_OBJECT (parent, "Latency %" GST_TIME_FORMAT "/%" GST_TIME_FORMAT,
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
audio_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (parent, "Received EVENT %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      qmmfsrc_audio_pad_flush_buffers_queue (pad, TRUE);
      success = gst_pad_pause_task (pad);
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      qmmfsrc_audio_pad_flush_buffers_queue (pad, FALSE);
      success = gst_pad_start_task (
          pad, (GstTaskFunction) audio_pad_worker_task, pad, NULL);
      gst_event_unref(event);
      break;
    case GST_EVENT_EOS:
      // After EOS, we should not send any more buffers, even if there are
      // more requests coming in.
      qmmfsrc_audio_pad_flush_buffers_queue (pad, TRUE);
      gst_event_unref (event);
      break;
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }
  return success;
}

static gboolean
audio_pad_activate_mode (GstPad * pad, GstObject * parent,
                         GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        qmmfsrc_audio_pad_flush_buffers_queue (pad, FALSE);
        success = gst_pad_start_task(
            pad, (GstTaskFunction) audio_pad_worker_task, pad, NULL);
      } else {
        qmmfsrc_audio_pad_flush_buffers_queue (pad, TRUE);
        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (parent, "Failed to activate audio pad task!");
    return success;
  }

  GST_DEBUG_OBJECT (parent, "Audio Pad (%u) mode: %s",
      GST_QMMFSRC_AUDIO_PAD (pad)->index, active ? "ACTIVE" : "STOPED");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
audio_pad_set_params (GstPad * pad, GstStructure *structure)
{
  GstQmmfSrcAudioPad *apad = GST_QMMFSRC_AUDIO_PAD (pad);

  GST_QMMFSRC_AUDIO_PAD_LOCK (pad);

  gst_structure_get_int (structure, "channels", &apad->channels);
  gst_structure_get_int (structure, "rate", &apad->samplerate);
  gst_structure_get_int (structure, "bitdepth", &apad->bitdepth);

  if (gst_structure_has_name (structure, "audio/mpeg")) {
    const gchar *type = NULL;

    apad->codec = GST_AUDIO_CODEC_TYPE_AAC;

    type = gst_structure_get_string (structure, "stream-format");
    gst_structure_set (apad->params, "type", G_TYPE_STRING, type, NULL);

    apad->duration = gst_util_uint64_scale_int (
        GST_SECOND, 1024, apad->samplerate);
  } else if (gst_structure_has_name (structure, "audio/AMR")) {
    apad->codec = GST_AUDIO_CODEC_TYPE_AMR;

    // AMR has a hardcoded framerate of 50 fps.
    apad->duration = gst_util_uint64_scale_int (GST_SECOND, 1, 50);
  } else if (gst_structure_has_name (structure, "audio/AMR-WB")) {
    apad->codec = GST_AUDIO_CODEC_TYPE_AMRWB;

    // AMR has a hardcoded framerate of 50 fps.
    apad->duration = gst_util_uint64_scale_int (GST_SECOND, 1, 50);
  }

  apad->format = GST_AUDIO_FORMAT_ENCODED;

  GST_QMMFSRC_AUDIO_PAD_UNLOCK (pad);
}

GstPad *
qmmfsrc_request_audio_pad (GstPadTemplate * templ, const gchar * name,
                           const guint index)
{
  GstPad *srcpad = GST_PAD (g_object_new (
      GST_TYPE_QMMFSRC_AUDIO_PAD,
      "name", name,
      "direction", templ->direction,
      "template", templ,
      NULL
  ));
  g_return_val_if_fail (srcpad != NULL, NULL);

  GST_QMMFSRC_AUDIO_PAD (srcpad)->index = index;

  gst_pad_set_query_function (srcpad, GST_DEBUG_FUNCPTR (audio_pad_query));
  gst_pad_set_event_function (srcpad, GST_DEBUG_FUNCPTR (audio_pad_event));
  gst_pad_set_activatemode_function (
      srcpad, GST_DEBUG_FUNCPTR (audio_pad_activate_mode));

  gst_pad_use_fixed_caps(srcpad);
  gst_pad_set_active(srcpad, TRUE);

  return srcpad;
}

void
qmmfsrc_release_audio_pad (GstElement * element, GstPad * pad)
{
  gchar *padname = GST_PAD_NAME (pad);
  guint index = GST_QMMFSRC_AUDIO_PAD (pad)->index;

  gst_object_ref (pad);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));
  gst_element_remove_pad (element, pad);
  gst_pad_set_active (pad, FALSE);

  gst_object_unref (pad);
}

void
qmmfsrc_audio_pad_flush_buffers_queue (GstPad * pad, gboolean flush)
{
  GST_INFO_OBJECT (gst_pad_get_parent (pad), "Flushing buffer queue: %s",
      flush ? "TRUE" : "FALSE");

  gst_data_queue_set_flushing (GST_QMMFSRC_AUDIO_PAD (pad)->buffers, flush);
  gst_data_queue_flush (GST_QMMFSRC_AUDIO_PAD (pad)->buffers);
}

gboolean
qmmfsrc_audio_pad_fixate_caps (GstPad * pad)
{
  GstCaps *caps;
  GstStructure *structure;
  gint channels = 0, samplerate = 0, bitdepth = 0;

  // Get the negotiated caps between the pad and its peer.
  caps = gst_pad_get_allowed_caps (pad);

  // Immediately return the fetched caps if they are fixed.
  if (gst_caps_is_fixed (caps)) {
    audio_pad_set_params (pad, gst_caps_get_structure (caps, 0));
    return TRUE;
  }

  // Capabilities are not fixated, fixate them.
  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &channels);
  gst_structure_get_int (structure, "rate", &samplerate);
  gst_structure_get_int (structure, "bitdepth", &bitdepth);

  if (!channels) {
    gst_structure_set (structure, "channels", G_TYPE_INT,
        DEFAULT_AUDIO_STREAM_CHANNELS, NULL);
    GST_DEBUG_OBJECT (pad, "Channels not set, using default value: %d",
        DEFAULT_AUDIO_STREAM_CHANNELS);
  }

  if (gst_structure_has_name (structure, "audio/mpeg")) {
    const gchar *type = NULL;

    type = gst_structure_get_string (structure, "stream-format");
    if (!type) {
      gst_structure_set (structure, "stream-format", G_TYPE_STRING,
          DEFAULT_AUDIO_STREAM_AAC_FORMAT, NULL);
      GST_DEBUG_OBJECT (pad, "Stream format not set, using default value: %s",
          DEFAULT_AUDIO_STREAM_AAC_FORMAT);
    }

    if (!samplerate) {
      gst_structure_set (structure, "rate", G_TYPE_INT,
          DEFAULT_AUDIO_STREAM_AAC_SAMPLERATE, NULL);
      GST_DEBUG_OBJECT (pad, "Sample rate not set, using default value: %d",
          DEFAULT_AUDIO_STREAM_AAC_SAMPLERATE);
    }

    if (!bitdepth) {
      gst_structure_set (structure, "bitdepth", G_TYPE_INT,
          DEFAULT_AUDIO_STREAM_AAC_BITDEPTH, NULL);
      GST_DEBUG_OBJECT (pad, "Bit depth not set, using default value: %d",
          DEFAULT_AUDIO_STREAM_AAC_BITDEPTH);
    }
  } else if (gst_structure_has_name (structure, "audio/AMR")) {
    if (!samplerate) {
      gst_structure_set (structure, "rate", G_TYPE_INT,
          DEFAULT_AUDIO_STREAM_AMR_SAMPLERATE, NULL);
      GST_DEBUG_OBJECT (pad, "Sample rate not set, using default value: %d",
          DEFAULT_AUDIO_STREAM_AMR_SAMPLERATE);
    }

    if (!bitdepth) {
      gst_structure_set (structure, "bitdepth", G_TYPE_INT,
          DEFAULT_AUDIO_STREAM_AMR_BITDEPTH, NULL);
      GST_DEBUG_OBJECT (pad, "Bit depth not set, using default value: %d",
          DEFAULT_AUDIO_STREAM_AMR_BITDEPTH);
    }
  } else if (gst_structure_has_name (structure, "audio/AMR-WB")) {
    if (!samplerate) {
      gst_structure_set (structure, "rate", G_TYPE_INT,
          DEFAULT_AUDIO_STREAM_AMRWB_SAMPLERATE, NULL);
      GST_DEBUG_OBJECT (pad, "Sample rate not set, using default value: %d",
          DEFAULT_AUDIO_STREAM_AMRWB_SAMPLERATE);
    }

    if (!bitdepth) {
      gst_structure_set (structure, "bitdepth", G_TYPE_INT,
          DEFAULT_AUDIO_STREAM_AMRWB_BITDEPTH, NULL);
      GST_DEBUG_OBJECT (pad, "Bit depth not set, using default value: %d",
          DEFAULT_AUDIO_STREAM_AMRWB_BITDEPTH);
    }
  }

  caps = gst_caps_fixate (caps);
  gst_pad_set_caps (pad, caps);

  audio_pad_set_params (pad, structure);
  return TRUE;
}


static void
audio_pad_set_property (GObject * object, guint property_id,
                        const GValue * value, GParamSpec * pspec)
{
  GstQmmfSrcAudioPad *pad = GST_QMMFSRC_AUDIO_PAD (object);

  GST_QMMFSRC_AUDIO_PAD_LOCK (pad);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_AUDIO_PAD_UNLOCK (pad);
}

static void
audio_pad_get_property (GObject * object, guint property_id,
                        GValue * value, GParamSpec * pspec)
{
  GstQmmfSrcAudioPad *pad = GST_QMMFSRC_AUDIO_PAD (object);

  GST_QMMFSRC_AUDIO_PAD_LOCK (pad);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_AUDIO_PAD_UNLOCK (pad);
}

static void
audio_pad_finalize (GObject * object)
{
  GstQmmfSrcAudioPad *pad = GST_QMMFSRC_AUDIO_PAD (object);

  if (pad->buffers != NULL) {
    gst_data_queue_set_flushing (pad->buffers, TRUE);
    gst_data_queue_flush (pad->buffers);
    gst_object_unref (GST_OBJECT_CAST(pad->buffers));
    pad->buffers = NULL;
  }

  if (pad->params != NULL) {
    gst_structure_free (pad->params);
    pad->params = NULL;
  }

  G_OBJECT_CLASS (qmmfsrc_audio_pad_parent_class)->finalize(object);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

// QMMF Source audio pad class initialization.
static void
qmmfsrc_audio_pad_class_init (GstQmmfSrcAudioPadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);

  gobject->get_property = GST_DEBUG_FUNCPTR (audio_pad_get_property);
  gobject->set_property = GST_DEBUG_FUNCPTR (audio_pad_set_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (audio_pad_finalize);

  GST_DEBUG_CATEGORY_INIT (qmmfsrc_audio_pad_debug, "qmmfsrc-audio", 0,
      "QTI QMMF Source audio pad");
}

// QMMF Source audio pad initialization.
static void
qmmfsrc_audio_pad_init (GstQmmfSrcAudioPad * pad)
{
  pad->index      = 0;

  pad->device     = DEFAULT_AUDIO_STREAM_DEVICE_ID;
  pad->channels   = -1;
  pad->samplerate = -1;
  pad->bitdepth   = -1;
  pad->format     = GST_AUDIO_FORMAT_UNKNOWN;
  pad->codec      = GST_AUDIO_CODEC_TYPE_UNKNOWN;
  pad->params     = gst_structure_new_empty ("codec-params");

  pad->duration   = 0;
  pad->tsbase     = 0;

  pad->buffers    = gst_data_queue_new (queue_is_full_cb, NULL, NULL, pad);
}
