/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Monodepth on video stream.
 *
 * Description:
 * The application takes video stream from camera/file/rtsp and gives
 * same to 2 parallel processing stream. One Stream to display scaled down
 * preview with Midasv2 TensorFlow Lite or SNPE DLC overlayed AI Model output.
 * And other stream to display camera/file/rtsp feed.
 *
 * Pipeline for Gstreamer Monodepth (2 Stream) using camera source below:-
 *
 *                    | -> qmmfsrc_caps -> waylandsink (Display)
 *                    |
 *                    |
 * source (camera) -> |
 *                    | -> qmmfsrc_caps -> pre process
 *                    |    -> ML Framework -> postprocess ->
 *                    |    -> qtivtransform ->
 *                    |    -> fpsdisplaysink (Display)
 *
 * Pipeline for Gstreamer Monodepth (2 Stream) using file source below:-
 *
 *                  | -> qtdemux -> h264parse -> v4l2h264dec ->
 *                  |    -> waylandsink (Display)
 *                  |
 *                  |
 * source (file) -> |
 *                  | -> qtdemux -> h264parse -> v4l2h264dec ->
 *                  |    -> ML Framework -> postprocess ->
 *                  |    -> qtivtransform ->
 *                  |    -> fpsdisplaysink (Display)
 *
 * * Pipeline for Gstreamer Monodepth (2 Stream) using RTSP source below:-
 *
 *
 *                  | -> rtph264depay -> h264parse -> v4l2h264dec ->
 *                  |    -> waylandsink (Display)
 *                  |
 *                  |
 * source (RTSP) -> |
 *                  | -> rtph264depay -> h264parse -> v4l2h264dec ->
 *                  |    -> ML Framework -> postprocess ->
 *                  |    -> qtivtransform ->
 *                  |    -> fpsdisplaysink (Display)
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

#include <gst/sampleapps/gst_sample_apps_utils.h>

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
#define PRIMARY_CAMERA_PREVIEW_OUTPUT_WIDTH 1920
#define PRIMARY_CAMERA_PREVIEW_OUTPUT_HEIGHT 1080
#define SECONDARY_CAMERA_PREVIEW_OUTPUT_WIDTH 1280
#define SECONDARY_CAMERA_PREVIEW_OUTPUT_HEIGHT 720
#define MONODEPTH_OUTPUT_WIDTH 640
#define MONODEPTH_OUTPUT_HEIGHT 360
#define DEFAULT_CAMERA_FRAME_RATE 30
#define DEFAULT_RTSP_FILE_TFLITE_FRAME_RATE 24

/**
 * Default wayland display width and height
 */
#define DEFAULT_DISPLAY_HEIGHT 1080
#define DEFAULT_DISPLAY_WIDTH 1920

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
#define QUEUE_COUNT 7

/**
 * Number of Stream in Pipeline
 */
#define STREAM_COUNT 2

/**
 * Default value of delegate
 */
#define DEFAULT_SNPE_DELEGATE GST_ML_SNPE_DELEGATE_DSP

/**
 * Structure for various application specific options
 */
typedef struct {
  const gchar *file_path;
  const gchar *rtsp_ip_port;
  const gchar *model_path;
  const gchar *labels_path;
  GstCameraSourceType camera_type;
  GstModelType model_type;
  gint delegate_type;
  gboolean use_cpu;
  gboolean use_gpu;
  gboolean use_dsp;
  gboolean use_file;
  gboolean use_rtsp;
  gboolean use_camera;
} GstAppOptions;

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
update_window_grid (GstVideoRectangle position[2])
{
  gint width, height;
  gint win_w, win_h;

  if (get_active_display_mode (&width, &height)) {
    g_print ("Display width = %d height = %d\n", width, height);
  } else {
    g_warning ("Failed to get active display mode, using 1080p default config");
    width = DEFAULT_DISPLAY_WIDTH;
    height = DEFAULT_DISPLAY_HEIGHT;
  }
  win_w = width / 2;
  win_h = height / 2;

  GstVideoRectangle window_grid[2] = {
    {win_w, 0, win_w, 2 * win_h},
    {0, 0, win_w, 2 * win_h}
  };

  for (gint idx = 0; idx < 2; idx++) {
    position[idx] = window_grid[idx];
  }
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstAppContext * appctx, GstAppOptions * options)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->file_path != NULL) {
    g_free (options->file_path);
  }

  if (options->rtsp_ip_port != NULL) {
    g_free (options->rtsp_ip_port);
  }

  if (options->model_path != DEFAULT_SNPE_MONODEPTH_MODEL &&
      options->model_path != DEFAULT_TFLITE_MONODEPTH_MODEL &&
      options->model_path != NULL) {
    g_free (options->model_path);
  }

  if (options->labels_path != DEFAULT_MONODEPTH_LABELS &&
      options->labels_path != NULL) {
    g_free (options->labels_path);
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }
}

/**
 * Function to link the dynamic video pad of demux to queue:
 *
 * @param element GStreamer source element
 * @param pad GStreamer source element pad
 * @param data sink element object
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;
  GstPadLinkReturn ret;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");

  // Link the source pad to the sink pad
  ret = gst_pad_link (pad, sinkpad);
  if (!ret)
  {
    g_printerr ("Failed to link pad to sinkpad\n");
  }

  gst_object_unref (sinkpad);
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 * @param options Application specific options.
 */
static gboolean
create_pipe (GstAppContext * appctx, GstAppOptions * options)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps_preview = NULL;
  GstElement *qmmfsrc_caps_monodepth = NULL, *queue[QUEUE_COUNT];
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvsegmentation = NULL, *segmentation_filter = NULL;
  GstElement *qtivtransform = NULL, *transform_filter = NULL;
  GstElement *filesrc[STREAM_COUNT], *qtdemux[STREAM_COUNT];
  GstElement *h264parse[STREAM_COUNT], *v4l2h264dec[STREAM_COUNT];
  GstElement *rtspsrc[STREAM_COUNT], *rtph264depay[STREAM_COUNT];
  GstElement *videorate[STREAM_COUNT], *videorate_caps[STREAM_COUNT];
  GstElement *fpsdisplaysink = NULL, *waylandsink_preview = NULL;
  GstElement *waylandsink_monodepth = NULL;
  GstCaps *pad_filter = NULL , *filtercaps = NULL, *videorate_filter = NULL;
  GstPad *qtiqmmfsrc_type = NULL;
  GstStructure *delegate_options = NULL;
  GstVideoRectangle position[STREAM_COUNT];
  gboolean ret = FALSE;
  gchar element_name[128];
  gint primary_camera_preview_width = PRIMARY_CAMERA_PREVIEW_OUTPUT_WIDTH;
  gint primary_camera_preview_height = PRIMARY_CAMERA_PREVIEW_OUTPUT_HEIGHT;
  gint secondary_camera_preview_width = SECONDARY_CAMERA_PREVIEW_OUTPUT_WIDTH;
  gint secondary_camera_preview_height = SECONDARY_CAMERA_PREVIEW_OUTPUT_HEIGHT;
  gint monodepth_width = MONODEPTH_OUTPUT_WIDTH;
  gint monodepth_height = MONODEPTH_OUTPUT_HEIGHT;
  gint camera_framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint file_rtsp_tflite_framerate = DEFAULT_RTSP_FILE_TFLITE_FRAME_RATE;
  gint module_id;
  GValue mean = G_VALUE_INIT, sigma = G_VALUE_INIT;
  GValue video_type = G_VALUE_INIT;
  gdouble mean_vals[3], sigma_vals[3];

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }

  for (gint i = 0; i < STREAM_COUNT; i++) {
    filesrc[i] = NULL;
    rtspsrc[i] = NULL;
    qtdemux[i] = NULL;
    h264parse[i] = NULL;
    v4l2h264dec[i] = NULL;
    rtph264depay[i] = NULL;
    videorate[i] = NULL;
    videorate_caps[i] = NULL;
  }

  update_window_grid (position);

  // 1. Create the elements or Plugins
  if (options->use_file) {
    // Create file source element for file stream
    for (gint i = 0; i < STREAM_COUNT; i++) {
      snprintf (element_name, 127, "filesrc-%d", i);
      filesrc[i] = gst_element_factory_make ("filesrc", element_name);
      if (!filesrc[i]) {
        g_printerr ("Failed to create filesrc\n");
        goto error_clean_elements;
      }

      // Create qtdemux for demuxing the filesrc
      snprintf (element_name, 127, "qtdemux-%d", i);
      qtdemux[i] = gst_element_factory_make ("qtdemux", element_name);
      if (!qtdemux[i]) {
        g_printerr ("Failed to create qtdemux\n");
        goto error_clean_elements;
      }

      // Create h264parse element for parsing the stream
      snprintf (element_name, 127, "h264parse-%d", i);
      h264parse[i] = gst_element_factory_make ("h264parse", element_name);
      if (!h264parse[i]) {
        g_printerr ("Failed to create h264parse\n");
        goto error_clean_elements;
      }

      // Create v4l2h264dec element for encoding the stream
      snprintf (element_name, 127, "v4l2h264dec-%d", i);
      v4l2h264dec[i] = gst_element_factory_make ("v4l2h264dec", element_name);
      if (!v4l2h264dec[i]) {
        g_printerr ("Failed to create v4l2h264dec\n");
        goto error_clean_elements;
      }

      if (options->model_type == GST_MODEL_TYPE_TFLITE) {
        // create videorate for modification of video speed by a certain factor
        snprintf (element_name, 127, "videorate-%d", i);
        videorate[i] = gst_element_factory_make ("videorate", element_name);
        if (!videorate[i]) {
          g_printerr ("Failed to create videorate\n");
          goto error_clean_elements;
        }

        // Used to set framerate for videorate plugin
        snprintf (element_name, 127, "videorate_caps-%d", i);
        videorate_caps[i] = gst_element_factory_make ("capsfilter", element_name);
        if (!videorate_caps[i]) {
          g_printerr ("Failed to create videorate_caps\n");
          goto error_clean_elements;
        }
      }
    }
  } else if (options->use_rtsp) {
    // Create rtspsrc plugin for rtsp input
    for (gint i = 0; i < STREAM_COUNT; i++) {
      snprintf (element_name, 127, "rtspsrc-%d", i);
      rtspsrc[i] = gst_element_factory_make ("rtspsrc", element_name);
      if (!rtspsrc[i]) {
        g_printerr ("Failed to create rtspsrc\n");
        goto error_clean_elements;
      }

      // Create rtph264depay plugin for rtsp payload parsing
      snprintf (element_name, 127, "rtph264depay-%d", i);
      rtph264depay[i] = gst_element_factory_make ("rtph264depay", element_name);
      if (!rtph264depay[i]) {
        g_printerr ("Failed to create rtph264depay\n");
        goto error_clean_elements;
      }

      // Create h264parse element for parsing the stream
      snprintf (element_name, 127, "h264parse-%d", i);
      h264parse[i] = gst_element_factory_make ("h264parse", element_name);
      if (!h264parse[i]) {
        g_printerr ("Failed to create h264parse\n");
        goto error_clean_elements;
      }

      // Create v4l2h264dec element for encoding the stream
      snprintf (element_name, 127, "v4l2h264dec-%d", i);
      v4l2h264dec[i] = gst_element_factory_make ("v4l2h264dec", element_name);
      if (!v4l2h264dec[i]) {
        g_printerr ("Failed to create v4l2h264dec\n");
        goto error_clean_elements;
      }

      if (options->model_type == GST_MODEL_TYPE_TFLITE) {
        // create videorate for modification of video speed by a certain factor
        snprintf (element_name, 127, "videorate-%d", i);
        videorate[i] = gst_element_factory_make ("videorate", element_name);
        if (!videorate[i]) {
          g_printerr ("Failed to create videorate\n");
          goto error_clean_elements;
        }

        // Used to set framerate for videorate plugin
        snprintf (element_name, 127, "videorate_caps-%d", i);
        videorate_caps[i] = gst_element_factory_make ("capsfilter", element_name);
        if (!videorate_caps[i]) {
          g_printerr ("Failed to create videorate_caps\n");
          goto error_clean_elements;
        }
      }
    }
  } else if (options->use_camera) {
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
  } else {
    g_printerr ("Invalid Source Type 1\n");
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
  if (options->model_type == GST_MODEL_TYPE_SNPE) {
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
  if (options->use_file && options->model_type == GST_MODEL_TYPE_SNPE) {
    // 2.1 Set the capabilities of file stream
    for (gint i = 0; i < STREAM_COUNT; i++) {
      g_object_set (G_OBJECT (v4l2h264dec[i]), "capture-io-mode", 5, NULL);
      g_object_set (G_OBJECT (v4l2h264dec[i]), "output-io-mode", 5, NULL);
      g_object_set (G_OBJECT (filesrc[i]), "location", options->file_path, NULL);
    }
  } else if (options->use_file && options->model_type == GST_MODEL_TYPE_TFLITE) {
    // 2.1 Set the capabilities of file stream
    for (gint i = 0; i < STREAM_COUNT; i++) {
      g_object_set (G_OBJECT (v4l2h264dec[i]), "capture-io-mode", 5, NULL);
      g_object_set (G_OBJECT (v4l2h264dec[i]), "output-io-mode", 5, NULL);
      g_object_set (G_OBJECT (filesrc[i]), "location", options->file_path, NULL);
      videorate_filter = gst_caps_new_simple ("video/x-raw",
          "framerate", GST_TYPE_FRACTION, file_rtsp_tflite_framerate, 1, NULL);
      g_object_set (G_OBJECT (videorate_caps[i]), "caps", videorate_filter, NULL);
      gst_caps_unref (videorate_filter);
    }
  } else if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_SNPE) {
    // 2.2 Set the capabilities of RTSP stream
    for (gint i = 0; i < STREAM_COUNT; i++) {
      g_object_set (G_OBJECT (v4l2h264dec[i]), "capture-io-mode", 5, NULL);
      g_object_set (G_OBJECT (v4l2h264dec[i]), "output-io-mode", 5, NULL);
      g_object_set (G_OBJECT (rtspsrc[i]), "location",
          options->rtsp_ip_port, NULL);
    }
  } else if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_TFLITE) {
    // 2.2 Set the capabilities of RTSP stream
    for (gint i = 0; i < STREAM_COUNT; i++) {
      g_object_set (G_OBJECT (v4l2h264dec[i]), "capture-io-mode", 5, NULL);
      g_object_set (G_OBJECT (v4l2h264dec[i]), "output-io-mode", 5, NULL);
      g_object_set (G_OBJECT (rtspsrc[i]), "location",
          options->rtsp_ip_port, NULL);
      videorate_filter = gst_caps_new_simple ("video/x-raw",
          "framerate", GST_TYPE_FRACTION, file_rtsp_tflite_framerate, 1, NULL);
      g_object_set (G_OBJECT (videorate_caps[i]), "caps", videorate_filter, NULL);
      gst_caps_unref (videorate_filter);
    }
  } else if (options->use_camera) {
    // 2.3 Set user provided Camera ID
    g_object_set (G_OBJECT (qtiqmmfsrc), "camera", options->camera_type, NULL);
    // 2.4 Set the capabilities of camera plugin output
    if (options->camera_type == GST_CAMERA_TYPE_PRIMARY) {
      // 2.4 Set the capabilities of primary and secondary camera preview
      // stream camera plugin output
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, primary_camera_preview_width,
          "height", G_TYPE_INT, primary_camera_preview_height,
          "framerate", GST_TYPE_FRACTION, camera_framerate, 1,
          "compression", G_TYPE_STRING, "ubwc", NULL);
      g_object_set (G_OBJECT (qmmfsrc_caps_preview), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    } else {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, secondary_camera_preview_width,
          "height", G_TYPE_INT, secondary_camera_preview_height,
          "framerate", GST_TYPE_FRACTION, camera_framerate, 1,
          "compression", G_TYPE_STRING, "ubwc", NULL);
      g_object_set (G_OBJECT (qmmfsrc_caps_preview), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    }
    // 2.5 Set the capabilities of Monodepth stream camera plugin output
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, monodepth_width,
        "height", G_TYPE_INT, monodepth_height,
        "framerate", GST_TYPE_FRACTION, camera_framerate, 1,
        "compression", G_TYPE_STRING, "ubwc", NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (qmmfsrc_caps_monodepth), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else {
    g_printerr ("Invalid Source Type\n");
    goto error_clean_elements;
  }

  //2.6 Set the Channel Mean and Sigma Value for qtimlvconverter Plugin
  g_value_init (&mean, GST_TYPE_ARRAY);
  g_value_init (&sigma, GST_TYPE_ARRAY);
  mean_vals[0] = MEAN_R; mean_vals[1] = MEAN_G; mean_vals[2] = MEAN_B;
  sigma_vals[0] = SIGMA_R; sigma_vals[1] = SIGMA_G; sigma_vals[2] = SIGMA_B;
  build_pad_property (&mean, mean_vals, 3);
  build_pad_property (&sigma, sigma_vals, 3);
  g_object_set_property (G_OBJECT (qtimlvconverter), "mean", &mean);
  g_object_set_property (G_OBJECT (qtimlvconverter), "sigma", &sigma);

  // 2.7 Select the HW to DSP/GPU/CPU for model inferencing using
  // delegate property
  if (options->model_type == GST_MODEL_TYPE_SNPE) {
    GstMLSnpeDelegate snpe_delegate;
    if (options->use_cpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_NONE;
      g_print ("Using CPU Delegate");
    } else if (options->use_gpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_GPU;
      g_print ("Using GPU Delegate");
    } else {
      snpe_delegate = GST_ML_SNPE_DELEGATE_DSP;
      g_print ("Using DSP Delegate");
    }
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "delegate", snpe_delegate, NULL);
  } else if (options->model_type == GST_MODEL_TYPE_TFLITE) {
    GstMLTFLiteDelegate tflite_delegate;
    if (options->use_cpu) {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_NONE;
      g_print ("Using CPU Delegate");
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", tflite_delegate, NULL);
    } else if (options->use_dsp) {
      g_print ("Using DSP Delegate");
      delegate_options = gst_structure_from_string (
          "QNNExternalDelegate,backend_type=htp;", NULL);
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
      g_object_set (G_OBJECT (qtimlelement),
          "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
      g_object_set (G_OBJECT (qtimlelement),
          "external-delegate-options", delegate_options, NULL);
      gst_structure_free (delegate_options);
    } else {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_GPU;
      g_print ("Using GPU Delegate");
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", tflite_delegate, NULL);
    }
  } else {
    g_printerr ("Invalid Model Type\n");
    goto error_clean_elements;
  }

  // 2.8 Set properties for ML postproc plugins- module, labels
  module_id = get_enum_value (qtimlvsegmentation, "module", "midas-v2");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvsegmentation),
        "module", module_id, "labels", options->labels_path, NULL);
  } else {
    g_printerr ("Module midas-v2 is not available in qtimlvsegmentation\n");
    goto error_clean_elements;
  }

  // 2.9 Set the properties of Wayland compositor
  for (gint i = 0; i < STREAM_COUNT; i++) {
    if (i==0) {
      if (options->use_camera) {
        g_object_set (G_OBJECT (waylandsink_monodepth), "sync", FALSE, NULL);
      } else {
        g_object_set (G_OBJECT (waylandsink_monodepth), "sync", TRUE, NULL);
      }
      g_object_set (G_OBJECT (waylandsink_monodepth), "x", position[i].x, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "y", position[i].y, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "width",
          position[i].w, NULL);
      g_object_set (G_OBJECT (waylandsink_monodepth), "height",
          position[i].h, NULL);
    } else {
      if (options->use_camera) {
        g_object_set (G_OBJECT (waylandsink_preview), "sync", FALSE, NULL);
      } else {
        g_object_set (G_OBJECT (waylandsink_preview), "sync", TRUE, NULL);
      }
      g_object_set (G_OBJECT (waylandsink_preview), "x", position[i].x, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "y", position[i].y, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "width",
          position[i].w, NULL);
      g_object_set (G_OBJECT (waylandsink_preview), "height",
          position[i].h, NULL);
    }
  }

  // 2.10 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  if (options->use_camera) {
    g_object_set (G_OBJECT (fpsdisplaysink), "sync", FALSE, NULL);
  } else {
    g_object_set (G_OBJECT (fpsdisplaysink), "sync", TRUE, NULL);
  }
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", TRUE,
      NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink",
      waylandsink_monodepth, NULL);

  // 2.11 Set the caps filter for segmentation_filter
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA", NULL);
  g_object_set (G_OBJECT (segmentation_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 2.12 Set caps filter for transform_filter
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
       NULL);
  g_object_set (G_OBJECT (transform_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options->use_file && options->model_type == GST_MODEL_TYPE_SNPE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc[0], filesrc[1],
        qtdemux[0], qtdemux[1], h264parse[0], h264parse[1], v4l2h264dec[0],
        v4l2h264dec[1], NULL);
  } else if (options->use_file && options->model_type == GST_MODEL_TYPE_TFLITE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc[0], filesrc[1],
        qtdemux[0], qtdemux[1], h264parse[0], h264parse[1], v4l2h264dec[0],
        v4l2h264dec[1], videorate[0], videorate[1], videorate_caps[0],
        videorate_caps[1], NULL);
  } else if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_SNPE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc[0], rtspsrc[1],
        rtph264depay[0], rtph264depay[1], h264parse[0], h264parse[1],
        v4l2h264dec[0], v4l2h264dec[1], NULL);
  } else if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_TFLITE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc[0], rtspsrc[1],
        rtph264depay[0], rtph264depay[1], h264parse[0], h264parse[1],
        v4l2h264dec[0], v4l2h264dec[1], videorate[0], videorate[1],
        videorate_caps[0], videorate_caps[1], NULL);
  } else if (options->use_camera) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
        qmmfsrc_caps_preview, qmmfsrc_caps_monodepth, NULL);
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter,
      qtimlelement, qtimlvsegmentation, segmentation_filter, qtivtransform,
      transform_filter, waylandsink_preview, fpsdisplaysink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create Pipeline for Monodepth
  if (options->use_file && options->model_type == GST_MODEL_TYPE_SNPE) {
    // Linking File source preview Stream
    ret = gst_element_link_many (filesrc[0], qtdemux[0], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for filesource->qtdemux\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[0], h264parse[0], v4l2h264dec[0],
        waylandsink_preview, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
      "parse->waylandsink_preview\n");
      goto error_clean_pipeline;
    }

    // Linking File AI Processing Stream
    ret = gst_element_link_many (filesrc[1], qtdemux[1], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for filesource->qtdemux\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[1], h264parse[1], v4l2h264dec[1],
        qtimlvconverter, queue[2], qtimlelement, qtimlvsegmentation,
        segmentation_filter, qtivtransform, transform_filter,
        queue[3], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
      "parse->fpsdisplaysink\n");
      goto error_clean_pipeline;
    }

  } else if (options->use_file && options->model_type == GST_MODEL_TYPE_TFLITE) {
    // Linking File source preview Stream
    ret = gst_element_link_many (filesrc[0], qtdemux[0], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for filesource->qtdemux\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[0], h264parse[0], v4l2h264dec[0], queue[1],
        videorate[0], videorate_caps[0], waylandsink_preview, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
      "parse->waylandsink_preview\n");
      goto error_clean_pipeline;
    }

    // Linking File AI Processing Stream
    ret = gst_element_link_many (filesrc[1], qtdemux[1], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for filesource->qtdemux\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[2], h264parse[1], v4l2h264dec[1], queue[3],
        videorate[1], videorate_caps[1], qtimlvconverter, queue[4],
        qtimlelement, queue[5], qtimlvsegmentation, segmentation_filter,
        qtivtransform, transform_filter, queue[6], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
      "parse->fpsdisplaysink\n");
      goto error_clean_pipeline;
    }

  } else if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_SNPE) {
    // Linking RTSP source preview Stream
    ret = gst_element_link_many (queue[0], rtph264depay[0], h264parse[0],
        v4l2h264dec[0], waylandsink_preview, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->waylandsink_preview\n");
      goto error_clean_pipeline;
    }

    // Linking RTSP source AI Processing stream
    ret = gst_element_link_many (queue[1], rtph264depay[1], h264parse[1],
        v4l2h264dec[1], qtimlvconverter, queue[2], qtimlelement,
        qtimlvsegmentation, segmentation_filter, qtivtransform,
        transform_filter, queue[3], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->fpsdisplaysink\n");
      goto error_clean_pipeline;
    }

  } else if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_TFLITE) {
    // Linking RTSP source preview Stream
    ret = gst_element_link_many (queue[0], rtph264depay[0], h264parse[0],
        v4l2h264dec[0], queue[1], videorate[0], videorate_caps[0],
        waylandsink_preview, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->waylandsink_preview\n");
      goto error_clean_pipeline;
    }

    // Linking RTSP source AI Processing stream
    ret = gst_element_link_many (queue[2], rtph264depay[1], h264parse[1],
        v4l2h264dec[1], queue[3], videorate[1], videorate_caps[1],
        qtimlvconverter, queue[4], qtimlelement, queue[5], qtimlvsegmentation,
        segmentation_filter, qtivtransform, transform_filter,
        queue[6], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->fpsdisplaysink\n");
      goto error_clean_pipeline;
    }

  } else {
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
       qtimlvconverter, queue[0], qtimlelement, queue[1], qtimlvsegmentation,
       segmentation_filter, qtivtransform, transform_filter,
       queue[2], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for monodepth stream, from"
          "qmmfsource->fpsdisplaysink\n");
      goto error_clean_pipeline;
    }
  }

  if (options->use_camera) {
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
  }

  if (options->use_file && options->model_type == GST_MODEL_TYPE_SNPE) {
    g_signal_connect (qtdemux[0], "pad-added",
        G_CALLBACK (on_pad_added), queue[0]);
    g_signal_connect (qtdemux[1], "pad-added",
        G_CALLBACK (on_pad_added), queue[1]);
  }

  if (options->use_file && options->model_type == GST_MODEL_TYPE_TFLITE) {
    g_signal_connect (qtdemux[0], "pad-added",
        G_CALLBACK (on_pad_added), queue[0]);
    g_signal_connect (qtdemux[1], "pad-added",
        G_CALLBACK (on_pad_added), queue[2]);
  }

  if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_SNPE) {
    g_signal_connect (rtspsrc[0], "pad-added",
        G_CALLBACK (on_pad_added), queue[0]);
    g_signal_connect (rtspsrc[1], "pad-added",
        G_CALLBACK (on_pad_added), queue[1]);
  }

  if (options->use_rtsp && options->model_type == GST_MODEL_TYPE_TFLITE) {
    g_signal_connect (rtspsrc[0], "pad-added",
        G_CALLBACK (on_pad_added), queue[0]);
    g_signal_connect (rtspsrc[1], "pad-added",
        G_CALLBACK (on_pad_added), queue[2]);
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps_preview,
      &qmmfsrc_caps_monodepth, &qtdemux, &h264parse, &v4l2h264dec,
      &rtph264depay, &qtimlvconverter, &qtimlelement, &qtimlvsegmentation,
      &segmentation_filter, &qtivtransform, &transform_filter, &fpsdisplaysink,
      &waylandsink_preview, &waylandsink_monodepth, NULL);
  for (gint i = 0; i < STREAM_COUNT; i++) {
    cleanup_gst (&filesrc[i], &rtspsrc[i], &qtdemux[i], &h264parse[i],
        &v4l2h264dec[i], &rtph264depay[i], &videorate[i], &videorate_caps[i],
        NULL);
  }
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
  const gchar *app_name = NULL;
  GstAppContext appctx = {};
  gboolean ret = FALSE;
  gchar help_description[1024];
  guint intrpt_watch_id = 0;
  GstAppOptions options = {};

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Set default value
  options.model_path = NULL;
  options.file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.labels_path = DEFAULT_MONODEPTH_LABELS;
  options.use_cpu = FALSE, options.use_gpu = FALSE, options.use_dsp = FALSE;
  options.use_file = FALSE, options.use_rtsp = FALSE, options.use_camera = FALSE;
  options.model_type = GST_MODEL_TYPE_SNPE;
  options.camera_type = GST_CAMERA_TYPE_NONE;

  // Structure to define the user options selection
  GOptionEntry entries[] = {
#ifdef ENABLE_CAMERA
    { "camera", 'c', 0, G_OPTION_ARG_INT,
      &options.camera_type,
      "Select (0) for Primary Camera and (1) for secondary one.\n"
      "      invalid camera id will switch to primary camera",
      "0 or 1"
    },
#endif // ENABLE_CAMERA
    { "file-path", 's', 0, G_OPTION_ARG_STRING,
      &options.file_path,
      "File source path",
      "/PATH"
    },
    { "rtsp-ip-port", 0, 0, G_OPTION_ARG_STRING,
      &options.rtsp_ip_port,
      "Use this parameter to provide the rtsp input.\n"
      "      Input should be provided as rtsp://<ip>:<port>/<stream>,\n"
      "      eg: rtsp://192.168.1.110:8554/live.mkv",
      "rtsp://<ip>:<port>/<stream>"
    },
    { "ml-framework", 'f', 0, G_OPTION_ARG_INT,
      &options.model_type,
      "Execute Model in SNPE DLC (1) or TFlite (2) format",
      "1 or 2"
    },
    { "model", 'm', 0, G_OPTION_ARG_STRING,
      &options.model_path,
      "This is an optional parameter and overrides default path\n"
      "      Default model path for SNPE DLC: "
      DEFAULT_SNPE_MONODEPTH_MODEL "\n"
      "      Default model path for TFLITE Model: "
      DEFAULT_TFLITE_MONODEPTH_MODEL,
      "/PATH"
    },
    { "labels", 'l', 0, G_OPTION_ARG_STRING,
      &options.labels_path,
      "This is an optional parameter and overrides default path\n"
      "      Default labels path: " DEFAULT_MONODEPTH_LABELS,
      "/PATH"
    },
    { "use_cpu", 0, 0, G_OPTION_ARG_NONE,
      &options.use_cpu,
      "This is an optional parameter to inference on CPU Runtime",
      NULL
    },
    { "use_gpu", 0, 0, G_OPTION_ARG_NONE,
      &options.use_gpu,
      "This is an optional parameter to inference on GPU Runtime",
      NULL
    },
    { "use_dsp", 0, 0, G_OPTION_ARG_NONE,
      &options.use_dsp,
      "This is an default and optional parameter to inference on DSP Runtime",
      NULL
    },
    { NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  snprintf (help_description, 1023, "\nExample:\n"
#ifdef ENABLE_CAMERA
      "  %s --ml-framework=1\n"
      "  %s -f 1 --model=%s --labels=%s\n"
#endif // ENABLE_CAMERA
      "  %s -s <file_path> -f 2\n"
      "\nThis Sample App demonstrates Monodepth on Live Stream",
#ifdef ENABLE_CAMERA
      app_name, app_name, DEFAULT_SNPE_MONODEPTH_MODEL,
      DEFAULT_MONODEPTH_LABELS,
#endif // ENABLE_CAMERA
      app_name);
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
      gst_app_context_free (&appctx, &options);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (&appctx, &options);
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    gst_app_context_free (&appctx, &options);
    return -EFAULT;
  }

// Check for input source
#ifdef ENABLE_CAMERA
  g_print ("TARGET Can support file source, RTSP source and camera source\n");
#else
  g_print ("TARGET Can only support file source and RTSP source.\n");
  if (options.file_path == NULL && options.rtsp_ip_port == NULL) {
    g_print ("User need to give proper input file or RTSP as source\n");
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }
#endif // ENABLE_CAMERA

  if (options.file_path != NULL) {
    options.use_file = TRUE;
  }

  if (options.rtsp_ip_port != NULL) {
    options.use_rtsp = TRUE;
  }

  // Use camera by default if user does not set anything
  if (! (options.use_file || (options.camera_type != GST_CAMERA_TYPE_NONE) ||
      options.use_rtsp)) {
    options.use_camera = TRUE;
    options.camera_type = GST_CAMERA_TYPE_PRIMARY;
    g_print ("Using PRIMARY camera by default, Not valid camera id selected\n");
  }

  // Checking camera id passed by user.
  if (options.camera_type < GST_CAMERA_TYPE_NONE ||
      options.camera_type > GST_CAMERA_TYPE_SECONDARY) {
    g_printerr ("Invalid Camera ID selected\n"
        "Available options:\n"
        "    PRIMARY: %d\n"
        "    SECONDARY %d\n",
        GST_CAMERA_TYPE_PRIMARY,
        GST_CAMERA_TYPE_SECONDARY);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  // Enable camera flag if user set the camera property
  if (options.camera_type == GST_CAMERA_TYPE_SECONDARY ||
      options.camera_type == GST_CAMERA_TYPE_PRIMARY)
    options.use_camera = TRUE;

  // Terminate if more than one source are there.
  if (options.use_file + options.use_camera + options.use_rtsp > 1) {
     g_printerr ("Select anyone source type either Camera or File or RTSP\n");
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if (options.use_file) {
    g_print ("File Source is Selected\n");
  } else if (options.use_rtsp) {
    g_print ("RTSP Source is Selected\n");
  } else {
    g_print ("Camera Source is Selected\n");
  }

  if (options.model_type < GST_MODEL_TYPE_SNPE ||
      options.model_type > GST_MODEL_TYPE_TFLITE) {
    g_printerr ("Invalid ml-framework option selected\n"
        "Available options:\n"
        "    SNPE: %d\n"
        "    TFLite: %d\n",
        GST_MODEL_TYPE_SNPE, GST_MODEL_TYPE_TFLITE);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if ((options.use_cpu + options.use_gpu + options.use_dsp) > 1) {
    g_print ("Select any one runtime from CPU or GPU or DSP\n");
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  // Set model path for execution
  if (options.model_path == NULL) {
    if (options.model_type == GST_MODEL_TYPE_SNPE)
      options.model_path = DEFAULT_SNPE_MONODEPTH_MODEL;
    else
      options.model_path = DEFAULT_TFLITE_MONODEPTH_MODEL;
  }

  if (!file_exists (options.model_path)) {
    g_print ("Invalid model file path: %s\n", options.model_path);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if (!file_exists (options.labels_path)) {
    g_print ("Invalid labels file path: %s\n", options.labels_path);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if (options.file_path != NULL) {
    if (!file_exists (options.file_path)) {
      g_print ("Invalid file source path: %s\n", options.file_path);
      gst_app_context_free (&appctx, &options);
      return -EINVAL;
    }
  }

  g_print ("Running app with model: %s and labels: %s\n",
      options.model_path, options.labels_path);

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    gst_app_context_free (&appctx, &options);
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx, &options);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    gst_app_context_free (&appctx, &options);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (&appctx, &options);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (&appctx, &options);
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
  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  g_print ("Set pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destroy pipeline\n");
  gst_app_context_free (&appctx, &options);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
