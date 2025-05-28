/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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
};

struct _GstBatchClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_batch_get_type (void);

G_END_DECLS

#endif // __GST_QTI_BATCH_H__
