/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_HEIFMUX_H__
#define __GST_QTI_HEIFMUX_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstdataqueue.h>
#include <gst/utils/common-utils.h>

#include "heifmuxpads.h"
#include "heif-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_HEIFMUX \
  (gst_heifmux_get_type())
#define GST_HEIFMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HEIFMUX,GstHeifMux))
#define GST_HEIFMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HEIFMUX,GstHeifMuxClass))
#define GST_IS_HEIFMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HEIFMUX))
#define GST_IS_HEIFMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HEIFMUX))
#define GST_HEIFMUX_CAST(obj)       ((GstHeifMux *)(obj))

#define GST_HEIFMUX_GET_LOCK(obj)   (&GST_HEIFMUX(obj)->lock)
#define GST_HEIFMUX_LOCK(obj)       g_mutex_lock(GST_HEIFMUX_GET_LOCK(obj))
#define GST_HEIFMUX_UNLOCK(obj)     g_mutex_unlock(GST_HEIFMUX_GET_LOCK(obj))

typedef struct _GstHeifMux GstHeifMux;
typedef struct _GstHeifMuxClass GstHeifMuxClass;

struct _GstHeifMux {
  /// Inherited parent structure.
  GstElement        parent;

  /// Global mutex lock.
  GMutex            lock;

  /// Next available index for the thumbnail sink pads.
  guint             nextidx;
  /// Convenient local reference to thumbnail sink pads.
  GList             *thumbpads;
  /// Convenient local reference to main sink pad.
  GstHeifMuxSinkPad *sinkpad;
  /// Convenient local reference to source pad.
  GstHeifMuxSrcPad  *srcpad;

  /// Output buffer pool.
  GstBufferPool     *outpool;

  /// Worker task.
  GstTask           *worktask;
  /// Worker task mutex.
  GRecMutex         worklock;
  /// Indicates whether the worker task is active or not.
  gboolean          active;
  /// Condition for push/pop buffers from the queues.
  GCond             wakeup;

  /// Heif engine.
  GstHeifEngine     *engine;

  /// Properties.
  guint             queue_size;
};

struct _GstHeifMuxClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

G_GNUC_INTERNAL GType gst_heifmux_get_type (void);

G_END_DECLS

#endif // __GST_QTI_HEIFMUX_H__
