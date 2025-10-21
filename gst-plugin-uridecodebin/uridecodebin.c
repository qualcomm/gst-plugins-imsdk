/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "uridecodebin.h"

#define GST_CAT_DEFAULT gst_uri_decodebin_debug
GST_DEBUG_CATEGORY_STATIC (gst_uri_decodebin_debug);

#define gst_uri_decodebin_parent_class parent_class
G_DEFINE_TYPE (GstQtiURIDecodeBin, gst_uri_decodebin, GST_TYPE_BIN);

#define DEFAULT_PROP_URI            NULL
#define DEFAULT_PROP_ITERATIONS     G_MAXUINT

#define DEFAULT_CUR_ITER            0
#define DEFAULT_N_DECODERS          0
#define DEFAULT_N_EOS               0
#define DEFAULT_DURATION            0
#define DEFAULT_LOOP                TRUE

enum
{
  PROP_0,
  PROP_URI,
  PROP_ITERATIONS,
};

static GstPadProbeReturn
gst_uri_decodebin_buffer_probe_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (user_data);
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  GST_TRACE_OBJECT (qtibin, "Pad %s:%s timestamp %" GST_TIME_FORMAT,
      GST_DEBUG_PAD_NAME (pad), GST_TIME_ARGS (GST_BUFFER_DTS_OR_PTS (buffer)));

  GST_QTI_URI_DECODEBIN_LOCK (qtibin);

  // Updating pts of buffer
  if (GST_BUFFER_PTS_IS_VALID (buffer))
    GST_BUFFER_PTS (buffer) += qtibin->cur_iter * qtibin->duration;
  else if (GST_BUFFER_DTS_IS_VALID (buffer))
    GST_BUFFER_DTS (buffer) += qtibin->cur_iter * qtibin->duration;

  GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

  GST_TRACE_OBJECT (qtibin, "Pad %s:%s updated timestamp %" GST_TIME_FORMAT,
      GST_DEBUG_PAD_NAME (pad), GST_TIME_ARGS (GST_BUFFER_DTS_OR_PTS (buffer)));

  return GST_PAD_PROBE_OK;
}

static gboolean
gst_element_send_seek (gpointer user_data)
{
  GstElement *element = GST_ELEMENT_CAST (user_data);
  gboolean success = FALSE;

  success = gst_element_seek_simple (element, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0);

  if (success)
    GST_DEBUG_OBJECT (element, "Seeking back to start of file");
  else
    GST_ERROR_OBJECT (element, "Seek failed");

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
gst_uri_decodebin_query_probe_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (user_data);
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 position = 0;

      gst_query_parse_position (query, NULL, &position);

      GST_QTI_URI_DECODEBIN_LOCK (qtibin);
      position += qtibin->duration * qtibin->cur_iter;
      GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

      gst_query_set_position (query, GST_FORMAT_TIME, position);

      GST_TRACE_OBJECT (pad, "Updated %" GST_PTR_FORMAT, query);
      break;
    }
    case GST_QUERY_DURATION:
    {
      gint64 duration = 0;

      gst_query_parse_duration (query, NULL, &duration);

      // Set internal value for duration.
      if (qtibin->duration == 0)
        qtibin->duration = duration;

      // gint64 overflow check
      duration = (qtibin->duration <= G_MAXINT64 / qtibin->iterations) ?
          (qtibin->duration * qtibin->iterations) : -1;

      gst_query_set_duration (query, GST_FORMAT_TIME, duration);

      GST_TRACE_OBJECT (pad, "Updated %" GST_PTR_FORMAT, query);
      break;
    }
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
gst_uri_decodebin_event_probe_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (user_data);
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment = {};
      gint64 segment_dur = 0;
      gboolean overflow = FALSE;

      GST_LOG_OBJECT (pad, "Received %" GST_PTR_FORMAT, event);

      GST_QTI_URI_DECODEBIN_LOCK (qtibin);

      // Drop all other segment events after first iteration segments are modified
      if (qtibin->cur_iter != DEFAULT_CUR_ITER) {
        GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);
        return GST_PAD_PROBE_DROP;
      }

      GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

      gst_event_copy_segment (event, &segment);

      // int64 overflow check
      segment_dur = segment.stop - segment.start;
      overflow =  (segment_dur > G_MAXINT64 / qtibin->iterations) ||
          (segment.start > G_MAXUINT64 - (segment_dur * qtibin->iterations));

      segment.stop = overflow ? (guint64)(-1) :
          (segment.start + qtibin->iterations * segment_dur);

      // Modify the first segment event
      gst_event_unref (event);
      GST_PAD_PROBE_INFO_DATA (info) = gst_event_new_segment (&segment);

      GST_LOG_OBJECT (pad, "Updated %" GST_PTR_FORMAT,
          GST_PAD_PROBE_INFO_EVENT (info));
      break;
    }
    case GST_EVENT_EOS:
    {
      gboolean drop = FALSE;

      GST_QTI_URI_DECODEBIN_LOCK (qtibin);

      // Determine whether to drop the current EOS event.
      drop = qtibin->loop && (qtibin->cur_iter + 1 < qtibin->iterations);

      qtibin->n_eos++;

      // Increase the iteration counter if one iteration has finished for all pads.
      if (qtibin->n_eos == qtibin->n_decoders) {
        GST_INFO_OBJECT (pad, "Iteration %d completed", qtibin->cur_iter + 1);

        qtibin->n_eos = DEFAULT_N_EOS;

        if (drop)
          g_idle_add (gst_element_send_seek, qtibin);

        qtibin->cur_iter += 1;
      }

      GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

      GST_LOG_OBJECT (pad, "%s %" GST_PTR_FORMAT, drop ? "Drop" : "Forward",
          event);
      return drop ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
    }
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    {
      gboolean drop = FALSE;

      GST_QTI_URI_DECODEBIN_LOCK (qtibin);

      drop = qtibin->loop && (qtibin->cur_iter < qtibin->iterations);

      GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

      GST_LOG_OBJECT (pad, "%s %" GST_PTR_FORMAT, drop ? "Drop" : "Forward",
          event);
      return drop ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
    }
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
gst_uri_decodebin_send_event (GstElement * element, GstEvent * event)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (element);

  GST_QTI_URI_DECODEBIN_LOCK (qtibin);

  if (GST_EVENT_TYPE(event) == GST_EVENT_EOS)
    qtibin->loop = FALSE;
  else if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_START)
    qtibin->cur_iter = DEFAULT_CUR_ITER;
  else if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP)
    qtibin->loop = TRUE;

  GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

  return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
}

static void
gst_uri_decodebin_deep_element_added_cb (GstBin * decodebin, GstBin * sub_bin,
    GstElement * element, gpointer user_data)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (user_data);
  GstPad *pad = NULL;

  if (!gst_uri_has_protocol (qtibin->uri, "file"))
    return;

  if (!GST_IS_VIDEO_DECODER (element) && !GST_IS_AUDIO_DECODER(element))
    return;

  GST_DEBUG_OBJECT (qtibin, "Decoder selected: %s (%s)",
      GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (sub_bin));

  pad = gst_element_get_static_pad (element, "sink");

  if (pad == NULL) {
    GST_ERROR_OBJECT (qtibin, "decoder sinkpad not found");
    return;
  }

  // install probes on the sink pad of decoder
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_BOTH |
      GST_PAD_PROBE_TYPE_EVENT_FLUSH, gst_uri_decodebin_event_probe_cb, qtibin,
      NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_BOTH |
      GST_PAD_PROBE_TYPE_PULL, gst_uri_decodebin_query_probe_cb, qtibin, NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      gst_uri_decodebin_buffer_probe_cb, qtibin, NULL);

  GST_QTI_URI_DECODEBIN_LOCK (qtibin);
  qtibin->n_decoders++;
  GST_QTI_URI_DECODEBIN_UNLOCK (qtibin);

  gst_object_unref (pad);
}

static void
gst_uri_decodebin_pad_added_cb (GstElement * element, GstPad * pad,
    GstQtiURIDecodeBin * qtibin)
{
  GstPad *newpad = NULL;
  GstPadTemplate *pad_tmpl = NULL;
  gchar *padname = NULL;

  GST_DEBUG_OBJECT (element, "pad: %s", GST_PAD_NAME (pad));

  // Create a ghost pad from the newly added pad
  padname = g_strdup_printf ("src_%u", GST_ELEMENT_CAST (qtibin)->numpads);
  pad_tmpl = gst_static_pad_template_get (&srctemplate);

  newpad = gst_ghost_pad_new_from_template (padname, pad, pad_tmpl);

  gst_object_unref (pad_tmpl);
  g_free (padname);

  // activate the pad and add
  gst_pad_set_active (newpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (qtibin), newpad);

  g_object_set_data (G_OBJECT (pad), "qtibin.ghostpad", newpad);
}

static void
gst_uri_decodebin_pad_removed_cb (GstElement * element, GstPad * pad,
    GstQtiURIDecodeBin * qtibin)
{
  GstPad *ghost = NULL;

  GST_DEBUG_OBJECT (element, "Pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  ghost = g_object_get_data (G_OBJECT (pad), "qtibin.ghostpad");

  if (ghost == NULL) {
    GST_WARNING_OBJECT (element, "no ghost pad found");
    return;
  }

  // unghost the pad
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ghost), NULL);

  // deactivate the pad and remove
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT_CAST (qtibin), ghost);
}

static void
gst_uri_decodebin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (object);

  switch (prop_id) {
    case PROP_URI:
      g_free (qtibin->uri);
      qtibin->uri = g_value_dup_string (value);
      g_object_set (qtibin->uridecodebin, "uri", qtibin->uri, NULL);
      break;
    case PROP_ITERATIONS:
      qtibin->iterations = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_uri_decodebin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (object);

  switch (prop_id) {
    case PROP_URI:
      g_value_set_string (value, qtibin->uri);
      break;
    case PROP_ITERATIONS:
      g_value_set_uint (value, qtibin->iterations);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_uri_decodebin_change_state (GstElement * element, GstStateChange transition)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      gint64 duration = 0;
      gboolean success = FALSE;

      if (qtibin->duration > 0)
        break;

      success = gst_element_query_duration (GST_ELEMENT_CAST (qtibin),
          GST_FORMAT_TIME, &duration);

      if (success)
        GST_DEBUG_OBJECT (qtibin, "Duration: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration));
      else
        GST_ERROR_OBJECT (qtibin, "Could not get duration");

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      // reset values so that bin can be reused
      qtibin->cur_iter = DEFAULT_CUR_ITER;
      qtibin->n_decoders = DEFAULT_N_DECODERS;
      qtibin->n_eos = DEFAULT_N_EOS;

      qtibin->duration = DEFAULT_DURATION;
      qtibin->loop = DEFAULT_LOOP;
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_uri_decodebin_finalize (GObject * obj)
{
  GstQtiURIDecodeBin *qtibin = GST_QTI_URI_DECODEBIN (obj);

  g_free (qtibin->uri);

  g_mutex_clear (&qtibin->lock);
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_uri_decodebin_class_init (GstQtiURIDecodeBinClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  // Initializes a new GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_uri_decodebin_debug, "qtiuridecodebin", 0,
      "QTI URI Decode Bin");

  gobject->set_property = gst_uri_decodebin_set_property;
  gobject->get_property = gst_uri_decodebin_get_property;
  gobject->finalize = gst_uri_decodebin_finalize;

  g_object_class_install_property (gobject, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI to decode", DEFAULT_PROP_URI,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_ITERATIONS,
      g_param_spec_uint ("iterations", "Iterations",
          "Specifies the number of iterations the file stream plays. "
          "This property is only used for file sources.", 1, G_MAXUINT,
          DEFAULT_PROP_ITERATIONS, G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element, &srctemplate);

  element->change_state = GST_DEBUG_FUNCPTR (gst_uri_decodebin_change_state);

  element->send_event = GST_DEBUG_FUNCPTR (gst_uri_decodebin_send_event);
}

static void
gst_uri_decodebin_init (GstQtiURIDecodeBin * qtibin)
{
  qtibin->uri = DEFAULT_PROP_URI;
  qtibin->iterations = DEFAULT_PROP_ITERATIONS;

  qtibin->cur_iter = DEFAULT_CUR_ITER;
  qtibin->n_decoders = DEFAULT_N_DECODERS;
  qtibin->n_eos = DEFAULT_N_EOS;

  qtibin->duration = DEFAULT_DURATION;

  qtibin->loop = DEFAULT_LOOP;

  qtibin->uridecodebin = gst_element_factory_make ("uridecodebin3", NULL);
  gst_bin_add (GST_BIN_CAST (qtibin), qtibin->uridecodebin);

  g_signal_connect (qtibin->uridecodebin, "pad-added",
      G_CALLBACK (gst_uri_decodebin_pad_added_cb), qtibin);
  g_signal_connect (qtibin->uridecodebin, "pad-removed",
      G_CALLBACK (gst_uri_decodebin_pad_removed_cb), qtibin);
  g_signal_connect (qtibin->uridecodebin, "deep-element-added",
      G_CALLBACK (gst_uri_decodebin_deep_element_added_cb), qtibin);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiuridecodebin", GST_RANK_PRIMARY,
      GST_TYPE_QTI_URI_DECODEBIN);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiuridecodebin,
    "QTI URI Decode Bin for file looping and other features",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
