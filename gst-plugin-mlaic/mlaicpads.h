/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
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
