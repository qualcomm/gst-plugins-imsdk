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

