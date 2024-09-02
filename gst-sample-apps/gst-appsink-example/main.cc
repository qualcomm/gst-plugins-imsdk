/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application to Demonstrates buffer access through appsink
 *
 * Description:
 * This application Demonstrates executing of appsink
 * when new sample is available in the pipeline then
 * application extracts the buffer from the sample for further processing.
 *
 * Usage:
 * gst-appsink-example --width=1920 --height=1080
 *
 * Help:
 * gst-appsink-example --help
 *
 * *********************************************************
 * Pipeline for appsink: qtiqmmfsrc->capsfilter->appsink
 * *********************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

#define GST_APP_SUMMARY "This application illustrates the utilization of the" \
  " appsink plugin to extract buffer samples from a camera pipeline." \
  "\n Users can employ these buffers for custom processing.\n" \
  "\nCommand:\n" \
  "  gst-appsink-example --width=1920 --height=1080 \n" \
  "\nOutput:\n" \
  "  Upon successful execution, the application will display the" \
  " following message on the screen:" \
  "\n  Received a buffer, doing some processing...\n"

// Structure to hold the application context
struct GstAppSinkContext : GstAppContext {
  gint width;
  gint height;
};

/**
 * Release GstSample when processing is done:
 *
 * @param sample buffer to release
 */
static void
gst_sample_release (GstSample * sample)
{
  gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
  gst_sample_set_buffer (sample, NULL);
#endif
}

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstAppSinkContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstAppSinkContext *ctx = (GstAppSinkContext *) g_new0 (GstAppSinkContext, 1);

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
  return ctx;
}

/**
 * Signal that a new sample is available:
 *
 * @param appsink element that emitted the signal
 * @param No description available
 */
static GstFlowReturn
new_sample (GstElement * sink, gpointer userdata)
{
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo info;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("\n Pulled sample is NULL!");
    return GST_FLOW_ERROR;
  }

  // retrieve the buffer
  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("\n Pulled buffer is NULL!");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // map the buffer
  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("\n Failed to map the pulled buffer!");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // user can access buffer in the application for any processing
  g_print ("\n Received a buffer, doing some processing ...\n\n");

  // after use unmap buffer
  gst_buffer_unmap (buffer, &info);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstAppSinkContext * appctx)
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
 */
static gboolean
create_pipe (GstAppSinkContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *qtiqmmfsrc, *capsfilter, *appsink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  appctx->plugins = NULL;

  // Create camera source and the element capability
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  filtercaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width, "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1, "compression", G_TYPE_STRING, "ubwc",
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // creating the appsink element and setting the properties
  appsink = gst_element_factory_make ("appsink", "appsink");
  g_object_set (G_OBJECT (appsink), "name", "sink", NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", true, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
      appsink, NULL);

  g_print ("\n Linking appsink elements ..\n");

  ret = gst_element_link_many (qtiqmmfsrc, capsfilter, appsink, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
        capsfilter, appsink, NULL);
    return FALSE;
  }

  // get the element with name from pipeline
  GstElement *element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "sink");

  // signal connect for the new_sample
  g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), NULL);

  // Append all elements to the plugins list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, appsink);

  g_print ("\n All elements are linked successfully\n");

  // Return TRUE if all elements were linked successfully, FALSE otherwise
  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstAppSinkContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
      {"width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
       "Supported camera width   -Default width:1280"},
      {"height", 'h', 0, G_OPTION_ARG_INT, &appctx->height,
       "Supported camera height  -Default height:720"},
      {NULL}
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
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\n Initializing: Unknown error!\n");
      return -1;
    }
  } else {
    g_printerr ("\n Failed to create options context!\n");
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-appsink-example");

  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (appctx);
  if (!ret) {
    g_printerr ("\n failed to create GST pipe.\n");
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

  // start the main loop
  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the Interrupt signal Handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  // free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST Library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
