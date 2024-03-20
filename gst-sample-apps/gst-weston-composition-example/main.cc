/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* Gstreamer weston composition for picture in picture and side by side
*
* Description:
* This application Demonstrates compostion using waylandsink
* and qtivcomposer for both picture in picture and side by side usecases.
* One input is from camera source and other is from AVC mp4 file source.
*
* Help:
* gst-weston-composition-example --help
*
* Usage:
* For waylandsink composing picture in picture:
* gst-weston-composition-example -c 0 -t 0 -i /opt/<h264_file>.mp4
* For waylandsink composing side by side:
* gst-weston-composition-example -c 0 -t 1 -i /opt/<h264_file>.mp4
* For qtivcomposer composing picture in picture:
* gst-weston-composition-example -c 1 -t 0 -i /opt/<h264_file>.mp4
* For qtivcomposer composing side by side:
* gst-weston-composition-example -c 1 -t 1 -i /opt/<h264_file>.mp4
*
* ***********************************************************************
* For waylandsink composition pipeline:
*                   qtiqmmfsrc->capsfilter->|
                                            |->waylandsink
* filesrc->qtdemux->h264parse->v4l2h264dec->|
*
* For qtivcomposer composition pipeline:
*                   qtiqmmfsrc->capsfilter->|
                                            |->qtivcomposer->waylandsink
* filesrc->qtdemux->h264parse->v4l2h264dec=>|
* ***********************************************************************
*/

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

#define GST_APP_SUMMARY                                                       \
  "This app enables the users for weston and qtivcomposer composition\n"      \
  "for both picture in picture and side by side\n"                            \
  "\nFor waylandsink composing picture in picture:\n"                         \
  "gst-weston-composition-example -c 0 -t 0 -i /opt/<h264_file>.mp4\n"        \
  "\nFor waylandsink composing side by side:\n"                               \
  "gst-weston-composition-example -c 0 -t 1 -i /opt/<h264_file>.mp4\n"        \
  "\nFor qtivcomposer composing picture in picture:\n"                        \
  "gst-weston-composition-example -c 1 -t 0 -i /opt/<h264_file>.mp4\n"        \
  "\nFor qtivcomposer composing side by side:\n"                              \
  "gst-weston-composition-example -c 1 -t 1 -i /opt/<h264_file>.mp4\n"

// Structure to hold the application context
struct GstComposeAppContext : GstAppContext {
  gchar *input_file;
  GstAppCompositionType composition;
  GstAppComposerOutput composer;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstComposeAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstComposeAppContext *ctx = (GstComposeAppContext *) g_new0 (GstComposeAppContext, 1);

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
  ctx->composition = GST_PIP_COMPOSE;
  ctx->composer = GST_APP_OUTPUT_WAYLANDSINK;
  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstComposeAppContext * appctx)
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

  // Finally, free the application context itself
  if (appctx != NULL)
    g_free (appctx);
}

/**
 * Create pad property parser callback
 *
 * @param Gst Element parser
 * @param pad to link with sinkpad
 * @param data pointer
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *h264parse = (GstElement *) data;

  sinkpad = gst_element_get_static_pad (h264parse, "sink");

  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
}

/**
 * Create pad property to set the position and dimensions for composition
 *
 * @param property to set position and dimension
 * @param values to be set for position and dimension
 * @param number of index as an integer
 */
static void
build_pad_property (GValue * property, gint values[], gint num)
{
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);

  for (int idx = 0; idx < num; idx++) {
    g_value_set_int (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

/**
 * Create the pipeline for waylandsink composition
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_pipe_waylandsink (GstComposeAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *pipeline, *qtiqmmfsrc, *capsfilter, *waylandsink_cam,
      *filesrc, *qtdemux, *h264parse, *v4l2h264dec, *waylandsink_filesrc;
  GstCaps *filtercaps;

  gboolean ret = FALSE;
  appctx->plugins = NULL;

  // Create camera source element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  // Set the source elements capability
  filtercaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
      "NV12", "width", G_TYPE_INT, 1280, "height", G_TYPE_INT, 720, "framerate",
      GST_TYPE_FRACTION, 30, 1, "compression", G_TYPE_STRING, "ubwc", NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // create the sink element for camera feed and set the position and dimensions
  waylandsink_cam = gst_element_factory_make ("waylandsink", "waylandsink_cam");
  g_object_set (G_OBJECT (waylandsink_cam), "async", true, NULL);
  g_object_set (G_OBJECT (waylandsink_cam), "sync", false, NULL);

  // Camera preview will start from top left corner
  g_object_set (G_OBJECT (waylandsink_cam), "x", 0, NULL);
  g_object_set (G_OBJECT (waylandsink_cam), "y", 0, NULL);
  if (appctx->composition == GST_PIP_COMPOSE) {
    // For PIP camera preview will be in small window: dimesion are 320*240
    g_object_set (G_OBJECT (waylandsink_cam), "width", 320, NULL);
    g_object_set (G_OBJECT (waylandsink_cam), "height", 240, NULL);
  } else {
    // For SIDE_BY_SIDE we divide screen into two euqal parts
    g_object_set (G_OBJECT (waylandsink_cam), "width", 640, NULL);
    g_object_set (G_OBJECT (waylandsink_cam), "height", 480, NULL);
  }

  // Create Source element for reading from a file
  filesrc = gst_element_factory_make ("filesrc", "filesrc");

  // Set the location property of the source element
  g_object_set (G_OBJECT (filesrc), "location", appctx->input_file, NULL);

  // Create Demuxer element to get video track
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");

  // create the video parse element
  h264parse = gst_element_factory_make ("h264parse", "h264parse");

  // create the video decoder element
  v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
  g_object_set(G_OBJECT(v4l2h264dec), "capture-io-mode", 5, NULL);
  g_object_set(G_OBJECT(v4l2h264dec), "output-io-mode", 5, NULL);

  // create the sink element for file source and set the position and dimensions
  waylandsink_filesrc = gst_element_factory_make ("waylandsink", "waylandsink_filesrc");
  g_object_set (G_OBJECT (waylandsink_filesrc), "async", true, NULL);
  if (appctx->composition == GST_PIP_COMPOSE) {
    // For PIP filesrc is full screen and camera preview is in small window
    g_object_set (G_OBJECT (waylandsink_filesrc), "width", 1280, NULL);
    g_object_set (G_OBJECT (waylandsink_filesrc), "height", 720, NULL);
    g_object_set (G_OBJECT (waylandsink_filesrc), "x", 0, NULL);
    g_object_set (G_OBJECT (waylandsink_filesrc), "y", 0, NULL);
  } else {
    // For SIDE_BY_SIDE divide screen into two euqal parts for camera & filesrc
    g_object_set (G_OBJECT (waylandsink_filesrc), "width", 640, NULL);
    g_object_set (G_OBJECT (waylandsink_filesrc), "height", 480, NULL);
    g_object_set (G_OBJECT (waylandsink_filesrc), "x", 640, NULL);
    g_object_set (G_OBJECT (waylandsink_filesrc), "y", 0, NULL);
  }

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
      waylandsink_cam, filesrc, qtdemux, h264parse, v4l2h264dec,
      waylandsink_filesrc, NULL);

  g_print ("\n Linking waylandsink composer elements ..\n");

  ret = gst_element_link_many (qtiqmmfsrc, capsfilter, waylandsink_cam, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        waylandsink_cam, filesrc, qtdemux, h264parse, v4l2h264dec,
        waylandsink_filesrc, NULL);
    return FALSE;
  }

  // Linking video streams element
  gst_element_link (filesrc, qtdemux);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        waylandsink_cam, filesrc, qtdemux, h264parse, v4l2h264dec,
        waylandsink_filesrc, NULL);
    return FALSE;
  }
  ret = gst_element_link_many (h264parse, v4l2h264dec, waylandsink_filesrc, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        waylandsink_cam, filesrc, qtdemux, h264parse, v4l2h264dec,
        waylandsink_filesrc, NULL);
    return FALSE;
  }

  // link demux video track pad to video queue
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), h264parse);

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink_cam);
  appctx->plugins = g_list_append (appctx->plugins, filesrc);
  appctx->plugins = g_list_append (appctx->plugins, qtdemux);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, v4l2h264dec);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink_filesrc);
  g_print ("\n All elements are linked successfully\n");
  return TRUE;
}

/**
 * Create the qtivcomposer composition pipeline
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_pipe_qtivcomposer (GstComposeAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *pipeline, *qtiqmmfsrc, *waylandsink, *capsfilter;
  GstElement *filesrc, *qtdemux, *h264parse, *v4l2h264dec, *qtivcomposer;
  GstCaps *filtercaps;
  guint ret = FALSE;
  GstPad *composer_sink_1, *composer_sink_2;

  // Create Source element for reading from a file and set the location
  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  g_object_set (G_OBJECT (filesrc), "location", appctx->input_file, NULL);

  // Create Demuxer element to get video track
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");

  // create the video decoder and parse element
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
  g_object_set(G_OBJECT(v4l2h264dec), "capture-io-mode", 5, NULL);
  g_object_set(G_OBJECT(v4l2h264dec), "output-io-mode", 5, NULL);

  // create camera source element and add capsfilter
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  filtercaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1280, "height", G_TYPE_INT, 720, "framerate",
      GST_TYPE_FRACTION, 30, 1,"compression", G_TYPE_STRING, "ubwc", NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // create qtivcomposer element to combine 2 i/p streams as in single display
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");

  // create waylandsink element to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", true, NULL);
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
      qtivcomposer, filesrc, qtdemux, h264parse, v4l2h264dec, waylandsink, NULL);

  g_print ("\n Linking qtivcomposer elements ..\n");

  ret = gst_element_link_many (qtiqmmfsrc, capsfilter, qtivcomposer,
      waylandsink, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
        capsfilter, qtivcomposer, waylandsink, NULL);
    return FALSE;
  }

  // Link filesrc video streams element
  ret = gst_element_link (filesrc, qtdemux);
  if (!ret) {
    g_printerr (
        "\n Pipeline elements filesrc and qtdemux cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc, qtdemux, NULL);
  }
  ret = gst_element_link_many (h264parse, v4l2h264dec, qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), h264parse, v4l2h264dec,
        qtivcomposer, NULL);
  }

  // link demux video track pad to video parse
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), h264parse);

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
  appctx->plugins = g_list_append (appctx->plugins, filesrc);
  appctx->plugins = g_list_append (appctx->plugins, qtdemux);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, v4l2h264dec);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // As we have two stream to compose create two pad/ports for qtivcomposer
  composer_sink_1 = gst_element_get_static_pad (qtivcomposer, "sink_0");
  composer_sink_2 = gst_element_get_static_pad (qtivcomposer, "sink_1");

  if (composer_sink_1 == NULL || composer_sink_2 == NULL) {
    g_printerr ("\n One or more sink pads are not available");
    return FALSE;
  }

  // Create and set the position and dimensions for qtivcomposer
  GValue pos1 = G_VALUE_INIT, dim1 = G_VALUE_INIT;
  g_value_init (&pos1, GST_TYPE_ARRAY);
  g_value_init (&dim1, GST_TYPE_ARRAY);

  // check the composition type and set the position and dimensions for sink1
  if (appctx->composition == GST_PIP_COMPOSE) {
    gint pos1_vals[] = { 0, 0 };
    gint dim1_vals[] = { 1280, 720 };

    build_pad_property (&pos1, pos1_vals, 2);
    build_pad_property (&dim1, dim1_vals, 2);
  } else {
    gint pos1_vals[] = { 0, 0 };
    gint dim1_vals[] = { 640, 480 };

    build_pad_property (&pos1, pos1_vals, 2);
    build_pad_property (&dim1, dim1_vals, 2);
  }

  g_object_set_property (G_OBJECT (composer_sink_1), "position", &pos1);
  g_object_set_property (G_OBJECT (composer_sink_1), "dimensions", &dim1);

  // check the composition type and set the position and dimensions for sink2
  GValue pos2 = G_VALUE_INIT, dim2 = G_VALUE_INIT;
  g_value_init (&pos2, GST_TYPE_ARRAY);
  g_value_init (&dim2, GST_TYPE_ARRAY);

  if (appctx->composition == GST_PIP_COMPOSE) {
    gint pos2_vals[] = { 0, 0 };
    gint dim2_vals[] = { 320, 240 };

    build_pad_property (&pos2, pos2_vals, 2);
    build_pad_property (&dim2, dim2_vals, 2);
  } else {
    gint pos2_vals[] = { 640, 0 };
    gint dim2_vals[] = { 640, 480 };

    build_pad_property (&pos2, pos2_vals, 2);
    build_pad_property (&dim2, dim2_vals, 2);
  }

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

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstComposeAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-weston-composition-example --help \n");
    return -1;
  }

  // create the app context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "composer", 'c', 0, G_OPTION_ARG_INT, &appctx->composer,
      "Select the composer Wayland or qtivcomposer"
      "\n\t0 - Wayland"
      "\n\t1 - Qtivcomposer"
      "  e.g. -c 0 or -c 1 "
    },
    { "type", 't', 0, G_OPTION_ARG_INT, &appctx->composition,
      "\t\t Select the composition type pip or side-by-side",
      "\n\t0-PIP"
      "\n\t1-SIDE_BY_SIDE"
      "  e.g. -t 0 or -t 1 "
    },
    { "input_file", 'i', 0, G_OPTION_ARG_FILENAME, &appctx->input_file,
      "input AVC mp4 Filename"
      "  e.g. -i /opt/<h264_file>.mp4"
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("gst-weston-composition-example")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_set_summary (ctx, GST_APP_SUMMARY);
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
  if (appctx->composer > GST_APP_OUTPUT_QTIVCOMPOSER ||
      (appctx->composition > GST_SIDE_BY_SIDE_COMPOSE) ||
      appctx->input_file == NULL) {
    g_printerr ("\n one of input parameters is not given -c %d -t %d -i %s\n",
        appctx->composer, appctx->composition, appctx->input_file);
    g_print ("\n usage: gst-weston-composition-example --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-weston-composition-example");

  // Create empty pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline based on user input
  if (appctx->composer == GST_APP_OUTPUT_WAYLANDSINK) {
    ret = create_pipe_waylandsink (appctx);
    if (!ret) {
      gst_app_context_free (appctx);
      return -1;
    }
  } else if (appctx->composer == GST_APP_OUTPUT_QTIVCOMPOSER) {
    ret = create_pipe_qtivcomposer (appctx);
    if (!ret) {
      gst_app_context_free (appctx);
      return -1;
    }
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

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("Setting pipeline to PAUSED state ...\n");
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

  // Start the main loop
  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
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
