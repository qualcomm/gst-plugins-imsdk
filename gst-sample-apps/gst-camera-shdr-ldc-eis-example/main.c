/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for camera feature of SHDR,LDC AND EIS
 *
 * Description:
 * This application Demonstrates camera usecases with below possible features:
 *     --SHDR
 *     --LDC
 *     --EIS
 *
 * Usage:
 * For SHDR:
 * gst-camera-shdr-ldc-eis-example -s 1
 * For LDC:
 * gst-camera-shdr-ldc-eis-example -l 1
 * For EIS:
 * gst-camera-shdr-ldc-eis-example -e 1
 *
 * Help:
 * gst-camera-shdr-ldc-eis-example --help
 *
 * *******************************************************************************
 * Pipeline For the Camera Features:SHDR,LDC AND EIS:
 * qtiqmmfsrc->capsfilter->waylandsink
 * *******************************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>
#include <stdbool.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_ENABLE 1
#define DEFAULT_DISABLE 0

#define GST_APP_SUMMARY "This app enables the users to visualize camera features of" \
  " SHDR, LDC and EIS \n" \
  "\nCommand:\n" \
  "For SHDR:\n" \
  "  gst-camera-shdr-ldc-eis-example -s 1 -w 1920 -h 1080 \n" \
  "For LDC:\n" \
  "  gst-camera-shdr-ldc-eis-example -l 1 -w 1920 -h 1080 \n" \
  "For EIS:\n" \
  "  gst-camera-shdr-ldc-eis-example -e 1 -w 1920 -h 1080 \n" \
  "  \nUpon execution, application will generates output on waylandsink as user selected. \n" \

// Structure to hold the application context
struct _GstCameraAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
  gint width;
  gint height;
  gboolean shdr;
  gboolean ldc;
  gboolean eis;
};

typedef struct _GstCameraAppContext GstCameraAppContext;

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstCameraAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstCameraAppContext *ctx =
      (GstCameraAppContext *) g_new0 (GstCameraAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }
  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->shdr = false;
  ctx->ldc = false;
  ctx->eis = false;
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

static void *
thread_fn (gpointer user_data)
{
  GstCameraAppContext *appctx = (GstCameraAppContext *) user_data;
  GstElement *qtiqmmfsrc = NULL;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qtiqmmfsrc");

  if (appctx->shdr) {
    g_object_set (G_OBJECT (qtiqmmfsrc), "vhdr", DEFAULT_DISABLE, NULL);
    g_print ("Disable SHDR on stream %d \n", appctx->shdr);
  }
  // wait for 10sec
  sleep (10);

  if (appctx->shdr) {
    // Run the stream with all setting off
    g_print ("Run the stream with all setting ON \n");
    g_object_set (G_OBJECT (qtiqmmfsrc), "vhdr", DEFAULT_ENABLE, NULL);
    g_print ("Enable SHDR on stream %d \n", appctx->shdr);
  }

  return NULL;
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
  GstElement *qtiqmmfsrc, *capsfilter, *waylandsink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  GstPad *pad = NULL;

  // Create camera source element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  //creating caps filter element
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  // creating wayland sink element
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !capsfilter || !waylandsink) {
    g_printerr ("\n Not all elements could be created.\n");
    return FALSE;
  }
  // Set the source elements capability
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // setting sink element properties
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // Get qmmfsrc Element class
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Get qmmfsrc pad template
  GstPadTemplate *qtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");

  // Request a pad from qmmfsrc
  pad = gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template,
      "video_%u", NULL);
  if (!pad) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
    return FALSE;
  }
  g_print ("Pad received - %s\n", gst_pad_get_name (pad));

  g_object_set (G_OBJECT (pad), "type", DEFAULT_ENABLE, NULL);
  gst_object_unref (pad);

  if (appctx->ldc) {
    g_object_set (G_OBJECT (qtiqmmfsrc), "ldc", DEFAULT_ENABLE, NULL);
    g_print ("Enable LDC on stream");
  }

  if (appctx->eis) {
    g_object_set (G_OBJECT (qtiqmmfsrc), "eis", DEFAULT_ENABLE, NULL);
    g_print ("Enable EIS on stream");
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
      waylandsink, NULL);

  g_print ("\n Link pipeline for all the elements ..\n");

  ret = gst_element_link_many (qtiqmmfsrc, capsfilter, waylandsink, NULL);
  if (!ret) {
    g_printerr ("\nPipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        waylandsink, NULL);
    return FALSE;
  }

  g_print ("\n All elements are linked successfully\n");
  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstCameraAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;
  guint thread_ret = 0;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }
  // Configure input parameters
  GOptionEntry entries[] = {
    {"width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
        "width", "camera width"},
    {"height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
        "camera height"},
    {"ldc", 'l', 0, G_OPTION_ARG_NONE,
          &appctx->ldc,
          "Enable ldc",
        "Parameter for enable ldc"},
    {"eis", 'e', 0, G_OPTION_ARG_NONE,
          &appctx->eis,
          "Enable eis",
        "Parameter for enable eis"},
    {"shdr", 's', 0, G_OPTION_ARG_NONE,
          &appctx->shdr,
          "Enable shdr",
        "Parameter for enable shdr"},
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

  g_set_prgname ("gst-camera-shdr-ldc-eis-example");

  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
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
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Run thread which perform link and unlink of streams
  pthread_t thread;
  thread_ret = pthread_create (&thread, NULL, &thread_fn, appctx);
  if (thread_ret != 0) {
    g_printerr ("\n Thread failed error code is %d.\n", thread_ret);
    gst_app_context_free (appctx);
    return -1;
  } else {
    pthread_detach (thread);
  }

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
