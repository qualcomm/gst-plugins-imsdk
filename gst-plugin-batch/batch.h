/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_BATCH_H__
#define __GST_QTI_BATCH_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_BATCH (gst_batch_get_type())
#define GST_BATCH(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BATCH,GstBatch))
#define GST_BATCH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BATCH,GstBatchClass))
#define GST_IS_BATCH(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BATCH))
#define GST_IS_BATCH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BATCH))
#define GST_BATCH_CAST(obj)       ((GstBatch *)(obj))

#define GST_BATCH_GET_LOCK(obj)   (&GST_BATCH(obj)->lock)
#define GST_BATCH_LOCK(obj)       g_mutex_lock(GST_BATCH_GET_LOCK(obj))
#define GST_BATCH_UNLOCK(obj)     g_mutex_unlock(GST_BATCH_GET_LOCK(obj))

typedef struct _GstBatch GstBatch;
typedef struct _GstBatchClass GstBatchClass;

struct _GstBatch
{
  /// Inherited parent structure.
  GstElement     parent;

  /// Global mutex lock.
  GMutex         lock;

  /// Next available index for the sink pads.
  guint          nextidx;

  /// Convenient local reference to media sink pads.
  GList          *sinkpads;
  /// Convenient local reference to source pad.
  GstPad         *srcpad;

  /// Output buffers duration.
  GstClockTime   duration;

  /// Worker task mutex.
  GRecMutex      worklock;
  /// Worker task.
  GstTask        *worktask;

  // Indicates whether the worker task is active or not.
  gboolean       active;
  /// End monotonic time until which to wait for buffers in the worker task.
  gint64         endtime;
  /// Condition for negotiating output caps and push/pop buffers from the queues.
  GCond          wakeup;
  /// Depth indicating how many buffers should be accumulated from each stream.
  guint          depth;

  /// Properties
  /// Indicating how many new buffers will be used for each output frame.
  guint          moving_window_size;
};

struct _GstBatchClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_batch_get_type (void);

G_END_DECLS

#endif // __GST_QTI_BATCH_H__
