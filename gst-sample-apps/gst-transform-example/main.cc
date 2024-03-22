/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Application for rotate, flip and scale video stream.
*
* Description:
* This is an application to demonstrate transform video stream such as rotate,
* flip and scale on input live camera stream. Output goes to display and
* also dumps to mp4 file
*
* Usage:
* gst-transform-example -r 90 -f 2 --input_width 3840 --input_height 2160
*                       --output_width 1920 --output_height 1080
*                       -o <output_file>.mp4
*
* Help:
* gst-transform-example --help
*
******************************************************************************************
* Transform Pipeline:
*
* qtiqmmfsrc->capsfilter->queue->qtivtransform->capsfilter->queue--|
*     --------------------------------------------------------------
*     |
*     |    |->queue->waylandsink
*    tee-->|
*          |->queue->encoder->queue->h264parse->mp4mux->queue->filesink
*
* ****************************************************************************************
*
*/

#include "include/gst_sample_apps_utils.h"

#define DEFAULT_OUTPUT_FILE "/opt/video_transform.mp4"

#define DEFAULT_INPUT_WIDTH 1920
#define DEFAULT_INPUT_HEIGHT 1080

#define DEFAULT_OUTPUT_WIDTH 1920
#define DEFAULT_OUTPUT_HEIGHT 1080

#define DEFAULT_ROTATION 90

#define QUEUE_COUNT 6

#define ARRAY_SIZE 20

#define GST_APP_SUMMARY \
  "This application facilitates rotation, flipping, and scaling operations. " \
  "It provides two outputs: one is a dump to an MP4 file, and the other is " \
  "a preview display. \n For example, it can scale from 1080P to 720P or " \
  "from 720P to 1080P. It can also rotate the image by 90, 180, or 270 " \
  "degrees. \n The flip options include no flip (0), horizontal flip (1), " \
  "vertical flip (2), or both (3). All three operations can be performed " \
  "simultaneously if needed.\n" \
  "\nCommand:\n" \
  "All three operations \n" \
  "  gst-transform-example -r 90 -f 2 --input_width 3840 --input_height 2160 " \
  "--output_width 1920 --output_height 1080 -o /opt/video_transform.mp4 \n" \
  "Perform the only rotation\n" \
  "  gst-transform-example -r 270 -o /opt/video_transform.mp4 \n" \
  "\nOutput:\n" \
  "  After the execution, the application gets the output to preview on " \
  "display and after the use case stops, the recorded o/p file is saved " \
  "at given path.(/opt/)" \

// Structure to hold the application context
struct GstTransformAppContext : GstAppContext {
  gchar *output_file;
  GstFlipVideoType flip_type;
  gint rotate;
  gint input_width;
  gint input_height;
  gint output_width;
  gint output_height;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstTransformAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstTransformAppContext *ctx = (GstTransformAppContext *) g_new0 (GstTransformAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context\n");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->output_file = DEFAULT_OUTPUT_FILE;
  ctx->rotate = DEFAULT_ROTATION;
  ctx->input_width = DEFAULT_INPUT_WIDTH;
  ctx->input_height = DEFAULT_INPUT_HEIGHT;
  ctx->output_width = DEFAULT_OUTPUT_WIDTH;
  ctx->output_height = DEFAULT_OUTPUT_HEIGHT;
  ctx->flip_type = GST_FLIP_TYPE_NONE;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstTransformAppContext * appctx)
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

  if (appctx->output_file != NULL && appctx->output_file != DEFAULT_OUTPUT_FILE)
    g_free (appctx->output_file);

  if (appctx != NULL)
    g_free (appctx);
}

/**
 * Create GST pipeline invloves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_transform_pipeline (GstTransformAppContext * appctx)
{
  GstElement *qtiqmmfsrc, *qmmfsrc_filter, *scale_filter, *tee;
  GstElement *qtivtransform, *encoder, *h264parse;
  GstElement *queue[QUEUE_COUNT];
  GstElement *mp4mux, *filesink, *waylandsink;
  GstCaps *filtercaps;
  gchar element_name[ARRAY_SIZE];
  gint rotate_opt;
  gboolean ret = FALSE;

  switch (appctx->rotate) {
    case 0:
      rotate_opt = GST_ROTATE_TYPE_NONE;
      break;
    case 90:
      rotate_opt = GST_ROTATE_TYPE_90CW;
      break;
    case 180:
      rotate_opt = GST_ROTATE_TYPE_180;
      break;
    case 270:
      rotate_opt = GST_ROTATE_TYPE_90CCW;
      break;
    default:
      g_printerr ("Invalid rotation option!\n");
      rotate_opt = GST_ROTATE_TYPE_NONE;
      break;
  }

  // Create camera source and the element capability
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  qmmfsrc_filter = gst_element_factory_make ("capsfilter", "qmmfsrc_filter");

  // Create tee element for duplicating the stream
  tee = gst_element_factory_make ("tee", "tee");

  // Create qtivtransform element and capabilities
  qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
  scale_filter = gst_element_factory_make ("capsfilter", "scale_filter");

  // Create encoder element and parser
  encoder = gst_element_factory_make ("v4l2h264enc", "encoder");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");

  // Create muxer element
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

  // Create filesink element
  filesink = gst_element_factory_make ("filesink", "filesink");

  // Create waylandsink element
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");

  // Create queue elements
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, sizeof (element_name), "queue_%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      return FALSE;
    }
  }

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !qmmfsrc_filter || !tee || !qtivtransform || !encoder ||
      !scale_filter || !h264parse || !mp4mux || !filesink || !waylandsink) {
    g_printerr ("Failed to create elements!\n");
    unref_elements (qtiqmmfsrc, qmmfsrc_filter, tee, qtivtransform,
        scale_filter, encoder, h264parse, mp4mux, filesink,
        waylandsink, "NULL");
    return FALSE;
  }

  // Set rotation property
  g_object_set (G_OBJECT (qtivtransform), "rotate", rotate_opt, NULL);

  // Set video image flip type property
  if (appctx->flip_type == GST_FLIP_TYPE_HORIZONTAL) {
    g_object_set (G_OBJECT (qtivtransform), "flip-horizontal", true, NULL);
  } else if (appctx->flip_type == GST_FLIP_TYPE_VERTICAL) {
    g_object_set (G_OBJECT (qtivtransform), "flip-vertical", true, NULL);
  } else if (appctx->flip_type == GST_FLIP_TYPE_BOTH) {
    g_object_set (G_OBJECT (qtivtransform), "flip-horizontal", true, NULL);
    g_object_set (G_OBJECT (qtivtransform), "flip-vertical", true, NULL);
  } else {
    g_print ("Flip is not enabled\n");
  }

  // Set capture I/O mode
  g_object_set (G_OBJECT (encoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (encoder), "output-io-mode", 5, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // Set location properties to filesink
  g_object_set(G_OBJECT(filesink), "location", appctx->output_file, NULL);

  // Configure the main stream capabilities based on width and height
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->input_width,
      "height", G_TYPE_INT, appctx->input_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  g_object_set (G_OBJECT (qmmfsrc_filter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the scale stream capabilities based on width and height
  if (appctx->output_width == DEFAULT_OUTPUT_WIDTH &&
      appctx->output_height == DEFAULT_OUTPUT_HEIGHT) {
    appctx->output_width = appctx->input_width;
    appctx->output_height = appctx->input_height;
  }

  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->output_width,
      "height", G_TYPE_INT, appctx->output_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  g_object_set (G_OBJECT (scale_filter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_filter,
      qtivtransform, scale_filter, tee, encoder, h264parse,
      mp4mux, filesink, waylandsink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  // Link camera stream to waylandsink
  ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_filter, queue[0],
      qtivtransform, scale_filter, queue[1], tee, queue[2],
      waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  // Link camera stream, encoder, filesink
  ret = gst_element_link_many (tee, queue[3], encoder, queue[4], h264parse,
      mp4mux, queue[5], filesink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, qmmfsrc_filter);
  appctx->plugins = g_list_append (appctx->plugins, qtivtransform);
  appctx->plugins = g_list_append (appctx->plugins, scale_filter);
  appctx->plugins = g_list_append (appctx->plugins, tee);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);
  appctx->plugins = g_list_append (appctx->plugins, encoder);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, mp4mux);
  appctx->plugins = g_list_append (appctx->plugins, filesink);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    appctx->plugins = g_list_append (appctx->plugins, queue[i]);
  }

  g_print ("All elements are linked successfully\n");

  return TRUE;

error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
      qmmfsrc_filter, qtivtransform, scale_filter, tee, encoder,
      h264parse, mp4mux, filesink, waylandsink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }
}

gint
main (gint argc, gchar **argv)
{
  GOptionContext *ctx;
  GstTransformAppContext *app_ctx = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstStateChangeReturn change_ret = GST_STATE_CHANGE_FAILURE;
  gint ret = -1;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/run/user/root", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Create the application context
  app_ctx = gst_app_context_new ();
  if (app_ctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return ret;
  }

  GOptionEntry entries[] = {
    {"rotate", 'r', 0, G_OPTION_ARG_INT,
      &app_ctx->rotate,
      "Image rotation",
      "Image rotate 90/180/270 degree, default 90"
    },
    {"flip", 'f', 0, G_OPTION_ARG_INT,
     &app_ctx->flip_type,
     "Flip video image enable",
     "Parameter flip type 0-noflip/1-horizontal/2-vertical/3-both, default 0"
    },
    {"input_width", 'W', 0, G_OPTION_ARG_INT,
     &app_ctx->input_width,
     "Width",
     "Camera input width, default 1920"
    },
    {"input_height", 'H', 0, G_OPTION_ARG_INT,
     &app_ctx->input_height,
     "Height",
     "Camera input height, default 1080"
    },
    {"output_width", 'w', 0, G_OPTION_ARG_INT,
     &app_ctx->output_width,
     "Width",
     "image scale output width, default 1920"
    },
    {"output_height", 'h', 0, G_OPTION_ARG_INT,
     &app_ctx->output_height,
     "Height",
     "image scale output height, default 1080"
    },
    {"output_file", 'o', 0, G_OPTION_ARG_STRING,
     &app_ctx->output_file,
     "Output Filename",
     "default - /opt/video_AVC_transform.mp4"
    },
    {NULL}
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
      gst_app_context_free (app_ctx);
      return ret;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (app_ctx);
      return ret;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    gst_app_context_free (app_ctx);
    return ret;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  app_ctx->pipeline = gst_pipeline_new ("gst-transform-example");
  if (!app_ctx->pipeline) {
    g_printerr ("Failed to create pipeline!\n");
    gst_app_context_free (app_ctx);
    return ret;
  }

  // Build transform pipeline
  if (!create_transform_pipeline (app_ctx)) {
    g_printerr ("Failed to create transform pipeline!\n");
    gst_app_context_free (app_ctx);
    return ret;
  }

  // Initialize main loop.
  app_ctx->mloop = g_main_loop_new (NULL, FALSE);
  if (!app_ctx->mloop) {
    g_printerr ("Failed to create main loop!\n");
    gst_app_context_free (app_ctx);
    return ret;
  }

  // Retrieve reference to the pipeline's bus.
  bus = gst_pipeline_get_bus (GST_PIPELINE(app_ctx->pipeline));
  if (!bus) {
    g_printerr ("Failed to get pipeline bus!\n");
    gst_app_context_free (app_ctx);
    return ret;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", G_CALLBACK(eos_cb), app_ctx->mloop);
  g_signal_connect (bus, "message::error", G_CALLBACK(error_cb), app_ctx->mloop);
  g_signal_connect (bus, "message::warning", G_CALLBACK(warning_cb), NULL);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK(state_changed_cb),
      app_ctx->pipeline);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, app_ctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("Setting pipeline to PAUSED state ...\n");
  change_ret = gst_element_set_state (app_ctx->pipeline, GST_STATE_PAUSED);

  switch (change_ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (app_ctx);
      return ret;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change to PLAYING was successful\n");
      break;
  }

  g_print ("\n Application is running\n");
  g_main_loop_run (app_ctx->mloop);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (app_ctx->pipeline, GST_STATE_NULL);

  g_print ("Output file dump to %s\n", app_ctx->output_file);

  ret = 0;

  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (app_ctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return ret;
}
