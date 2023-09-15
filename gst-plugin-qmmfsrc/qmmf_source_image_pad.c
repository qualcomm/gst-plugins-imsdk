/*
* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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
*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "qmmf_source_image_pad.h"

#include <gst/gstplugin.h>
#include <gst/gstelementfactory.h>
#include <gst/gstpadtemplate.h>
#include <gst/allocators/allocators.h>

#include "qmmf_source_utils.h"

// Declare qmmfsrc_image_pad_class_init() and qmmfsrc_image_pad_init()
// functions, implement qmmfsrc_image_pad_get_type() function and set
// qmmfsrc_image_pad_parent_class variable.
G_DEFINE_TYPE(GstQmmfSrcImagePad, qmmfsrc_image_pad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (qmmfsrc_image_pad_debug);
#define GST_CAT_DEFAULT qmmfsrc_image_pad_debug

#define DEFAULT_IMAGE_STREAM_WIDTH   640
#define DEFAULT_IMAGE_STREAM_HEIGHT  480
#define DEFAULT_IMAGE_STREAM_FPS_NUM 30
#define DEFAULT_IMAGE_STREAM_FPS_DEN 1
#define DEFAULT_IMAGE_RAW_FORMAT     "NV21"
#define DEFAULT_IMAGE_BAYER_FORMAT   "bggr"
#define DEFAULT_IMAGE_BAYER_BPP      "10"

#define DEFAULT_PROP_QUALITY            85
#define DEFAULT_PROP_THUMBNAIL_WIDTH    0
#define DEFAULT_PROP_THUMBNAIL_HEIGHT   0
#define DEFAULT_PROP_THUMBNAIL_QUALITY  85
#define DEFAULT_PROP_SCREENNAIL_WIDTH   0
#define DEFAULT_PROP_SCREENNAIL_HEIGHT  0
#define DEFAULT_PROP_SCREENNAIL_QUALITY 85
#define DEFAULT_PROP_ROTATE             ROTATE_NONE

enum
{
  SIGNAL_PAD_RECONFIGURE,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_IMAGE_ROTATE,
};

static guint signals[LAST_SIGNAL];

static void
image_pad_send_stream_start (GstPad * pad)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  gchar *stream_id = NULL;
  gchar *pad_name  = NULL;
  GstEvent *event  = NULL;

  if (!ipad->stream_start)
    return;

  pad_name = gst_pad_get_name (pad);
  stream_id =  g_strconcat ("qmmfsrc/", pad_name, NULL);

  GST_DEBUG_OBJECT (pad, "Pushing STREAM_START");
  event = gst_event_new_stream_start (stream_id);
  gst_event_set_group_id (event, gst_util_group_id_next ());

  gst_pad_push_event (pad, event);
  ipad->stream_start = FALSE;

  g_free (stream_id);
  g_free (pad_name);
}

void
image_pad_worker_task (GstPad * pad)
{
  GstDataQueue *buffers;
  GstDataQueueItem *item;

  buffers = GST_QMMFSRC_IMAGE_PAD (pad)->buffers;

  if (gst_data_queue_pop (buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    gst_pad_push (pad, buffer);
  } else {
    GST_INFO_OBJECT (pad, "Pause image pad worker thread");
    gst_pad_pause_task (pad);
  }
}

static gboolean
image_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Received QUERY %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      if (!(caps = gst_pad_get_current_caps (pad)))
        caps = gst_pad_get_pad_template_caps (pad);

      GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);

      if (filter != NULL) {
        GstCaps *intersection  =
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (caps);
        caps = intersection;
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;

      // Minimum latency is the time to capture one video frame.
      min_latency = GST_QMMFSRC_IMAGE_PAD (pad)->duration;

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
image_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Received EVENT %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      qmmfsrc_image_pad_flush_buffers_queue (pad, TRUE);
      success = gst_pad_pause_task (pad);
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      qmmfsrc_image_pad_flush_buffers_queue (pad, FALSE);
      success = gst_pad_start_task (
          pad, (GstTaskFunction) image_pad_worker_task, pad, NULL);
      gst_event_unref (event);

      gst_segment_init (&GST_QMMFSRC_IMAGE_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_EOS:
      // After EOS, we should not send any more buffers, even if there are
      // more requests coming in.
      qmmfsrc_image_pad_flush_buffers_queue (pad, TRUE);
      gst_event_unref (event);

      gst_segment_init (&GST_QMMFSRC_IMAGE_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_RECONFIGURE:
      success = qmmfsrc_image_pad_fixate_caps (pad);

      // Clear the RECONFIGURE flag on success.
      if (success)
        GST_OBJECT_FLAG_UNSET (pad, GST_PAD_FLAG_NEED_RECONFIGURE);
      break;
    case GST_EVENT_CUSTOM_UPSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_BOTH:
      break;
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }
  return success;
}

static gboolean
image_pad_activate_mode (GstPad * pad, GstObject * parent, GstPadMode mode,
    gboolean active)
{
  gboolean success = TRUE;
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        qmmfsrc_image_pad_flush_buffers_queue (pad, FALSE);
        success = gst_pad_start_task (
            pad, (GstTaskFunction) image_pad_worker_task, pad, NULL);
      } else {
        qmmfsrc_image_pad_flush_buffers_queue (pad, TRUE);
        success = gst_pad_stop_task (pad);

        gst_segment_init (&GST_QMMFSRC_IMAGE_PAD (pad)->segment,
            GST_FORMAT_UNDEFINED);
      }
      ipad->stream_start = active;
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to activate image pad task!");
    return success;
  }

  GST_DEBUG_OBJECT (pad, "Video Pad (%u) mode: %s",
      GST_QMMFSRC_IMAGE_PAD (pad)->index, active ? "ACTIVE" : "STOPED");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
image_pad_update_params (GstPad * pad, GstStructure *structure)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  gint width = 0, height = 0, fps_n = 0, fps_d = 0;
  gint format = GST_VIDEO_FORMAT_UNKNOWN, codec = GST_IMAGE_CODEC_NONE;
  gdouble framerate = 0.0;
  gboolean reconfigure = FALSE;

  GST_QMMFSRC_IMAGE_PAD_LOCK (pad);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);

  // Use max-framerate for fps calculation if variable framerate is negotiated.
  if ((fps_n == 0) && (fps_d == 1))
    gst_structure_get_fraction (structure, "max-framerate", &fps_n, &fps_d);

  ipad->duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  framerate = 1 / GST_TIME_AS_SECONDS (gst_guint64_to_gdouble (ipad->duration));

  // Raise the reconfiguation flag if dimensions and/or frame rate changed.
  reconfigure |= (width != ipad->width) || (height != ipad->height) ||
      (framerate != ipad->framerate);

  ipad->width = width;
  ipad->height = height;
  ipad->framerate = framerate;

  if (gst_structure_has_name (structure, "image/jpeg")) {
    codec = GST_IMAGE_CODEC_JPEG;
    format = GST_VIDEO_FORMAT_ENCODED;
  } else if (gst_structure_has_name (structure, "video/x-raw")) {
    codec = GST_IMAGE_CODEC_NONE;
    format = gst_video_format_from_string (
        gst_structure_get_string (structure, "format"));
  } else if (gst_structure_has_name (structure, "video/x-bayer")) {
    const gchar *string = NULL;
    guint bpp = 0;

    string = gst_structure_get_string (structure, "bpp");

    if (g_strcmp0 (string, "8") == 0)
      bpp = 8;
    else if (g_strcmp0 (string, "10") == 0)
      bpp = 10;
    else if (g_strcmp0 (string, "12") == 0)
      bpp = 12;
    else if (g_strcmp0 (string, "16") == 0)
      bpp = 16;

    // Raise the reconfiguation flag if bayer format BPP changed.
    reconfigure |= (bpp != ipad->bpp);

    ipad->bpp = bpp;

    string = gst_structure_get_string (structure, "format");

    if (g_strcmp0 (string, "bggr") == 0)
      format = GST_BAYER_FORMAT_BGGR;
    else if (g_strcmp0 (string, "rggb") == 0)
      format = GST_BAYER_FORMAT_RGGB;
    else if (g_strcmp0 (string, "gbrg") == 0)
      format = GST_BAYER_FORMAT_GBRG;
    else if (g_strcmp0 (string, "grbg") == 0)
      format = GST_BAYER_FORMAT_GRBG;
    else if (g_strcmp0 (string, "mono") == 0)
      format = GST_BAYER_FORMAT_MONO;

    codec = GST_IMAGE_CODEC_NONE;
  }

  // Raise the reconfiguation flag if format or codec changed.
  reconfigure |= (format != ipad->format) || (codec != ipad->codec);

  ipad->format = format;
  ipad->codec = codec;

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (pad);

  // Send reconfigurtion signal only when paramters have changed.
  if (reconfigure)
    g_signal_emit (pad, signals[SIGNAL_PAD_RECONFIGURE], 0);
}


GstPad *
qmmfsrc_request_image_pad (GstPadTemplate * templ, const gchar * name,
    const guint index)
{
  GstPad *srcpad = GST_PAD (g_object_new (
      GST_TYPE_QMMFSRC_IMAGE_PAD,
      "name", name,
      "direction", templ->direction,
      "template", templ,
      NULL
  ));
  g_return_val_if_fail (srcpad != NULL, NULL);

  GST_QMMFSRC_IMAGE_PAD (srcpad)->index = index;

  gst_pad_set_query_function (srcpad, GST_DEBUG_FUNCPTR (image_pad_query));
  gst_pad_set_event_function (srcpad, GST_DEBUG_FUNCPTR (image_pad_event));
  gst_pad_set_activatemode_function (
      srcpad, GST_DEBUG_FUNCPTR (image_pad_activate_mode));

  gst_pad_use_fixed_caps (srcpad);
  gst_pad_set_active (srcpad, TRUE);

  return srcpad;
}

void
qmmfsrc_release_image_pad (GstElement * element, GstPad * pad)
{
  gst_object_ref (pad);

  gst_pad_set_active (pad, FALSE);
  gst_child_proxy_child_removed (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));
  gst_element_remove_pad (element, pad);

  gst_object_unref (pad);
}

void
qmmfsrc_image_pad_flush_buffers_queue (GstPad * pad, gboolean flush)
{
  GST_INFO_OBJECT (pad, "Flushing buffer queue: %s", flush ? "TRUE" : "FALSE");

  gst_data_queue_set_flushing (GST_QMMFSRC_IMAGE_PAD (pad)->buffers, flush);
  gst_data_queue_flush (GST_QMMFSRC_IMAGE_PAD (pad)->buffers);
}

gboolean
qmmfsrc_image_pad_fixate_caps (GstPad * pad)
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
    image_pad_send_stream_start (pad);
    gst_pad_set_caps (pad, caps);

    GST_DEBUG_OBJECT (pad, "Caps already fixated to: %" GST_PTR_FORMAT, caps);
    image_pad_update_params (pad, gst_caps_get_structure (caps, 0));
    gst_caps_unref (caps);

    return TRUE;
  }

  GST_DEBUG_OBJECT (pad, "Trying to fixate caps: %" GST_PTR_FORMAT, caps);

  g_return_val_if_fail (!gst_caps_is_empty(caps), FALSE);

  // Capabilities are not fixated, fixate them.
  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  framerate = gst_structure_get_value (structure, "framerate");

  if (!width) {
    gst_structure_set (structure, "width", G_TYPE_INT,
        DEFAULT_IMAGE_STREAM_WIDTH, NULL);
    GST_DEBUG_OBJECT (pad, "Width not set, using default value: %d",
        DEFAULT_IMAGE_STREAM_WIDTH);
  }

  if (!height) {
    gst_structure_set (structure, "height", G_TYPE_INT,
        DEFAULT_IMAGE_STREAM_HEIGHT, NULL);
    GST_DEBUG_OBJECT (pad, "Height not set, using default value: %d",
        DEFAULT_IMAGE_STREAM_HEIGHT);
  }

  if (!gst_value_is_fixed (framerate)) {
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_IMAGE_STREAM_FPS_NUM, DEFAULT_IMAGE_STREAM_FPS_DEN);
    GST_DEBUG_OBJECT (pad, "Framerate not set, using default value: %d/%d",
        DEFAULT_IMAGE_STREAM_FPS_NUM, DEFAULT_IMAGE_STREAM_FPS_DEN);
  }

  if (gst_structure_has_field (structure, "format")) {
    const gchar *format = gst_structure_get_string (structure, "format");
    gboolean isbayer = gst_structure_has_name (structure, "video/x-bayer");

    if (!format) {
      gst_structure_fixate_field_string (structure, "format",
          isbayer ? DEFAULT_IMAGE_BAYER_FORMAT : DEFAULT_IMAGE_RAW_FORMAT);
      GST_DEBUG_OBJECT (pad, "Format not set, using default value: %s",
          isbayer ? DEFAULT_IMAGE_BAYER_FORMAT : DEFAULT_IMAGE_RAW_FORMAT);
    }
  }

  if (gst_structure_has_field (structure, "bpp")) {
    const gchar *bpp = gst_structure_get_string (structure, "bpp");

    if (!bpp) {
      gst_structure_fixate_field_string (structure, "bpp",
          DEFAULT_IMAGE_BAYER_BPP);
      GST_DEBUG_OBJECT (pad, "BPP not set, using default value: %s",
          DEFAULT_IMAGE_BAYER_BPP);
    }
  }

  // Always fixate pixel aspect ratio to 1/1.
  gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        1, 1, NULL);

  image_pad_send_stream_start (pad);
  caps = gst_caps_fixate (caps);
  gst_pad_set_caps (pad, caps);

  GST_DEBUG_OBJECT (pad, "Caps fixated to: %" GST_PTR_FORMAT, caps);
  image_pad_update_params (pad, structure);
  gst_caps_unref (caps);
  return TRUE;
}

static void
image_pad_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQmmfSrcImagePad *pad = GST_QMMFSRC_IMAGE_PAD (object);
  GstElement *parent = gst_pad_get_parent_element (GST_PAD (pad));
  const gchar *propname = g_param_spec_get_name (pspec);

  // Extract the state from the pad parent or in case there is no parent
  // use default value as parameters are being set upon object construction.
  GstState state = parent ? GST_STATE (parent) : GST_STATE_VOID_PENDING;

  // Decrease the pad parent reference count as it is not needed any more.
  if (parent != NULL)
    gst_object_unref (parent);

  if (!QMMFSRC_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (pad, "Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_QMMFSRC_IMAGE_PAD_LOCK (pad);

  switch (property_id) {
    case PROP_IMAGE_ROTATE:
      pad->rotate = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (pad);
}

static void
image_pad_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  GstQmmfSrcImagePad *pad = GST_QMMFSRC_IMAGE_PAD (object);

  GST_QMMFSRC_IMAGE_PAD_LOCK (pad);

  switch (property_id) {
    case PROP_IMAGE_ROTATE:
      g_value_set_enum (value, pad->rotate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (pad);
}

static void
image_pad_finalize (GObject * object)
{
  GstQmmfSrcImagePad *pad = GST_QMMFSRC_IMAGE_PAD (object);

  if (pad->buffers != NULL) {
    gst_data_queue_set_flushing(pad->buffers, TRUE);
    gst_data_queue_flush(pad->buffers);
    gst_object_unref(GST_OBJECT_CAST(pad->buffers));
    pad->buffers = NULL;
  }

  if (pad->params != NULL) {
    gst_structure_free (pad->params);
    pad->params = NULL;
  }

  G_OBJECT_CLASS (qmmfsrc_image_pad_parent_class)->finalize(object);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

// QMMF Source image pad class initialization.
static void
qmmfsrc_image_pad_class_init (GstQmmfSrcImagePadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);

  gobject->get_property = GST_DEBUG_FUNCPTR (image_pad_get_property);
  gobject->set_property = GST_DEBUG_FUNCPTR (image_pad_set_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (image_pad_finalize);

  g_object_class_install_property (gobject, PROP_IMAGE_ROTATE,
    g_param_spec_enum ("rotate", "Rotate",
        "Set Orientation Angle for Image Stream",
        GST_TYPE_QMMFSRC_ROTATE, DEFAULT_PROP_ROTATE,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_PAD_RECONFIGURE] =
      g_signal_new ("reconfigure", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);

  GST_DEBUG_CATEGORY_INIT (qmmfsrc_image_pad_debug, "qtiqmmfsrc", 0,
      "QTI QMMF Source image pad");
}

// QMMF Source image pad initialization.
static void
qmmfsrc_image_pad_init (GstQmmfSrcImagePad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->stream_start = FALSE;

  pad->index     = 0;

  pad->width     = -1;
  pad->height    = -1;
  pad->framerate = 0;
  pad->format    = GST_VIDEO_FORMAT_UNKNOWN;
  pad->codec     = GST_IMAGE_CODEC_UNKNOWN;
  pad->rotate    = DEFAULT_PROP_ROTATE;
  pad->params    = gst_structure_new_empty ("codec-params");

  pad->duration  = GST_CLOCK_TIME_NONE;

  // TODO temporality solution until properties are implemented.
  gst_structure_set (pad->params, "quality", G_TYPE_UINT,
      DEFAULT_PROP_QUALITY, NULL);

  pad->buffers   = gst_data_queue_new (queue_is_full_cb, NULL, NULL, pad);
}
