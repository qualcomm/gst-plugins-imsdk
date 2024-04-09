/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_SMART_CODEC_ENGINE_H_
#define _GST_SMART_CODEC_ENGINE_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <time.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

typedef struct _SmartCodecEngine SmartCodecEngine;
typedef struct _RectDeltaQP RectDeltaQP;
typedef struct _RectDeltaQPs RectDeltaQPs;

typedef void (* GstBitrateReceivedCallback) (guint bitrate, gpointer user_data);
typedef void (* GstGOPLengthReceivedCallback) (guint goplength,
    guint64 insert_syncframe_pts, gpointer user_data);
typedef void (* GstReleaseBufferCallback) (gpointer user_data);

#define MAX_RECT_ROI_NUM 10

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
  guint64 timestamp;
};

GST_API SmartCodecEngine *
gst_smartcodec_engine_new ();

GST_API void
gst_smartcodec_engine_free (SmartCodecEngine * engine);

GST_API void
gst_smartcodec_engine_init (SmartCodecEngine * engine,
    GstCaps * caps);

GST_API void
gst_smartcodec_engine_process_output_caps (SmartCodecEngine * engine,
    GstCaps *caps);

GST_API void
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
    gpointer user_data);

GST_API void
gst_smartcodec_engine_update_fr_divider (SmartCodecEngine * engine,
    guint fr_divider);

GST_API gboolean
gst_smartcodec_engine_process_input_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer);

GST_API void
gst_smartcodec_engine_process_output_videobuffer (SmartCodecEngine * engine,
    GstBuffer * buffer, gboolean bSyncFrame);

GST_API void
gst_smartcodec_engine_get_fps (SmartCodecEngine * engine, guint * n, guint * d);

GST_API gboolean
gst_smartcodec_engine_get_rois_from_queue (SmartCodecEngine * engine,
    RectDeltaQPs * rect_delta_qps);

GST_API void
gst_smartcodec_engine_remove_rois_from_queue (SmartCodecEngine * engine);

GST_API void
gst_smartcodec_engine_push_ctrl_buff (SmartCodecEngine * engine, guint8 * buff,
    guint32 stride, guint64 timestamp);

GST_API void
gst_smartcodec_engine_push_ml_buff (SmartCodecEngine * engine, gchar * data,
    guint64 timestamp);

GST_API guint
gst_smartcodec_engine_get_buff_cnt_delay (SmartCodecEngine * engine);

GST_API void
gst_smartcodec_engine_flush (SmartCodecEngine * engine);

G_END_DECLS

#endif // _GST_SMART_CODEC_ENGINE_H_
