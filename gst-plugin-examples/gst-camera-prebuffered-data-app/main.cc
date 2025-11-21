/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
 * GStreamer Application:
 * GStreamer Application for Demonstrating Pre-Buffering and Live Recording
 *
 * Description:
 * This application demonstrates a use case where video frames are pre-buffered
 * before recording starts, ensuring that the final video includes content from
 * a few seconds before the recording trigger.
 *
 * Features:
 *   -- Pre-buffer frames from camera using appsink
 *   -- Push pre-buffered frames to appsrc pipeline for encoding
 *   -- Smooth transition from pre-buffered content to live recording
 *
 * Usage:
 * gst-camera-prebuffered-data-app [OPTIONS]
 * Example:
 * gst-camera-prebuffered-data-app -c 0 -w 1920 -h 1080 -d 30 -r 30
 *
 * Options:
 *   --camera-id       Camera ID
 *   --width           Frame width
 *   --height          Frame height
 *   --delay           Delay before recording starts (seconds)
 *   --record-duration Duration of live recording (seconds)
 *   --queue-size      Max buffer queue size
 *   --buffer-mode     Buffering mode: 0 - Normal, 1 - RDI, 2 - IPE ByPass
 *
 * Help:
 * gst-camera-prebuffered-data-app --help
 *
 * *******************************************************************************
 * Pipeline for Pre-buffering and Recording:
 * Main Pipeline:
 *   qtiqmmfsrc -> capsfilter -> appsink (for prebuffering)
 *   qtiqmmfsrc -> capsfilter -> encoder -> h264parse -> mp4mux -> filesink (for live data)
 * Appsrc Pipeline:
 *   appsrc -> queue -> encoder -> h264parse -> mp4mux -> filesink
 * *******************************************************************************
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/base/gstdataqueue.h>
#include <pthread.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define MAX_QUEUE_SIZE           300
#define OUTPUT_WIDTH             1920
#define OUTPUT_HEIGHT            1080
#define DELAY_TO_START_RECORDING 30
#define RECORD_DURATION          30

#define CAMERA_SESSION_TAG "org.codeaurora.qcamera3.sessionParameters.DynamicTapOut"

typedef struct _GstAppContext GstAppContext;
typedef struct _GstStreamInf GstStreamInf;

typedef enum
{
  GST_TAPOUT_NORMAL,
  GST_TAPOUT_RDI,
  GST_TAPOUT_IPEBYPASS
} GstDynamicTapOut;

// Stream information
struct _GstStreamInf
{
  GstElement *capsfilter;
  GstElement *waylandsink;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *appsink;
  GstPad *qmmf_pad;
  GstCaps *qmmf_caps;
  gint width;
  gint height;
  gboolean dummy;
  gboolean is_encoder;
};

// Contains app context information
struct _GstAppContext
{
  // Pointer to the main pipeline
  GstElement *main_pipeline;

  // Pointer to the appsrc pipeline and components
  GstElement *appsrc_pipeline;
  GstElement *appsrc;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *queue;

  // Pointer to the mainloop
  GMainLoop *mloop;
  // Queue to store pre buffered data
  GQueue *buffers_queue;
  // Camera ID
  guint camera_id;
  // Height
  guint height;
  //Width
  guint width;
  // Wait for time before recording starts
  guint delay_to_start_recording;
  // Live record duration
  guint record_duration;
  // Max queue size
  guint queue_size;
  // Buffering mode
  GstDynamicTapOut mode;
  // List with all streams
  GList *streams_list;
  // Stream count
  gint stream_cnt;
  // Stream count
  GMutex lock;
  // Exit thread flag
  gboolean exit;
  // EOS signal
  GCond eos_signal;
  // First live frame PTS
  GstClockTime first_live_pts;
  // Switch to live stream
  gboolean switch_to_live;
  // Live PTS arrived signal
  GCond live_pts_signal;
  // Source ID
  guint process_src_id;
  // Encoder Name
  gchar *encoder_name;
  // Selected usecase
  void (*usecase_fn) (GstAppContext * appctx);
};

// Forward Declaration
void release_stream (GstAppContext * appctx, GstStreamInf * stream);

static gchar *
get_encoder_name ()
{
  if (gst_element_factory_find ("qtic2venc")) {
    g_print ("[INFO] Using qtic2venc encoder plugin\n");
    return "qtic2venc";
  } else if (gst_element_factory_find ("omxh264enc")) {
    g_print ("[INFO] Using omxh264enc encoder plugin\n");
    return "omxh264enc";
  } else {
    g_printerr ("[ERROR] No suitable encoder plugin found (qtic2venc or omxh264enc)\n");
    return NULL;
  }
}

static void
clear_buffers_queue (GstAppContext *appctx)
{
  if (!appctx || !appctx->buffers_queue)
    return;

  g_mutex_lock (&appctx->lock);

  while (!g_queue_is_empty (appctx->buffers_queue)) {
    GstBuffer *buffer = (GstBuffer *) g_queue_pop_head (appctx->buffers_queue);
    if (buffer)
      gst_buffer_unref (buffer);
  }

  g_mutex_unlock (&appctx->lock);

  g_print ("[INFO] Cleared buffer queue\n");
}

GstPadProbeReturn
live_frame_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    if (buffer && ctx->first_live_pts == GST_CLOCK_TIME_NONE) {
      ctx->first_live_pts = GST_BUFFER_PTS (buffer);
      g_cond_signal(&ctx->live_pts_signal);
      g_print ("[INFO] First live frame PTS: %" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (ctx->first_live_pts));
      return GST_PAD_PROBE_REMOVE;
    }
  }
  return GST_PAD_PROBE_OK;
}

GstFlowReturn
on_new_sample (GstAppSink *appsink, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  GstSample *sample = gst_app_sink_pull_sample (appsink);

  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&ctx->lock);

  if (g_queue_get_length (ctx->buffers_queue) >= ctx->queue_size) {
    GstBuffer *item = (GstBuffer *) g_queue_pop_head (ctx->buffers_queue);
    if (item)
      gst_buffer_unref (item);
  }

  if (!ctx->switch_to_live) {
    gst_buffer_ref (buffer);
    g_queue_push_tail (ctx->buffers_queue, buffer);
  }

  g_mutex_unlock (&ctx->lock);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static gboolean
check_for_exit (GstAppContext * appctx)
{
  g_mutex_lock (&appctx->lock);
  if (appctx->exit) {
    g_mutex_unlock (&appctx->lock);
    return TRUE;
  }
  g_mutex_unlock (&appctx->lock);
  return FALSE;
}

// Wait for end of streaming
static gboolean
wait_for_eos (GstAppContext * appctx)
{
  g_mutex_lock (&appctx->lock);
  gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (5000000);
  gboolean timeout = g_cond_wait_until (&appctx->eos_signal,
      &appctx->lock, wait_time);
  if (!timeout) {
    g_print ("[ERROR] Timeout on wait for eos\n");
    g_mutex_unlock (&appctx->lock);
    return FALSE;
  }
  g_mutex_unlock (&appctx->lock);
  return TRUE;
}

// Release all streams in the list
static void
release_all_streams (GstAppContext * appctx)
{
  GList *list = NULL;
  for (list = appctx->streams_list; list != NULL; list = list->next) {
    GstStreamInf *stream = (GstStreamInf *) list->data;
    release_stream (appctx, stream);
  }
}

// Handles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;

  g_print ("\n[INFO] Received interrupt signal . . .\n");

  g_mutex_lock (&appctx->lock);
  if (appctx->exit) {
    g_mutex_unlock (&appctx->lock);
    return TRUE;
  }
  appctx->exit = TRUE;
  g_mutex_unlock (&appctx->lock);

  if (appctx->main_pipeline)
    gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  if (appctx->appsrc_pipeline)
    gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);

  /* Clear any queued buffers */
  if (appctx->buffers_queue) {
    g_print ("[INFO] Clearing buffer queue\n");
    clear_buffers_queue (appctx);
  }

  /* Signal any waiting threads */
  g_print ("[INFO] Signaling EOS condition to waiting threads\n");
  g_cond_signal (&appctx->eos_signal);

  if (appctx->mloop && g_main_loop_is_running (appctx->mloop)) {
    g_print ("[INFO] Quitting main loop\n");
    g_main_loop_quit (appctx->mloop);
  }

  g_print ("[INFO] Interrupt handling complete\n");
  return TRUE;
}

// Handles state change transisions
static void
state_changed_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  /* Handle state changes only for the provided pipeline */
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  const gchar *pipeline_name = gst_object_get_name (GST_OBJECT (pipeline));

  g_print ("\n[INFO] Pipeline '%s' state changed from %s to %s, pending: %s\n",
      pipeline_name,
      gst_element_state_get_name (old),
      gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
}

// Handle warnings
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

// Handle errors
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

// Error callback function
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  g_print ("\n[INFO] Received End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_mutex_lock (&appctx->lock);
  g_cond_signal (&appctx->eos_signal);
  g_mutex_unlock (&appctx->lock);

  if (check_for_exit (appctx))
    g_main_loop_quit (appctx->mloop);

}

static gboolean
create_encoder_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  static guint output_cnt = 0;
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "encoder_%d", appctx->stream_cnt);
  stream->encoder = gst_element_factory_make(appctx->encoder_name, temp_str);

  snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("filesink", temp_str);

  snprintf (temp_str, sizeof (temp_str), "h264parse_%d", appctx->stream_cnt);
  stream->h264parse = gst_element_factory_make ("h264parse", temp_str);

  snprintf (temp_str, sizeof (temp_str), "mp4mux_%d", appctx->stream_cnt);
  stream->mp4mux = gst_element_factory_make ("mp4mux", temp_str);

  if (!stream->capsfilter || !stream->encoder || !stream->filesink ||
      !stream->h264parse || !stream->mp4mux) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->encoder)
      gst_object_unref (stream->encoder);
    if (stream->filesink)
      gst_object_unref (stream->filesink);
    if (stream->h264parse)
      gst_object_unref (stream->h264parse);
    if (stream->mp4mux)
      gst_object_unref (stream->mp4mux);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }
  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (stream->encoder), "target-bitrate", 6000000, NULL);
  if (g_strcmp0 (appctx->encoder_name, "qtic2venc") == 0)
    g_object_set (G_OBJECT (stream->encoder), "control-rate", 3, NULL); /* VBR-CFR */
  else {
    g_object_set (G_OBJECT (stream->encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (stream->encoder), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (stream->encoder), "control-rate", 2, NULL);
  }

  // Set mp4mux in robust mode
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-moov-update-period",
      1000000, NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-bytes-per-sec", 10000,
      NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-max-duration", 1000000000,
      NULL);

  snprintf (temp_str, sizeof (temp_str), "/data/video_live_data_%d.mp4",
      output_cnt++);
  g_object_set (G_OBJECT (stream->filesink), "location", temp_str, NULL);

  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  // Sync the elements state to the curtent main_pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->encoder);
  gst_element_sync_state_with_parent (stream->h264parse);
  gst_element_sync_state_with_parent (stream->mp4mux);
  gst_element_sync_state_with_parent (stream->filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }
  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->encoder,
          stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  return FALSE;
}

static void
release_encoder_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  GstState state = GST_STATE_VOID_PENDING;
  GstElement *qtiqmmfsrc = NULL;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("[INFO] Unlinking elements for encoder stream...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, NULL);

  gst_element_get_state (appctx->main_pipeline, &state, NULL,
      GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING)
    gst_element_send_event (stream->encoder, gst_event_new_eos ());

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_get_state (stream->encoder, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_get_state (stream->h264parse, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_get_state (stream->mp4mux, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);
  gst_element_get_state (stream->filesink, NULL, NULL, GST_CLOCK_TIME_NONE);

  // Unlink the elements of this stream
  gst_element_unlink_many (stream->capsfilter, stream->encoder,
      stream->h264parse, stream->mp4mux, stream->filesink, NULL);
  g_print ("[INFO] Unlinked successfully for encoder stream \n");

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->encoder = NULL;
  stream->h264parse = NULL;
  stream->mp4mux = NULL;
  stream->filesink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_appsink_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "appsink_%d", appctx->stream_cnt);
  stream->appsink = gst_element_factory_make ("appsink", temp_str);

  // Check if all elements are created successfully
  if (!stream->capsfilter || !stream->appsink) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->appsink)
      gst_object_unref (stream->appsink);
    g_printerr ("[ERROR] One element could not be created of found. Exiting.\n");
    return FALSE;
  }
  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);
  gst_app_sink_set_emit_signals (GST_APP_SINK (stream->appsink), TRUE);
  g_signal_connect (stream->appsink, "new-sample", G_CALLBACK (on_new_sample),
      appctx);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->appsink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Error: Link cannot be done!\n");
    goto cleanup;
  }
  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->appsink, NULL)) {
    g_printerr ("[ERROR] Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->appsink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);

  return FALSE;
}

static void
release_appsink_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name(GST_BIN(appctx->main_pipeline), "qmmf");

  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] qmmfsrc not found in pipeline\n");
    return;
  }

  g_print("[INFO] Unlinking elements for appsink stream...\n");
  gst_element_unlink_many(qtiqmmfsrc, stream->capsfilter, stream->appsink, NULL);
  g_print("[INFO] Unlinked successfully for appsink stream\n");

  // Lock state to prevent parent forcing PLAYING
  gst_element_set_locked_state(stream->capsfilter, TRUE);
  gst_element_set_locked_state(stream->appsink, TRUE);

  gst_element_set_state(stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state(stream->appsink, GST_STATE_NULL);
  gst_element_get_state (stream->appsink, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_bin_remove_many(GST_BIN(appctx->main_pipeline),
                      stream->capsfilter, stream->appsink, NULL);

  stream->capsfilter = NULL;
  stream->appsink = NULL;

  gst_object_unref(qtiqmmfsrc);
}

static gboolean
create_dummy_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("fakesink", temp_str);

  // Check if all elements are created successfully
  if (!stream->capsfilter || !stream->filesink) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->filesink)
      gst_object_unref (stream->filesink);
    g_printerr ("[ERROR] One element could not be created of found. Exiting.\n");
    return FALSE;
  }
  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Add the elements to the main_pipeline
  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  // Sync the elements state to the curtent main_pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }
  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  return FALSE;
}

static void
release_dummy_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("[INFO] Unlinking elements for dummy stream...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter,
      stream->filesink, NULL);
  g_print ("[INFO] Unlinked successfully for dummy stream \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);
  gst_element_get_state (stream->filesink, NULL, NULL, GST_CLOCK_TIME_NONE);

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->filesink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static void
link_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  gboolean ret = FALSE;

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return;
  }
  // Activation the pad
  gst_pad_set_active (stream->qmmf_pad, TRUE);
  g_print ("[INFO] Pad name - %s\n", gst_pad_get_name (stream->qmmf_pad));

  if (stream->is_encoder)
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  else
    ret = create_appsink_stream (appctx, stream, qtiqmmfsrc);
  if (!ret) {
    g_printerr ("[ERROR] failed to create steam\n");
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return;
}

static void
unlink_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Unlink all elements for that stream
  if (stream->dummy) {
    release_dummy_stream (appctx, stream);
    stream->dummy = FALSE;
  } else if (stream->is_encoder) {
    release_encoder_stream (appctx, stream);
  } else {
    release_appsink_stream (appctx, stream);
  }

  // Deactivation the pad
  gst_pad_set_active (stream->qmmf_pad, FALSE);

  g_print ("\n");
}

static gboolean
configure_metadata (GstAppContext *appctx)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return FALSE;
  }

  ::camera::CameraMetadata session_meta(128, 128);
  ::camera::CameraMetadata *static_meta = nullptr;
  uint32_t tag;

  /* Get static and session metadata from qtiqmmfsrc */
  g_object_get(G_OBJECT(qtiqmmfsrc),
               "static-metadata", &static_meta,
               NULL);

  if (!static_meta) {
    g_printerr("[WARN] Failed to retrieve metadata objects \n");
    gst_object_unref(qtiqmmfsrc);
    return FALSE;
  }

  /* Find the vendor tag for CAMERA_SESSION_TAG */
  gint ret = static_meta->getTagFromName(CAMERA_SESSION_TAG, NULL, &tag);
  if (ret != 0) {
    g_printerr("[WARN] Vendor tag not found \n");
    gst_object_unref(qtiqmmfsrc);
    return FALSE;
  }

  /* Update session metadata with mode value */
  int32_t mode_val = static_cast<int32_t>(appctx->mode);
  session_meta.update(tag, &mode_val, 1);

  /* Apply updated session metadata back to qtiqmmfsrc */
  g_object_set(G_OBJECT(qtiqmmfsrc), "session-metadata", &session_meta, NULL);
  g_print("[INFO] Session metadata updated successfully \n");
  gst_object_unref(qtiqmmfsrc);

  return TRUE;
}

static GstStreamInf *
create_stream (GstAppContext *appctx, gboolean dummy,
    gboolean encoder, gint w, gint h)
{
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);

  /* Get qtiqmmfsrc instance */
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return nullptr;
  }

  stream->dummy = dummy;
  stream->is_encoder = encoder;
  stream->width = w;
  stream->height = h;

  stream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, w,
      "height", G_TYPE_INT, h,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  gst_caps_set_features (stream->qmmf_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  /* Get qmmfsrc Element class */
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  /* Get qmmfsrc pad template */
  GstPadTemplate *qtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");

  /* Request a pad from qmmfsrc */
  stream->qmmf_pad = gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template,
      "video_%u", NULL);
  if (!stream->qmmf_pad) {
    g_printerr ("[ERROR] pad cannot be retrieved from qmmfsrc!\n");
    goto cleanup;
  }

  g_print ("[INFO] Pad received - %s\n", gst_pad_get_name (stream->qmmf_pad));

  if (dummy)
    ret = create_dummy_stream (appctx, stream, qtiqmmfsrc);
  else if (stream->is_encoder)
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  else {
    /* set extra buffer for camera stream to match queue size */
    g_object_set (G_OBJECT (stream->qmmf_pad), "extra-buffers",
        (guint) appctx->queue_size, NULL);
    ret = create_appsink_stream (appctx, stream, qtiqmmfsrc);
  }
  if (!ret) {
    g_printerr ("[ERROR] failed to create stream\n");
    goto cleanup;
  }

  /* Add the stream to the list */
  appctx->streams_list = g_list_append (appctx->streams_list, stream);
  appctx->stream_cnt++;

  gst_object_unref (qtiqmmfsrc);
  return stream;

cleanup:
  if (stream->qmmf_pad) {
    /* Release the unlinked pad */
    gst_pad_set_active (stream->qmmf_pad, FALSE);
    gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
  }

  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);
  g_free (stream);

  return NULL;
}

void
release_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Unlink all elements for that stream
  unlink_stream (appctx, stream);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element in release_stream\n");
    gst_caps_unref (stream->qmmf_caps);
    appctx->streams_list = g_list_remove (appctx->streams_list, stream);
    g_free (stream);
    return;
  }

  // Release the unlinked pad
  gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);

  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);

  // Remove the stream from the list
  appctx->streams_list = g_list_remove (appctx->streams_list, stream);

  g_free (stream);

  g_print ("\n");
}

// In case of ASYNC state change it will properly wait for state change
static gboolean
wait_for_state_change (GstElement *pipeline)
{
  g_return_val_if_fail (pipeline != NULL, FALSE);

  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  const gchar *pipeline_name = gst_object_get_name (GST_OBJECT (pipeline));

  g_print ("[INFO] Pipeline '%s' is PREROLLING ...\n", pipeline_name);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("[ERROR] Pipeline '%s' failed to PREROLL!\n", pipeline_name);
    return FALSE;
  }

  return TRUE;
}

/*
 * process_queued_buffers:
 * @appctx: (in): Application context containing appsrc pipeline and buffer queue.
 *
 * This function processes buffers queued for prebuffering and pushes them
 * into the `appsrc` element of the pipeline.
 */
static gboolean
process_queued_buffers (gpointer user_data)
{
  GstAppContext *appctx = NULL;
  GstElement *appsrc = NULL;
  GstAppSrc *src = NULL;
  GstBuffer *buffer = NULL;
  gboolean empty = FALSE;

  appctx = static_cast<GstAppContext *> (user_data);

  if (check_for_exit (appctx)) {
    g_print ("[INFO] Exit requested, stopping buffer processing\n");
    return FALSE;
  }

  appsrc = gst_bin_get_by_name (GST_BIN (appctx->appsrc_pipeline), "appsrc");
  if (!appsrc) {
    g_printerr ("[ERROR] Failed to retrieve appsrc element\n");
    return FALSE;
  }

  src = GST_APP_SRC (appsrc);

  /* Check if queue is empty */
  g_mutex_lock (&appctx->lock);
  empty = g_queue_is_empty (appctx->buffers_queue);
  g_mutex_unlock (&appctx->lock);

  if (empty) {
    gst_app_src_end_of_stream (src);
    g_print ("[INFO] Buffer queue empty, sending EOS and stopping\n");
    g_print ("[INFO] Procesing of queued buffers are done.\n");
    gst_object_unref (appsrc);
    return FALSE;
  }

  /* Pop buffer from queue under lock */
  g_mutex_lock (&appctx->lock);
  buffer = GST_BUFFER (g_queue_pop_head (appctx->buffers_queue));
  g_mutex_unlock (&appctx->lock);

  /* Validate PTS and push or discard */
  if (GST_CLOCK_TIME_IS_VALID (appctx->first_live_pts) &&
      GST_BUFFER_PTS (buffer) >= appctx->first_live_pts) {
    g_print ("[INFO] Discarding buffer after live PTS reached\n");
    gst_buffer_unref (buffer);
  } else {
    gst_app_src_push_buffer (src, buffer);
  }

  gst_object_unref (appsrc);

  return TRUE;
}

static gboolean
start_pushing_buffers (gpointer user_data)
{
  GstAppContext *appctx = static_cast<GstAppContext *> (user_data);

  g_print ("[INFO] Starting to push queued buffers to appsrc pipeline\n");
  appctx->process_src_id = g_timeout_add(16, process_queued_buffers, appctx);

  return FALSE;
}

/**
 * prebuffering_usecase:
 * @appctx: (in): Application context containing pipelines and stream info.
 *
 * Implements a pre-buffering use case for video recording with smooth
 * transition from prebuffered frames to live recording.
 *
 * Workflow:
 *   1. Create two streams:
 *        - Appsink stream for prebuffering.
 *        - Encoder stream for live recording.
 *   2. Add a pad probe on the live stream to capture the first live frame PTS.
 *   3. Transition the main pipeline to PAUSED for caps negotiation.
 *   4. Configure camera session metadata.
 *   5. Unlink the live stream, then set the main pipeline to PLAYING.
 *   6. Start the appsrc pipeline and wait for the configured delay.
 *   7. Link the live stream back and wait until the first live PTS is received
 *      (using a condition variable for efficient waiting).
 *   8. Switch to live mode and start pushing prebuffered frames to appsrc.
 *   9. Unlink the prebuffered appsink stream after switching to live.
 *   10. Record for the configured duration, then clear the buffer queue.
 *   11. Re-link the prebuffered stream, send EOS to flush data, and wait for EOS.
 *   12. Transition both pipelines to NULL state and release all resources.
 */
static void
prebuffering_usecase (GstAppContext *appctx)
{
  GstStreamInf *stream_inf_1;
  GstStreamInf *stream_inf_2;

  g_print ("[INFO] Creating appsink stream (%dx%d)\n",
      appctx->width, appctx->height);
  stream_inf_1 = create_stream (appctx, FALSE, FALSE, 1920, 1080);
  if (!stream_inf_1) {
    g_printerr ("Failed to create appsink stream\n");
    return;
  }

  g_print ("[INFO] Creating live encoder stream (%dx%d)\n",
      appctx->width, appctx->height);
  stream_inf_2 = create_stream (appctx, TRUE, TRUE, 1920, 1080);
  if (!stream_inf_2) {
    g_printerr ("Failed to create live stream\n");
    release_stream (appctx, stream_inf_1);
    return;
  }

  gst_pad_add_probe (stream_inf_2->qmmf_pad, GST_PAD_PROBE_TYPE_BUFFER,
      live_frame_probe, appctx, NULL);

  /* Transition main pipeline to PAUSED for caps negotiation */
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->main_pipeline, GST_STATE_PAUSED))
    wait_for_state_change (appctx->main_pipeline);

  if (!configure_metadata (appctx))
    g_printerr ("[WARN] Failed to configure camera session params \n");

  g_print ("[INFO] Unlinking live stream before switching pipeline "
      "to PLAYING\n");
  unlink_stream (appctx, stream_inf_2);

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->main_pipeline, GST_STATE_PLAYING))
    wait_for_state_change (appctx->main_pipeline);

  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_PLAYING);

  /* Wait before switching to live */
  g_print ("[INFO] Prebuffering is going on ...\n");
  g_print ("[INFO] Waiting %u seconds before switching to live recording...\n",
      appctx->delay_to_start_recording);

  sleep (appctx->delay_to_start_recording);

  g_print ("[INFO] Linking live stream back to pipeline\n");
  link_stream (appctx, stream_inf_2);

  g_mutex_lock (&appctx->lock);

  while (appctx->first_live_pts == GST_CLOCK_TIME_NONE && !appctx->exit)
    g_cond_wait (&appctx->live_pts_signal, &appctx->lock);

  g_mutex_unlock (&appctx->lock);

  appctx->switch_to_live = TRUE;

  /* Start pushing buffers */
  start_pushing_buffers (appctx);

  /* Unlink appsink stream (prebuffered) after switching to live */
  unlink_stream (appctx, stream_inf_1);

  /* Record for specified duration */
  g_print ("[INFO] Live recording started for %u seconds\n",
      appctx->record_duration);
  sleep (appctx->record_duration);

  clear_buffers_queue (appctx);

  link_stream (appctx, stream_inf_1);

  /* Send EOS to allow proper flushing */
  g_print ("[INFO] Sending EOS event to main pipeline\n");
  gst_element_send_event (appctx->main_pipeline, gst_event_new_eos ());

  /* Wait for EOS message on bus */
  wait_for_eos (appctx);

  /* Transition pipelines to NULL state */
  g_print ("[INFO] Transitioning main pipeline to NULL state\n");
  gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->main_pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);

  g_print ("[INFO] Transitioning appsrc pipeline to NULL state\n");
  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->appsrc_pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);

  /* Release streams and pads */
  release_stream (appctx, stream_inf_1);
  release_stream (appctx, stream_inf_2);

  g_print ("[INFO] Cleanup complete\n");
}

static void *
thread_fn (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;

  /* Execute the selected use case */
  appctx->usecase_fn (appctx);

  /* Quit the main loop only if we are not already exiting and the loop is running */
  if (!check_for_exit (appctx)
      && appctx->mloop
      && g_main_loop_is_running (appctx->mloop))
    g_main_loop_quit (appctx->mloop);

  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstCaps *filtercaps;
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL;
  GstElement *appsrc = NULL;
  GstElement *queue = NULL;
  GstElement *h264parse = NULL;
  GstElement *mp4mux = NULL;
  GstElement *encoder = NULL;
  GstElement *filesink = NULL;
  gboolean ret = FALSE;
  GstAppContext *appctx = g_new0 (GstAppContext, 1);
  g_mutex_init (&appctx->lock);
  g_cond_init (&appctx->eos_signal);
  g_cond_init (&appctx->live_pts_signal);
  appctx->stream_cnt = 0;
  appctx->camera_id = 0;
  appctx->height = OUTPUT_HEIGHT;
  appctx->width = OUTPUT_WIDTH;
  appctx->delay_to_start_recording = DELAY_TO_START_RECORDING;
  appctx->queue_size = MAX_QUEUE_SIZE;
  appctx->mode = GST_TAPOUT_NORMAL;
  appctx->usecase_fn = prebuffering_usecase;
  appctx->first_live_pts = GST_CLOCK_TIME_NONE;
  appctx->switch_to_live = FALSE;
  appctx->record_duration = RECORD_DURATION;

  GOptionEntry entries[] = {
    {
        "camera-id", 'c', 0, G_OPTION_ARG_INT, &appctx->camera_id,
        "Camera ID", "ID"},
    {
        "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height,
        "Frame height", "HEIGHT"},
    {
        "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
        "Frame width", "WIDTH"},
    {
        "delay", 'd', 0, G_OPTION_ARG_INT, &appctx->delay_to_start_recording,
        "Delay before recording starts (seconds)", "DELAY"},
    {
        "record-duration", 'r', 0, G_OPTION_ARG_INT, &appctx->record_duration,
        "Record duration after recording starts (seconds)", "DURATION"},
    {
        "queue-size", 'q', 0, G_OPTION_ARG_INT, &appctx->queue_size,
        "Max buffer queue size", "SIZE"},
    {
        "tap-out", 't', 0, G_OPTION_ARG_INT, &appctx->mode,
        "Tap out mode: 0 - Normal, 1 - RDI, 2 - IPE By Pass", "MODE"},
    {NULL}
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("Pre-Buffered data and recording ")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("[ERROR] Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("[ERROR] Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("[ERROR] Failed to create options context!\n");
    return -EFAULT;
  }

  if (appctx->mode != GST_TAPOUT_NORMAL
      && appctx->mode != GST_TAPOUT_RDI &&
      appctx->mode != GST_TAPOUT_IPEBYPASS) {
    g_printerr ("[ERROR] Invalid buffer mode: %d\n",appctx->mode);
    return -EFAULT;
  }

  g_print ("[INFO] Parsed Options:\n");
  g_print ("[INFO] Camera ID: %u\n", appctx->camera_id);
  g_print ("[INFO] Height: %u\n", appctx->height);
  g_print ("[INFO] Width: %u\n", appctx->width);
  g_print ("[INFO] Delay to Start Recording: %u seconds\n",
      appctx->delay_to_start_recording);
  g_print ("[INFO] Record Duration: %u seconds\n", appctx->record_duration);
  g_print ("[INFO] Queue Size: %u\n", appctx->queue_size);
  g_print ("[INFO] Tap out mode: %d\n",appctx->mode);

  // Initialize GST library.
  gst_init (&argc, &argv);

  appctx->encoder_name = get_encoder_name();
  if (!appctx->encoder_name)
    return -EFAULT;

  pipeline = gst_pipeline_new ("gst-main-pipeline");
  appctx->main_pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);

  // Set the Camera ID
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", appctx->camera_id, NULL);

  // Add qmmfsrc to the main_pipeline
  gst_bin_add (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);
    gst_object_unref (appctx->main_pipeline);
    g_printerr ("[ERROR] Failed to create Main loop!\n");
    return -1;
  }
  appctx->mloop = mloop;

  pipeline = gst_pipeline_new ("gst-appsrc-pipeline");
  appsrc = gst_element_factory_make ("appsrc", "appsrc");
  queue =  gst_element_factory_make ("queue", "queue");
  encoder = gst_element_factory_make(appctx->encoder_name, "encoder");
  filesink = gst_element_factory_make ("filesink", "filesink");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

  // Check if all elements are created successfully
  if (!pipeline || !appsrc || !queue || !encoder || !filesink || !h264parse || !mp4mux) {
    g_printerr ("[ERROR] One element could not be created of found. Exiting.\n");
    return -1;
  }
  // Set properties
  g_object_set (G_OBJECT (h264parse), "name", "h264parse", NULL);
  g_object_set (G_OBJECT (mp4mux), "name", "mp4mux", NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "name", "encoder", NULL);
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

  if (g_strcmp0(appctx->encoder_name, "qtic2venc") == 0)
    g_object_set (G_OBJECT (encoder), "control-rate", 3, NULL);   // VBR-CFR
  else {
    g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
  }

  g_object_set (G_OBJECT (filesink), "name", "filesink", NULL);
  g_object_set (G_OBJECT (filesink), "location", "/data/video_prebuffered_data.mp4",
      NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);

  // Set appsrc properties
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (appsrc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (appsrc), "stream-type", 0,    // GST_APP_STREAM_TYPE_STREAM
      "format", GST_FORMAT_TIME, "is-live", TRUE, NULL);

  // Assign elements to context
  appctx->appsrc_pipeline = pipeline;
  appctx->appsrc = appsrc;
  appctx->queue = queue;
  appctx->h264parse = h264parse;
  appctx->mp4mux = mp4mux;
  appctx->encoder = encoder;
  appctx->filesink = filesink;

  // Add elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->appsrc_pipeline),
      appsrc, queue, encoder, h264parse, mp4mux, filesink, NULL);

  if (!gst_element_link_many (appsrc, queue, encoder,
          h264parse, mp4mux, filesink, NULL)) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    return -1;
  }
  // Retrieve reference to the main_pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
       GST_PIPELINE (appctx->main_pipeline))) == NULL) {
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);
    gst_object_unref (appctx->main_pipeline);
    g_printerr ("[ERROR] Failed to retrieve main_pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  // Watch for messages on the main_pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->main_pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx);
  gst_object_unref (bus);

  // Retrieve reference to the main_pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
       GST_PIPELINE (appctx->appsrc_pipeline))) == NULL) {
    gst_object_unref (appctx->appsrc_pipeline);
    g_printerr ("[ERROR] Failed to retrieve appsrc_pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->appsrc_pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  appctx->buffers_queue = g_queue_new ();

  // Run thread which perform link and unlink of streams
  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, appctx);

  // Run main loop.
  g_print ("[INFO] g_main_loop_run\n");
  g_main_loop_run (mloop);

  if (appctx->process_src_id) {
    GSource *src = g_main_context_find_source_by_id(NULL, appctx->process_src_id);
    if (src && !g_source_is_destroyed(src)) {
      g_source_remove(appctx->process_src_id);
      g_print("[INFO] Removed buffer pushing source\n");
    }
    appctx->process_src_id = 0;
  }

  pthread_join(thread, NULL);
  g_print ("[INFO] g_main_loop_run ends\n");

  g_print ("[INFO] Setting main_pipeline to NULL state ...\n");
  if (appctx->main_pipeline)
    gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);

  if (appctx->appsrc_pipeline)
    gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);

  /* Release any remaining streams */
  if (appctx->streams_list != NULL)
    release_all_streams (appctx);

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  // Remove qmmfsrc from the main_pipeline
  if (appctx->main_pipeline && qtiqmmfsrc)
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);

  // Free the streams list
  if (appctx->streams_list != NULL) {
    g_list_free (appctx->streams_list);
    appctx->streams_list = NULL;
  }

 // Clear buffer queue
  if (appctx->buffers_queue) {
    clear_buffers_queue (appctx);
    g_queue_free (appctx->buffers_queue);
  }

  g_mutex_clear (&appctx->lock);
  g_cond_clear (&appctx->eos_signal);
  g_cond_clear(&appctx->live_pts_signal);

  // Cleanup pipelines
  if (appctx->appsrc_pipeline)
    gst_object_unref (appctx->appsrc_pipeline);

  if (appctx->main_pipeline)
    gst_object_unref (appctx->main_pipeline);

  g_free (appctx);

  gst_deinit ();

  g_print ("[INFO] main: Exit\n");

  return 0;
}
