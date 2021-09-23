/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * SECTION:element-batch
 *
 * A Muxer that merge multi input tensor streams to stream.
 *
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 \
 * filesrc location=/data/h265.mp4 ! batch.sink_0 \
 * filesrc location=/data/h265.mp4 ! batch.sink_1 \
 * filesrc location=/data/h265.mp4 ! batch.sink_2 \
 * qtibatch name=batch ! filesink location=/data/test/batch
 * ]|
 */
#include "gstbatch.h"

GST_DEBUG_CATEGORY_STATIC (gst_batch_debug);
#define GST_CAT_DEFAULT gst_batch_debug
#define gst_batch_parent_class parent_class
#define MIMETYPE_TENSORS "other/tensors"

enum
{
  PROP_0,
  PROP_SILENT
};


// the capabilities of the inputs and outputs.
static GstStaticPadTemplate gst_sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE(BATCH_CAPS))
  );

static GstStaticPadTemplate gst_src_template = GST_STATIC_PAD_TEMPLATE ("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (MIMETYPE_TENSORS)
  );

G_DEFINE_TYPE (GstBatch, gst_batch, GST_TYPE_AGGREGATOR);

static void gst_batch_set_property (GObject *object,
    guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_batch_get_property (GObject *object,
    guint prop_id, GValue *value, GParamSpec *pspec);

static GstFlowReturn gst_batch_aggregate (GstAggregator * aggregator, gboolean timeout);

// initialize the batch's class
static void
gst_batch_class_init (GstBatchClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *base_aggregator_class = GST_AGGREGATOR_CLASS (klass);;

  gobject_class->set_property = gst_batch_set_property;
  gobject_class->get_property = gst_batch_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &gst_src_template,  GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &gst_sink_template,  GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_set_static_metadata (gstelement_class, "Batch", "Tensor/Muxer",
      "merge multi tensor stream to one", "QTI");

  base_aggregator_class->aggregate =  GST_DEBUG_FUNCPTR (gst_batch_aggregate);
}

/* initialize the new element */
static void
gst_batch_init (GstBatch * batch)
{
  batch->silent = FALSE;
  GST_DEBUG_CATEGORY_INIT(gst_batch_debug, "qtibatch", 0,
      "debug category for batch element");
}

static GstFlowReturn
gst_batch_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstBatch *batch = GST_BATCH (aggregator);
  GstIterator *iter = NULL;
  gboolean all_eos = TRUE;
  GstBuffer *inbuffer = NULL;
  GstBuffer *outbuffer = NULL;
  gboolean done = FALSE;
  GValue list = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  GstClockTime current_time = 0;
  gint channel = 0;
  gchar *pad_name;

  g_value_init (&list,  GST_TYPE_LIST);
  outbuffer = gst_buffer_new ();

  if (outbuffer == NULL) {
    GST_WARNING_OBJECT (aggregator, "failed to create gstbuffer");
    return GST_FLOW_ERROR;
  }

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (batch));

  while (!done) {
    GValue item =  G_VALUE_INIT;
    GstAggregatorPad *pad = NULL;

    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&item);

        if (gst_aggregator_pad_is_eos (pad) == FALSE)
          all_eos = FALSE;

        pad_name = g_strdup (GST_PAD_NAME (pad));
        channel = g_ascii_strtoull (&pad_name[5], NULL, 10);
        GST_DEBUG_OBJECT (aggregator, "pad name: %s, channel: %d", pad_name, channel);
        inbuffer = gst_aggregator_pad_peek_buffer (pad);

        if (!gst_aggregator_pad_has_buffer (GST_AGGREGATOR_PAD (pad))) {
          GST_DEBUG_OBJECT (aggregator, "dosen't have an gstbuffer for channel: %d", channel);
          continue;
        }

        if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (inbuffer)) &&
            current_time < GST_BUFFER_PTS (inbuffer)) {
            current_time = GST_BUFFER_PTS (inbuffer);

        } else if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (inbuffer)) &&
            current_time < GST_BUFFER_DTS (inbuffer)) {
            current_time = GST_BUFFER_DTS (inbuffer);
        }

        guint n_mem = gst_buffer_n_memory(inbuffer);

        for (guint i = 0; i < n_mem; i++) {
          GstMemory* mem = gst_buffer_get_memory (inbuffer, i);
          gst_buffer_append_memory (outbuffer, mem);
          GST_DEBUG_OBJECT (aggregator, "append memory, channel: %d", channel);
          gst_buffer_add_parent_buffer_meta(outbuffer, inbuffer);
          g_value_init (&value, G_TYPE_INT);
          g_value_set_int (&value, channel);
          gst_value_list_append_value (&list, &value);
          g_value_unset (&value);
        }

        gst_aggregator_pad_drop_buffer (pad);
        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (aggregator, "GST_ITERATOR_ERROR happen, done iterate!");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }

  }

  gst_iterator_free (iter);

  if (all_eos) {
    gst_pad_push_event (aggregator->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }

  GstStructure * structure = gst_structure_new_empty ("channel");
  gst_structure_set_value (structure, "channel_id", &list);
  GstVideoRegionOfInterestMeta *meta = gst_buffer_add_video_region_of_interest_meta (
      outbuffer, "channel", 0 ,0 , 0, 0);

  if (meta)
    gst_video_region_of_interest_meta_add_param (meta, structure);

  GST_BUFFER_PTS (outbuffer) = current_time;
  gst_aggregator_finish_buffer (aggregator, outbuffer);
  return GST_FLOW_OK;
}

static void
gst_batch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBatch *batch = GST_BATCH (object);

  switch (prop_id) {
    case PROP_SILENT:
      batch->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_batch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBatch *batch = GST_BATCH (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, batch->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtibatch", GST_RANK_NONE,
      GST_TYPE_BATCH);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    qtibatch, "QTI Batch",
    plugin_init, PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY,
    PACKAGE_ORIGIN)