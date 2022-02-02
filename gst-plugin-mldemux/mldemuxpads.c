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

#include "mldemuxpads.h"

G_DEFINE_TYPE(GstMLDemuxSinkPad, gst_ml_demux_sinkpad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstMLDemuxSrcPad, gst_ml_demux_srcpad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (gst_ml_demux_debug);
#define GST_CAT_DEFAULT gst_ml_demux_debug

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_ml_demux_sinkpad_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_ml_demux_sinkpad_parent_class)->finalize(object);
}

void
gst_ml_demux_sinkpad_class_init (GstMLDemuxSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_demux_sinkpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_ml_demux_debug, "qtimldemux", 0,
      "QTI ML Demux sink pad");
}

void
gst_ml_demux_sinkpad_init (GstMLDemuxSinkPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
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

  G_OBJECT_CLASS (gst_ml_demux_srcpad_parent_class)->finalize(object);
}

void
gst_ml_demux_srcpad_class_init (GstMLDemuxSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_demux_srcpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_ml_demux_debug, "qtimldemux", 0,
      "QTI ML Demux src pad");
}

void
gst_ml_demux_srcpad_init (GstMLDemuxSrcPad * pad)
{
  pad->mlinfo = NULL;
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
}

