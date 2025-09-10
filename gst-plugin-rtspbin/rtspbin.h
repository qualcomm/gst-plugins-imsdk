/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_RTSP_BIN_H__
#define __GST_QTI_RTSP_BIN_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/rtsp-server/rtsp-server.h>

G_BEGIN_DECLS

#define GST_TYPE_RTSP_BIN (gst_rtsp_bin_get_type())
#define GST_RTSP_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSP_BIN,GstRtspBin))
#define GST_RTSP_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSP_BIN,GstRtspBinClass))
#define GST_IS_RTSP_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSP_BIN))
#define GST_IS_RTSP_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSP_BIN))
#define GST_RTSP_BIN_CAST(obj)  ((GstRtspBin *)(obj))

#define GST_RTSP_BIN_GET_LOCK(obj) (&GST_RTSP_BIN(obj)->lock)
#define GST_RTSP_BIN_LOCK(obj) g_mutex_lock(GST_RTSP_BIN_GET_LOCK(obj))
#define GST_RTSP_BIN_UNLOCK(obj) g_mutex_unlock(GST_RTSP_BIN_GET_LOCK(obj))

typedef struct _GstRtspBin GstRtspBin;
typedef struct _GstRtspBinClass GstRtspBinClass;

typedef enum {
  GST_RTSPBIN_MODE_ASYNC,
  GST_RTSPBIN_MODE_SYNC,
} GstRtspBinMode;

struct _GstRtspBin
{
  // Inherited parent structure.
  GstBin              parent;
  // Global mutex lock.
  GMutex              lock;
  /// Next available index for the sink pads.
  guint               nextidx;
  /// Convenient local reference to data sink pads.
  GList               *sinkpads;
  /// RTSP server instance
  GstRTSPServer       *server;
  /// RTSP factory instance
  GstRTSPMediaFactory *factory;
  /// Prepared flag
  gboolean            media_prepared;

  /// Properties.
  GstRtspBinMode      mode;
  gchar               *address;
  gchar               *port;
  gchar               *mpoint;
};

struct _GstRtspBinClass
{
  // Inherited parent structure.
  GstBinClass parent_class;
};

GType gst_rtsp_bin_get_type(void);

G_END_DECLS

#endif /* __GST_QTI_RTSP_BIN_H__ */
