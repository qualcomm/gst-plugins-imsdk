/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for multiple camera and streams
 * Description:
 * This application Demonstrates multicamera live preview on display
 * or dumping video encoder output
 *
 * Usage:
 * For Preview on Display:
 * gst-multi-camera-example --output=0 --width=1920 --height=1080
 * For YUV dump on device:
 * gst-multi-camera-example --output=1 --width=1920 --height=1080
 *
 * Help:
 * gst-multi-camera-example --help
 *
 * *******************************************************************
 * Pipeline For encoder dump on device:
 * qtiqmmfsrc->camera0->capsfilter->v4l2h264enc->h264parse->mp4mux->filesink
 *
 * qtiqmmfsrc->camera1->capsfilter->v4l2h264enc->h264parse->mp4mux->filesink
 *
 * Pipeline For Preview on Display:
 * qtiqmmfsrc-camera0->capsfilter->|
 *                                 |->qtivcomposer->waylandsink
 * qtiqmmfsrc->camera1->capsfilter>|
 * *******************************************************************
 */

#include <glib-unix.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define DEFAULT_OUTPUT_FILENAME_CAM1 "/opt/cam1_vid.mp4"
#define DEFAULT_OUTPUT_FILENAME_CAM2 "/opt/cam2_vid.mp4"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

#define GST_APP_SUMMARY "This application allows users to utilize a " \
  "multi-camera live preview on their display. It also provides the " \
  "functionality to either use Waylandsink or dump the encoded output\n" \
  "\nCommand:\n" \
  "\nFor Waylandsink Preview:\n" \
  "  gst-multi-camera-example -o 0 -w 1920 -h 1080 \n" \
  "\nFor Encoded output:\n" \
  "  gst-multi-camera-example -o 1 -w 1920 -h 1080 " \
  "\nOutput:\n" \
  "  Upon execution, application will generates output as preview or " \
  "encoded files for two cameras."

// Structure to hold the application context
struct GstMultiCamAppContext : GstAppContext {
  GstSinkType sinktype;
  gint width;
  gint height;
};

/**
* Create and initialize application context:
*
* @param NULL
*/
static GstMultiCamAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstMultiCamAppContext *ctx = (GstMultiCamAppContext *)
      g_new0 (GstMultiCamAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->sinktype = GST_WAYLANDSINK;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;

  return ctx;
}

/**
 * Build Property for pad.
 *
 * @param property Property Name.
 * @param values Value of Property.
 * @param num count of Property Values.
 */
static void
build_pad_property (GValue * property, gint values[], gint num)
{
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);

  for (gint idx = 0; idx < num; idx++) {
    g_value_set_int (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

/**
* Free Application context:
*
* @param appctx application context object
*/
static void
gst_app_context_free (GstMultiCamAppContext * appctx)
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

  if (appctx != NULL)
    g_free (appctx);
}

/**
* Create GST pipeline involves 3 main steps
* 1. Create all elements/GST Plugins
* 2. Set Paramters for each plugin
* 3. Link plugins to create GST pipeline
*
* @param appctx application context object.
*
*/
static gboolean
create_camera_wayland_pipe (GstMultiCamAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *pipeline, *qtiqmmf_cam1, *waylandsink, *capsfilter_cam1;
  GstElement *qtiqmmf_cam2, *qtivcomposer, *capsfilter_cam2;
  GstCaps *filtercaps_cam1, *filtercaps_cam2;
  GstPad *composer_sink_1, *composer_sink_2;
  guint ret = FALSE;

  // Create first source element set the first camera
  qtiqmmf_cam1 = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmf_cam1");
  g_object_set (G_OBJECT (qtiqmmf_cam1), "camera", 0, NULL);

  // Create second source element set the second camera
  qtiqmmf_cam2 = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmf_cam2");
  g_object_set (G_OBJECT (qtiqmmf_cam2), "camera", 1, NULL);

  capsfilter_cam1 = gst_element_factory_make ("capsfilter", "capsfilter_cam1");

  filtercaps_cam1 = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      NULL);
  gst_caps_set_features (filtercaps_cam1, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_cam1), "caps", filtercaps_cam1, NULL);
  gst_caps_unref (filtercaps_cam1);

  capsfilter_cam2 = gst_element_factory_make ("capsfilter", "capsfilter_cam2");

  filtercaps_cam2 = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      NULL);
  gst_caps_set_features (filtercaps_cam2, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_cam2), "caps", filtercaps_cam2, NULL);
  gst_caps_unref (filtercaps_cam2);

  // create qtivcomposer element to combine 2 i/p streams as in single display
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");

  // create waylandsink element to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmf_cam2, capsfilter_cam1,
      qtivcomposer, qtiqmmf_cam1, capsfilter_cam2, waylandsink, NULL);

  g_print ("\n Link preview pipeline elements ..\n");

  ret = gst_element_link_many (qtiqmmf_cam1, capsfilter_cam1, qtivcomposer,
      waylandsink, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmf_cam1, capsfilter_cam1,
        qtivcomposer, waylandsink, NULL);
    return FALSE;
  }

  ret = gst_element_link_many (qtiqmmf_cam2, capsfilter_cam2, qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmf_cam2, capsfilter_cam2,
        qtivcomposer, NULL);
  return -1;
  }

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmf_cam1);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_cam1);
  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmf_cam2);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_cam2);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // As we have multicamera stream to compose create two pad/ports for qtivcomposer
  composer_sink_1 = gst_element_get_static_pad (qtivcomposer, "sink_0");
  composer_sink_2 = gst_element_get_static_pad (qtivcomposer, "sink_1");

  if (composer_sink_1 == NULL || composer_sink_2 == NULL) {
    g_printerr ("\n One or more sink pads are not available");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmf_cam2, capsfilter_cam2,
        qtivcomposer, NULL);
    return FALSE;
  }

  // Create and set the position and dimensions for qtivcomposer
  GValue pos1 = G_VALUE_INIT, dim1 = G_VALUE_INIT;
  g_value_init (&pos1, GST_TYPE_ARRAY);
  g_value_init (&dim1, GST_TYPE_ARRAY);

  // check the composition type and set the position and dimensions for sink1
  gint pos1_vals[] = { 0, 0 };
  gint dim1_vals[] = { 640, 480 };

  build_pad_property (&pos1, pos1_vals, 2);
  build_pad_property (&dim1, dim1_vals, 2);

  g_object_set_property (G_OBJECT (composer_sink_1), "position", &pos1);
  g_object_set_property (G_OBJECT (composer_sink_1), "dimensions", &dim1);

  // check the composition type and set the position and dimensions for sink2
  GValue pos2 = G_VALUE_INIT, dim2 = G_VALUE_INIT;
  g_value_init (&pos2, GST_TYPE_ARRAY);
  g_value_init (&dim2, GST_TYPE_ARRAY);

  gint pos2_vals[] = { 640, 0 };
  gint dim2_vals[] = { 640, 480 };

  build_pad_property (&pos2, pos2_vals, 2);
  build_pad_property (&dim2, dim2_vals, 2);

  g_object_set_property (G_OBJECT (composer_sink_2), "position", &pos2);
  g_object_set_property (G_OBJECT (composer_sink_2), "dimensions", &dim2);

  // unref the sink pads after use
  gst_object_unref (composer_sink_1);
  gst_object_unref (composer_sink_2);

  g_value_unset (&pos1);
  g_value_unset (&dim1);
  g_value_unset (&pos2);
  g_value_unset (&dim2);

  return TRUE;
}

/**
* Create GST pipeline involves 3 main steps
* 1. Create all elements/GST Plugins
* 2. Set Paramters for each plugin
* 3. Link plugins to create GST pipeline
*
* @param appctx application context object.
*
*/
static gboolean
create_camera_video_pipe (GstMultiCamAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *qtiqmmf_cam1, *qtiqmmf_cam2, *capsfilter_cam1, *capsfilter_cam2,
      *v4l2h264enc_cam1, *h264parse_cam1, *mp4mux_cam1, *filesink_cam1,
      *filesink_cam2, *v4l2h264enc_cam2, *h264parse_cam2, *mp4mux_cam2;
  GstCaps *filtercaps_cam1, *filtercaps_cam2;
  GstStructure *fcontrols, *scontrols;
  gboolean ret = FALSE;
  appctx->plugins = NULL;

  // Create first source element set the first camera
  qtiqmmf_cam1 = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmf_cam1");
  g_object_set (G_OBJECT (qtiqmmf_cam1), "camera", 0, NULL);

  // Create second source element set the second camera
  qtiqmmf_cam2 = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmf_cam2");
  g_object_set (G_OBJECT (qtiqmmf_cam2), "camera", 1, NULL);

  capsfilter_cam1 = gst_element_factory_make ("capsfilter", "capsfilter_cam1");

  filtercaps_cam1 = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);
  gst_caps_set_features (filtercaps_cam1, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_cam1), "caps", filtercaps_cam1, NULL);
  gst_caps_unref (filtercaps_cam1);

  capsfilter_cam2 = gst_element_factory_make ("capsfilter", "capsfilter_cam2");

  filtercaps_cam2 = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);
  gst_caps_set_features (filtercaps_cam2, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_cam2), "caps", filtercaps_cam2, NULL);
  gst_caps_unref (filtercaps_cam2);

  // Create v4l2h264enc element for first source and set the element properties
  v4l2h264enc_cam1 = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc_cam1");
  g_object_set (G_OBJECT (v4l2h264enc_cam1), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (v4l2h264enc_cam1), "output-io-mode", 5, NULL);
  fcontrols = gst_structure_from_string (
      "fcontrols,video_bitrate_mode=0", NULL);
  g_object_set (G_OBJECT (v4l2h264enc_cam1), "extra-controls", fcontrols, NULL);

  // Create h264parse element for first source
  h264parse_cam1 = gst_element_factory_make ("h264parse", "h264parse_cam1");

  // Create mp4mux element for first source
  mp4mux_cam1 = gst_element_factory_make ("mp4mux", "mp4mux_cam1");

  // Create v4l2h264enc element for second source and set the element properties
  v4l2h264enc_cam2 = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc_cam2");
  g_object_set (G_OBJECT (v4l2h264enc_cam2), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (v4l2h264enc_cam2), "output-io-mode", 5, NULL);

  scontrols = gst_structure_from_string (
      "scontrols,video_bitrate_mode=0", NULL);
  g_object_set (G_OBJECT (v4l2h264enc_cam2), "extra-controls", scontrols, NULL);

  // Create h264parse element for second source
  h264parse_cam2 = gst_element_factory_make ("h264parse", "h264parse_cam2");

  // Create mp4mux element for first source
  mp4mux_cam2 = gst_element_factory_make ("mp4mux", "mp4mux_cam2");

  // Create filesink for first source and set the location and element properties
  filesink_cam1 = gst_element_factory_make ("filesink", "filesink_cam1");
  g_object_set (G_OBJECT (filesink_cam1), "location", DEFAULT_OUTPUT_FILENAME_CAM1,
      NULL);

  // Create filesink for second source and set the location and element properties
  filesink_cam2 = gst_element_factory_make ("filesink", "filesink_cam2");
  g_object_set (G_OBJECT (filesink_cam2), "location", DEFAULT_OUTPUT_FILENAME_CAM2,
      NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmf_cam1, qtiqmmf_cam2,
      capsfilter_cam1, capsfilter_cam2, v4l2h264enc_cam1, h264parse_cam1,
      mp4mux_cam1, filesink_cam1, filesink_cam2, v4l2h264enc_cam2, h264parse_cam2,
      mp4mux_cam2, NULL);

  g_print ("\n Link video encoder elements ..\n");

  ret = gst_element_link_many (qtiqmmf_cam1, capsfilter_cam1, v4l2h264enc_cam1,
      h264parse_cam1, mp4mux_cam1, filesink_cam1, NULL);
  if (!ret) {
    g_printerr (
        "\n first camera pipeline video encoder elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmf_cam1, capsfilter_cam1,
        v4l2h264enc_cam1, h264parse_cam1, mp4mux_cam1, filesink_cam1, NULL);
    return FALSE;
  }

  ret = gst_element_link_many (qtiqmmf_cam2, capsfilter_cam2, v4l2h264enc_cam2,
      h264parse_cam2, mp4mux_cam2, filesink_cam2, NULL);
  if (!ret) {
    g_printerr (
        "\n second camera pipeline video encoder elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmf_cam2,
      capsfilter_cam2, v4l2h264enc_cam2, h264parse_cam2,mp4mux_cam2,filesink_cam2, NULL);
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmf_cam1);
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmf_cam2);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_cam1);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_cam2);
  appctx->plugins = g_list_append (appctx->plugins, v4l2h264enc_cam1);
  appctx->plugins = g_list_append (appctx->plugins, h264parse_cam1);
  appctx->plugins = g_list_append (appctx->plugins, mp4mux_cam1);
  appctx->plugins = g_list_append (appctx->plugins, v4l2h264enc_cam2);
  appctx->plugins = g_list_append (appctx->plugins, h264parse_cam2);
  appctx->plugins = g_list_append (appctx->plugins, mp4mux_cam2);
  appctx->plugins = g_list_append (appctx->plugins, filesink_cam1);
  appctx->plugins = g_list_append (appctx->plugins, filesink_cam2);

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
  GstMultiCamAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "width", "camera width" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
      "camera height" },
    { "output", 'o', 0, G_OPTION_ARG_INT, &appctx->sinktype,
      "\t\t\t\t\t   output",
      "\n\t0-DISPLAY"
      "\n\t1-FILE" },
    { NULL }
    };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("gst-multi-camera-example")) != NULL) {
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

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-multi-camera-example");

  //check the Input value
  if (appctx->sinktype > GST_VIDEO_ENCODE) {
    g_printerr ("\n Invalid user Input:gst-multi-camera-example --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  switch (appctx->sinktype) {
    case GST_VIDEO_ENCODE:
      ret = create_camera_video_pipe (appctx);
      break;
    case GST_WAYLANDSINK:
      ret = create_camera_wayland_pipe (appctx);
      break;
    default:
      g_printerr ("\n Invalid output type selected.\n");
      gst_app_context_free (appctx);
      return -1;
    }

  if (!ret) {
      g_printerr ("\n Failed to create GST pipe.\n");
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

  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (state_changed_cb),
      pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("\n Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
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

  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  if (appctx->sinktype == GST_VIDEO_ENCODE)
    g_print ("\n Encoded files are in %s  %s \n", DEFAULT_OUTPUT_FILENAME_CAM1,
        DEFAULT_OUTPUT_FILENAME_CAM2);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
