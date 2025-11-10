/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_HEIFMUX_PADS_H__
#define __GST_HEIFMUX_PADS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_HEIFMUX_SINK_PAD (gst_heifmux_sink_pad_get_type())
#define GST_HEIFMUX_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HEIFMUX_SINK_PAD,\
      GstHeifMuxSinkPad))
#define GST_HEIFMUX_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HEIFMUX_SINK_PAD,\
      GstHeifMuxSinkPadClass))
#define GST_IS_HEIFMUX_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HEIFMUX_SINK_PAD))
#define GST_IS_HEIFMUX_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HEIFMUX_SINK_PAD))
#define GST_HEIFMUX_SINK_PAD_CAST(obj) ((GstHeifMuxSinkPad *)(obj))

#define GST_TYPE_HEIFMUX_SRC_PAD (gst_heifmux_src_pad_get_type())
#define GST_HEIFMUX_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HEIFMUX_SRC_PAD,\
      GstHeifMuxSrcPad))
#define GST_HEIFMUX_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HEIFMUX_SRC_PAD,\
      GstHeifMuxSrcPadClass))
#define GST_IS_HEIFMUX_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HEIFMUX_SRC_PAD))
#define GST_IS_HEIFMUX_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HEIFMUX_SRC_PAD))
#define GST_HEIFMUX_SRC_PAD_CAST(obj) ((GstHeifMuxSrcPad *)(obj))

#define GST_HEIFMUX_SRC_GET_LOCK(obj) (&GST_HEIFMUX_SRC_PAD(obj)->lock)
#define GST_HEIFMUX_SRC_LOCK(obj) \
    g_mutex_lock(GST_HEIFMUX_SRC_GET_LOCK(obj))
#define GST_HEIFMUX_SRC_UNLOCK(obj) \
    g_mutex_unlock(GST_HEIFMUX_SRC_GET_LOCK(obj))

#define GST_HEIFMUX_PAD_SIGNAL_IDLE(pad, idle) \
{\
  g_mutex_lock (&(pad->lock));                                     \
                                                                   \
  if (pad->is_idle != idle) {                                      \
    pad->is_idle = idle;                                           \
    GST_TRACE_OBJECT (pad, "State %s", idle ? "Idle" : "Running"); \
    g_cond_signal (&(pad->drained));                               \
  }                                                                \
                                                                   \
  g_mutex_unlock (&(pad->lock));                                   \
}

#define GST_HEIFMUX_PAD_WAIT_IDLE(pad) \
{\
  g_mutex_lock (&(pad->lock));                                         \
  GST_TRACE_OBJECT (pad, "Waiting until idle");                        \
                                                                       \
  while (!pad->is_idle) {                                              \
    gint64 endtime = g_get_monotonic_time () + 1 * G_TIME_SPAN_SECOND; \
                                                                       \
    if (!g_cond_wait_until (&(pad->drained), &(pad->lock), endtime))   \
      GST_WARNING_OBJECT (pad, "Timeout while waiting for idle!");     \
  }                                                                    \
                                                                       \
  GST_TRACE_OBJECT (pad, "Received idle");                             \
  g_mutex_unlock (&(pad->lock));                                       \
}

typedef struct _GstHeifMuxSinkPad GstHeifMuxSinkPad;
typedef struct _GstHeifMuxSinkPadClass GstHeifMuxSinkPadClass;

typedef struct _GstHeifMuxSrcPad GstHeifMuxSrcPad;
typedef struct _GstHeifMuxSrcPadClass GstHeifMuxSrcPadClass;

struct _GstHeifMuxSinkPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;

  /// Video info extracted from negotiated caps.
  GstVideoInfo *vinfo;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond        drained;
  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// Queue for managing incoming video/audio buffers.
  GstDataQueue *buffers;

  /// The count of buffers the queue can hold.
  guint        buffers_limit;
};

struct _GstHeifMuxSinkPadClass {
  /// Inherited parent structure.
  GstPadClass  parent;
};

struct _GstHeifMuxSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond        drained;
  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// Segment.
  GstSegment   segment;

  /// Queue for output buffers.
  GstDataQueue *buffers;

  /// The count of buffers the queue can hold.
  guint        buffers_limit;
};

struct _GstHeifMuxSrcPadClass {
  /// Inherited parent structure.
  GstPadClass  parent;
};

GType gst_heifmux_sink_pad_get_type (void);

GType gst_heifmux_src_pad_get_type (void);

gboolean gst_heifmux_src_pad_event (GstPad * pad, GstObject * parent,
                                    GstEvent * event);
gboolean gst_heifmux_src_pad_query (GstPad * pad, GstObject * parent,
                                    GstQuery * query);
gboolean gst_heifmux_src_pad_activate_mode (GstPad * pad, GstObject * parent,
                                            GstPadMode mode, gboolean active);

G_END_DECLS

#endif // __GST_HEIFMUX_PADS_H__
