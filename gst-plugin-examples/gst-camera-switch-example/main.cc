/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

/*
* Application:
* GStreamer Switch cameras in Playing state
*
* Description:
* This application uses the two cameras of the device and switch them
* without changing the state of the pipeline. The switching is done in
* Playing state every 5 seconds.
*
* Usage:
* gst-camera-switch-example
*
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <pthread.h>

#define OUTPUT_WIDTH 1280
#define OUTPUT_HEIGHT 720

#define USE_DISPLAY

typedef struct _GstCameraSwitchCtx GstCameraSwitchCtx;

// Contains app context information
struct _GstCameraSwitchCtx
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;

  GstElement *qtiqmmfsrc_0;
  GstElement *qtiqmmfsrc_1;
  GstElement *capsfilter;
  GstElement *waylandsink;

  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *omxh264enc;
  GstElement *filesink;

  gboolean is_camera0;
  GMutex lock;
  gboolean exit;
};

// Hangles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) userdata;
  guint idx = 0;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      cameraswitchctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (cameraswitchctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (cameraswitchctx->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (cameraswitchctx->mloop);
  }

  g_mutex_lock (&cameraswitchctx->lock);
  cameraswitchctx->exit = true;
  g_mutex_unlock (&cameraswitchctx->lock);

  return TRUE;
}

// Handles state change transisions
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
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
  GMainLoop *mloop = (GMainLoop*) userdata;
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
  GMainLoop *mloop = (GMainLoop*) userdata;
  static guint eoscnt = 0;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

void
switch_camera (GstCameraSwitchCtx *cameraswitchctx) {

  GstElement *qmmf = NULL;
  GstElement *qmmf_second = NULL;
  GstElement *capsfilter = NULL;
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  g_print ("\n\nSwitch_camera...\n");

  if (!cameraswitchctx->is_camera0) {
    qmmf = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc_0");
    g_object_set (G_OBJECT (qmmf), "name", "qmmf_0", NULL);
    g_object_set (G_OBJECT (qmmf), "camera", 0, NULL);
    cameraswitchctx->qtiqmmfsrc_0 = qmmf;

    qmmf_second = cameraswitchctx->qtiqmmfsrc_1;
  } else {
    qmmf = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc_1");
    g_object_set (G_OBJECT (qmmf), "name", "qmmf_1", NULL);
    g_object_set (G_OBJECT (qmmf), "camera", 1, NULL);
    cameraswitchctx->qtiqmmfsrc_1 = qmmf;

    qmmf_second = cameraswitchctx->qtiqmmfsrc_0;
  }

  // Adding qmmfsrc
  gst_bin_add (GST_BIN (cameraswitchctx->pipeline), qmmf);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (qmmf);

  // Unlink the current camera stream
  g_print ("Unlinking current camera stream...\n");
  gst_element_unlink (qmmf_second, cameraswitchctx->capsfilter);
  g_print ("Unlinked current camera stream successfully \n");

  // Link the next camera stream
  g_print ("Linking next camera stream...\n");
  if (!gst_element_link (qmmf, cameraswitchctx->capsfilter)) {
    g_printerr ("Error: Link cannot be done!\n");
    return;
  }
  g_print ("Linked next camera stream successfully \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (qmmf_second, GST_STATE_NULL);

  gst_bin_remove (GST_BIN (cameraswitchctx->pipeline), qmmf_second);

  cameraswitchctx->is_camera0 = !cameraswitchctx->is_camera0;
}

static void *
thread_fn (gpointer user_data)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) user_data;

 while (true) {
    sleep (5);
    g_mutex_lock (&cameraswitchctx->lock);
    if (cameraswitchctx->exit) {
      g_mutex_unlock (&cameraswitchctx->lock);
      return NULL;
    }
    g_mutex_unlock (&cameraswitchctx->lock);

    switch_camera (cameraswitchctx);
  }

  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstCaps *filtercaps;
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc_0 = NULL;
  GstElement *capsfilter = NULL;
  GstElement *waylandsink = NULL;
  GstElement *omxh264enc = NULL;
  GstElement *filesink = NULL;
  GstElement *h264parse = NULL;
  GstElement *mp4mux = NULL;
  gboolean ret = FALSE;
  GstStateChangeReturn state_ret = GST_STATE_CHANGE_FAILURE;
  GstCameraSwitchCtx cameraswitchctx = {};
  cameraswitchctx.exit = false;
  g_mutex_init (&cameraswitchctx.lock);

  // Initialize GST library.
  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("gst-cameraswitch");
  cameraswitchctx.pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc_0 = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc_0");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

#ifdef USE_DISPLAY
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
#else
  omxh264enc      = gst_element_factory_make ("omxh264enc", "omxh264enc");
  filesink        = gst_element_factory_make ("filesink", "filesink");
  h264parse       = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux          = gst_element_factory_make ("mp4mux", "mp4mux");
#endif

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc_0 || !capsfilter) {
    g_printerr ("One element could not be created of found. Exiting.\n");
    return -1;
  }

#ifndef USE_DISPLAY
  g_object_set (G_OBJECT (h264parse), "name", "h264parse", NULL);
  g_object_set (G_OBJECT (mp4mux), "name", "mp4mux", NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (omxh264enc), "name", "omxh264enc", NULL);
  g_object_set (G_OBJECT (omxh264enc), "target-bitrate", 6000000, NULL);
  g_object_set (G_OBJECT (omxh264enc), "periodicity-idr", 1, NULL);
  g_object_set (G_OBJECT (omxh264enc), "interval-intraframes", 29, NULL);
  g_object_set (G_OBJECT (omxh264enc), "control-rate", 2, NULL);

  g_object_set (G_OBJECT (filesink), "name", "filesink", NULL);
  g_object_set (G_OBJECT (filesink), "location", "/data/mux.mp4", NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", false, NULL);
#endif

  // Set qmmfsrc 0 properties
  g_object_set (G_OBJECT (qtiqmmfsrc_0), "name", "qmmf_0", NULL);
  g_object_set (G_OBJECT (qtiqmmfsrc_0), "camera", 0, NULL);

  // Set capsfilter properties
  g_object_set (G_OBJECT (capsfilter), "name", "capsfilter", NULL);

#ifdef USE_DISPLAY
  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "name", "waylandsink", NULL);
  g_object_set (G_OBJECT (waylandsink), "x", 0, NULL);
  g_object_set (G_OBJECT (waylandsink), "y", 0, NULL);
  g_object_set (G_OBJECT (waylandsink), "width", 600, NULL);
  g_object_set (G_OBJECT (waylandsink), "height", 400, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "enable-last-sample", false, NULL);
#endif

  // Set caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, OUTPUT_WIDTH,
      "height", G_TYPE_INT, OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  cameraswitchctx.qtiqmmfsrc_0 = qtiqmmfsrc_0;
  cameraswitchctx.capsfilter = capsfilter;
  cameraswitchctx.is_camera0 = true;

#ifdef USE_DISPLAY
  cameraswitchctx.waylandsink = waylandsink;
#else
  cameraswitchctx.h264parse = h264parse;
  cameraswitchctx.mp4mux = mp4mux;
  cameraswitchctx.omxh264enc = omxh264enc;
  cameraswitchctx.filesink = filesink;
#endif

#ifdef USE_DISPLAY
  // Add qmmfsrc to the pipeline
  gst_bin_add_many (GST_BIN (cameraswitchctx.pipeline), qtiqmmfsrc_0,
      capsfilter, waylandsink, NULL);
#else
  // Add qmmfsrc to the pipeline
  gst_bin_add_many (GST_BIN (cameraswitchctx.pipeline), qtiqmmfsrc_0,
      capsfilter, omxh264enc, h264parse, mp4mux, filesink, NULL);
#endif

#ifdef USE_DISPLAY
  // Link the elements
  if (!gst_element_link_many (qtiqmmfsrc_0, capsfilter, waylandsink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    return -1;
  }
#else
  // Link the elements
  if (!gst_element_link_many (qtiqmmfsrc_0, capsfilter, omxh264enc,
        h264parse, mp4mux, filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    return -1;
  }
#endif

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }
  cameraswitchctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
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
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &cameraswitchctx);

  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, &cameraswitchctx);

  // Run main loop.
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  pthread_join (thread, NULL);

  g_print ("Setting pipeline to NULL state ...\n");
  state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  switch (state_ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to state!\n");
      return -1;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      state_ret = gst_element_get_state (
          pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (state_ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return -1;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  if (cameraswitchctx.is_camera0) {
#ifdef USE_DISPLAY
    gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline),
      cameraswitchctx.qtiqmmfsrc_0, cameraswitchctx.capsfilter,
      cameraswitchctx.waylandsink, NULL);
#else
    gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline),
      cameraswitchctx.qtiqmmfsrc_0, cameraswitchctx.capsfilter,
      cameraswitchctx.omxh264enc, cameraswitchctx.h264parse,
      cameraswitchctx.mp4mux, cameraswitchctx.filesink, NULL);
#endif
  } else {
#ifdef USE_DISPLAY
    gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline),
      cameraswitchctx.qtiqmmfsrc_1, cameraswitchctx.capsfilter,
      cameraswitchctx.waylandsink, NULL);
#else
    gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline),
      cameraswitchctx.qtiqmmfsrc_1, cameraswitchctx.capsfilter,
      cameraswitchctx.omxh264enc, cameraswitchctx.h264parse,
      cameraswitchctx.mp4mux, cameraswitchctx.filesink, NULL);
#endif
  }

  g_mutex_clear (&cameraswitchctx.lock);

  gst_deinit ();

  g_print ("main: Exit\n");

  return 0;
}
