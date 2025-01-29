/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* Gstreamer application to demonstrate snapshot capture with preview stream.
*
* Description:
* This application creates one preview stream and snapshot stream.
* One stream preview to display and another stream capture snapshots
* and dump to a file in JPEG format. Number of snapshot count is user
* defined.
*
* Usage:
* gst-snapshot-stream-example -W <input_width> -H <input_height>
*                             -w <snap_width> -h <snap_height>
*                             -c <max-snap-limit>
*
* Help:
* gst-snapshot-stream-example --help
*
* ********************************************************
* Snapshot pipeline:
*              |---->capsfilter->waylandsink
*              |
* qtiqmmfsrc---|
*              |
*              |---->capsfilter->multifilesink
* ********************************************************
*/

#include <stdlib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_CAMERA_WIDTH 1280
#define DEFAULT_CAMERA_HEIGHT 720

#define DEFAULT_SNAPSHOT_WIDTH 3840
#define DEFAULT_SNAPSHOT_HEIGHT 2160

#define ARRAY_LENGTH 100

#define DEFAULT_SNAP_OUTPUT_PATH "/opt"
#define SNAP_OUTPUT_FILE "snapshot%d.jpg"

#define DEFAULT_MAX_SNAPSHOTS 5

#define GST_APP_SUMMARY \
  "This application facilitates the creation of two streams: a preview stream" \
  " and a snapshot stream. The preview stream is used for display purposes,\n" \
  " while the snapshot stream captures snapshots and saves them to a file in" \
  " JPEG format. \n The number of snapshots taken is determined by the user." \
  "\nCommand:\n" \
  "  gst-snapshot-stream-example -W 1280 -H 720 -w 3840 -h 2160 -c 5" \
  "\nOutput:\n" \
  "  Upon execution, the application will generate an output for preview " \
  "on the display. \n  Once the use case concludes, snapshot output files will" \
  " be available at the '/opt/' directory unless custom output directory set."

// Structure to hold the application context
struct GstSnapshotAppContext : GstAppContext {
  gint snapcount;
  gint input_width;
  gint input_height;
  gint snap_width;
  gint snap_height;
  gchar *output_path;
};

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstSnapshotAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstSnapshotAppContext *ctx = (GstSnapshotAppContext *) g_new0 (GstSnapshotAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context\n");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->snapcount = DEFAULT_MAX_SNAPSHOTS;
  ctx->input_width = DEFAULT_CAMERA_WIDTH;
  ctx->input_height = DEFAULT_CAMERA_HEIGHT;
  ctx->snap_width = DEFAULT_SNAPSHOT_WIDTH;
  ctx->snap_height = DEFAULT_SNAPSHOT_HEIGHT;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstSnapshotAppContext * appctx)
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

  if (appctx->output_path != (gchar *)(
      &DEFAULT_SNAP_OUTPUT_PATH) &&
      appctx->output_path != NULL) {
    g_free ((gpointer)appctx->output_path);
  }

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
create_pipe (GstSnapshotAppContext * appctx)
{
  GstElement *qtiqmmfsrc, *capsfilter_prev, *capsfilter_snap;
  GstElement *multifilesink, *waylandsink;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  gchar temp_str[100];

  // Create camera source and the element capability
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter_prev = gst_element_factory_make ("capsfilter", "capsfilter_prev");
  capsfilter_snap = gst_element_factory_make ("capsfilter", "capsfilter_snap");

  // Create the multifilesink element
  multifilesink = gst_element_factory_make ("multifilesink", "multifilesink");

  // Create the waylandsink element
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !capsfilter_prev || !capsfilter_snap || !multifilesink
      || !waylandsink) {
    g_printerr ("One element could not be created. Exiting.\n");
    unref_elements (qtiqmmfsrc, capsfilter_prev, capsfilter_snap,
        multifilesink, waylandsink, "NULL");
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_prev);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter_snap);
  appctx->plugins = g_list_append (appctx->plugins, multifilesink);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // Set location and max file limit properties to multifilesink
  snprintf (temp_str, sizeof (temp_str), "%s/%s",
      appctx->output_path, SNAP_OUTPUT_FILE);
  g_object_set (G_OBJECT (multifilesink), "location", temp_str, NULL);
  g_object_set (G_OBJECT (multifilesink), "enable-last-sample", false, NULL);
  g_object_set (G_OBJECT (multifilesink), "max-files", appctx->snapcount, NULL);

  // Configure the preview stream capabilities based on width and height
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->input_width,
      "height", G_TYPE_INT, appctx->input_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

  g_object_set (G_OBJECT (capsfilter_prev), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the snapshot stream capabilities based on width and height
  filtercaps = gst_caps_new_simple ("image/jpeg",
      "width", G_TYPE_INT, appctx->snap_width,
      "height", G_TYPE_INT, appctx->snap_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter_snap), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_prev,
      capsfilter_snap, multifilesink, waylandsink, NULL);

  // Link the preview stream
  ret = gst_element_link_many (qtiqmmfsrc, capsfilter_prev, waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_prev,
        capsfilter_snap, multifilesink, waylandsink, NULL);
    return FALSE;
  }

  // Link the snapshot stream
  ret = gst_element_link_many (qtiqmmfsrc, capsfilter_snap, multifilesink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_prev,
        capsfilter_snap, multifilesink, waylandsink, NULL);
    return FALSE;
  }

  g_print ("All elements are linked successfully\n");

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GstSnapshotAppContext *appctx = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gboolean ret = -1;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return ret;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "input_width", 'W', 0, G_OPTION_ARG_INT,
      &appctx->input_width,
      "camera input width", "-Default width:1280"
    },
    { "input_height", 'H', 0, G_OPTION_ARG_INT,
      &appctx->input_height,
      "camera input height", "-Default height:720"
    },
    { "snap_width", 'w', 0, G_OPTION_ARG_INT,
      &appctx->snap_width,
      "snapshot image width", "-Default width:3840"
    },
    { "snap_height", 'h', 0, G_OPTION_ARG_INT,
      &appctx->snap_height,
      "snapshot image height", "-Default height:2160"
    },
    { "snapcount", 'c', 0, G_OPTION_ARG_INT,
      &appctx->snapcount,
      "max number of snapshot count", "-Default count:5"
    },
    { "output_path", 'o', 0, G_OPTION_ARG_STRING,
      &appctx->output_path,
      "Path to save snapshot images to.",
      "-Default path: /opt"
    },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
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
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return ret;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return ret;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    gst_app_context_free (appctx);
    return ret;
  }

  // Set default snap output path if none set in arguments.
  if (NULL == appctx->output_path) {
    appctx->output_path = (gchar *)DEFAULT_SNAP_OUTPUT_PATH;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  appctx->pipeline = gst_pipeline_new ("gst-snapshot-stream-example");
  if (!appctx->pipeline) {
    g_printerr ("failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return ret;
  }

  // Build the pipeline
  if (!create_pipe (appctx)) {
    g_printerr ("failed to create GST pipeline.\n");
    gst_app_context_free (appctx);
    return ret;
  }

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return ret;
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return ret;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), appctx->mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx->mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("Failed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return ret;
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

  g_print ("\n Application is running\n");
  g_main_loop_run (appctx->mloop);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  ret = 0;

  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("gst_deinit\n");
  gst_deinit ();

  return ret;
}
