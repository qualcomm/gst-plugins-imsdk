/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer application for transcoding the user input video file from
 * AVC to HEVC codec and viceversa.
 *
 * Description:
 * This is an application to demonstrate video transcoding.
 * Application can accept the user input file of video codec AVC or HEVC
 * and transcode to other codec format.
 *
 * Usage:
 * For Transcoding AVC to HEVC:
 * gst-video-transcode-example -i /opt/avc.mp4 -c 1 -o /opt/hevc.mp4
 * For Transcoding HEVC to AVC:
 * gst-video-transcode-example -i /opt/hevc.mp4 -c 2 -o /opt/avc.mp4
 *
 * Help:
 * gst-video-transcode-example --help
 *
 * *******************************************************************
 * Pipeline for Transcoding AVC to HEVC:
 * filesrc->qtdemux->queue->h264parse->v4l2h264dec->v4l2h265enc->|
 *                                    _ _ _ _ _ _ _ _ _ _ _ _ _ _|
 *                                  |
 *                                  |->h265parse->mp4mux->filesink
 *
 * Pipeline for Transcoding HEVC to AVC:
 * filesrc->qtdemux->queue->h265parse->v4l2h265dec->v4l2h264enc->|
 *                                    _ _ _ _ _ _ _ _ _ _ _ _ _ _|
 *                                  |
 *                                  |->h264parse->mp4mux->filesink
 *
 * *******************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_OUTPUT_FILENAME "/opt/transcoded_video.mp4"

#define GST_APP_SUMMARY "This application is designed to showcase video "\
  "transcoding capabilities. It can accept user input files encoded in" \
  "either AVC or HEVC video codecs and transcode them into either HEVC " \
  "or AVC format.\n" \
  "\nCommand:\n" \
  "For AVC to HEVC transcode\n" \
  "  gst-video-transcode-example -i /opt/avc.mp4 -c 1 -o /opt/hevc.mp4 \n" \
  "For HEVC to AVC transcode\n" \
  "  gst-video-transcode-example -i /opt/hevc.mp4 -c 2 -o /opt/avc.mp4 \n" \
  "\nOutput:\n" \
  "  Upon execution, application will generates output mp4 file at given path"

// Structure to hold the application context
struct GstTranscodeAppContext : GstAppContext {
  gchar *input_file;
  gchar *output_file;
  GstVideoPlayerCodecType input_format;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstTranscodeAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstTranscodeAppContext *ctx = (GstTranscodeAppContext *)
      g_new0 (GstTranscodeAppContext, 1);

  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context\n");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->plugins = NULL;
  ctx->mloop = NULL;
  ctx->input_file = NULL;
  ctx->output_file = DEFAULT_OUTPUT_FILENAME;
  ctx->input_format = GST_VCODEC_NONE;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx application context object
 */
static void
gst_app_context_free (GstTranscodeAppContext * appctx)
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

  if (appctx->input_file != NULL)
    g_free (appctx->input_file);

  if (!appctx->output_file && appctx->output_file != DEFAULT_OUTPUT_FILENAME)
    g_free (appctx->output_file);

  if (appctx != NULL)
    g_free (appctx);
}
/**
 * Function to link the dynamic video pad of demux to queue:
 *
 * @param element GStreamer source element
 * @param pad GStreamer source element pad
 * @param data sink element object
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;
  GstPadLinkReturn ret;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");

  // Link the source pad to the sink pad
  ret = gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx application context object.
 */
static gboolean
create_pipe (GstTranscodeAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *filesrc, *qtdemux, *queue, *encoder, *enc_parse, *dec_parse,
      *decoder, *mp4mux, *filesink;
  GstCaps *filtercaps;
  GstStructure *fcontrols;
  gboolean ret = FALSE;

  // Create file source element for transcoding
  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  g_object_set (G_OBJECT (filesrc), "location", appctx->input_file, NULL);

  // create qtdemux for demuxing the filesrc
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");

  // create queue element for processing
  queue = gst_element_factory_make ("queue", "queue");

  // create elements based on input format
  if (appctx->input_format == GST_VCODEC_AVC) {
    // Create H264 decoder and parser element
    decoder = gst_element_factory_make ("v4l2h264dec", "decoder");
    dec_parse = gst_element_factory_make ("h264parse", "dec_parse");

    // Create H265 encoder and parser element
    encoder = gst_element_factory_make ("v4l2h265enc", "encoder");
    enc_parse = gst_element_factory_make ("h265parse", "enc_parse");

  } else if (appctx->input_format == GST_VCODEC_HEVC) {
    // Create H265 decoder and parser element
    decoder = gst_element_factory_make ("v4l2h265dec", "decoder");
    dec_parse = gst_element_factory_make ("h265parse", "dec_parse");

    // Create H264 encoder and parser element
    encoder = gst_element_factory_make ("v4l2h264enc", "encoder");
    enc_parse = gst_element_factory_make ("h264parse", "enc_parse");
  }

  // Set encoder and decoder element properties
  g_object_set (G_OBJECT (decoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (decoder), "output-io-mode", 5, NULL);
  g_object_set (G_OBJECT (encoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (encoder), "output-io-mode", 5, NULL);
  fcontrols = gst_structure_from_string (
      "fcontrols,video_bitrate_mode=0", NULL);
  g_object_set (G_OBJECT (encoder), "extra-controls", fcontrols, NULL);

  // Create mp4mux element for muxing the stream
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

  // Create filesink element for storing the encoding stream
  filesink = gst_element_factory_make ("filesink", "filesink");

  // Check if all elements are created successfully
  if (!filesrc || !qtdemux || !queue || !dec_parse || !enc_parse || !decoder ||
      !encoder || !mp4mux || !filesink) {
    g_printerr ("\n One element could not be created. Exiting app.\n");
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, filesrc);
  appctx->plugins = g_list_append (appctx->plugins, qtdemux);
  appctx->plugins = g_list_append (appctx->plugins, queue);
  appctx->plugins = g_list_append (appctx->plugins, dec_parse);
  appctx->plugins = g_list_append (appctx->plugins, enc_parse);
  appctx->plugins = g_list_append (appctx->plugins, decoder);
  appctx->plugins = g_list_append (appctx->plugins, encoder);
  appctx->plugins = g_list_append (appctx->plugins, mp4mux);
  appctx->plugins = g_list_append (appctx->plugins, filesink);

  // Set filesink_enc properties
  g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

  // Add elements to the pipeline and link them
  g_print ("\n Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, queue, dec_parse,
      enc_parse, decoder, encoder, mp4mux, filesink, NULL);

  g_print ("\n Link filesrc and qtdemux elements...\n");
  // Link the display stream
  ret = gst_element_link (filesrc, qtdemux);
  if (!ret) {
    g_printerr ("filesrc and qtdemux elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, queue,
        dec_parse, decoder, encoder, enc_parse, mp4mux, filesink, NULL);
    return FALSE;
  }

  g_print ("\n Link decoder and encoder elements...\n");
  ret = gst_element_link_many (queue, dec_parse, decoder, encoder, enc_parse,
      mp4mux, filesink, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, queue,
        dec_parse, decoder, encoder, enc_parse, mp4mux, filesink, NULL);
    return FALSE;
  }
  // link demux video pad to queue
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), queue);

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
  GstTranscodeAppContext *appctx = NULL;
  guint intrpt_watch_id = 0;

  // if user gives only app name print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-video-transcode-example --help \n");
    return -1;
  }

  // create the app context
  appctx = gst_app_context_new ();
  if (NULL == appctx) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "input_file", 'i', 0, G_OPTION_ARG_FILENAME, &appctx->input_file,
      "Input Filename - i/p AVC/HEVC mp4 file path and name \
          -i /opt/<h264_file/h265_file>.mp4" },
    { "input_codec", 'c', 0, G_OPTION_ARG_INT, &appctx->input_format,
      "Input codec type - AVC/HEVC \
       -c 1(AVC)/2(HEVC)" },
    { "output_file", 'o', 0, G_OPTION_ARG_FILENAME, &appctx->output_file,
      "Output Filename - o/p filename & path where user want to \
        store AVC/HEVC stream \
        -o /opt/<h264_file/h265_file>.mp4 " },
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

  // check for input parameters from user
  if (appctx->input_file == NULL || appctx->input_format < GST_VCODEC_AVC ||
      appctx->input_format > GST_VCODEC_HEVC) {
    g_printerr ("\n one of input parameters is not correct \n");
    g_print ("\n usage: gst-video-transcode-example --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("gst-video-transcode-example");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->pipeline = pipeline;

  // Build the pipeline
  if (!create_pipe (appctx)) {
    g_printerr ("failed to create GST Transcode pipe.\n");
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

  // Start playing
  if (appctx->input_format == GST_VCODEC_AVC) {
    g_print ("\n Transcoding to hevc format \n");
  } else {
    g_print ("\n Transcoding to avc format\n");
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("\n Failed to retrieve pipeline bus!\n");
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
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      if(intrpt_watch_id)
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

  g_print ("\n Application is running...");
  g_main_loop_run (mloop);

  if(intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("\n Transcoded video file will be stored at %s\n",
      appctx->output_file);

  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
