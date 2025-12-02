/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_DEMUX_H__
#define __GST_QTI_ML_DEMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_DEMUX (gst_ml_demux_get_type())
#define GST_ML_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_DEMUX,GstMLDemux))
#define GST_ML_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_DEMUX,GstMLDemuxClass))
#define GST_IS_ML_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_DEMUX))
#define GST_IS_ML_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_DEMUX))

#define GST_ML_DEMUX_GET_LOCK(obj)   (&GST_ML_DEMUX(obj)->lock)
#define GST_ML_DEMUX_LOCK(obj)       g_mutex_lock(GST_ML_DEMUX_GET_LOCK(obj))
#define GST_ML_DEMUX_UNLOCK(obj)     g_mutex_unlock(GST_ML_DEMUX_GET_LOCK(obj))

typedef struct _GstMLDemux GstMLDemux;
typedef struct _GstMLDemuxClass GstMLDemuxClass;

struct _GstMLDemux
{
  /// Inherited parent structure.
  GstElement parent;

  /// Global mutex lock.
  GMutex     lock;

  /// Next available index for the source pads.
  guint      nextidx;

  /// Convenient local reference to sink pad.
  GstPad     *sinkpad;
  /// Convenient local reference to source pads.
  GList      *srcpads;
};

struct _GstMLDemuxClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_ml_demux_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_DEMUX_H__
