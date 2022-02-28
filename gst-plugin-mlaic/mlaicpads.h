/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_ML_AIC_PADS_H__
#define __GST_ML_AIC_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_AIC_SINKPAD (gst_ml_aic_sinkpad_get_type())
#define GST_ML_AIC_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_AIC_SINKPAD,GstMLAicSinkPad))
#define GST_ML_AIC_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_AIC_SINKPAD,GstMLAicSinkPadClass))
#define GST_IS_ML_AIC_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_AIC_SINKPAD))
#define GST_IS_ML_AIC_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_AIC_SINKPAD))
#define GST_ML_AIC_SINKPAD_CAST(obj) ((GstMLAicSinkPad *)(obj))

#define GST_TYPE_ML_AIC_SRCPAD (gst_ml_aic_srcpad_get_type())
#define GST_ML_AIC_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_AIC_SRCPAD,GstMLAicSrcPad))
#define GST_ML_AIC_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_AIC_SRCPAD,GstMLAicSrcPadClass))
#define GST_IS_ML_AIC_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_AIC_SRCPAD))
#define GST_IS_ML_AIC_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_AIC_SRCPAD))
#define GST_ML_AIC_SRCPAD_CAST(obj) ((GstMLAicSrcPad *)(obj))

#define GST_ML_AIC_SINKPAD_GET_LOCK(obj) (&GST_ML_AIC_SINKPAD(obj)->lock)
#define GST_ML_AIC_SINKPAD_LOCK(obj) \
  g_mutex_lock(GST_ML_AIC_SINKPAD_GET_LOCK(obj))
#define GST_ML_AIC_SINKPAD_UNLOCK(obj) \
  g_mutex_unlock(GST_ML_AIC_SINKPAD_GET_LOCK(obj))

typedef struct _GstMLAicSinkPad GstMLAicSinkPad;
typedef struct _GstMLAicSinkPadClass GstMLAicSinkPadClass;
typedef struct _GstMLAicSrcPad GstMLAicSrcPad;
typedef struct _GstMLAicSrcPadClass GstMLAicSrcPadClass;

struct _GstMLAicSinkPad {
  /// Inherited parent structure.
  GstPad        parent;

  /// Global mutex lock.
  GMutex        lock;

  /// Segment.
  GstSegment    segment;

  /// Buffer pool.
  GstBufferPool *pool;
  /// Map of input and output buffers that are paired together.
  GHashTable    *bufpairs;
};

struct _GstMLAicSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstMLAicSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Worker queue.
  GstDataQueue *requests;
};

struct _GstMLAicSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_ml_aic_sinkpad_get_type (void);

GType gst_ml_aic_srcpad_get_type (void);

G_END_DECLS

#endif // __GST_ML_AIC_PADS_H__
