/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_METAMUX_H__
#define __GST_QTI_METAMUX_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "metamuxpads.h"

G_BEGIN_DECLS

#define GST_TYPE_METAMUX (gst_metamux_get_type())
#define GST_METAMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_METAMUX,GstMetaMux))
#define GST_METAMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_METAMUX,GstMetaMuxClass))
#define GST_IS_METAMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_METAMUX))
#define GST_IS_METAMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_METAMUX))
#define GST_METAMUX_CAST(obj)       ((GstMetaMux *)(obj))

#define GST_METAMUX_GET_LOCK(obj)   (&GST_METAMUX(obj)->lock)
#define GST_METAMUX_LOCK(obj)       g_mutex_lock(GST_METAMUX_GET_LOCK(obj))
#define GST_METAMUX_UNLOCK(obj)     g_mutex_unlock(GST_METAMUX_GET_LOCK(obj))

typedef struct _GstMetaMux GstMetaMux;
typedef struct _GstMetaMuxClass GstMetaMuxClass;

typedef enum {
  GST_METAMUX_MODE_ASYNC,
  GST_METAMUX_MODE_SYNC,
} GstMetaMuxMode;

struct _GstMetaMux
{
  /// Inherited parent structure.
  GstElement        parent;

  /// Global mutex lock.
  GMutex            lock;

  /// Next available index for the sink pads.
  guint             nextidx;

  /// Convenient local reference to data sink pads.
  GList             *metapads;
  /// Convenient local reference to media sink pad.
  GstMetaMuxSinkPad *sinkpad;
  /// Convenient local reference to source pad.
  GstMetaMuxSrcPad  *srcpad;

  /// Info regarding the negotiated audio/video caps.
  GstVideoInfo      *vinfo;
  GstAudioInfo      *ainfo;

  /// Worker task.
  GstTask           *worktask;
  /// Worker task mutex.
  GRecMutex         worklock;
  // Indicates whether the worker task is active or not.
  gboolean          active;
  /// Condition for push/pop buffers from the queues.
  GCond             wakeup;
  /// The timestamp of the first buffer, used to calcculate the elapsed time.
  GstClockTime      basetime;
  /// The sync time initialized at first buffer and used to wait for synced data.
  gint64            synctime;

  /// Properties.
  GstMetaMuxMode    mode;
  GstClockTime      latency;
  guint             queue_size;
};

struct _GstMetaMuxClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_metamux_get_type (void);

G_END_DECLS

#endif // __GST_QTI_METAMUX_H__

