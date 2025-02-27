/*
* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* Gstreamer Application to decode the JPEG Images on waylandsink
*
* Description:
* This application Demonstrates in decoding the JPEG files on waylandsink
*
* Help:
* gst-jpg-decode-example --help
*
* Usage:
* gst-jpg-decode-example -w 1280 -h 720 -i /opt/imagefiles_%d.jpg
*
* ***********************************************************************
* For Decoding of JPG files: pipeline:
* multifilesrc->capsfilter->jpegdec->videoconvert->autovideosink

* ***********************************************************************
*/

#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/sampleapps/gst_sample_apps_utils.h>

#define GST_APP_SUMMARY                                                       \
  "This application showcases the decoding of JPG files on waylandsink  " \
   "\nCommand:\n" \
  "\n gst-jpg-decode-example -w 1280 -h 720 -i /opt/<imagefiles_%d>.jpg \n" \
  "\n File names must be <imagefiles_1>.jpg,<imagefiles_2>.jpg,<imagefiles_3>.jpg and many"

// Structure to hold the application context
struct GstComposeAppContext : GstAppContext {
  gchar *input_file;
  gint width;
  gint height;
};

/**
* Create and initialize application context:
*
* @param NULL
*/
static GstComposeAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstComposeAppContext *ctx = (GstComposeAppContext *) g_new0 (GstComposeAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context\n");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->input_file = NULL;
  return ctx;
}

/**
* Free Application context:
*
* @param appctx Application Context object
*/
static void
gst_app_context_free (GstComposeAppContext * appctx)
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

  if (appctx->input_file != NULL)
    g_free (appctx->input_file);

  // Finally, free the application context itself
  if (appctx != NULL)
    g_free (appctx);
}

/**
* Create the pipeline for waylandsink composition
* 1. Create all elements/GST Plugins
* 2. Set Paramters for each plugin
* 3. Link plugins to create GST pipeline
*
* @param appctx Application Context Object.
*/
static gboolean
create_pipe_jpgdecode (GstComposeAppContext * appctx)
{
  GstElement *multifilesrc, *capsfilter, *jpegdec, *videoconvert, *autovideosink;
  GstCaps *filtercaps;

  gboolean ret = FALSE;
  appctx->plugins = NULL;

  // Create camera source element
  multifilesrc = gst_element_factory_make ("multifilesrc", "multifilesrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  g_object_set (G_OBJECT (multifilesrc), "location", appctx->input_file, NULL);
  g_object_set (G_OBJECT (multifilesrc), "index", 1, NULL);

  // Set the source elements capability
  filtercaps = gst_caps_new_simple ("image/jpeg", "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height, "framerate",
      GST_TYPE_FRACTION, 1, 1, NULL);

  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // create the elements jpegdec and videoconvert
  jpegdec = gst_element_factory_make ("jpegdec", "jpegdec");
  videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");

  // create the sink element for viewing the jpg files
  autovideosink = gst_element_factory_make ("autovideosink", "autovideosink");

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (appctx->pipeline), multifilesrc, capsfilter,jpegdec,
      videoconvert, autovideosink, NULL);

  g_print ("\n Linking All the elements ..\n");

  ret = gst_element_link_many (multifilesrc, capsfilter, jpegdec,videoconvert,
      autovideosink, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), multifilesrc, capsfilter, jpegdec,
        videoconvert, autovideosink, NULL);
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, multifilesrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, jpegdec);
  appctx->plugins = g_list_append (appctx->plugins, videoconvert);
  appctx->plugins = g_list_append (appctx->plugins, autovideosink);
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
  GstComposeAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the app context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "width", "camera width" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
      "camera height" },
    { "input_file", 'i', 0, G_OPTION_ARG_FILENAME,
      &appctx->input_file,
      "path", "Images Path" },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
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
      g_printerr ("\n Initializing: Unknown error!\n");
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

  g_set_prgname ("gst-jpg-decode-example");

  // Create empty pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline based on user input
  ret = create_pipe_jpgdecode (appctx);
  if (!ret) {
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

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("Setting pipeline to PAUSED state ...\n");
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

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
