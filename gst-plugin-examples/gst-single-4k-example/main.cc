/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
* GStreamer Single 4K stream saved to MP4
*
* Description:
* This application creates one 4K stream and encode it in MP4
*
* Usage:
* gst-single-4k-example
*
* Help:
* gst-single-4k-example --help
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define DEFAULT_OUTPUT_WIDTH 3840
#define DEFAULT_OUTPUT_HEIGHT 2160

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of pipeline plugins
  GList *plugins;
  // Pointer to the mainloop
  GMainLoop *mloop;
};

// Handle interrupt by CTRL+C
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

// Handle state change events for the pipeline
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

// Unlink and remove all elements
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

// Create all elements and link
static gboolean
create_pipe (GstAppContext *appctx, gint width, gint height)
{
  GstElement *qtiqmmfsrc, *main_capsfilter, *encoder, *h264parse,
      *mp4mux, *filesink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;

  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  main_capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
#ifdef CODEC2_ENCODE
    encoder = gst_element_factory_make ("qtic2venc", "qtic2venc");
#else
    encoder = gst_element_factory_make ("omxh264enc", "omxh264enc");
#endif
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
    filesink = gst_element_factory_make ("filesink", "filesink");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !main_capsfilter || !encoder ||
      !h264parse || !mp4mux || !filesink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, encoder);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, mp4mux);
  appctx->plugins = g_list_append (appctx->plugins, filesink);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

#ifndef CODEC2_ENCODE
  // OMX encoder specific props
  g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
  g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
  g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
#endif

  // Set filesink properties
  g_object_set (G_OBJECT (filesink), "location", "/data/mux.mp4", NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", false, NULL);

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

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtiqmmfsrc, main_capsfilter, encoder, h264parse, mp4mux, filesink, NULL);

  g_print ("Linking elements...\n");

  // Linking the stream
  ret = gst_element_link_many (
      qtiqmmfsrc, main_capsfilter, encoder, h264parse, mp4mux, filesink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        qtiqmmfsrc, main_capsfilter, encoder, h264parse,
        mp4mux, filesink, NULL);
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
  gint width = DEFAULT_OUTPUT_WIDTH;
  gint height = DEFAULT_OUTPUT_HEIGHT;

  // Configure input parameters
  GOptionEntry entries[] = {
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

  // Build the pipeline
  ret = create_pipe (&appctx, width, height);
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
