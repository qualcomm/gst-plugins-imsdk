/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_MLMETA_EXTRACTOR_PADS_H__
#define __GST_MLMETA_EXTRACTOR_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_MLMETA_EXTRACTOR_SRCPAD (gst_mlmeta_extractor_srcpad_get_type())
#define GST_MLMETA_EXTRACTOR_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MLMETA_EXTRACTOR_SRCPAD,GstMlMetaExtractorSrcPad))
#define GST_MLMETA_EXTRACTOR_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MLMETA_EXTRACTOR_SRCPAD,GstMlMetaExtractorSrcPadClass))
#define GST_IS_MLMETA_EXTRACTOR_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MLMETA_EXTRACTOR_SRCPAD))
#define GST_IS_MLMETA_EXTRACTOR_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MLMETA_EXTRACTOR_SRCPAD))
#define GST_MLMETA_EXTRACTOR_SRCPAD_CAST(obj) ((GstMlMetaExtractorSrcPad *)(obj))

#define GST_MLMETA_EXTRACTOR_PAD_SIGNAL_IDLE(pad, idle) \
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

#define GST_MLMETA_EXTRACTOR_PAD_WAIT_IDLE(pad) \
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

typedef struct _GstMlMetaExtractorSrcPad GstMlMetaExtractorSrcPad;
typedef struct _GstMlMetaExtractorSrcPadClass GstMlMetaExtractorSrcPadClass;

struct _GstMlMetaExtractorSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond        drained;
  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// Worker queue.
  GstDataQueue *buffers;
};

struct _GstMlMetaExtractorSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_mlmeta_extractor_sinkpad_get_type (void);

GType gst_mlmeta_extractor_srcpad_get_type (void);

G_END_DECLS

#endif // __GST_MLMETA_EXTRACTOR_PADS_H__
