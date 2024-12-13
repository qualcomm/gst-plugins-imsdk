/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application:
 * GStreamer Switch cameras in Playing state
 *
 * Description:
 * This application uses the two cameras of the device and switch them
 * without changing the state of the pipeline. The switching is done in
 * Playing state every 5 seconds.
 *
 * Usage:
 * gst-camera-switch-example
 *
 * Help:
 * gst-camera-switch-example --help
 *
 * Parameters:
 * -d - Enable display
 *
 */

#include <glib-unix.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define GST_APP_SUMMARY "This application uses the two cameras of the " \
  "device and switch them without changing the state of the pipeline. \n" \
  "The switching is done in Playing state every 5 seconds. \n" \
  "\nCommand:\n" \
  "For Display Stream \n" \
  "  gst-camera-switch-example -d \n" \
  "For Encode Stream(Default option) \n" \
  "  gst-camera-switch-example \n" \
  "\nOutput:\n" \
  "  Upon execution, application will generates output as preview OR " \
  "encoded mp4 file."

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

// Contains app context information
struct GstCameraSwitchCtxStruct {
  GstElement *pipeline;
  GList *plugins;
  GMainLoop *mloop;
  GstElement *current_camsrc;
  GstElement *capsfilter;
  GstElement *waylandsink;
  gboolean is_camera0;
  GMutex lock;
  gboolean exit;
  gboolean use_display;
  guint camera0_id;
  guint camera1_id;
};
typedef struct GstCameraSwitchCtxStruct GstCameraSwitchCtx;

/**
 * In case of ASYNC state change it will properly wait for state change
 *
 * @param element gst element object.
 */
static gboolean
wait_for_state_change (GstElement *element) {
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Element is PREROLLING ...\n");

  ret = gst_element_get_state (element, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Element failed to PREROLL!\n");
    return FALSE;
  }
  return TRUE;
}

/**
 * Switch Camera involves 3 main steps
 * 1. Create next camera elements and Sync state to curtent pipeline state
 * 2. Unlink the current camera stream
 * 3. Link the next camera stream
 *
 * @param cameraswitchctx Application Context Object.
 */
static gboolean
switch_camera (GstCameraSwitchCtx *cameraswitchctx)
{
  GstElement *new_camsrc = NULL;
  GstElement *current_camsrc = cameraswitchctx->current_camsrc;

  g_print ("\n\nSwitch_camera...\n");

  if (!cameraswitchctx->is_camera0) {
    new_camsrc = gst_element_factory_make ("qtiqmmfsrc", "camsrc_0");
    g_object_set (G_OBJECT (new_camsrc), "camera", cameraswitchctx->camera0_id,
        NULL);
  } else {
    new_camsrc = gst_element_factory_make ("qtiqmmfsrc", "camsrc_1");
    g_object_set (G_OBJECT (new_camsrc), "camera", cameraswitchctx->camera1_id,
        NULL);
  }

  // Adding qmmfsrc
  gst_bin_add (GST_BIN (cameraswitchctx->pipeline), new_camsrc);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (new_camsrc);

  // Unlink the current camera stream
  g_print ("Unlinking current camera stream...\n");
  gst_element_unlink (current_camsrc, cameraswitchctx->capsfilter);
  g_print ("Unlinked current camera stream successfully \n");

  // Link the next camera stream
  g_print ("Linking next camera stream...\n");
  if (!gst_element_link (new_camsrc, cameraswitchctx->capsfilter)) {
    g_printerr ("Error: Link cannot be done!\n");
    return FALSE;
  }
  g_print ("Linked next camera stream successfully \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (current_camsrc, GST_STATE_NULL);

  // Wait for state change to complete
  if (!wait_for_state_change (current_camsrc)) {
    g_printerr ("Error: Set state failed for element!\n");
    return FALSE;
  }

  gst_bin_remove (GST_BIN (cameraswitchctx->pipeline), current_camsrc);

  cameraswitchctx->is_camera0 = !cameraswitchctx->is_camera0;
  cameraswitchctx->current_camsrc = new_camsrc;

  return TRUE;
}

/**
 * Create thread function to switch the camera
 * 1. Call switch_camera function
 * 2. Check if user wants to exit the application
 *
 * @param user_data Application Context Object.
 */
static void *
thread_fn (gpointer user_data)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) user_data;

  while (true) {
    sleep (5);
    g_mutex_lock (&cameraswitchctx->lock);
    if (cameraswitchctx->exit) {
      g_mutex_unlock (&cameraswitchctx->lock);
      return NULL;
    }
    g_mutex_unlock (&cameraswitchctx->lock);

    if (!switch_camera (cameraswitchctx)) {
      g_printerr ("Failed to switch camera...Exiting.\n");
      return NULL;
    }
  }

  return NULL;
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin and connect pad signal
 * 3. Link plugins to create GST pipeline
 *
 * @param cameraswitchctx Application Context Object.
 */
static gboolean
create_pipe (GstCameraSwitchCtx *cameraswitchctx)
{
  GstCaps *filtercaps;
  GstElement *camsrc = NULL;
  GstElement *capsfilter = NULL;
  GstElement *waylandsink = NULL;
  GstElement *encoder = NULL;
  GstElement *filesink = NULL;
  GstElement *h264parse = NULL;
  GstElement *mp4mux = NULL;

  // Create qmmfsrc element
  camsrc = gst_element_factory_make ("qtiqmmfsrc", "camsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  // Check if all elements are created successfully
  if (!camsrc || !capsfilter) {
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }

  // Set qmmfsrc 0 properties
  g_object_set (G_OBJECT (camsrc), "camera", cameraswitchctx->camera0_id, NULL);

  // Set capsfilter properties
  g_object_set (G_OBJECT (capsfilter), "name", "capsfilter", NULL);

  // Set caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, DEFAULT_WIDTH,
      "height", G_TYPE_INT, DEFAULT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "compression", G_TYPE_STRING, "ubwc",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  cameraswitchctx->current_camsrc = camsrc;
  cameraswitchctx->capsfilter = capsfilter;
  cameraswitchctx->is_camera0 = true;

  if (cameraswitchctx->use_display) {
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    // Check if all elements are created successfully
    if (!waylandsink) {
      g_printerr ("waylandsink could not be created of found. Exiting.\n");
      return FALSE;
    }
    cameraswitchctx->waylandsink = waylandsink;
    // Set waylandsink properties
    g_object_set (G_OBJECT (waylandsink), "name", "waylandsink", NULL);
    g_object_set (G_OBJECT (waylandsink), "x", 0, NULL);
    g_object_set (G_OBJECT (waylandsink), "y", 0, NULL);
    g_object_set (G_OBJECT (waylandsink), "width", 600, NULL);
    g_object_set (G_OBJECT (waylandsink), "height", 400, NULL);
    g_object_set (G_OBJECT (waylandsink), "enable-last-sample", false, NULL);

    // Add qmmfsrc to the pipeline
    gst_bin_add_many (GST_BIN (cameraswitchctx->pipeline), camsrc, capsfilter,
        waylandsink, NULL);

    // Link the elements
    if (!gst_element_link_many (camsrc, capsfilter, waylandsink, NULL)) {
      g_printerr ("Error: Link cannot be done!\n");
      return FALSE;
    }
  } else {
    encoder = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    filesink = gst_element_factory_make ("filesink", "filesink");
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

    // Check if all elements are created successfully
    if (!encoder || !filesink || !h264parse || !mp4mux) {
      g_printerr ("Encoder's elements could not be created of found. Exiting.\n");
      return FALSE;
    }

    // Set encoder properties
    g_object_set (G_OBJECT (encoder), "capture-io-mode", 5, NULL);
    g_object_set (G_OBJECT (encoder), "output-io-mode", 5, NULL);

    g_object_set (G_OBJECT (h264parse), "name", "h264parse", NULL);
    g_object_set (G_OBJECT (mp4mux), "name", "mp4mux", NULL);
    g_object_set (G_OBJECT (filesink), "name", "filesink", NULL);
    g_object_set (G_OBJECT (filesink), "location", "/opt/mux.mp4", NULL);
    g_object_set (G_OBJECT (filesink), "enable-last-sample", false, NULL);

    // Add qmmfsrc to the pipeline
    gst_bin_add_many (GST_BIN (cameraswitchctx->pipeline), camsrc, capsfilter,
        encoder, h264parse, mp4mux, filesink, NULL);

    // Link the elements
    if (!gst_element_link_many (camsrc, capsfilter, encoder, h264parse, mp4mux,
            filesink, NULL)) {
      g_printerr ("Error: Link cannot be done!\n");
      return FALSE;
    }
  }

  g_print ("All elements are linked successfully\n");

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GstElement *pipeline = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  GstCameraSwitchCtx cameraswitchctx = {};
  cameraswitchctx.exit = false;
  cameraswitchctx.use_display = false;
  cameraswitchctx.camera0_id = 0;
  cameraswitchctx.camera1_id = 1;
  g_mutex_init (&cameraswitchctx.lock);

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  GOptionEntry entries[] = {
      { "display", 'd', 0, G_OPTION_ARG_NONE,
        &cameraswitchctx.use_display,
        "Enable display",
        "Parameter for enable display output"
      },
      { "camera0_id", 'm', 0, G_OPTION_ARG_INT,
        &cameraswitchctx.camera0_id,
        "ID of camera0",
        NULL,
      },
      { "camera1_id", 's', 0, G_OPTION_ARG_INT,
        &cameraswitchctx.camera1_id,
        "ID of camera1",
        NULL,
      },
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
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      return -1;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    return -1;
  }

  g_print ("Using camera0 id = %d and camera1 id = %d\n",
      cameraswitchctx.camera0_id, cameraswitchctx.camera1_id);

  pipeline = gst_pipeline_new ("gst-cameraswitch");
  cameraswitchctx.pipeline = pipeline;

  // Check if all elements are created successfully
  if (!pipeline) {
    g_printerr ("Failed to create pipeline...Exiting.\n");
    return -1;
  }

  // Build the pipeline
  ret = create_pipe (&cameraswitchctx);
  if (!ret) {
    g_printerr ("failed to create GST pipe.\n");
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }
  cameraswitchctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
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
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal,
      &cameraswitchctx);

  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PLAYING)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PLAY state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      g_main_loop_unref (mloop);
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

  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, &cameraswitchctx);

  // Run main loop.
  g_print ("Application Running\n");
  g_main_loop_run (mloop);
  g_print ("Stop application\n");

  g_mutex_lock (&cameraswitchctx.lock);
  cameraswitchctx.exit = true;
  g_mutex_unlock (&cameraswitchctx.lock);
  pthread_join (thread, NULL);

  g_print ("Setting pipeline to NULL state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_NULL)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("Failed to transition to state NULL!\n");
      g_source_remove (intrpt_watch_id);
      g_main_loop_unref (mloop);
      g_mutex_clear (&cameraswitchctx.lock);
      gst_object_unref (pipeline);
      return -1;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\n Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("\n Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  g_mutex_clear (&cameraswitchctx.lock);
  gst_object_unref (pipeline);

  // Deinitialize the GST library
  g_print ("gst_deinit\n");
  gst_deinit ();
  return 0;
}
