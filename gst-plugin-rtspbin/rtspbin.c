/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "rtspbin.h"
#include "rtspbinsinkpad.h"

#include <gst/app/gstappsrc.h>

#define GST_CAT_DEFAULT gst_rtsp_bin_debug
GST_DEBUG_CATEGORY (gst_rtsp_bin_debug);

#define gst_rtsp_bin_parent_class parent_class
G_DEFINE_TYPE (GstRtspBin, gst_rtsp_bin, GST_TYPE_BIN);

#define DEFAULT_PROP_ADDRESS "127.0.0.1"
#define DEFAULT_PROP_PORT    "8900"
#define DEFAULT_PROP_MPOINT   "/live"

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_MPOINT,
};

#define GST_RTSP_BIN_CAPS \
    "video/x-h264; " \
    "video/x-h265; " \
    "audio/mpeg; "   \
    "text/x-raw; "   \
    "application/x-rtp"

static GstStaticPadTemplate gst_rtsp_bin_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_RTSP_BIN_CAPS)
);

// Called when the media pipeline is uninitialized
static void
gst_rtsp_media_unprepared (GstRTSPMedia * media, gpointer userdata)
{
  GstRtspBin *rtspbin = (GstRtspBin *) userdata;
  GList *list = NULL;

  for (list = rtspbin->sinkpads; list; list = list->next) {
    GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (list->data);

    if (sinkpad->appsrc) {
      g_clear_pointer (&(sinkpad->appsrc), gst_object_unref);
    }
  }

  rtspbin->media_prepared = FALSE;

  GST_INFO_OBJECT (rtspbin, "Media unprepared");
}

// Called when a new media pipeline is constructed. We can query the
// pipeline and configure appsrc
static void
gst_rtsp_factory_media_configure (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media, gpointer userdata)
{
  GstRtspBin *rtspbin = (GstRtspBin *) userdata;
  GstElement *element = NULL;
  guint idx = 0;
  GList *list = NULL;
  gchar appsrc_name[50];

  // Get the element used for providing the streams of the media
  element = gst_rtsp_media_get_element (media);

  // Iterate all sinkpads
  for (list = rtspbin->sinkpads; list; list = list->next) {
    GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (list->data);
    GstStructure *structure = gst_caps_get_structure (sinkpad->caps, 0);

    if (!structure) {
      GST_ERROR_OBJECT (rtspbin, "Cannot get sinkpad structure");
      return;
    }

    if (gst_structure_has_name (structure, "application/x-rtp")) {
      snprintf (appsrc_name, sizeof (appsrc_name), "pay%d", idx);
    } else {
      snprintf (appsrc_name, sizeof (appsrc_name), "appsrc%d", idx);
    }

    // Get appsrc instance
    sinkpad->appsrc =
        gst_bin_get_by_name_recurse_up (GST_BIN (element), appsrc_name);
    gst_util_set_object_arg (G_OBJECT (sinkpad->appsrc), "format", "time");
    g_object_set (G_OBJECT (sinkpad->appsrc), "caps", sinkpad->caps, NULL);

    idx++;
  }

  // Make sure datails freed when the media is gone
  g_object_set_data_full (G_OBJECT (media), "my-extra-data", NULL,
      (GDestroyNotify) g_free);
  gst_object_unref (element);

  // Notify when our media is unprepared. When all streams are removed
  g_signal_connect (media, "unprepared",
      (GCallback) gst_rtsp_media_unprepared, rtspbin);

  GST_INFO_OBJECT (rtspbin, "Media configured");

  rtspbin->media_prepared = TRUE;
}

static GstRTSPFilterResult
gst_rtsp_bin_client_filter (GstRTSPServer * server, GstRTSPClient * client,
    gpointer user_data)
{
  // Simple filter that shuts down all clients.
  return GST_RTSP_FILTER_REMOVE;
}

static void
gst_rtsp_bin_deinit_server (GstRtspBin * rtspbin)
{
  GstRTSPMountPoints *mounts;

  GST_INFO_OBJECT (rtspbin, "Disconnecting existing clients");

  // Remove the mount point to prevent new clients connecting
  mounts = gst_rtsp_server_get_mount_points (rtspbin->server);
  gst_rtsp_mount_points_remove_factory (mounts, rtspbin->mpoint);
  g_object_unref (mounts);

  // Filter existing clients and remove them
  gst_rtsp_server_client_filter (rtspbin->server,
      gst_rtsp_bin_client_filter, NULL);

  if (rtspbin->server) {
    g_clear_pointer (&(rtspbin->server), gst_object_unref);
  }
}

static gboolean
gst_rtsp_bin_init_server (GstRtspBin * rtspbin)
{
  GstRTSPMountPoints *mounts = NULL;
  guint idx = 0;
  GString *string = NULL;
  gchar *launch_content = NULL;
  GList *list = NULL;
  GstStructure *structure = NULL;

  if (rtspbin->server) {
    GST_INFO_OBJECT (rtspbin, "RTSP server already initialized");
    return TRUE;
  }

  // Initialize RTSP server.
  rtspbin->server = gst_rtsp_server_new ();
  if (!rtspbin->server) {
    GST_ERROR_OBJECT (rtspbin, "Failed to create RTSP server");
    return FALSE;
  }

  // Set the server IP address.
  gst_rtsp_server_set_address (rtspbin->server, rtspbin->address);

  // Set the server port.
  gst_rtsp_server_set_service (rtspbin->server, rtspbin->port);

  // Get the mount points for this server.
  mounts = gst_rtsp_server_get_mount_points (rtspbin->server);
  if (!mounts) {
    GST_ERROR_OBJECT (rtspbin, "Failed to get mount points!");
    g_object_unref (rtspbin->server);
    return FALSE;
  }

  // Create a media factory.
  rtspbin->factory = gst_rtsp_media_factory_new ();
  if (!rtspbin->factory) {
    GST_ERROR_OBJECT (rtspbin, "Failed to create factory!");
    g_object_unref (rtspbin->server);
    return FALSE;
  }

  gst_rtsp_media_factory_set_shared (rtspbin->factory, TRUE);

  // Add the factory with given path to the mount points.
  gst_rtsp_mount_points_add_factory (mounts, rtspbin->mpoint, rtspbin->factory);

  // No need to keep reference for below objects.
  g_object_unref (mounts);

  gst_rtsp_media_factory_set_transport_mode (rtspbin->factory,
      GST_RTSP_TRANSPORT_MODE_PLAY);

  string = g_string_new ("( ");
  for (list = rtspbin->sinkpads; list; list = list->next) {
    GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (list->data);

    structure = gst_caps_get_structure (sinkpad->caps, 0);
    if (!structure) {
      GST_ERROR_OBJECT (rtspbin, "Cannot get sinkpad structure");
      g_object_unref (rtspbin->factory);
      g_object_unref (rtspbin->server);

      return FALSE;
    }

    if (gst_structure_has_name (structure, "video/x-h264")) {
      g_string_append_printf (string, "appsrc is-live=true name=appsrc%d ! queue ! rtph264pay name=pay%d pt=96 ", idx, idx);
    } else if (gst_structure_has_name (structure, "video/x-h265")) {
      g_string_append_printf (string, "appsrc is-live=true name=appsrc%d ! queue ! rtph265pay name=pay%d pt=97 ", idx, idx);
    } else if (gst_structure_has_name (structure, "audio/mpeg")) {
      g_string_append_printf (string, "appsrc is-live=true name=appsrc%d ! queue ! rtpmp4apay name=pay%d pt=97 ", idx, idx);
    } else if (gst_structure_has_name (structure, "text/x-raw")) {
      g_string_append_printf (string, "appsrc is-live=true name=appsrc%d ! queue ! rtpgstpay name=pay%d pt=98 ", idx, idx);
    } else if (gst_structure_has_name (structure, "application/x-rtp")) {
      g_string_append_printf (string, "appsrc is-live=true name=pay%d ", idx);
    }

    idx++;
  }
  string = g_string_append (string, ")");

  // Get the raw character data.
  launch_content = g_string_free (string, FALSE);

  // Set launch pipeline
  gst_rtsp_media_factory_set_launch (rtspbin->factory, launch_content);
  GST_INFO_OBJECT (rtspbin, "RTSP launch pipeline: %s", launch_content);
  g_free (launch_content);

  // Notify when our media is ready, This is called whenever someone asks for
  // the media and a new pipeline with our appsrc is created
  g_signal_connect (rtspbin->factory, "media-configure",
      (GCallback) gst_rtsp_factory_media_configure, rtspbin);

  // Attach the RTSP server to the main context.
  if (0 == gst_rtsp_server_attach (rtspbin->server, NULL)) {
    GST_ERROR_OBJECT (rtspbin,
        "Failed to attach RTSP server to main loop context!");

    g_object_unref (rtspbin->factory);
    g_object_unref (rtspbin->server);

    return FALSE;
  }

  GST_INFO_OBJECT (rtspbin, "Stream ready at rtsp://%s:%s%s",
      rtspbin->address, rtspbin->port, rtspbin->mpoint);

  return TRUE;
}

static GstFlowReturn
gst_rtsp_bin_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (parent);
  GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (pad);
  GstFlowReturn ret;

  if (rtspbin->media_prepared) {
    // Update the timestamp
    // When the first stream is created the timestamp should start from zero
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
      GST_BUFFER_PTS (buffer) = GST_BUFFER_PTS (buffer) - sinkpad->pts_offset;
      GST_BUFFER_DTS (buffer) = GST_BUFFER_DTS (buffer) - sinkpad->dts_offset;
    }

    ret = gst_app_src_push_buffer (GST_APP_SRC (sinkpad->appsrc), buffer);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (rtspbin, "Cannot push buffer to appsrc");
      gst_buffer_unref (buffer);
    }
  } else {
    // Save the current timestamp when there are no streams created
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
      sinkpad->pts_offset = GST_BUFFER_PTS (buffer);
      sinkpad->dts_offset = GST_BUFFER_DTS (buffer);
    }

    gst_buffer_unref (buffer);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_rtsp_bin_check_all_caps_received (GstRtspBin * rtspbin)
{
  GList *list = NULL;

  for (list = rtspbin->sinkpads; list; list = list->next) {
    GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (list->data);

    if (!sinkpad->caps)
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rtsp_bin_all_sink_pads_eos (GstRtspBin * rtspbin, GstPad * pad)
{
  GList *list = NULL;
  gboolean eos = TRUE;

  GST_OBJECT_LOCK (rtspbin);

  // Check all whether other sink pads are in EOS state.
  for (list = GST_ELEMENT (rtspbin)->sinkpads; list; list = list->next) {
    // Skip current sink pad as it is already in EOS state.
    if (g_strcmp0 (GST_PAD_NAME (list->data), GST_PAD_NAME (pad)) == 0)
      continue;

    GST_OBJECT_LOCK (GST_PAD (list->data));
    eos &= GST_PAD_IS_EOS (GST_PAD (list->data));
    GST_OBJECT_UNLOCK (GST_PAD (list->data));
  }

  GST_OBJECT_UNLOCK (rtspbin);

  return eos;
}

static gboolean
gst_rtsp_bin_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (parent);

  GST_INFO_OBJECT(rtspbin, "Received %s event: %p",
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (pad);

      gst_event_parse_caps (event, &caps);
      sinkpad->caps = gst_caps_ref (caps);

      // Check if all caps are received
      if (!gst_rtsp_bin_check_all_caps_received (rtspbin))
        break;

      GST_INFO_OBJECT (rtspbin, "All pad caps received, initialize server");

      if (!gst_rtsp_bin_init_server (rtspbin)) {
        GST_ERROR_OBJECT (rtspbin, "Failed to init RTSP server");
        return FALSE;
      }
    }
    break;
    case GST_EVENT_EOS:
    {
      GstMessage *message;
      guint32 seqnum;
      GstRtspBinSinkPad *sinkpad = GST_RTSP_BIN_SINKPAD (pad);

      // Send EOS to the appsrc attached to the pad
      if (sinkpad->appsrc) {
        gst_element_send_event (sinkpad->appsrc, gst_event_new_eos ());
      }

      // When all other sink pads are in EOS state post EOS.
      if (gst_rtsp_bin_all_sink_pads_eos (rtspbin, pad)) {
        GST_DEBUG_OBJECT (rtspbin, "All sink pads are in EOS");

        // Now we can post the message
        GST_DEBUG_OBJECT (rtspbin, "Posting EOS");

        seqnum = gst_event_get_seqnum (event);
        message = gst_message_new_eos (parent);
        gst_message_set_seqnum (message, seqnum);
        gst_element_post_message (GST_ELEMENT_CAST (parent), message);
      }
    }
    break;

    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstStateChangeReturn
gst_rtsp_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtsp_bin_deinit_server (rtspbin);
      break;
    default:
      break;
  }

  return ret;
}

static GstPad*
gst_rtsp_bin_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_RTSP_BIN_LOCK (rtspbin);

  if (reqname && sscanf (reqname, "sink_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= rtspbin->nextidx) ? index + 1 : rtspbin->nextidx;
  } else {
    index = rtspbin->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  name = g_strdup_printf ("sink_%u", index);

  pad = g_object_new (GST_TYPE_RTSP_BIN_SINKPAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (rtspbin, "Failed to create sink pad!");
    return NULL;
  }

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_rtsp_bin_sink_pad_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_rtsp_bin_sink_pad_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (rtspbin, "Failed to add sink pad!");
    gst_object_unref (pad);
    return NULL;
  }

  rtspbin->sinkpads = g_list_append (rtspbin->sinkpads, pad);
  rtspbin->nextidx = nextindex;

  GST_RTSP_BIN_UNLOCK (rtspbin);

  GST_DEBUG_OBJECT (rtspbin, "Created pad: %s", GST_PAD_NAME (pad));

  return pad;
}

static void
gst_rtsp_bin_release_pad (GstElement * element, GstPad * pad)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (element);

  GST_DEBUG_OBJECT (rtspbin, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_RTSP_BIN_LOCK (rtspbin);
  rtspbin->sinkpads = g_list_remove (rtspbin->sinkpads, pad);
  GST_RTSP_BIN_UNLOCK (rtspbin);

  gst_element_remove_pad (element, pad);
}

static void
gst_rtsp_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (object);

  GST_RTSP_BIN_LOCK (rtspbin);

  switch (prop_id) {
    case PROP_ADDRESS:
      rtspbin->address = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      rtspbin->port = g_strdup (g_value_get_string (value));
      break;
    case PROP_MPOINT:
      rtspbin->mpoint = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_RTSP_BIN_UNLOCK (rtspbin);
}

static void
gst_rtsp_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (object);

  GST_RTSP_BIN_LOCK (rtspbin);

  switch (prop_id) {
    case PROP_ADDRESS:
      g_value_set_string (value, rtspbin->address);
      break;
    case PROP_PORT:
      g_value_set_string (value, rtspbin->port);
      break;
    case PROP_MPOINT:
      g_value_set_string (value, rtspbin->mpoint);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_RTSP_BIN_UNLOCK (rtspbin);
}

static void
gst_rtsp_bin_finalize (GObject * object)
{
  GstRtspBin *rtspbin = GST_RTSP_BIN (object);

  g_mutex_clear (&rtspbin->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (rtspbin));
}

static void
gst_rtsp_bin_class_init (GstRtspBinClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_rtsp_bin_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_rtsp_bin_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_rtsp_bin_finalize);

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_rtsp_bin_sink_template));

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_rtsp_bin_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_rtsp_bin_release_pad);

  g_object_class_install_property (gobject, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "IP address of the server",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_PORT,
      g_param_spec_string ("port", "Port",
          "Port to listening",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_MPOINT,
      g_param_spec_string ("mpoint", "MPoint",
          "Mounting point",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_set_static_metadata (element, "RTSP streaming Bin",
      "Generic/Bin/RTSP", "RTSP streaming bin plugin", "QTI");

  element->change_state = GST_DEBUG_FUNCPTR (gst_rtsp_bin_change_state);
}

static void
gst_rtsp_bin_init (GstRtspBin * rtspbin)
{
  g_mutex_init (&rtspbin->lock);

  rtspbin->address = DEFAULT_PROP_ADDRESS;
  rtspbin->port = DEFAULT_PROP_PORT;
  rtspbin->mpoint = DEFAULT_PROP_MPOINT;

  rtspbin->nextidx = 0;
  rtspbin->sinkpads = NULL;
  rtspbin->server = NULL;
  rtspbin->factory = NULL;
  rtspbin->media_prepared = FALSE;

  GST_OBJECT_FLAG_SET (rtspbin, GST_ELEMENT_FLAG_SINK);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  // Initializes a new GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_rtsp_bin_debug, "qtirtspbin", 0,
      "QTI RTSP Bin");

  return gst_element_register (plugin, "qtirtspbin", GST_RANK_NONE,
      GST_TYPE_RTSP_BIN);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtirtspbin,
    "QTI RTSP Bin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
