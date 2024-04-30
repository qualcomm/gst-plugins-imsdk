/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Monodepth on Live stream.
 *
 * Description:
 * The application takes live video stream from camera and gives same to
 * 2 parallel processing stream. One Stream to display scaled down preview with
 * Midasv2 TensorFlow Lite or SNPE DLC overlayed AI Model output. And
 * other stream to display live camera feed.
 *
 * Pipeline for Gstreamer Monodepth (2 Stream) below:-
 *
 *                        | -> qmmfsrc_caps -> waylandsink (Display)
 *                        |
 *                        |
 * qtiqmmfsrc (camera) -> |
 *                        | -> qmmfsrc_caps -> pre process -> ML Framework ->
 *                        |    -> postprocess -> qtivtransform ->
 *                        |    -> fpsdisplaysink (Display)
 *
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post process: qtimlvsegmentation -> segmentation_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <stdarg.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "include/gst_sample_apps_utils.h"

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_SNPE_MONODEPTH_MODEL "/opt/midasv2.dlc"
#define DEFAULT_TFLITE_MONODEPTH_MODEL "/opt/midasv2.tflite"
#define DEFAULT_MONODEPTH_LABELS "/opt/monodepth.labels"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define PREVIEW_OUTPUT_WIDTH 1920
#define PREVIEW_OUTPUT_HEIGHT 1080
#define MONODEPTH_OUTPUT_WIDTH 1280
#define MONODEPTH_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Channel mean subtraction values for FLOAT tensors for qtimlvconverter plugin
 */
#define MEAN_R 123.675
#define MEAN_G 116.28
#define MEAN_B 103.53

/**
 * Channel divisor values for FLOAT tensors for qtimlvconverter plugin
 */
#define SIGMA_R 58.395
#define SIGMA_G 57.12
#define SIGMA_B 57.375

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 2

/**
 * Number of Stream in Pipeline
 */
#define STREAM_COUNT 2

/**
 * Build Property for pad.
 *
 * @param property Property Name.
 * @param values Value of Property.
 * @param num count of Property Values.
 */
static void
build_pad_property (GValue * property, gdouble values[], gint num)
{
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_DOUBLE);

  for (gint idx = 0; idx < num; idx++) {
    g_value_set_double (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

/*
 * Update Window Grid
 * Change position of grid as per display resolution
 */
static void
update_window_grid (GstVideoRectangle * position)
{
  gint width, height;
  if (get_active_display_mode (&width, &height)) {
    gint win_w = width / 2;
    gint win_h = height / 2;
    position[0] = {win_w, 0, win_w, 2 * win_h};
    position[1] = {0, 0, win_w, 2 * win_h};
  } else {
    position[0] = {960, 0, 960, 1080};
    position[1] = {0, 0, 960, 1080};
    g_warning ("Failed to get active display mode, using 1080p default config");
  }
}

// Recieves a list of pointers to variable containing pointer to gst element
// and unrefs the gst element if needed
static void
cleanup_many_gst (void * first_elem, ...)
{
  va_list args;
  void **p_gst_obj = (void **)first_elem;

  va_start (args, first_elem);
  while (p_gst_obj) {
    if (*p_gst_obj)
      gst_object_unref (*p_gst_obj);
    p_gst_obj = va_arg (args, void **);
  }
  va_end (args);
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 * @param model_type Type of Model container for the Runtime.
 * @param model_path Location of Model Container.
 * @param labels_path Location of Model Labels.
 */
static gboolean
create_pipe (GstAppContext * appctx, GstModelType model_type,
    const gchar * model_path, const gchar * labels_path)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps_preview = NULL;
  GstElement *qmmfsrc_caps_monodepth = NULL, *queue[QUEUE_COUNT];
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvsegmentation = NULL, *segmentation_filter = NULL;
  GstElement *qtivtransform = NULL, *transform_filter = NULL;
  GstElement *fpsdisplaysink = NULL, *waylandsink_preview = NULL;
  GstElement *waylandsink_monodepth = NULL;
  GstCaps *pad_filter = NULL , *filtercaps = NULL;
  GstPad *qtiqmmfsrc_type = NULL;
  GstStructure *delegate_options = NULL;
  GstVideoRectangle position[STREAM_COUNT];
  gboolean ret = FALSE;
  gchar element_name[128];
  gint preview_width = PREVIEW_OUTPUT_WIDTH;
  gint preview_height = PREVIEW_OUTPUT_HEIGHT;
  gint monodepth_width = MONODEPTH_OUTPUT_WIDTH;
  gint monodepth_height = MONODEPTH_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;
  GValue mean = G_VALUE_INIT, sigma = G_VALUE_INIT;
  GValue video_type = G_VALUE_INIT;
  gdouble mean_vals[3], sigma_vals[3];

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }

  update_window_grid (position);

  // 1. Create the elements or Plugins
  // Create qtiqmmfsrc plugin for camera stream
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  if (!qtiqmmfsrc) {
    g_printerr ("Failed to create qtiqmmfsrc\n");
    goto error_clean_elements;
  }

  // Use capsfilter to define the preview stream camera output settings
  qmmfsrc_caps_preview = gst_element_factory_make ("capsfilter",
      "qmmfsrc_caps_preview");
  if (!qmmfsrc_caps_preview) {
    g_printerr ("Failed to create qmmfsrc_caps_preview\n");
    goto error_clean_elements;
  }

  // Use capsfilter to define the monodepth stream camera output settings
  qmmfsrc_caps_monodepth = gst_element_factory_make ("capsfilter",
      "qmmfsrc_caps_monodepth");
  if (!qmmfsrc_caps_monodepth) {
    g_printerr ("Failed to create qmmfsrc_caps_monodepth\n");
    goto error_clean_elements;
  }

  // Create queue to decouple the processing on sink and source pad.
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create qtimlvconverter for Input preprocessing
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter",
      "qtimlvconverter");
  if (!qtimlvconverter) {
    g_printerr ("Failed to create qtimlvconverter\n");
    goto error_clean_elements;
  }

  // Create the ML inferencing plugin SNPE/TFLITE
  if (model_type == GST_MODEL_TYPE_SNPE) {
    qtimlelement = gst_element_factory_make ("qtimlsnpe", "qtimlsnpe");
  } else {
    qtimlelement = gst_element_factory_make ("qtimltflite", "qtimltflite");
  }
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    goto error_clean_elements;
  }

  // Create plugin for ML postprocessing for Segmentation
  qtimlvsegmentation = gst_element_factory_make ("qtimlvsegmentation",
      "qtimlvsegmentation");
  if (!qtimlvsegmentation) {
    g_printerr ("Failed to create qtimlvSegmentation\n");
    goto error_clean_elements;
  }

  // Used to negotiate between ML post proc o/p and qtivtransform
  segmentation_filter = gst_element_factory_make ("capsfilter",
      "segmentation_filter");
  if (!segmentation_filter) {
    g_printerr ("Failed to create Segmentation_filter\n");
    goto error_clean_elements;
  }

  // Create qtivtransform to convert UBWC Buffers to Non-UBWC buffers
  qtivtransform = gst_element_factory_make ("qtivtransform",
      "qtivtransform");
  if (!qtivtransform) {
    g_printerr ("Failed to create qtivtransform\n");
    goto error_clean_elements;
  }

  // Used to negotiate between qtivtransform and fpsdisplaysink
  transform_filter = gst_element_factory_make ("capsfilter",
      "transform_filter");
  if (!transform_filter) {
    g_printerr ("Failed to create transform_filter\n");
    goto error_clean_elements;
  }

  // Create Wayland compositor to render preview output on Display
  waylandsink_preview = gst_element_factory_make ("waylandsink",
      "waylandsink_preview");
  if (!waylandsink_preview) {
    g_printerr ("Failed to create waylandsink_preview\n");
    goto error_clean_elements;
  }

  // Create Wayland compositor to render monodepth output on Display
  waylandsink_monodepth = gst_element_factory_make ("waylandsink",
      "waylandsink_monodepth");
  if (!waylandsink_monodepth) {
    g_printerr ("Failed to create waylandsink_monodepth \n");
    goto error_clean_elements;
  }

  // Create fpsdisplaysink to display the current and
  // average framerate as a text overlay
  fpsdisplaysink = gst_element_factory_make ("fpsdisplaysink",
      "fpsdisplaysink");
  if (!fpsdisplaysink ) {
    g_printerr ("Failed to create fpsdisplaysink\n");
    goto error_clean_elements;
  }

  // 2. Set properties for all GST plugin elements
  // 2.1 Set the capabilities of camera preview stream camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, preview_width,
      "height", G_TYPE_INT, preview_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
  g_object_set (G_OBJECT (qmmfsrc_caps_preview), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.2 Set the capabilities of Monodepth stream camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, monodepth_width,
      "height", G_TYPE_INT, monodepth_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (qmmfsrc_caps_monodepth), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  //2.3 Set the Channel Mean and Sigma Value for qtimlvconverter Plugin
  g_value_init (&mean, GST_TYPE_ARRAY);
  g_value_init (&sigma, GST_TYPE_ARRAY);
  mean_vals[0] = MEAN_R; mean_vals[1] = MEAN_G; mean_vals[2] = MEAN_B;
  sigma_vals[0] = SIGMA_R; sigma_vals[1] = SIGMA_G; sigma_vals[2] = SIGMA_B;
  build_pad_property (&mean, mean_vals, 3);
  build_pad_property (&sigma, sigma_vals, 3);
  g_object_set_property (G_OBJECT (qtimlvconverter), "mean", &mean);
  g_object_set_property (G_OBJECT (qtimlvconverter), "sigma", &sigma);

  // 2.4 Select the HW to DSP for SNPE model and GPU for TFLITE Model
  // inferencing using delegate property
  if (model_type == GST_MODEL_TYPE_SNPE) {
    g_object_set (G_OBJECT (qtimlelement), "model", model_path,
        "delegate", GST_ML_SNPE_DELEGATE_DSP, NULL);
  } else {
    delegate_options = gst_structure_from_string (
        "QNNExternalDelegate,backend_type=htp;", NULL);
    g_object_set (G_OBJECT (qtimlelement), "model", model_path,
        "delegate", GST_ML_TFLITE_DELEGATE_GPU, NULL);
    g_object_set (G_OBJECT (qtimlelement),
        "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
    g_object_set (G_OBJECT (qtimlelement),
        "external-delegate-options", delegate_options, NULL);
    gst_structure_free (delegate_options);
  }

  // 2.5 Set properties for ML postproc plugins- module, labels
  module_id = get_enum_value (qtimlvsegmentation, "module", "midas-v2");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvsegmentation),
        "module", module_id, "labels", labels_path, NULL);
  } else {
    g_printerr ("Module midas-v2 is not available in qtimlvsegmentation\n");
    goto error_clean_elements;
  }

  // 2.6 Set the properties of Wayland compositor
  for (gint i = 0; i < STREAM_COUNT; i++) {
    if (i==0) {
      g_object_set (G_OBJECT (waylandsink_monodepth), "sync", false, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "x", position[i].x, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "y", position[i].y, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "width",
          position[i].w, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "height",
          position[i].h, NULL);
    } else {
      g_object_set (G_OBJECT (waylandsink_preview), "sync", false, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "x", position[i].x, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "y", position[i].y, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "width",
          position[i].w, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "height",
          position[i].h, NULL);
    }
  }

  // 2.7 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  g_object_set (G_OBJECT (fpsdisplaysink), "sync", false, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", true,
      NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", true, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink",
      waylandsink_monodepth, NULL);

  // Set the caps filter for segmentation_filter
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA", NULL);
  g_object_set (G_OBJECT (segmentation_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // Set caps filter for transform_filter
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
       NULL);
  g_object_set (G_OBJECT (transform_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_caps_preview,
      qmmfsrc_caps_monodepth, qtimlvconverter, qtimlelement, qtimlvsegmentation,
      segmentation_filter, qtivtransform, transform_filter,
      waylandsink_preview, fpsdisplaysink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create Pipeline for Monodepth
  // Linking Camera Preview Stream
  ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps_preview,
      waylandsink_preview, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for preview Stream, from"
        "qmmfsource->waylandsink\n");
    goto error_clean_pipeline;
  }

  // Linking Monodepth AI Processing stream
  ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps_monodepth,
       qtimlvconverter, queue[0], qtimlelement, qtimlvsegmentation,
       segmentation_filter, qtivtransform, transform_filter,
       queue[1], fpsdisplaysink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for monodepth stream, from"
        "qmmfsource->fpsdisplaysink\n");
    goto error_clean_pipeline;
  }

  // Setting Up qtiqmmfsrc streamtype property
  qtiqmmfsrc_type = gst_element_get_static_pad (qtiqmmfsrc, "video_0");
  if (!qtiqmmfsrc_type) {
    g_printerr ("video_0 of qtiqmmfsrc couldn't be retrieved\n");
    goto error_clean_pipeline;
  }

  g_value_init (&video_type, G_TYPE_INT);
  g_value_set_int (&video_type, GST_SOURCE_STREAM_TYPE_PREVIEW);
  g_object_set_property (G_OBJECT (qtiqmmfsrc_type), "type", &video_type);
  g_value_unset (&video_type);
  gst_object_unref (qtiqmmfsrc_type);

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  cleanup_many_gst (&qtiqmmfsrc, &qmmfsrc_caps_preview,
      &qmmfsrc_caps_monodepth, &qtimlvconverter, &qtimlelement,
      &qtimlvsegmentation, &segmentation_filter, &qtivtransform,
      &transform_filter, &fpsdisplaysink, &waylandsink_preview,
      &waylandsink_monodepth, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }

  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GstBus *bus = NULL;
  GMainLoop *mloop = NULL;
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *model_path = NULL;
  const gchar *labels_path = DEFAULT_MONODEPTH_LABELS;
  const gchar *app_name = NULL;
  GstAppContext appctx = {};
  GstModelType model_type = GST_MODEL_TYPE_TFLITE;
  gboolean ret = FALSE;
  gchar help_description[1024];
  guint intrpt_watch_id = 0;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "ml-framework", 'f', 0, G_OPTION_ARG_INT,
      &model_type,
      "Execute Model in SNPE DLC (1) or TFlite (2) format",
      "1 or 2"
    },
    { "model", 'm', 0, G_OPTION_ARG_STRING,
      &model_path,
      "This is an optional parameter and overrides default path\n"
      "      Default model path for SNPE DLC: "
      DEFAULT_SNPE_MONODEPTH_MODEL "\n"
      "      Default model path for TFLITE Model: "
      DEFAULT_TFLITE_MONODEPTH_MODEL,
      "/PATH"
    },
    { "labels", 'l', 0, G_OPTION_ARG_STRING,
      &labels_path,
      "This is an optional parameter and overrides default path\n"
      "      Default labels path: " DEFAULT_MONODEPTH_LABELS,
      "/PATH"
    },
    { NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  snprintf (help_description, 1023, "\nExample:\n"
      "  %s --ml-framework=1\n"
      "  %s -f 2\n"
      "  %s -f 1 --model=%s --labels=%s\n"
      "\nThis Sample App demonstrates Monodepth on Live Stream",
      app_name, app_name, app_name, DEFAULT_SNPE_MONODEPTH_MODEL,
      DEFAULT_MONODEPTH_LABELS);
  help_description[1023] = '\0';

  // Parse command line entries.
  if ((ctx = g_option_context_new (help_description)) != NULL) {
    GError *error = NULL;
    gboolean success = FALSE;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    return -EFAULT;
  }

  if (model_type < GST_MODEL_TYPE_SNPE ||
      model_type > GST_MODEL_TYPE_TFLITE) {
    g_printerr ("Invalid ml-framework option selected\n"
        "Available options:\n"
        "    SNPE: %d\n"
        "    TFLite: %d\n",
        GST_MODEL_TYPE_SNPE, GST_MODEL_TYPE_TFLITE);
    return -EINVAL;
  }

  // Set model path for execution
  model_path = model_path ? model_path : (model_type == GST_MODEL_TYPE_SNPE ?
      DEFAULT_SNPE_MONODEPTH_MODEL : DEFAULT_TFLITE_MONODEPTH_MODEL);

  if (!file_exists (model_path)) {
    g_print ("Invalid model file path: %s\n", model_path);
    return -EINVAL;
  }

  if (!file_exists (labels_path)) {
    g_print ("Invalid labels file path: %s\n", labels_path);
    return -EINVAL;
  }

  g_print ("Running app with model: %s and labels: %s\n",
      model_path, labels_path);

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx, model_type, model_path, labels_path);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    gst_object_unref (appctx.pipeline);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_object_unref (appctx.pipeline);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    gst_object_unref (appctx.pipeline);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);

  // Register respective callback function based on message
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);

  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), mloop);

  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

  // On successful transition to PAUSED state, state_changed_cb is called.
  // state_changed_cb callback is used to send pipeline to play state.
  g_print ("Set pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      goto error;
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

  // Wait till pipeline encounters an error or EOS
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

error:
  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  g_print ("Set pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destroy pipeline\n");
  gst_object_unref (appctx.pipeline);


  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
