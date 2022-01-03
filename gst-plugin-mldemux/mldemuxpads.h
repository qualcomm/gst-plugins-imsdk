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

#define GST_ML_DEMUX_SINKPAD_GET_LOCK(obj) (&GST_ML_DEMUX_SINKPAD(obj)->lock)
#define GST_ML_DEMUX_SINKPAD_LOCK(obj) \
  g_mutex_lock(GST_ML_DEMUX_SINKPAD_GET_LOCK(obj))
#define GST_ML_DEMUX_SINKPAD_UNLOCK(obj) \
  g_mutex_unlock(GST_ML_DEMUX_SINKPAD_GET_LOCK(obj))

typedef struct _GstMLDemuxSinkPad GstMLDemuxSinkPad;
typedef struct _GstMLDemuxSinkPadClass GstMLDemuxSinkPadClass;
typedef struct _GstMLDemuxSrcPad GstMLDemuxSrcPad;
typedef struct _GstMLDemuxSrcPadClass GstMLDemuxSrcPadClass;

struct _GstMLDemuxSinkPad {
  /// Inherited parent structure.
  GstPad     parent;

  /// Global mutex lock.
  GMutex     lock;

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

  // Output ML tensors info from caps..
  GstMLInfo    *mlinfo;

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
