/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "camera-image-reprocess-pad.h"
#include <gst/utils/common-utils.h>

GST_DEBUG_CATEGORY_EXTERN (gst_camera_image_reproc_debug);
#define GST_CAT_DEFAULT gst_camera_image_reproc_debug

#define DEFAULT_PROP_SINK_CAMERA_ID             0
#define DEFAULT_PROP_SINK_REQUEST_METADATA_PATH NULL
#define DEFAULT_PROP_SINK_REQUEST_METADATA_STEP 0
#define DEFAULT_PROP_SINK_EIS \
    GST_CAMERA_IMAGE_REPROC_EIS_NONE
#define GST_TYPE_CAMERA_IMAGE_REPROC_EIS \
    (gst_camera_image_reproc_eis_get_type())

#define gst_camera_reproc_sink_pad_parent_class   sinkpad_parent_class
#define gst_camera_reproc_src_pad_parent_class    srcpad_parent_class

G_DEFINE_TYPE(GstCameraReprocSinkPad, gst_camera_reproc_sink_pad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstCameraReprocSrcPad, gst_camera_reproc_src_pad, GST_TYPE_PAD);

enum
{
  PROP_SINK_0,
  PROP_SINK_CAMERA_ID,
  PROP_SINK_REQUEST_METADATA_PATH,
  PROP_SINK_REQUEST_METADATA_STEP,
  PROP_SINK_EIS,
};

static GType
gst_camera_image_reproc_eis_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_CAMERA_IMAGE_REPROC_EIS_V3,
      "Eis with version 3 which will consume future frames",
      "v3"
    },
    { GST_CAMERA_IMAGE_REPROC_EIS_V2,
        "Eis with version 2 which will consume previous frames",
        "v2"
    },
    { GST_CAMERA_IMAGE_REPROC_EIS_NONE,
        "None", "none"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraImageReprocEis", variants);

  return gtype;
}


static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstPad *pad = GST_PAD (checkdata);

  if (GST_IS_CAMERA_REPROC_SRC_PAD (pad)) {
    GstCameraReprocSrcPad *srcpad = GST_CAMERA_REPROC_SRC_PAD_CAST (pad);

    GST_CAMERA_IMAGE_REPROC_PAD_SIGNAL_IDLE (srcpad, FALSE);

    // Limiting the output queue
    if (visible >= srcpad->buffers_limit) {
      GST_TRACE_OBJECT (pad, "Queue limit reached of %d buffers!",
          srcpad->buffers_limit);
      return TRUE;
    }
  } else if (GST_IS_CAMERA_REPROC_SINK_PAD (pad)) {
    GstCameraReprocSinkPad *sinkpad = GST_CAMERA_REPROC_SINK_PAD_CAST (pad);

    GST_CAMERA_IMAGE_REPROC_PAD_SIGNAL_IDLE (sinkpad, FALSE);

    // Limiting the input queue
    if (visible >= sinkpad->buffers_limit) {
      GST_TRACE_OBJECT (pad, "Queue limit reached of %d buffers!",
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

  if (GST_IS_CAMERA_REPROC_SRC_PAD (pad)) {
    GstCameraReprocSrcPad *srcpad = GST_CAMERA_REPROC_SRC_PAD_CAST (pad);

    GST_CAMERA_IMAGE_REPROC_PAD_SIGNAL_IDLE (srcpad, TRUE);
  } else if (GST_IS_CAMERA_REPROC_SINK_PAD (pad)) {
    GstCameraReprocSinkPad *sinkpad = GST_CAMERA_REPROC_SINK_PAD_CAST (pad);

    GST_CAMERA_IMAGE_REPROC_PAD_SIGNAL_IDLE (sinkpad, TRUE);
  }
}

static void
gst_camera_reproc_src_pad_worker_task (gpointer userdata)
{
  GstCameraReprocSrcPad *srcpad = GST_CAMERA_REPROC_SRC_PAD (userdata);
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
gst_camera_reproc_src_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstCameraReprocSrcPad *srcpad = GST_CAMERA_REPROC_SRC_PAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
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

      GST_CAMERA_REPROC_SRC_LOCK (srcpad);

      segment = &(srcpad)->segment;

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));

      GST_CAMERA_REPROC_SRC_UNLOCK (srcpad);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_camera_reproc_src_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstCameraReprocSrcPad *srcpad = GST_CAMERA_REPROC_SRC_PAD (pad);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
    {
      GstTaskState state = gst_pad_get_task_state (pad);
      gboolean success = FALSE;

      GST_INFO_OBJECT (srcpad, "%s task",
          active ? "Activating" : "Deactivating");

      if (active && (state != GST_TASK_STARTED)) {
        gst_data_queue_set_flushing (srcpad->buffers, FALSE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_start_task (GST_PAD (srcpad),
            gst_camera_reproc_src_pad_worker_task, srcpad, NULL);
      } else if (!active  && (state != GST_TASK_STOPPED)) {
        gst_data_queue_set_flushing (srcpad->buffers, TRUE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_stop_task (GST_PAD (srcpad));

        GST_CAMERA_REPROC_SRC_LOCK (srcpad);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_UNDEFINED);
        GST_CAMERA_REPROC_SRC_UNLOCK (srcpad);
      }

      if (!success) {
        GST_ERROR_OBJECT (pad, "Failed to %s task!",
            active ? "activate" : "deactivate");
        return FALSE;
      }

      GST_INFO_OBJECT (srcpad, "Task %s",
          active ? "activated" : "deactivated");
      break;
    }
    default:
      break;
  }

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
gst_camera_reproc_sinkpad_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec *pspec)
{
  GstCameraReprocSinkPad *sinkpad = GST_CAMERA_REPROC_SINK_PAD (object);
  GstElement *parent = gst_pad_get_parent_element (GST_PAD (sinkpad));
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state;

  // Extract the state from the pad parent or in case there is no parent
  // use default value as parameters are being set upon object construction.
  state = parent ? GST_STATE (parent) : GST_STATE_VOID_PENDING;

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (sinkpad, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_CAMERA_REPROC_SINK_LOCK (sinkpad);

  switch (property_id) {
    case PROP_SINK_CAMERA_ID:
      sinkpad->camera_id = g_value_get_uint (value);
      break;
    case PROP_SINK_REQUEST_METADATA_PATH:
      if (sinkpad->req_meta_path)
        g_free (sinkpad->req_meta_path);

      sinkpad->req_meta_path = g_strdup (g_value_get_string (value));
      break;
    case PROP_SINK_REQUEST_METADATA_STEP:
      sinkpad->req_meta_step = g_value_get_uint (value);
      break;
    case PROP_SINK_EIS:
      sinkpad->eis = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (sinkpad, property_id, pspec);
      break;
  }

  GST_CAMERA_REPROC_SINK_UNLOCK (sinkpad);
}

static void
gst_camera_reproc_sinkpad_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraReprocSinkPad *sinkpad = GST_CAMERA_REPROC_SINK_PAD (object);

  GST_CAMERA_REPROC_SINK_LOCK (sinkpad);

  switch (property_id) {
    case PROP_SINK_CAMERA_ID:
      g_value_set_uint (value, sinkpad->camera_id);
      break;
    case PROP_SINK_REQUEST_METADATA_PATH:
      g_value_set_string (value, sinkpad->req_meta_path);
      break;
    case PROP_SINK_REQUEST_METADATA_STEP:
      g_value_set_uint (value, sinkpad->req_meta_step);
      break;
    case PROP_SINK_EIS:
      g_value_set_enum (value, sinkpad->eis);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (sinkpad, property_id, pspec);
      break;
  }

  GST_CAMERA_REPROC_SINK_UNLOCK (sinkpad);
}

static void
gst_camera_reproc_sink_pad_finalize (GObject * object)
{
  GstCameraReprocSinkPad *pad = GST_CAMERA_REPROC_SINK_PAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);
  gst_object_unref (GST_OBJECT_CAST (pad->buffers));

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (sinkpad_parent_class)->finalize(object);
}

void
gst_camera_reproc_sink_pad_class_init (
    GstCameraReprocSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_camera_reproc_sinkpad_get_property);
  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_camera_reproc_sinkpad_set_property);
  gobject->finalize =
      GST_DEBUG_FUNCPTR (gst_camera_reproc_sink_pad_finalize);

  g_object_class_install_property (gobject, PROP_SINK_CAMERA_ID,
      g_param_spec_uint ("camera-id", "Camera ID",
          "Camera ID", 0, G_MAXINT8, DEFAULT_PROP_SINK_CAMERA_ID,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
  g_object_class_install_property (gobject, PROP_SINK_REQUEST_METADATA_PATH,
      g_param_spec_string ("request-meta-path", "Request Metadata Path",
          "Absolute path of request metadata to read by camera hal.",
          DEFAULT_PROP_SINK_REQUEST_METADATA_PATH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_SINK_REQUEST_METADATA_STEP,
      g_param_spec_uint ("request-meta-step", "Request Metadata Step",
          "Step to read request metadata by camera hal.",
          0, G_MAXUINT16, DEFAULT_PROP_SINK_REQUEST_METADATA_STEP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
  g_object_class_install_property (gobject, PROP_SINK_EIS,
      g_param_spec_enum ("eis", "EIS",
          "Electronic Image Stabilization to reduce the effects of camera \
          shake", GST_TYPE_CAMERA_IMAGE_REPROC_EIS, DEFAULT_PROP_SINK_EIS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
}

void
gst_camera_reproc_sink_pad_init (GstCameraReprocSinkPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  pad->buffers =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
  pad->buffers_limit = 0;
}

static void
gst_camera_reproc_src_pad_finalize (GObject * object)
{
  GstCameraReprocSrcPad *pad =
      GST_CAMERA_REPROC_SRC_PAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);
  gst_object_unref (GST_OBJECT_CAST (pad->buffers));

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (
      gst_camera_reproc_src_pad_parent_class)->finalize(object);
}

void
gst_camera_reproc_src_pad_class_init (GstCameraReprocSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize =
      GST_DEBUG_FUNCPTR (gst_camera_reproc_src_pad_finalize);
}

void
gst_camera_reproc_src_pad_init (GstCameraReprocSrcPad * pad)
{
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->buffers =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
  pad->buffers_limit = 0;
}
