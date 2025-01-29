/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application:
 * Gstreamer Application for audio video encode
 *
 * Description:
 * This is an application to demonstrate the Audio Video Encode
 * and store into the user provided file.
 *
 * Usage:
 * For AVC:Audio Video Encode:
 * gst-audio-video-encode -w 1920 -h 1080 -c 1 -o /opt/audiovideo.mp4
 * For HEVC:Audio Video Encode:
 * gst-audio-video-encode -w 1920 -h 1080 -c 2 -o /opt/audiovideo.mp4
 *
 * Help:
 * gst-audio-video-encode --help
 *
 * **********************************************************************************
 * Pipeline for AVC:Audio Video Encode:
 * qtiqmmfsrc->capsfilter->v4l2h264enc->queue->h264parse->|
 *                                                        |->mp4mux->queue->filesink
 * pulsesrc->capsfilter->audioconvert->queue->lamemp3enc->|
 *
 * Pipeline for HEVC:Audio Video Encode:
 * qtiqmmfsrc->capsfilter->v4l2h265enc->queue->h265parse->|
 *                                                        |->mp4mux->queue->filesink
 * pulsesrc->capsfilter->audioconvert->queue->lamemp3enc->|

 ************************************************************************************
 */

#include <glib-unix.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_OUTPUT_FILENAME "/opt/audiovideo.mp4"
#define DEFAULT_OUTPUT_WIDTH 1280
#define DEFAULT_OUTPUT_HEIGHT 720

#define GST_PIPELINE_AUDIO_VIDEO_AVC \
  "qtiqmmfsrc name=qmmf ! capsfilter name=caps ! \
  queue ! v4l2h264enc capture-io-mode=4 output-io-mode=5 ! queue ! h264parse ! \
  muxer. pulsesrc do-timestamp=true provide-clock=false volume=10 ! \
  audio/x-raw,format=S16LE,channels=1,rate=48000 ! audioconvert ! queue ! \
  lamemp3enc ! muxer. mp4mux name=muxer ! queue ! filesink name=mp4sink \
  location=DEFAULT_OUTPUT_FILENAME"

#define GST_PIPELINE_AUDIO_VIDEO_HEVC \
  "qtiqmmfsrc name=qmmf ! capsfilter name=caps ! \
  queue ! v4l2h265enc capture-io-mode=4 output-io-mode=5 ! queue ! h265parse ! \
  muxer. pulsesrc do-timestamp=true provide-clock=false volume=10 ! \
  audio/x-raw,format=S16LE,channels=1,rate=48000 ! audioconvert ! queue ! \
  lamemp3enc ! muxer. mp4mux name=muxer ! queue ! filesink name=mp4sink \
  location=DEFAULT_OUTPUT_FILENAME"

#define GST_APP_SUMMARY \
  "This Application will execute the usecase of AudioVideo Encode"     \
  "\nCommand:" \
  "\nFor AVC: Audio Video Encode:\n"                                   \
  "gst-audio-video-encode -w 1920 -h 1080 -c 1 -o /opt/audiovideo.mp4" \
  "\nFor HEVC: Audio Video Encode:\n"                                  \
  "gst-audio-video-encode -w 1920 -h 1080 -c 2 -o /opt/audiovideo.mp4" \
  "\nOutput\n:" \
  "Upon executing the application user finds encoded file in output location" \

// Structure to hold the application context
struct GstAudioVideoAppContext : GstAppContext {
  gchar *output_file;
  gint width;
  gint height;
  GstVideoPlayerCodecType input_format;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstAudioVideoAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstAudioVideoAppContext *ctx = (GstAudioVideoAppContext *)
      g_new0 (GstAudioVideoAppContext, 1);

  if (NULL == ctx) {
    g_printerr ("Unable to create App Context");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->width = DEFAULT_OUTPUT_WIDTH;
  ctx->height = DEFAULT_OUTPUT_HEIGHT;
  ctx->input_format = GST_VCODEC_AVC;
  ctx->output_file = const_cast<gchar *> (DEFAULT_OUTPUT_FILENAME);

  return ctx;
}

/**
 * Free Application context:
 *
 * @param ctx application context object
 */
static void
gst_app_context_free (GstAudioVideoAppContext * ctx)
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

  if (ctx->output_file != NULL &&
    ctx->output_file != (gchar *)(&DEFAULT_OUTPUT_FILENAME))
    g_free ((gpointer)ctx->output_file);

  g_free ((gpointer)ctx);
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create the GST pipeline
 * 2. Set width and height for camera plugin
 * 3. Set location Paramters for sink plugin
 *
 * @param appctx application context object.
 */
static gboolean
create_pipe (GstAudioVideoAppContext * appctx)
{
  GError *error = NULL;
  GstElement *sink = NULL;
  GstElement *caps = NULL;
  GstCaps *filtercaps;

  // Initiate an pipeline for Audio Video Encode based on user Input
  switch (appctx->input_format) {
    case GST_VCODEC_AVC:
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_AUDIO_VIDEO_AVC, &error);
      break;
    case GST_VCODEC_HEVC:
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_AUDIO_VIDEO_HEVC, &error);
      break;
    default:
      g_printerr ("Pipeline couldn't be created,invalid video codec type: %d\n",
          appctx->input_format);
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

  // Set the capabilities for camera
  caps = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "caps");
  if (caps != NULL) {
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, appctx->width,
        "height", G_TYPE_INT, appctx->height,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        "interlace-mode", G_TYPE_STRING, "progressive",
        "colorimetry", G_TYPE_STRING, "bt601",
        NULL);
    g_object_set (G_OBJECT (caps), "caps", filtercaps, NULL);
    gst_object_unref (caps);
    gst_object_unref (filtercaps);
  } else {
    g_printerr ("Couldn't find filtercaps \n");
    return FALSE;
  }

  // Set output file path
  sink = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "mp4sink");
  if (sink != NULL) {
    g_object_set (G_OBJECT (sink), "location", appctx->output_file, NULL);
    gst_object_unref (sink);
  } else {
    g_printerr ("Couldn't find the filesink \n");
    return FALSE;
  }

  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstAudioVideoAppContext *appctx = NULL;
  guint intrpt_watch_id = 0;

  // Create the application context
  appctx = gst_app_context_new ();
  if (NULL == appctx) {
    g_printerr ("Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "width", "camera width" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height,
      "height", "camera height" },
    { "input_videocodec", 'c', 0, G_OPTION_ARG_INT, &appctx->input_format,
      "input video codec",
      "-c 1(AVC)/2(HEVC)" },
    { "output_file", 'o', 0, G_OPTION_ARG_STRING, &appctx->output_file,
      "output filename",
      "e.g. -o /opt/audiovideo.mp4" },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
    };

  // Parse the command line entries
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
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
    g_printerr ("Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("Failed to retrieve pipeline bus!\n");
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
  // Move application state to PLAYING state in state_changed_cb function
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
  g_print ("\n Application is running...i.e Audio Video Encode File %s \n", appctx->output_file);
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  g_print ("\n Audio video recorded file will be stored at %s\n",
      appctx->output_file);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
