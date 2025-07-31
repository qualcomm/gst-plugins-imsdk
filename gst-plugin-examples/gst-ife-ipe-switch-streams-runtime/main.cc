/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * GStreamer Application:
 * Dynamic Stream Switching Between IFE and IPE.
 *
 * Description:
 * This application demonstrates runtime switching of streams using GStreamer.
 * It supports two operational modes:
 *   - Buffering Mode:
 *       - Activates 1080p IFE stream for encoding
 *       - Activates 480p FD stream for encoding
 *   - Encoding Mode:
 *       - Activates 1080p IPE stream for encoding
 *       - Activates 480p IPE stream for display (Wayland)
 *
 * Features:
 *   - Safe pad linking/unlinking using pad probes
 *   - Dynamic stream reconfiguration
 *   - Interactive runtime mode switching via user input
 *
 * Usage:
 *   gst-ife-ipe-switch-streams-runtime-example
 *
 * **************************************************
 * Pipeline Overview:
 * - Stream 0 qtiqmmfsrc -> capsfilter_1 -> encoder -> mux -> filesink
 * - Stream 1 qtiqmmfsrc -> capsfilter_1 -> encoder -> mux -> filesink
 * - Stream 2: qtiqmmfsrc -> capsfilter_2 -> waylandsink
 * - Stream 3: qtiqmmfsrc -> capsfilter_3 -> encoder -> mux -> filesink
 *
 * Buffering Mode:
 *   - Stream-1, Stream-3 will be active
 * Encoding Mode:
 *   - Stream-0, Stream-2 will be active
 *
 * **************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <gst/gst.h>
#include <glib-unix.h>

#define MAX_STREAMS 4

/* Delay to accommodate initial buffer latency (~300ms)
 * when switching to a new stream
 */
#define STREAM_SWITCH_DELAY (300 * 1000) // 300 milliseconds


enum _StreamMode {
  MODE_NONE,
  MODE_BUFFERING,
  MODE_ENCODING
};

struct _GstStreamInf {
  GstPad *qmmf_pad;
  GstCaps *qmmf_caps;
  gint width;
  gint height;
};

struct _GstBufferingEncodingAppContext {
  _GstStreamInf *streams[MAX_STREAMS];
  _StreamMode current_mode;
  GstElement *pipeline;
  GMainLoop *mloop;
  GMutex lock;
  gboolean exit;
  GCond eos_signal;
};

struct _PadUnlinkData {
  GstPad *src_pad;
  GstPad *sink_pad;
  gboolean *completed;
  GCond *cond;
  GMutex *mutex;
};

struct _DeactivateStreamData {
  _GstBufferingEncodingAppContext *appctx;
  gint stream_index;
};

struct _DisplayToEncoderData {
  _GstBufferingEncodingAppContext *appctx;
  GstElement *qtiqmmfsrc;
};

struct _EncoderToDisplayData {
  _GstBufferingEncodingAppContext *appctx;
  GstElement *qtiqmmfsrc;
};

static _GstBufferingEncodingAppContext*
gst_app_context_new ()
{
  GST_DEBUG ("Creating application context");

  _GstBufferingEncodingAppContext *ctx =
      g_new0 (_GstBufferingEncodingAppContext, 1);
  if (!ctx) {
    g_printerr ("Failed to allocate memory for app context.\n");
    return NULL;
  }

  ctx->current_mode = MODE_NONE;
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->exit = FALSE;

  g_mutex_init (&ctx->lock);
  g_cond_init (&ctx->eos_signal);

  return ctx;
}

static void
gst_app_context_free (_GstBufferingEncodingAppContext * appctx)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  g_mutex_clear (&appctx->lock);
  g_cond_clear (&appctx->eos_signal);

  // Finally, free the application context itself
  if (appctx != NULL)
    g_free (appctx);
}

static gboolean
check_for_exit (_GstBufferingEncodingAppContext * appctx)
{
  g_mutex_lock (&appctx->lock);
  if (appctx->exit) {
    g_mutex_unlock (&appctx->lock);
    return TRUE;
  }
  g_mutex_unlock (&appctx->lock);
  return FALSE;
}

// In case of ASYNC state change it will properly wait for state change
static gboolean
wait_for_state_change (_GstBufferingEncodingAppContext * appctx)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Pipeline is PREROLLING ...\n");

  ret = gst_element_get_state (appctx->pipeline,
      NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline failed to PREROLL!\n");
    return FALSE;
  }
  return TRUE;
}

static void
state_change_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, newstate, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &newstate, &pending);
  GST_DEBUG ("'%s' state changed from %s to %s, pending: %s\n",
  GST_ELEMENT_NAME (pipeline), gst_element_state_get_name (old),
      gst_element_state_get_name (newstate), gst_element_state_get_name (pending));
}

static gboolean
wait_for_eos (_GstBufferingEncodingAppContext * appctx)
{
  g_mutex_lock (&appctx->lock);
  gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (2000000);
  gboolean timeout = g_cond_wait_until (&appctx->eos_signal,
      &appctx->lock, wait_time);

  if (!timeout) {
    g_print ("Timeout on wait for eos\n");
    g_mutex_unlock (&appctx->lock);
    return FALSE;
  }
  g_mutex_unlock (&appctx->lock);
  return TRUE;
}

gboolean
handle_interrupt_signal (gpointer userdata)
{
  _GstBufferingEncodingAppContext *appctx =
      (_GstBufferingEncodingAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    g_printerr ("ERROR: Failed to get current state!\n");
  }

  if (state == GST_STATE_PLAYING || state == GST_STATE_PAUSED) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());

    // Wait for EOS signal (with timeout)
    if (!wait_for_eos(appctx))
      g_printerr ("Timeout waiting for EOS. Forcing shutdown.\n");
    else
      g_print ("EOS received successfully.\n");
  }

  g_main_loop_quit (appctx->mloop);

  g_mutex_lock (&appctx->lock);
  appctx->exit = TRUE;
  g_mutex_unlock (&appctx->lock);

  return TRUE;
}

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

  g_main_loop_quit (mloop);
}

static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  _GstBufferingEncodingAppContext *appctx =
      (_GstBufferingEncodingAppContext *) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_mutex_lock (&appctx->lock);
  g_cond_signal (&appctx->eos_signal);
  g_mutex_unlock (&appctx->lock);

  if (check_for_exit (appctx))
    g_main_loop_quit (appctx->mloop);
}

static GstPadProbeReturn
block_and_unlink_cb (GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
  _PadUnlinkData *data = (_PadUnlinkData *)user_data;

  // Unlink pads
  if (gst_pad_is_linked (data->src_pad)) {
    gst_pad_unlink (data->src_pad, data->sink_pad);
    GST_DEBUG ("Pad unlinked safely.");
  }

  // Signal completion
  g_mutex_lock (data->mutex);
  *(data->completed) = TRUE;
  g_cond_signal (data->cond);
  g_mutex_unlock (data->mutex);

  return GST_PAD_PROBE_REMOVE;
}

static void
safe_unlink_pads (_GstBufferingEncodingAppContext *appctx,
    GstPad *src_pad, GstPad *sink_pad)
{
  GMutex mutex;
  GCond cond;
  GstState state;
  gboolean completed = FALSE;
  g_mutex_init (&mutex);
  g_cond_init (&cond);

  _PadUnlinkData *data = g_new0 (_PadUnlinkData, 1);
  data->src_pad = src_pad;
  data->sink_pad = sink_pad;
  data->completed = &completed;
  data->cond = &cond;
  data->mutex = &mutex;

  GST_DEBUG ("Adding pad probe to safely unlink pad");

  gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);

  if (state != GST_STATE_PLAYING)
    g_printerr ("Pipeline isn't in PLAYING state. Probe may not trigger\n");
  else
    gst_pad_add_probe (src_pad, GST_PAD_PROBE_TYPE_IDLE,
        block_and_unlink_cb, data, g_free);

  g_mutex_lock (&mutex);
  gint64 end_time = g_get_monotonic_time() + G_TIME_SPAN_SECOND * 5;
  while (!completed) {
    if (!g_cond_wait_until (&cond, &mutex, end_time)) {
      g_printerr ("Timeout while waiting for pad unlink to complete.\n");
      break;
    }
  }
  g_mutex_unlock (&mutex);

  g_mutex_clear (&mutex);
  g_cond_clear (&cond);
}

static gboolean
create_encoder_stream (_GstBufferingEncodingAppContext * appctx,
    _GstStreamInf * stream, GstElement *qtiqmmfsrc, gint stream_id)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", stream_id);
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf(temp_str, sizeof (temp_str), "encoder_%d", stream_id);
  GstElement *encoder = gst_element_factory_make ("qtic2venc", temp_str);

  snprintf (temp_str, sizeof (temp_str), "filesink_%d", stream_id);
  GstElement *filesink = gst_element_factory_make ("filesink", temp_str);

  snprintf (temp_str, sizeof (temp_str), "h264parse_%d", stream_id);
  GstElement *h264parse = gst_element_factory_make ("h264parse", temp_str);

  snprintf (temp_str, sizeof (temp_str), "mp4mux_%d", stream_id);
  GstElement *mp4mux = gst_element_factory_make ("mp4mux", temp_str);

  if (!capsfilter || !encoder || !filesink ||
      !h264parse || !mp4mux) {
    gst_object_unref (capsfilter);
    gst_object_unref (encoder);
    gst_object_unref (filesink);
    gst_object_unref (h264parse);
    gst_object_unref (mp4mux);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }

  // Set caps the the caps filter
  g_object_set (G_OBJECT (capsfilter), "caps", stream->qmmf_caps, NULL);

  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

  snprintf (temp_str, sizeof (temp_str), "/tmp/video_%d.mp4", stream_id);
  g_object_set (G_OBJECT (filesink), "location", temp_str, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      capsfilter, encoder, h264parse, mp4mux, filesink, NULL);

  // Sync the elements state to the current pipeline state
  gst_element_sync_state_with_parent (capsfilter);
  gst_element_sync_state_with_parent (encoder);
  gst_element_sync_state_with_parent (h264parse);
  gst_element_sync_state_with_parent (mp4mux);
  gst_element_sync_state_with_parent (filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (
      qtiqmmfsrc, gst_pad_get_name (stream->qmmf_pad),
      capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Link the elements
  if (!gst_element_link_many (capsfilter, encoder,
      h264parse, mp4mux, filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }
  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (capsfilter, GST_STATE_NULL);
  gst_element_set_state (encoder, GST_STATE_NULL);
  gst_element_set_state (h264parse, GST_STATE_NULL);
  gst_element_set_state (mp4mux, GST_STATE_NULL);
  gst_element_set_state (filesink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      capsfilter, encoder, h264parse, mp4mux, filesink, NULL);

  return FALSE;
}

static gboolean
create_dummy_stream (_GstBufferingEncodingAppContext * appctx,
    _GstStreamInf * stream, gint stream_id)
{
  gchar temp_str[100];

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", stream_id);
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "fakesink_%d", stream_id);
  GstElement *fakesink = gst_element_factory_make ("fakesink", temp_str);

  if (!capsfilter || !fakesink) {
    gst_object_unref (capsfilter);
    gst_object_unref (fakesink);
    g_printerr ("One element could not be created or found. Exiting.\n");
    return FALSE;
  }

  // Set caps the the caps filter
  g_object_set (G_OBJECT (capsfilter), "caps", stream->qmmf_caps, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      capsfilter, fakesink, NULL);

  // Sync the elements state to the current pipeline state
  gst_element_sync_state_with_parent (capsfilter);
  gst_element_sync_state_with_parent (fakesink);

  GstPad *sink_pad = gst_element_get_static_pad (capsfilter, "sink");
  if (gst_pad_link (stream->qmmf_pad, sink_pad) != GST_PAD_LINK_OK) {
    g_printerr ("Failed to link IFE stream pad to capsfilter_0 sink pad\n");
    gst_object_unref (sink_pad);
    goto cleanup;
  }
  gst_object_unref (sink_pad);

  // Link the elements
  if (!gst_element_link_many (capsfilter, fakesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }
  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (capsfilter, GST_STATE_NULL);
  gst_element_set_state (fakesink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      capsfilter, fakesink, NULL);

  return FALSE;
}

static gboolean
create_display_stream (_GstBufferingEncodingAppContext * appctx,
    _GstStreamInf * stream, GstElement *qtiqmmfsrc, gint stream_id)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", stream_id);
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "waylandsink_%d", stream_id);
  GstElement *waylandsink = gst_element_factory_make ("waylandsink", temp_str);

  // Check if all elements are created successfully
  if (!capsfilter || !waylandsink) {
    gst_object_unref (capsfilter);
    gst_object_unref (waylandsink);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }

  // Set caps the the caps filter
  g_object_set (G_OBJECT (capsfilter), "caps", stream->qmmf_caps, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "x", 0, NULL);
  g_object_set (G_OBJECT (waylandsink), "y", 0, NULL);
  g_object_set (G_OBJECT (waylandsink), "width", 640, NULL);
  g_object_set (G_OBJECT (waylandsink), "height", 480, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", TRUE, NULL);
  g_object_set (G_OBJECT (waylandsink), "enable-last-sample", FALSE,
      NULL);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      capsfilter, waylandsink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (capsfilter);
  gst_element_sync_state_with_parent (waylandsink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (
      qtiqmmfsrc, gst_pad_get_name (stream->qmmf_pad),
      capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Link the elements
  if (!gst_element_link_many (capsfilter, waylandsink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }
  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (capsfilter, GST_STATE_NULL);
  gst_element_set_state (waylandsink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      capsfilter, waylandsink, NULL);

  return FALSE;
}

static _GstStreamInf*
create_stream (GstElement *qtiqmmfsrc, gint w, gint h, gboolean display)
{
  _GstStreamInf *stream = g_new0 (_GstStreamInf, 1);
  stream->width  = w;
  stream->height = h;

  stream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, w,
      "height", G_TYPE_INT, h,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

  if (!display)
    gst_caps_set_features (stream->qmmf_caps, 0,
        gst_caps_features_new ("memory:GBM", NULL));

  GstElementClass *klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);
  GstPadTemplate *pad_template =
      gst_element_class_get_pad_template (klass, "video_%u");

  if (!pad_template) {
    g_printerr ("Pad template not found!\n");
    g_free (stream);
    return NULL;
  }

  stream->qmmf_pad =
      gst_element_request_pad (qtiqmmfsrc, pad_template, "video_%u", NULL);
  if (!stream->qmmf_pad) {
    g_printerr ("Failed to request pad from qtiqmmfsrc.\n");
    gst_caps_unref (stream->qmmf_caps);
    g_free (stream);
    return NULL;
  }

  return stream;
}

static gboolean
create_qmmf_streams (_GstBufferingEncodingAppContext *appctx)
{
  gboolean ret = FALSE;
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qtiqmmfsrc");

  _GstStreamInf *stream0 = create_stream (qtiqmmfsrc, 1920, 1080, FALSE);
  _GstStreamInf *stream1 = create_stream (qtiqmmfsrc, 1920, 1080, FALSE);
  _GstStreamInf *stream2 = create_stream (qtiqmmfsrc, 640, 480, TRUE);
  _GstStreamInf *stream3 = create_stream (qtiqmmfsrc, 640, 480, FALSE);

  if (!stream0 || !stream1 || !stream2 || !stream3) {
    g_printerr ("Failed to create streams.\n");
    goto cleanup;
  }

  appctx->streams[0] = stream0;
  appctx->streams[1] = stream1;
  appctx->streams[2] = stream2;
  appctx->streams[3] = stream3;

  ret = TRUE;

cleanup:
  if (!ret) {
    if (stream0) g_free (stream0);
    if (stream1) g_free (stream1);
    if (stream2) g_free (stream2);
    if (stream3) g_free (stream3);
  }
  gst_object_unref (qtiqmmfsrc);

  return ret;
}

static void
release_encoder_stream (_GstBufferingEncodingAppContext * appctx,
    _GstStreamInf * stream)
{
  GstState state = GST_STATE_VOID_PENDING;
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "qtiqmmfsrc");
  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "capsfilter_3");
  GstElement *encoder = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "encoder_3");
  GstElement *h264parse = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "h264parse_3");
  GstElement *mp4mux = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "mp4mux_3");
  GstElement *filesink = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "filesink_3");

  // Unlink the elements of this stream
  g_print ("Unlinking elements...\n");
  gst_element_unlink_many (qtiqmmfsrc, capsfilter, NULL);

  gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING)
    gst_element_send_event (encoder, gst_event_new_eos ());

  // Set NULL state to the unlinked elemets
  gst_element_set_state (capsfilter, GST_STATE_NULL);
  gst_element_set_state (encoder, GST_STATE_NULL);
  gst_element_set_state (h264parse, GST_STATE_NULL);
  gst_element_set_state (mp4mux, GST_STATE_NULL);
  gst_element_set_state (filesink, GST_STATE_NULL);

  // Unlink the elements of this stream
  gst_element_unlink_many (capsfilter, encoder,
      h264parse, mp4mux, filesink, NULL);
  g_print ("Unlinked successfully \n");

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      capsfilter, encoder, h264parse,
      mp4mux, filesink, NULL);

  gst_object_unref (qtiqmmfsrc);
  gst_object_unref (capsfilter);
  gst_object_unref (encoder);
  gst_object_unref (h264parse);
  gst_object_unref (mp4mux);
  gst_object_unref (filesink);

  capsfilter = NULL;
  encoder = NULL;
  h264parse = NULL;
  mp4mux = NULL;
  filesink = NULL;
}

static void
release_display_stream (_GstBufferingEncodingAppContext * appctx,
    _GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "qtiqmmfsrc");
  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "capsfilter_2");
  GstElement *waylandsink = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "waylandsink_2");

  // Unlink the elements of this stream
  g_print ("Unlinking elements...\n");
  gst_element_unlink_many (qtiqmmfsrc, capsfilter,
      waylandsink, NULL);
  g_print ("Unlinked successfully \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (capsfilter, GST_STATE_NULL);
  gst_element_set_state (waylandsink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      capsfilter, waylandsink, NULL);

  gst_object_unref (qtiqmmfsrc);
  gst_object_unref (capsfilter);
  gst_object_unref (waylandsink);

  capsfilter = NULL;
  waylandsink = NULL;
}

void
release_dummy_stream (_GstBufferingEncodingAppContext *appctx,
    _GstStreamInf *stream)
{
  if (!appctx || !stream)
    return;

  gchar name[64];

  // Unlink qmmf_pad from capsfilter_1 if linked
  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "capsfilter_0");
  if (capsfilter) {
    GstPad *sink_pad = gst_element_get_static_pad (capsfilter, "sink");
    if (gst_pad_is_linked (stream->qmmf_pad))
      gst_pad_unlink (stream->qmmf_pad, sink_pad);
    gst_object_unref (sink_pad);
  }

  // List of all possible element base names
  const gchar *element_bases[] = {
      "capsfilter", "fakesink"
  };

  // Remove and unref all elements associated with this stream
  for (size_t i = 0; i < G_N_ELEMENTS (element_bases); ++i) {
    snprintf (name, sizeof (name), "%s_%d", element_bases[i], 0);
    GstElement *elem = gst_bin_get_by_name (GST_BIN (appctx->pipeline), name);
    if (elem) {
      gst_element_set_state (elem, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (appctx->pipeline), elem);
      gst_object_unref (elem);
    }
  }
  gst_object_unref(capsfilter);
}

static void
release_stream (_GstBufferingEncodingAppContext *appctx, gint stream_id)
{
  if (!appctx || stream_id < 0 || stream_id >= MAX_STREAMS ||
      !appctx->streams[stream_id])
    return;

  _GstStreamInf *stream = appctx->streams[stream_id];
  gchar name[64];

  // List of all possible element base names
  const gchar *element_bases[] = {
      "capsfilter", "encoder", "filesink", "h264parse", "mp4mux", "fakesink",
      "waylandsink"
  };

  // Remove and unref all elements associated with this stream
  for (size_t i = 0; i < G_N_ELEMENTS (element_bases); ++i) {
    snprintf (name, sizeof (name), "%s_%d", element_bases[i], stream_id);
    GstElement *elem = gst_bin_get_by_name (GST_BIN (appctx->pipeline), name);
    if (elem) {
      gst_element_set_state (elem, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (appctx->pipeline), elem);
      gst_object_unref (elem);
    }
  }

  // Deactivate and release the pad
  if (stream->qmmf_pad) {
    GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
        "qtiqmmfsrc");
    if (qtiqmmfsrc) {
      gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
      gst_object_unref (qtiqmmfsrc);
    }
    gst_object_unref (stream->qmmf_pad);
    stream->qmmf_pad = NULL;
  }

  // Unref caps
  if (stream->qmmf_caps) {
    gst_caps_unref (stream->qmmf_caps);
    stream->qmmf_caps = NULL;
  }

  g_free (stream);
  appctx->streams[stream_id] = NULL;
}

static void
release_all_streams (_GstBufferingEncodingAppContext *appctx)
{
  for (int i = 0; i < MAX_STREAMS; ++i) {
    if (appctx->streams[i] != NULL)
      release_stream (appctx, i);
  }
}

static gboolean
handle_ife_stream (_GstBufferingEncodingAppContext *appctx, gboolean link,
    gboolean use_probe)
{
  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "capsfilter_1");
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "qtiqmmfsrc");
  GstPad *sink_pad = gst_element_get_static_pad (capsfilter, "sink");
  _GstStreamInf *stream = appctx->streams[1];

  if (link && !gst_pad_is_linked (stream->qmmf_pad)) {
    if (gst_pad_link (stream->qmmf_pad, sink_pad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link IFE stream pad to capsfilter_1 sink pad\n");
      gst_object_unref (sink_pad);
      gst_object_unref (capsfilter);
      gst_object_unref (qtiqmmfsrc);
      return FALSE;
    }

    GST_DEBUG ("Successfully linked pad %s to capsfilter_1.\n",
        gst_pad_get_name (stream->qmmf_pad));

  } else if (link && gst_pad_is_linked (stream->qmmf_pad)) {
    GST_DEBUG ("Pad %s is already linked. Skipping re-link.\n",
        gst_pad_get_name (stream->qmmf_pad));

  } else if (!link && gst_pad_is_linked (stream->qmmf_pad)) {
    if (use_probe)
      safe_unlink_pads (appctx, stream->qmmf_pad, sink_pad);
    else
      gst_pad_unlink (stream->qmmf_pad, sink_pad);
  }

  gst_object_unref (sink_pad);
  gst_object_unref (capsfilter);
  gst_object_unref (qtiqmmfsrc);
  return TRUE;
}

static gboolean
handle_ipe_streams (_GstBufferingEncodingAppContext *appctx, gboolean link,
  gboolean use_probe)
{
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "qtiqmmfsrc");

  gchar capsfilter_name[32];
  snprintf (capsfilter_name, sizeof (capsfilter_name), "capsfilter_1");

  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      capsfilter_name);
  GstPad *sink_pad = gst_element_get_static_pad (capsfilter, "sink");

  _GstStreamInf *stream = appctx->streams[0];

  if (link && !gst_pad_is_linked (stream->qmmf_pad)) {
    if (gst_pad_link (stream->qmmf_pad, sink_pad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link IPE stream[%d] pad to %s sink pad\n",
          0, capsfilter_name);
      gst_object_unref (sink_pad);
      gst_object_unref (capsfilter);
      gst_object_unref (qtiqmmfsrc);
      return FALSE;
    }

    GST_DEBUG ("Successfully linked pad %s to %s",
        gst_pad_get_name (stream->qmmf_pad), capsfilter_name);

  } else if (link && gst_pad_is_linked (stream->qmmf_pad)) {
    GST_DEBUG ("Pad %s is already linked. Skipping re-link",
        gst_pad_get_name (stream->qmmf_pad));

  } else if (!link && gst_pad_is_linked (stream->qmmf_pad)) {
    if (use_probe)
      safe_unlink_pads (appctx, stream->qmmf_pad, sink_pad);
    else
      gst_pad_unlink (stream->qmmf_pad, sink_pad);
  }

  gst_object_unref (sink_pad);
  gst_object_unref (capsfilter);
  gst_object_unref (qtiqmmfsrc);
  return TRUE;
}

static
gpointer display_to_encoder_thread (gpointer data)
{
  _DisplayToEncoderData *dtedata = (_DisplayToEncoderData *)data;
  _GstBufferingEncodingAppContext *appctx = dtedata->appctx;
  GstElement *qtiqmmfsrc = dtedata->qtiqmmfsrc;

  gst_pad_set_active (appctx->streams[2]->qmmf_pad, FALSE);
  release_display_stream (appctx, appctx->streams[2]);

  if (!gst_pad_is_linked (appctx->streams[3]->qmmf_pad)) {
    gst_pad_set_active (appctx->streams[3]->qmmf_pad, TRUE);
    create_encoder_stream (appctx, appctx->streams[3], qtiqmmfsrc, 3);
  }

  gst_object_unref (qtiqmmfsrc);
  g_free (dtedata);
  return NULL;
}

static
gpointer encoder_to_display_thread (gpointer data)
{
  _EncoderToDisplayData *etd_data = (_EncoderToDisplayData *)data;
  _GstBufferingEncodingAppContext *appctx = etd_data->appctx;
  GstElement *qtiqmmfsrc = etd_data->qtiqmmfsrc;

  gst_pad_set_active (appctx->streams[3]->qmmf_pad, FALSE);
  release_encoder_stream (appctx, appctx->streams[3]);

  if (!gst_pad_is_linked (appctx->streams[2]->qmmf_pad)) {
    gst_pad_set_active (appctx->streams[2]->qmmf_pad, TRUE);
    create_display_stream (appctx, appctx->streams[2], qtiqmmfsrc, 2);
  }

  gst_object_unref (qtiqmmfsrc);
  g_free (etd_data);
  return NULL;
}

static
gpointer deactivate_stream_thread (gpointer data)
{
  _DeactivateStreamData *deact_data = (_DeactivateStreamData *)data;
  _GstBufferingEncodingAppContext *appctx = deact_data->appctx;
  gint index = deact_data->stream_index;

  g_usleep (STREAM_SWITCH_DELAY);

  gst_pad_set_active (appctx->streams[index]->qmmf_pad, FALSE);

  g_free (deact_data);
  return NULL;
}

static void
switch_to_stream (_GstBufferingEncodingAppContext *appctx, _StreamMode mode,
    gboolean use_probe)
{
  gboolean success = FALSE;

  if ((mode == MODE_BUFFERING && appctx->current_mode == MODE_BUFFERING) ||
      (mode == MODE_ENCODING && appctx->current_mode == MODE_ENCODING)) {
    g_print ("Requested Mode is already active. No switch needed.\n");
    return;
  }

  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qtiqmmfsrc");

  if (mode == MODE_BUFFERING) {

    gst_pad_set_active (appctx->streams[1]->qmmf_pad, TRUE);

    _DisplayToEncoderData *dtedata = g_new(_DisplayToEncoderData, 1);
    dtedata->appctx = appctx;
    dtedata->qtiqmmfsrc = GST_ELEMENT(gst_object_ref(qtiqmmfsrc));
    (void)g_thread_new ("display-to-encoder", display_to_encoder_thread, dtedata);

    _DeactivateStreamData *data0 = g_new(_DeactivateStreamData, 1);
    data0->appctx = appctx;
    data0->stream_index = 0;
    (void)g_thread_new ("deactivate-stream0", deactivate_stream_thread, data0);

    g_usleep (STREAM_SWITCH_DELAY);

    success = handle_ipe_streams (appctx, FALSE, use_probe);
    GST_DEBUG ("unlinked IPE stream");
    if (!success) {
      g_printerr ("Failed to unlink IPE streams.\n");
      gst_object_unref (qtiqmmfsrc);
      return;
    }

    success = handle_ife_stream (appctx, TRUE, use_probe);
    GST_DEBUG ("linked IFE stream");
    if (!success) {
      g_printerr ("Failed to link IFE streams.\n");
      gst_object_unref (qtiqmmfsrc);
      return;
    }

    g_print ("Switched to Buffering Mode \n");

  } else {

    gst_pad_set_active (appctx->streams[0]->qmmf_pad, TRUE);

    _EncoderToDisplayData *etd_data = g_new(_EncoderToDisplayData, 1);
    etd_data->appctx = appctx;
    etd_data->qtiqmmfsrc = GST_ELEMENT (gst_object_ref(qtiqmmfsrc));
    (void)g_thread_new ("encoder-to-display", encoder_to_display_thread, etd_data);

    _DeactivateStreamData *data1 = g_new (_DeactivateStreamData, 1);
    data1->appctx = appctx;
    data1->stream_index = 1;
    (void)g_thread_new ("deactivate-stream1", deactivate_stream_thread, data1);

    g_usleep (STREAM_SWITCH_DELAY);

    success = handle_ife_stream (appctx, FALSE, use_probe);
    GST_DEBUG ("unlinked IFE stream");
    if (!success) {
      g_printerr ("Failed to unlink IFE streams.\n");
      gst_object_unref (qtiqmmfsrc);
      return;
    }

    success = handle_ipe_streams (appctx, TRUE, use_probe);
    GST_DEBUG ("linked IPE stream");
    if (!success) {
      g_printerr ("Failed to link IPE streams.\n");
      gst_object_unref (qtiqmmfsrc);
      return;
    }

    g_print("Switched to Encoding Mode \n");
  }

  appctx->current_mode = mode;
  gst_object_unref (qtiqmmfsrc);
  return;
}

static void* user_input_thread (gpointer user_data)
{
  _GstBufferingEncodingAppContext *appctx =
      (_GstBufferingEncodingAppContext *) user_data;
  gchar input[100];

  while (TRUE) {
    g_print ("=============================================================\n");
    g_print ("\nSelect an option:\n");
    g_print ("1. Buffering Mode\n");
    g_print ("2. Encoding Mode\n");
    g_print ("3. Quit\n");
    g_print ("=============================================================\n");
    g_print ("Enter your choice: ");

    if (!fgets (input, sizeof (input), stdin)) {
      g_printerr ("Error reading input.\n");
      continue;
    }

    int choice = atoi (input);
    switch (choice) {
      case 1:
        switch_to_stream (appctx, MODE_BUFFERING, TRUE);
        break;
      case 2:
        switch_to_stream (appctx, MODE_ENCODING, TRUE);
        break;
      case 3:
        g_print ("Exiting application...\n");
        gst_element_send_event (appctx->pipeline, gst_event_new_eos());
        wait_for_eos (appctx);
        g_main_loop_quit (appctx->mloop);
        return NULL;
      default:
        g_print ("Invalid choice. Please try again.\n");
        break;
    }
  }
  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  _GstBufferingEncodingAppContext *appctx = NULL;
  GMainLoop *mloop = NULL;
  GstElement *qtiqmmfsrc = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gboolean ret = -1;
  gboolean success = FALSE;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  appctx = gst_app_context_new ();
  if (!appctx) return -1;

  gst_init (&argc, &argv);

  appctx->pipeline = gst_pipeline_new ("qmmf-pipeline");
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  if (!appctx->pipeline || !qtiqmmfsrc) {
    g_printerr ("Failed to create pipeline or qtiqmmfsrc.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  gst_bin_add (GST_BIN (appctx->pipeline), qtiqmmfsrc);

  success = create_qmmf_streams (appctx);
  if (!success) {
    g_printerr ("Failed to create QMMF streams.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  success = create_dummy_stream (appctx, appctx->streams[0], 0);
  if (!success) {
    g_printerr ("Failed to create dummy stream 0.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  success = create_encoder_stream (appctx, appctx->streams[1], qtiqmmfsrc, 1);
  if (!success) {
    g_printerr ("Failed to create encoder stream 1.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  success = create_display_stream (appctx, appctx->streams[2], qtiqmmfsrc, 2);
  if (!success) {
    g_printerr ("Failed to create display stream 2.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  success = create_encoder_stream (appctx, appctx->streams[3], qtiqmmfsrc, 3);
  if (!success) {
    g_printerr ("Failed to create encoder stream 3.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    gst_bin_remove (GST_BIN (appctx->pipeline), qtiqmmfsrc);
    gst_app_context_free (appctx);
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return ret;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    gst_bin_remove (GST_BIN (appctx->pipeline), qtiqmmfsrc);
    gst_app_context_free (appctx);
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    return ret;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_change_cb), appctx->pipeline);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    if (!wait_for_state_change (appctx)) {
      gst_app_context_free (appctx);
      return -1;
    }
  }

  release_dummy_stream (appctx, appctx->streams[0]);
  switch_to_stream (appctx, MODE_BUFFERING, FALSE);

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    if (!wait_for_state_change (appctx)) {
      gst_app_context_free (appctx);
      return -1;
    }
  }

  g_print ("pipeline in PLAYING state\n");

  pthread_t thread;
  pthread_create (&thread, NULL, user_input_thread, appctx);
  pthread_detach (thread);

  g_main_loop_run (appctx->mloop);

  g_print ("Shutting down...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Unlink all stream if any
  release_all_streams (appctx);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  gst_deinit ();

  return 0;
}
