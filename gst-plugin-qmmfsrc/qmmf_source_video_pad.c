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
*/

#include "qmmf_source_video_pad.h"

#include <gst/gstplugin.h>
#include <gst/gstelementfactory.h>
#include <gst/gstpadtemplate.h>
#include <gst/allocators/allocators.h>

#include "qmmf_source_utils.h"

// Declare qmmfsrc_video_pad_class_init() and qmmfsrc_video_pad_init()
// functions, implement qmmfsrc_video_pad_get_type() function and set
// qmmfsrc_video_pad_parent_class variable.
G_DEFINE_TYPE(GstQmmfSrcVideoPad, qmmfsrc_video_pad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (qmmfsrc_video_pad_debug);
#define GST_CAT_DEFAULT qmmfsrc_video_pad_debug

#define DEFAULT_VIDEO_STREAM_WIDTH    640
#define DEFAULT_VIDEO_STREAM_HEIGHT   480
#define DEFAULT_VIDEO_STREAM_FPS_NUM  30
#define DEFAULT_VIDEO_STREAM_FPS_DEN  1
#define DEFAULT_VIDEO_RAW_FORMAT      "NV12"
#define DEFAULT_VIDEO_RAW_COMPRESSION "none"
#define DEFAULT_VIDEO_BAYER_FORMAT    "bggr"
#define DEFAULT_VIDEO_BAYER_BPP       "10"

#define DEFAULT_PROP_SOURCE_INDEX    (-1)
#define DEFAULT_PROP_FRAMERATE       30.0
#define DEFAULT_PROP_CROP_X          0
#define DEFAULT_PROP_CROP_Y          0
#define DEFAULT_PROP_CROP_WIDTH      0
#define DEFAULT_PROP_CROP_HEIGHT     0
#define DEFAULT_PROP_EXTRA_BUFFERS   0

enum
{
  SIGNAL_PAD_RECONFIGURE,
  SIGNAL_PAD_ACTIVATION,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_VIDEO_SOURCE_INDEX,
  PROP_VIDEO_FRAMERATE,
  PROP_VIDEO_CROP,
  PROP_VIDEO_EXTRA_BUFFERS,
};

static guint signals[LAST_SIGNAL];

static void
video_pad_worker_task (GstPad * pad)
{
  GstDataQueue *buffers;
  GstDataQueueItem *item;

  buffers = GST_QMMFSRC_VIDEO_PAD (pad)->buffers;

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
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (pad, "Template caps: %" GST_PTR_FORMAT, caps);

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
      min_latency = GST_QMMFSRC_VIDEO_PAD (pad)->duration;

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
      qmmfsrc_video_pad_flush_buffers_queue (pad, TRUE);
      success = gst_pad_pause_task (pad);
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      qmmfsrc_video_pad_flush_buffers_queue (pad, FALSE);
      success = gst_pad_start_task (
          pad, (GstTaskFunction) video_pad_worker_task, pad, NULL);
      gst_event_unref (event);

      gst_segment_init (&GST_QMMFSRC_VIDEO_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_EOS:
      // After EOS, we should not send any more buffers, even if there are
      // more requests coming in.
      qmmfsrc_video_pad_flush_buffers_queue (pad, TRUE);
      gst_event_unref (event);

      gst_segment_init (&GST_QMMFSRC_VIDEO_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_RECONFIGURE:
      if (GST_STATE (parent) == GST_STATE_PAUSED ||
          GST_STATE (parent) == GST_STATE_PLAYING ) {
        success = qmmfsrc_video_pad_fixate_caps (pad);

        // Clear the RECONFIGURE flag on success.
        if (success)
          GST_OBJECT_FLAG_UNSET (pad, GST_PAD_FLAG_NEED_RECONFIGURE);
      }
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
        qmmfsrc_video_pad_flush_buffers_queue (pad, FALSE);
        success = gst_pad_start_task (
            pad, (GstTaskFunction) video_pad_worker_task, pad, NULL);
        g_signal_emit (pad, signals[SIGNAL_PAD_ACTIVATION], 0, TRUE);
      } else {
        qmmfsrc_video_pad_flush_buffers_queue (pad, TRUE);
        success = gst_pad_stop_task (pad);

        gst_segment_init (&GST_QMMFSRC_VIDEO_PAD (pad)->segment,
            GST_FORMAT_UNDEFINED);
        g_signal_emit (pad, signals[SIGNAL_PAD_ACTIVATION], 0, FALSE);
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
      GST_QMMFSRC_VIDEO_PAD (pad)->index, active ? "ACTIVE" : "STOPED");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
video_pad_update_params (GstPad * pad, GstStructure * structure)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  gint width = 0, height = 0, fps_n = 0, fps_d = 0;
  gint format = GST_VIDEO_FORMAT_UNKNOWN, codec = GST_VIDEO_CODEC_NONE;
  gdouble framerate = 0.0;
  gboolean reconfigure = FALSE;

  GST_QMMFSRC_VIDEO_PAD_LOCK (pad);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);

  vpad->duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  framerate = 1 / GST_TIME_AS_SECONDS (gst_guint64_to_gdouble (vpad->duration));

  // Raise the reconfiguation flag if dimensions and/or frame rate changed.
  reconfigure |= (width != vpad->width) || (height != vpad->height) ||
      (framerate != vpad->framerate);

  vpad->width = width;
  vpad->height = height;
  vpad->framerate = framerate;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    format = gst_video_format_from_string (
        gst_structure_get_string (structure, "format"));
    codec = GST_VIDEO_CODEC_NONE;
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
    reconfigure |= (bpp != vpad->bpp);

    vpad->bpp = bpp;

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

    codec = GST_VIDEO_CODEC_NONE;
  } else {
    format = GST_VIDEO_FORMAT_ENCODED;
    codec = GST_VIDEO_CODEC_JPEG;
  }

  // Raise the reconfiguation flag if format or codec changed.
  reconfigure |= (format != vpad->format) || (codec != vpad->codec);

  vpad->format = format;
  vpad->codec = codec;

  if (gst_structure_has_field (structure, "compression")) {
    const gchar *string = gst_structure_get_string (structure, "compression");
    GstVideoCompression compression = (g_strcmp0 (string, "ubwc") == 0) ?
        GST_VIDEO_COMPRESSION_UBWC : GST_VIDEO_COMPRESSION_NONE;

    // Raise the reconfiguation flag if format compression changed.
    reconfigure |= (compression != vpad->compression);

    vpad->compression = compression;
  }

  GST_QMMFSRC_VIDEO_PAD_UNLOCK (pad);

  // Send reconfigurtion signal only when paramters have changed.
  if (reconfigure)
    g_signal_emit (pad, signals[SIGNAL_PAD_RECONFIGURE], 0);
}

GstPad *
qmmfsrc_request_video_pad (GstPadTemplate * templ, const gchar * name,
    const guint index)
{
  GstPad *srcpad = GST_PAD (g_object_new (
      GST_TYPE_QMMFSRC_VIDEO_PAD,
      "name", name,
      "direction", templ->direction,
      "template", templ,
      NULL
  ));
  g_return_val_if_fail (srcpad != NULL, NULL);

  GST_QMMFSRC_VIDEO_PAD (srcpad)->index = index;

  gst_pad_set_query_function (srcpad, GST_DEBUG_FUNCPTR (video_pad_query));
  gst_pad_set_event_function (srcpad, GST_DEBUG_FUNCPTR (video_pad_event));
  gst_pad_set_activatemode_function (
      srcpad, GST_DEBUG_FUNCPTR (video_pad_activate_mode));

  gst_pad_use_fixed_caps (srcpad);
  gst_pad_set_active (srcpad, TRUE);

  return srcpad;
}

void
qmmfsrc_release_video_pad (GstElement * element, GstPad * pad)
{
  gst_object_ref (pad);

  gst_pad_set_active (pad, FALSE);
  gst_child_proxy_child_removed (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));
  gst_element_remove_pad (element, pad);

  gst_object_unref (pad);
}

gboolean
qmmfsrc_video_pad_fixate_caps (GstPad * pad)
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

    GST_DEBUG_OBJECT (pad, "Caps already fixated to: %" GST_PTR_FORMAT, caps);
    video_pad_update_params (pad, gst_caps_get_structure (caps, 0));

    return TRUE;
  }

  GST_DEBUG_OBJECT (pad, "Trying to fixate caps: %" GST_PTR_FORMAT, caps);

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
    gboolean isbayer = gst_structure_has_name (structure, "video/x-bayer");

    if (!format) {
      gst_structure_fixate_field_string (structure, "format",
          isbayer ? DEFAULT_VIDEO_BAYER_FORMAT : DEFAULT_VIDEO_RAW_FORMAT);
      GST_DEBUG_OBJECT (pad, "Format not set, using default value: %s",
          isbayer ? DEFAULT_VIDEO_BAYER_FORMAT : DEFAULT_VIDEO_RAW_FORMAT);
    }
  }

  if (gst_structure_has_field (structure, "bpp")) {
    const gchar *bpp = gst_structure_get_string (structure, "bpp");

    if (!bpp) {
      gst_structure_fixate_field_string (structure, "bpp",
          DEFAULT_VIDEO_BAYER_BPP);
      GST_DEBUG_OBJECT (pad, "BPP not set, using default value: %s",
          DEFAULT_VIDEO_BAYER_BPP);
    }
  }

  if (gst_structure_has_field (structure, "compression")) {
    const gchar *compression =
        gst_structure_get_string (structure, "compression");

    if (!compression) {
      gst_structure_fixate_field_string (structure, "compression",
            DEFAULT_VIDEO_RAW_COMPRESSION);
      GST_DEBUG_OBJECT (pad, "Compression not set, using default value: %s",
            DEFAULT_VIDEO_RAW_COMPRESSION);
    }
  }

  // Always fixate pixel aspect ratio to 1/1.
  gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        1, 1, NULL);

  caps = gst_caps_fixate (caps);
  gst_pad_set_caps (pad, caps);

  GST_DEBUG_OBJECT (pad, "Caps fixated to: %" GST_PTR_FORMAT, caps);
  video_pad_update_params (pad, structure);
  return TRUE;
}

void
qmmfsrc_video_pad_flush_buffers_queue (GstPad * pad, gboolean flush)
{
  GST_INFO_OBJECT (pad, "Flushing buffer queue: %s", flush ? "TRUE" : "FALSE");

  gst_data_queue_set_flushing (GST_QMMFSRC_VIDEO_PAD (pad)->buffers, flush);
  gst_data_queue_flush (GST_QMMFSRC_VIDEO_PAD (pad)->buffers);
}

static void
video_pad_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec *pspec)
{
  GstQmmfSrcVideoPad *pad = GST_QMMFSRC_VIDEO_PAD (object);
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

  GST_QMMFSRC_VIDEO_PAD_LOCK (pad);

  switch (property_id) {
    case PROP_VIDEO_SOURCE_INDEX:
      pad->srcidx = g_value_get_int (value);
      break;
    case PROP_VIDEO_FRAMERATE:
      pad->framerate = g_value_get_double (value);
      break;
    case PROP_VIDEO_CROP:
    {
      g_return_if_fail (gst_value_array_get_size (value) == 4);

      pad->crop.x = g_value_get_int (gst_value_array_get_value (value, 0));
      pad->crop.y = g_value_get_int (gst_value_array_get_value (value, 1));
      pad->crop.w = g_value_get_int (gst_value_array_get_value (value, 2));
      pad->crop.h = g_value_get_int (gst_value_array_get_value (value, 3));
      break;
    }
    case PROP_VIDEO_EXTRA_BUFFERS:
      pad->xtrabufs = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (pad, property_id, pspec);
      break;
  }

  GST_QMMFSRC_VIDEO_PAD_UNLOCK (pad);

  // Emit a 'notify' signal for the changed property.
  g_object_notify_by_pspec (G_OBJECT (pad), pspec);
}

static void
video_pad_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  GstQmmfSrcVideoPad *pad = GST_QMMFSRC_VIDEO_PAD (object);

  GST_QMMFSRC_VIDEO_PAD_LOCK (pad);

  switch (property_id) {
    case PROP_VIDEO_SOURCE_INDEX:
      g_value_set_int (value, pad->srcidx);
      break;
    case PROP_VIDEO_FRAMERATE:
      g_value_set_double (value, pad->framerate);
      break;
    case PROP_VIDEO_CROP:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, pad->crop.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, pad->crop.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, pad->crop.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, pad->crop.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_VIDEO_EXTRA_BUFFERS:
      g_value_set_uint (value, pad->xtrabufs);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (pad, property_id, pspec);
      break;
  }

  GST_QMMFSRC_VIDEO_PAD_UNLOCK (pad);
}

static void
video_pad_finalize (GObject * object)
{
  GstQmmfSrcVideoPad *pad = GST_QMMFSRC_VIDEO_PAD (object);

  if (pad->buffers != NULL) {
    gst_data_queue_set_flushing (pad->buffers, TRUE);
    gst_data_queue_flush (pad->buffers);
    gst_object_unref (GST_OBJECT_CAST (pad->buffers));
    pad->buffers = NULL;
  }

  G_OBJECT_CLASS (qmmfsrc_video_pad_parent_class)->finalize(object);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

// QMMF Source video pad class initialization.
static void
qmmfsrc_video_pad_class_init (GstQmmfSrcVideoPadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);

  gobject->get_property = GST_DEBUG_FUNCPTR (video_pad_get_property);
  gobject->set_property = GST_DEBUG_FUNCPTR (video_pad_set_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (video_pad_finalize);

  g_object_class_install_property (gobject, PROP_VIDEO_SOURCE_INDEX,
      g_param_spec_int ("source-index", "Source index",
          "Index of the source video pad to which this pad will be linked",
          -1, G_MAXINT, DEFAULT_PROP_SOURCE_INDEX,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_VIDEO_FRAMERATE,
      g_param_spec_double ("framerate", "Framerate",
          "Target framerate in frames per second for displaying",
          0.0, 30.0, DEFAULT_PROP_FRAMERATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_VIDEO_CROP,
      gst_param_spec_array ("crop", "Crop rectangle",
          "Crop rectangle ('<X, Y, WIDTH, HEIGHT>'). Applicable only for "
          "JPEG and YUY2 formats",
          g_param_spec_int ("value", "Crop Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_VIDEO_EXTRA_BUFFERS,
      g_param_spec_uint ("extra-buffers", "Extra Buffers",
          "Number of additional buffers that will be allocated.",
          0, G_MAXUINT, DEFAULT_PROP_EXTRA_BUFFERS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  signals[SIGNAL_PAD_RECONFIGURE] =
      g_signal_new ("reconfigure", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);

  signals[SIGNAL_PAD_ACTIVATION] =
      g_signal_new ("activation", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  GST_DEBUG_CATEGORY_INIT (qmmfsrc_video_pad_debug, "qtiqmmfsrc", 0,
      "QTI QMMF Source video pad");
}

// QMMF Source video pad initialization.
static void
qmmfsrc_video_pad_init (GstQmmfSrcVideoPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->session_id   = 0;
  pad->index        = -1;
  pad->srcidx       = -1;

  pad->width        = -1;
  pad->height       = -1;
  pad->framerate    = 0.0;
  pad->format       = GST_VIDEO_FORMAT_UNKNOWN;
  pad->compression  = GST_VIDEO_COMPRESSION_NONE;
  pad->codec        = GST_VIDEO_CODEC_UNKNOWN;

  pad->crop.x       = DEFAULT_PROP_CROP_X;
  pad->crop.y       = DEFAULT_PROP_CROP_Y;
  pad->crop.w       = DEFAULT_PROP_CROP_WIDTH;
  pad->crop.h       = DEFAULT_PROP_CROP_HEIGHT;
  pad->xtrabufs     = DEFAULT_PROP_EXTRA_BUFFERS;

  pad->duration  = GST_CLOCK_TIME_NONE;

  pad->buffers   = gst_data_queue_new (queue_is_full_cb, NULL, NULL, pad);
}
