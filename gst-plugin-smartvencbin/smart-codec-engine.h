/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_SMART_CODEC_ENGINE_H_
#define _GST_SMART_CODEC_ENGINE_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <time.h>

G_BEGIN_DECLS

typedef struct _SmartCodecEngine SmartCodecEngine;
typedef struct _BwStats BwStats;
typedef struct _RectDeltaQP RectDeltaQP;
typedef struct _RectDeltaQPs RectDeltaQPs;

typedef void (* GstBitrateReceivedCallback) (guint bitrate, gpointer user_data);
typedef void (* GstFRDeviderReceivedCallback) (guint frdivider,
    gpointer user_data);
typedef void (* GstReleaseBufferCallback) (gpointer user_data);

#define STATS_BANDWIDTH 0x1
#define MAX_RECT_ROI_NUM 10

struct _BwStats {
  gint total_size;
  gint total_frames;
  guint64 initial_time_ms;
  guint64 last_stats_time_ms;
  guint64 next_stats_time_ms;
};

struct _RectDeltaQP {
  gint left;
  gint top;
  gint width;
  gint height;
  gint delta_qp;
};

struct _RectDeltaQPs {
  guint num_rectangles;
  RectDeltaQP mRectangle[MAX_RECT_ROI_NUM];
};

GST_API SmartCodecEngine *
gst_smartcodec_engine_new ();

GST_API void
gst_smartcodec_engine_free (SmartCodecEngine * engine);

GST_API void
gst_smartcodec_engine_set_callbacks (SmartCodecEngine * engine,
    GstBitrateReceivedCallback bitrate_callback,
    GstFRDeviderReceivedCallback fr_callback,
    GstReleaseBufferCallback release_buffer_callback,
    gpointer user_data);

GST_API void
gst_smartcodec_engine_init (SmartCodecEngine * engine,
    GstCaps * caps, guint stats_mask);

GST_API void
gst_smartcodec_engine_update_fr_divider (SmartCodecEngine * engine,
    guint fr_divider);

GST_API gboolean
gst_smartcodec_engine_process_input_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer);

GST_API void
gst_smartcodec_engine_process_output_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer);

GST_API void
gst_smartcodec_engine_get_fps (SmartCodecEngine * engine, guint * n, guint * d);

GST_API void
gst_smartcodec_engine_get_rois_with_qp (SmartCodecEngine * engine,
    RectDeltaQPs * rect_delta_qps);

GST_API void
gst_smartcodec_engine_check_elapsed_time_and_print_bwstats (
    SmartCodecEngine * engine);

GST_API void
gst_smartcodec_engine_push_ctrl_buff (SmartCodecEngine * engine, guint8 * buff);

GST_API void
gst_smartcodec_engine_push_ml_buff (SmartCodecEngine * engine, gchar * data);

GST_API void
gst_smartcodec_engine_set_video_info (SmartCodecEngine * engine,
    guint width, guint height, guint stride);

GST_API void
gst_smartcodec_engine_set_target_bitrate (SmartCodecEngine * engine,
    guint target_bitrate);

GST_API void
gst_smartcodec_engine_set_bitrate_thresholds (SmartCodecEngine * engine,
    GArray * bitrate_thresholds);

GST_API void
gst_smartcodec_engine_set_fr_thresholds (SmartCodecEngine * engine,
    GArray * framerate_thresholds);

GST_API void
gst_smartcodec_engine_set_roi_classes_qps (SmartCodecEngine * engine,
    GArray * roi_qualitys);

GST_API void
gst_smartcodec_engine_flush (SmartCodecEngine * engine);

G_END_DECLS

#endif // _GST_SMART_CODEC_ENGINE_H_
