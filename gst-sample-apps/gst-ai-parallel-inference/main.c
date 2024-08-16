/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Parallel Classification, Pose Detection, Object Detection and
 * Segmentation on 4-Streams.
 *
 * Description:
 * The application takes video stream from camera/file/rtsp and gives same
 * to 4 Ch parallel processing by AI models (Classification, Pose detection and
 * Object detection and Segmentation) and display scaled down preview with
 * overlayed AI models output composed as 2x2 matrix
 *
 * Pipeline for Gstreamer Parallel Inferencing (4 Stream) below:
 *
 * Buffer handling for different sources:
 * 1. For camera source:
 * qtiqmmfsrc (Camera) -> qmmfsrc_caps -> tee (SPLIT)
 *
 * 2. For File source:
 * filesrc -> qtdemux -> h264parse -> tee (SPLIT)
 *
 * 3. For RTSP source:
 * rtspsrc -> rtph264depay -> h264parse -> tee (SPLIT)
 *
 * Pipeline after tee is common for all
 * sources (qtiqmmfsrc/filesrc/rtspsrc)
 *
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     |  qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     |  qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post process: qtimlvdetection/qtimlclassification/
 *                   qtimlvsegmentation/qtimlvpose -> detection_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "include/gst_sample_apps_utils.h"

/**
 * Default models path and labels path
 */
#define DEFAULT_SNPE_OBJECT_DETECTION_MODEL "/opt/yolonas.dlc"
#define DEFAULT_OBJECT_DETECTION_LABELS "/opt/yolonas.labels"
#define DEFAULT_TFLITE_CLASSIFICATION_MODEL \
    "/opt/inception_v3_quantized.tflite"
#define DEFAULT_CLASSIFICATION_LABELS "/opt/classification.labels"
#define DEFAULT_TFLITE_POSE_DETECTION_MODEL \
    "/opt/hrnet_pose_quantized.tflite"
#define DEFAULT_POSE_DETECTION_LABELS "/opt/posenet_mobilenet_v1.labels"
#define DEFAULT_SNPE_SEGMENTATION_MODEL "/opt/deeplabv3_resnet50.dlc"
#define DEFAULT_SEGMENTATION_LABELS "/opt/deeplabv3_resnet50.labels"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input size
 */
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1920
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 1080
#define SECONDARY_CAMERA_OUTPUT_WIDTH 1280
#define SECONDARY_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Default wayland display width and height
 */
#define DEFAULT_DISPLAY_WIDTH 1920
#define DEFAULT_DISPLAY_HEIGHT 1080

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 25

/**
 * To enable softmax operation for post processing
 */
#define GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX 1

/**
 * PipelineData:
 * @model: Path to model file
 * @labels: Path to label file
 * @mlframework: ML inference plugin
 * @postproc: Post processing plugin
 * @delegate: ML Execution Core.
 * @position: Window Dimension and Co-ordinate.
 * @constants: Offset and scale values
 *
 * Pipeline Data context to use plugins and path of Model, Labels.
 */
typedef struct {
  const gchar * model;
  const gchar * labels;
  const gchar * preproc;
  const gchar * mlframework;
  const gchar * postproc;
  GstVideoRectangle position;
  gint delegate;
  const gchar * constants;
} GstPipelineData;

/**
 * Structure for various application specific options
 */
typedef struct {
  gchar *file_path;
  gchar *rtsp_ip_port;
  GstCameraSourceType camera_type;
  gboolean use_file;
  gboolean use_rtsp;
  gboolean use_camera;
} GstAppOptions;

/**
 * Default properties of pipeline for Object detection,
 * Classification, Posture detection and Segmentation
 */
static GstPipelineData pipeline_data[GST_PIPELINE_CNT] = {
  {DEFAULT_SNPE_OBJECT_DETECTION_MODEL, DEFAULT_OBJECT_DETECTION_LABELS,
      "qtimlvconverter", "qtimlsnpe", "qtimlvdetection",
      {0, 0, 960, 540}, GST_ML_SNPE_DELEGATE_DSP},
  {DEFAULT_TFLITE_CLASSIFICATION_MODEL, DEFAULT_CLASSIFICATION_LABELS,
      "qtimlvconverter", "qtimltflite", "qtimlvclassification",
      {960, 0, 960, 540}, GST_ML_TFLITE_DELEGATE_EXTERNAL,
      "Mobilenet,q-offsets=<-95.0>,q-scales=<0.18740029633045197>;"},
  {DEFAULT_TFLITE_POSE_DETECTION_MODEL, DEFAULT_POSE_DETECTION_LABELS,
      "qtimlvconverter", "qtimltflite", "qtimlvpose",
      {0, 540, 960, 540}, GST_ML_TFLITE_DELEGATE_EXTERNAL,
      "Posenet,q-offsets=<8.0>,q-scales=<0.0040499246679246426>;"},
  {DEFAULT_SNPE_SEGMENTATION_MODEL, DEFAULT_SEGMENTATION_LABELS,
      "qtimlvconverter", "qtimlsnpe", "qtimlvsegmentation",
      {960, 540, 960, 540}, GST_ML_SNPE_DELEGATE_DSP}
};

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

/*
 * Update Window Grid
 * Change position of grid as per display resolution
 */
static void
update_window_grid ()
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

  GstVideoRectangle window_grid[GST_PIPELINE_CNT] = {
    {0, 0, win_w, win_h},
    {win_w, 0, win_w, win_h},
    {0, win_h, win_w, win_h},
    {win_w, win_h, win_w, win_h}
  };

  for (gint idx = 0; idx < GST_PIPELINE_CNT; idx++) {
    pipeline_data[idx].position = window_grid[idx];
  }
}

/**
 * Callback function used for demuxer dynamic pad.
 *
 * @param element Plugin supporting dynamic pad.
 * @param pad The source pad that is added.
 * @param data Userdata set at callback registration.
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");
  g_assert (gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK);

  gst_object_unref (sinkpad);
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

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 */
static gboolean
create_pipe (GstAppContext * appctx, GstAppOptions * options)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *qtimlvconverter[GST_PIPELINE_CNT] = {NULL};
  GstElement *qtimlelement[GST_PIPELINE_CNT] = {NULL};
  GstElement *qtimlvpostproc[GST_PIPELINE_CNT] = {NULL};
  GstElement *detection_filter[GST_PIPELINE_CNT] = {NULL};
  GstElement *qtivcomposer[GST_PIPELINE_CNT] = {NULL};
  GstElement *waylandsink[GST_PIPELINE_CNT] = {NULL};
  GstElement *fpsdisplaysink[GST_PIPELINE_CNT] = {NULL};
  GstElement *queue[QUEUE_COUNT] = {NULL}, *tee = NULL;;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse = NULL;
  GstElement *v4l2h264dec = NULL, *rtspsrc = NULL, *rtph264depay = NULL;
  GstStructure *delegate_options = NULL;
  GstCaps *filtercaps = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint module_id;
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;

  update_window_grid ();

 if (options->use_file) {
    // Create file source element for file stream
    filesrc = gst_element_factory_make ("filesrc", "filesrc");
    if (!filesrc ) {
      g_printerr ("Failed to create filesrc\n");
      goto error_clean_elements;
    }

    // Create qtdemux for demuxing the filesrc
    qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");
    if (!qtdemux ) {
      g_printerr ("Failed to create qtdemux\n");
      goto error_clean_elements;
    }

    // Create h264parse element for parsing the stream
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    // Create v4l2h264dec element for encoding the stream
    v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
    if (!v4l2h264dec) {
      g_printerr ("Failed to create v4l2h264dec\n");
      goto error_clean_elements;
    }

  } else if (options->use_rtsp) {
    // Create rtspsrc plugin for rtsp input
    rtspsrc = gst_element_factory_make ("rtspsrc", "rtspsrc");
    if (!rtspsrc) {
      g_printerr ("Failed to create rtspsrc\n");
      goto error_clean_elements;
    }

    // Create rtph264depay plugin for rtsp payload parsing
    rtph264depay = gst_element_factory_make ("rtph264depay", "rtph264depay");
    if (!rtph264depay) {
      g_printerr ("Failed to create rtph264depay\n");
      goto error_clean_elements;
    }

    // Create h264parse element for parsing the stream
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    // Create v4l2h264dec element for encoding the stream
    v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
    if (!v4l2h264dec) {
      g_printerr ("Failed to create v4l2h264dec\n");
      goto error_clean_elements;
    }
  } else {
    // 1. Create the elements or Plugins
    // Create qtiqmmfsrc plugin for camera stream
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
    if (!qtiqmmfsrc) {
      g_printerr ("Failed to create qtiqmmfsrc\n");
      goto error_clean_elements;
    }

    // Use capsfilter to define the camera output settings
    qmmfsrc_caps = gst_element_factory_make ("capsfilter", "qmmfsrc_caps");
    if (!qmmfsrc_caps) {
      g_printerr ("Failed to create qmmfsrc_caps\n");
      goto error_clean_elements;
    }
  }

  // Use tee to send same data buffer
  // one for AI inferencing, one for Display composition
  tee = gst_element_factory_make ("tee", "tee");
  if (!tee) {
    g_printerr ("Failed to create tee\n");
    goto error_clean_elements;
  }

  // Composer to combine 4 input streams as 2x2 matrix in single display
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    snprintf (element_name, 127, "qtivcomposer-%d", i);
    qtivcomposer[i] = gst_element_factory_make ("qtivcomposer", element_name);
    if (!qtivcomposer[i]) {
      g_printerr ("Failed to create qtivcomposer\n");
      goto error_clean_elements;
    }
  }

  // Create 4 pipelines for AI inferencing on same camera stream
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    const gchar *preproc = pipeline_data[i].preproc;
    const gchar *mlframework = pipeline_data[i].mlframework;
    const gchar *postproc = pipeline_data[i].postproc;

    // Create qtimlvconverter for Input preprocessing
    snprintf (element_name, 127, "%s-%d", preproc, i);
    qtimlvconverter[i] = gst_element_factory_make (preproc, element_name);
    if (!qtimlvconverter[i]) {
      g_printerr ("Failed to create qtimlvconverter %d\n", i);
      goto error_clean_elements;
    }

    // Create the ML inferencing plugin SNPE/TFLite
    snprintf (element_name, 127, "%s-%d", mlframework, i);
    qtimlelement[i] = gst_element_factory_make (mlframework, element_name);
    if (!qtimlelement[i]) {
      g_printerr ("Failed to create qtimlelement %d\n", i);
      goto error_clean_elements;
    }

    // Plugin for ML postprocessing based on config
    snprintf (element_name, 127, "%s-%d", postproc, i);
    qtimlvpostproc[i] = gst_element_factory_make (postproc, element_name);
    if (!qtimlvpostproc[i]) {
      g_printerr ("Failed to create qtimlvpostproc %d\n", i);
      goto error_clean_elements;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "capsfilter-%d", i);
    detection_filter[i] = gst_element_factory_make ("capsfilter",
        element_name);
    if (!detection_filter[i]) {
      g_printerr ("Failed to create detection_filter %d\n", i);
      goto error_clean_elements;
    }
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

  // Create Wayland compositor to render output on Display
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    snprintf (element_name, 127, "waylandsink-%d", i);
    waylandsink[i] = gst_element_factory_make ("waylandsink", element_name);
    if (!waylandsink[i]) {
      g_printerr ("Failed to create waylandsink \n");
      goto error_clean_elements;
    }
  }

  // Creating fpsdisplaysink to display the current and
  // average framerate as a text overlay
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    snprintf (element_name, 127, "fpsdisplaysink-%d", i);
    fpsdisplaysink[i] = gst_element_factory_make ("fpsdisplaysink",
        element_name);
    if (!fpsdisplaysink[i]) {
      g_printerr ("Failed to create fpsdisplaysink \n");
      goto error_clean_elements;
    }
  }

  // 2. Set properties for all GST plugin elements
  // 2.1 set properties for 4 AI pipelines, like HW, Model, Post proc
  if (options->use_file) {
    g_object_set (G_OBJECT (v4l2h264dec), "capture-io-mode", 5, NULL);
    g_object_set (G_OBJECT (v4l2h264dec), "output-io-mode", 5, NULL);
    g_object_set (G_OBJECT (filesrc), "location", options->file_path, NULL);
  } else if (options->use_rtsp) {
    g_object_set (G_OBJECT (v4l2h264dec), "capture-io-mode", 5, NULL);
    g_object_set (G_OBJECT (v4l2h264dec), "output-io-mode", 5, NULL);
    g_object_set (G_OBJECT (rtspsrc), "location", options->rtsp_ip_port, NULL);
  } else {
    g_object_set (G_OBJECT (qtiqmmfsrc), "camera",options->camera_type, NULL);

    if (options->camera_type == GST_CAMERA_TYPE_PRIMARY) {
      // 2.2 Set the capabilities of camera plugin output
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, DEFAULT_CAMERA_OUTPUT_WIDTH,
          "height", G_TYPE_INT, DEFAULT_CAMERA_OUTPUT_HEIGHT,
          "framerate", GST_TYPE_FRACTION, framerate, 1,
          "compression", G_TYPE_STRING, "ubwc", NULL);
    } else {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, SECONDARY_CAMERA_OUTPUT_WIDTH,
          "height", G_TYPE_INT, SECONDARY_CAMERA_OUTPUT_HEIGHT,
          "framerate", GST_TYPE_FRACTION, framerate, 1,
          "compression", G_TYPE_STRING, "ubwc", NULL);
    }

    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));

    g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  }

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    GValue layers = G_VALUE_INIT;
    GValue value = G_VALUE_INIT;

    // Set ML plugin properties: HW, Model, Labels
    g_object_set (G_OBJECT (qtimlelement[i]),
        "delegate", pipeline_data[i].delegate, NULL);
    if (pipeline_data[i].delegate == GST_ML_TFLITE_DELEGATE_EXTERNAL) {
      delegate_options = gst_structure_from_string (
          "QNNExternalDelegate,backend_type=htp;", NULL);
      g_object_set (G_OBJECT (qtimlelement[i]),
          "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
      g_object_set (G_OBJECT (qtimlelement[i]),
          "external-delegate-options", delegate_options, NULL);
      gst_structure_free (delegate_options);
    }

    g_object_set (G_OBJECT (qtimlelement[i]),
        "model", pipeline_data[i].model, NULL);
    g_object_set (G_OBJECT (qtimlvpostproc[i]),
        "labels", pipeline_data[i].labels, NULL);

    // Set properties for ML postproc plugins- module, layers, threshold
    switch (i) {
      case GST_OBJECT_DETECTION:
        g_value_init (&layers, GST_TYPE_ARRAY);
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, "/heads/Mul");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "/heads/Sigmoid");
        gst_value_array_append_value (&layers, &value);
        g_object_set_property (G_OBJECT (qtimlelement[i]), "layers", &layers);

        module_id = get_enum_value (qtimlvpostproc[i], "module", "yolo-nas");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]),
              "module", module_id, NULL);
        } else {
          g_printerr ("Module yolo-nas is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        // Set the object detection module threshold limit
        g_object_set (G_OBJECT (qtimlvpostproc[GST_OBJECT_DETECTION]),
            "threshold", 40.0, "results", 10, NULL);
        break;

      case GST_CLASSIFICATION:
        module_id = get_enum_value (qtimlvpostproc[i], "module", "mobilenet");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]), "threshold", 40.0,
              "results", 2, "module", module_id,
              "extra-operation", GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX,
              "constants", pipeline_data[i].constants,
              NULL);
        } else {
          g_printerr ("Module mobilenet not available in "
              "qtimlvclassifivation\n");
          goto error_clean_elements;
        }
        break;

      case GST_POSE_DETECTION:
        module_id = get_enum_value (qtimlvpostproc[i], "module", "hrnet");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]), "threshold", 40.0,
              "results", 2, "module", module_id,
              "constants", pipeline_data[i].constants,
              NULL);
        } else {
          g_printerr ("Module hrnet is not available in qtimlvpose\n");
          goto error_clean_elements;
        }
        break;

      case GST_SEGMENTATION:
        module_id = get_enum_value (qtimlvpostproc[i], "module",
            "deeplab-argmax");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]),
              "module", module_id, NULL);
        } else {
          g_printerr ("Module deeplab-argmax is not available in "
              "qtimlvsegmentation\n");
          goto error_clean_elements;
        }
        break;
      default:
        g_printerr ("Cannot be here");
        goto error_clean_elements;
    }
  }


  // Set the properties of pad_filter for negotiation with qtivcomposer
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    if (i == GST_SEGMENTATION) {
      // Use different detection filter settings for segmentation
      continue;
    }
    g_object_set (G_OBJECT (detection_filter[i]), "caps", filtercaps, NULL);
  }
  gst_caps_unref (filtercaps);

  // Use specific detection filter settings for segmentation
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, 256,
      "height", G_TYPE_INT, 144, NULL);

  g_object_set (G_OBJECT (detection_filter[GST_SEGMENTATION]),
      "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.3 Set the properties of Wayland compositor
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    GstVideoRectangle pos = pipeline_data[i].position;
    g_object_set (G_OBJECT (waylandsink[i]), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (waylandsink[i]), "x", pos.x, NULL);
    g_object_set (G_OBJECT (waylandsink[i]), "y", pos.y, NULL);
    g_object_set (G_OBJECT (waylandsink[i]), "width", pos.w, NULL);
    g_object_set (G_OBJECT (waylandsink[i]), "height", pos.h, NULL);
  }

  // 2.4 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    g_object_set (G_OBJECT (fpsdisplaysink[i]), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink[i]), "signal-fps-measurements",
        TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink[i]), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink[i]), "video-sink",
        waylandsink[i], NULL);
  }

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options->use_file) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc,  qtdemux,
        h264parse,v4l2h264dec, tee, NULL);
  } else if (options->use_rtsp) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc,
        rtph264depay, h264parse, v4l2h264dec, tee, NULL);
  } else {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,  qmmfsrc_caps,
        tee, NULL);
  }

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter[i],
        qtimlelement[i], qtimlvpostproc[i], detection_filter[i],
        qtivcomposer[i], fpsdisplaysink[i], NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // 3.1 Create pipeline for Parallel Inferencing
  if (options->use_file) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "filesource->qtdemux\n");
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (queue[0], h264parse, v4l2h264dec,
        tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for parse->tee\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_rtsp) {
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse,
        v4l2h264dec, tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
      "rtspsource->rtph264depay\n");
      goto error_clean_pipeline;
    }
  }
  else {
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[0],
        tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for qmmfsource->tee\n");
      goto error_clean_pipeline;
    }
  }
  // 3.2 Create links for all 4 streams
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    ret = gst_element_link_many (
        tee, queue[6*i + 1], qtivcomposer[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for object detection\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (qtivcomposer[i], queue[6*i + 2],
        fpsdisplaysink[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
      "composer->fpsdisplaysink.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (
        tee, queue[6*i + 3], qtimlvconverter[i], queue[6*i + 4],
        qtimlelement[i], queue[6*i + 5],
        qtimlvpostproc[i],
        detection_filter[i], queue[6*i + 6], qtivcomposer[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for object detection\n");
      goto error_clean_pipeline;
    }
  }

  g_print ("All elements are linked successfully\n");

  if (options->use_file) {
    // 3.3 Setup dynamic pad to link qtdemux to queue
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  } else if (options->use_rtsp) {
    // 3.4 Setup dynamic pad to link qtdemux to queue
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  }

  // 3.5 Set position of Object Classification window and alpha channel
  // value for Segmentation overlay window
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    GstPad *composer_sink;
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;
    gdouble alpha_value;
    gint pos_vals[2], dim_vals[2];

    switch (i) {
      case GST_CLASSIFICATION:
        g_value_init (&position, GST_TYPE_ARRAY);
        g_value_init (&dimension, GST_TYPE_ARRAY);
        pos_vals[0] = 30; pos_vals[1] = 45;
        dim_vals[0] = 320; dim_vals[1] = 180;
        composer_sink = gst_element_get_static_pad (
            qtivcomposer[GST_CLASSIFICATION], "sink_1");
        if (!composer_sink) {
          g_printerr ("Sink pad 1 of Classification composer couldn't "
              "be retrieved\n");
          goto error_clean_pipeline;
        }

        build_pad_property (&position, pos_vals, 2);
        build_pad_property (&dimension, dim_vals, 2);
        g_object_set_property (G_OBJECT (composer_sink),
            "position", &position);
        g_object_set_property (G_OBJECT (composer_sink),
            "dimensions", &dimension);
        g_value_unset (&position);
        g_value_unset (&dimension);
        gst_object_unref (composer_sink);
        break;
      case GST_SEGMENTATION:
        // Create 1 composer pads for segmentation pipeline
        // to assign alpha channel value.
        composer_sink = gst_element_get_static_pad (
            qtivcomposer[GST_SEGMENTATION], "sink_1");
        if (!composer_sink) {
          g_printerr ("Sink pad 1 of Segmentation composer couldn't "
              "be retrieved\n");
          goto error_clean_pipeline;
        }

        alpha_value = 0.5;
        g_object_set (composer_sink, "alpha", &alpha_value, NULL);
        gst_object_unref (composer_sink);
        break;
    }
  }

  return TRUE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, &filesrc, &qtdemux,
      &h264parse, &v4l2h264dec, &rtspsrc, &rtph264depay, &tee, NULL);

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    cleanup_gst (GST_BIN (appctx->pipeline), qtimlvconverter[i],
        qtimlelement[i], qtimlvpostproc[i], detection_filter[i],
        qtivcomposer[i], fpsdisplaysink[i], NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }
  return FALSE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);

  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GstBus *bus = NULL;
  GMainLoop *mloop = NULL;
  GstElement *pipeline = NULL;
  const gchar *app_name = NULL;
  GstAppContext appctx = {};
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;
  GOptionContext *ctx = NULL;
  gchar help_description[2048];
  GstAppOptions options = {};

  options.camera_type = GST_CAMERA_TYPE_NONE;
  options.file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.use_file = FALSE;
  options.use_rtsp = FALSE;
  options.use_camera = FALSE;

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

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
    { NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];
  snprintf (help_description, 2047, "\nExample:\n"
#ifdef ENABLE_CAMERA
      "  %s --camera=0\n"
#endif // ENABLE_CAMERA
      "  %s --file-path=\"/opt/video.mp4\"\n"
      "  %s --rtsp-ip-port=\"rtsp://<ip>:<port>/<stream>\"\n"
      "\nThis Sample App demonstrates Classification, Segmemtation"
      "Object Detection, Pose Detection On Live Stream "
      "and output 4 Parallel Stream.\n\n"
      "Default Path for model and labels used are as below:\n"
      "  ------------------------------------------------------------"
      "--------------------------------------------\n"
      "  |Algorithm         %-50s  %-32s|\n"
      "  ------------------------------------------------------------"
      "--------------------------------------------\n"
      "  |Object detection  %-50s  %-32s|\n"
      "  |Pose estimation   %-50s  %-32s|\n"
      "  |Segmentation      %-50s  %-32s|\n"
      "  |Classification    %-50s  %-32s|\n"
      "  ------------------------------------------------------------"
      "--------------------------------------------\n"
      "\nTo use your own model and labels replace at the default paths\n",
#ifdef ENABLE_CAMERA
      app_name,
#endif // ENABLE_CAMERA
      app_name, app_name, "Model", "Labels",
      DEFAULT_SNPE_OBJECT_DETECTION_MODEL, DEFAULT_OBJECT_DETECTION_LABELS,
      DEFAULT_TFLITE_POSE_DETECTION_MODEL,DEFAULT_POSE_DETECTION_LABELS,
      DEFAULT_SNPE_SEGMENTATION_MODEL, DEFAULT_SEGMENTATION_LABELS,
      DEFAULT_TFLITE_CLASSIFICATION_MODEL, DEFAULT_CLASSIFICATION_LABELS);
  help_description[2047] = '\0';

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
      // gst_app_context_free (&appctx, &options);
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
    g_print ("User need to give proper input file as source\n");
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
    g_print ("Using PRIMARY camera by default,"
        " Not valid camera id selected\n");
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

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    if (!file_exists (pipeline_data[i].model)) {
      g_printerr ("File does not exist: %s\n", pipeline_data[i].model);
      gst_app_context_free (&appctx, &options);
      return -EINVAL;
    }

    if (!file_exists (pipeline_data[i].labels)) {
      g_printerr ("File does not exist: %s\n", pipeline_data[i].labels);
      gst_app_context_free (&appctx, &options);
      return -EINVAL;
    }
  }

  if (options.use_file) {
    if (!file_exists (options.file_path)) {
      g_print ("Invalid file source path: %s\n", options.file_path);
      gst_app_context_free (&appctx, &options);
      return -EINVAL;
    }
  }

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
