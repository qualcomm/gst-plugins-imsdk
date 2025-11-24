/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_OVERLAY_H__
#define __GST_QTI_OVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/video/video-converter-engine.h>

#include "overlayutils.h"

G_BEGIN_DECLS

#define GST_TYPE_OVERLAY (gst_overlay_get_type())
#define GST_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OVERLAY, GstVOverlay))
#define GST_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OVERLAY, GstVOverlayClass))
#define GST_IS_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OVERLAY))
#define GST_IS_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OVERLAY))
#define GST_OVERLAY_CAST(obj) ((GstOverlay *)(obj))

#define GST_OVERLAY_GET_LOCK(obj) (&GST_OVERLAY(obj)->lock)
#define GST_OVERLAY_LOCK(obj)     g_mutex_lock(GST_OVERLAY_GET_LOCK(obj))
#define GST_OVERLAY_UNLOCK(obj)   g_mutex_unlock(GST_OVERLAY_GET_LOCK(obj))

typedef struct _GstVOverlay GstVOverlay;
typedef struct _GstVOverlayClass GstVOverlayClass;

struct _GstVOverlay {
  GstBaseTransform     parent;

  GMutex               lock;

  /// Maximum latency.
  GstClockTime         latency;

  /// Video info extracted from negotiated sink/src caps.
  GstVideoInfo         *vinfo;

  /// Internal intermediary buffer pools, used for drawing image overlays.
  GstBufferPool        *ovlpools[GST_OVERLAY_TYPE_MAX];
  /// Video info for the intermediary buffers produced by the pools.
  GstVideoInfo         *ovlinfos[GST_OVERLAY_TYPE_MAX];

  /// Video converter engine.
  GstVideoConvEngine   *converter;

  /// Properties.
  GstVideoConvBackend  backend;
  GArray               *bboxes;
  GArray               *timestamps;
  GArray               *strings;
  GArray               *simages;
  GArray               *masks;
};

struct _GstVOverlayClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_overlay_get_type (void);

G_END_DECLS

#endif // __GST_QTI_OVERLAY_H__
