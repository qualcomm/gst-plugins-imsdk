/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Application for Audio Encoder
*
* Description:
* This application Encodes the audio in flac or wav format
*
* help:
* gst-audio-encode-example --help
*
* Usage:
* For flac audio format:
* gst-audio-encode-example -o location/<filename>.flac --audio_format=1
* For wav audio format:
* gst-audio-encode-example -o location/<filename>.wav --audio_format=2
*
* *******************************************************************
* Pipeline for wav: pulsesrc->audioconvert->wavenc->filesink
* Pipeline for flac: pulsesrc->capsfilter->audioconvert->flacenc->filesink
* *******************************************************************
*/

#include <glib-unix.h>
#include <glib.h>
#include <stdio.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define GST_APP_SUMMARY                                                      \
  "This app enables the users to encode audio i.e. wav or flac "             \
  "format.\n"                                                                \
  "flac:gst-audio-encode-example -o /opt/<filename>.flac --audio_format=1 \n" \
  "wav: gst-audio-encode-example -o /opt/<filename>.wav  --audio_format=2"

// Enum to define the type of audio codec that user can set
enum GstAudioCodecType {
  GST_ADEFAULT,
  GST_FLAC,
  GST_WAV,
};

// Structure to hold the application context
struct GstAudioAppContext : GstAppContext {
  gchar *output_file;
  GstAudioCodecType format;
};

// Function to create a new application context
static GstAudioAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstAudioAppContext *ctx = (GstAudioAppContext *) g_new0 (GstAudioAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context\n");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->output_file = NULL;
  ctx->format = GST_ADEFAULT;

  return ctx;
}

// Function to free the application context
static void
gst_app_context_free (GstAudioAppContext * appctx)
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

  if (appctx->output_file != NULL)
    g_free (appctx->output_file);

  // Finally, free the application context itself
  if (appctx != NULL)
    g_free (appctx);
}

// Function to create the pipeline and link all elements
static gboolean
create_pipe (GstAudioAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *pipeline, *pulsesrc, *main_capsfilter, *encoder, *audioconvert,
      *filesink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  appctx->plugins = NULL;

  g_print ("\n Audio Encoding i.e. %s \n", appctx->output_file);

  // Create the source element
  pulsesrc = gst_element_factory_make ("pulsesrc", "pulsesrc");

  // Depending on the format, create the audio encoder and capsfilter for flac
  if (appctx->format == GST_WAV)
    encoder = gst_element_factory_make ("wavenc", "wavenc");
  else {
    encoder = gst_element_factory_make ("flacenc", "encoder");
    main_capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

    filtercaps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING,
        "S16LE", "rate", G_TYPE_INT, 48000, "channels", G_TYPE_INT, 1, NULL);
    g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);

    gst_caps_unref (filtercaps);

    if (!main_capsfilter) {
      g_printerr ("\n main_capsfilter element could be created.\n");
      return FALSE;
    }
  }

  // create the audioconvert element
  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");

  // create the filesink elemnet to store the encoded audio
  filesink = gst_element_factory_make ("filesink", "filesink");

  // Set the location property of the source element
  g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

  // Check if all elements are created successfully
  if (!pulsesrc || !encoder || !audioconvert || !filesink) {
    g_printerr ("\n Not all elements could be created.\n");
    return FALSE;
  }

  // add objects to the main pipeline and link src to sink elemnets
  if (appctx->format == GST_WAV) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), pulsesrc, audioconvert,
        encoder, filesink, NULL);
    ret = gst_element_link_many (pulsesrc, audioconvert, encoder, filesink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), pulsesrc, audioconvert,
          encoder, filesink, NULL);
      return FALSE;
    }
  } else {
    gst_bin_add_many (GST_BIN (appctx->pipeline), pulsesrc, main_capsfilter,
        audioconvert, encoder, filesink, NULL);
    ret = gst_element_link_many (pulsesrc, main_capsfilter, audioconvert,
        encoder, filesink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), pulsesrc, audioconvert,
          main_capsfilter, encoder, filesink, NULL);
      return FALSE;
    }
  }

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, pulsesrc);
  appctx->plugins = g_list_append (appctx->plugins, audioconvert);
  appctx->plugins = g_list_append (appctx->plugins, encoder);
  appctx->plugins = g_list_append (appctx->plugins, filesink);
  if (appctx->format == GST_FLAC)
    appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);

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
  GstAudioAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-audio-encode-example --help \n");
    return -1;
  }

  // Create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure the input parameters
  GOptionEntry entries[] = {
      {"audio_format", 'f', 0, G_OPTION_ARG_INT, &appctx->format,
       "\t\t\t  format",
       "\n\t1-FLAC"
       "\n\t2-WAV"
      },
      {"output_file", 'o', 0, G_OPTION_ARG_STRING, &appctx->output_file,
       "Output Filename , \
       -o /opt/<audiofile>"
      },
      {NULL}
  };

  // Parse the command line entries
  if ((ctx = g_option_context_new ("gst-audio-encode-example")) != NULL) {
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

// check for input parameters from user
  if (appctx->format < GST_FLAC || appctx->format > GST_WAV ||
      appctx->output_file == NULL) {
    g_printerr ("\n one of input parameters is not given -f %d -i %s\n",
        appctx->format, appctx->output_file);
    g_print ("\n usage: gst-audio-encode-example --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-audio-encode-example");

  // Create the empty pipeline
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
    g_main_loop_unref (mloop);
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
  g_print ("\n Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      break;
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
