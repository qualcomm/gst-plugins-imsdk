/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "rtspbinsinkpad.h"

#define MAX_BUFFERS 30

GST_DEBUG_CATEGORY_STATIC (gst_rtsp_bin_sinkpad_debug);
#define GST_CAT_DEFAULT gst_rtsp_bin_sinkpad_debug

G_DEFINE_TYPE (GstRtspBinSinkPad, gst_rtsp_bin_sinkpad,
               GST_TYPE_PAD);

static void
gst_rtsp_bin_sinkpad_finalize (GObject * object)
{
  GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (object);

  g_mutex_clear (&sinkpad->lock);

  if (sinkpad->caps) {
    gst_caps_unref (sinkpad->caps);
    sinkpad->caps = NULL;
  }

  if (sinkpad->appsrc) {
    gst_object_unref (sinkpad->appsrc);
    sinkpad->appsrc = NULL;
  }

  gst_object_unref (GST_OBJECT_CAST (sinkpad->buffers));

  G_OBJECT_CLASS (gst_rtsp_bin_sinkpad_parent_class)->finalize(object);
}

static void
gst_rtsp_bin_sinkpad_class_init (GstRtspBinSinkPadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_rtsp_bin_sinkpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_rtsp_bin_sinkpad_debug,
      "qtirtspbin", 0, "QTI Rtsp Bin sink pad");
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  return (visible >= MAX_BUFFERS) ? TRUE : FALSE;
}

static void
gst_rtsp_bin_sinkpad_init (GstRtspBinSinkPad * sinkpad)
{
  g_mutex_init (&sinkpad->lock);

  sinkpad->index = 0;
  sinkpad->caps = NULL;
  sinkpad->appsrc = NULL;
  sinkpad->current_timestamp = 0;
  sinkpad->last_timestamp = 0;

  sinkpad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
}
