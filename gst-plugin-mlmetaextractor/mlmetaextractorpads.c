/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mlmetaextractorpads.h"

GST_DEBUG_CATEGORY_EXTERN (gst_mlmeta_extractor_debug);
#define GST_CAT_DEFAULT gst_mlmeta_extractor_debug

G_DEFINE_TYPE(GstMlMetaExtractorSrcPad, gst_mlmeta_extractor_srcpad, GST_TYPE_PAD);

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstMlMetaExtractorSrcPad *srcpad = GST_MLMETA_EXTRACTOR_SRCPAD (checkdata);
  GST_MLMETA_EXTRACTOR_PAD_SIGNAL_IDLE (srcpad, FALSE);

  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
queue_empty_cb (GstDataQueue * queue, gpointer checkdata)
{
  GstMlMetaExtractorSrcPad *srcpad = GST_MLMETA_EXTRACTOR_SRCPAD_CAST (checkdata);
  GST_MLMETA_EXTRACTOR_PAD_SIGNAL_IDLE (srcpad, TRUE);
}

static void
gst_mlmeta_extractor_srcpad_finalize (GObject * object)
{
  GstMlMetaExtractorSrcPad *pad = GST_MLMETA_EXTRACTOR_SRCPAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);

  gst_object_unref (GST_OBJECT_CAST(pad->buffers));

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_mlmeta_extractor_srcpad_parent_class)->finalize(object);
}

void
gst_mlmeta_extractor_srcpad_class_init (GstMlMetaExtractorSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_srcpad_finalize);
}

void
gst_mlmeta_extractor_srcpad_init (GstMlMetaExtractorSrcPad * pad)
{
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  pad->buffers = gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
  pad->is_idle = TRUE;
}
