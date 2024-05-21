/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_RTSP_BIN_SINKPAD_H__
#define __GST_RTSP_BIN_SINKPAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_RTSP_BIN_SINKPAD \
  (gst_rtsp_bin_sinkpad_get_type())
#define GST_RTSP_BIN_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSP_BIN_SINKPAD,\
                              GstRtspBinSinkPad))
#define GST_RTSP_BIN_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSP_BIN_SINKPAD,\
                           GstRtspBinSinkPadClass))
#define GST_IS_RTSP_BIN_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSP_BIN_SINKPAD))
#define GST_IS_RTSP_BIN_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSP_BIN_SINKPAD))
#define GST_RTSP_BIN_SINKPAD_CAST(obj) ((GstRtspBinSinkPad *)(obj))

#define GST_RTSP_BIN_SINKPAD_GET_LOCK(obj) \
  (&GST_RTSP_BIN_SINKPAD(obj)->lock)
#define GST_RTSP_BIN_SINKPAD_LOCK(obj) \
  g_mutex_lock(GST_RTSP_BIN_SINKPAD_GET_LOCK(obj))
#define GST_RTSP_BIN_SINKPAD_UNLOCK(obj) \
  g_mutex_unlock(GST_RTSP_BIN_SINKPAD_GET_LOCK(obj))

typedef struct _GstRtspBinSinkPad GstRtspBinSinkPad;
typedef struct _GstRtspBinSinkPadClass GstRtspBinSinkPadClass;

struct _GstRtspBinSinkPad {
  /// Inherited parent structure.
  GstPad        parent;
  /// Global mutex lock.
  GMutex        lock;
  /// Sink pad index.
  guint         index;
  /// Pad caps
  GstCaps       *caps;
  /// Appsrc instance linked to the pad
  GstElement    *appsrc;
  /// Timestamp offset from the begining of the stream start
  GstClockTime  pts_offset;
  GstClockTime  dts_offset;
};

struct _GstRtspBinSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_rtsp_bin_sinkpad_get_type (void);

G_END_DECLS

#endif // __GST_RTSP_BIN_SINKPAD_H__
