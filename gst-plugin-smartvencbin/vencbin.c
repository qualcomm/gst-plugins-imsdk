/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vencbin.h"

#define GST_CAT_DEFAULT gst_venc_bin_debug
GST_DEBUG_CATEGORY (gst_venc_bin_debug);

#define gst_venc_bin_parent_class parent_class
G_DEFINE_TYPE (GstVideoEncBin, gst_venc_bin, GST_TYPE_BIN);

#define GST_VENC_BIN_MAIN_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_VENC_BIN_AUX_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_VENC_BIN_SRC_CAPS \
    "video/x-h264; " \
    "video/x-h265"

enum {
  LAST_SIGNAL
};

#define GST_TYPE_VENC_BIN_ENCODER   (gst_venc_bin_encoder_get_type())
#define DEFAULT_PROP_ENCODER        GST_VENC_BIN_C2_ENC
#define DEFAULT_PROP_MAX_BITRATE (6000000)
#define DEFAULT_PROP_DEFAULT_GOP_LENGTH (30)
#define DEFAULT_PROP_MAX_GOP_LENGTH (600)
#define DEFAULT_PROP_SMART_FRAMERATE (TRUE)
#define DEFAULT_PROP_SMART_GOP (TRUE)

enum
{
  PROP_0,
  PROP_ENCODER,
  PROP_MAX_BITRATE,
  PROP_SMART_FRAMERATE,
  PROP_SMART_GOP,
  PROP_DEFAULT_GOP_LENGTH,
  PROP_MAX_GOP_LENGTH,
  PROP_LEVELS_OVERRIDE,
  PROP_ROI_QUALITY_CFG,
};

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_VIDEO_FORMATS "{ NV12 }"

#define GST_ML_VIDEO_DETECTION_TEXT_FORMATS \
    "{ utf8 }"

#define GST_ML_VIDEO_DETECTION_SINK_CAPS                           \
    "text/x-raw, "                                                 \
    "format = (string) " GST_ML_VIDEO_DETECTION_TEXT_FORMATS

static GstStaticPadTemplate gst_venc_bin_main_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
        GST_VIDEO_FORMATS))
);

static GstStaticPadTemplate gst_venc_bin_control_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_ctrl",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
        GST_VIDEO_FORMATS))
);

static GstStaticPadTemplate gst_venc_bin_ml_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_ml",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(GST_ML_VIDEO_DETECTION_SINK_CAPS)
);

static GstStaticPadTemplate gst_venc_bin_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VENC_BIN_SRC_CAPS)
    );

static GType
gst_venc_bin_encoder_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_VENC_BIN_C2_ENC,
        "Codec2 encoder.",
        "c2enc"
    },
    { GST_VENC_BIN_OMX_ENC,
        "OMX encoder.",
        "omxenc"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstBinEncoderType", variants);

  return gtype;
}

static gboolean
gst_venc_bin_update_encoder (GstVideoEncBin * vencbin)
{
  GstPad *srcpad = NULL, *sinkpad = NULL;
  gboolean success = TRUE;

  if (vencbin->encoder != NULL) {
    // Remove ghost pad links.
    gst_ghost_pad_set_target (GST_GHOST_PAD (vencbin->srcpad), NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD (vencbin->sinkpad), NULL);

    // Remove previous encoder.
    gst_bin_remove (GST_BIN (vencbin), vencbin->encoder);
  }

  // Create encoder plugin
  switch (vencbin->encoder_type) {
    case GST_VENC_BIN_C2_ENC:
      vencbin->encoder = gst_element_factory_make("qtic2venc", NULL);
      if (NULL == vencbin->encoder) {
        GST_ERROR_OBJECT (vencbin, "failed to create videoctrl");
        return FALSE;
      }

      // Set default properties
      g_object_set (G_OBJECT (vencbin->encoder), "control-rate", 3, NULL);
      g_object_set (G_OBJECT (vencbin->encoder), "target-bitrate",
          vencbin->max_bitrate, NULL);
      g_object_set (G_OBJECT (vencbin->encoder), "roi-quant-mode", TRUE, NULL);

      break;
    case GST_VENC_BIN_OMX_ENC:
      vencbin->encoder = gst_element_factory_make("omxh264enc", NULL);
      if (NULL == vencbin->encoder) {
        GST_ERROR_OBJECT (vencbin, "failed to create videoctrl");
        return FALSE;
      }

      // Set default properties
      g_object_set (G_OBJECT (vencbin->encoder), "target-bitrate",
          vencbin->max_bitrate, NULL);
      g_object_set (G_OBJECT (vencbin->encoder), "roi-quant-mode", TRUE, NULL);

      break;
    default:
      GST_ERROR_OBJECT (vencbin, "Unsupported encoder type '%d'!",
          vencbin->encoder_type);
      return FALSE;
  }

  gst_bin_add (GST_BIN (vencbin), vencbin->encoder);

  if ((srcpad = gst_element_get_static_pad (vencbin->encoder, "src")) == NULL) {
    GST_WARNING_OBJECT (vencbin, "Element %s has no 'src' pad!",
        GST_ELEMENT_NAME (vencbin->encoder));
    goto cleanup;
  }

  // Link encoder src pad to the ghost src pad of the bin
  success = gst_ghost_pad_set_target (GST_GHOST_PAD (vencbin->srcpad), srcpad);
  gst_object_unref (srcpad);

  if (!success) {
    GST_WARNING_OBJECT (vencbin, "Can not set %s:%s as target for %s:%s",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (vencbin->srcpad));
    goto cleanup;
  }

  if ((sinkpad = gst_element_get_static_pad (vencbin->encoder, "sink")) == NULL) {
    GST_WARNING_OBJECT (vencbin, "Element %s has no 'sink' pad!",
        GST_ELEMENT_NAME (vencbin->encoder));
    goto cleanup;
  }

  // Link encoder sink pad to the ghost sink pad of the bin
  success = gst_ghost_pad_set_target (GST_GHOST_PAD (vencbin->sinkpad), sinkpad);
  gst_object_unref (sinkpad);

  if (!success) {
    GST_WARNING_OBJECT (vencbin, "Can not set %s:%s as target for %s:%s",
        GST_DEBUG_PAD_NAME (sinkpad), GST_DEBUG_PAD_NAME (vencbin->sinkpad));
    goto cleanup;
  }

  return TRUE;

cleanup:
  gst_bin_remove (GST_BIN (vencbin), vencbin->encoder);
  vencbin->encoder = NULL;

  return FALSE;
}


static void
on_videoctrl_bitrate_received (guint bitrate, gpointer user_data)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (user_data);

  if (NULL == vencbin) {
    GST_ERROR("unexpected NULL filter");
    return;
  }

  if (NULL == vencbin->encoder) {
    GST_ERROR_OBJECT (vencbin, "unexpected NULL video encoder");
    return;
  }

  GST_INFO_OBJECT (vencbin, "bitrate=%u", bitrate);
  g_object_set (G_OBJECT (vencbin->encoder), "target-bitrate", bitrate, NULL);
}

static void
on_videoctrl_goplength_received (guint goplength, guint64 pts,
    gpointer user_data)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (user_data);

  GST_VENC_BIN_LOCK (vencbin);

  if (NULL == vencbin) {
    GST_ERROR_OBJECT (vencbin, "unexpected NULL object");
    GST_VENC_BIN_UNLOCK (vencbin);
    return;
  }

  if (NULL == vencbin->encoder) {
    GST_ERROR_OBJECT (vencbin, "unexpected NULL video encoder");
    GST_VENC_BIN_UNLOCK (vencbin);
    return;
  }

  GST_INFO_OBJECT (vencbin, "goplength=%u, pts=%lu", goplength,
      GST_TIME_AS_MSECONDS(pts));

  if (0 == pts) {
    // Set GOP length to encoder (init gop)
    GST_INFO_OBJECT (vencbin, "Set GOP LEN - %d (default=%d)", goplength,
        vencbin->default_goplength);
    g_object_set(G_OBJECT(vencbin->encoder), "idr-interval", goplength, NULL);

     // no longer relevant
    vencbin->pending_gop_pts = 0;
    vencbin->pending_gop_len = 0;
  } else {
    // Set GOP len when HD PTS reaches gop len increase pts
    vencbin->pending_gop_pts = pts;
    vencbin->pending_gop_len = goplength;

    GST_INFO_OBJECT(vencbin, "Set pending gop len %d at pts=%ld",
      vencbin->pending_gop_len, GST_TIME_AS_MSECONDS(vencbin->pending_gop_pts));
  }

  GST_VENC_BIN_UNLOCK (vencbin);
}

void
gst_venc_bin_release_ctrl_buffer (gpointer user_data)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (user_data);
  if (!gst_data_queue_is_empty (vencbin->ctrl_frames)) {
    GstDataQueueItem *item = NULL;
    if (!gst_data_queue_pop (vencbin->ctrl_frames, &item)) {
      GST_INFO_OBJECT (vencbin, "buffers_queue flushing");
      return;
    }
    item->destroy (item);
  }
  GST_DEBUG_OBJECT (vencbin, "Release ctrl buffer");

  return;
}

static void
gst_venc_init_session (GstVideoEncBin * vencbin, GstCaps * caps)
{
  GST_INFO_OBJECT (vencbin, "gst_venc_init_session");

  if (NULL == vencbin || NULL == caps) {
    GST_ERROR_OBJECT (vencbin,
        "unexpected vencbin %p is NULL or caps %p is NULL", vencbin, caps);
    return;
  }

  if (NULL == vencbin->engine) {
    GST_ERROR_OBJECT (vencbin, "ERROR NULL smartCodecEngine");
    return;
  }

  gst_smartcodec_engine_init (vencbin->engine, caps);
}

static void
gst_venc_init_video_crtl_session (GstVideoEncBin * vencbin, GstCaps * caps)
{
  GST_INFO_OBJECT (vencbin, "gst_venc_init_video_crtl_session");

  if (NULL == vencbin || NULL == caps) {
    GST_ERROR_OBJECT (vencbin,
      "unexpected vencbin %p is NULL or caps %p is NULL", vencbin, caps);
    return;
  }

  if (NULL == vencbin->engine) {
    GST_ERROR_OBJECT (vencbin, "ERROR NULL smartCodecEngine");
    return;
  }

  gst_video_info_from_caps (&vencbin->video_ctrl_info, caps);

  // Set config of the engine
  gst_smartcodec_engine_config (vencbin->engine,
      vencbin->smart_framerate_en,
      vencbin->smart_gop_en,
      GST_VIDEO_INFO_WIDTH (&vencbin->video_ctrl_info),
      GST_VIDEO_INFO_HEIGHT (&vencbin->video_ctrl_info),
      vencbin->video_ctrl_info.stride[0],
      vencbin->video_ctrl_info.fps_n,
      vencbin->video_ctrl_info.fps_d,
      vencbin->max_bitrate,
      vencbin->default_goplength,
      vencbin->max_goplength,
      vencbin->levels_override,
      vencbin->roi_qualitys,
      (GstBitrateReceivedCallback) G_CALLBACK (
          on_videoctrl_bitrate_received),
      (GstGOPLengthReceivedCallback) G_CALLBACK (
          on_videoctrl_goplength_received),
      (GstReleaseBufferCallback) G_CALLBACK (
          gst_venc_bin_release_ctrl_buffer),
      vencbin);
}

GstPadProbeReturn
gst_venc_encoder_output_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (user_data);
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  GST_INFO_OBJECT (vencbin, "gst_venc_encoder_output_probe");

  if (!buffer) {
    GST_ERROR_OBJECT (vencbin, "null buffer");
    return GST_PAD_PROBE_DROP;
  }

  if (!vencbin->output_caps_processed) {
    gst_smartcodec_engine_process_output_caps (vencbin->engine,
        gst_pad_get_current_caps (pad));
    vencbin->output_caps_processed = TRUE;
  }

  buffer = gst_buffer_make_writable (buffer);

  if (!buffer) {
    GST_ERROR_OBJECT (vencbin, "failed to make buffer writable");
    return GST_PAD_PROBE_DROP;
  }

  gboolean bSyncFrame = !GST_BUFFER_FLAG_IS_SET (buffer,
      GST_BUFFER_FLAG_DELTA_UNIT);

  gst_smartcodec_engine_process_output_videobuffer (vencbin->engine, buffer,
      bSyncFrame);

  GstClockTime pts_ns = GST_BUFFER_PTS (buffer);
  if (bSyncFrame) {
    GST_DEBUG_OBJECT (vencbin, "New sync frame: PTS - %ld",
        GST_TIME_AS_MSECONDS (pts_ns));
  }

  return GST_PAD_PROBE_OK;
}

void
gst_venc_set_roi_qp (GstVideoEncBin * vencbin, RectDeltaQPs * qps)
{
  GValue roi_rect_params_array = G_VALUE_INIT;
  g_value_init(&roi_rect_params_array, GST_TYPE_ARRAY);

  GST_INFO("%s: %u rois", __func__, qps->num_rectangles);

  for (guint i = 0; i < qps->num_rectangles; ++i) {
    RectDeltaQP rect = qps->mRectangle[i];

    GValue roi_rect_params = G_VALUE_INIT;
    g_value_init (&roi_rect_params, GST_TYPE_ARRAY);

    GValue roi_param = G_VALUE_INIT;
    g_value_init (&roi_param, G_TYPE_INT);

    g_value_set_int (&roi_param, rect.left);
    gst_value_array_append_value (&roi_rect_params, &roi_param);

    g_value_set_int (&roi_param, rect.top);
    gst_value_array_append_value (&roi_rect_params, &roi_param);

    g_value_set_int (&roi_param, rect.width);
    gst_value_array_append_value (&roi_rect_params, &roi_param);

    g_value_set_int (&roi_param, rect.height);
    gst_value_array_append_value (&roi_rect_params, &roi_param);

    g_value_set_int (&roi_param, rect.delta_qp);
    gst_value_array_append_value (&roi_rect_params, &roi_param);

    gst_value_array_append_value (&roi_rect_params_array, &roi_rect_params);

    GST_INFO ("i=%d: lefttop(%d,%d) widthheight(%d,%d) qp=%d",
        i, rect.left, rect.top, rect.width, rect.height, rect.delta_qp);
  }

  guint arrsize = gst_value_array_get_size (&roi_rect_params_array);
  if (arrsize > 0) {
    GST_INFO ("invoke setprop roi-quant-boxes");
    g_object_set_property (G_OBJECT (vencbin->encoder),
        "roi-quant-boxes", &roi_rect_params_array);
  } else {
    GST_INFO ("skip roi-quant-boxes");
  }
}

static void
gst_free_ctrl_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  GstCtrlFrameData *framedata = (GstCtrlFrameData *) item->object;

  gst_video_frame_unmap (&framedata->vframe);
  gst_buffer_unref (framedata->buffer);

  g_slice_free (GstCtrlFrameData, framedata);
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_free_main_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  GstBuffer *buffer = (GstBuffer *) item->object;
  gst_buffer_unref (buffer);
  g_slice_free (GstDataQueueItem, item);
}

GstFlowReturn
gst_venc_bin_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (parent);

  // Put the new main frame in a queue for later processing
  GstDataQueueItem *item = NULL;
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->visible = TRUE;
  item->destroy = gst_free_main_queue_item;
  if (!gst_data_queue_push (vencbin->main_frames, item)) {
    GST_ERROR_OBJECT (vencbin, "ERROR: Cannot push data to the queue!");
    item->destroy (item);
    return GST_FLOW_OK;
  }
  g_cond_signal (&vencbin->wakeup);

  return GST_FLOW_OK;
}

static gboolean
gst_venc_bin_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (parent);

  GST_INFO_OBJECT(vencbin, "Received %s event: %p",
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gst_event_parse_caps (event, &caps);
      gst_venc_init_session (vencbin, caps);
    }
    break;

  default:
    break;
  }

  return gst_pad_event_default (pad, parent, event);
}

GstFlowReturn
gst_venc_bin_sinkctrl_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (parent);
  GstFlowReturn retval = GST_FLOW_OK;
  GstCtrlFrameData *ctrlframedata = g_slice_new0 (GstCtrlFrameData);

  ctrlframedata->buffer = buffer;
  if (!gst_video_frame_map (&ctrlframedata->vframe, &vencbin->video_ctrl_info,
      buffer, (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (vencbin, "frame_map failed");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  // Put the new ctrl frame in a queue for processing
  // It should be released via callback when not needed anymore
  GstDataQueueItem *item = NULL;
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (ctrlframedata);
  item->visible = TRUE;
  item->destroy = gst_free_ctrl_queue_item;
  if (!gst_data_queue_push (vencbin->ctrl_frames, item)) {
    GST_ERROR_OBJECT (vencbin, "ERROR: Cannot push data to the queue!");
    item->destroy (item);
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (vencbin, "Push ctrl buffer");
  gst_smartcodec_engine_push_ctrl_buff (vencbin->engine,
      GST_VIDEO_FRAME_PLANE_DATA (&ctrlframedata->vframe, 0),
      GST_VIDEO_FRAME_PLANE_STRIDE (&ctrlframedata->vframe, 0),
      GST_BUFFER_TIMESTAMP (ctrlframedata->buffer));

  return retval;
}

static gboolean
gst_venc_bin_sinkctrl_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (parent);

  GST_INFO_OBJECT(vencbin, "Received %s event: %p",
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gst_event_parse_caps (event, &caps);
      gst_venc_init_video_crtl_session (vencbin, caps);
    }
    break;

  default:
    break;
  }

  return gst_pad_event_default (pad, parent, event);
}

GstFlowReturn
gst_venc_bin_ml_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (parent);
  GstFlowReturn retval = GST_FLOW_OK;

  GstMapInfo memmap = { };
  if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
    GST_ERROR_OBJECT (vencbin, "Failed to map buffer %p!", buffer);
    return GST_FLOW_OK;
  }

  gchar *data =  g_strndup ((const gchar *) memmap.data, memmap.size);
  if (data) {
    GST_DEBUG_OBJECT (vencbin, "gst_smartcodec_engine_push_ml_buff");
    gst_smartcodec_engine_push_ml_buff (vencbin->engine, data,
        GST_BUFFER_TIMESTAMP (buffer));
    g_free (data);
  } else {
    GST_ERROR_OBJECT (vencbin, "failed null string");
  }

  gst_buffer_unmap (buffer, &memmap);
  gst_buffer_unref (buffer);

  return retval;
}

static void
gst_venc_bin_worker_task (gpointer user_data)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (user_data);
  GstDataQueueSize queuelevel;
  GstDataQueueItem *item = NULL;
  GstBuffer *buffer = NULL;
  GstPad *encoder_sink = NULL;
  gboolean shouldDrop = FALSE;
  guint64 buf_pts = 0;
  guint64 ml_pts = 0;
  guint fps_n = 0;
  guint fps_d = 0;

  GST_VENC_BIN_LOCK (vencbin);

  gst_data_queue_get_level (vencbin->main_frames, &queuelevel);
  if (queuelevel.visible >= vencbin->buff_cnt_delay) {
    if (!gst_data_queue_pop (vencbin->main_frames, &item)) {
      GST_INFO_OBJECT (vencbin, "buffers_queue flushing");
      GST_VENC_BIN_UNLOCK (vencbin);
      return;
    }

    buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    if (NULL == vencbin->encoder) {
      GST_ERROR_OBJECT (vencbin, "failed to get encoder");
      GST_VENC_BIN_UNLOCK (vencbin);
      return;
    }

    encoder_sink = gst_element_get_static_pad (vencbin->encoder, "sink");
    if (NULL == encoder_sink) {
      GST_ERROR_OBJECT (vencbin, "failed to get encoder sink pad");
      GST_VENC_BIN_UNLOCK (vencbin);
      return;
    }

    buf_pts = GST_TIME_AS_MSECONDS (GST_BUFFER_TIMESTAMP (buffer));

    shouldDrop = gst_smartcodec_engine_process_input_videobuffer (
        vencbin->engine, buffer);

    if (FALSE == shouldDrop) {
      RectDeltaQPs rect_delta_qps;
      gst_smartcodec_engine_get_fps (vencbin->engine, &fps_n, &fps_d);
      gboolean has_roi = FALSE;

      while (gst_smartcodec_engine_get_rois_from_queue (vencbin->engine,
          &rect_delta_qps)) {
        ml_pts = GST_TIME_AS_MSECONDS (rect_delta_qps.timestamp);
        has_roi = TRUE;

        if (buf_pts > ml_pts) {
          gst_smartcodec_engine_remove_rois_from_queue (vencbin->engine);
          continue;
        }
        break;
      }

      if (has_roi) {
        GST_DEBUG_OBJECT (vencbin, "buf_pts - %ld, ml_pts - %ld",
            buf_pts, ml_pts);

        if (buf_pts == ml_pts) {
          GST_DEBUG_OBJECT (vencbin, "Number of rectangles set: %d",
              rect_delta_qps.num_rectangles);
          gst_venc_set_roi_qp (vencbin, &rect_delta_qps);
        } else {
          GST_DEBUG_OBJECT (vencbin,
              "ML timestamp is not in sync with HD timestamp");
        }

        if (buf_pts >= ml_pts) {
          gst_smartcodec_engine_remove_rois_from_queue (vencbin->engine);
        }
      }

      guint64 pending_gop_pts = GST_TIME_AS_MSECONDS (vencbin->pending_gop_pts);
      if (pending_gop_pts > 0 &&
          buf_pts >= pending_gop_pts) {

        // Set GOP length to encoder (long gop)
        GST_INFO_OBJECT (vencbin,
            "Increase GOP LEN - %d (default=%d) at pts %ld",
            vencbin->pending_gop_len, vencbin->default_goplength,
            pending_gop_pts);
        g_object_set (G_OBJECT (vencbin->encoder), "idr-interval",
            vencbin->pending_gop_len, NULL);

        vencbin->pending_gop_pts = 0;
        vencbin->pending_gop_len = 0;
      }

      GST_DEBUG_OBJECT (vencbin, "Push video buffer: encode frame");
      gst_pad_chain (encoder_sink, buffer);
    } else {
      GST_DEBUG_OBJECT (vencbin, "drop frame");
      gst_buffer_unref (buffer);
    }

    gst_object_unref (encoder_sink);
  } else {
    if (vencbin->active)
      g_cond_wait (&vencbin->wakeup, GST_VENC_BIN_GET_LOCK (vencbin));
  }

  GST_VENC_BIN_UNLOCK (vencbin);
}

static gboolean
gst_venc_bin_start_worker_task (GstVideoEncBin * vencbin)
{
  GST_VENC_BIN_LOCK (vencbin);

  if (vencbin->active) {
    GST_VENC_BIN_UNLOCK (vencbin);
    return TRUE;
  }

  vencbin->worktask = gst_task_new (gst_venc_bin_worker_task, vencbin, NULL);
  gst_task_set_lock (vencbin->worktask, &vencbin->worklock);

  GST_INFO_OBJECT (vencbin, "Created task %p", vencbin->worktask);

  vencbin->active = TRUE;
  GST_VENC_BIN_UNLOCK (vencbin);

  if (!gst_task_start (vencbin->worktask)) {
    GST_ERROR_OBJECT (vencbin, "Failed to start worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (vencbin, "Started task %p", vencbin->worktask);
  return TRUE;
}

static gboolean
gst_venc_bin_stop_worker_task (GstVideoEncBin * vencbin)
{
  GST_VENC_BIN_LOCK (vencbin);

  if (!vencbin->active) {
    GST_VENC_BIN_UNLOCK (vencbin);
    return TRUE;
  }

  GST_INFO_OBJECT (vencbin, "Stopping task %p", vencbin->worktask);

  if (!gst_task_stop (vencbin->worktask))
    GST_WARNING_OBJECT (vencbin, "Failed to stop worker task!");

  vencbin->active = FALSE;
  g_cond_signal (&vencbin->wakeup);

  GST_VENC_BIN_UNLOCK (vencbin);

  if (!gst_task_join (vencbin->worktask)) {
    GST_ERROR_OBJECT (vencbin, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (vencbin, "Removing task %p", vencbin->worktask);

  gst_object_unref (vencbin->worktask);

  vencbin->worktask = NULL;

  return TRUE;
}

static GstStateChangeReturn
gst_venc_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_data_queue_set_flushing (vencbin->main_frames, FALSE);
      gst_venc_bin_start_worker_task (vencbin);
      gst_data_queue_set_flushing (vencbin->ctrl_frames, FALSE);
      vencbin->output_caps_processed = FALSE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_data_queue_set_flushing (vencbin->main_frames, TRUE);
      gst_data_queue_flush (vencbin->main_frames);
      gst_venc_bin_stop_worker_task (vencbin);
      gst_data_queue_set_flushing (vencbin->ctrl_frames, TRUE);
      gst_data_queue_flush (vencbin->ctrl_frames);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT (vencbin, "Engine flush");
      gst_smartcodec_engine_flush (vencbin->engine);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_venc_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (object);

  GST_VENC_BIN_LOCK (vencbin);

  switch (prop_id) {
    case PROP_ENCODER:
      if (GST_STATE (vencbin) != GST_STATE_NULL) {
        GST_ERROR_OBJECT (vencbin, "Can't set encoder non-NULL state!");
        break;
      }
      vencbin->encoder_type = g_value_get_enum (value);
      gst_venc_bin_update_encoder (vencbin);
      break;
    case PROP_MAX_BITRATE:
    {
      vencbin->max_bitrate = g_value_get_uint (value);
      if (vencbin->encoder != NULL) {
        g_object_set (G_OBJECT (vencbin->encoder), "target-bitrate",
            vencbin->max_bitrate, NULL);
        GST_INFO_OBJECT (vencbin, "Set encoder target bitrate - %d",
            vencbin->max_bitrate);
      }
      break;
    }
    case PROP_SMART_FRAMERATE:
    {
      vencbin->smart_framerate_en = g_value_get_boolean (value);
      break;
    }
    case PROP_SMART_GOP:
    {
      vencbin->smart_gop_en = g_value_get_boolean (value);
      break;
    }
    case PROP_DEFAULT_GOP_LENGTH:
    {
      vencbin->default_goplength = g_value_get_uint (value);
      break;
    }
    case PROP_MAX_GOP_LENGTH:
    {
      vencbin->max_goplength = g_value_get_uint (value);
      break;
    }
    case PROP_LEVELS_OVERRIDE:
    {
      const gchar *input = g_value_get_string (value);
      GValue value = G_VALUE_INIT;

      g_value_init (&value, GST_TYPE_STRUCTURE);

      if (!gst_value_deserialize (&value, input)) {
        GST_ERROR ("Failed to deserialize string!");
        break;
      }

      if (vencbin->levels_override != NULL)
        gst_structure_free (vencbin->levels_override);

      vencbin->levels_override = GST_STRUCTURE (g_value_dup_boxed (&value));
      g_value_unset (&value);
      break;
    }
    case PROP_ROI_QUALITY_CFG:
    {
      const gchar *input = g_value_get_string (value);
      GValue value = G_VALUE_INIT;

      g_value_init (&value, GST_TYPE_STRUCTURE);

      if (!gst_value_deserialize (&value, input)) {
        GST_ERROR ("Failed to deserialize string!");
        break;
      }

      if (vencbin->roi_qualitys != NULL)
        gst_structure_free (vencbin->roi_qualitys);

      vencbin->roi_qualitys = GST_STRUCTURE (g_value_dup_boxed (&value));
      g_value_unset (&value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VENC_BIN_UNLOCK (vencbin);
}

static void
gst_venc_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (object);

  GST_VENC_BIN_LOCK (vencbin);

  switch (prop_id) {
    case PROP_ENCODER:
      g_value_set_enum (value, vencbin->encoder_type);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, vencbin->max_bitrate);
      break;
    case PROP_SMART_FRAMERATE:
      g_value_set_boolean (value, vencbin->smart_framerate_en);
      break;
    case PROP_SMART_GOP:
      g_value_set_boolean (value, vencbin->smart_gop_en);
      break;
    case PROP_DEFAULT_GOP_LENGTH:
      g_value_set_uint (value, vencbin->default_goplength);
      break;
    case PROP_MAX_GOP_LENGTH:
      g_value_set_uint (value, vencbin->max_goplength);
      break;
    case PROP_LEVELS_OVERRIDE:
    {
      gchar *string = NULL;
      if (vencbin->levels_override != NULL) {
        string = gst_structure_to_string (vencbin->levels_override);
        g_value_set_string (value, string);
        g_free (string);
      }

      break;
    }
    case PROP_ROI_QUALITY_CFG:
    {
      gchar *string = NULL;
      if (vencbin->roi_qualitys != NULL) {
        string = gst_structure_to_string (vencbin->roi_qualitys);
        g_value_set_string (value, string);
        g_free (string);
      }

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VENC_BIN_UNLOCK (vencbin);
}

static void
gst_venc_bin_finalize (GObject * object)
{
  GstVideoEncBin *vencbin = GST_VENC_BIN (object);

  if (vencbin->levels_override != NULL)
    gst_structure_free (vencbin->levels_override);

  if (vencbin->roi_qualitys != NULL)
    gst_structure_free (vencbin->roi_qualitys);

  if (vencbin->ctrl_frames != NULL) {
    gst_data_queue_set_flushing (vencbin->ctrl_frames, TRUE);
    gst_data_queue_flush (vencbin->ctrl_frames);
    gst_object_unref (GST_OBJECT_CAST (vencbin->ctrl_frames));
    vencbin->ctrl_frames = NULL;
  }

  if (vencbin->main_frames != NULL) {
    gst_data_queue_set_flushing (vencbin->main_frames, TRUE);
    gst_data_queue_flush (vencbin->main_frames);
    gst_object_unref (GST_OBJECT_CAST (vencbin->main_frames));
    vencbin->main_frames = NULL;
  }

  if (vencbin->syncframe_timestamps != NULL) {
    g_list_free (vencbin->syncframe_timestamps);
    vencbin->syncframe_timestamps = NULL;
  }

  if (vencbin->encoders)
    gst_plugin_feature_list_free (vencbin->encoders);

  if (vencbin->engine) {
    gst_smartcodec_engine_free (vencbin->engine);
    vencbin->engine = NULL;
  }

  g_mutex_clear (&vencbin->lock);
  g_rec_mutex_clear (&vencbin->worklock);
  g_cond_clear (&vencbin->wakeup);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vencbin));
}

static void
gst_venc_bin_class_init (GstVideoEncBinClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_venc_bin_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_venc_bin_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_venc_bin_finalize);

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_venc_bin_main_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_venc_bin_control_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_venc_bin_ml_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_venc_bin_src_template));

  g_object_class_install_property (gobject, PROP_ENCODER,
      g_param_spec_enum ("encoder", "Encoder",
          "Encoder to use (Callable only in NULL state)",
          GST_TYPE_VENC_BIN_ENCODER, DEFAULT_PROP_ENCODER,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max bitrate",
          "Max bitrate in bits per second",
          0, G_MAXUINT, DEFAULT_PROP_MAX_BITRATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_SMART_FRAMERATE,
      g_param_spec_boolean ("smart-framerate", "Smart framerate enable",
          "Enable/Disable smart framerate functionality",
          DEFAULT_PROP_SMART_FRAMERATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_SMART_GOP,
      g_param_spec_boolean ("smart-gop", "Smart GOP enable",
          "Enable/Disable smart GOP functionality",
          DEFAULT_PROP_SMART_GOP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_DEFAULT_GOP_LENGTH,
      g_param_spec_uint ("default-gop", "Default GOP length",
          "Default GOP length",
          0, G_MAXUINT, DEFAULT_PROP_DEFAULT_GOP_LENGTH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MAX_GOP_LENGTH,
      g_param_spec_uint ("max-gop", "Max GOP length",
          "Max GOP length",
          0, G_MAXUINT, DEFAULT_PROP_MAX_GOP_LENGTH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_LEVELS_OVERRIDE,
      g_param_spec_string ("levels-override", "Levels override",
          "Override bitrate and FR levels "
          "e.g. \"LevelsOverride,bitrate_static=160000,bitrate_low=358000,"
          "bitrate_medium=700000,bitrate_high=1400000,fr_static=15,fr_low=3,"
          "fr_medium=1,fr_high=0;\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_ROI_QUALITY_CFG,
      g_param_spec_string ("roi-quality-cfg", "ROI Quality Config",
          "ROI Quality Config "
          "e.g. \"ROIQPs,car=2,person=1,tree=-2;\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element, "Smart Video Encode Bin",
      "Generic/Bin/Encoder", "Smart control over video encoding", "QTI");

  element->change_state = GST_DEBUG_FUNCPTR (gst_venc_bin_change_state);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_venc_bin_init (GstVideoEncBin * vencbin)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&vencbin->lock);

  // Load all available encoder plugins.
  vencbin->encoders = gst_element_factory_list_get_elements (
      GST_ELEMENT_FACTORY_TYPE_ENCODER, GST_RANK_MARGINAL);

  vencbin->encoder = NULL;
  vencbin->engine = NULL;
  vencbin->worktask = NULL;
  vencbin->active = FALSE;
  vencbin->syncframe_timestamps = NULL;
  vencbin->output_caps_processed = FALSE;

  vencbin->pending_gop_pts = 0;
  vencbin->pending_gop_len = 0;

  g_rec_mutex_init (&vencbin->worklock);
  g_cond_init (&vencbin->wakeup);
  vencbin->max_bitrate = DEFAULT_PROP_MAX_BITRATE;
  vencbin->smart_framerate_en = DEFAULT_PROP_SMART_FRAMERATE;
  vencbin->smart_gop_en = DEFAULT_PROP_SMART_GOP;
  vencbin->default_goplength = DEFAULT_PROP_DEFAULT_GOP_LENGTH;
  vencbin->max_goplength = DEFAULT_PROP_MAX_GOP_LENGTH;

  vencbin->levels_override = NULL;
  vencbin->roi_qualitys = NULL;

  gst_video_info_init (&vencbin->video_ctrl_info);
  vencbin->ctrl_frames =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  vencbin->main_frames =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);

  // Create sink proxy pad.
  template = gst_static_pad_template_get (&gst_venc_bin_main_sink_template);
  vencbin->sinkpad =
      gst_ghost_pad_new_no_target_from_template ("sink", template);
  gst_object_unref (template);
  gst_pad_set_event_function (vencbin->sinkpad,
      GST_DEBUG_FUNCPTR (gst_venc_bin_sink_pad_event));
  gst_pad_set_chain_function (vencbin->sinkpad,
      GST_DEBUG_FUNCPTR (gst_venc_bin_sink_pad_chain));

  gst_pad_set_active (vencbin->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (vencbin), vencbin->sinkpad);

  // Create sink_control proxy pad.
  template = gst_static_pad_template_get (&gst_venc_bin_control_sink_template);
  vencbin->sinkctrlpad = gst_pad_new_from_template (template, "sink_ctrl");
  gst_object_unref (template);
  gst_pad_set_event_function (vencbin->sinkctrlpad,
      GST_DEBUG_FUNCPTR (gst_venc_bin_sinkctrl_pad_event));
  gst_pad_set_chain_function (vencbin->sinkctrlpad,
      GST_DEBUG_FUNCPTR (gst_venc_bin_sinkctrl_pad_chain));

  gst_pad_set_active (vencbin->sinkctrlpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (vencbin), vencbin->sinkctrlpad);

  // Create sink_ml proxy pad.
  template = gst_static_pad_template_get (&gst_venc_bin_ml_sink_template);
  vencbin->sinkmlpad = gst_pad_new_from_template (template, "sink_ml");
  gst_object_unref (template);
  gst_pad_set_chain_function (vencbin->sinkmlpad,
      GST_DEBUG_FUNCPTR (gst_venc_bin_ml_pad_chain));

  gst_pad_set_active (vencbin->sinkmlpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (vencbin), vencbin->sinkmlpad);

  // Create src proxy pad.
  template = gst_static_pad_template_get (&gst_venc_bin_src_template);
  vencbin->srcpad = gst_ghost_pad_new_no_target_from_template ("src", template);
  gst_object_unref (template);
  GST_INFO_OBJECT (vencbin, "Adding probe to encoder src pad");
  gst_pad_add_probe (vencbin->srcpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) gst_venc_encoder_output_probe, vencbin, NULL);

  gst_pad_set_active (vencbin->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (vencbin), vencbin->srcpad);

  vencbin->engine = gst_smartcodec_engine_new ();
  if (NULL == vencbin->engine) {
    GST_ERROR_OBJECT (vencbin, "Failed to create engine");
    return;
  }

  // Get buffer count delay
  vencbin->buff_cnt_delay =
      gst_smartcodec_engine_get_buff_cnt_delay (vencbin->engine);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  // Initializes a new GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_venc_bin_debug, "qtismartvencbin", 0,
      "QTI Smart Video Encode Bin");

  return gst_element_register (plugin, "qtismartvencbin", GST_RANK_NONE,
      GST_TYPE_VENC_BIN);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtismartvencbin,
    "QTI Smart Video Encode Bin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
