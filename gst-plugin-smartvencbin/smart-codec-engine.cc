/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "smart-codec-engine.h"

#include <sys/time.h>
#include <dlfcn.h>
#include <mutex>

#include <iot-core-algs/videoctrl.h>

struct _SmartCodecEngine {
  std::mutex           engine_lock;
  GstVideoInfo         video_info;
  GstClockTime         last_buffer_ts;
  GstDataQueue         *ml_rois_queue;

  // VideoCtrl library handle.
  gpointer             videoctrlhandle;
  // VideoCtrl engine interface.
  ::videoctrl::IEngine *videoctrlengine;
};

#define GST_CAT_DEFAULT smartcodecbin_engine_category ()

static GstDebugCategory*
smartcodecbin_engine_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone =
        (gsize) _gst_debug_category_new ("smart-codec-engine",
        0, "Smart Codec engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

SmartCodecEngine *
gst_smartcodec_engine_new ()
{
  SmartCodecEngine *engine = NULL;
  ::videoctrl::NewIEngine NewVideoCtrlEngine;

  std::string libname = "libVideoCtrl.so." + std::string(VIDEO_CTRL_VERSION_MAJOR);

  engine = g_new0 (SmartCodecEngine, 1);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->last_buffer_ts = -1;
  gst_video_info_init (&engine->video_info);

  if ((engine->videoctrlhandle = dlopen (libname.c_str(), RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open VideoCtrl library, error: %s!", dlerror());
    goto cleanup;
  }

  NewVideoCtrlEngine = (::videoctrl::NewIEngine) dlsym (engine->videoctrlhandle,
      VIDEOCTRL_ENGINE_NEW_FUNC);
  if (NewVideoCtrlEngine == NULL) {
    GST_ERROR ("Failed to load VideoCtrl symbol, error: %s!", dlerror ());
    goto cleanup;
  }

  try {
    engine->videoctrlengine = NewVideoCtrlEngine ();
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create and init new engine, error: '%s'!", e.what ());
    goto cleanup;
  }

  GST_INFO ("Created smartcodec engine: %p", engine);
  return engine;

cleanup:
  gst_smartcodec_engine_free (engine);
  return NULL;
}

void
gst_smartcodec_engine_free (SmartCodecEngine * engine)
{
  GST_INFO ("Destroyed smartcodec engine: %p", engine);

  if (engine->videoctrlengine != NULL)
    delete engine->videoctrlengine;

  if (engine->videoctrlhandle != NULL)
    dlclose (engine->videoctrlhandle);

  if (engine->ml_rois_queue != NULL) {
    gst_data_queue_set_flushing (engine->ml_rois_queue, TRUE);
    gst_data_queue_flush (engine->ml_rois_queue);
    gst_object_unref (GST_OBJECT_CAST (engine->ml_rois_queue));
    engine->ml_rois_queue = NULL;
  }

  g_free (engine);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

void
gst_smartcodec_engine_init (SmartCodecEngine * engine,
    GstCaps * caps)
{
  GST_INFO ("initializing tsStore and frame dropper");

  gst_video_info_from_caps (&engine->video_info, caps);

  engine->ml_rois_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
}

static gboolean
roi_quality_item (GQuark id, const GValue * value, gpointer data)
{
  ::videoctrl::Config *config = (::videoctrl::Config *) data;

  ::videoctrl::RoiQualitys q;
  g_strlcpy (q.qualitys_lb, g_quark_to_string (id), sizeof (q.qualitys_lb));
  q.qualitys_qp = g_value_get_int (value);
  config->roi_qualitys.push_back (q);

  GST_INFO ("ROI QPs: %s, qp: %d", q.qualitys_lb, q.qualitys_qp);

  return TRUE;
}

void
gst_smartcodec_engine_process_output_caps (SmartCodecEngine * engine,
    GstCaps *caps)
{
  GstStructure *structure = NULL;

  // Check which encoder is used and configure video-ctrl lib
  if (caps) {
    structure = gst_caps_get_structure (caps, 0);

    if (gst_structure_has_name (structure, "video/x-h264")) {
      engine->videoctrlengine->SetEncoderType (::videoctrl::ENCODER_H264);
    } else if (gst_structure_has_name (structure, "video/x-h265")) {
      engine->videoctrlengine->SetEncoderType (::videoctrl::ENCODER_H265);
    }
  }
}

void
gst_smartcodec_engine_config (SmartCodecEngine * engine,
    gboolean smart_framerate_en,
    gboolean smart_gop_en,
    guint width,
    guint height,
    guint stride,
    guint fps_ctrl_n,
    guint fps_ctrl_d,
    guint max_bitrate,
    guint default_goplength,
    guint max_goplength,
    GstStructure * levels_override,
    GstStructure * roi_qualitys,
    GstBitrateReceivedCallback bitrate_callback,
    GstGOPLengthReceivedCallback goplength_callback,
    GstReleaseBufferCallback release_buffer_callback,
    gpointer user_data)
{
  guint idx = 0;
  ::videoctrl::Config config;

  config.smart_framerate_en = smart_framerate_en;
  config.smart_gop_en = smart_gop_en;

  config.fps_main_n = engine->video_info.fps_n;
  config.fps_main_d = engine->video_info.fps_d;
  config.fps_ctrl_n = fps_ctrl_n;
  config.fps_ctrl_d = fps_ctrl_d;

  config.hd_width = GST_VIDEO_INFO_WIDTH (&engine->video_info);
  config.hd_height = GST_VIDEO_INFO_HEIGHT (&engine->video_info);
  config.width = width;
  config.height = height;
  config.stride = stride;
  config.max_bitrate = max_bitrate;
  config.default_gop_len = default_goplength;
  config.max_gop_len = max_goplength;

  config.callbacks.bitrate_callback = bitrate_callback;
  config.callbacks.goplength_callback = goplength_callback;
  config.callbacks.release_buffer_callback = release_buffer_callback;
  config.callbacks.user_data = user_data;

  if (levels_override &&
      gst_structure_has_name (levels_override, "LevelsOverride")) {
    GST_INFO ("Has level override values");

    if (gst_structure_has_field (levels_override, "bitrate_static")) {
      ::videoctrl::LevelBitrate t;
      t.level = ::videoctrl::BITRATE_LEVEL_STATIC;
      gst_structure_get_int (levels_override, "bitrate_static",
          (int32_t *) (&t.bitrate));
      config.bitrate_levels_override.push_back (t);
      GST_INFO ("Override Bitrate level: %d, bitrate: %d", t.level, t.bitrate);
    }

    if (gst_structure_has_field (levels_override, "bitrate_low")) {
      ::videoctrl::LevelBitrate t;
      t.level = ::videoctrl::BITRATE_LEVEL_LOW;
      gst_structure_get_int (levels_override, "bitrate_low",
          (int32_t *) (&t.bitrate));
      config.bitrate_levels_override.push_back (t);
      GST_INFO ("Override Bitrate level: %d, bitrate: %d", t.level, t.bitrate);
    }

    if (gst_structure_has_field (levels_override, "bitrate_medium")) {
      ::videoctrl::LevelBitrate t;
      t.level = ::videoctrl::BITRATE_LEVEL_MED;
      gst_structure_get_int (levels_override, "bitrate_medium",
          (int32_t *) (&t.bitrate));
      config.bitrate_levels_override.push_back (t);
      GST_INFO ("Override Bitrate level: %d, bitrate: %d", t.level, t.bitrate);
    }

    if (gst_structure_has_field (levels_override, "bitrate_high")) {
      ::videoctrl::LevelBitrate t;
      t.level = ::videoctrl::BITRATE_LEVEL_HIGH;
      gst_structure_get_int (levels_override, "bitrate_high",
          (int32_t *) (&t.bitrate));
      config.bitrate_levels_override.push_back (t);
      GST_INFO ("Override Bitrate level: %d, bitrate: %d", t.level, t.bitrate);
    }

    if (gst_structure_has_field (levels_override, "fr_static")) {
      ::videoctrl::LevelFR t;
      t.level = ::videoctrl::FR_LEVEL_STATIC;
      gst_structure_get_int (levels_override, "fr_static",
          (int32_t *) (&t.frdivider));
      config.fr_levels_override.push_back (t);
      GST_INFO ("Override FR level: %d, frames: %d", t.level, t.frdivider);
    }

    if (gst_structure_has_field (levels_override, "fr_low")) {
      ::videoctrl::LevelFR t;
      t.level = ::videoctrl::FR_LEVEL_LOW;
      gst_structure_get_int (levels_override, "fr_low",
          (int32_t *) (&t.frdivider));
      config.fr_levels_override.push_back (t);
      GST_INFO ("Override FR level: %d, frames: %d", t.level, t.frdivider);
    }

    if (gst_structure_has_field (levels_override, "fr_medium")) {
      ::videoctrl::LevelFR t;
      t.level = ::videoctrl::FR_LEVEL_MED;
      gst_structure_get_int (levels_override, "fr_medium",
          (int32_t *) (&t.frdivider));
      config.fr_levels_override.push_back (t);
      GST_INFO ("Override FR level: %d, frames: %d", t.level, t.frdivider);
    }

    if (gst_structure_has_field (levels_override, "fr_high")) {
      ::videoctrl::LevelFR t;
      t.level = ::videoctrl::FR_LEVEL_HIGH;
      gst_structure_get_int (levels_override, "fr_high",
          (int32_t *) (&t.frdivider));
      config.fr_levels_override.push_back (t);
      GST_INFO ("Override FR level: %d, frames: %d", t.level, t.frdivider);
    }
  }

  if (roi_qualitys && gst_structure_has_name (roi_qualitys, "ROIQPs")) {
    GST_INFO ("Has ROI QP values");
    gst_structure_foreach (roi_qualitys, roi_quality_item, &config);
  }

  engine->videoctrlengine->SetConfig (&config);
}

void
gst_smartcodec_engine_update_fr_divider (SmartCodecEngine * engine,
    guint fr_divider)
{
  GST_INFO ("set fr_divider=%u", fr_divider);
  engine->videoctrlengine->UpdateFrDivider (fr_divider);
}

gboolean
gst_smartcodec_engine_process_input_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer)
{
  gboolean should_drop = FALSE;
  GstClockTime mod_ts_ns = 0;
  GstClockTime interframe_delta = 0;
  GstClockTime buf_ts = GST_BUFFER_PTS (buffer);

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    should_drop = FALSE;
    GST_ERROR ("%s: invalid TS", __func__);
    return FALSE;
  }

  if (-1 != engine->last_buffer_ts) {
    interframe_delta = buf_ts - engine->last_buffer_ts;
  }

  engine->last_buffer_ts = buf_ts;

  should_drop = engine->videoctrlengine->FrameDropNeeded (buf_ts, mod_ts_ns,
      interframe_delta);
  if (FALSE == should_drop) {
    GST_BUFFER_PTS (buffer) = mod_ts_ns;
  }

  return should_drop;
}

void
gst_smartcodec_engine_process_output_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer, gboolean bSyncFrame)
{
  // Modify the encoded frame's timestamp
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    GstClockTime dts_ns = GST_BUFFER_DTS (buffer);
    GstClockTime pts_ns = GST_BUFFER_PTS (buffer);

    GST_INFO ("%s: buffer TS (encoder to plugin) GST_BUFFER_PTS=%lu "
        "GST_BUFFER_DTS=%lu", __func__,
        GST_TIME_AS_MSECONDS (pts_ns), GST_TIME_AS_MSECONDS (dts_ns));

    guint duration = 0;
    gboolean res = engine->videoctrlengine->GetOutBuffTS (dts_ns, pts_ns,
        duration);
    if (!res)
      GST_ERROR ("failed to GetOutBuffTS");


    GST_BUFFER_DTS (buffer) = dts_ns;
    GST_BUFFER_PTS (buffer) = pts_ns;

    GST_INFO ("buffer TS (encoder to plugin) GST_BUFFER_PTS=%lu "
        "GST_BUFFER_DTS=%lu duration=%lu",
        GST_TIME_AS_MSECONDS (pts_ns), GST_TIME_AS_MSECONDS (dts_ns),
        GST_TIME_AS_MSECONDS (duration));

    if (duration > 0) {
      GST_INFO ("buffer duration updated from %lu to %u (ms)",
          GST_BUFFER_DURATION (buffer), duration);
      GST_BUFFER_DURATION (buffer) = duration;
    }

    engine->videoctrlengine->ProcessOutputBuffer (bSyncFrame, pts_ns);
  }
}

static void
gst_free_ml_rois_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  RectDeltaQPs *rect_delta_qps = (RectDeltaQPs *) item->object;
  g_free (rect_delta_qps);
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_smartcodec_engine_handle_rois (SmartCodecEngine * engine,
    std::vector<::videoctrl::RoiQPRectangle>& rects, guint64 timestamp)
{
  std::lock_guard<std::mutex> lock (engine->engine_lock);

  GstDataQueueItem *item = NULL;
  RectDeltaQPs *rect_delta_qps = g_new0 (RectDeltaQPs, 1);

  rect_delta_qps->num_rectangles = 0;
  rect_delta_qps->timestamp = timestamp;

  const int resolution_width  = GST_VIDEO_INFO_WIDTH (&engine->video_info);
  const int resolution_height = GST_VIDEO_INFO_HEIGHT (&engine->video_info);

  for(uint32_t i = 0; i < rects.size(); i++) {
    GST_INFO ("i=%d left,top(%.4f,%.4f) right,bottom(%.4f,%.4f) QP=%d Label=%s",
        i, rects[i].left, rects[i].top, rects[i].right,
        rects[i].bottom, rects[i].qp, rects[i].label);

    int left_val   = ABS (rects[i].left   * resolution_width);
    int top_val    = ABS (rects[i].top    * resolution_height);
    int right_val  = ABS (rects[i].right  * resolution_width);
    int bottom_val = ABS (rects[i].bottom * resolution_height);

    if (left_val < 0) {
      GST_INFO ("clipped left_val from %d to 0", left_val);
      left_val = 0;
    }

    if (top_val < 0) {
      GST_INFO ("clipped top_val from %d to 0", top_val);
      top_val = 0;
    }

    if (right_val >= resolution_width) {
      GST_INFO ("clipped right_val from %d to %d",
          right_val, resolution_width - 1);
      right_val = resolution_width - 1;
    }

    if (bottom_val >= resolution_height) {
      GST_INFO ("clipped right_val from %d to %d",
          bottom_val, resolution_height - 1);
      bottom_val = resolution_height - 1;
    }

    // Clip width and height if it outside the frame limits.
    int width = right_val - left_val + 1;
    int height = bottom_val - top_val + 1;

    GST_INFO ("%s: ABS left,top:(%d,%d), right,bottom:(%d,%d) "
        "roi_width=%d roi_height=%d", __func__, left_val,
        top_val, right_val, bottom_val, width, height);

    const guint arrayIdx = rect_delta_qps->num_rectangles;
    rect_delta_qps->mRectangle[arrayIdx].left = left_val;
    rect_delta_qps->mRectangle[arrayIdx].top = top_val;
    rect_delta_qps->mRectangle[arrayIdx].width = width;
    rect_delta_qps->mRectangle[arrayIdx].height = height;
    rect_delta_qps->mRectangle[arrayIdx].delta_qp = rects[i].qp;
    ++rect_delta_qps->num_rectangles;
  }

  // Push rois in the queue
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (rect_delta_qps);
  item->visible = TRUE;
  item->destroy = gst_free_ml_rois_queue_item;
  if (!gst_data_queue_push (engine->ml_rois_queue, item)) {
    GST_ERROR ("ERROR: Cannot push data to the queue!");
    item->destroy (item);
  }
}

void
gst_smartcodec_engine_get_fps (SmartCodecEngine * engine, guint * n, guint * d)
{
  *n = engine->video_info.fps_n;
  *d = engine->video_info.fps_d;
}

gboolean
gst_smartcodec_engine_get_rois_from_queue (SmartCodecEngine * engine,
    RectDeltaQPs * rect_delta_qps)
{
  std::lock_guard<std::mutex> lock (engine->engine_lock);
  GstDataQueueItem *item = NULL;
  RectDeltaQPs *rect_delta_qps_itm = NULL;

  if (gst_data_queue_is_empty (engine->ml_rois_queue)) {
    return FALSE;
  }

  if (!gst_data_queue_peek (engine->ml_rois_queue, &item)) {
    GST_INFO ("ml_rois_queue flushing");
    return FALSE;
  }
  rect_delta_qps_itm = (RectDeltaQPs *) item->object;

  for (int i=0; i < rect_delta_qps_itm->num_rectangles; ++i) {
    rect_delta_qps->mRectangle[i] = rect_delta_qps_itm->mRectangle[i];
  }
  rect_delta_qps->num_rectangles = rect_delta_qps_itm->num_rectangles;
  rect_delta_qps->timestamp = rect_delta_qps_itm->timestamp;
  return TRUE;
}

void
gst_smartcodec_engine_remove_rois_from_queue (SmartCodecEngine * engine)
{
  std::lock_guard<std::mutex> lock (engine->engine_lock);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_is_empty (engine->ml_rois_queue)) {
    return;
  }

  if (!gst_data_queue_pop (engine->ml_rois_queue, &item)) {
    GST_INFO ("buffers_queue flushing");
    return;
  }

  item->destroy (item);
}

void
gst_smartcodec_engine_push_ctrl_buff (SmartCodecEngine * engine, guint8 * buff,
    guint32 stride, guint64 timestamp)
{
  if (NULL == buff) {
    GST_ERROR ("invalid buff");
    return;
  }

  engine->videoctrlengine->PushBuffer (buff, stride, timestamp);
}

void
gst_smartcodec_engine_push_ml_buff (SmartCodecEngine * engine, gchar * data,
    guint64 timestamp)
{
  if (NULL == data) {
    GST_ERROR ("invalid data");
    return;
  }

  std::vector<::videoctrl::RoiQPRectangle> rects;

  char *remaining = data, *token = NULL;
  guint idx = 0;

  while ((token = strtok_r (remaining, "\n", &remaining))) {
    GValue value_list = G_VALUE_INIT;
    g_value_init (&value_list, GST_TYPE_LIST);
    bool success = gst_value_deserialize (&value_list, token);
    GST_DEBUG ("idx=%u token='%s'", idx, token);

    if (!success) {
      GST_ERROR ("failed to deserialize data");
      continue;
    }

    const guint list_size = gst_value_list_get_size (&value_list);

    for (guint i = 0; i < list_size; ++i) {
      ::videoctrl::RoiQPRectangle rect;
      const GValue *list_entry = gst_value_list_get_value (&value_list, i);
      GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (list_entry));

      if (!gst_structure_has_name (structure, "ObjectDetection")) {
        GST_DEBUG("gst_structure ObjectDetection not found");
        continue;
      }

      // Fetch bounding box rectangle if it exists and fill ROI coordinates.
      const GValue *bounding_boxes =
        gst_structure_get_value(structure, "bounding-boxes");

      if (NULL == bounding_boxes) {
        GST_DEBUG("failed to get bounding-boxes");
        continue;
      }

      guint size = gst_value_array_get_size (bounding_boxes);
      GST_DEBUG("got %u bounding-boxes", size);

      for (guint idx = 0; idx < size; idx++) {
        const GValue *value = gst_value_array_get_value (bounding_boxes, idx);
        GstStructure *roi_entry = GST_STRUCTURE (g_value_get_boxed (value));
        if (!roi_entry) {
          GST_ERROR("no roi_entry for idx %d", idx);
          continue;
        }

        const gchar *label = gst_structure_get_name(roi_entry);
        double confidence = 0;
        gst_structure_get_double(roi_entry, "confidence", &confidence);

        if (NULL == label) {
          continue;
        }

        // Fetch bounding box rectangle if it exists and fill ROI coordinates.
        value = gst_structure_get_value (roi_entry, "rectangle");

        if (gst_value_array_get_size (value) != 4) {
          GST_ERROR ("Badly formed ROI rectangle, expected 4 "
            "entries but received %u!", gst_value_array_get_size (value));
          continue;
        }

        rect.left = g_value_get_float (gst_value_array_get_value (value, 0));
        rect.top = g_value_get_float (gst_value_array_get_value (value, 1));
        gfloat width = g_value_get_float (gst_value_array_get_value (value, 2));
        gfloat height = g_value_get_float (gst_value_array_get_value (value, 3));
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;

        GST_DEBUG("bbox %u:  Label='%s' Confidence %.3lf "
          "[left,top](%.3f,%.3f) [width,height]:%.3f,%.3f",
          idx, label, confidence, rect.left, rect.top, width, height);

        g_strlcpy (rect.label, label, sizeof (rect.label));
        rects.push_back (rect);
      }
    }

    ++idx;
    g_value_unset (&value_list);
  }

  engine->videoctrlengine->PushMLData (rects);
  gst_smartcodec_engine_handle_rois (engine, rects, timestamp);
}

guint
gst_smartcodec_engine_get_buff_cnt_delay (SmartCodecEngine * engine)
{
  return engine->videoctrlengine->GetBuffCntDelay ();
}

void
gst_smartcodec_engine_flush (SmartCodecEngine * engine)
{
  engine->videoctrlengine->Flush ();
}
