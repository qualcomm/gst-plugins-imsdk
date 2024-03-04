/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Application for Audio Video Decoder.
*
* Description:
* This is an application to demonstrate to use of audio video playback.
* In this example we are decoding the AVC/HEVC video codec
* and FLAC/mp3 audio codec content.
*
* Help:
* gst-audio-video-playback --help
*
* Usage:
* gst-audio-video-playback [option]
*
* ****************************************************************************************
* Audio Video Playback Pipeline:
*                   |->queue->h264parse/h265parse->v4l2h264dec/v4l2h265dec->waylandsink
* filesrc->qtdemux->|
*                   |->queue->flacparse/mpegaudioparse->flacdec/mpg123audiodec->pulsesink
* ****************************************************************************************
*
*/

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define GST_APP_SUMMARY \
  "This app enables the user to Decode a Audio Video mp4 file show the output \
  on the display E.g: gst-audio-video-playback -v 1 -a 1 -i <filename>.mp4"

// Structure to hold the application context
struct GstVideoAppContext : GstAppContext {
  gchar *input_file;
  GstVideoPlayerCodecType vc_format;
  GstAudioPlayerCodecType ac_format;
};

/**
 * Link video and audio track to demux dynamically
 *
 * @param Gst element pointer
 * @param Gst pad pointer
 * @param data pointer
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");

  // Link the source pad to the sink pad
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
}

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstVideoAppContext*
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstVideoAppContext *ctx = (GstVideoAppContext *) g_new0 (GstVideoAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("Unable to create App Context");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->input_file = NULL;
  ctx->vc_format = GST_VCODEC_NONE;
  ctx->ac_format = GST_ACODEC_NONE;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstVideoAppContext *appctx)
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

  // If the main loop is not NULL, unref it
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
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin and connect pad signal
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_pipe (GstVideoAppContext *appctx)
{
  // Declare the elements of the pipeline
  GstElement *filesrc, *qtdemux, *queue1, *vparse, *vdecoder, *queue2, *aparse,
      *adecoder, *pulsesink, *vsink;
  gboolean ret = FALSE;

  // Create Source element for reading from a file
  filesrc = gst_element_factory_make ("filesrc", "filesrc");

  // Create Demuxer element to get audio and video tracks
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");

  // create the video decoder and parse element based on codec type
  if (appctx->vc_format == GST_VCODEC_AVC) {
    g_print ("Creating the AVC...\n");
    vparse = gst_element_factory_make ("h264parse", "vparse");
    vdecoder = gst_element_factory_make ("v4l2h264dec", "vdecoder");
  } else if (appctx->vc_format == GST_VCODEC_HEVC) {
    g_print ("Creating the HEVC...\n");
    vparse = gst_element_factory_make ("h265parse", "vparse");
    vdecoder = gst_element_factory_make ("v4l2h265dec", "vdecoder");
  }

  // create the audio decoder and parse element based on codec type
  if (appctx->ac_format == GST_ACODEC_FLAC) {
    g_print ("Creating the FLAC...\n");
    aparse = gst_element_factory_make ("flacparse", "aparse");
    adecoder = gst_element_factory_make ("flacdec", "adecoder");
  } else if (appctx->ac_format == GST_ACODEC_MP3) {
    g_print ("Creating the MP3...\n");
    aparse = gst_element_factory_make ("mpegaudioparse", "aparse");
    adecoder = gst_element_factory_make ("mpg123audiodec", "adecoder");
  }

  // Create the queue elements for buffering data
  queue1 = gst_element_factory_make ("queue", "queue1");
  queue2 = gst_element_factory_make ("queue", "queue2");

  // Create the sink elements for output
  pulsesink = gst_element_factory_make ("pulsesink", "pulsesink");
  vsink = gst_element_factory_make ("waylandsink", "vsink");

  // Check if all elements are created successfully
  if (!filesrc || !qtdemux || !queue1 || !vparse || !vdecoder || !queue2 ||
      !aparse || !adecoder || !pulsesink || !vsink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, filesrc);
  appctx->plugins = g_list_append (appctx->plugins, qtdemux);
  appctx->plugins = g_list_append (appctx->plugins, queue1);
  appctx->plugins = g_list_append (appctx->plugins, vparse);
  appctx->plugins = g_list_append (appctx->plugins, vdecoder);
  appctx->plugins = g_list_append (appctx->plugins, queue2);
  appctx->plugins = g_list_append (appctx->plugins, aparse);
  appctx->plugins = g_list_append (appctx->plugins, adecoder);
  appctx->plugins = g_list_append (appctx->plugins, pulsesink);
  appctx->plugins = g_list_append (appctx->plugins, vsink);

  // Set decoder properties
  g_object_set (G_OBJECT (vdecoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (vdecoder), "output-io-mode", 5, NULL);

  // Set location
  g_object_set (G_OBJECT (filesrc), "location", appctx->input_file, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (vsink), "sync", false, NULL);
  g_object_set (G_OBJECT (vsink), "fullscreen", true, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, queue1, vparse,
      vdecoder, queue2, aparse, adecoder, pulsesink, vsink, NULL);

  // Linking the src and demux element
  g_print ("Linking the streams elements...\n");
  ret = gst_element_link (filesrc, qtdemux);
  if (!ret) {
    g_printerr ("Pipeline elements(src) cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, vsink, queue1,
        vparse, vdecoder, queue2, aparse, adecoder, pulsesink, NULL);
    return FALSE;
  }

  // Linking video streams
  ret = gst_element_link_many (queue1, vparse, vdecoder, vsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements(queue1) cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, vsink, queue1,
        vparse, vdecoder, queue2, aparse, adecoder, pulsesink, NULL);
    return FALSE;
  }

  // Linking audio streams
  ret = gst_element_link_many (queue2, aparse, adecoder, pulsesink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements(queue2) cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, vsink, queue1,
        vparse, vdecoder, queue2, aparse, adecoder, pulsesink, NULL);
    return FALSE;
  }

  // link demux video track pad to video queue
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), queue1);

  // link demux audio track pad to audio queue
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), queue2);

  g_print ("All elements are linked successfully\n");

  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstVideoAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-audio-video-playback --help \n");
    return -1;
  }

  // Create the application context
  appctx = gst_app_context_new ();
  if (NULL == appctx) {
    g_printerr ("Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "video_codec", 'v', 0,
      G_OPTION_ARG_INT, &appctx->vc_format,
      "Select Video codec type -v 1 (AVC) or -v 2 (HEVC)"
      "  e.g. -v 1 or -v 2 "
    },
    { "audio_codec", 'a', 0,
      G_OPTION_ARG_INT, &appctx->ac_format,
      "Select Audio codec type -a 1 (FLAC) or -a 2 (MP3)"
      "  e.g. -a 1 or -a 2"
    },
    { "input_file", 'i', 0,
      G_OPTION_ARG_FILENAME, &appctx->input_file,
      "Input Filename - i/p mp4 file path and name"
      "  e.g. -i /opt/<file_name>.mp4"
    },
    { NULL }
  };

  // Parse the command line entries
  if ((ctx = g_option_context_new ("gst-audio-video-playback")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_set_summary (ctx, GST_APP_SUMMARY);
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
  if (appctx->vc_format < GST_VCODEC_AVC || appctx->vc_format > GST_VCODEC_HEVC ||
      appctx->ac_format < GST_ACODEC_FLAC || appctx->ac_format > GST_ACODEC_MP3 ||
      appctx->input_file == NULL) {
    g_printerr ("\n one of input parameters is not given -v %d -a %d -i %s\n",
        appctx->vc_format, appctx->ac_format, appctx->input_file);
    g_print ("\n usage: gst-audio-video-playback --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  g_set_prgname ("gst-Audio-Video-Playback");

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create empty pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (appctx);
  if (!ret) {
    g_printerr ("failed to create GST pipe.\n");
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
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
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

  // Set the pipeline to the NULL state
  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
