/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Concurrent video playback for HEVC and AVC codec.
*
* Description:
* This application to demonstrate to use of different codec simultaneously
* In this example we are using the HEVC and AVC coded content to decode.
* AVC content will go to display and HEVC content will be dumped into file.
*
* Help:
* gst-videocodec-concurrent-playback --help
*
* Usage:
* gst-videocodec-concurrent-playback -i <h264_file>.mp4 -i <h265_file>.mp4
*                                    -o <filename>.yuv
*
* *******************************************************************
* Pipeline 1: filesrc->qtdemux->h264parse->v4l2h264dec->waylandsink
* Pipeline 2: filesrc->qtdemux->h265parse->v4l2h265dec->filesink
* *******************************************************************
*/

#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define ARRAY_LENGTH 20
#define STREAM_CNT 2
#define DEFAULT_AVC_FILESOURCE "/opt/Animals_000_720p_180s_30FPS.mp4"
#define DEFAULT_HEVC_FILESOURCE "/opt/Animals_000_720p_180s_30FPS.mp4"
#define DEFAULT_YUV_FILESINK "/opt/h265_dump.yuv"


#define GST_PIPELINE_2STREAM_VIDEO "filesrc name=source1 " \
  "location=DEFAULT_AVC_FILESOURCE ! qtdemux ! queue ! h264parse ! " \
  "v4l2h264dec capture-io-mode=5 output-io-mode=5 ! " \
  "queue ! waylandsink enable-last-sample=false fullscreen=true " \
  "filesrc name=source2 location=DEFAULT_HEVC_FILESOURCE ! qtdemux ! " \
  "h265parse ! v4l2h265dec capture-io-mode=5 output-io-mode=5 ! " \
  "filesink name=sink_yuv enable-last-sample=false location=DEFAULT_YUV_FILESINK " \

#define GST_APP_SUMMARY \
  "This application demonstrates the concurrent ability of Qualcomm video " \
  "engine decoding the different video codecs content concurrently. \n" \
  "The first file should be H264 and the second file should be HEVC with MP4 container.\n" \
  "\nCommand:\n" \
  "  gst-videocodec-concurrent-playback -i <h264_file>.mp4 -i <h265_file>.mp4 "\
  "-o <filename>.yuv \n" \
  "\nOutput:\n" \
  "  H264 content goes to the display and HEVC content is dumped to YUV file.\n"

// Structure to hold the application context
struct GstVideoAppContext : GstAppContext {
  gchar **in_files;
  gchar *out_file;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstVideoAppContext *
gst_app_context_new ()
{
  GstVideoAppContext *ctx = (GstVideoAppContext *)g_new0 (GstVideoAppContext, 1);

  if (NULL == ctx) {
    g_printerr ("Unable to create App Context");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->in_files = NULL;
  ctx->out_file = NULL;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstVideoAppContext * ctx)
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

  if (ctx->in_files)
    g_strfreev (ctx->in_files);

  if (ctx->out_file)
    g_free (ctx->out_file);
  g_free (ctx);
}

/**
 * Create GST pipeline invloves 3 main steps
 * 1. Initiate an empty pipeline
 * 2. Get source element and set input file location
 * 3. Get sink element and Set output file location
 *
 * @param appctx Application Context Object.
 * @param number of stream counts.
 */
static gboolean
create_pipe (GstVideoAppContext *appctx, gint stream_cnt)
{
  GError *error = NULL;
  GstElement *element = NULL;
  GstElement *sink = NULL;
  gchar temp_str[ARRAY_LENGTH];

  // Initiate an empty pipeline
  appctx->pipeline = gst_parse_launch (GST_PIPELINE_2STREAM_VIDEO, &error);

  if (appctx->pipeline == NULL) {
    if (NULL != error) {
      g_printerr ("Pipeline couldn't be created, error %s",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
    }
    return FALSE;
  }

  // Get source element from pipeline Set input file location
  for (int i = 1; i <= stream_cnt; i++)
  {
    snprintf (temp_str, sizeof (temp_str), "source%d", i);
    element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), temp_str);

    if (element != NULL) {
      if (appctx->in_files == NULL) {
        g_printerr ("Couldn't find input files\n");
        gst_object_unref (element);
        return FALSE;
      } else {
        g_object_set (G_OBJECT (element), "location",
            appctx->in_files[i-1], NULL);
        gst_object_unref (element);
      }
    } else {
      g_printerr ("Couldn't find filesrc\n");
      return FALSE;
    }
    memset( temp_str, 0, ARRAY_LENGTH );
  }

  // Get sink element from pipeline Set output file location
  sink = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "sink_yuv");
  if (sink != NULL) {
    if (appctx->out_file == NULL) {
      g_printerr ("Couldn't find output file path\n");
      gst_object_unref (sink);
      return FALSE;
    } else {
      g_object_set (G_OBJECT (sink), "location", appctx->out_file, NULL);
      gst_object_unref (sink);
    }
  } else {
    g_printerr ("Couldn't find filesrc\n");
    return FALSE;
  }

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstVideoAppContext *appctx = NULL;
  guint intrpt_watch_id = 0;
  gint stream_cnt = STREAM_CNT;
  gboolean ret = FALSE;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-videocodec-concurrent-playback --help \n");
    return -1;
  }

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Create the application context
  appctx = gst_app_context_new ();
  if (NULL == appctx){
    g_printerr ("Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "input_file", 'i', 0,
      G_OPTION_ARG_FILENAME_ARRAY, &appctx->in_files,
      " Two mp4 Input Filenames - First is AVC & second HEVC codec in order.",
      "  e.g. -i /opt/<h264_file>.mp4 -i /opt/<h265_file>.mp4"
    },
    { "output_file", 'o', 0,
      G_OPTION_ARG_FILENAME, &appctx->out_file,
      "Output Filename",
      "  e.g. -o /opt/<filename>.yuv"
    },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (
      "Concurrent Video playback for AVC and HEVC codec ")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_set_summary (ctx, GST_APP_SUMMARY);
    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Check the input parameters from the user
  if (appctx->in_files == NULL || appctx->out_file == NULL) {
    g_printerr ("\n one of input parameters is not given");
    g_print ("\n usage: gst-videocodec-concurrent-playback --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Build the pipeline
  ret = create_pipe (appctx, stream_cnt);
  if (!ret) {
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
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
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
