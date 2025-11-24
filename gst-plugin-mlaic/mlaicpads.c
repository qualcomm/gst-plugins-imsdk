/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mlaicpads.h"

G_DEFINE_TYPE(GstMLAicSinkPad, gst_ml_aic_sinkpad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstMLAicSrcPad, gst_ml_aic_srcpad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (gst_ml_aic_debug);
#define GST_CAT_DEFAULT gst_ml_aic_debug

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_ml_aic_sinkpad_finalize (GObject * object)
{
  GstMLAicSinkPad *pad = GST_ML_AIC_SINKPAD (object);

  if (pad->bufpairs != NULL)
    g_hash_table_destroy(pad->bufpairs);

  if (pad->pool != NULL)
    gst_object_unref (pad->pool);

  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_ml_aic_sinkpad_parent_class)->finalize(object);
}

void
gst_ml_aic_sinkpad_class_init (GstMLAicSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_aic_sinkpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_ml_aic_debug, "qtimlaic", 0,
      "QTI ML AIC sink pad");
}

void
gst_ml_aic_sinkpad_init (GstMLAicSinkPad * pad)
{
  g_mutex_init (&pad->lock);
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->pool = NULL;
  pad->bufpairs = g_hash_table_new (NULL, NULL);
}

static void
gst_ml_aic_srcpad_finalize (GObject * object)
{
  GstMLAicSrcPad *pad = GST_ML_AIC_SRCPAD (object);

  gst_data_queue_set_flushing (pad->requests, TRUE);
  gst_data_queue_flush (pad->requests);

  gst_object_unref (GST_OBJECT_CAST(pad->requests));

  G_OBJECT_CLASS (gst_ml_aic_srcpad_parent_class)->finalize(object);
}

void
gst_ml_aic_srcpad_class_init (GstMLAicSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_aic_srcpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_ml_aic_debug, "qtimlaic", 0,
      "QTI ML AIC src pad");
}

void
gst_ml_aic_srcpad_init (GstMLAicSrcPad * pad)
{
  pad->requests = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
}

