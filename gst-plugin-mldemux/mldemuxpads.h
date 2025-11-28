/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_ML_DEMUX_PADS_H__
#define __GST_ML_DEMUX_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/ml/ml-info.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_DEMUX_SINKPAD (gst_ml_demux_sinkpad_get_type())
#define GST_ML_DEMUX_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_DEMUX_SINKPAD,GstMLDemuxSinkPad))
#define GST_ML_DEMUX_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_DEMUX_SINKPAD,GstMLDemuxSinkPadClass))
#define GST_IS_ML_DEMUX_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_DEMUX_SINKPAD))
#define GST_IS_ML_DEMUX_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_DEMUX_SINKPAD))
#define GST_ML_DEMUX_SINKPAD_CAST(obj) ((GstMLDemuxSinkPad *)(obj))

#define GST_TYPE_ML_DEMUX_SRCPAD (gst_ml_demux_srcpad_get_type())
#define GST_ML_DEMUX_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_DEMUX_SRCPAD,GstMLDemuxSrcPad))
#define GST_ML_DEMUX_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_DEMUX_SRCPAD,GstMLDemuxSrcPadClass))
#define GST_IS_ML_DEMUX_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_DEMUX_SRCPAD))
#define GST_IS_ML_DEMUX_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_DEMUX_SRCPAD))
#define GST_ML_DEMUX_SRCPAD_CAST(obj) ((GstMLDemuxSrcPad *)(obj))

#define GST_ML_DEMUX_PAD_SIGNAL_IDLE(pad, idle) \
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

#define GST_ML_DEMUX_PAD_WAIT_IDLE(pad) \
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

typedef struct _GstMLDemuxSinkPad GstMLDemuxSinkPad;
typedef struct _GstMLDemuxSinkPadClass GstMLDemuxSinkPadClass;
typedef struct _GstMLDemuxSrcPad GstMLDemuxSrcPad;
typedef struct _GstMLDemuxSrcPadClass GstMLDemuxSrcPadClass;

struct _GstMLDemuxSinkPad {
  /// Inherited parent structure.
  GstPad     parent;

  /// ML tensors info from caps.
  GstMLInfo  *mlinfo;

  /// Segment.
  GstSegment segment;
};

struct _GstMLDemuxSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstMLDemuxSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;
  /// ID/Index with which this pad was created.
  guint        id;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond        drained;
  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// ML tensors info from caps.
  GstMLInfo    *mlinfo;

  /// Segment.
  GstSegment   segment;

  /// Worker queue.
  GstDataQueue *buffers;
};

struct _GstMLDemuxSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_ml_demux_sinkpad_get_type (void);

GType gst_ml_demux_srcpad_get_type (void);

G_END_DECLS

#endif // __GST_ML_DEMUX_PADS_H__
