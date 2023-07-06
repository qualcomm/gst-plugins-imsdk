/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application to demonstrates WebRTC streaming over network
 *
 * Description:
 * This application Demonstrates WebRTC video streaming over network.
 * It comunicates with the signaling server in order to do a registration and
 * receive unique ID. With that ID the other peers can connet and start
 * the streaming
 *
 * Usage:
 * gst-webrtc-sendrecv-example --remote-id <ID>
 * gst-webrtc-sendrecv-example --local-id <ID>
 *
 * Help:
 * gst-webrtc-sendrecv-example --help
 *
 */

#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/rtp/rtp.h>
#include <glib-unix.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <string.h>

enum AppState
{
  APP_STATE_UNKNOWN,
  APP_STATE_ERROR,
  SERVER_CONNECTING,
  SERVER_CONNECTION_ERROR,
  SERVER_CONNECTED,
  SERVER_REGISTERING,
  SERVER_REGISTRATION_ERROR,
  SERVER_REGISTERED,
  SERVER_CLOSED,
  PEER_CONNECTING,
  PEER_CONNECTION_ERROR,
  PEER_CONNECTED,
  PEER_CALL_NEGOTIATING,
  PEER_CALL_STARTED,
  PEER_CALL_STOPPING,
  PEER_CALL_STOPPED,
  PEER_CALL_ERROR,
};

#define GST_APP_CONTEXT_CAST(obj) ((GstAppContext*)(obj))

#define GST_APP_SUMMARY \
  "This application Demonstrates WebRTC video streaming over network.\n"\
  "  It comunicates with the signaling server in order to do a registration and"\
  " receive unique ID.\n  With that ID the other peers can connet and start "\
  "the streaming"

#define SIGNALING_SERVER "wss://webrtc.nirbheek.in:8443"

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of pipeline plugins
  GList *plugins;
  // Pointer to the mainloop
  GMainLoop *mloop;
  // Pointer to the WebRTC bin plugin
  GstElement *webrtcbin;
  // Curent application state
  enum AppState app_state;
  // Pointer to the Soup message
  SoupMessage *soup_message;
  // Create offer flag
  gboolean create_offer;
  // Pointer to the socket connection
  SoupWebsocketConnection *ws_conn;
  // Remote ID
  gchar *remote_id;
  // Local ID
  gchar *local_id;
  // Request remote to generate the offer
  gboolean ask_remote_for_offer;
  gchar **args;
};

// Handle interrupt by CTRL+C
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  guint idx = 0;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  g_main_loop_quit (appctx->mloop);

  return TRUE;
}

// Handle state change events for the pipeline
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;
}

// Handle warning events
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Handle error events
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_print ("error_cb\n");

  g_main_loop_quit (mloop);
}

// Handle end of stream event
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;
  static guint eoscnt = 0;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

static gboolean
wait_for_state_change (GstElement * pipeline)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Pipeline is PREROLLING ...\n");

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline failed to PREROLL!\n");
    return FALSE;
  }

  return TRUE;
}

static void
disconnect_and_quit_loop (GstAppContext * appctx)
{
  if (appctx->ws_conn) {
    if (soup_websocket_connection_get_state (appctx->ws_conn) ==
        SOUP_WEBSOCKET_STATE_OPEN) {
      soup_websocket_connection_close (appctx->ws_conn, 1000, "");
    } else {
      g_clear_object (&appctx->ws_conn);
    }
  }

  if (appctx->mloop) {
    g_main_loop_quit (appctx->mloop);
  }
}

static gchar *
get_str_from_json_obj (JsonObject * obj)
{
  JsonNode *node;
  JsonGenerator *gen;
  gchar *ret;

  gen = json_generator_new ();
  node = json_node_init_object (json_node_alloc (), obj);
  json_generator_set_root (gen, node);
  ret = json_generator_to_data (gen, NULL);

  json_node_free (node);
  g_object_unref (gen);
  return ret;
}

static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstElement *rtph264depay, *decoder, *waylandsink, *h264parse,
      *queue1, *queue2;
  GstPad *sinkpad;
  GstCaps *filtercaps;

  gst_print ("Incoming stream received\n");

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  rtph264depay = gst_element_factory_make ("rtph264depay", NULL);
  queue1 = gst_element_factory_make ("queue", NULL);
  h264parse = gst_element_factory_make ("h264parse", NULL);
  decoder = gst_element_factory_make ("v4l2h264dec", NULL);
  queue2 = gst_element_factory_make ("queue", NULL);
  waylandsink = gst_element_factory_make ("waylandsink", NULL);

  // Append all elements in a list
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, rtph264depay);
  appctx->plugins = g_list_append (appctx->plugins, queue1);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, decoder);
  appctx->plugins = g_list_append (appctx->plugins, queue2);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // Set decoder properties
  g_object_set (G_OBJECT (decoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (decoder), "output-io-mode", 5, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline), rtph264depay, queue1,
      h264parse, decoder, queue2, waylandsink, NULL);

  if (!gst_element_link_many (rtph264depay, queue1, h264parse, decoder,
      queue2, waylandsink, NULL)) {
    gst_printerr ("Error link incoming stream\n");
    disconnect_and_quit_loop (appctx);
    return;
  }

  sinkpad = gst_element_get_static_pad (rtph264depay, "sink");
  if (gst_pad_link (pad, sinkpad)) {
    gst_printerr ("Error link incoming pad\n");
    disconnect_and_quit_loop (appctx);
    return;
  }
  gst_print ("Link incoming stream successfull\n");
  gst_object_unref (sinkpad);

  gst_element_sync_state_with_parent (rtph264depay);
  gst_element_sync_state_with_parent (queue1);
  gst_element_sync_state_with_parent (h264parse);
  gst_element_sync_state_with_parent (decoder);
  gst_element_sync_state_with_parent (queue2);
  gst_element_sync_state_with_parent (waylandsink);
}

static void
send_ice_candidate_message (GstElement * webrtc, guint mlineindex,
    gchar * candidate, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  gchar *text;
  JsonObject *ice, *msg;

  if (appctx->app_state < PEER_CALL_NEGOTIATING) {
    gst_printerr ("Can't send ICE, not in call\n");
    disconnect_and_quit_loop (appctx);
    return;
  }

  ice = json_object_new ();
  json_object_set_string_member (ice, "candidate", candidate);
  json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
  msg = json_object_new ();
  json_object_set_object_member (msg, "ice", ice);
  text = get_str_from_json_obj (msg);
  json_object_unref (msg);

  gst_print ("send_ice_candidate_message data - %s\n", text);

  soup_websocket_connection_send_text (appctx->ws_conn, text);
  g_free (text);
}

static void
send_sdp_to_peer (GstAppContext * appctx, GstWebRTCSessionDescription * desc)
{
  gchar *text;
  JsonObject *msg, *sdp;

  if (appctx->app_state < PEER_CALL_NEGOTIATING) {
    gst_printerr ("Can't send SDP to peer, not in call\n");
    disconnect_and_quit_loop (appctx);
    return;
  }

  text = gst_sdp_message_as_text (desc->sdp);
  sdp = json_object_new ();

  if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER) {
    gst_print ("Sending offer:\n%s\n", text);
    json_object_set_string_member (sdp, "type", "offer");
  } else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER) {
    gst_print ("Sending answer:\n%s\n", text);
    json_object_set_string_member (sdp, "type", "answer");
  } else {
    g_assert_not_reached ();
  }

  json_object_set_string_member (sdp, "sdp", text);
  g_free (text);

  msg = json_object_new ();
  json_object_set_object_member (msg, "sdp", sdp);
  text = get_str_from_json_obj (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (appctx->ws_conn, text);
  g_free (text);
}

// Offer created by our pipeline, to be sent to the peer
static void
on_create_offer (GstPromise * promise, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;

  g_assert_cmphex (appctx->app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (appctx->webrtcbin, "set-local-description",
      offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  // Send offer to peer
  send_sdp_to_peer (appctx, offer);
  gst_webrtc_session_description_free (offer);
}

static void
on_negotiation_needed (GstElement * element, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  appctx->app_state = PEER_CALL_NEGOTIATING;

  if (appctx->ask_remote_for_offer) {
    soup_websocket_connection_send_text (appctx->ws_conn, "OFFER_REQUEST");
  } else if (appctx->create_offer) {
    GstPromise *promise =
        gst_promise_new_with_change_func (on_create_offer, appctx, NULL);
    g_signal_emit_by_name (appctx->webrtcbin, "create-offer", NULL, promise);
  }
}

static void
data_channel_on_error (GObject * dc, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  gst_printerr ("Data channel error\n");
  disconnect_and_quit_loop (appctx);
}

static void
data_channel_on_open (GObject * dc, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GBytes *bytes = g_bytes_new ("data", strlen ("data"));
  gst_print ("data channel opened\n");
  g_signal_emit_by_name (dc, "send-string", "Test msg sent");
  g_signal_emit_by_name (dc, "send-data", bytes);
  g_bytes_unref (bytes);
}

static void
data_channel_on_close (GObject * dc, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  gst_printerr ("Data channel closed\n");
  disconnect_and_quit_loop (appctx);
}

static void
connect_data_channel_signals (GstAppContext * appctx, GObject * data_channel)
{
  g_signal_connect (data_channel, "on-error",
      G_CALLBACK (data_channel_on_error), appctx);
  g_signal_connect (data_channel, "on-open", G_CALLBACK (data_channel_on_open),
      appctx);
  g_signal_connect (data_channel, "on-close",
      G_CALLBACK (data_channel_on_close), appctx);
}

static void
on_data_channel (GstElement * webrtc, GObject * data_channel,
    gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  connect_data_channel_signals (appctx, data_channel);
}

static GstElement *
create_pipeline (gchar * pipeline_des)
{
  GstElement *pipeline = NULL;
  GError *error = NULL;

  g_print ("\nCreating pipeline %s\n", pipeline_des);
  pipeline = gst_parse_launch ((const gchar *) pipeline_des, &error);

  if (error != NULL) {
    g_printerr ("ERROR: %s\n", GST_STR_NULL (error->message));
    g_clear_error (&error);
    return NULL;
  }

  return pipeline;
}

static GstElement*
get_element_from_pipeline (GstElement * pipeline, const gchar * factory_name)
{
  GstElement *element = NULL;
  GstElementFactory *elem_factory = gst_element_factory_find (factory_name);
  GstIterator *it = NULL;
  GValue value = G_VALUE_INIT;

  // Iterate the pipeline and check factory of each element.
  for (it = gst_bin_iterate_elements (GST_BIN (pipeline));
      gst_iterator_next (it, &value) == GST_ITERATOR_OK;
      g_value_reset (&value)) {
    element = GST_ELEMENT (g_value_get_object (&value));

    if (gst_element_get_factory (element) == elem_factory)
      goto free;
  }
  g_value_reset (&value);
  element = NULL;

free:
  gst_iterator_free (it);
  gst_object_unref (elem_factory);

  return element;
}

static gboolean
start_pipeline (GstAppContext * appctx, gboolean create_offer)
{
  GObject *send_channel = NULL;
  gboolean ret = FALSE;
  GstBus *bus = NULL;

  appctx->create_offer = create_offer;

  if ((appctx->webrtcbin = get_element_from_pipeline (
      appctx->pipeline, "webrtcbin")) == NULL)
    return FALSE;

  // Called when go to PLAYING state
  g_signal_connect (appctx->webrtcbin, "on-negotiation-needed",
      G_CALLBACK (on_negotiation_needed), appctx);
  // Provide ICE candidates to the remote peer
  g_signal_connect (appctx->webrtcbin, "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), appctx);

  // Transition to READY state.
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_READY)) {
    wait_for_state_change (appctx->pipeline);
  }

  // Create incoming data channel
  g_signal_emit_by_name (appctx->webrtcbin, "create-data-channel",
      "channel", NULL, &send_channel);
  if (send_channel) {
    gst_print ("Created data channel\n");
    connect_data_channel_signals (appctx, send_channel);
  } else {
    gst_print ("Could not create data channel, is usrsctp available?\n");
  }

  g_signal_connect (appctx->webrtcbin, "on-data-channel",
      G_CALLBACK (on_data_channel), appctx);
  // Incoming streams will be exposed via this signal
  g_signal_connect (appctx->webrtcbin, "pad-added",
      G_CALLBACK (on_incoming_stream), appctx);

  gst_print ("Starting pipeline\n");
  // Transition to PLAYING state.
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx->pipeline);
  }

  return TRUE;
}

static gboolean
try_connect_to_peer (GstAppContext * appctx)
{
  gchar *msg;

  if (soup_websocket_connection_get_state (appctx->ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  if (!appctx->remote_id)
    return FALSE;

  gst_print ("Connecting to signalling server with %s\n", appctx->remote_id);
  appctx->app_state = PEER_CONNECTING;
  msg = g_strdup_printf ("SESSION %s", appctx->remote_id);
  soup_websocket_connection_send_text (appctx->ws_conn, msg);
  g_free (msg);
  return TRUE;
}

// Answer created by our pipeline, to be sent to the peer
static void
on_answer_created (GstPromise * promise, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstWebRTCSessionDescription *answer = NULL;
  const GstStructure *reply;

  g_assert_cmphex (appctx->app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (appctx->webrtcbin, "set-local-description",
      answer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  // Send answer to peer
  send_sdp_to_peer (appctx, answer);
  gst_webrtc_session_description_free (answer);
}

static void
on_offer_set (GstPromise * promise, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  gst_promise_unref (promise);
  promise = gst_promise_new_with_change_func (on_answer_created, appctx, NULL);
  g_signal_emit_by_name (appctx->webrtcbin, "create-answer", NULL, promise);
}

static void
on_offer_received (GstAppContext * appctx, GstSDPMessage * sdp)
{
  GstWebRTCSessionDescription *offer = NULL;
  GstPromise *promise;

  offer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_OFFER, sdp);
  if (offer) {
    // Set remote description on our pipeline
    promise = gst_promise_new_with_change_func (on_offer_set, appctx, NULL);
    g_signal_emit_by_name (appctx->webrtcbin, "set-remote-description",
        offer, promise);
    gst_webrtc_session_description_free (offer);
  }
}

static void
on_server_closed (SoupWebsocketConnection * conn,
    gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  appctx->app_state = SERVER_CLOSED;
  gst_printerr ("Server connection closed\n");
  disconnect_and_quit_loop (appctx);
}

// Message handler from server
static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
    GBytes * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  gchar *text;

  switch (type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
      gst_printerr ("Received unknown binary message, ignoring\n");
      return;
    case SOUP_WEBSOCKET_DATA_TEXT:{
      gsize size;
      const gchar *data = g_bytes_get_data (message, &size);
      // Convert to NULL-terminated string
      text = g_strndup (data, size);
      break;
    }
    default:
      g_assert_not_reached ();
  }

  if (g_strcmp0 (text, "HELLO") == 0) {
    // Server has accepted our registration, we are ready to send commands
    if (appctx->app_state != SERVER_REGISTERING) {
      gst_printerr ("ERROR: Received HELLO when not registering\n");
      disconnect_and_quit_loop (appctx);
      goto out;
    }
    appctx->app_state = SERVER_REGISTERED;
    gst_print ("Registration successfull\n");
    if (!appctx->local_id) {
      // Ask signalling server to connect to specific peer
      if (!try_connect_to_peer (appctx)) {
        gst_printerr ("ERROR: Failed to setup call\n");
        disconnect_and_quit_loop (appctx);
        goto out;
      }
    } else {
      gst_print ("Waiting for connection from peer (local-id: %s)\n",
          appctx->local_id);
    }
  } else if (g_strcmp0 (text, "SESSION_OK") == 0) {
    // Peer connected successfully
    if (appctx->app_state != PEER_CONNECTING) {
      gst_printerr ("ERROR: Received SESSION_OK when not calling\n");
      disconnect_and_quit_loop (appctx);
      goto out;
    }

    appctx->app_state = PEER_CONNECTED;
    // Start negotiation (exchange SDP and ICE candidates)
    // Start camera pipeline
    if (!start_pipeline (appctx, TRUE)) {
      gst_printerr ("ERROR: failed to start pipeline\n");
      disconnect_and_quit_loop (appctx);
    }
  } else if (g_strcmp0 (text, "OFFER_REQUEST") == 0) {
    if (appctx->app_state != SERVER_REGISTERED) {
      gst_printerr ("Received OFFER_REQUEST at incorrect state, ignoring\n");
      goto out;
    }
    gst_print ("Received OFFER_REQUEST, sending offer\n");
    // Peer wants us to start negotiation (exchange SDP and ICE candidates)
    if (!start_pipeline (appctx, TRUE)) {
      gst_printerr ("ERROR: failed to start pipeline\n");
      disconnect_and_quit_loop (appctx);
    }
  } else if (g_str_has_prefix (text, "ERROR")) {
    // Handle errors
    switch (appctx->app_state) {
      case SERVER_CONNECTING:
        appctx->app_state = SERVER_CONNECTION_ERROR;
        break;
      case SERVER_REGISTERING:
        appctx->app_state = SERVER_REGISTRATION_ERROR;
        break;
      case PEER_CONNECTING:
        appctx->app_state = PEER_CONNECTION_ERROR;
        break;
      case PEER_CONNECTED:
      case PEER_CALL_NEGOTIATING:
        appctx->app_state = PEER_CALL_ERROR;
        break;
      default:
        appctx->app_state = APP_STATE_ERROR;
    }
    gst_printerr ("%s\n", text);
    disconnect_and_quit_loop (appctx);
  } else {
    // Look for JSON messages containing SDP and ICE candidates
    JsonNode *root;
    JsonObject *object, *child;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, text, -1, NULL)) {
      gst_printerr ("Unknown message '%s', ignoring\n", text);
      g_object_unref (parser);
      goto out;
    }

    root = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root)) {
      gst_printerr ("Unknown json message '%s', ignoring\n", text);
      g_object_unref (parser);
      goto out;
    }

    // If peer connection wasn't made yet and we are expecting peer will
    // connect to us, launch pipeline at this moment
    if (!appctx->webrtcbin && appctx->local_id) {
      if (!start_pipeline (appctx, FALSE)) {
        gst_printerr ("ERROR: failed to start pipeline\n");
        disconnect_and_quit_loop (appctx);
      }

      appctx->app_state = PEER_CALL_NEGOTIATING;
    }

    object = json_node_get_object (root);
    // Check type of JSON message
    if (json_object_has_member (object, "sdp")) {
      int ret;
      GstSDPMessage *sdp;
      const gchar *text, *sdptype;
      GstWebRTCSessionDescription *answer;

      g_assert_cmphex (appctx->app_state, ==, PEER_CALL_NEGOTIATING);

      child = json_object_get_object_member (object, "sdp");

      if (!json_object_has_member (child, "type")) {
        gst_printerr ("ERROR: received SDP without 'type'\n");
        disconnect_and_quit_loop (appctx);
        goto out;
      }

      sdptype = json_object_get_string_member (child, "type");
      text = json_object_get_string_member (child, "sdp");
      ret = gst_sdp_message_new (&sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);
      ret = gst_sdp_message_parse_buffer ((guint8 *) text, strlen (text), sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      if (g_str_equal (sdptype, "answer")) {
        gst_print ("Received answer:\n%s\n", text);
        answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
            sdp);
        // Set remote description on our pipeline
        if (answer) {
          GstPromise *promise = gst_promise_new ();
          g_signal_emit_by_name (appctx->webrtcbin, "set-remote-description",
              answer, promise);
          gst_promise_interrupt (promise);
          gst_promise_unref (promise);
          appctx->app_state = PEER_CALL_STARTED;
        }
      } else {
        gst_print ("Received offer:\n%s\n", text);
        on_offer_received (appctx, sdp);
      }

    } else if (json_object_has_member (object, "ice")) {
      const gchar *candidate;
      gint sdpmlineindex;

      child = json_object_get_object_member (object, "ice");
      candidate = json_object_get_string_member (child, "candidate");
      sdpmlineindex = json_object_get_int_member (child, "sdpMLineIndex");

      gst_print ("json_object_has_member ice sdpmlineindex - %d data - %s\n",
          sdpmlineindex, candidate);

      // Add ice candidate sent by remote peer
      g_signal_emit_by_name (appctx->webrtcbin, "add-ice-candidate",
          sdpmlineindex, candidate);
    } else {
      gst_printerr ("Ignoring unknown JSON message:\n%s\n", text);
    }
    g_object_unref (parser);
  }

out:
  g_free (text);
}

static void
on_server_connected (SoupSession * session, GAsyncResult * res,
    gpointer userdata)
{
  GError *error = NULL;
  GstAppContext *appctx = (GstAppContext *) userdata;
  gchar *hello_str;

  appctx->ws_conn = soup_session_websocket_connect_finish (session, res, &error);
  if (error) {
    gst_printerr ("%s\n", error->message);
    disconnect_and_quit_loop (appctx);
    g_error_free (error);
    return;
  }

  if (!appctx->ws_conn) {
    gst_printerr ("soup_session_websocket_connect_finish failed");
    return;
  }

  appctx->app_state = SERVER_CONNECTED;
  gst_print ("Connected to signalling server\n");

  g_signal_connect (appctx->ws_conn, "closed",
      G_CALLBACK (on_server_closed), appctx);
  g_signal_connect (appctx->ws_conn, "message",
      G_CALLBACK (on_server_message), appctx);

  // Register in the server with id
  if (soup_websocket_connection_get_state (appctx->ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return;

  if (!appctx->local_id) {
    gint32 id;
    id = g_random_int_range (1000, 10000);
    gst_print ("Registering id %i with server\n", id);
    hello_str = g_strdup_printf ("HELLO %i", id);
  } else {
    gst_print ("Registering id %s with server\n", appctx->local_id);
    hello_str = g_strdup_printf ("HELLO %s", appctx->local_id);
  }

  appctx->app_state = SERVER_REGISTERING;

  soup_websocket_connection_send_text (appctx->ws_conn, hello_str);
  g_free (hello_str);
}

// Connect to the signalling server
static void
connect_to_websocket_server_async (GstAppContext * appctx)
{
  SoupLogger *logger;
  SoupSession *session;
  const char *https_aliases[] = { "wss", NULL };

  session =
      soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, FALSE,
      SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
      //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-certificates.crt",
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  appctx->soup_message = soup_message_new (SOUP_METHOD_GET, SIGNALING_SERVER);

  gst_print ("Connecting to server...\n");

  // Connect to the signaling server
  soup_session_websocket_connect_async (session, appctx->soup_message, NULL,
      NULL, NULL, (GAsyncReadyCallback) on_server_connected, appctx);
  appctx->app_state = SERVER_CONNECTING;
}

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = NULL;
  g_return_val_if_fail ((ctx = g_new0 (GstAppContext, 1)) != NULL, NULL);

  ctx->pipeline = NULL;
  ctx->plugins = NULL;
  ctx->mloop = NULL;

  ctx->app_state = 0;
  ctx->webrtcbin = NULL;
  ctx->soup_message = NULL;
  ctx->create_offer = FALSE;
  ctx->ws_conn = NULL;
  ctx->remote_id = NULL;
  ctx->local_id = NULL;
  ctx->ask_remote_for_offer = FALSE;
  ctx->args = NULL;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx == NULL)
    return;

  // If the plugins list is not empty, unlink and remove all elements
  if (ctx->plugins != NULL) {
    GstElement *element_curr = (GstElement *) ctx->plugins->data;
    GstElement *element_next;

    GList *list = ctx->plugins->next;
    for (; list != NULL; list = list->next) {
      element_next = (GstElement *) list->data;
      gst_element_unlink (element_curr, element_next);
      gst_bin_remove (GST_BIN (ctx->pipeline), element_curr);
      element_curr = element_next;
    }
    gst_bin_remove (GST_BIN (ctx->pipeline), element_curr);

    // Free the plugins list
    g_list_free (ctx->plugins);
    ctx->plugins = NULL;
  }

  if (ctx->pipeline != NULL) {
    gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
    gst_object_unref (ctx->pipeline);
  }

  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->webrtcbin)
    gst_object_unref (ctx->webrtcbin);

  if (ctx->soup_message)
    gst_object_unref (ctx->soup_message);

  g_free (ctx);
  return;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *optctx;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  GstBus *bus = NULL;
  GstAppContext *appctx = NULL;
  gint status = -1;
  GError *error = NULL;
  GThread *mthread = NULL;

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create app context.
  if ((appctx = gst_app_context_new ()) == NULL) {
    g_printerr ("ERROR: Couldn't create app context!\n");
    goto exit;
  }

  // Configure input parameters
  GOptionEntry options[] = {
    {"remote-id", 'r', 0, G_OPTION_ARG_STRING, &appctx->remote_id,
      "ID of the remote peer which will connect to", "ID"},
    {"local-id", 'l', 0, G_OPTION_ARG_STRING, &appctx->local_id,
        "Our local ID which remote peer can connect to us", "ID"},
    {"ask-remote-for-offer", 'o', 0, G_OPTION_ARG_NONE,
        &appctx->ask_remote_for_offer,
        "Request remote to generate the offer and we'll answer", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &appctx->args, NULL},
    { NULL }
  };

  optctx = g_option_context_new (GST_APP_SUMMARY);
  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());

  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (error->message));

    g_option_context_free (optctx);
    g_clear_error (&error);

    gst_app_context_free (appctx);
    return -1;
  }
  g_option_context_free (optctx);

  if (appctx->args == NULL || (!appctx->remote_id && !appctx->local_id)) {
    g_print ("Usage: gst-webrtc-sendrecv-example <pipeline> [OPTION]\n");
    g_print ("\nFor help: gst-webrtc-sendrecv-example [-h | --help]\n\n");
    goto exit;
  }

  if (appctx->remote_id && appctx->local_id) {
    gst_printerr ("specify only --remote-id or --local-id\n");
    goto exit;
  }

  // Parse input pipeline
  if ((appctx->pipeline = create_pipeline (*appctx->args)) == NULL)
    goto exit;

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    goto exit;
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (appctx->mloop);
    goto exit;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), appctx->mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx->mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  connect_to_websocket_server_async (appctx);

  g_print ("g_main_loop_run\n");
  g_main_loop_run (appctx->mloop);
  g_print ("g_main_loop_run ends\n");

  g_source_remove (intrpt_watch_id);

  status = 0;

exit:
  gst_app_context_free (appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return status;
}
