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
  guint                cur_fr_divider;
  GstClockTime         last_buffer_ts;
  guint                stats_mask;
  BwStats              bw_stats;
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

  engine = g_new0 (SmartCodecEngine, 1);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->cur_fr_divider = 1;
  engine->last_buffer_ts = 0;
  engine->stats_mask = 0;
  gst_video_info_init (&engine->video_info);

  engine->bw_stats.total_size = 0;
  engine->bw_stats.total_frames = 0;
  engine->bw_stats.initial_time_ms = 0;
  engine->bw_stats.last_stats_time_ms = 0;
  engine->bw_stats.next_stats_time_ms = 0;

  if ((engine->videoctrlhandle =
      dlopen ("libVideoCtrl.so", RTLD_NOW)) == NULL) {
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
    GstCaps * caps, guint stats_mask)
{
  GST_INFO ("initializing tsStore and frame dropper");

  gst_video_info_from_caps (&engine->video_info, caps);
  engine->stats_mask = stats_mask;

  engine->ml_rois_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
}

void
gst_smartcodec_engine_config (SmartCodecEngine * engine,
    gboolean smart_bitrate_en,
    gboolean smart_framerate_en,
    gboolean smart_gop_en,
    guint width,
    guint height,
    guint stride,
    guint fps_ctrl_n,
    guint fps_ctrl_d,
    guint target_bitrate,
    guint initial_goplength,
    guint long_goplength,
    guint gop_threshold,
    GArray * bitrate_thresholds,
    GArray * framerate_thresholds,
    GArray * roi_qualitys,
    GstBitrateReceivedCallback bitrate_callback,
    GstFRDeviderReceivedCallback fr_callback,
    GstGOPLengthReceivedCallback goplength_callback,
    GstReleaseBufferCallback release_buffer_callback,
    gpointer user_data)
{
  guint idx = 0;
  ::videoctrl::Config config;

  config.smart_bitrate_en = smart_bitrate_en;
  config.smart_framerate_en = smart_framerate_en;
  config.smart_gop_en = smart_gop_en;

  config.fps_main_n = engine->video_info.fps_n;
  config.fps_main_d = engine->video_info.fps_d;
  config.fps_ctrl_n = fps_ctrl_n;
  config.fps_ctrl_d = fps_ctrl_d;

  config.width = width;
  config.height = height;
  config.stride = stride;
  config.target_bitrate = target_bitrate;
  config.gop_len = initial_goplength;
  config.long_gop_len = long_goplength;
  config.gop_threshold = gop_threshold;

  config.callbacks.bitrate_callback = bitrate_callback;
  config.callbacks.fr_callback = fr_callback;
  config.callbacks.goplength_callback = goplength_callback;
  config.callbacks.release_buffer_callback = release_buffer_callback;
  config.callbacks.user_data = user_data;

  for (idx = 0; idx < bitrate_thresholds->len; idx++) {
    guint *values =
        (guint *) g_array_index (bitrate_thresholds, gpointer, idx);
    ::videoctrl::BitrateThres t;
    t.threshold = values[0];
    t.reduction = values[1];
    config.bitrate_thresholds.push_back (t);
  }

  for (idx = 0; idx < framerate_thresholds->len; idx++) {
    guint thres = g_array_index (framerate_thresholds, guint, idx);
    config.framerate_thresholds.push_back (thres);
  }

  for (idx = 0; idx < roi_qualitys->len; idx++) {
    char **values = (char **) g_array_index (roi_qualitys, gpointer, idx);
    ::videoctrl::RoiQualitys q;
    g_strlcpy (q.qualitys_lb, values[0], sizeof (q.qualitys_lb));
    g_strlcpy (q.qualitys_qp, values[1], sizeof (q.qualitys_qp));
    config.roi_qualitys.push_back (q);
  }

  engine->videoctrlengine->SetConfig (&config);
}

void
gst_smartcodec_engine_update_fr_divider (SmartCodecEngine * engine,
    guint fr_divider)
{
  GST_INFO ("set fr_divider=%u", fr_divider);
  engine->videoctrlengine->UpdateFrDivider (fr_divider);
  engine->cur_fr_divider = fr_divider;
}

gboolean
gst_smartcodec_engine_process_input_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer)
{
  gboolean should_drop = FALSE;
  GstClockTime mod_ts_ns = 0;

  const GstClockTime buf_ts = GST_BUFFER_PTS (buffer);

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    should_drop = FALSE;
    GST_ERROR ("%s: invalid TS", __func__);
    return FALSE;
  }

  GstClockTime interframe_delta = 0;

  if (0 != engine->last_buffer_ts) {
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
    GstBuffer * buffer)
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
      GST_ERROR("failed to GetOutBuffTS");


    GST_BUFFER_DTS (buffer) = dts_ns;
    GST_BUFFER_PTS (buffer) = pts_ns;

    GST_INFO ("buffer TS (encoder to plugin) GST_BUFFER_PTS=%lu "
        "GST_BUFFER_DTS=%lu duration=%lu",
        GST_TIME_AS_MSECONDS (pts_ns), GST_TIME_AS_MSECONDS (dts_ns),
        GST_TIME_AS_MSECONDS (duration));

    if (duration > 0) {
      GST_INFO ("buffer duration updated from %lu to %u (ms)",
          GST_BUFFER_DURATION(buffer), duration);
      GST_BUFFER_DURATION (buffer) = duration;
    }
  }

  gst_smartcodec_engine_check_elapsed_time_and_print_bwstats(engine);

  if (engine->stats_mask & STATS_BANDWIDTH) {
    GstMapInfo map_info;
    gst_buffer_map (buffer, &map_info, GST_MAP_READ);

    // Get the video frame size
    guint frame_size = map_info.size;

    // Get the presentation timestamp (PTS)
    GstClockTime pts = GST_BUFFER_PTS (buffer);
    GstClockTime dts = GST_BUFFER_DTS (buffer);
    // Unmap the buffer's memory
    gst_buffer_unmap (buffer, &map_info);

    engine->bw_stats.total_size += frame_size;
    GST_INFO ("frame_size=%u PTS=%lu DTS=%lu (duration=%lu)",
        frame_size,
        GST_TIME_AS_MSECONDS (pts), GST_TIME_AS_MSECONDS (dts),
        GST_TIME_AS_MSECONDS (GST_BUFFER_DURATION (buffer)));
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
gst_smartcodec_engine_check_elapsed_time_and_print_bwstats (
    SmartCodecEngine * engine)
{
  const guint64 stats_duration_ms = 1000 * engine->cur_fr_divider;
  struct timeval current_time;
  gettimeofday (&current_time, NULL);

  guint64 sysTimeMs = (1000 * current_time.tv_sec) +
      current_time.tv_usec / 1000;

  if (0 == engine->bw_stats.last_stats_time_ms) {
    // first time
    engine->bw_stats.initial_time_ms = sysTimeMs;
    engine->bw_stats.last_stats_time_ms = sysTimeMs;
  }

  guint64 next_stats_time_ms =
      engine->bw_stats.last_stats_time_ms + stats_duration_ms;

  if (sysTimeMs >= next_stats_time_ms) {
    engine->bw_stats.last_stats_time_ms = next_stats_time_ms;
    int calcBitrateKbps = (8.0 * engine->bw_stats.total_size) /
        (stats_duration_ms / 1000.0);
    calcBitrateKbps /= 1024;

    GST_INFO ("calcBitrateKbps ElapsedTime: %lu CalcBitrate: %d divider=%d",
        sysTimeMs - engine->bw_stats.initial_time_ms,
        calcBitrateKbps, engine->cur_fr_divider);

    engine->bw_stats.total_size = 0;
  }
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

  char *next = NULL, *remaining = data;
  int32_t idx = 0;
  char *token = NULL;

  while ((token = strtok_r (remaining, "\n", &remaining))) {
    GValue value_list = G_VALUE_INIT;
    g_value_init (&value_list, GST_TYPE_LIST);
    bool success = gst_value_deserialize (&value_list, token);
    GST_DEBUG ("idx=%d token='%s'", idx, token);

    if (!success) {
      GST_DEBUG ("failed to deserialize data");
      continue;
    }

    const uint32_t list_size = gst_value_list_get_size (&value_list);
    GST_DEBUG ("list_size=%d", list_size);

    for (int32_t i = 0; i < list_size; ++i) {
      ::videoctrl::RoiQPRectangle rect;
      const GValue *list_entry = gst_value_list_get_value (&value_list, i);
      GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (list_entry));

      // Skip the 'Parameters' structure as this is not a prediction result.
      if (gst_structure_has_name (structure, "Parameters")) {
        GST_INFO ("skip parameters at idx %d", i);
        continue;
      }

      // Fetch bounding box rectangle if it exists and fill ROI coordinates.
      const GValue *entry = gst_structure_get_value (structure, "rectangle");
      if (NULL == entry) {
        continue;
      }

      if (gst_value_array_get_size (entry) != 4) {
        GST_DEBUG ("Badly formed ROI rectangle, expected 4 "
          "entries but received %u!", gst_value_array_get_size (entry));
        continue;
      }

      float left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
      const char *label = NULL;

      rect.top    = g_value_get_float (gst_value_array_get_value (entry, 0));
      rect.left   = g_value_get_float (gst_value_array_get_value (entry, 1));
      rect.bottom = g_value_get_float (gst_value_array_get_value (entry, 2));
      rect.right  = g_value_get_float (gst_value_array_get_value (entry, 3));

      label = gst_structure_get_string (structure, "label");

      GST_DEBUG ("ROI: label=%s top,left:(%.4f,%.4f) bottom,right:(%.4f,%.4f)",
          (label ? label : "NO_LABEL"), rect.top, rect.left,
          rect.bottom, rect.right);

      g_strlcpy (rect.label, label, sizeof (rect.label));
      rects.push_back (rect);
    }

    ++idx;
    g_value_unset (&value_list);
  }

  engine->videoctrlengine->PushMLData (rects);
  gst_smartcodec_engine_handle_rois (engine, rects, timestamp);
}

void
gst_smartcodec_engine_flush (SmartCodecEngine * engine)
{
  engine->videoctrlengine->Flush ();
}
