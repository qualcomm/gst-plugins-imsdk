/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <glib-unix.h>
#include <stdlib.h>


#include <gst/gst.h>

#define DASH_LINE   "----------------------------------------------------------------------"

#define QUIT_OPTION                    "q"
#define MENU_BACK_OPTION               "b"

#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE "APP_PIPELINE_STATE_MSG"
#define PIPELINE_EOS_MESSAGE   "APP_PIPELINE_EOS_MSG"
#define STDIN_MESSAGE          "APP_STDIN_MSG"

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppContext*)(obj))

#define GST_APP_SUMMARY \
  "This application enables users to create and utilize a video pipeline " \
  "for playback. It provides essential playback features such as play, " \
  "pause, fast forward, and rewind.\n" \
  "To use this application effectively, users should have knowledge of " \
  "pipeline construction in GStreamer.\n" \
  "\nCommand:\n" \
  "AVC Video Codec Playback:\n" \
  "  gst-video-playback-example -e filesrc location=<avc_file>.mp4 ! qtdemux ! "\
  "queue ! h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! "\
  "waylandsink enable-last-sample=false async=false fullscreen=true \n" \
  "HEVC Video Codec Playback:\n" \
  "  gst-video-playback-example -e filesrc location=<hevc_file>.mp4 ! qtdemux ! "\
  "queue ! h265parse ! v4l2h265dec capture-io-mode=5 output-io-mode=5 ! "\
  "waylandsink enable-last-sample=false async=false fullscreen=true \n" \
  "\nOutput:\n" \
  "  Upon executing the application, user will observe video content " \
  "displayed on the screen, \n" \

// TODO: Add stop feature.

typedef enum {
  GST_PLAY_OPTION = 1,
  GST_PAUSE_OPTION,
  GST_FAST_FORWARD_OPTION,
  GST_REWIND_OPTION
} GstMainMenuOption;

typedef enum {
  GST_TIME_BASED = 1,
  GST_SPEED_BASED
} GstFFRMenuOption;

typedef struct {
  // Main application event loop.
  GMainLoop   *mloop;

  // GStreamer pipeline instance.
  GstElement  *pipeline;

  // Asynchronous queue for thread communication.
  GAsyncQueue *messages;

  // Current playback speed.
  gdouble rate;

  // Whether app is waiting for eos.
  gboolean waiting_eos;
} GstAppContext;

static gboolean eos_on_shutdown = FALSE;

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = NULL;
  g_return_val_if_fail ((ctx = g_new0 (GstAppContext, 1)) != NULL, NULL);

  ctx->mloop = NULL;
  ctx->pipeline = NULL;
  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->rate = 1.0;
  ctx->waiting_eos = FALSE;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  g_async_queue_unref (ctx->messages);

  g_free (ctx);
  return;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);

  // Send EOS first. If another interrupt while waiting, force close.
  if (!appctx->waiting_eos) {
    // Signal menu thread to exit.
    g_async_queue_push (appctx->messages,
        gst_structure_new_empty (TERMINATE_MESSAGE));
    g_print ("\nTerminating menu thread ...\n");
  } else {
    g_print ("Interrupt while waiting for EOS, exiting...\n");
    g_async_queue_push (appctx->messages,
        gst_structure_new_empty (PIPELINE_EOS_MESSAGE));
  }

  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  static GstState target_state = GST_STATE_VOID_PENDING;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));

      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      break;
    }
    case GST_MESSAGE_EOS:
    {
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (PIPELINE_EOS_MESSAGE));

      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

      break;
    }
    case GST_MESSAGE_REQUEST_STATE:
    {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting pipeline state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (appctx->pipeline, state);
      target_state = state;

      g_free (name);

      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState oldstate, newstate, pending;

      // Handle state changes only for the pipeline.
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
        break;

      gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
      g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
          gst_element_state_get_name (oldstate),
          gst_element_state_get_name (newstate),
          gst_element_state_get_name (pending));

      g_async_queue_push (appctx->messages, gst_structure_new (
          PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, newstate,
          "pending", G_TYPE_UINT, pending, NULL));

      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gchar *input = NULL;
  GIOStatus status = G_IO_STATUS_NORMAL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);

      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("ERROR: Unknown error!\n");

      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  if (strlen (input) > 1)
    input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

static gboolean
wait_stdin_message (GAsyncQueue * queue, gchar ** input)
{
  GstStructure *message = NULL;

  // Clear input from previous use.
  g_free (*input);
  *input = NULL;

  // Block the thread until there's no input from the user or eos/error msg occurs.
  while ((message = (GstStructure *)g_async_queue_pop (queue)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE) ||
        gst_structure_has_name (message, PIPELINE_EOS_MESSAGE)) {
      gst_structure_free (message);
      // Returning FALSE will cause menu thread to terminate.
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }

    // Clear message to terminate the loop after having popped the data.
    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_eos_message (GAsyncQueue * messages)
{
  GstStructure *message = NULL;

  // Wait for either a PIPELINE_EOS or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_EOS_MESSAGE))
      break;

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_state_message (GAsyncQueue * messages, GstState state)
{
  GstStructure *message = NULL;

  // Pipeline does not notify us when changing to NULL state, skip wait.
  if (state == GST_STATE_NULL)
    return TRUE;

  // Wait for either a PIPELINE_STATE or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE) ||
        gst_structure_has_name (message, PIPELINE_EOS_MESSAGE)) {
      gst_structure_free (message);
      // Returning FALSE will cause menu thread to terminate.
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState newstate = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint*) &newstate);

      if (newstate == state)
        break;
    }
    gst_structure_free (message);
  }
  gst_structure_free (message);
  return TRUE;
}

static gboolean
update_pipeline_state (GstAppContext *appctx, GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (appctx->pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to retrieve pipeline state!\n");
    return FALSE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
  }

  // Check whether to send an EOS event on the pipeline.
  if (eos_on_shutdown && current == GST_STATE_PLAYING &&
      (state == GST_STATE_NULL || state == GST_STATE_READY)) {
    g_print ("EOS enabled -- Sending EOS on the pipeline\n");

    if (!gst_element_send_event (appctx->pipeline, gst_event_new_eos ())) {
      g_printerr ("ERROR: Failed to send EOS event!");
      return FALSE;
    }
    appctx->waiting_eos = TRUE;

    if (!wait_pipeline_eos_message (appctx->messages))
      return FALSE;
  }

  g_print ("Setting pipeline to %s\n", gst_element_state_get_name (state));
  ret = gst_element_set_state (appctx->pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));

      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      ret = gst_element_get_state (appctx->pipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
        return FALSE;
      }

      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  if (!wait_pipeline_state_message (appctx->messages, state))
    return FALSE;

  return TRUE;
}

static void
print_ffr_menu (gint opt)
{
  gchar *title = (opt == GST_REWIND_OPTION ? "Rewind" : "Fast Forward");
  gint spaces = (opt == GST_REWIND_OPTION ? 24 : 21);

  g_print ("\n%.*s %s %.*s\n", spaces, DASH_LINE, title, spaces, DASH_LINE);
  g_print ("   (%d) %-25s\n", GST_TIME_BASED, "Time-based");
  g_print ("   (%d) %-25s\n", GST_SPEED_BASED, "Speed-based");

  g_print ("%.56s\n", DASH_LINE);
  g_print ("   (%s) %-25s\n", MENU_BACK_OPTION, "Back");
  g_print ("\nChoose an option: ");
}

static void
print_menu ()
{
  g_print ("\n%.25s MENU %.25s\n", DASH_LINE, DASH_LINE);
  g_print ("   (%d) %-25s\n", GST_PLAY_OPTION, "Play");
  g_print ("   (%d) %-25s\n", GST_PAUSE_OPTION, "Pause");
  g_print ("   (%d) %-25s\n", GST_FAST_FORWARD_OPTION, "Fast Forward");
  g_print ("   (%d) %-25s\n", GST_REWIND_OPTION, "Rewind");

  g_print ("%.56s\n", DASH_LINE);
  g_print ("   (%s) %-25s\n", QUIT_OPTION, "Quit");
  g_print ("\nChoose an option: ");
}

gint64
query_position (GstAppContext * appctx)
{
  gint64 pos = -1;
  if (!gst_element_query_position (appctx->pipeline, GST_FORMAT_TIME, &pos)) {
    g_print ("ERROR: Couldn't query position\n");
    return -1;
  }

  return pos;
}

static gboolean
perform_seek (GstAppContext * appctx, gdouble rate, gint64 position)
{
  GstEvent *seek_event;

  // If rate > 0, seek segment will be from given pos to end of stream.
  // Whereas if rate < 0 (playing backwards), seek segment will be from
  // start of stream to given pos.
  if ((rate > 0 && gst_element_seek (appctx->pipeline, rate,
      GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE)) ||
      (rate < 0 && gst_element_seek (appctx->pipeline, rate,
      GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, position))) {
    appctx->rate = rate;
    return TRUE;
  }

  return FALSE;
}

static gboolean
handle_ffr_menu (GstAppContext * appctx, gint * opt)
{
  gchar *str = NULL, *endptr;
  gint64 pos = 0;
  gint input = 0;
  guint bitrate = 0;
  gint mul = (*opt == GST_REWIND_OPTION ? -1 : 1);

  print_ffr_menu (*opt);

  if (!wait_stdin_message (appctx->messages, &str))
    return FALSE;

  if (g_str_equal (str, MENU_BACK_OPTION)) {
    *opt = 0;
    goto exit;
  }

  input = g_ascii_strtoll ((const gchar *) str, &endptr, 0);

  switch (input) {
    case GST_TIME_BASED:
      g_print ("By how many seconds you want to seek "
          "(or press Enter to return): ");

      if (!wait_stdin_message (appctx->messages, &str))
        return FALSE;
      else if (!g_str_equal (str, "\n")) {
        input = g_ascii_strtoll ((const gchar *) str, &endptr, 0);
        pos = query_position (appctx);

        if (pos >= 0) {
          // set new position to 'input' seconds forward or backward
          // from the current position.
          pos += input * GST_SECOND * mul;

          if (perform_seek (appctx, appctx->rate, pos))
            g_print ("Seeked...\n");
          else
            g_print ("Couldn't seek!\n");
        }
      }
      break;
    case GST_SPEED_BASED:
      g_print ("Enter speed (or press Enter to return): ");

      if (!wait_stdin_message (appctx->messages, &str))
        return FALSE;
      else if (!g_str_equal (str, "\n")) {
        gdouble speed = g_ascii_strtod ((const gchar *) str, &endptr);
        if (speed < 0) {
          g_print ("Use rewind for negative speed.\n");
          break;
        }
        pos = query_position (appctx);

        if (pos >= 0 && perform_seek (appctx, mul*speed, pos))
          g_print ("Seeked...\n");
        else
          g_print ("Couldn't seek!\n");
      }
      break;
    default:
      break;
  }

exit:
  g_free (str);
  return TRUE;
}

static void
change_state (GstMainMenuOption opt, GstAppContext * appctx)
{
  switch (opt) {
    case GST_PLAY_OPTION:
    {
      if (!update_pipeline_state (appctx, GST_STATE_PLAYING))
        g_printerr ("ERROR: Couldn't play!");
      else
        g_print ("Playing...");

      break;
    }
    case GST_PAUSE_OPTION:
    {
      if (!update_pipeline_state (appctx, GST_STATE_PAUSED))
        g_printerr ("ERROR: Couldn't pause!");
      else
        g_print ("Paused...");

      break;
    }
    default:
      break;
  }
}

static gboolean
handle_main_menu (GstAppContext * appctx, gint * opt)
{
  gchar *str = NULL, *endptr;

  print_menu ();

  if (!wait_stdin_message (appctx->messages, &str))
    return FALSE;

  if (g_str_equal (str, QUIT_OPTION)) {
    g_free (str);
    return FALSE;
  }

  *opt = g_ascii_strtoll ((const gchar *) str, &endptr, 0);
  if (*opt >= GST_PLAY_OPTION && *opt <= GST_PAUSE_OPTION)
    change_state (*opt, appctx);

  g_free (str);
  return TRUE;
}

static gpointer
main_menu (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gint opt = 0;
  gboolean active = TRUE;

  // Transition to PLAYING state.
  if (!update_pipeline_state (appctx, GST_STATE_PLAYING)) {
    g_main_loop_quit (appctx->mloop);
    return NULL;
  }

  while (active) {
    if (opt == GST_FAST_FORWARD_OPTION || opt == GST_REWIND_OPTION)
      active = handle_ffr_menu (appctx, &opt);
    else
      active = handle_main_menu (appctx, &opt);
  }

  // Stop the pipeline.
  update_pipeline_state (appctx, GST_STATE_NULL);

  g_main_loop_quit (appctx->mloop);

  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext *appctx;
  GOptionContext *optctx;
  GstBus *bus = NULL;
  GIOChannel *gio = NULL;
  GThread *mthread = NULL;
  GError *error = NULL;
  gchar **pipeline = NULL;
  guint bus_watch_id = 0, intrpt_watch_id = 0, stdin_watch_id = 0;
  gint status = -1;

  g_set_prgname ("gst-video-playback-example");

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Initialize GST library.
  gst_init (&argc, &argv);

  GOptionEntry options[] = {
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
     "Send EOS event before transition from PLAYING to READY/NULL state.",
     NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &pipeline, NULL},
    {NULL}
  };

  optctx = g_option_context_new ("<pipeline>");
  g_option_context_set_summary (optctx, GST_APP_SUMMARY);
  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());

  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (error->message));

    g_option_context_free (optctx);
    g_clear_error (&error);

    return -1;
  }
  g_option_context_free (optctx);

  if (pipeline == NULL) {
    g_print ("You must provide a valid pipeline to play.\n\n");
    g_print ("Usage: gst-video-playback-example <pipeline> [OPTION]\n");
    g_print ("For help: gst-video-playback-example [-h | --help]");

    return -1;
  }

  if ((appctx = gst_app_context_new ()) == NULL) {
    g_printerr ("ERROR: Couldn't create app context!\n");

    g_strfreev (pipeline);
    return -1;
  }

  appctx->pipeline = gst_parse_launchv ((const gchar **) pipeline, &error);

  // Check for errors on pipe creation.
  if ((NULL == appctx->pipeline) && (error != NULL)) {
    g_printerr ("ERROR: Failed to create pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    goto exit;
  } else if ((NULL == appctx->pipeline) && (NULL == error)) {
    g_printerr ("ERROR: Failed to create pipeline, unknown error!\n");

    goto exit;
  } else if ((appctx->pipeline != NULL) && (error != NULL)) {
    g_printerr ("ERROR: Erroneous pipeline, error: %s!\n",
        GST_STR_NULL (error->message));

    g_clear_error (&error);
    goto exit;
  }

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    goto exit;
  }

  // Initiate the menu thread.
  if ((mthread = g_thread_new ("MainMenu", main_menu, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create menu thread!\n");
    goto exit;
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    goto exit;
  }

  // Create a GIOChannel to listen to the standard input stream.
  if ((gio = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize I/O support!\n");
    gst_object_unref (bus);
    goto exit;
  }

  // Watch for messages on the pipeline's bus.
  bus_watch_id = gst_bus_add_watch (bus, handle_bus_message, appctx);
  gst_object_unref (bus);

  // Watch for user's input on stdin.
  stdin_watch_id = g_io_add_watch (gio, G_IO_PRI | G_IO_IN,
      handle_stdin_source, appctx);
  g_io_channel_unref (gio);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  // Wait until main menu thread finishes.
  g_thread_join (mthread);

  g_source_remove (bus_watch_id);
  g_source_remove (intrpt_watch_id);
  g_source_remove (stdin_watch_id);

  status = 0;

exit:
  g_strfreev (pipeline);

  gst_app_context_free (appctx);

  gst_deinit ();
  return status;
}
