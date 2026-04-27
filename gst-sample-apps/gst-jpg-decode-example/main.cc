/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application:
 * GStreamer application to decode JPEG images and display them on waylandsink
 *
 * Description:
 * This application demonstrates JPEG decoding using the pipeline:
 *
 * multifilesrc ! jpegdec ! videoconvert ! qtivtransform ! waylandsink
 *
 * Supported input modes:
 * 1. Sequence input  : /etc/media/frame_%d.jpg
 *    - Plays from frame 0 up to the last available frame, then exits on EOS.
 *
 * 2. Single image    : /etc/media/frame_1.jpg
 *    - Keeps redisplaying the same image until interrupted.
 *
 * Usage:
 * gst-jpg-decode-example -i /etc/media/frame_%d.jpg
 * gst-jpg-decode-example -i /etc/media/frame_1.jpg
 */

#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst_sample_apps_utils.h>

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_INPUT_PATH "/etc/media/frame_%d.jpg"

#define GST_APP_SUMMARY                                                        \
  "This application showcases JPEG decoding on waylandsink.\n"                 \
  "\nExamples:\n"                                                              \
  "  Sequence input : gst-jpg-decode-example -i /etc/media/frame_%d.jpg\n"    \
  "  Single image   : gst-jpg-decode-example -i /etc/media/frame_1.jpg\n"

/* Structure to hold the application context */
struct GstComposeAppContext : GstAppContext {
  gchar *input_file;
  gint width;
  gint height;
};

/**
 * Create and initialize application context
 */
static GstComposeAppContext *
gst_app_context_new () {
  GstComposeAppContext *ctx =
      (GstComposeAppContext *) g_new0 (GstComposeAppContext, 1);

  if (ctx == NULL) {
    g_printerr ("[ERROR] Unable to create application context\n");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->input_file = g_strdup (DEFAULT_INPUT_PATH);

  return ctx;
}

/**
 * Free application context
 */
static void
gst_app_context_free (GstComposeAppContext *appctx) {
  if (appctx == NULL)
    return;

  if (appctx->plugins != NULL) {
    GstElement *element_curr = (GstElement *) appctx->plugins->data;
    GstElement *element_next = NULL;

    GList *list = appctx->plugins->next;
    for (; list != NULL; list = list->next) {
      element_next = (GstElement *) list->data;
      gst_element_unlink (element_curr, element_next);
      gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);
      element_curr = element_next;
    }

    gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);
    g_list_free (appctx->plugins);
    appctx->plugins = NULL;
  }

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx->input_file != NULL) {
    g_free (appctx->input_file);
    appctx->input_file = NULL;
  }

  g_free (appctx);
}

/**
 * Check whether the input path is a sequence pattern.
 * Example: /etc/media/frame_%d.jpg
 */
static gboolean
is_pattern_input (const gchar *input_file) {
  return (input_file != NULL && g_strrstr (input_file, "%d") != NULL);
}

/**
 * Find the last available frame index for a sequence pattern.
 * Scans from start_index until the first missing file.
 */
static gint
find_last_frame_index (const gchar *pattern, gint start_index) {
  gint idx = start_index;

  while (TRUE) {
    gchar *candidate = g_strdup_printf (pattern, idx);
    gboolean exists = g_file_test (candidate, G_FILE_TEST_EXISTS);

    g_free (candidate);

    if (!exists)
      return idx - 1;

    idx++;
  }

  return start_index;
}

/**
 * Create the JPEG decode pipeline:
 * multifilesrc ! jpegdec ! videoconvert ! qtivtransform ! waylandsink
 */
static gboolean
create_pipe_jpgdecode (GstComposeAppContext *appctx) {
  GstElement *multifilesrc = NULL;
  GstElement *jpegdec = NULL;
  GstElement *videoconvert = NULL;
  GstElement *qtivtransform = NULL;
  GstElement *waylandsink = NULL;

  gboolean ret = FALSE;
  appctx->plugins = NULL;

  multifilesrc = gst_element_factory_make ("multifilesrc", "multifilesrc");
  jpegdec = gst_element_factory_make ("jpegdec", "jpegdec");
  videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");
  qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");

  if (!multifilesrc || !jpegdec || !videoconvert || !qtivtransform ||
      !waylandsink) {
    g_printerr ("[ERROR] Failed to create one or more GStreamer elements\n");
    g_printerr (
        "[ERROR] multifilesrc=%p jpegdec=%p videoconvert=%p qtivtransform=%p "
        "waylandsink=%p\n",
        multifilesrc, jpegdec, videoconvert, qtivtransform, waylandsink);
    return FALSE;
  }

  g_print ("[INFO] Input path: %s\n", appctx->input_file);
  g_print ("[INFO] Width/height arguments received: %d x %d\n",
      appctx->width, appctx->height);
  g_print (
      "[INFO] Width/height are kept for CLI compatibility only and are not "
      "applied in the baseline pipeline\n");

  if (is_pattern_input (appctx->input_file)) {
    gint start_index = 0;
    gint last_index = find_last_frame_index (appctx->input_file, start_index);

    g_print ("[INFO] Detected sequence input using %%d pattern\n");
    g_print ("[INFO] Scanning frame availability starting from index %d\n",
        start_index);

    if (last_index < start_index) {
      g_printerr ("[ERROR] No input frames found for pattern: %s\n",
          appctx->input_file);
      return FALSE;
    }

    g_print ("[INFO] Last available frame index detected: %d\n", last_index);
    g_print (
        "[INFO] Configuring multifilesrc for bounded playback from %d to %d\n",
        start_index, last_index);

    g_object_set (G_OBJECT (multifilesrc),
        "location", appctx->input_file,
        "index", start_index,
        "stop-index", last_index,
        "loop", FALSE,
        NULL);
  } else {
    if (!g_file_test (appctx->input_file, G_FILE_TEST_EXISTS)) {
      g_printerr ("[ERROR] Input image file does not exist: %s\n",
          appctx->input_file);
      return FALSE;
    }

    g_print ("[INFO] Detected single-image input\n");
    g_print (
        "[INFO] Configuring multifilesrc to redisplay the same image "
        "continuously until interrupt\n");

    g_object_set (G_OBJECT (multifilesrc),
        "location", appctx->input_file,
        "loop", TRUE,
        NULL);
  }

  g_object_set (G_OBJECT (waylandsink),
      "fullscreen", TRUE,
      "sync", FALSE,
      NULL);

  g_print (
      "[INFO] waylandsink configured with fullscreen=true and sync=false\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      multifilesrc, jpegdec, videoconvert, qtivtransform, waylandsink, NULL);

  g_print (
      "[INFO] Linking pipeline elements: multifilesrc -> jpegdec -> "
      "videoconvert -> qtivtransform -> waylandsink\n");

  ret = gst_element_link_many (multifilesrc, jpegdec, videoconvert,
      qtivtransform, waylandsink, NULL);

  if (!ret) {
    g_printerr ("[ERROR] Pipeline elements could not be linked\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        multifilesrc, jpegdec, videoconvert, qtivtransform, waylandsink, NULL);
    return FALSE;
  }

  appctx->plugins = g_list_append (appctx->plugins, multifilesrc);
  appctx->plugins = g_list_append (appctx->plugins, jpegdec);
  appctx->plugins = g_list_append (appctx->plugins, videoconvert);
  appctx->plugins = g_list_append (appctx->plugins, qtivtransform);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  g_print ("[INFO] Pipeline created successfully\n");
  return TRUE;
}

gint
main (gint argc, gchar *argv[]) {
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstComposeAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("[ERROR] Failed to initialize application context\n");
    return -1;
  }

  GOptionEntry entries[] = {
    {"width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
     "Width argument (parsed but not applied in baseline pipeline)", "width"},
    {"height", 'h', 0, G_OPTION_ARG_INT, &appctx->height,
     "Height argument (parsed but not applied in baseline pipeline)", "height"},
    {"input_file", 'i', 0, G_OPTION_ARG_FILENAME, &appctx->input_file,
     "Path to input image or image sequence", "path"},
    {NULL, 0, 0, (GOptionArg) 0, NULL, NULL, NULL}
  };

  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("[ERROR] Failed to parse command line options: %s\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (error == NULL)) {
      g_printerr (
          "[ERROR] Failed to parse command line options: unknown error\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("[ERROR] Failed to create option context\n");
    gst_app_context_free (appctx);
    return -1;
  }

  gst_init (&argc, &argv);
  g_set_prgname ("gst-jpg-decode-example");

  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("[ERROR] Failed to create pipeline\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  ret = create_pipe_jpgdecode (appctx);
  if (!ret) {
    gst_app_context_free (appctx);
    return -1;
  }

  mloop = g_main_loop_new (NULL, FALSE);
  if (mloop == NULL) {
    g_printerr ("[ERROR] Failed to create main loop\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  if (bus == NULL) {
    g_printerr ("[ERROR] Failed to retrieve pipeline bus\n");
    gst_app_context_free (appctx);
    return -1;
  }

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  g_print ("[INFO] Setting pipeline to PAUSED state\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("[ERROR] Failed to transition pipeline to PAUSED state\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return -1;

    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("[INFO] Pipeline is live and does not require preroll\n");
      break;

    case GST_STATE_CHANGE_ASYNC:
      g_print ("[INFO] Pipeline is prerolling asynchronously\n");
      break;

    case GST_STATE_CHANGE_SUCCESS:
      g_print ("[INFO] Pipeline entered PAUSED state successfully\n");
      break;

    default:
      break;
  }

  g_print ("[INFO] Application is running\n");
  g_main_loop_run (mloop);

  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  g_print ("[INFO] Setting pipeline to NULL state\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  g_print ("[INFO] Releasing application resources\n");
  gst_app_context_free (appctx);

  g_print ("[INFO] Deinitializing GStreamer\n");
  gst_deinit ();

  return 0;
}
