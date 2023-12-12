/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Application for Audio Decoder
*
* Description:
* This application Decodes the audio i.e. mp3 and raw
*
* help:
* gst-audio-decode-example --help
*
* Usage:
* For mp3: gst-audio-decode-example -i path/<filename>.mp3 -f 1
* For wavefile: gst-audio-decode-example -i path/<filename>.wav -f 2
*
* *******************************************************************
* Pipeline used for wavfile: filesrc->wavparse->pulsesink
* Pipeline used for mp3: filesrc->mpegaudioparse->mpg123audiodec->pulsesink
* *******************************************************************
*/

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define GST_APP_SUMMARY                                                       \
  "This audio decode app enables the users to decode audio i.e wav "          \
  "file or mp3.\n"                                                            \
  "For mp3: gst-audio-decode-example -i /opt/<filename>.mp3  -f 1 \n" \
  "For wav: gst-audio-decode-example -i /opt/<filename>.wav  -f 2"

// Enum to define the type of audio codec that user can set
enum GstAudioCodecType {
  GST_ADEFAULT,
  GST_MP3,
  GST_WAV,
};

// Structure to hold the application context
struct GstAudioAppContext : GstAppContext {
  gchar *input_file;
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
  ctx->input_file = NULL;
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

  if (appctx->input_file != NULL)
    g_free (appctx->input_file);

  if (appctx != NULL)
    g_free (appctx);
}

// Function to create the pipeline and link all elements
static gboolean
create_pipe (GstAudioAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *pipeline, *filesrc, *parse, *decoder, *audiosink;
  gboolean ret = FALSE;
  appctx->plugins = NULL;

  // Create the source element
  filesrc = gst_element_factory_make ("filesrc", "source");

  // Set the location property of the source element
  g_object_set (G_OBJECT (filesrc), "location", appctx->input_file, NULL);

  // Create the audio sink element
  audiosink = gst_element_factory_make ("pulsesink", "audiosink");

  // Depending on the format, create the parser and decoder elements, and link all elements
  if (appctx->format == GST_WAV) {
    parse = gst_element_factory_make ("wavparse", "parse");

    // Check if all elements are created successfully
    if (!filesrc || !parse || !audiosink) {
      g_printerr ("\n One element could not be created. Exiting.\n");
      return FALSE;
    }
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc, parse, audiosink,
        NULL);

    g_print ("\n Linking All elements ..\n");
    ret = gst_element_link_many (filesrc, parse, audiosink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, parse,
          audiosink, NULL);
      return FALSE;
    }
  } else if (appctx->format == GST_MP3) {
    parse = gst_element_factory_make ("mpegaudioparse", "parse");
    decoder = gst_element_factory_make ("mpg123audiodec", "decoder");

    // Check if all elements are created successfully
    if (!filesrc || !parse || !decoder || !audiosink) {
      g_printerr ("\n One element could not be created. Exiting.\n");
      return FALSE;
    }
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc, parse, decoder,
        audiosink, NULL);

    g_print ("\n Linking All elements ..\n");
    ret = gst_element_link_many (filesrc, parse, decoder, audiosink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, parse, decoder,
          audiosink, NULL);
      return FALSE;
    }
  }

  // Append all elements to the plugins list
  appctx->plugins = g_list_append (appctx->plugins, filesrc);
  appctx->plugins = g_list_append (appctx->plugins, parse);
  appctx->plugins = g_list_append (appctx->plugins, audiosink);
  if (appctx->format == GST_MP3)
    appctx->plugins = g_list_append (appctx->plugins, decoder);

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
  GstAudioAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-audio-decode-example --help \n");
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
       "\n\t1-MP3"
       "\n\t2-WAV"
      },
      {"input_file", 'i', 0, G_OPTION_ARG_STRING, &appctx->input_file,
       "Input Filename , \
       -i /opt/<audiofile>"
      },
      {NULL}
  };

  // Parse the command line entries
  if ((ctx = g_option_context_new ("gst-audio-decode-example")) != NULL) {
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

  // Check the input parameters from the user
  if (appctx->format < GST_MP3 || appctx->format > GST_WAV ||
      appctx->input_file == NULL) {
    g_printerr ("\n one of input parameters is not given -f %d -i %s\n",
        appctx->format, appctx->input_file);
    g_print ("\n usage: gst-audio-video-playback --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-audio-decode-example");

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
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
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
