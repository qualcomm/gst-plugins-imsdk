/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application:
 * Concurrent avc video playback and composition on display.
 *
 * Description:
 * This is an application to demonstrate the Concurrent video playback
 * of any supported resolution and composition on display (Video wall).
 * The output is shown on the display.
 *
 * Usage:
 * gst-concurrent-videoplay-composition -c 2 -i <h264_file>.mp4 -i <h264_file>.mp4
 *
 * Help:
 * gst-concurrent-videoplay-composition --help
 *
 *
 ** ***********************************************************************
 * For two Concurrent decode and composition pipeline:
 * filesrc->qtdemux->h264parse->v4l2h264dec->|
 *                                           |->qtivcomposer->waylandsink
 * filesrc->qtdemux->h264parse->v4l2h264dec->|
 * ***********************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>
#include <sys/resource.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_pipeline.h"
#include "include/gst_sample_apps_utils.h"

#define ARRAY_LENGTH 20
#define TWO_STREAM_CNT 2
#define FOUR_STREAM_CNT 4
#define EIGHT_STREAM_CNT 8
#define SIXTEEN_STREAM_CNT 16

#define GST_APP_SUMMARY "This application performs concurrent " \
  "video playback for AVC codec and composition on display (video wall).\n" \
  "The application expects at least one input file from the user. If the " \
  "number of input files is less than the concurrency count, the same " \
  "file will be played concurrently. \n The input file is expected to be " \
  "an MP4 file encoded with the AVC codec. \n" \
  "\nCommand:\n" \
  "concurrent playback for two sessions \n" \
  "  gst-concurrent-videoplay-composition -c 2 -i <h264_file>.mp4 " \
  "-i <h264_file>.mp4 \n" \
  "concurrent playback for four sessions \n " \
  "  gst-concurrent-videoplay-composition -c 4 -i <h264_file>.mp4 \n" \
  "\nOutput:\n" \
  "  Upon executing the application, concurrent video playback can be " \
  "observed on the display."

// Structure to hold the application context
struct GstVideoAppContext : GstAppContext {
  gchar **input_files;
  gint stream_cnt;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstVideoAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstVideoAppContext *ctx = (GstVideoAppContext *) g_new0 (GstVideoAppContext, 1);

  if (NULL == ctx) {
    g_printerr ("Unable to create App Context");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->input_files = NULL;
  ctx->stream_cnt = TWO_STREAM_CNT;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param ctx application context object
 */
static void
gst_app_context_free (GstVideoAppContext *ctx)
{
  if (ctx->mloop != NULL) {
    g_main_loop_unref (ctx->mloop);
    ctx->mloop = NULL;
  }

  if (ctx->pipeline != NULL) {
    gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
    gst_object_unref (ctx->pipeline);
    ctx->pipeline = NULL;
  }

  if (ctx->input_files)
    g_strfreev (ctx->input_files);
  g_free (ctx);
}

/**
 * Create GST pipeline involves 2 main steps
 * 1. Create the GST pipeline based on user input
 * 2. Set location Paramters for each source plugin
 *
 * @param appctx application context object.
 */
static gboolean
create_pipe (GstVideoAppContext *appctx)
{
  GError *error = NULL;
  GstElement *element = NULL;
  gchar temp_str[ARRAY_LENGTH];
  gint file_index = 0;
  gint file_count = 0;

  // Initiate an pipeline for Concurrent decode based on user input
  switch (appctx->stream_cnt) {
    case TWO_STREAM_CNT:
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_2STREAM, &error);
      break;
    case FOUR_STREAM_CNT:
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_4STREAM, &error);
      break;
    case EIGHT_STREAM_CNT:
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_8STREAM, &error);
      break;
    case SIXTEEN_STREAM_CNT:
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_16STREAM, &error);
      break;
    default:
      g_printerr ("Pipeline couldn't be created,invalid stream count: %d\n",
          appctx->stream_cnt);
      return FALSE;
  }

  if (appctx->pipeline == NULL) {
    if (NULL != error) {
      g_printerr ("Pipeline couldn't be created, error %s",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
    }
    return FALSE;
  }

  // find the input file count from user
  while (appctx->input_files[file_count]) {
    file_count++;
  }

  g_print ("Setting the file location\n");
  for (gint i = 0; i < appctx->stream_cnt; i++) {
    file_index = i % file_count;
    snprintf (temp_str, sizeof (temp_str), "source%d", i);
    if ((element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), temp_str)) == NULL) {
      g_printerr ("Couldn't find filesrc\n");
      return FALSE;
    }

    g_object_set (G_OBJECT (element), "location",
        appctx->input_files[file_index], NULL);
    gst_object_unref (element);
    memset (temp_str, 0, ARRAY_LENGTH);
  }

  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstVideoAppContext *appctx = NULL;
  guint intrpt_watch_id = 0;
  struct rlimit rl;

  // Define the new limit
  rl.rlim_cur = 4096; // Soft limit
  rl.rlim_max = 4096; // Hard limit

  // Set the new limit
  if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
    g_printerr ("Failed to set setrlimit\n");
  }

  // Get the current limit
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
    g_printerr ("Failed to get getrlimit\n");
  }

  // Setting Display environment variables
  g_print ("Setting Display environment \n");
  setenv ("XDG_RUNTIME_DIR", "/run/user/root", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Create the application context
  appctx = gst_app_context_new ();
  if (NULL == appctx) {
    g_printerr ("Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "stream_cnt", 'c', 0, G_OPTION_ARG_INT, &appctx->stream_cnt,
      "No of stream for decode and composition 2, 4, 8 or 16" },
    { "input_file", 'i', 0, G_OPTION_ARG_FILENAME_ARRAY, &appctx->input_files,
      "Input AVC Filenames - Path of AVC files to be played with filenames, \
      e.g. -i /opt/<h264_file>.mp4 -i /opt/<h264_file>.mp4" },
    { NULL }
  };

  // Parse the command line entries
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) !=
      NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Check the input parameters from the user
  if (!appctx->input_files || (appctx->stream_cnt == 0) ||
      (appctx->stream_cnt & (appctx->stream_cnt-1))) {
    g_printerr ("\n one of input param is not valid: count %d input file %s\n",
        appctx->stream_cnt, appctx->input_files);
    g_print ("\n usage: gst-concurrent-videoplay-composition --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library
  gst_init (&argc, &argv);

  // Build the pipeline
  if (!create_pipe (appctx)) {
    g_printerr ("Failed to create GST pipe.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (state_changed_cb),
      appctx->pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("Failed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return -1;
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

  // Start the main loop
  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
