/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application: Programmatic GStreamer PPE Compliance Monitoring
 *
 * Description:
 *   This application demonstrates a real-time PPE detection pipeline.
 *   Video input is processed to detect persons and analyze whether they
 *   are wearing required safety equipment such as helmets and vests.
 *   Detected persons and PPE items are attached as metadata, visualized with
 *   bounding boxes on the video stream, and streamed over the network for
 *   remote monitoring and integration.
 *
 *   The pipeline is intentionally NOT provided as a command-line string, and
 *   this application intentionally builds the graph manually. Every
 *   element is created, configured, added to the bin, and linked explicitly in
 *   C/C++. This keeps the code close to how production applications are usually
 *   structured and makes ownership, dynamic pads, bus handling, and error paths
 *   visible to the reader.
 *
 * Supported inputs:
 *   --input-type=usb   --input-config=/dev/video0
 *   --input-type=isp   --input-config=0
 *   --input-type=rtsp  --input-config=rtsp://...
 *   --input-type=file  --input-config=/path/to/video.mp4
 *
 * Supported outputs:
 *   --output-type=none
 *   --output-type=file    --output-config=/path/to/output.mp4
 *   --output-type=rtsp    --output-config=8554
 *   --output-type=webrtc  --output-config=ws://127.0.0.1:8443
 *
 * Display output is enabled by default but can be disabled with:
 *   --no-display
 *
 * Example usage:
 *   ./gst-ppe-detection --input-type=isp --width=1280 --height=720
 *
 *   ./gst-ppe-detection \
 *       --input-type=usb \
 *       --input-config=/dev/video0 \
 *       --width=1280 \
 *       --height=720 \
 *       --framerate=30 \
 *       --output-type=file \
 *       --output-config=output.mp4
 *
 *   ./gst-ppe-detection \
 *       --input-type=rtsp \
 *       --input-config=rtsp://192.168.1.10:8554/camera
 *
 * Notes:
 *   - RTSP input currently assumes H.264 RTP video. If a demo needs H.265 or
 *     another codec, replace the RTSP depay/parser elements in
 *     gst_app_create_input_pipe().
 *   - File and RTSP inputs are explicitly decoded with v4l2h264dec.
 *   - File and RTSP outputs are explicitly encoded with v4l2h264enc.
 *   - WebRTC output uses webrtcbin with an explicit H.264 RTP branch and a
 *     simple WebSocket signalling client.
 */

#include <errno.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <gst/webrtc/webrtc.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FRAMERATE 30
#define DEFAULT_RTSP_LATENCY_MS 200
#define DEFAULT_WEBRTC_ID 1010

#define QUEUE_COUNT 15
#define TEE_COUNT 3

#define STAGE1_MODEL_PATH "foot_track_net_quantized.tflite"
#define STAGE1_LABELS_PATH "foot_track_net.json"
#define STAGE1_SETTINGS_PATH "foot_track_net_settings.json"

#define STAGE2_MODEL_PATH "gear_guard_net.tflite"
#define STAGE2_LABELS_PATH "gear_guard_net.json"

/*
 * Small helper used by dynamic-pad callbacks.
 *
 * Some GStreamer elements, such as qtdemux and rtspsrc, create their source
 * pads only after stream discovery. The callback needs to know which downstream
 * element should receive the pad and whether that stream has already been
 * linked.
 */
typedef struct GstAppPadLinkData
{
  GstElement *target;
  gboolean linked;
} GstAppPadLinkData;

/*
 * User-selectable application configuration.
 *
 * The demo pipeline itself stays hardcoded, but the source/sink endpoints and
 * basic raw-video properties are configurable so that the same compiled sample
 * can be exercised with cameras, files, RTSP streams, display output, encoded
 * file output, or RTSP push output.
 */
typedef struct GstAppConfig
{
  gchar *input_type;
  gchar *input_config;
  gchar *output_type;
  gchar *output_config;
  gchar *model_base_path;
  gboolean no_display;
  gint width;
  gint height;
  gint framerate;
  gint webrtc_id;
} GstAppConfig;

/*
 * Runtime state owned by the application.
 *
 * Keep long-lived objects here so that callbacks can access the pipeline, main
 * loop, dynamic-pad state, without relying on global variables. This pattern
 * is easier to reuse in production applications where multiple pipelines or
 * instances may exist in one process.
 */
typedef struct GstAppContext
{
  GstAppConfig config;

  GstElement *pipeline;
  GMainLoop *mloop;

  GstAppPadLinkData qtdemux_link;
  GstAppPadLinkData rtspsrc_link;

  GstElement *webrtc;
  GstWebRTCDataChannel *webrtc_meta_channel;
  gchar *webrtc_signalling_url;
  SoupSession *ws_session;
  SoupWebsocketConnection *ws_conn;
  guint ws_local_id;

  /*
   * Shutdown guard shared by Ctrl+C, WebRTC signalling disconnects, bus
   * errors, and cleanup paths. WebRTC/live pipelines may not reliably produce
   * EOS, so shutdown must be idempotent and must be able to stop the pipeline
   * directly.
   */
  gboolean is_shutting_down;
} GstAppContext;

/* Forward declaration used by WebRTC callbacks that are defined before the
 * generic application lifecycle helpers.
 */
static void gst_app_request_shutdown (GstAppContext * appctx,
    const gchar * reason, gboolean try_eos);

/**
 * Callback function for ICE candidate
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param mlineindex SDP index
 * @param candidate ICE candidate string
 * @param appctx Pointer to AppContext.
 *
 */
static void
on_webrtc_ice_candidate (GstElement * webrtcbin, guint mlineindex,
  gchar * candidate, GstAppContext * appctx)
{
  if (!appctx || appctx->is_shutting_down || !candidate || !appctx->ws_conn)
    return;

  JsonBuilder * b = json_builder_new ();
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "ice");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "candidate");
  json_builder_add_string_value (b, candidate);
  json_builder_set_member_name (b, "sdpMLineIndex");
  json_builder_add_int_value (b, (gint) mlineindex);
  json_builder_end_object (b);
  json_builder_end_object (b);

  JsonGenerator * gen = json_generator_new ();
  JsonNode * root = json_builder_get_root (b);
  json_generator_set_root (gen, root);
  gchar * msg = json_generator_to_data (gen, NULL);

  soup_websocket_connection_send_text (appctx->ws_conn, msg);

  g_free (msg);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (b);
}

/**
 * Callback function to send offer to peer
 *
 * @param promise GstPromise containing SDP offer
 * @param user_data Pointer to SignallingCtx object
 *
 */
static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;
  if (!appctx || appctx->is_shutting_down)
    return;

  const GstStructure * reply;
  GstWebRTCSessionDescription * offer = NULL;

  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
    &offer, NULL);
  gst_promise_unref (promise);

  if (!offer)
    return;

  if (!appctx->ws_conn) {
    gst_webrtc_session_description_free (offer);
    return;
  }

  GstElement * webrtcbin = appctx->webrtc;

  // Set local description
  GstPromise * local_promise = gst_promise_new ();
  g_signal_emit_by_name (webrtcbin, "set-local-description", offer,
    local_promise);
  gst_promise_interrupt (local_promise);
  gst_promise_unref (local_promise);

  gchar * sdp_str = gst_sdp_message_as_text (offer->sdp);

  JsonBuilder * b = json_builder_new ();
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "sdp");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "type");
  json_builder_add_string_value (b, "offer");
  json_builder_set_member_name (b, "sdp");
  json_builder_add_string_value (b, sdp_str);
  json_builder_end_object (b);
  json_builder_end_object (b);

  JsonGenerator * gen = json_generator_new ();
  JsonNode * root = json_builder_get_root (b);
  json_generator_set_root (gen, root);
  gchar * msg = json_generator_to_data (gen, NULL);

  soup_websocket_connection_send_text (appctx->ws_conn, msg);

  g_free (msg);
  g_free (sdp_str);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (b);

  gst_webrtc_session_description_free (offer);
}

/**
 * Callback function to store a reference to the data channel inside app context
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param channel Newly created data channel object
 * @param appctx Pointer to App context object
 *
 */
static void
on_data_channel (GstElement * webrtcbin, GstWebRTCDataChannel * channel,
    GstAppContext * appctx)
{
  if (!appctx || !webrtcbin || !channel)
    return;

  if (appctx->webrtc_meta_channel)
    g_object_unref (appctx->webrtc_meta_channel);

  appctx->webrtc_meta_channel =
    (GstWebRTCDataChannel *) g_object_ref (channel);

  g_print ("[webrtc] on-data-channel (remote)");
}

/**
 * Callback function to handle "on-open" event
 *
 * @param channel Data channel object
 * @param appctx Pointer to App context object
 *
 */
static void
on_data_channel_open (GstWebRTCDataChannel * channel, GstAppContext * appctx)
{
  if (!appctx || !channel)
    return;

  // Keep a reference; channel can outlive this callback.
  if (appctx->webrtc_meta_channel)
    g_object_unref (appctx->webrtc_meta_channel);

  appctx->webrtc_meta_channel =
    (GstWebRTCDataChannel *) g_object_ref (channel);

  g_print ("[webrtc] on_data_channel_open\n");
}

/**
 * Callback function to handle "on-close" event
 *
 * @param channel Data channel object
 * @param appctx Pointer to App context object
 *
 */
static void
on_data_channel_close (GstWebRTCDataChannel * channel, GstAppContext * appctx)
{
  if (!appctx || !channel)
    return;

  gchar * label = NULL;
  g_object_get (channel, "label", &label, NULL);

  g_print ("[webrtc] on_data_channel_close label - %s\n", label);

  g_free (label);
}

/**
 * Create a data channel and set open/close callbacks
 *
 * @param appctx Pointer to App context object
 *
 */
static void
webrtc_setup_data_channel (GstAppContext * appctx)
{
  if (!appctx)
    return;

  GstElement * webrtcbin = appctx->webrtc;
  gchar name[128];

  // Create DataChannel 'meta' before offer
  GstWebRTCDataChannel * ch = NULL;
  g_snprintf (name, sizeof (name), "meta");
  g_signal_emit_by_name (webrtcbin, "create-data-channel", name, NULL, &ch);

  if (ch) {
    g_signal_connect (ch, "on-open", G_CALLBACK (on_data_channel_open), appctx);
    g_signal_connect (ch, "on-close", G_CALLBACK (on_data_channel_close), appctx);

    g_object_unref (ch);
    g_print ("[webrtc] Created DataChannel 'meta'\n");
  }

  g_signal_connect (webrtcbin, "on-data-channel", G_CALLBACK (on_data_channel),
    appctx);
}

/**
 * Create and send new SDP offer for the given webrtcbin element
 *
 * @param appctx Pointer to the webrtcbin element
 * @param appctx Pointer to App context object
 *
 */
static void
send_offer (GstElement * webrtcbin, GstAppContext * appctx)
{
  GstPromise * promise = gst_promise_new_with_change_func (
      on_offer_created, appctx, NULL);
  g_signal_emit_by_name (webrtcbin, "create-offer", NULL, promise);
}

/**
 * Handles on-negotiation-needed signal
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param appctx Pointer to App context object
 *
 */
static void
on_negotiation_needed (GstElement * webrtcbin, GstAppContext * appctx)
{
  if (!appctx || !appctx->ws_conn)
    return;

  g_print ("[webrtc] negotiation needed\n");
}

/**
 * Process incoming WebSocket signalling messages
 *
 * @param conn Web socket connection that received the message
 * @param type Web socket message type
 * @param message Raw message
 * @param user_data Pointer to SignallingCtx object
 *
 */
static void
on_ws_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
  GBytes * message, gpointer user_data)
{
  (void) conn;

  GstAppContext *appctx = (GstAppContext *) user_data;
  if (!appctx || appctx->is_shutting_down)
    return;

  if (type != SOUP_WEBSOCKET_DATA_TEXT || !message)
    return;

  gsize sz = 0;
  const gchar * txt = (const gchar *) g_bytes_get_data (message, &sz);
  if (!txt || sz == 0)
    return;

  g_print ("[webrtc] signalling rx: %s\n", txt);

  if (g_str_has_prefix (txt, "HELLO")) {
    g_print ("[webrtc] Registration successful\n");
    return;
  } else if (g_strcmp0 (txt, "SESSION_OK") == 0) {
    g_print ("[webrtc] Peer connected");
    return;
  } else if (g_strcmp0 (txt, "OFFER_REQUEST") == 0) {
    g_print ("[webrtc] OFFER_REQUEST\n");
    send_offer (appctx->webrtc, appctx);
    return;
  } else if (g_str_has_prefix (txt, "ERROR")) {
    g_printerr ("[webrtc] %s", txt);
    return;
  }

  // Parse JSON
  JsonParser * parser = json_parser_new ();
  GError * error = NULL;
  if (!json_parser_load_from_data (parser, txt, (gssize) sz, &error)) {
    if (error) {
      g_printerr ("[webrtc] signalling parse error: %s",
                  error->message);
      g_error_free (error);
    }
    g_object_unref (parser);
    return;
  }

  JsonNode * root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    g_object_unref (parser);
    return;
  }
  JsonObject * obj = json_node_get_object (root);

  GstElement * webrtcbin = appctx->webrtc;

  if (json_object_has_member (obj, "sdp")) {
    JsonObject * sdp = json_object_get_object_member (obj, "sdp");
    const gchar * sdp_type = json_object_get_string_member (sdp, "type");
    const gchar * sdp_str = json_object_get_string_member (sdp, "sdp");

    if (sdp_type && sdp_str && g_strcmp0 (sdp_type, "answer") == 0) {
      GstSDPMessage * sdp_msg = NULL;
      gst_sdp_message_new (&sdp_msg);
      if (gst_sdp_message_parse_buffer ((guint8 *) sdp_str, strlen (sdp_str),
          sdp_msg) == GST_SDP_OK) {
        GstWebRTCSessionDescription * answer =
          gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
          sdp_msg);

        GstPromise * p = gst_promise_new ();

        g_signal_emit_by_name (webrtcbin, "set-remote-description", answer, p);
        gst_promise_interrupt (p);
        gst_promise_unref (p);
        gst_webrtc_session_description_free (answer);
      } else {
        gst_sdp_message_free (sdp_msg);
        g_printerr ("[webrtc] Failed to parse SDP answer");
      }
    }
  } else if (json_object_has_member (obj, "ice")) {
    JsonObject * ice = json_object_get_object_member (obj, "ice");
    const gchar * candidate = json_object_get_string_member (ice, "candidate");
    gint mline = json_object_get_int_member (ice, "sdpMLineIndex");

    if (candidate) {
      g_signal_emit_by_name (webrtcbin, "add-ice-candidate", (guint) mline,
        candidate);
    }
  } else if (json_object_has_member (obj, "cmd")) {
    const gchar * cmd = json_object_get_string_member (obj, "cmd");

    g_print ("[webrtc] cmd - %s\n", cmd);
    if (g_strcmp0 (cmd, "READY") == 0) {
      /*
      * Create a dedicated data channel for metadata. The user pipeline still
      * sees only meta_head; this common code owns the WebRTC transport.
      */
      webrtc_setup_data_channel (appctx);

      send_offer (webrtcbin, appctx);

      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
    }
  }

  g_object_unref (parser);
}

/**
 * Handle Web Socket closed "event"
 *
 * @param conn Web socket connection that was closed
 * @param user_data Pointer to SignallingCtx object
 *
 */
static void
on_ws_closed (SoupWebsocketConnection * conn, gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;
  if (!appctx)
    return;

  g_printerr ("[webrtc] signalling websocket closed\n");

  /*
   * The "closed" signal means libsoup has already started or completed the
   * WebSocket close handshake. Do not call
   * soup_websocket_connection_close() again for the same object, because
   * libsoup asserts when close was already sent. Drop the application-owned
   * reference here and clear ws_conn before requesting shutdown.
   */
  if (appctx->ws_conn == conn) {
    g_object_unref (appctx->ws_conn);
    appctx->ws_conn = NULL;
  }

  /*
   * A WebRTC session depends on the signalling channel for ICE/SDP exchange
   * and peer lifetime notifications. When the server or peer disconnects, do
   * not leave the media pipeline running forever; request the same direct
   * shutdown path used by Ctrl+C for WebRTC output.
   */
  gst_app_request_shutdown (appctx,
      "WebRTC signalling websocket closed", FALSE);
}

/**
 * Finalize Web Socket connection
 *
 * @param session SoupSession that initiated the connection
 * @param res Asynchronous result
 * @param userdata Pointer to SignallingCtx object
 *
 */
static void
on_server_connected (SoupSession * session, GAsyncResult * res,
  gpointer userdata)
{
  GError * error = NULL;
  GstAppContext *appctx = (GstAppContext *) userdata;
  if (!appctx)
    return;

  SoupWebsocketConnection *conn =
      soup_session_websocket_connect_finish (session, res, &error);

  if (error) {
    g_printerr ("[webrtc] %s", error->message);
    g_error_free (error);
    gst_app_request_shutdown (appctx,
        "Failed to connect to WebRTC signalling server", FALSE);
    return;
  }

  if (!conn) {
    g_printerr ("[webrtc] soup_session_websocket_connect_finish failed");
    gst_app_request_shutdown (appctx,
        "Failed to create WebRTC signalling connection", FALSE);
    return;
  }

  appctx->ws_conn = conn;

  /* Keep only a non-owning reference to the application context. The context
   * is stack-owned by main(), so it must not be freed by the websocket object.
   */
  g_object_set_data (G_OBJECT (conn), "signalling-ctx", appctx);

  g_print ("[webrtc] Connected to signalling server");

  g_signal_connect (conn, "message", G_CALLBACK (on_ws_message), appctx);
  g_signal_connect (conn, "closed", G_CALLBACK (on_ws_closed), appctx);

  // HELLO + SESSION (python protocol)
  guint local_id = appctx->ws_local_id;

  gchar * hello = g_strdup_printf ("HELLO %u", local_id);
  soup_websocket_connection_send_text (conn, hello);
  g_free (hello);

  g_print ("[webrtc] signalling ready (id=%u)\n", local_id);
}

/**
 * Initialize Web Socket signalling for all streams
 *
 * @param appctx Pointer to App context object
 *
 */
static gboolean
webrtc_connect_signalling (GstAppContext * appctx)
{
  if (!appctx)
    return FALSE;

  if (appctx->ws_local_id == 0) {
    g_printerr ("[webrtc] Missing local WebRTC id.\n");
    return FALSE;
  }

  if (appctx->webrtc_signalling_url == NULL ||
      appctx->webrtc_signalling_url[0] == '\0') {
    g_printerr ("[webrtc] Missing signalling URL.\n");
    return FALSE;
  }

  appctx->ws_session = soup_session_new ();
  SoupMessage * msg = soup_message_new ("GET", appctx->webrtc_signalling_url);
  if (!msg) {
    g_printerr ("[webrtc] Failed to create SoupMessage for %s",
      appctx->webrtc_signalling_url);
    return FALSE;
  }

#ifdef GST_SOUP3
  soup_session_websocket_connect_async (appctx->ws_session, msg, NULL,
      NULL, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) on_server_connected, appctx);
#else
  soup_session_websocket_connect_async (appctx->ws_session, msg, NULL,
      NULL, NULL,
      (GAsyncReadyCallback) on_server_connected, appctx);
#endif

  g_object_unref (msg);

  return TRUE;
}

/**
 * Closes up and cleans all Web Socket signalling connections
 *
 * @param appctx Pointer to App context object
 *
 */
static gboolean
webrtc_disconnect_signalling (GstAppContext * appctx)
{
  if (!appctx)
    return FALSE;

  if (appctx->ws_conn != NULL) {
    SoupWebsocketState state =
        soup_websocket_connection_get_state (appctx->ws_conn);

    /*
     * Close only an OPEN websocket. If the connection is already CLOSING or
     * CLOSED, libsoup has already sent the close frame and calling close again
     * can trigger: assertion '!priv->close_sent' failed.
     */
    if (state == SOUP_WEBSOCKET_STATE_OPEN) {
      soup_websocket_connection_close (appctx->ws_conn,
          SOUP_WEBSOCKET_CLOSE_NO_STATUS, NULL);
    }

    g_object_unref (appctx->ws_conn);
    appctx->ws_conn = NULL;
  }

  if (appctx->ws_session != NULL) {
    g_object_unref (appctx->ws_session);
    appctx->ws_session = NULL;
  }

  g_clear_pointer (&appctx->webrtc_signalling_url, g_free);

  return TRUE;
}

/*
 * Create a GStreamer element by factory name.
 *
 * Production note: element creation is the first place where deployment issues
 * usually appear, for example a missing plugin package, a missing hardware
 * plugin, or an incorrect runtime registry. Printing both the requested factory
 * and element name makes those failures much easier to diagnose.
 */
static GstElement *
gst_app_make_element (const gchar * factory, const gchar * name)
{
  GstElement *element = gst_element_factory_make (factory, name);

  if (element == NULL) {
    g_printerr ("ERROR: Failed to create element '%s' from factory '%s'.\n",
        GST_STR_NULL (name), GST_STR_NULL (factory));
  }

  return element;
}

/**
 * Sets an enum property on a GstElement
 */
void
gst_element_set_enum_property (GstElement * element, const gchar * propname,
    const gchar * valname)
{
  GValue value = G_VALUE_INIT;
  GParamSpec *propspecs = NULL;

  propspecs =
      g_object_class_find_property (G_OBJECT_GET_CLASS (element), propname);
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  gst_value_deserialize (&value, valname);

  g_object_set_property (G_OBJECT (element), propname, &value);
  g_value_unset (&value);
}

/*
 * Sample release utility.
 */
static void
gst_sample_release (GstSample * sample)
{
  gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
  gst_sample_set_buffer (sample, NULL);
#endif
}

/*
 * Link a newly-created dynamic source pad to the static sink pad of target.
 *
 * Educational note: dynamic pads cannot be linked during normal pipeline
 * construction because they do not exist yet. The demuxer/source emits
 * "pad-added" later, and this helper performs the final pad-to-pad link.
 */
static gboolean
gst_app_link_dynamic_src_pad (GstPad * srcpad, GstElement * target)
{
  GstPad *sinkpad = NULL;
  GstPadLinkReturn link_ret = GST_PAD_LINK_REFUSED;

  sinkpad = gst_element_get_static_pad (target, "sink");
  if (sinkpad == NULL) {
    g_printerr ("ERROR: Target element '%s' does not have a static sink pad.\n",
        GST_ELEMENT_NAME (target));
    return FALSE;
  }

  if (gst_pad_is_linked (sinkpad)) {
    gst_object_unref (sinkpad);
    return TRUE;
  }

  link_ret = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);

  if (GST_PAD_LINK_FAILED (link_ret)) {
    g_printerr ("ERROR: Failed to link dynamic pad to '%s'.\n",
        GST_ELEMENT_NAME (target));
    return FALSE;
  }

  return TRUE;
}

/*
 * Handle qtdemux dynamic pads and link the first H.264 video stream.
 *
 * qtdemux can expose multiple streams, for example video, audio, subtitles, or
 * metadata. This sample intentionally accepts only video/x-h264 because the
 * file input branch is designed around h264parse followed by v4l2h264dec.
 */
static void
gst_app_qtdemux_pad_added_cb (GstElement * qtdemux, GstPad * srcpad,
    gpointer userdata)
{
  GstAppPadLinkData *link_data = (GstAppPadLinkData *) userdata;
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  const gchar *name = NULL;

  (void) qtdemux;

  if (link_data->linked)
    return;

  caps = gst_pad_get_current_caps (srcpad);
  if (caps == NULL)
    caps = gst_pad_query_caps (srcpad, NULL);

  if (caps == NULL || gst_caps_is_empty (caps))
    goto cleanup;

  structure = gst_caps_get_structure (caps, 0);
  if (structure == NULL)
    goto cleanup;

  name = gst_structure_get_name (structure);
  if (!g_str_has_prefix (name, "video/x-h264"))
    goto cleanup;

  link_data->linked = gst_app_link_dynamic_src_pad (srcpad, link_data->target);

cleanup:
  if (caps != NULL)
    gst_caps_unref (caps);
}

/*
 * Handle rtspsrc dynamic RTP pads and link the H.264 video stream.
 *
 * rtspsrc may expose several RTP streams. The caps check below filters for an
 * RTP video stream with encoding-name=H264 before linking to rtph264depay.
 */
static void
gst_app_rtspsrc_pad_added_cb (GstElement * rtspsrc, GstPad * srcpad,
    gpointer userdata)
{
  GstAppPadLinkData *link_data = (GstAppPadLinkData *) userdata;
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  const gchar *media = NULL;
  const gchar *encoding_name = NULL;

  (void) rtspsrc;

  if (link_data->linked)
    return;

  caps = gst_pad_get_current_caps (srcpad);
  if (caps == NULL)
    caps = gst_pad_query_caps (srcpad, NULL);

  if (caps == NULL || gst_caps_is_empty (caps))
    goto cleanup;

  structure = gst_caps_get_structure (caps, 0);
  if (structure == NULL)
    goto cleanup;

  if (g_strcmp0 (gst_structure_get_name (structure), "application/x-rtp") != 0)
    goto cleanup;

  media = gst_structure_get_string (structure, "media");
  encoding_name = gst_structure_get_string (structure, "encoding-name");

  if (g_strcmp0 (media, "video") != 0 ||
      g_strcmp0 (encoding_name, "H264") != 0) {
    goto cleanup;
  }

  link_data->linked = gst_app_link_dynamic_src_pad (srcpad, link_data->target);

cleanup:
  if (caps != NULL)
    gst_caps_unref (caps);
}

/* Check whether the selected command-line input type is supported. */
static gboolean
gst_app_is_valid_input_type (const gchar * type)
{
  return g_strcmp0 (type, "usb") == 0 ||
      g_strcmp0 (type, "isp") == 0 ||
      g_strcmp0 (type, "rtsp") == 0 ||
      g_strcmp0 (type, "file") == 0;
}

/* Check whether the selected command-line output type is supported. */
static gboolean
gst_app_is_valid_output_type (const gchar * type)
{
  return g_strcmp0 (type, "none") == 0 ||
      g_strcmp0 (type, "file") == 0 ||
      g_strcmp0 (type, "rtsp") == 0 ||
      g_strcmp0 (type, "webrtc") == 0;
}

/*
 * Fill missing command-line options with practical defaults.
 *
 * Defaults make the sample easy to launch during education and testing while
 * still requiring explicit configs for inputs/outputs that cannot be guessed
 * safely, such as RTSP URLs and file paths.
 */
static void
gst_app_config_apply_defaults (GstAppConfig * config)
{
  if (config->input_type == NULL)
    config->input_type = g_strdup ("isp");

  if (config->output_type == NULL)
    config->output_type = g_strdup ("none");

  if (config->input_config == NULL) {
    if (g_strcmp0 (config->input_type, "usb") == 0)
      config->input_config = g_strdup ("/dev/video0");
    else if (g_strcmp0 (config->input_type, "isp") == 0)
        config->input_config = g_strdup ("0");
    else
      config->input_config = g_strdup ("");
  }

  if (config->output_config == NULL) {
    if (g_strcmp0 (config->output_type, "file") == 0)
      config->output_config = g_strdup ("output.mp4");
    else if (g_strcmp0 (config->output_type, "webrtc") == 0)
      config->output_config = g_strdup ("ws://127.0.0.1:8443");
    else if (g_strcmp0 (config->output_type, "rtsp") == 0)
      config->output_config = g_strdup ("8554");
    else
      config->output_config = g_strdup ("");
  }

  if (config->model_base_path == NULL)
    config->model_base_path = g_strdup ("/etc");

  if (config->webrtc_id <= 0)
    config->webrtc_id = DEFAULT_WEBRTC_ID;
}

/*
 * Validate command-line options after defaults are applied.
 *
 * Validation is kept separate from default assignment so that error handling is
 * predictable and each failure can report a clear user-facing message.
 */
static gboolean
gst_app_config_validate (const GstAppConfig * config)
{
  if (!gst_app_is_valid_input_type (config->input_type)) {
    g_printerr ("ERROR: Unsupported input type '%s'. Use usb, isp, rtsp or file.\n",
        GST_STR_NULL (config->input_type));
    return FALSE;
  }

  if (!gst_app_is_valid_output_type (config->output_type)) {
    g_printerr ("ERROR: Unsupported output type '%s'. Use none, file, rtsp or webrtc.\n",
        GST_STR_NULL (config->output_type));
    return FALSE;
  }

  if ((g_strcmp0 (config->input_type, "rtsp") == 0 ||
          g_strcmp0 (config->input_type, "file") == 0) &&
      (config->input_config == NULL || config->input_config[0] == '\0')) {
    g_printerr ("ERROR: --input-config is required for input type '%s'.\n",
        config->input_type);
    return FALSE;
  }

  if (g_strcmp0 (config->input_type, "isp") == 0 &&
      config->input_config != NULL &&
      config->input_config[0] != '\0') {

    gint cam_id = atoi(config->input_config);
    if (cam_id < 0) {
      g_printerr("ERROR: ISP camera id must be >= 0\n");
      return FALSE;
    }
  }

  if ((g_strcmp0 (config->output_type, "file") == 0 ||
      g_strcmp0 (config->output_type, "webrtc") == 0) &&
      (config->output_config == NULL || config->output_config[0] == '\0')) {
    g_printerr ("ERROR: --output-config is required for output type '%s'.\n",
        config->output_type);
    return FALSE;
  }

  if (config->width <= 0 || config->height <= 0 || config->framerate <= 0) {
    g_printerr ("ERROR: width, height and framerate must be positive values.\n");
    return FALSE;
  }

  return TRUE;
}

/* Release heap memory owned by the configuration structure. */
static void
gst_app_config_free (GstAppConfig * config)
{
  g_free (config->input_type);
  g_free (config->input_config);
  g_free (config->output_type);
  g_free (config->output_config);
  g_free (config->model_base_path);
}

/*
 * Create the H.264 decoder used by file and RTSP input branches.
 *
 * This sample uses v4l2h264dec explicitly so that the selected hardware
 * decoder and I/O modes are visible and deterministic.
 */
static GstElement *
gst_app_create_h264_decoder (void)
{
  GstElement *decoder = NULL;

  decoder = gst_app_make_element ("v4l2h264dec", "h264_decoder");
  if (decoder == NULL)
    return NULL;

  gst_element_set_enum_property (decoder, "capture-io-mode", "dmabuf");
  gst_element_set_enum_property (decoder, "output-io-mode", "dmabuf");

  return decoder;
}

/*
 * Create the H.264 encoder used by file and RTSP output branches.
 *
 * This sample uses v4l2h264enc explicitly instead of a generic encoder bin so
 * that production deployments can control the exact hardware encoder path.
 */
static GstElement *
gst_app_create_h264_encoder (void)
{
  GstElement *encoder = NULL;

  encoder = gst_app_make_element ("v4l2h264enc", "h264_encoder");
  if (encoder == NULL)
    return NULL;

  gst_element_set_enum_property (encoder, "capture-io-mode", "dmabuf");
  gst_element_set_enum_property (encoder, "output-io-mode", "dmabuf-import");

  return encoder;
}

/*
 * Create the selected input branch.
 *
 * The returned element is the last element of the input branch. The user
 * pipeline must link from this element to its own first element.
 *
 * The branch always ends with input_queue. This gives the common input branch
 * and the demo-specific user branch a clean scheduling boundary and a stable
 * handoff point for documentation examples.
 */
static gboolean
gst_app_create_input_pipe (GstAppContext * appctx, GstElement ** input_tail)
{
  GstElement *source = NULL;
  GstElement *demux = NULL;
  GstElement *depay = NULL;
  GstElement *parse = NULL;
  GstElement *decoder = NULL;
  GstElement *queue = NULL;
  GstElement *capsfilter = NULL;
  GstElement *qtivtransform = NULL;
  GstCaps *caps = NULL;
  gboolean ret = FALSE;

  /*
   * Create the input boundary queue first because every supported input branch
   * ends here. The caller receives this element through input_tail and links
   * the user/demo pipeline after it.
   */
  queue = gst_app_make_element ("queue", "input_queue");

  if (!queue)
    goto error;

  /*
   * The queue is added once and then reused as the final element for all input
   * variants. Each branch below links its last real source/decoder element to
   * this common handoff point.
   */
  gst_bin_add (GST_BIN (appctx->pipeline), queue);

  /*
   * Select exactly one input implementation. The rest of the application does
   * not need to know whether frames came from USB, ISP, file, or RTSP.
   */
  if (g_strcmp0 (appctx->config.input_type, "usb") == 0) {
    /*
     * USB input: v4l2src reads frames from a Linux video device such as
     * /dev/video0. The capsfilter below documents and enforces the expected
     * raw frame size before the frame enters the common queue.
     */
    source = gst_app_make_element ("v4l2src", "usb_camera_src");
    if (!source)
      goto error;

    g_object_set (G_OBJECT (source), "device", appctx->config.input_config, NULL);

    capsfilter = gst_app_make_element ("capsfilter", "input_capsfilter");
    if (!capsfilter)
      goto error;

    caps = gst_caps_new_simple ("video/x-raw",
        "width", G_TYPE_INT, appctx->config.width,
        "height", G_TYPE_INT, appctx->config.height,
        NULL);
    g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
    gst_caps_unref (caps);
    caps = NULL;

    /*
     * qtivtransform normalizes the camera branch into a format/path that is
     * suitable for the downstream Qualcomm video pipeline. Keeping it explicit
     * also makes this hardware-specific step visible in documentation.
     */
    qtivtransform = gst_app_make_element ("qtivtransform", "qtivtransform");
    if (!qtivtransform)
      goto error;

    gst_bin_add_many (GST_BIN (appctx->pipeline), source, capsfilter, qtivtransform, NULL);

    /*
     * Link the complete USB branch into input_queue. From this point onward,
     * the user pipeline sees the same input_tail abstraction as every other
     * input type.
     */
    ret = gst_element_link_many (source, capsfilter, qtivtransform, queue, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link USB camera source.\n");
      goto error;
    }
  } else if (g_strcmp0 (appctx->config.input_type, "isp") == 0) {
    /*
     * ISP input: qtiqmmfsrc is the Qualcomm camera source. The capsfilter makes
     * the documented resolution, format, and framerate contract explicit.
     */
    source = gst_app_make_element ("qtiqmmfsrc", "isp_camera_src");
    if (!source)
      goto error;

    /*
    * input-config (when using ISP) is interpreted as camera ID.
    * Default is 0 if not provided or invalid.
    */
    gint camera_id = 0;

    if (appctx->config.input_config != NULL &&
        appctx->config.input_config[0] != '\0') {

      camera_id = atoi(appctx->config.input_config);

      if (camera_id < 0) {
        g_printerr("WARNING: Invalid ISP camera id '%s', using 0\n",
                  appctx->config.input_config);
        camera_id = 0;
      }
    }

    /* Set camera ID property */
    g_object_set (G_OBJECT (source),
                  "camera", camera_id,
                  NULL);

    capsfilter = gst_app_make_element ("capsfilter", "input_capsfilter");
    if (!capsfilter)
      goto error;

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, appctx->config.width,
        "height", G_TYPE_INT, appctx->config.height,
        "framerate", GST_TYPE_FRACTION, appctx->config.framerate, 1,
        NULL);
    g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
    gst_caps_unref (caps);
    caps = NULL;

    gst_bin_add_many (GST_BIN (appctx->pipeline), source, capsfilter, NULL);

    /*
     * Link the ISP camera directly into input_queue. Hardware camera sources
     * are live sources, so downstream state changes may report NO_PREROLL.
     */
    ret = gst_element_link_many (source, capsfilter, queue, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link ISP camera source.\n");
      goto error;
    }
  } else if (g_strcmp0 (appctx->config.input_type, "file") == 0) {
    /*
     * H.264 file input: filesrc reads bytes, qtdemux extracts the MP4 track,
     * h264parse prepares the elementary stream, and v4l2h264dec produces raw
     * frames for the rest of the pipeline.
     */
    source = gst_app_make_element ("filesrc", "file_src");
    demux = gst_app_make_element ("qtdemux", "file_qtdemux");
    parse = gst_app_make_element ("h264parse", "file_h264_parse");
    decoder = gst_app_create_h264_decoder ();

    capsfilter = gst_app_make_element ("capsfilter", "input_capsfilter");
    if (!capsfilter)
      goto error;

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        NULL);
    g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
    gst_caps_unref (caps);
    caps = NULL;

    if (!source || !demux || !parse || !decoder || !capsfilter)
      goto error;

    g_object_set (G_OBJECT (source), "location", appctx->config.input_config, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        source, demux, parse, decoder, capsfilter, NULL);

    /*
     * Only filesrc -> qtdemux can be linked immediately. The demuxer creates
     * its output pad later after it discovers the streams in the container.
     */
    ret = gst_element_link (source, demux);
    if (!ret) {
      g_printerr ("ERROR: Failed to link file source to qtdemux.\n");
      goto error;
    }

    /*
     * Link the static part after qtdemux. The dynamic demuxer pad will be
     * connected to h264parse by gst_app_qtdemux_pad_added_cb().
     */
    ret = gst_element_link_many (parse, decoder, capsfilter, queue, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link H.264 parser/decoder branch.\n");
      goto error;
    }

    /*
     * Store callback state in appctx so the dynamic pad handler knows which
     * element should receive the first H.264 video pad from qtdemux.
     */
    appctx->qtdemux_link.target = parse;
    appctx->qtdemux_link.linked = FALSE;
    g_signal_connect (demux, "pad-added",
        G_CALLBACK (gst_app_qtdemux_pad_added_cb), &appctx->qtdemux_link);
  } else if (g_strcmp0 (appctx->config.input_type, "rtsp") == 0) {
    /*
     * RTSP input: rtspsrc discovers RTP streams at runtime. The branch is kept
     * explicit for H.264 by using rtph264depay, h264parse, and v4l2h264dec.
     */
    source = gst_app_make_element ("rtspsrc", "rtsp_src");
    depay = gst_app_make_element ("rtph264depay", "rtsp_h264_depay");
    parse = gst_app_make_element ("h264parse", "rtsp_h264_parse");
    decoder = gst_app_create_h264_decoder ();

    capsfilter = gst_app_make_element ("capsfilter", "input_capsfilter");
    if (!capsfilter)
      goto error;

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        NULL);
    g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
    gst_caps_unref (caps);
    caps = NULL;

    if (!source || !depay || !parse || !decoder || !capsfilter)
      goto error;

    g_object_set (G_OBJECT (source),
        "location", appctx->config.input_config,
        "latency", DEFAULT_RTSP_LATENCY_MS,
        NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        source, depay, parse, decoder, capsfilter, NULL);

    /*
     * Link the static RTSP decode path. rtspsrc itself is connected later from
     * the pad-added callback once the H.264 RTP pad appears.
     */
    ret = gst_element_link_many (depay, parse, decoder, capsfilter, queue, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link RTSP H.264 depay/parser/decoder branch.\n");
      goto error;
    }

    /*
     * Store callback state in appctx so the dynamic pad handler can link only
     * the first RTP/H264 video stream to rtph264depay.
     */
    appctx->rtspsrc_link.target = depay;
    appctx->rtspsrc_link.linked = FALSE;
    g_signal_connect (source, "pad-added",
        G_CALLBACK (gst_app_rtspsrc_pad_added_cb), &appctx->rtspsrc_link);
  }

  /*
   * Export the common input handoff element to the caller. The caller should
   * not link to source-specific elements directly.
   */
  *input_tail = queue;
  return TRUE;

error:
  return FALSE;
}

/*
 * Send one metadata buffer over the WebRTC metadata data channel.
 *
 * The user pipeline feeds text metadata into meta_head. For WebRTC output the
 * common application code terminates that metadata branch with an appsink and
 * forwards each buffer through the WebRTC data channel named "metadata".
 */
static GstFlowReturn
gst_app_webrtc_meta_new_sample_cb (GstElement * appsink, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo mapinfo = GST_MAP_INFO_INIT;
  GstFlowReturn ret = GST_FLOW_OK;
  gchar *metadata = NULL;

  (void) appsink;

  if (appctx == NULL || appctx->webrtc_meta_channel == NULL)
    return GST_FLOW_OK;

  g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
  if (ret != GST_FLOW_OK || sample == NULL)
    return GST_FLOW_EOS;

  buffer = gst_sample_get_buffer (sample);
  if (buffer == NULL) {
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &mapinfo, GST_MAP_READ)) {
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  /*
   * Metadata is expected to be text/x-raw. Copy it into a NUL-terminated
   * string before passing it to the WebRTC data channel API.
   */
  metadata = g_strndup ((const gchar *) mapinfo.data, mapinfo.size);
  gst_buffer_unmap (buffer, &mapinfo);
  gst_sample_release (sample);

  if (metadata != NULL && metadata[0] != '\0') {
    GstWebRTCDataChannelState state = GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED;

    g_object_get (appctx->webrtc_meta_channel, "ready-state", &state, NULL);
    if (state == GST_WEBRTC_DATA_CHANNEL_STATE_OPEN)
      g_signal_emit_by_name (appctx->webrtc_meta_channel, "send-string", metadata);
  }

  g_free (metadata);
  return GST_FLOW_OK;
}

/*
 * Link a metadata queue to a requested sink pad on qtirtspbin.
 *
 * Video is linked to qtirtspbin by the normal video branch. When RTSP metadata
 * is enabled, this helper requests one additional sink pad and links the
 * metadata branch to it. The exact stream interpretation is owned by qtirtspbin;
 * the skeleton only exposes a stable meta_head entry point to the user code.
 */
static gboolean
gst_app_link_meta_queue_to_rtspbin (GstElement *meta_queue, GstElement *rtspbin)
{
  GstPad *queue_src_pad = NULL;
  GstPad *rtsp_meta_sink_pad = NULL;
  GstPadLinkReturn pad_ret = GST_PAD_LINK_REFUSED;

  queue_src_pad = gst_element_get_static_pad (meta_queue, "src");
  rtsp_meta_sink_pad = gst_element_request_pad_simple (rtspbin, "sink_%u");

  if (queue_src_pad == NULL || rtsp_meta_sink_pad == NULL) {
    g_printerr ("ERROR: Failed to get RTSP metadata pads.\n");
    if (queue_src_pad != NULL)
      gst_object_unref (queue_src_pad);
    if (rtsp_meta_sink_pad != NULL)
      gst_object_unref (rtsp_meta_sink_pad);
    return FALSE;
  }

  pad_ret = gst_pad_link (queue_src_pad, rtsp_meta_sink_pad);
  gst_object_unref (queue_src_pad);
  gst_object_unref (rtsp_meta_sink_pad);

  if (GST_PAD_LINK_FAILED (pad_ret)) {
    g_printerr ("ERROR: Failed to link metadata branch to qtirtspbin.\n");
    return FALSE;
  }

  return TRUE;
}

/*
 * Create the selected output branch.
 *
 * output_head is the first element of the video output branch. The user
 * pipeline must link its last video element to this element.
 *
 * The user pipeline can link a text/x-raw metadata stream to meta_head; this
 * function hides whether that metadata stream is carried by RTSP or WebRTC.
 *
 * The video branch always starts with output_queue. This isolates the
 * demo-specific processing section from display, encoder, or network
 * backpressure.
 */
static gboolean
gst_app_create_output_pipe (GstAppContext * appctx, GstElement ** output_head,
    GstElement ** meta_head)
{
  GstElement *tee = NULL;
  GstElement *tee_queue = NULL;
  GstElement *output_queue = NULL;
  GstElement *encoder = NULL;
  GstElement *parse = NULL;
  GstElement *mux = NULL;
  GstElement *sink = NULL;
  GstElement *meta_queue = NULL;
  gboolean ret = FALSE;

  /* tee_queue is the stable entry point for the common output branch. */
  tee_queue = gst_app_make_element ("queue", "tee_queue");
  output_queue = gst_app_make_element ("queue", "output_queue");
  tee = gst_app_make_element ("tee", "output_tee");

  if (!tee_queue || !tee || !output_queue)
    goto error;

  if (meta_head != NULL)
    *meta_head = NULL;

  gst_bin_add_many (GST_BIN (appctx->pipeline), tee_queue, tee, output_queue, NULL);

  if (!gst_element_link_many(tee_queue, tee, output_queue, NULL)) {
    g_printerr ("ERROR: Failed to link output queue to tee.\n");
    goto error;
  }

  if (!appctx->config.no_display) {
    /*
     * Wayland output is the simplest visual path for demos: raw frames are
     * converted if needed and displayed directly.
     */

    GstElement *display_queue = NULL;
    GstElement *display_sink = NULL;

    display_queue = gst_app_make_element ("queue", "display_queue");
    display_sink = gst_app_make_element ("waylandsink", "wayland_sink");
    if (!display_queue || !display_sink)
      goto error;

    g_object_set (G_OBJECT (display_sink), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (display_sink), "fullscreen", TRUE, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
      display_queue,
      display_sink,
      NULL);

    ret = gst_element_link_many (tee, display_queue, display_sink, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link Wayland output branch.\n");
      goto error;
    }
  }

  if (g_strcmp0 (appctx->config.output_type, "file") == 0) {
    /*
     * H.264 file output: encode raw frames, parse the stream, mux it into MP4,
     * and write it to disk. The explicit encoder keeps hardware usage visible.
     */
    encoder = gst_app_create_h264_encoder ();
    parse = gst_app_make_element ("h264parse", "output_h264_parse");
    mux = gst_app_make_element ("mp4mux", "output_mp4_mux");
    sink = gst_app_make_element ("filesink", "file_sink");
    if (!encoder || !parse || !mux || !sink)
      goto error;

    g_object_set (G_OBJECT (sink), "location", appctx->config.output_config, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        encoder, parse, mux, sink, NULL);

    ret = gst_element_link_many (output_queue, encoder, parse, mux, sink, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link file output branch.\n");
      goto error;
    }
  } else if (g_strcmp0 (appctx->config.output_type, "rtsp") == 0) {
    /*
     * RTSP output: encode raw frames, prepare the H.264 stream, payload it as
     * RTP, and push it to an existing RTSP server through rtspclientsink.
     */
    encoder = gst_app_create_h264_encoder ();
    parse = gst_app_make_element ("h264parse", "output_h264_parse");
    sink = gst_app_make_element ("qtirtspbin", "rtsp_bin");
    if (!encoder || !parse || !sink)
      goto error;

    g_object_set (G_OBJECT (parse), "config-interval", 1, NULL);

    g_object_set (G_OBJECT (sink),
        "address", "0.0.0.0",
        "port", appctx->config.output_config,
        NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        encoder, parse, sink, NULL);

    ret = gst_element_link_many (output_queue, encoder, parse, sink, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link RTSP output branch.\n");
      goto error;
    }

    meta_queue = gst_app_make_element ("queue", "meta_output_queue");
    if (!meta_queue)
        goto error;

    gst_bin_add (GST_BIN (appctx->pipeline), meta_queue);

    if (!gst_app_link_meta_queue_to_rtspbin (meta_queue, sink))
      goto error;

    if (meta_head != NULL)
      *meta_head = meta_queue;
  } else if (g_strcmp0 (appctx->config.output_type, "webrtc") == 0) {
    /*
     * WebRTC output: encode raw frames with the hardware H.264 encoder, parse
     * the elementary stream, packetize it as RTP/H264, and feed it to
     * webrtcbin. The browser-side peer receives a normal H.264 video track.
     */
    GstElement *pay = NULL;
    GstPad *pay_src_pad = NULL;
    GstPad *webrtc_sink_pad = NULL;
    GstCaps *rtp_caps = NULL;
    GstPadLinkReturn pad_ret = GST_PAD_LINK_REFUSED;

    encoder = gst_app_create_h264_encoder ();
    parse = gst_app_make_element ("h264parse", "webrtc_h264_parse");
    pay = gst_app_make_element ("rtph264pay", "webrtc_h264_pay");
    appctx->webrtc = gst_app_make_element ("webrtcbin", "webrtc_bin");
    if (!encoder || !parse || !pay || !appctx->webrtc)
      goto error;

    g_object_set (G_OBJECT (parse), "config-interval", -1, NULL);
    g_object_set (G_OBJECT (pay),
        "pt", 96,
        "config-interval", -1,
        NULL);
    g_object_set (G_OBJECT (appctx->webrtc),
        "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE,
        "stun-server", "stun://stun1.l.google.com:19302",
        NULL);

    /*
     * Keep a copy of the signalling endpoint in appctx because the WebSocket
     * connection is opened asynchronously and used by the WebRTC callbacks.
     */
    appctx->webrtc_signalling_url = g_strdup (appctx->config.output_config);
    appctx->ws_local_id = (guint) appctx->config.webrtc_id;

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        encoder, parse, pay, appctx->webrtc, NULL);

    ret = gst_element_link_many (output_queue, encoder, parse, pay, NULL);
    if (!ret) {
      g_printerr ("ERROR: Failed to link WebRTC H.264 encoder branch.\n");
      goto error;
    }

    /*
     * webrtcbin uses request sink pads. Link the RTP payloader src pad to a
     * requested webrtcbin sink pad explicitly so the pad ownership is visible.
     */
    pay_src_pad = gst_element_get_static_pad (pay, "src");
    webrtc_sink_pad = gst_element_request_pad_simple (appctx->webrtc, "sink_%u");
    if (pay_src_pad == NULL || webrtc_sink_pad == NULL) {
      g_printerr ("ERROR: Failed to get WebRTC RTP pads.\n");
      if (pay_src_pad != NULL)
        gst_object_unref (pay_src_pad);
      if (webrtc_sink_pad != NULL)
        gst_object_unref (webrtc_sink_pad);
      goto error;
    }

    rtp_caps = gst_caps_from_string (
        "application/x-rtp,media=video,encoding-name=H264,payload=96");
    gst_pad_set_caps (pay_src_pad, rtp_caps);
    gst_caps_unref (rtp_caps);

    pad_ret = gst_pad_link (pay_src_pad, webrtc_sink_pad);
    gst_object_unref (pay_src_pad);
    gst_object_unref (webrtc_sink_pad);
    if (GST_PAD_LINK_FAILED (pad_ret)) {
      g_printerr ("ERROR: Failed to link RTP payloader to webrtcbin.\n");
      goto error;
    }

    g_signal_connect (appctx->webrtc, "on-negotiation-needed",
        G_CALLBACK (on_negotiation_needed), appctx);
    g_signal_connect (appctx->webrtc, "on-ice-candidate",
        G_CALLBACK (on_webrtc_ice_candidate), appctx);

    if (!webrtc_connect_signalling (appctx)) {
      g_printerr ("ERROR: Failed to connect WebRTC signalling.\n");
      goto error;
    }

    GstElement *meta_sink = NULL;

    meta_queue = gst_app_make_element ("queue", "meta_output_queue");
    meta_sink = gst_app_make_element ("appsink", "webrtc_meta_sink");
    if (!meta_queue || !meta_sink)
      goto error;

    g_object_set (G_OBJECT (meta_sink),
        "emit-signals", TRUE,
        "sync", FALSE,
        NULL);
    g_signal_connect (meta_sink, "new-sample",
        G_CALLBACK (gst_app_webrtc_meta_new_sample_cb), appctx);

    gst_bin_add_many (GST_BIN (appctx->pipeline), meta_queue, meta_sink, NULL);

    if (!gst_element_link_many (meta_queue, meta_sink, NULL)) {
      g_printerr ("ERROR: Failed to link WebRTC metadata output branch.\n");
      goto error;
    }

    if (meta_head != NULL)
      *meta_head = meta_queue;
  }

  if (g_strcmp0 (appctx->config.output_type, "file") == 0 ||
      g_strcmp0 (appctx->config.output_type, "none") == 0) {
    GstElement *meta_sink = NULL;

    meta_queue = gst_app_make_element ("queue", "meta_output_queue");
    meta_sink = gst_app_make_element ("filesink", "meta_file_sink");

    if (!meta_queue || !meta_sink)
      goto error;

    g_object_set (meta_sink,
        "location", "metadata.txt",
        "sync", FALSE,
        NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), meta_queue, meta_sink, NULL);

    if (!gst_element_link_many (meta_queue, meta_sink, NULL)) {
        g_printerr ("ERROR: Failed to link file metadata output branch.\n");
      goto error;
    }

    if (meta_head)
        *meta_head = meta_queue;
  }

  /*
   * Export the common output entry element to the caller. The user pipeline
   * links into this queue and remains independent from the selected sink type.
   */
  *output_head = tee_queue;
  return TRUE;

error:
  return FALSE;
}

/*
 * Create the demo-specific pipeline section.
 *
 * Documentation examples should replace ONLY this function body. The function
 * is the intentional copy-paste area for product examples. Everything outside
 * this function is reusable application infrastructure.
 *
 * Contract:
 *   - input_tail is the last element from the selected input branch.
 *   - output_head is the first element from the selected video output branch.
 *   - meta_head is NULL when metadata output is disabled, otherwise it is the
 *     first element of the selected metadata output branch. User code can link
 *     text/x-raw metadata buffers to meta_head without knowing whether the
 *     transport is RTSP or WebRTC.
 *   - This function must create all demo-specific elements, add them to the
 *     pipeline, configure them, and link input_tail -> demo -> output_head.
 *
 * Production note: keep element creation, property configuration, bin addition,
 * and linking explicit. This makes failures easy to localize and keeps the
 * sample suitable for step-by-step debugging.
 */
static gboolean
gst_app_create_user_pipe (GstAppContext * appctx, GstElement * input_tail,
    GstElement * output_head, GstElement * meta_head)
{
  GstStructure *delegate_options = NULL;

  GstElement *qtimlvconverter_stage1 = NULL, *qtimlvconverter_stage2 = NULL;
  GstElement *qtimltflite_stage1 = NULL, *qtimltflite_stage2 = NULL;
  GstElement *qtimlpostprocess_stage1 = NULL, *qtimlpostprocess_stage2 = NULL;
  GstElement *qtimetamux_stage1 = NULL, *qtimetamux_stage2 = NULL;
  GstElement *postprocess_caps_stage1 = NULL, *postprocess_caps_stage2 = NULL;
  GstElement *qtivoverlay = NULL, *qtimlmetaparser = NULL;
  GstElement *tee[TEE_COUNT], *queue[QUEUE_COUNT];
  GstCaps *pad_filter = NULL;

  gchar element_name[128];

  /* Create processing elements */
  g_print ("Creating elements...\n");

  qtimlvconverter_stage1 =
    gst_app_make_element ("qtimlvconverter", "qtimlvconverter_stage1");

  qtimlvconverter_stage2 =
    gst_app_make_element ("qtimlvconverter", "qtimlvconverter_stage2");

  qtimltflite_stage1 =
    gst_app_make_element ("qtimltflite", "qtimltflite_stage1");

  qtimltflite_stage2 =
    gst_app_make_element ("qtimltflite", "qtimltflite_stage2");

  qtimlpostprocess_stage1 =
    gst_app_make_element ("qtimlpostprocess", "qtimlpostprocess_stage1");

  qtimlpostprocess_stage2 =
    gst_app_make_element ("qtimlpostprocess", "qtimlpostprocess_stage2");

  postprocess_caps_stage1 =
    gst_app_make_element ("capsfilter", "postprocess_caps_stage1");

  postprocess_caps_stage2 =
    gst_app_make_element ("capsfilter", "postprocess_caps_stage2");

  qtimetamux_stage1 =
    gst_app_make_element ("qtimetamux", "qtimetamux_stage1");

  qtimetamux_stage2 =
    gst_app_make_element ("qtimetamux", "qtimetamux_stage2");

  qtivoverlay =
    gst_app_make_element ("qtivoverlay", "qtivoverlay");

  qtimlmetaparser =
    gst_app_make_element ("qtimlmetaparser", "qtimlmetaparser");

  if (!qtimlvconverter_stage1  || !qtimlvconverter_stage2  ||
      !qtimltflite_stage1      || !qtimltflite_stage2      ||
      !qtimlpostprocess_stage1 || !qtimlpostprocess_stage2 ||
      !postprocess_caps_stage1 || !postprocess_caps_stage2 ||
      !qtimetamux_stage1       || !qtimetamux_stage2       ||
      !qtivoverlay             || !qtimlmetaparser) {
    g_printerr ("ERROR: Failed to create elements\n");
    return FALSE;
  }

  /* Create tee and queue elements */

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_app_make_element ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
        return FALSE;
    }
  }

  for (gint i = 0; i < TEE_COUNT; i++) {
    snprintf (element_name, 127, "tee-%d", i);
    tee[i] = gst_app_make_element ("tee", element_name);
    if (!tee[i]) {
      g_printerr ("Failed to create tee %d\n", i);
        return FALSE;
    }
  }

  /* Set elements' properties */
  g_print ("Setting elements' properties...\n");

  gst_element_set_enum_property (qtimlvconverter_stage2,
      "mode", "roi-batch-cumulative");

  gchar *stage1_labels = g_strdup_printf("%s/labels/%s",
      appctx->config.model_base_path,
      STAGE1_LABELS_PATH);

  gchar *stage1_settings = g_strdup_printf("%s/labels/%s",
      appctx->config.model_base_path,
      STAGE1_SETTINGS_PATH);

  g_object_set (G_OBJECT (qtimlpostprocess_stage1),
      "results", 10,
      "labels", stage1_labels,
      "bbox-stabilization", TRUE,
      "settings", stage1_settings,
      NULL);

  g_free(stage1_labels);
  g_free(stage1_settings);

  gst_element_set_enum_property (qtimlpostprocess_stage1, "module", "qpd");

  gchar *stage2_labels = g_strdup_printf("%s/labels/%s",
      appctx->config.model_base_path,
      STAGE2_LABELS_PATH);

  g_object_set (G_OBJECT (qtimlpostprocess_stage2),
      "results", 10,
      "labels", stage2_labels,
      "bbox-stabilization", TRUE,
      NULL);

    g_free(stage2_labels);

  gst_element_set_enum_property (qtimlpostprocess_stage2, "module", "yolov8");

  delegate_options =
      gst_structure_from_string ("QNNExternalDelegate,backend_type=htp;", NULL);


  gchar *stage1_model = g_strdup_printf("%s/models/%s",
      appctx->config.model_base_path,
      STAGE1_MODEL_PATH);

  g_object_set (G_OBJECT (qtimltflite_stage1),
      "external-delegate-path", "libQnnTFLiteDelegate.so",
      "external-delegate-options", delegate_options,
      "model", stage1_model,
      NULL);

  g_free(stage1_model);

  gst_element_set_enum_property (qtimltflite_stage1, "delegate", "external");


  gchar *stage2_model = g_strdup_printf("%s/models/%s",
      appctx->config.model_base_path,
      STAGE2_MODEL_PATH);

  g_object_set (G_OBJECT (qtimltflite_stage2),
      "external-delegate-path", "libQnnTFLiteDelegate.so",
      "external-delegate-options", delegate_options,
      "model", stage2_model,
      NULL);

  g_free(stage2_model);

  gst_element_set_enum_property (qtimltflite_stage2, "delegate", "external");

  gst_structure_free (delegate_options);

  gst_element_set_enum_property (qtimlmetaparser, "module", "json");

  pad_filter = gst_caps_new_simple ("text/x-raw",
      "format", G_TYPE_STRING, "utf8", NULL);

  g_object_set (G_OBJECT (postprocess_caps_stage1), "caps", pad_filter, NULL);
  g_object_set (G_OBJECT (postprocess_caps_stage2), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  /* Add elements to pipeline */
  g_print ("Adding elements to pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtimlvconverter_stage1, qtimlvconverter_stage2,
      qtimltflite_stage1, qtimltflite_stage2,
      qtimlpostprocess_stage1, qtimlpostprocess_stage2,
      postprocess_caps_stage1, postprocess_caps_stage2,
      qtimetamux_stage1, qtimetamux_stage2,
      qtivoverlay, qtimlmetaparser, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++)
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);

  for (gint i = 0; i < TEE_COUNT; i++)
    gst_bin_add_many (GST_BIN (appctx->pipeline), tee[i], NULL);

  /* Link elements */
  g_print ("Linking elements...\n");

  // Linking input to tee 1. Then linking tee 1 to metamux 1
  gst_element_link_many (input_tail, tee[0], queue[0], qtimetamux_stage1, NULL);

  // Linking input to first stage of ML processing -> metamux 1
  gst_element_link_many (tee[0], queue[1], qtimlvconverter_stage1, queue[2],
    qtimltflite_stage1, queue[3], qtimlpostprocess_stage1, postprocess_caps_stage1,
    queue[4], qtimetamux_stage1, NULL);

  // Linking metamux 1 to tee 2
  gst_element_link_many (qtimetamux_stage1, queue[5], tee[1], NULL);

  // Linking tee 2 to metamux 2
  gst_element_link_many (tee[1], queue[6], qtimetamux_stage2, NULL);

  // Linking input to second stage of ML processing -> metamux 2
  gst_element_link_many (tee[1], queue[7], qtimlvconverter_stage2, queue[8],
    qtimltflite_stage2, queue[9], qtimlpostprocess_stage2, postprocess_caps_stage2,
    queue[10], qtimetamux_stage2, queue[11], qtivoverlay, queue[12], tee[2], NULL);

  // Linking ML output 2 to output head (video stream)
  if (output_head != NULL)
    gst_element_link_many (tee[2], queue[13], output_head, NULL);
  else {
    g_printerr ("Output head element is NULL\n");
    return FALSE;
  }

  /*
   * Linking ML output 2 to meta_head (JSON meta stream)
   * meta_head is the optional metadata output entry point returned by
   * gst_app_create_output_pipe(). Demos that produce metadata should
   * link their metadata source to meta_head when meta_head is not NULL.
   */
  if (meta_head != NULL)
    gst_element_link_many (tee[2], queue[14], qtimlmetaparser, meta_head, NULL);
  else
    g_print ("Cannot link meta stream, meta head is NULL\n");

  return TRUE;
}

/*
 * Create the complete pipeline from input branch, user branch, and output
 * branch.
 *
 * The construction order is important for readability: common input first,
 * common output second, and the demo-specific middle section last.
 */
static gboolean
gst_app_create_pipe (GstAppContext * appctx)
{
  GstElement *input_tail = NULL;
  GstElement *output_head = NULL;
  GstElement *meta_head = NULL;

  appctx->pipeline = gst_pipeline_new ("gst-ppe-detection");
  if (appctx->pipeline == NULL) {
    g_printerr ("ERROR: Failed to create pipeline.\n");
    return FALSE;
  }

  /*
   * Build the reusable input branch selected by --input-type.
   *
   * gst_app_create_input_pipe() hides only the common source-side plumbing:
   * camera/file/RTSP elements, hardware H.264 decode where needed, caps,
   * queues, and dynamic-pad callbacks. It returns input_tail, which is the
   * stable handoff element that the demo-specific pipeline links from.
   */
  if (!gst_app_create_input_pipe (appctx, &input_tail)) {
    g_printerr ("ERROR: Failed to create input branch.\n");
    return FALSE;
  }

  /*
   * Build the reusable output branch selected by --output-type.
   *
   * gst_app_create_output_pipe() owns the common sink-side plumbing:
   * display, hardware H.264 encode, muxing, RTP payloading, or filesink
   * setup. It returns output_head, which is the stable entry
   * element that the demo-specific pipeline links into.
   */
  if (!gst_app_create_output_pipe (appctx, &output_head, &meta_head)) {
    g_printerr ("ERROR: Failed to create output branch.\n");
    return FALSE;
  }

  /*
   * Build the hardcoded demo-specific middle section.
   *
   * This is the only section that documentation examples normally replace.
   * The function receives the common input tail and output head so the sample
   * can focus only on the elements that demonstrate the feature being taught.
   */
  if (!gst_app_create_user_pipe (appctx, input_tail, output_head, meta_head)) {
    g_printerr ("ERROR: Failed to create user pipeline branch.\n");
    return FALSE;
  }

  return TRUE;
}

/*
 * Release all pipeline resources owned by the application context.
 *
 * The caller is expected to set the pipeline to NULL before unref so that
 * elements can release devices, files, network sockets, and hardware resources
 * cleanly.
 */
static void
gst_app_destroy_pipe (GstAppContext * appctx)
{
  if (appctx->webrtc_meta_channel != NULL) {
    g_object_unref (appctx->webrtc_meta_channel);
    appctx->webrtc_meta_channel = NULL;
  }

  if (appctx->ws_conn != NULL || appctx->ws_session != NULL ||
      appctx->webrtc_signalling_url != NULL) {
    webrtc_disconnect_signalling (appctx);
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }
}

/*
 * Handle Ctrl+C by sending EOS when the pipeline is running.
 *
 * Sending EOS gives muxers and sinks a chance to finalize output cleanly. This
 * is especially important for MP4 output, where the container must be finalized
 * before the file is usable.
 */
 /*
 * Request application shutdown from any asynchronous path.
 *
 * This helper is intentionally idempotent. Ctrl+C, WebSocket "closed", bus
 * errors, and cleanup can happen close together. The first caller performs the
 * shutdown decision; later callers only make sure the main loop is not left
 * running.
 *
 * For normal file-style outputs, try_eos lets muxers finalize their containers.
 * For WebRTC/live output, waiting for EOS is unsafe because webrtcbin/network
 * sinks may never forward EOS after the peer or signalling server is gone. In
 * that case the pipeline is moved directly to NULL and the main loop exits.
 */
static void
gst_app_request_shutdown (GstAppContext * appctx, const gchar * reason,
    gboolean try_eos)
{
  GstState state = GST_STATE_NULL;
  GstState pending = GST_STATE_VOID_PENDING;
  gboolean is_webrtc_output = FALSE;

  if (appctx == NULL)
    return;

  if (appctx->is_shutting_down) {
    if (appctx->mloop != NULL)
      g_main_loop_quit (appctx->mloop);
    return;
  }

  appctx->is_shutting_down = TRUE;

  g_print ("Shutdown requested: %s\n",
      reason != NULL ? reason : "no reason specified");

  is_webrtc_output = g_strcmp0 (appctx->config.output_type, "webrtc") == 0;

  /*
   * Stop signalling first. This prevents late ICE/SDP/data-channel callbacks
   * from trying to send messages while the pipeline is being torn down.
   *
   * Do not unref appctx->ws_conn here. The close operation may synchronously
   * emit "closed"; final ownership cleanup is centralized in
   * webrtc_disconnect_signalling().
   */
  if (is_webrtc_output && appctx->ws_conn != NULL) {
    SoupWebsocketState state =
        soup_websocket_connection_get_state (appctx->ws_conn);

    /*
     * Only initiate the close handshake when the socket is still OPEN. If the
     * server disconnected first, the "closed" callback will clear ws_conn. If
     * libsoup is already CLOSING, calling close again triggers a critical
     * assertion because the close frame has already been sent.
     */
    if (state == SOUP_WEBSOCKET_STATE_OPEN) {
      soup_websocket_connection_close (appctx->ws_conn,
          SOUP_WEBSOCKET_CLOSE_GOING_AWAY, "application shutdown");
    }
  }

  /*
   * WebRTC and other live network branches can hang forever if shutdown waits
   * for EOS. For WebRTC, go directly to NULL and quit the main loop.
   */
  if (is_webrtc_output) {
    if (appctx->pipeline != NULL)
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

    if (appctx->mloop != NULL)
      g_main_loop_quit (appctx->mloop);
    return;
  }

  if (appctx->pipeline == NULL) {
    if (appctx->mloop != NULL)
      g_main_loop_quit (appctx->mloop);
    return;
  }

  if (try_eos &&
      gst_element_get_state (appctx->pipeline, &state, &pending, 0) !=
          GST_STATE_CHANGE_FAILURE &&
      (state == GST_STATE_PLAYING || state == GST_STATE_PAUSED)) {
    /*
     * For file-like outputs this gives muxers a chance to write trailers.
     * The EOS bus message will quit the main loop.
     */
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return;
  }

  if (appctx->mloop != NULL)
    g_main_loop_quit (appctx->mloop);
}

/*
 * Handle Ctrl+C.
 *
 * For regular outputs, Ctrl+C requests EOS so muxers/sinks can finalize. For
 * WebRTC output, gst_app_request_shutdown() detects the live WebRTC transport
 * and stops directly instead of waiting for an EOS that may never arrive.
 */
static gboolean
gst_app_handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;

  g_print ("\nReceived interrupt signal.\n");
  gst_app_request_shutdown (appctx, "Ctrl+C", TRUE);

  return TRUE;
}

/*
 * Move the pipeline to PLAYING after a successful PAUSED preroll.
 *
 * Non-live pipelines often need to preroll in PAUSED before PLAYING. Live
 * pipelines may report NO_PREROLL and are handled separately in main().
 */
static void
gst_app_state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old_state = GST_STATE_NULL;
  GstState new_state = GST_STATE_NULL;
  GstState pending = GST_STATE_VOID_PENDING;

  (void) bus;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old_state, &new_state, &pending);

  g_print ("Pipeline state changed from %s to %s.\n",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (new_state));
}

/* Print warning messages from the bus without stopping the application. */
static void
gst_app_warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  (void) bus;
  (void) userdata;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_clear_error (&error);
  g_free (debug);
}

/*
 * Print error messages from the bus and stop the main loop.
 *
 * Bus errors are treated as fatal for this sample because continuing after a
 * pipeline error usually hides the real failure and complicates debugging.
 */
static void
gst_app_error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  (void) bus;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_clear_error (&error);
  g_free (debug);

  gst_app_request_shutdown (appctx, "GStreamer bus error", FALSE);
}

/* Stop the application when the pipeline reaches EOS. */
static void
gst_app_eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;

  (void) bus;

  g_print ("Received EOS from '%s'.\n", GST_MESSAGE_SRC_NAME (message));

  if (appctx != NULL && appctx->mloop != NULL)
    g_main_loop_quit (appctx->mloop);
}

/*
 * Attach bus watches for state, warning, error, and EOS messages.
 *
 * Applications should always watch the bus. Without bus handling, important
 * asynchronous failures from elements can be missed completely.
 */
static gboolean
gst_app_watch_bus (GstAppContext * appctx)
{
  GstBus *bus = NULL;

  bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline));
  if (bus == NULL) {
    g_printerr ("ERROR: Failed to get pipeline bus.\n");
    return FALSE;
  }

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (gst_app_state_changed_cb), appctx->pipeline);
  g_signal_connect (bus, "message::warning",
      G_CALLBACK (gst_app_warning_cb), NULL);
  g_signal_connect (bus, "message::error",
      G_CALLBACK (gst_app_error_cb), appctx);
  g_signal_connect (bus, "message::eos",
      G_CALLBACK (gst_app_eos_cb), appctx);

  gst_object_unref (bus);
  return TRUE;
}

/*
 * Application entry point.
 *
 * The lifecycle is intentionally linear and explicit:
 *   1. parse command-line options and initialize GStreamer;
 *   2. apply defaults and validate the final configuration;
 *   3. build the full programmatic pipeline;
 *   4. attach bus and signal handlers;
 *   5. start the pipeline and run the GLib main loop;
 *   6. stop the pipeline and release all resources.
 */
int
main (int argc, char * argv[])
{
  GOptionContext *option_ctx = NULL;
  GError *error = NULL;
  GstAppContext appctx = { 0 };
  guint interrupt_watch_id = 0;
  gboolean success = FALSE;
  gint result = 0;

  appctx.config.width = DEFAULT_WIDTH;
  appctx.config.height = DEFAULT_HEIGHT;
  appctx.config.framerate = DEFAULT_FRAMERATE;
  appctx.config.webrtc_id = DEFAULT_WEBRTC_ID;

  GOptionEntry entries[] = {
    { "input-type", 0, 0, G_OPTION_ARG_STRING, &appctx.config.input_type,
      "Input type: usb, isp, rtsp or file", "TYPE" },
    { "input-config", 0, 0, G_OPTION_ARG_STRING, &appctx.config.input_config,
      "Input config: /dev/videoX, rtsp://..., or file path", "CONFIG" },
    { "output-type", 0, 0, G_OPTION_ARG_STRING, &appctx.config.output_type,
      "Output type: none, file, rtsp or webrtc", "TYPE" },
    { "output-config", 0, 0, G_OPTION_ARG_STRING, &appctx.config.output_config,
      "Output config for file, RTSP port, or WebRTC signalling URL", "CONFIG" },
    { "model-base-path", 0, 0, G_OPTION_ARG_STRING, &appctx.config.model_base_path,
      "Base path for models and labels (expects /models and /labels inside)", "PATH" },
    { "no-display", 0, 0, G_OPTION_ARG_NONE, &appctx.config.no_display,
      "Disable on-screen display", NULL },
    { "width", 'w', 0, G_OPTION_ARG_INT, &appctx.config.width,
      "Output/input raw video width", "WIDTH" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &appctx.config.height,
      "Output/input raw video height", "HEIGHT" },
    { "framerate", 'f', 0, G_OPTION_ARG_INT, &appctx.config.framerate,
      "Output/input raw video framerate", "FPS" },
    { "webrtc-id", 0, 0, G_OPTION_ARG_INT, &appctx.config.webrtc_id,
      "Local WebRTC signalling id", "ID" },
    { NULL }
  };

  option_ctx = g_option_context_new ("- Runs a two-stage GStreamer PPE detection"
      " pipeline with the options of specifiying different input/outputs");
  if (option_ctx == NULL) {
    g_printerr ("ERROR: Failed to create option context.\n");
    return -EFAULT;
  }

  g_option_context_add_main_entries (option_ctx, entries, NULL);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());

  success = g_option_context_parse (option_ctx, &argc, &argv, &error);
  g_option_context_free (option_ctx);

  if (!success) {
    g_printerr ("ERROR: Failed to parse options: %s\n",
        error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
    gst_app_config_free (&appctx.config);
    return -EFAULT;
  }

  /*
   * Normalize the user configuration before building any GStreamer objects.
   * Defaults make simple demos easy to run, while validation keeps invalid
   * combinations from failing later inside element creation or linking.
   */
  gst_app_config_apply_defaults (&appctx.config);
  if (!gst_app_config_validate (&appctx.config)) {
    gst_app_config_free (&appctx.config);
    return -EINVAL;
  }

  /*
   * Create the complete programmatic pipeline.
   *
   * The helper below creates the GstPipeline container and then assembles the
   * input branch, the output branch, and the user/demo branch explicitly.
   */
  if (!gst_app_create_pipe (&appctx)) {
    result = -EFAULT;
    goto cleanup;
  }

  appctx.mloop = g_main_loop_new (NULL, FALSE);
  if (appctx.mloop == NULL) {
    g_printerr ("ERROR: Failed to create main loop.\n");
    gst_app_config_free (&appctx.config);
    return -ENOMEM;
  }

  /*
   * Attach bus handlers before starting state changes so asynchronous errors,
   * warnings, EOS, and state transitions are visible from the beginning.
   */
  if (!gst_app_watch_bus (&appctx)) {
    result = -EFAULT;
    goto cleanup;
  }

  /*
   * Register Ctrl+C handling through the GLib main context. This keeps signal
   * handling in the same event loop as the GStreamer bus callbacks and allows
   * the app to send EOS instead of terminating abruptly.
   */
  interrupt_watch_id =
      g_unix_signal_add (SIGINT, gst_app_handle_interrupt_signal, &appctx);

  g_print ("Setting pipeline to PLAYING ...\n");
  GstStateChangeReturn ret =
      gst_element_set_state(appctx.pipeline, GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
      g_printerr("ERROR: Failed to transition pipeline to PLAYING.\n");
      result = -EFAULT;
      goto cleanup;
  }

  g_print ("Running main loop ...\n");
  /*
   * From this point the app is event-driven. Bus messages, dynamic pads,
   * appsink samples, and Ctrl+C are handled by callbacks registered earlier.
   */
  g_main_loop_run (appctx.mloop);
  g_print ("Main loop stopped.\n");

cleanup:
  if (interrupt_watch_id != 0)
    g_source_remove (interrupt_watch_id);

  if (appctx.pipeline != NULL) {
    g_print ("Setting pipeline to NULL ...\n");
    gst_element_set_state (appctx.pipeline, GST_STATE_NULL);
  }

  /*
   * Release application-owned resources after the pipeline has been set to
   * NULL, so elements have already stopped using devices, files, and buffers.
   */
  gst_app_destroy_pipe (&appctx);

  if (appctx.mloop != NULL)
    g_main_loop_unref (appctx.mloop);

  gst_app_config_free (&appctx.config);
  gst_deinit ();

  return result;
}
