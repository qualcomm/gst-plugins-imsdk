/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mldemuxpads.h"

GST_DEBUG_CATEGORY_EXTERN (gst_ml_demux_debug);
#define GST_CAT_DEFAULT gst_ml_demux_debug

G_DEFINE_TYPE(GstMLDemuxSinkPad, gst_ml_demux_sinkpad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstMLDemuxSrcPad, gst_ml_demux_srcpad, GST_TYPE_PAD);

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD (checkdata);
  GST_ML_DEMUX_PAD_SIGNAL_IDLE (srcpad, FALSE);

  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
queue_empty_cb (GstDataQueue * queue, gpointer checkdata)
{
  GstMLDemuxSrcPad *srcpad = GST_ML_DEMUX_SRCPAD_CAST (checkdata);
  GST_ML_DEMUX_PAD_SIGNAL_IDLE (srcpad, TRUE);
}

static void
gst_ml_demux_sinkpad_finalize (GObject * object)
{
  GstMLDemuxSinkPad *pad = GST_ML_DEMUX_SINKPAD (object);

  if (pad->mlinfo != NULL)
    gst_ml_info_free (pad->mlinfo);

  G_OBJECT_CLASS (gst_ml_demux_sinkpad_parent_class)->finalize(object);
}

void
gst_ml_demux_sinkpad_class_init (GstMLDemuxSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_demux_sinkpad_finalize);
}

void
gst_ml_demux_sinkpad_init (GstMLDemuxSinkPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->mlinfo = NULL;
}

static void
gst_ml_demux_srcpad_finalize (GObject * object)
{
  GstMLDemuxSrcPad *pad = GST_ML_DEMUX_SRCPAD (object);

  if (pad->mlinfo != NULL)
    gst_ml_info_free (pad->mlinfo);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);

  gst_object_unref (GST_OBJECT_CAST(pad->buffers));

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_ml_demux_srcpad_parent_class)->finalize(object);
}

void
gst_ml_demux_srcpad_class_init (GstMLDemuxSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_demux_srcpad_finalize);
}

void
gst_ml_demux_srcpad_init (GstMLDemuxSrcPad * pad)
{
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->id = 0;
  pad->mlinfo = NULL;
  pad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
}

