/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for single camera multistream usecases
 *
 * Description:
 * This Application Demonstrates in viewing camera Live on the
 * waylandsink and Dump Video Encoder output
 *
 * Usage:
 * gst-multi-stream-example --num_of_streams=2 --width=1280 --height=720
 *
 * Help:
 * gst-multi-stream-example --help
 *
 * *******************************************************************
 * Pipeline for two stream:
 *                         |->waylandsink
 * qtiqmmfsrc->capsfilter->|
 *                         |->v4l2h264enc->h264parse->mp4mux->filesink
 *
 * *******************************************************************
 */

#include <glib-unix.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define DEFAULT_OUTPUT_FILENAME "/opt/video.mp4"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_NUM_OF_STREAM 2

#define GST_APP_SUMMARY "This application demonstrates the use of a single " \
  "camera to generate multiple streams for various purposes. \n One stream " \
  "is displayed as a preview, while the other stream is stored as an " \
  "encoded stream. \n " \
  "\nCommand:\n" \
  "For Two Stream \n" \
  "  gst-multi-stream-example -w 1920 -h 1080 -n 2 -o /opt/video.mp4 \n" \
  "\nOutput:\n" \
  "  Upon execution, application will generates output as preview and " \
  "encoded mp4 file."

// Structure to hold the application context
struct GstMultiStreamAppContext : GstAppContext {
  gint width;
  gint height;
  gint stream_count;
  gchar *output_file;
};

/**
* Create and initialize application context:
*
* @param NULL
*/
static GstMultiStreamAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstMultiStreamAppContext *ctx = (GstMultiStreamAppContext *)
      g_new0 (GstMultiStreamAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->stream_count = DEFAULT_NUM_OF_STREAM;
  ctx->output_file = DEFAULT_OUTPUT_FILENAME;
  return ctx;
}

/**
* Free Application context:
*
* @param appctx application context object
*/
static void
gst_app_context_free (GstMultiStreamAppContext * appctx)
{
  // If the plugins list is not empty, unlink and remove all elements
  if (appctx->plugins != NULL) {
    GstElement *element_curr = (GstElement *) appctx->plugins->data;
    GstElement *element_next;

    GList *list = appctx->plugins->next;
    for (; list != NULL; list = list->next) {
      element_next = (GstElement *) list->data;
      gst_element_unlink (element_curr, element_next);
      gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);
      element_curr = element_next;
    }
    gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);

    // Free the plugins list
    g_list_free (appctx->plugins);
    appctx->plugins = NULL;
  }

  // If specific pointer is not NULL, unref it

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx->output_file != NULL && appctx->output_file != DEFAULT_OUTPUT_FILENAME)
    g_free (appctx->output_file);

  if (appctx != NULL)
    g_free (appctx);
}

/**
* Create GST pipeline involves 3 main steps
* 1. Create all elements/GST Plugins
* 2. Set Paramters for each plugin
* 3. Link plugins to create GST pipeline
*
* @param appctx Application Context object.
*
*/
static gboolean
create_two_stream_pipe (GstMultiStreamAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *qtiqmmfsrc, *capsfilter_dis, *capsfilter_enc, *v4l2h264enc,
      *h264parse, *mp4mux, *filesink, *waylandsink;
  GstCaps *filtercaps;
  GstStructure *controls;
  gboolean ret = FALSE;

  // Create first source element set the first camera
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  // Create capsfilter element for waylandsink to properties
  capsfilter_dis = gst_element_factory_make ("capsfilter", "capsfilter_dis");

  // Create capsfilter element for the encoder to set properties
  capsfilter_enc = gst_element_factory_make ("capsfilter", "capsfilter_enc");

  // Create waylandsink element to display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");

  // Create v4l2h264enc element and set the properties
  v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
  g_object_set (G_OBJECT (v4l2h264enc), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (v4l2h264enc), "output-io-mode", 5, NULL);
  controls = gst_structure_from_string (
      "controls,video_bitrate=512000,video_bitrate_mode=0", NULL);
  g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", controls, NULL);

  // Create h264parse element for parsing the stream
  h264parse = gst_element_factory_make ("h264parse", "h264parse");

  // Create mp4mux element for muxing the stream
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

  // Create filesink element for storing the encoding stream
  filesink = gst_element_factory_make ("filesink", "filesink");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !capsfilter_dis || !capsfilter_enc || !v4l2h264enc ||
      !h264parse || !mp4mux || !filesink || !waylandsink) {
    g_printerr ("\n One element could not be created. Exiting experiment.\n");
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_dis);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_enc);
  appctx->plugins = g_list_append (appctx->plugins, v4l2h264enc);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, mp4mux);
  appctx->plugins = g_list_append (appctx->plugins, filesink);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // Set filesink_enc properties
  g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", true, NULL);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_dis), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_enc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  g_print ("\n Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_dis,
      capsfilter_enc, v4l2h264enc, h264parse, mp4mux, filesink, waylandsink, NULL);

  g_print ("\n Link display elements...\n");
  // Linking the display stream
  ret = gst_element_link_many (qtiqmmfsrc, capsfilter_dis, waylandsink, NULL);
  if (!ret) {
    g_printerr ("\n Display Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_dis,
        capsfilter_enc, v4l2h264enc, h264parse, mp4mux, filesink, waylandsink,
        NULL);
    return FALSE;
  }

  g_print ("\n Link encoder elements...\n");
  // Linking the encoder stream
  ret = gst_element_link_many (qtiqmmfsrc, capsfilter_enc, v4l2h264enc, h264parse,
      mp4mux, filesink, NULL);
  if (!ret) {
    g_printerr ("\n Video Encoder Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_dis,
        capsfilter_enc, v4l2h264enc, h264parse, mp4mux, filesink, waylandsink,
        NULL);
    return FALSE;
  }

  g_print ("\n All elements are linked successfully\n");

  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstMultiStreamAppContext *appctx = NULL;
  guint intrpt_watch_id = 0;

  // Setting Display environment variables
  g_print ("Setting Display environment \n");
  setenv ("XDG_RUNTIME_DIR", "/run/user/root", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', DEFAULT_WIDTH, G_OPTION_ARG_INT,
      &appctx->width, "width", "image width" },
    { "height", 'h', DEFAULT_HEIGHT, G_OPTION_ARG_INT, &appctx->height, "height",
      "image height" },
    { "num_of_streams", 'n', DEFAULT_NUM_OF_STREAM, G_OPTION_ARG_INT,
      &appctx->stream_count, "num_of_streams", "Stream count for single camera" },
    { "output_file", 'o', 0, G_OPTION_ARG_STRING, &appctx->output_file,
      "Output Filename , \
          -o /opt/video.mp4" },
    { NULL }
    };

  // Parse command line entries.
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("\n Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\n Failed Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("\n Failed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("gst-multi-stream-example");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  if (appctx->stream_count == 2) {
    ret = create_two_stream_pipe (appctx);
    if (!ret) {
      g_printerr ("\n failed to create GST pipe.\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("\n Stream count is not valid.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("\n Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("\n Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (state_changed_cb),
      pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("\n Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return -1;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\n Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("\n Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\n Pipeline state change was successful\n");
      break;
  }

  // Start the main loop
  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  g_print ("Encoded mp4 File %s\n", appctx->output_file);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
