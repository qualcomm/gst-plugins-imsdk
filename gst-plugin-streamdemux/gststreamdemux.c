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
 * SECTION:element-qtistreamdemux
 *
 * A Muxer that merge multi input tensor streams to stream.
 *
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 \
 * qtibatch name=batch ! qtistreamdemux name=demux \
 * filesrc location=/data/h265.mp4 ! batch.sink_0 filesrc location=/data/h265.mp4 ! batch.sink_1 filesrc location=/data/h265.mp4 ! batch.sink_2 \
 * demux.src_0 ! queue ! filesink location=/data/test0 demux.src_1 ! queue ! filesink location=/data/test1 \
 * demux.src_2 ! queue ! filesink location=/data/test2
 * ]|
 */

#include "gststreamdemux.h"

GST_DEBUG_CATEGORY_STATIC (gst_stream_demux_debug);
#define GST_CAT_DEFAULT gst_stream_demux_debug
#define MIMETYPE_TENSORS "other/tensors"

enum
{
  PROP_0,
  PROP_SILENT
};

typedef struct
{
  GstPad *pad;
  guint n;
} GstDemuxPad;

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (MIMETYPE_TENSORS)
  );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src_%u",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE(STREAM_DEMUX_CAPS))
  );

#define gst_stream_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstStreamDemux, gst_stream_demux, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT(gst_stream_demux_debug, "qtistreamdemux", 0,
        "debug category for stream_demux element"));

static void gst_stream_demux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_stream_demux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_stream_demux_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_stream_demux_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstDemuxPad *gst_demux_get_src_pad (GstStreamDemux *demux,
    const guint nth);
static void gst_stream_demux_dispose (GObject * object);

/* initialize the streamdemux's class */
static void
gst_stream_demux_class_init (GstStreamDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_stream_demux_dispose;
  gobject_class->set_property = gst_stream_demux_set_property;
  gobject_class->get_property = gst_stream_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "Stream Demux", "Tensor/Demux",
  "Demux one stream to multi streams", "QTI Corporation");
}

static void
gst_stream_demux_init (GstStreamDemux * demux)
{
  demux->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (GST_PAD (demux->sinkpad),
      GST_DEBUG_FUNCPTR (gst_stream_demux_sink_event));
  gst_pad_set_chain_function (GST_PAD (demux->sinkpad),
      GST_DEBUG_FUNCPTR (gst_stream_demux_chain));
  GST_PAD_SET_PROXY_CAPS (demux->sinkpad);
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->num_srcpads = 0;
  demux->srcpads = NULL;
  demux->silent = FALSE;
}

static void
gst_stream_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstStreamDemux *demux = GST_STREAM_DEMUX (object);

  switch (prop_id) {
    case PROP_SILENT:
      demux->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_stream_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstStreamDemux *demux = GST_STREAM_DEMUX (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, demux->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_stream_demux_remove_pads (GstStreamDemux * demux)
{

  while (demux->srcpads != NULL) {
    GstDemuxPad *demux_pad = demux->srcpads->data;
    gst_element_remove_pad (GST_ELEMENT (demux), demux_pad->pad);
    g_free (demux_pad);
    demux->srcpads = g_slist_delete_link (demux->srcpads, demux->srcpads);
  }

  demux->srcpads = NULL;
  demux->num_srcpads = 0;
}

static void
gst_stream_demux_dispose (GObject * object)
{
  GstStreamDemux *demux = GST_STREAM_DEMUX (object);
  gst_stream_demux_remove_pads (demux);
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

// this function handles sink events
static gboolean
gst_stream_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstStreamDemux *demux;

  demux = GST_STREAM_DEMUX (parent);

  GST_LOG_OBJECT (demux, "Received %s event",
  GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      break;
    }
    case GST_EVENT_EOS:

      if (!demux->srcpads) {
        gst_event_unref (event);
        return FALSE;
      }
  
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstDemuxPad *
gst_demux_get_src_pad (GstStreamDemux *demux,
    const guint nth)
{
  GSList *list = NULL;
  GstPad *pad = NULL;
  GstDemuxPad *demuxpad = NULL;
  gchar *name = NULL;

  list = demux->srcpads;

  while (list) {
    GstDemuxPad *dpad= (GstDemuxPad*)list->data;
  
    if (nth == dpad->n) {
      GST_DEBUG_OBJECT (demux, "src pad exist, return directly, n:%d", nth);
      return dpad;
    }

    list = list->next;
  }

  demuxpad = g_new0 (GstDemuxPad, 1);
  g_assert (demuxpad != NULL);
  GST_DEBUG_OBJECT (demux, "create src pad, channel_id: %d", nth);

  name = g_strdup_printf ("src_%u", nth);
  pad = gst_pad_new_from_static_template (&src_factory, name);
  demuxpad->pad = pad;
  demuxpad->n = nth;
  demux->srcpads = g_slist_append (demux->srcpads, demuxpad);
  demux->num_srcpads++;
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), pad);

  g_free (name);
  return demuxpad;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_stream_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstStreamDemux *demux;
  guint num_tensor, i;
  GstFlowReturn ret = GST_FLOW_OK;
  const GValue *list;
  const GValue *value;
  guint number;
  GstStructure *structure;

  demux = GST_STREAM_DEMUX (parent);
  num_tensor = gst_buffer_n_memory (buf);

  GST_DEBUG_OBJECT (demux, "Number of tensor: %d", num_tensor);

  GstMeta *meta = gst_buffer_get_meta (buf, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  if (meta) {
    GstVideoRegionOfInterestMeta *roi_meta = (GstVideoRegionOfInterestMeta*)meta;

    for (GList *l = roi_meta->params; l; l = g_list_next (l)) {
      structure = (GstStructure *)l->data;

      if (gst_structure_has_field (structure, "channel_id")) {
        list = gst_structure_get_value (structure, "channel_id");
        number = gst_value_list_get_size (list);
        GST_DEBUG_OBJECT (demux, "number of channel: %d", number);
        break;
      }

    }

  }

  if (number != num_tensor) {
    GST_WARNING_OBJECT (demux, "channel number is not equal to tensor number");
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < num_tensor; i ++) {
    GstDemuxPad *srcpad;
    GstBuffer *outbuf;
    GstMemory *mem;
    gint channel;

    value =  gst_value_list_get_value (list, i);
    channel = g_value_get_int (value);
    srcpad = gst_demux_get_src_pad (demux, channel);
    outbuf = gst_buffer_new ();
    mem = gst_buffer_get_memory (buf, i);
    gst_buffer_append_memory (outbuf, mem);

    outbuf = gst_buffer_make_writable (outbuf);
    gst_buffer_copy_into (outbuf, buf, GST_BUFFER_COPY_METADATA, 0, -1);
    GST_DEBUG_OBJECT (demux, "pushing buffer to srcpads, channel_id: %d", channel);
    /* just push out the incoming buffer without touching it */
    ret = gst_pad_push (srcpad->pad, outbuf);

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (demux, "push buf to srcpad failed, result =%s", gst_flow_get_name (ret));
      break;
    }

  }

  if (meta)
    gst_buffer_remove_meta(buf, meta);

  gst_buffer_unref (buf);
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtistreamdemux", GST_RANK_PRIMARY,
      GST_TYPE_STREAM_DEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    qtistreamdemux, "QTI Stream Demux",
    plugin_init, PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY,
    PACKAGE_ORIGIN)