/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <linux/input.h>

#define INPUT_EVENT0_DEVICE "/dev/input/event0"
#define POWER_WAKE_LOCK     "/sys/power/wake_lock"
#define POWER_WAKE_UNLOCK   "/sys/power/wake_unlock"

#define POWER_WAKE_ID       "GST_PIPELINE_SUSPEND"

/// Global variables.
static GMainLoop *mloop = NULL;
static gint wake_lock_fd = -1;
static gint wake_unlock_fd = -1;

/// Command line option variables.
static gboolean eos_on_shutdown = FALSE;

static const GOptionEntry entries[] = {
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "Send EOS event before transition from PLAYING to NULL state", NULL
    },
    {NULL}
};

static gboolean
start_pipeline (GstElement * pipeline)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  g_print ("Setting pipeline to PLAYING state ...\n");
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return FALSE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  return TRUE;
}

static gboolean
stop_pipeline (GstElement * pipeline)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  g_print ("Setting pipeline to NULL ...\n");
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to NULL state!\n");
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return FALSE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  return TRUE;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState state = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  // Get the current state of the pipeline.
  gst_element_get_state (pipeline, &state, NULL, 0);

  if (eos_on_shutdown && !waiting_eos && (state == GST_STATE_PLAYING)) {
    g_print ("\nEOS on shutdown enabled -- Forcing EOS on the pipeline\n");

    gst_element_post_message (GST_ELEMENT (pipeline),
        gst_message_new_custom (GST_MESSAGE_EOS, GST_OBJECT (pipeline),
            gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));

    g_print ("\nWaiting for EOS...\n");
    waiting_eos = TRUE;
  } else if (eos_on_shutdown && waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");

    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_main_loop_quit (mloop);

    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_main_loop_quit (mloop);
  }

  return TRUE;
}

static gboolean
handle_event_source (gint fd, GIOCondition condition, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  struct input_event event;
  gsize bytes = 0;
  static gboolean suspended = TRUE;

  if ((bytes = read (fd, &event, sizeof (struct input_event))) == 0)
    return TRUE;

  if (bytes < sizeof (struct input_event)) {
    g_printerr ("Error reading input event\n");
    return TRUE;
  }

  // Skip this event in case it is not supported.
  if (event.type != EV_KEY || event.code != KEY_POWER || event.value != 1)
    return TRUE;

  if (!suspended) {
    GstState state = GST_STATE_VOID_PENDING;

    g_print ("\n'Power Key' Press detected, going to suspend ...\n");

    // Get the current state of the pipeline.
    gst_element_get_state (pipeline, &state, NULL, 0);

    // Quit main loop in case we failed to stop pipeline.
    if (eos_on_shutdown && (state == GST_STATE_PLAYING)) {
      g_print ("EOS on stop enabled -- Sending EOS event on the pipeline\n");

      if (!gst_element_send_event (pipeline, gst_event_new_eos ())) {
        gst_element_set_state (pipeline, GST_STATE_NULL);
        g_main_loop_quit (mloop);

        return TRUE;
      }
    } else if (!stop_pipeline (pipeline)) {
      gst_element_set_state (pipeline, GST_STATE_NULL);
      g_main_loop_quit (mloop);

      return TRUE;
    }

    if (write (wake_unlock_fd, POWER_WAKE_ID, strlen (POWER_WAKE_ID)) == -1) {
      g_print ("Failed to write to wake_unlock, error: %d (%s)\n", errno,
          strerror (errno));

      gst_element_set_state (pipeline, GST_STATE_NULL);
      g_main_loop_quit (mloop);

      return TRUE;
    }

    suspended = TRUE;
  } else {
    g_print ("\n'Power Key' Press detected, going to resume ...\n");

    // Quit main loop in case we failed to start pipeline.
    if (!start_pipeline (pipeline)) {
      gst_element_set_state (pipeline, GST_STATE_NULL);
      g_main_loop_quit (mloop);

      return TRUE;
    }

    if (write (wake_lock_fd, POWER_WAKE_ID, strlen (POWER_WAKE_ID)) == -1) {
      g_print ("Failed to write to wake_lock, error: %d (%s)\n", errno,
          strerror(errno));

      gst_element_set_state (pipeline, GST_STATE_NULL);
      g_main_loop_quit (mloop);

      return TRUE;
    }

    suspended = FALSE;
  }

  return TRUE;
}

static void
request_state_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
  GstState state;

  gst_message_parse_request_state (message, &state);
  g_print ("\nSetting pipeline state to %s as requested by %s...\n",
      gst_element_state_get_name (state), name);

  gst_element_set_state (pipeline, state);
  g_free (name);
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new),
      gst_element_state_get_name (pending));
}

static void
buffering_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  static GstState target_state = GST_STATE_VOID_PENDING;
  static gboolean buffering = FALSE;
  gint percent = 0;

  gst_message_parse_buffering (message, &percent);
  g_print ("\nBuffering... %d%%  \r", percent);

  if (percent == 100) {
    // Clear the BUFFERING status flag.
    buffering = FALSE;

    // Done buffering, if the pending state is playing, go back.
    if (target_state == GST_STATE_PLAYING) {
      g_print ("\nFinished buffering, setting state to PLAYING.\n");
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
    }
  } else {
    // Busy buffering...
    target_state = GST_STATE_TARGET (pipeline);

    if (!buffering && target_state == GST_STATE_PLAYING) {
      // Buffering has been initiated, PAUSE the pipeline.
      g_print ("\nBuffering, setting pipeline to PAUSED state.\n");
      gst_element_set_state (pipeline, GST_STATE_PAUSED);
    }

    buffering = TRUE;
  }
}

static void
progress_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstProgressType type;
  gchar *code = NULL, *text = NULL;

  gst_message_parse_progress (message, &type, &code, &text);
  g_print ("\nProgress: (%s) %s\n", code, text);

  g_free (code);
  g_free (text);
}

static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  g_print ("\n\n");
  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GError *error = NULL;
  gchar *debug = NULL;

  g_print ("\n\n");
  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_quit (mloop);
}

static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  stop_pipeline (pipeline);

  if (gst_message_has_name (message, "GST_PIPELINE_INTERRUPT"))
    g_main_loop_quit (mloop);
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *optsctx = NULL;
  GstBus *bus = NULL;
  GError *error = NULL;

  GstElement *pipeline = NULL;
  guint intrpt_watch_id = 0, event_watch_id = 0;
  gchar **argvn = NULL;
  gint fd = -1;

  g_set_prgname ("gst-warmboot-app");

  // Parse command line entries.
  if ((optsctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;

    g_option_context_add_main_entries (optsctx, entries, NULL);
    g_option_context_add_group (optsctx, gst_init_get_option_group ());

    success = g_option_context_parse (optsctx, &argc, &argv, &error);
    g_option_context_free (optsctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // GST pipeline creation sequence.
  if ((argvn = g_new0 (gchar *, argc)) != NULL) {
    // Create a null-terminated copy of argv[].
    memcpy (argvn, argv + 1, sizeof (gchar *) * (argc - 1));

    // Create a GST pipeline element.
    pipeline = gst_parse_launchv ((const gchar **) argvn, &error);
    g_free (argvn);

    // Check for errors on pipe creation.
    if ((NULL == pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if ((NULL == pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");
      return -1;
    } else if ((pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      gst_object_unref (pipeline);
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to allocate memory for input arguments!\n");
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_object_unref (pipeline);
    return -1;
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");

    g_main_loop_unref (mloop);
    gst_object_unref (pipeline);

    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "message::request-state",
      G_CALLBACK (request_state_cb), pipeline);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::buffering", G_CALLBACK (buffering_cb),
      pipeline);
  g_signal_connect (bus, "message::progress", G_CALLBACK (progress_cb), NULL);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), pipeline);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), pipeline);

  gst_object_unref (bus);

  if ((wake_lock_fd = open (POWER_WAKE_LOCK, O_WRONLY)) == -1) {
    g_print ("Failed to open '%s', error: %d (%s)\n", POWER_WAKE_LOCK,
        errno, strerror (errno));

    g_main_loop_unref (mloop);
    gst_object_unref (pipeline);

    return -1;
  }

  if ((wake_unlock_fd = open (POWER_WAKE_UNLOCK, O_WRONLY)) == -1) {
    g_print ("Failed to open '%s', error: %d (%s)\n", POWER_WAKE_UNLOCK,
        errno, strerror (errno));

    close (wake_unlock_fd);

    g_main_loop_unref (mloop);
    gst_object_unref (pipeline);

    return -1;
  }

  if ((fd = open (INPUT_EVENT0_DEVICE, O_RDONLY)) == -1) {
    g_printerr ("Failed to open '%s', error: %d (%s)\n", INPUT_EVENT0_DEVICE,
        errno, strerror (errno));

    close (wake_unlock_fd);
    close (wake_lock_fd);

    g_main_loop_unref (mloop);
    gst_object_unref (pipeline);

    return -1;
  }

  // Register handing function with the main loop for events data.
  event_watch_id = g_unix_fd_add (
      fd, G_IO_IN | G_IO_PRI, handle_event_source, pipeline);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (
      SIGINT, handle_interrupt_signal, pipeline);

  g_print ("\nPress 'Power Key' to start/stop the pipeline 'Ctrl+C' to quit\n");

  // Run main loop.
  g_main_loop_run (mloop);

  g_source_remove (intrpt_watch_id);
  g_source_remove (event_watch_id);

  close (fd);
  close (wake_unlock_fd);
  close (wake_lock_fd);

  g_main_loop_unref (mloop);
  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;
}
