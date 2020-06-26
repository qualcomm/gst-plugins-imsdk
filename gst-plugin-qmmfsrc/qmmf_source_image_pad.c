/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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
#define DEFAULT_IMAGE_BAYER_FORMAT   "RAW10"

#define DEFAULT_PROP_QUALITY            85
#define DEFAULT_PROP_THUMBNAIL_WIDTH    0
#define DEFAULT_PROP_THUMBNAIL_HEIGHT   0
#define DEFAULT_PROP_THUMBNAIL_QUALITY  85
#define DEFAULT_PROP_SCREENNAIL_WIDTH   0
#define DEFAULT_PROP_SCREENNAIL_HEIGHT  0
#define DEFAULT_PROP_SCREENNAIL_QUALITY 85
#define DEFAULT_PROP_IMAGE_MODE         GST_IMAGE_MODE_VIDEO

enum
{
  PROP_0,
  PROP_IMAGE_MODE,
};

GType
gst_qmmfsrc_image_pad_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_IMAGE_MODE_VIDEO,
        "Video snapshot is captured with video settings. Video recording will "
        "not be interrupted in this mode", "video"
    },
    { GST_IMAGE_MODE_CONTINUOUS,
        "Continuously capture images at the set frame rate in the "
        "capabilities.", "continuous"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstImageMode", variants);

  return gtype;
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

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        qmmfsrc_image_pad_flush_buffers_queue (pad, FALSE);
        success = gst_pad_start_task (
            pad, (GstTaskFunction) image_pad_worker_task, pad, NULL);
      } else {
        qmmfsrc_image_pad_flush_buffers_queue (pad, TRUE);
        success = gst_pad_stop_task (pad);
      }
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
  gint fps_n = 0, fps_d = 0;

  GST_QMMFSRC_IMAGE_PAD_LOCK (pad);

  gst_structure_get_int (structure, "width", &ipad->width);
  gst_structure_get_int (structure, "height", &ipad->height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);

  ipad->duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  ipad->framerate = 1 / GST_TIME_AS_SECONDS (
      gst_guint64_to_gdouble (ipad->duration));

  if (gst_structure_has_name (structure, "image/jpeg")) {
    ipad->codec = GST_IMAGE_CODEC_JPEG;
    ipad->format = GST_VIDEO_FORMAT_ENCODED;
  } else if (gst_structure_has_name (structure, "video/x-raw")) {
    ipad->codec = GST_IMAGE_CODEC_NONE;
    ipad->format = gst_video_format_from_string (
        gst_structure_get_string (structure, "format"));
  } else if (gst_structure_has_name (structure, "video/x-bayer")) {
    ipad->codec = GST_IMAGE_CODEC_NONE;
    const gchar *bayer = gst_structure_get_string (structure, "format");
    if (g_strcmp0(bayer, "RAW8") == 0) {
      ipad->bayer = GST_IMAGE_FORMAT_RAW8;
    } else if (g_strcmp0(bayer, "RAW10") == 0) {
      ipad->bayer = GST_IMAGE_FORMAT_RAW10;
    } else if (g_strcmp0(bayer, "RAW12") == 0) {
      ipad->bayer = GST_IMAGE_FORMAT_RAW12;
    } else if (g_strcmp0(bayer, "RAW16") == 0) {
      ipad->bayer = GST_IMAGE_FORMAT_RAW16;
    }
  }

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (pad);
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
  gchar *padname = GST_PAD_NAME (pad);
  guint index = GST_QMMFSRC_IMAGE_PAD (pad)->index;

  gst_object_ref (pad);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));
  gst_element_remove_pad (element, pad);
  gst_pad_set_active (pad, FALSE);

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
    image_pad_update_params (pad, gst_caps_get_structure (caps, 0));
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

  if (gst_structure_has_name (structure, "video/x-bayer")) {
    const gchar *type = NULL;
    type = gst_structure_get_string (structure, "format");
    if (!type) {
      gst_structure_set (structure, "format", G_TYPE_STRING,
          DEFAULT_IMAGE_BAYER_FORMAT, NULL);
      GST_DEBUG_OBJECT (pad, "Image format not set, using default value: %s",
          DEFAULT_IMAGE_BAYER_FORMAT);
    }
  }

  if (gst_structure_has_name (structure, "video/x-raw")) {
    const gchar *type = NULL;
    type = gst_structure_get_string (structure, "format");
    if (!type) {
      gst_structure_set (structure, "format", G_TYPE_STRING,
          DEFAULT_IMAGE_RAW_FORMAT, NULL);
      GST_DEBUG_OBJECT (pad, "Image format not set, using default value: %s",
          DEFAULT_IMAGE_RAW_FORMAT);
    }
  }

  caps = gst_caps_fixate (caps);
  gst_pad_set_caps (pad, caps);

  image_pad_update_params (pad, structure);
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
    case PROP_IMAGE_MODE:
      pad->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (pad);

  // Emit a 'notify' signal for the changed property.
  g_object_notify_by_pspec (G_OBJECT (pad), pspec);
}

static void
image_pad_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  GstQmmfSrcImagePad *pad = GST_QMMFSRC_IMAGE_PAD (object);

  GST_QMMFSRC_IMAGE_PAD_LOCK (pad);

  switch (property_id) {
    case PROP_IMAGE_MODE:
      g_value_set_enum (value, pad->mode);
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

  g_object_class_install_property (gobject, PROP_IMAGE_MODE,
      g_param_spec_enum ("mode", "Image Mode",
          "Different image mode.",
          GST_TYPE_QMMFSRC_IMAGE_PAD_MODE, DEFAULT_PROP_IMAGE_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  GST_DEBUG_CATEGORY_INIT (qmmfsrc_image_pad_debug, "qtiqmmfsrc", 0,
      "QTI QMMF Source image pad");
}

// QMMF Source image pad initialization.
static void
qmmfsrc_image_pad_init (GstQmmfSrcImagePad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->index     = 0;

  pad->width     = -1;
  pad->height    = -1;
  pad->framerate = 0;
  pad->format    = GST_VIDEO_FORMAT_UNKNOWN;
  pad->bayer     = GST_IMAGE_FORMAT_UNKNOWN;
  pad->codec     = GST_IMAGE_CODEC_UNKNOWN;
  pad->params    = gst_structure_new_empty ("codec-params");
  pad->mode      = DEFAULT_PROP_IMAGE_MODE;

  pad->duration  = GST_CLOCK_TIME_NONE;

  // TODO temporality solution until properties are implemented.
  gst_structure_set (pad->params, "quality", G_TYPE_UINT,
      DEFAULT_PROP_QUALITY, NULL);
  gst_structure_set (pad->params, "thumbnail-width", G_TYPE_UINT,
      DEFAULT_PROP_THUMBNAIL_WIDTH, NULL);
  gst_structure_set (pad->params, "thumbnail-height", G_TYPE_UINT,
      DEFAULT_PROP_THUMBNAIL_HEIGHT, NULL);
  gst_structure_set (pad->params, "thumbnail-quality", G_TYPE_UINT,
      DEFAULT_PROP_THUMBNAIL_QUALITY, NULL);
  gst_structure_set (pad->params, "screennail-width", G_TYPE_UINT,
      DEFAULT_PROP_SCREENNAIL_WIDTH, NULL);
  gst_structure_set (pad->params, "screennail-height", G_TYPE_UINT,
      DEFAULT_PROP_SCREENNAIL_HEIGHT, NULL);
  gst_structure_set (pad->params, "screennail-quality", G_TYPE_UINT,
      DEFAULT_PROP_SCREENNAIL_QUALITY, NULL);

  pad->buffers   = gst_data_queue_new (queue_is_full_cb, NULL, NULL, pad);
}
