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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtimuxserialize.h"
#include <stdio.h>
#include <chrono>
#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>
#include <gst/video/video.h>

using namespace std;

#define PACKAGE "gst-qtimuxserialize-plugin"

GST_DEBUG_CATEGORY_STATIC (gst_qtimuxserialize_debug);
#define GST_CAT_DEFAULT gst_qtimuxserialize_debug

#define DEFAULT_THRESHHOLD  60       // confidence of inference
#define DEFAULT_BOXES       FALSE    // draw boxes or not for visulization
#define DEFAULT_TIMEOUT_V   80       // timeout - ms to wait for all pads

#define BUFFER_DURATION 100000000       /* 10 frames per second */
#define TEST_GAP_PTS 0
#define TEST_GAP_DURATION (5 * GST_SECOND)

#define fail_error_message(msg)     \
  G_STMT_START {        \
    GError *error;        \
    gst_message_parse_error(msg, &error, NULL);       \
    fail_unless(FALSE, "Error Message from %s : %s",      \
    GST_OBJECT_NAME (GST_MESSAGE_SRC(msg)), error->message); \
    g_error_free (error);           \
  } G_STMT_END;

#define gst_qtimuxserialize_parent_class parent_class
G_DEFINE_TYPE (Qtimuxserialize, gst_qtimuxserialize, GST_TYPE_AGGREGATOR);

// Filter signals and args
enum
{
    // FILL ME
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_TIMEOUTV,
    PROP_SILENT,
    PROP_THRESH,
    PROP_BOXES
};

guint g_total_memory = 0;
gboolean g_save = true;

static GstStaticPadTemplate _src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate _sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static void gst_qtimuxserialize_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qtimuxserialize_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_qtimuxserialize_dispose(GObject *object);
static void gst_qtimuxserialize_finalize(GObject *object);
static gboolean gst_qtimuxserialize_start(GstAggregator *trans);
static gboolean gst_qtimuxserialize_stop(GstAggregator *trans);

// GObject GstAggregator vmethod implementations
static GstFlowReturn
gst_qtimuxserialize_aggregate (GstAggregator * aggregator, gboolean timeout)
{
    GstIterator *iter;
    gboolean all_eos = TRUE;
    Qtimuxserialize *testagg;
    GstBuffer *inbuf, *outbuf;
    GstMemory *memory;
    gint cnt = 0, ret = 0;
    char c = timeout?'T':'F';

    g_print ("--------------- Entering _aggregate(),  timeout =%c, \
        g_total_memory =%d\n", c, g_total_memory);
    gboolean done_iterating = FALSE;
    g_total_memory = 0;

    testagg = GST_QTIMUXSERIALIZE (aggregator);
    outbuf = gst_buffer_new ();

    iter = gst_element_iterate_sink_pads (GST_ELEMENT (testagg));
    while (!done_iterating) {
        GValue value = { 0, };
        GstAggregatorPad *pad;

        switch (gst_iterator_next (iter, &value)) {
            case GST_ITERATOR_OK:
                pad = (GstAggregatorPad *)g_value_get_object (&value);

                if (gst_aggregator_pad_is_eos (pad) == FALSE)
                  all_eos = FALSE;

                testagg->gap_expected = TRUE;  // hardcode
                if (testagg->gap_expected == TRUE) {
                  inbuf = gst_aggregator_pad_peek_buffer (pad);
                  g_print ("gap_expected=T, inbuf=%p\n", inbuf);

                  ret = append_to_outbuf(inbuf, outbuf);
                  g_print("append_to_outbuf() return value =%d\n", ret);

                  //save inbuf to local file for unit test
                  if(g_save) {
                      gchar s[64];
                      snprintf(s, 64,  "inbuf_%d", cnt);
                      dump_gst_memory(inbuf, true, s);
                  }

                  gst_buffer_unref (inbuf);
                  testagg->gap_expected = FALSE;
                }

                gst_aggregator_pad_drop_buffer (pad);

                g_value_reset (&value);
                cnt++;
                break;
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync (iter);
                break;
            case GST_ITERATOR_ERROR:
                GST_WARNING_OBJECT (testagg, "Sinkpads iteration error");
                done_iterating = TRUE;
                break;
            case GST_ITERATOR_DONE:
                done_iterating = TRUE;
                break;
        }
        g_print ("Current sink pads = %d\n", cnt);
    }
    gst_iterator_free (iter);

    if (all_eos == TRUE) {
        g_print("all EOS \n");
        GST_INFO_OBJECT (testagg, "no data available, must be EOS");
        gst_pad_push_event (aggregator->srcpad, gst_event_new_eos ());
        return GST_FLOW_EOS;
    }

    verify_outbuf(outbuf);

    gst_aggregator_finish_buffer (aggregator, outbuf);

    g_print ("+++++++++++++++ Leaving %s @ line-%d, Total_pad=%d， \
        g_total_memory=%d\n", __FUNCTION__, __LINE__, cnt,   g_total_memory);

    g_total_memory = 0;
    g_save = false;
    // We just check finish_frame return FLOW_OK
    return GST_FLOW_OK;
}

//class init func
static void gst_qtimuxserialize_class_init (QtimuxserializeClass * klass){
    GObjectClass *gobject_class = (GObjectClass *) klass;
    GstElementClass *gstelement_class = (GstElementClass *) klass;
    GstAggregatorClass *base_aggregator_class = GST_AGGREGATOR_CLASS (klass);

    gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
        &_src_template, GST_TYPE_AGGREGATOR_PAD);

    gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
        &_sink_template, GST_TYPE_AGGREGATOR_PAD);

    gobject_class->set_property = gst_qtimuxserialize_set_property;
    gobject_class->get_property = gst_qtimuxserialize_get_property;
    gobject_class->dispose = gst_qtimuxserialize_dispose;
    gobject_class->finalize = gst_qtimuxserialize_finalize;

    g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
            FALSE, G_PARAM_READWRITE));

    gst_element_class_set_static_metadata (gstelement_class, "Tensor mux",
        "Tensor/Serializer", "Combine N buffers of Tensors", "QTI");

    base_aggregator_class->start =GST_DEBUG_FUNCPTR(gst_qtimuxserialize_start);
    base_aggregator_class->stop = GST_DEBUG_FUNCPTR(gst_qtimuxserialize_stop);

    //mandatory virtue method
    base_aggregator_class->aggregate = GST_DEBUG_FUNCPTR (gst_qtimuxserialize_aggregate);
}

// initialize the new element
static void gst_qtimuxserialize_init (Qtimuxserialize * self){
    GstAggregator *agg = GST_AGGREGATOR (self);
    gst_segment_init (&GST_AGGREGATOR_PAD (agg->srcpad)->segment, GST_FORMAT_TIME);

    self->timestamp = 0;
    self->gap_expected = FALSE;
    self->silent = FALSE;
    self->timeout_v = DEFAULT_TIMEOUT_V;

    g_print ("gst_qtimuxserialize_init()\n");
}

//plugin lifecyle func
void gst_qtimuxserialize_dispose(GObject *object) {
    Qtimuxserialize *qtimuxserialize = GST_QTIMUXSERIALIZE(object);

    GST_DEBUG_OBJECT(qtimuxserialize, "dispose");

    // clean up as possible.  may be called multiple times

    G_OBJECT_CLASS(gst_qtimuxserialize_parent_class)->dispose(object);
    g_print ("gst_qtimuxserialize_dispose()\n");
}

//plugin lifecyle func
void gst_qtimuxserialize_finalize(GObject *object) {
    Qtimuxserialize *qtimuxserialize = GST_QTIMUXSERIALIZE(object);

    GST_DEBUG_OBJECT(qtimuxserialize, "finalize");

    G_OBJECT_CLASS(gst_qtimuxserialize_parent_class)->finalize(object);
}

//plugin lifecyle func
static gboolean gst_qtimuxserialize_start(GstAggregator *trans) {
    Qtimuxserialize *qtimuxserialize = GST_QTIMUXSERIALIZE(trans);

    GST_DEBUG_OBJECT(qtimuxserialize, "start");
    return TRUE;
}

//plugin lifecyle func
static gboolean gst_qtimuxserialize_stop(GstAggregator *trans) {
    Qtimuxserialize *qtimuxserialize = GST_QTIMUXSERIALIZE(trans);

    GST_DEBUG_OBJECT(qtimuxserialize, "stop");

    return TRUE;
}

//plugin set property
static void gst_qtimuxserialize_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec){
    Qtimuxserialize *filter = GST_QTIMUXSERIALIZE (object);

    switch (prop_id) {
        case PROP_TIMEOUTV:
            filter->timeout_v = g_value_get_uint64 (value);
            break;
        case PROP_SILENT:
            filter->silent = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

//plugin get property
static void gst_qtimuxserialize_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec){
    Qtimuxserialize *filter = GST_QTIMUXSERIALIZE (object);

    switch (prop_id) {
        case PROP_TIMEOUTV:
            g_value_set_uint64 (value, filter->timeout_v);
            break;
        case PROP_SILENT:
            g_value_set_boolean (value, filter->silent);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

// entry point to initialize the plug-in
// initialize the plug-in itself
// register the element factories and other features
static gboolean gst_qtimuxserialize_plugin_init (GstPlugin * plugin){
    GST_DEBUG_CATEGORY_INIT (gst_qtimuxserialize_debug, "qtimuxserialize", \
        0, "QTI qtimuxserialize");

    return gst_element_register (plugin, "qtimuxserialize", GST_RANK_NONE,
        GST_TYPE_QTIMUXSERIALIZE);
}

// gstreamer looks for this structure to register qtimuxserializes
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimuxserialize,
    "Muxing tensor and serialize data",
    gst_qtimuxserialize_plugin_init,
    PACKAGE_VERSION,
    "BSD",
    "GStreamer TensorMuxer/Serializer Plug-in",
    "https://www.codeaurora.org/"
)
