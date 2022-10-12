/*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <stdio.h>
#include <glib-unix.h>
#include <pthread.h>
#include <gst/gst.h>

#define RTSP_OUTPUT 0
#define DISPLAY_OUTPUT 1
#define DEFAULT_OUTPUT_WIDTH 1920
#define DEFAULT_OUTPUT_HEIGHT 1080
#define DEFAULT_ITERATIONS 6
#define SLEEP_DURATION 4
#define HOST "127.0.0.1"
#define PORT 8554

#define MAX_BITRATE_CTRL_METHOD 0x7F000001

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of pipeline plugins
  GList *plugins;
  // Pointer to the mainloop
  GMainLoop *mloop;
  // sHDR state
  gboolean shdr;
  // Iterations
  gint iterations;
};

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  guint idx = 0;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr (
          "\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
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
  GMainLoop *mloop = (GMainLoop*) userdata;
  static guint eoscnt = 0;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

static void
cam_timestamp_signal (GstElement * element, gint64 timestamp, gpointer userdata)
{
  static gint64 last_timestamp = timestamp;

  if (((timestamp - last_timestamp) / 1000000.0) > 200.0) {
    g_print ("Gap in video - %.3f ms\n",
        (timestamp - last_timestamp) / 1000000.0);
  }
  last_timestamp = timestamp;
}

static gboolean
wait_for_state_change (GstAppContext * appctx)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  ret = gst_element_get_state (appctx->pipeline,
      NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline failed to PREROLL!\n");
    return FALSE;
  }
  return TRUE;
}

void test_shdr_by_pipe_restart (GstAppContext *appctx)
{
  GstElement *qtiqmmfsrc, *h264enc;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (
      GST_BIN (appctx->pipeline), "qmmf");

  h264enc = gst_bin_get_by_name (
      GST_BIN (appctx->pipeline), "h264enc");

  // Send EOS to the encoder in order to flush it's buffers
  if (h264enc) {
    gst_element_send_event (h264enc, gst_event_new_eos ());
    gst_object_unref (h264enc);
  }

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL)) {
    wait_for_state_change (appctx);
  }

  appctx->shdr = !appctx->shdr;
  g_print ("%s sHDR by restart. \n", appctx->shdr ? "Enable" : "Disable");
  g_object_set (G_OBJECT (qtiqmmfsrc), "shdr", appctx->shdr, NULL);

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }

  gst_object_unref (qtiqmmfsrc);
}

void test_shdr_by_option (GstAppContext *appctx)
{
  GstElement *qtiqmmfsrc;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (
      GST_BIN (appctx->pipeline), "qmmf");

  appctx->shdr = !appctx->shdr;
  g_print ("%s sHDR by configuration. \n", appctx->shdr ? "Enable" : "Disable");
  g_object_set (G_OBJECT (qtiqmmfsrc), "shdr", appctx->shdr, NULL);

  gst_object_unref (qtiqmmfsrc);
}

static void *
thread_fn (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;

  for (gint i = 0; i < appctx->iterations; i++) {
    sleep (SLEEP_DURATION);
    test_shdr_by_pipe_restart (appctx);
  }

  for (gint i = 0; i < appctx->iterations; i++) {
    sleep (SLEEP_DURATION);
    test_shdr_by_option (appctx);
  }

  sleep (SLEEP_DURATION);
  gst_element_send_event (appctx->pipeline, gst_event_new_eos ());

  return NULL;
}

static void
destroy_pipe (GstAppContext *appctx)
{
  GstElement * element_1 = (GstElement *) appctx->plugins->data;
  GstElement * element_2;

  GList *list = appctx->plugins->next;
  for ( ; list != NULL; list = list->next) {
    element_2 = (GstElement *) list->data;
    gst_element_unlink (element_1, element_2);
    gst_bin_remove (GST_BIN (appctx->pipeline), element_1);
    element_1 = element_2;
  }
  gst_bin_remove (GST_BIN (appctx->pipeline), element_1);

  g_list_free (appctx->plugins);
  appctx->plugins = NULL;
  gst_object_unref (appctx->pipeline);
}

static gboolean
create_display_pipe (GstAppContext *appctx, gint width, gint height)
{
  GstElement *qtiqmmfsrc, *main_capsfilter, *sink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;

  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  main_capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  sink = gst_element_factory_make ("waylandsink", "waylandsink");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !main_capsfilter || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, sink);

  g_object_set (G_OBJECT (sink), "fullscreen", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "async", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (sink), "enable-last-sample", FALSE, NULL);

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Connect a callback to the timestamp signal.
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera-timestamp-sig", TRUE, NULL);
  g_signal_connect (qtiqmmfsrc, "camera-timestamp",
    G_CALLBACK (cam_timestamp_signal), NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtiqmmfsrc, main_capsfilter, sink, NULL);

  g_print ("Linking elements...\n");

  // Linking the stream
  ret = gst_element_link_many (
      qtiqmmfsrc, main_capsfilter, sink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        qtiqmmfsrc, main_capsfilter, sink, NULL);
    return FALSE;
  }
  g_print ("All elements are linked successfully\n");

  return TRUE;
}

static gboolean
create_rtsp_pipe (GstAppContext *appctx, gint width, gint height)
{
  GstElement *qtiqmmfsrc, *main_capsfilter, *queue1, *encoder;
  GstElement *queue2, *h264parse, *rtph264pay, *sink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;

  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  main_capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  queue1 = gst_element_factory_make ("queue", "queue1");
#ifdef CODEC2_ENCODE
  encoder = gst_element_factory_make ("qtic2venc", "h264enc");
#else
  encoder = gst_element_factory_make ("omxh264enc", "h264enc");
#endif
  queue2 = gst_element_factory_make ("queue", "queue2");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  rtph264pay = gst_element_factory_make ("rtph264pay", "rtph264pay");
  sink = gst_element_factory_make ("udpsink", "udpsink");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !main_capsfilter || !queue1 || !encoder ||
      !queue2 || !h264parse || !rtph264pay || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, queue1);
  appctx->plugins = g_list_append (appctx->plugins, encoder);
  appctx->plugins = g_list_append (appctx->plugins, queue2);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, rtph264pay);
  appctx->plugins = g_list_append (appctx->plugins, sink);

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);
#ifndef CODEC2_ENCODE
  // Set omxh264enc properties
  g_object_set (G_OBJECT (encoder), "control-rate", MAX_BITRATE_CTRL_METHOD, NULL);
  g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
  g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
#endif

  // Set h264parse properties
  g_object_set (G_OBJECT (h264parse), "config-interval", -1, NULL);

  // Set rtph264pay properties
  g_object_set (G_OBJECT (rtph264pay), "pt", 96, NULL);

  // Set udpsink properties
  g_object_set (G_OBJECT (sink), "host", HOST, NULL);
  g_object_set (G_OBJECT (sink), "port", PORT, NULL);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Connect a callback to the timestamp signal.
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera-timestamp-sig", TRUE, NULL);
  g_signal_connect (qtiqmmfsrc, "camera-timestamp",
    G_CALLBACK (cam_timestamp_signal), NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtiqmmfsrc, main_capsfilter, queue1, encoder, queue2,
      h264parse, rtph264pay, sink, NULL);

  g_print ("Linking elements...\n");

  // Linking the stream
  ret = gst_element_link_many (
      qtiqmmfsrc, main_capsfilter, queue1, encoder, queue2,
      h264parse, rtph264pay, sink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        qtiqmmfsrc, main_capsfilter, queue1, encoder, queue2,
        h264parse, rtph264pay, sink, NULL);
    return FALSE;
  }
  g_print ("All elements are linked successfully\n");

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstAppContext appctx = {};
  gint output = RTSP_OUTPUT;
  gint width = DEFAULT_OUTPUT_WIDTH;
  gint height = DEFAULT_OUTPUT_HEIGHT;
  gint iterations = DEFAULT_ITERATIONS;

  GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_INT,
      &output,
      "Output",
      "0 - rtsp, 1 - wayland"
    },
    { "width", 'w', DEFAULT_OUTPUT_WIDTH, G_OPTION_ARG_INT,
      &width,
      "width",
      "image width"
    },
    { "height", 'h', DEFAULT_OUTPUT_HEIGHT, G_OPTION_ARG_INT,
      &height,
      "height",
      "image height"
    },
    { "iterations", 'i', DEFAULT_ITERATIONS, G_OPTION_ARG_INT,
      &iterations,
      "iterations",
      "use-case iterations"
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }


  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("gst-test-app");
  if (!pipeline) {
    g_printerr ("failed to create pipeline.\n");
    return -1;
  }

  appctx.pipeline = pipeline;
  appctx.iterations = iterations;

  if (output == DISPLAY_OUTPUT) {
    ret = create_display_pipe (&appctx, width, height);
  } else {
    ret = create_rtsp_pipe (&appctx, width, height);
  }
  if (!ret) {
    g_printerr ("failed to create GST pipe.\n");
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    destroy_pipe (&appctx);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    destroy_pipe (&appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, &appctx);
  // todo: thread join

  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destory pipeline\n");
  destroy_pipe (&appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
