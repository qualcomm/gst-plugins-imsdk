/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for single Camera usecases with different possible o/p
 *
 * Description:
 * This application Demonstrates in Viewing Camera Live on waylandsink
 * or Dumping the Camera YUV to a filesink of user choice
 *
 * Usage:
 * For Preview on Display:
 * gst-camera-single-stream-example --sinktype=0 --width=1920 --height=1080
 * For YUV dump on device:
 * gst-camera-single-stream-example --sinktype=1 --width=1920 --height=1080
 * /opt/yuv_dump%d.yuv
 *
 * Help:
 * gst-camera-single-stream-example --help
 *
 * *******************************************************************
 * Pipeline For YUV dump on device: qtiqmmfsrc->capsfilter->filesink
 * Pipeline For Preview on Display: qtiqmmfsrc->capsfilter->waylandsink
 * *******************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define DEFAULT_OUTPUT_FILENAME "/opt/yuv_dump%d.yuv"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

#define GST_APP_SUMMARY                                                     \
  "This app enables the users to view the either to have Camera Live\n"     \
  "on Waylandsink or for encoding to filesink and for Camera YUV Dumping\n" \
  "\nForWaylandsink Preview:\n"                                             \
  "gst-camera-single-stream-example -s 0 -w 1920 -h 1080 \n"                \
  "\nFor YUV Dump:\n"                                                       \
  "gst-camera-single-stream-example -s 1 -w 1920 -h 1080 "                  \
  "--output_file=/opt/yuv_dump%d.yuv"

// Structure to hold the application context
struct GstCameraAppContext : GstAppContext {
  gchar *output_file;
  GstSinkType sinktype;
  gint width;
  gint height;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstCameraAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstCameraAppContext *ctx = (GstCameraAppContext *) g_new0 (GstCameraAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->output_file = DEFAULT_OUTPUT_FILENAME;
  ctx->sinktype = GST_WAYLANDSINK;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstCameraAppContext * appctx)
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

  if (appctx->output_file != NULL && appctx->sinktype != NULL)
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
 * @param appctx Application Context Object.
 */
static gboolean
create_pipe (GstCameraAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *qtiqmmfsrc, *capsfilter, *waylandsink, *filesink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  appctx->plugins = NULL;

  // Create camera source element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  // Set the source elements capability
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING,
      "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT,
      appctx->height, "framerate", GST_TYPE_FRACTION, 30, 1, "compression",
      G_TYPE_STRING, "ubwc", NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // check the sink type and create the sink elements
  if (appctx->sinktype == GST_WAYLANDSINK) {
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
    g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

    if (!waylandsink) {
      g_printerr ("\n waylandsink element not created. Exiting.\n");
      return FALSE;
    }

    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        waylandsink, NULL);

    g_print ("\n Linking display elements ..\n");

    ret = gst_element_link_many (qtiqmmfsrc, capsfilter, waylandsink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
          capsfilter, waylandsink, NULL);
      return FALSE;
    }
  } else if (appctx->sinktype == GST_YUVDUMP) {
    // set the output file location for filesink element
    filesink = gst_element_factory_make ("multifilesink", "filesink");
    g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);
    g_object_set (G_OBJECT (filesink), "enable-last-sample", false, NULL);
    g_object_set (G_OBJECT (filesink), "max-files", 2, NULL);

    if (!qtiqmmfsrc || !capsfilter || !filesink) {
      g_printerr ("\n One element could not be created. Exiting.\n");
      return FALSE;
    }

    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        filesink, NULL);

    g_print ("\n Linking elements yuv dump..\n");

    ret = gst_element_link_many (qtiqmmfsrc, capsfilter, filesink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
          capsfilter, filesink, NULL);
      return FALSE;
    }
  }

  // Append all elements to the plugins list for clean up
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  if (appctx->sinktype == GST_WAYLANDSINK) {
    appctx->plugins = g_list_append (appctx->plugins, waylandsink);
  } else if (appctx->sinktype == GST_YUVDUMP) {
    appctx->plugins = g_list_append (appctx->plugins, filesink);
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
  GstCameraAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-camera-single-stream-example --help \n");
    return -1;
  }

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
      {"width", 'w', 0, G_OPTION_ARG_INT, &appctx->width, "width",
       "image width"
      },
      {"height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
       "image height"
      },
      {"sinktype", 's', 0, G_OPTION_ARG_INT, &appctx->sinktype,
       "\t\t\t\t\t   sinktype",
       "\n\t0-WAYLANDSINK"
       "\n\t1-YUVDUMP"
      },
      {"output_file", 'o', 0, G_OPTION_ARG_STRING, &appctx->output_file,
       "Output Filename , \
          -o /opt/yuv_dump%d.yuv"
      },
      {NULL}
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("gst-camera-single-stream-example")) != NULL) {
    g_option_context_set_summary (ctx, GST_APP_SUMMARY);
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

  g_set_prgname ("gst-camera-single-stream-example");

  // Check the Input value
  if (appctx->sinktype > GST_YUVDUMP) {
    g_printerr ("\n Invalid user Input:gst-camera-single-stream-example --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

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
