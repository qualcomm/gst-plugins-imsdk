/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GST_QTI_BATCH_PADS_H__
#define __GST_QTI_BATCH_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_BATCH_SINK_PAD (gst_batch_sink_pad_get_type())
#define GST_BATCH_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BATCH_SINK_PAD,\
      GstBatchSinkPad))
#define GST_BATCH_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BATCH_SINK_PAD,\
      GstBatchSinkPadClass))
#define GST_IS_BATCH_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BATCH_SINK_PAD))
#define GST_IS_BATCH_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BATCH_SINK_PAD))
#define GST_BATCH_SINK_PAD_CAST(obj) ((GstBatchSinkPad *)(obj))

#define GST_TYPE_BATCH_SRC_PAD (gst_batch_src_pad_get_type())
#define GST_BATCH_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BATCH_SRC_PAD,\
      GstBatchSrcPad))
#define GST_BATCH_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BATCH_SRC_PAD,\
      GstBatchSrcPadClass))
#define GST_IS_BATCH_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BATCH_SRC_PAD))
#define GST_IS_BATCH_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BATCH_SRC_PAD))
#define GST_BATCH_SRC_PAD_CAST(obj) ((GstBatchSrcPad *)(obj))

#define GST_BATCH_SRC_GET_LOCK(obj) (&GST_BATCH_SRC_PAD(obj)->lock)
#define GST_BATCH_SRC_LOCK(obj)     g_mutex_lock(GST_BATCH_SRC_GET_LOCK(obj))
#define GST_BATCH_SRC_UNLOCK(obj)   g_mutex_unlock(GST_BATCH_SRC_GET_LOCK(obj))

#define GST_BATCH_PAD_SIGNAL_IDLE(pad, idle) \
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

#define GST_BATCH_PAD_WAIT_IDLE(pad) \
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

typedef struct _GstBatchSinkPad GstBatchSinkPad;
typedef struct _GstBatchSinkPadClass GstBatchSinkPadClass;
typedef struct _GstBatchSrcPad GstBatchSrcPad;
typedef struct _GstBatchSrcPadClass GstBatchSrcPadClass;

struct _GstBatchSinkPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// Queue for managing incoming buffers.
  GQueue       *buffers;
};

struct _GstBatchSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstBatchSrcPad {
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
  /// Flag indicating whether STREAM_START event need to be sent.
  gboolean     stmstart;

  /// Queue for batched buffers that are ready to be sent to next plugin.
  GstDataQueue *buffers;
};

struct _GstBatchSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_batch_sink_pad_get_type (void);
GType gst_batch_src_pad_get_type (void);


gboolean gst_batch_src_pad_event (GstPad * pad, GstObject * parent,
                                  GstEvent * event);
gboolean gst_batch_src_pad_query (GstPad * pad, GstObject * parent,
                                  GstQuery * query);
gboolean gst_batch_src_pad_activate_mode (GstPad * pad, GstObject * parent,
                                          GstPadMode mode, gboolean active);

G_END_DECLS

#endif // __GST_QTI_BATCH_PADS_H__
