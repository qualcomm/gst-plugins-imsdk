/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based daisy chain Object Detection and Classification
 *
 * Description:
 * The application takes live video/file/rtsp stream and gives same to
 * Yolo models for object detection and splits frame based on bounding box
 * for classifcation, displays preview with overlayed
 * AI Model output Labels.
 *
* Pipeline for Gstreamer with Camera:
 * qtiqmmfsrc (Preview)     -> qmmfsrc_caps  -> qtimetamux
 * qtiqmmfsrc (Daisychain)  -> qmmfsrc_caps  -> Pre process-> ML Framework
 *                                           -> Post process -> qtimetamux
 *                          |-> qtivcomposer
 *     qtimetamux -> tee -> |
 *                          |-> qtivsplit ->tee (4 splits)
 *                                         | -> qtivcomposer
 *                                  tee -> |
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *
 *                                         | -> qtivcomposer
 *                                  tee -> |
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *
 *                                         | -> qtivcomposer
 *                                  tee -> |
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *
 *                                         | -> qtivcomposer
 *                                  tee  ->|
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *
 * Pipeline for Gstreamer with File/RTSP source:
 *
 * File source:
 * filesrc -> qtdemux -> h264parse
 *
 * RTSP source:
 * rtspsrc -> rtph264depay -> h264parse
 *
 * Common for both File and RTSP:
 * h264parse -> v4l2h264dec  -> tee (2 splits)
 *            | -> qtimetamux
 *      tee ->|
 *            | -> Pre process-> ML Framework -> Post process -> qtimetamux
 *
 *                          |-> qtivcomposer
 *     qtimetamux -> tee -> |
 *                          |-> qtivsplit ->tee (4 splits)
 *                                         | -> qtivcomposer
 *                                  tee -> |
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *
 *                                         | -> qtivcomposer
 *                                  tee -> |
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *
 *                                         | -> qtivcomposer
 *                                  tee -> |
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *
 *                                         | -> qtivcomposer
 *                                  tee  ->|
 *                                         | -> Pre process-> ML Framework
 *                                           -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimltflite
 *     Post process: qtimlvdetection / qtimlvclassification -> filter
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_TFLITE_YOLOV5_MODEL "/opt/yolov5.tflite"
#define DEFAULT_TFLITE_CLASSIFICATION_MODEL \
    "/opt/inceptionv3.tflite"
#define DEFAULT_YOLOV5_LABELS "/opt/yolov5.labels"
#define DEFAULT_CLASSIFICATION_LABELS "/opt/classification.labels"

/**
 * Default path of config file
 */
#define DEFAULT_CONFIG_FILE "/opt/config_daisychain_detection_classification.json"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_CAMERA_DAISYCHAIN_OUTPUT_WIDTH 640
#define DEFAULT_CAMERA_DAISYCHAIN_OUTPUT_HEIGHT 360
#define DEFAULT_CAMERA_PREVIEW_OUTPUT_WIDTH 1920
#define DEFAULT_CAMERA_PREVIEW_OUTPUT_HEIGHT 1080
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Maximum count of various sources possible to configure
 */
#define QUEUE_COUNT 8
#define TEE_COUNT 6
#define DETECTION_COUNT 1
#define CLASSIFICATION_COUNT 4
#define TFLITE_ELEMENT_COUNT 5
#define SPLIT_COUNT 4
#define COMPOSER_SINK_COUNT 9
#define SINGLE_ROI_META 2

/**
 * Scale and Offset valu for YOLOV5 for post processing
 */
#define YOLOV5_CONSTANT "YoloV5,q-offsets=<3.0>,q-scales=<0.005047998391091824>;"

/**
 * Structure for various application specific options
 */
typedef struct {
  gboolean camera_source;
  gchar *file_path;
  gchar *rtsp_ip_port;
  gchar *detection_model_path;
  gchar *classification_model_path;
  gchar *detection_labels_path;
  gchar *classification_labels_path;
  gchar *detection_constants;
  GstStreamSourceType source_type;
} GstAppOptions;


/**
 * GstDaisyChainModelType:
 * @GST_DETECTION_TYPE_YOLO            : Yolov5 Object Detection Model.
 * @GST_CLASSIFICATION_TYPE_INCEPTION  : Inception Classification Model.
 *
 * Type of Usecase.
 */
typedef enum {
  GST_DETECTION_TYPE_YOLO,
  GST_CLASSIFICATION_TYPE_INCEPTION
} GstDaisyChainModelType;

/**
 * Static grid points to display 4 split stream
 */
static GstVideoRectangle composer_sink_position[COMPOSER_SINK_COUNT] = {
  {0, 0, 1280, 720}, {0, 0, 384, 216}, {896, 0, 384, 216},
  {0, 504, 384, 216}, {896, 504, 384, 216}, {0, 0, 384, 40},
  {896, 0, 384, 40}, {0, 504, 384, 40}, {896, 504, 384, 40},
};

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (
    GstAppContext * appctx, GstAppOptions * options, gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->file_path != NULL) {
    g_free (options->file_path);
    options->file_path = NULL;
  }

  if (options->rtsp_ip_port != NULL) {
    g_free (options->rtsp_ip_port);
    options->rtsp_ip_port = NULL;
  }

  if (options->detection_model_path != NULL &&
      options->detection_model_path != DEFAULT_TFLITE_YOLOV5_MODEL) {
    g_free (options->detection_model_path);
    options->detection_model_path = NULL;
  }

  if (options->classification_model_path != NULL &&
      options->classification_model_path != DEFAULT_TFLITE_CLASSIFICATION_MODEL) {
    g_free (options->classification_model_path);
    options->classification_model_path = NULL;
  }

  if (options->detection_labels_path != NULL &&
      options->detection_labels_path != DEFAULT_YOLOV5_LABELS) {
    g_free (options->detection_labels_path);
    options->detection_labels_path = NULL;
  }

  if (options->classification_labels_path != NULL &&
      options->classification_labels_path != DEFAULT_CLASSIFICATION_LABELS) {
    g_free (options->classification_labels_path);
    options->classification_labels_path = NULL;
  }

  if (options->detection_constants != NULL &&
      options->detection_constants != YOLOV5_CONSTANT) {
    g_free (options->detection_constants);
    options->detection_constants = NULL;
  }

  if (config_file != NULL &&
      config_file != DEFAULT_CONFIG_FILE) {
    g_free (config_file);
    config_file = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }
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
 * Callback function used for demuxer dynamic pad.
 *
 * @param element Plugin supporting dynamic pad.
 * @param pad The source pad that is added.
 * @param data Userdata set at callback registration.
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad = NULL;
  gchar *caps_str = NULL;
  GstElement *queue = (GstElement *) data;
  GstCaps *caps = gst_pad_get_current_caps (pad);
  if (!caps) {
    caps = gst_pad_query_caps (pad, NULL);
  }

  if (caps) {
    caps_str = gst_caps_to_string (caps);
  } else {
    g_print ("No caps available for this pad\n");
  }

  // Check if caps contains video
  if (caps_str) {
    if (g_strrstr (caps_str, "video")) {
      // Get the static sink pad from the queue
      sinkpad = gst_element_get_static_pad (queue, "sink");
      // Get the static sink pad from the queue
      g_assert (gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK);
      gst_object_unref (sinkpad);
    } else {
      g_print ("Ignoring caps\n");
    }
  }
  g_free (caps_str);
  gst_caps_unref (caps);
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 * @param source_type Type of stream (camera/file/RTSP).
 * @param file_source Location of video file.
 * @param rtsp_source RTSP stream.
 */
static gboolean
create_pipe (GstAppContext * appctx, const GstAppOptions options)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *queue[QUEUE_COUNT] = {NULL}, *qmmfsrc_caps_preview = NULL;
  GstElement *tee[TEE_COUNT] = {NULL};
  GstElement *qtimlvconverter[TFLITE_ELEMENT_COUNT] = {NULL};
  GstElement *qtimlelement[TFLITE_ELEMENT_COUNT] = {NULL};
  GstElement *classification_filter[CLASSIFICATION_COUNT] = {NULL};
  GstElement *qtimlvdetection[DETECTION_COUNT] = {NULL} ;
  GstElement *qtimlvclassification[CLASSIFICATION_COUNT] = {NULL} ;
  GstElement  *fpsdisplaysink = NULL, *waylandsink = NULL, *qtimetamux = NULL;
  GstElement *qtivsplit = NULL, *qtivcomposer = NULL;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse = NULL;
  GstElement *rtspsrc = NULL, *rtph264depay = NULL, *v4l2h264dec = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstStructure *delegate_options = NULL;
  GstPad *qtiqmmfsrc_type = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint daisychain_width = DEFAULT_CAMERA_DAISYCHAIN_OUTPUT_WIDTH;
  gint daisychain_height = DEFAULT_CAMERA_DAISYCHAIN_OUTPUT_HEIGHT;
  gint preview_width = DEFAULT_CAMERA_PREVIEW_OUTPUT_WIDTH;
  gint preview_height = DEFAULT_CAMERA_PREVIEW_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;
  gint pos_vals[2], dim_vals[2];
  GValue video_type = G_VALUE_INIT;

  // 1. Create the elements or Plugins
  if (options.source_type == GST_STREAM_TYPE_CAMERA) {
    // Create qtiqmmfsrc plugin for camera stream
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
    if (!qtiqmmfsrc) {
      g_printerr ("Failed to create qtiqmmfsrc\n");
      goto error_clean_elements;
    }

    // Use capsfilter to define the camera output settings for daisychain
    qmmfsrc_caps = gst_element_factory_make ("capsfilter", "qmmfsrc_caps");
    if (!qmmfsrc_caps) {
      g_printerr ("Failed to create qmmfsrc_caps\n");
      goto error_clean_elements;
    }

    // Use capsfilter to define the camera output settings for preview
    qmmfsrc_caps_preview = gst_element_factory_make ("capsfilter",
        "qmmfsrc_caps_preview");
    if (!qmmfsrc_caps_preview) {
      g_printerr ("Failed to create qmmfsrc_caps_preview\n");
      goto error_clean_elements;
    }
  } else if (options.source_type == GST_STREAM_TYPE_FILE) {
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

    // Create v4l2h264dec element for decoding the stream
    v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
    if (!v4l2h264dec) {
      g_printerr ("Failed to create v4l2h264dec\n");
      goto error_clean_elements;
    }
  } else if (options.source_type == GST_STREAM_TYPE_RTSP) {
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

    // Create v4l2h264dec element for decoding the stream
    v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
    if (!v4l2h264dec) {
      g_printerr ("Failed to create v4l2h264dec\n");
      goto error_clean_elements;
    }
  }
  // Create qtimetamux element to attach postprocessing string results
  // on original frame
  qtimetamux = gst_element_factory_make ("qtimetamux", "qtimetamux");
  if (!qtimetamux) {
    g_printerr ("Failed to create qtimetamux\n");
    goto error_clean_elements;
  }

  // Create qtivcomposer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }

  // Create qtivsplit to split single stream to multiple streams
  qtivsplit = gst_element_factory_make ("qtivsplit", "qtivsplit");
  if (!qtivsplit) {
    g_printerr ("Failed to create qtivsplit\n");
    goto error_clean_elements;
  }

  // Create queue element for processing
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create tee to send same data buffer to mulitple elements
  for (gint i = 0; i < TEE_COUNT; i++) {
    snprintf (element_name, 127, "tee-%d", i);
    tee[i] = gst_element_factory_make ("tee", element_name);
    if (!tee[i]) {
      g_printerr ("Failed to create tee %d\n", i);
      goto error_clean_elements;
    }
  }

  // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    snprintf (element_name, 127, "classification_filter-%d", i);
    classification_filter[i] =
        gst_element_factory_make ("capsfilter", element_name);
    if (!classification_filter[i]) {
      g_printerr ("Failed to create classification_filter %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create qtimlvconverter for Input preprocessing
  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    snprintf (element_name, 127, "qtimlvconverter-%d", i);
    qtimlvconverter[i] =
        gst_element_factory_make ("qtimlvconverter", element_name);
    if (!qtimlvconverter[i]) {
      g_printerr ("Failed to create qtimlvconverter %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create the ML inferencing plugin TFLite
  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    snprintf (element_name, 127, "qtimltflite-%d", i);
    qtimlelement[i] = gst_element_factory_make ("qtimltflite", element_name);
    if (!qtimlelement[i]) {
      g_printerr ("Failed to create qtimlelement %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create plugin for ML postprocessing for object detection
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    snprintf (element_name, 127, "qtimlvdetection-%d", i);
    qtimlvdetection[i] =
        gst_element_factory_make ("qtimlvdetection", element_name);
    if (!qtimlvdetection[i]) {
      g_printerr ("Failed to create qtimlvdetection %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create plugin for ML postprocessing for classification
  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    snprintf (element_name, 127, "qtimlvclassification-%d", i);
    qtimlvclassification[i] =
        gst_element_factory_make ("qtimlvclassification", element_name);
    if (!qtimlvclassification[i]) {
      g_printerr ("Failed to create qtimlvclassification %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create Wayland compositor to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink \n");
    goto error_clean_elements;
  }

  // Create fpsdisplaysink to display the current and
  // average framerate as a text overlay
  fpsdisplaysink =
      gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
  if (!fpsdisplaysink ) {
    g_printerr ("Failed to create fpsdisplaysink\n");
    goto error_clean_elements;
  }

  // 2. Set properties for all GST plugin elements
  if (options.source_type == GST_STREAM_TYPE_CAMERA) {
    // 2.1 Set the capabilities of camera stream for daisychain
    filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, daisychain_width,
      "height", G_TYPE_INT, daisychain_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    // 2.2 Set the capabilities of camera stream for preview
    filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, preview_width,
      "height", G_TYPE_INT, preview_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (qmmfsrc_caps_preview), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options.source_type == GST_STREAM_TYPE_FILE) {
    // 2.3 Set the capabilities of file stream
    g_object_set (G_OBJECT (filesrc), "location", options.file_path, NULL);
    g_object_set (G_OBJECT (v4l2h264dec), "capture-io-mode", 5, NULL);
    g_object_set (G_OBJECT (v4l2h264dec), "output-io-mode", 5, NULL);
  } else if (options.source_type == GST_STREAM_TYPE_RTSP) {
    // 2.3 Set the capabilities of file stream
    g_object_set (G_OBJECT (rtspsrc), "location", options.rtsp_ip_port, NULL);
    g_object_set (G_OBJECT (v4l2h264dec), "capture-io-mode", 5, NULL);
    g_object_set (G_OBJECT (v4l2h264dec), "output-io-mode", 5, NULL);
  }

  // 2.3 Set the properties of pad_filter for negotiation with qtivcomposer
  // for classification
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, 384,
      "height", G_TYPE_INT, 40, NULL);
  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    g_object_set (G_OBJECT (classification_filter[i]), "caps", pad_filter, NULL);

  }
  gst_caps_unref (pad_filter);

  // 2.4 Select the HW to DSP for model inferencing using delegate property
  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    if (i == GST_DETECTION_TYPE_YOLO)
    {
      g_object_set (G_OBJECT (qtimlelement[i]),
          "model", options.detection_model_path,
          "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
    }
    else
    {
      g_object_set (G_OBJECT (qtimlelement[i]),
          "model", options.classification_model_path,
          "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
    }
    delegate_options = gst_structure_from_string (
        "QNNExternalDelegate,backend_type=htp;", NULL);
    g_object_set (G_OBJECT (qtimlelement[i]),
        "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
    g_object_set (G_OBJECT (qtimlelement[i]),
        "external-delegate-options", delegate_options, NULL);
    gst_structure_free (delegate_options);
  }

  // 2.5 Set properties for detection postproc plugins- module, labels,
  // threshold, constants
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    module_id = get_enum_value (qtimlvdetection[i], "module", "yolov5");
    if (module_id != -1) {
      g_object_set (G_OBJECT (qtimlvdetection[i]),
          "threshold", 40.0, "results", 4,
          "module", module_id, "labels", options.detection_labels_path,
          "constants", options.detection_constants,
          NULL);
      }
    else {
      g_printerr ("Module yolov5 is not available in qtimlvdetection.\n");
      goto error_clean_elements;
    }
  }

  // 2.6 Set properties for classification postproc plugins- module, labels,
  // threshold
  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    module_id = get_enum_value (qtimlvclassification[i], "module", "mobilenet");
    if (module_id != -1) {
      g_object_set (G_OBJECT (qtimlvclassification[i]),
          "threshold", 40.0, "results", 2,
          "module", module_id,
          "labels", options.classification_labels_path, NULL);
      }
    else {
      g_printerr ("Module mobilenet is not available in qtimlvclassification.\n");
      goto error_clean_elements;
    }
  }

  // 2.7 Set the properties of Wayland compositor
  g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // 2.8 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  g_object_set (G_OBJECT (fpsdisplaysink), "sync", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options.source_type == GST_STREAM_TYPE_CAMERA) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_caps,
        qmmfsrc_caps_preview, NULL);
  } else if (options.source_type == GST_STREAM_TYPE_FILE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        filesrc, qtdemux, h264parse, v4l2h264dec, NULL);
  } else if (options.source_type == GST_STREAM_TYPE_RTSP) {
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        rtspsrc, rtph264depay, h264parse, v4l2h264dec, NULL);
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtimetamux, qtivsplit, qtivcomposer, fpsdisplaysink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  for (gint i = 0; i < TEE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), tee[i], NULL);
  }

  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), classification_filter[i], NULL);
  }

  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlelement[i], NULL);
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter[i], NULL);
  }

  for (gint i = 0; i < DETECTION_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvdetection[i], NULL);
  }

  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvclassification[i], NULL);
  }

  // 3.1 Create pipeline for Parallel Inferencing
  g_print ("Linking elements...\n");
  if (options.source_type == GST_STREAM_TYPE_CAMERA) {
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps,
        queue[1], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtiqmmfsrc -> qmmfsrc_caps"
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps_preview,
      qtimetamux, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtiqmmfsrc -> qmmfsrc_caps_preview"
          " -> qtimetamux cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  } else if (options.source_type == GST_STREAM_TYPE_FILE) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements filesrc -> qtdemux elements "
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[0], h264parse, v4l2h264dec,
        tee[0], qtimetamux, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtdemux -> h264parse -> v4l2h264dec"
          " ->qtimetamux  cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (tee[0], queue[1], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee -> queue cannot be linked."
          "Exiting.\n");
      goto error_clean_pipeline;
    }
  } else if (options.source_type == GST_STREAM_TYPE_RTSP) {
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse,
        v4l2h264dec, tee[0], qtimetamux, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements rtph264depay -> h264parse -> v4l2h264dec"
          " -> qtimetamux cannot be linked.Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (tee[0], queue[1], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee and queue cannot be linked."
          "Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  ret = gst_element_link_many (queue[1], qtimlvconverter[0], qtimlelement[0],
      qtimlvdetection[0], NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements src -> qtimlvconverter -> qtimlelement "
        " -> qtimlvdetection cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  filtercaps = gst_caps_from_string ("text/x-raw");
  ret = gst_element_link_filtered (qtimlvdetection[0] , qtimetamux, filtercaps);
  if (!ret) {
    g_printerr ("\n pipeline elements qtimlvdetection -> qtimetamux "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }
  gst_caps_unref (filtercaps);

  ret = gst_element_link_many (qtimetamux, tee[1], NULL);
  if (!ret) {
    g_printerr ("\n pipeline element qtimetamux -> tee "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[1], queue[2], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements tee -> qtivcomposer "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[1], qtivsplit, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements tee -> qtivsplit "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    ret = gst_element_link_many (qtivsplit, tee[i + 2],
        NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtivsplit -> tee "
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  // 3.2 Create links for all 4 splits
  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    ret = gst_element_link_many (tee[i + 2], queue[i + 3], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee -> qtivcomposer "
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    ret = gst_element_link_many (tee[i + 2],
      qtimlvconverter[i + 1], qtimlelement[i + 1],
      qtimlvclassification[i], classification_filter[i],
      qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtimlvconverter -> qtimlelement "
          " -> qtimlvclassification and  qtivcomposer cannot be linked. "
          "Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  ret = gst_element_link_many (qtivcomposer, fpsdisplaysink, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements qtivcomposer -> fpsdisplaysink "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  g_print ("All elements are linked successfully\n");

  if (options.source_type == GST_STREAM_TYPE_CAMERA) {
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
  } else if (options.source_type == GST_STREAM_TYPE_FILE) {
    // 3.3 Set pad to link dynamic video to queue
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), queue[0]);
  } else if (options.source_type == GST_STREAM_TYPE_RTSP) {
    // 3.3 Set pad to link dynamic video to queue
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), queue[0]);
  }

  // 3.4 Set src properties of qtivsplit for all splits
  for (gint i = 0; i < SPLIT_COUNT; i++) {
    GstPad *vsplit_src;
    GValue value = G_VALUE_INIT;
    g_value_init (&value, G_TYPE_INT);

    snprintf (element_name, 127, "src_%d", i);
    vsplit_src = gst_element_get_static_pad (qtivsplit, element_name);
    if (vsplit_src == NULL) {
      g_printerr ("src pad of qtivsplit couldn't be retrieved\n");
      goto error_clean_pipeline;
    }
    // Set split mode as single-roi-meta
    g_value_set_int (&value, SINGLE_ROI_META);
    g_object_set_property (G_OBJECT (vsplit_src), "mode", &value);

    g_value_unset (&value);
    gst_object_unref (vsplit_src);
  }

  for (gint i = 0; i < COMPOSER_SINK_COUNT; i++) {
    GstPad *vcomposer_sink;
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;

    snprintf (element_name, 127, "sink_%d", i);
    vcomposer_sink = gst_element_get_static_pad (qtivcomposer, element_name);
    if (vcomposer_sink == NULL) {
      g_printerr ("Sink pad %d of vcomposer couldn't be retrieved\n",i);
      goto error_clean_pipeline;
    }

    g_value_init (&position, GST_TYPE_ARRAY);
    g_value_init (&dimension, GST_TYPE_ARRAY);

    GstVideoRectangle pos = composer_sink_position[i];
    pos_vals[0] = pos.x; pos_vals[1] = pos.y;
    dim_vals[0] = pos.w; dim_vals[1] = pos.h;

    build_pad_property (&position, pos_vals, 2);
    build_pad_property (&dimension, dim_vals, 2);

    g_object_set_property (G_OBJECT (vcomposer_sink), "position", &position);
    g_object_set_property (G_OBJECT (vcomposer_sink), "dimensions", &dimension);

    g_value_unset (&position);
    g_value_unset (&dimension);
    gst_object_unref (vcomposer_sink);
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  g_printerr ("Pipeline elements cannot be linked\n");
  if (options.source_type == GST_STREAM_TYPE_CAMERA) {
    cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, &qmmfsrc_caps_preview, NULL);
  } else if (options.source_type == GST_STREAM_TYPE_FILE) {
    cleanup_gst (&filesrc, &qtdemux, &h264parse, &v4l2h264dec,
        &qtimetamux, NULL);
  } else if (options.source_type == GST_STREAM_TYPE_RTSP) {
    cleanup_gst (&rtspsrc, &rtph264depay, &h264parse, &v4l2h264dec,
        &qtimetamux, NULL);
  }

  cleanup_gst (&qtivsplit, &qtivcomposer, &fpsdisplaysink, NULL);

  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    if (classification_filter[i]) {
      gst_object_unref (classification_filter[i]);
    }
  }

  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    if (qtimlelement[i]) {
      gst_object_unref (qtimlelement[i]);
    }
  }

  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    if (qtimlvconverter[i]) {
      gst_object_unref (qtimlvconverter[i]);
    }
  }

  for (gint i = 0; i < DETECTION_COUNT; i++) {
    if (qtimlvdetection[i]) {
      gst_object_unref (qtimlvdetection[i]);
    }
  }

  for (gint i = 0; i < CLASSIFICATION_COUNT; i++) {
    if (qtimlvclassification[i]) {
      gst_object_unref (qtimlvclassification[i]);
    }
  }

  for (gint i = 0; i < TEE_COUNT; i++) {
    if (tee[i]) {
      gst_object_unref (tee[i]);
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    if (queue[i]) {
      gst_object_unref (queue[i]);
    }
  }

  return FALSE;
}

/**
 * Parse JSON file to read input parameters
 *
 * @param config_file Path to config file
 * @param options Application specific options
 */
gint
parse_json(gchar * config_file, GstAppOptions * options)
{
  JsonParser *parser = NULL;
  JsonArray *pipeline_info = NULL;
  JsonNode *root = NULL;
  JsonObject *root_obj = NULL;
  GError *error = NULL;

  parser = json_parser_new ();

  // Load the JSON file
  if (!json_parser_load_from_file (parser, config_file, &error)) {
    g_printerr ("Unable to parse JSON file: %s\n", error->message);
    g_error_free (error);
    g_object_unref (parser);
    return -1;
  }

  // Get the root object
  root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    gst_printerr ("Failed to load json object\n");
    g_object_unref (parser);
    return -1;
  }

  root_obj = json_node_get_object (root);

  if (json_object_has_member (root_obj, "input-file")) {
    options->file_path =
        g_strdup (json_object_get_string_member (root_obj, "input-file"));
  }

  if (json_object_has_member (root_obj, "rtsp-ip-port")) {
    options->rtsp_ip_port =
        g_strdup (json_object_get_string_member (root_obj, "rtsp-ip-port"));
  }

  gboolean camera_is_available = is_camera_available ();

  if (camera_is_available) {
    if ((!json_object_has_member (root_obj, "rtsp-ip-port")) &&
        (!json_object_has_member (root_obj, "input-file")))
      options->camera_source = TRUE;
  }

  if (json_object_has_member (root_obj, "detection-model")) {
    options->detection_model_path =
        g_strdup (json_object_get_string_member (root_obj, "detection-model"));
  }

  if (json_object_has_member (root_obj, "detection-labels")) {
    options->detection_labels_path =
        g_strdup (json_object_get_string_member (root_obj, "detection-labels"));
  }

  if (json_object_has_member (root_obj, "classification-model")) {
    options->classification_model_path =
        g_strdup (
            json_object_get_string_member (root_obj, "classification-model"));
  }

  if (json_object_has_member (root_obj, "classification-labels")) {
    options->classification_labels_path =
        g_strdup (
            json_object_get_string_member (root_obj, "classification-labels"));
  }

  if (json_object_has_member (root_obj, "detection-constants")) {
    options->detection_constants =
        g_strdup (
            json_object_get_string_member (root_obj, "detection-constants"));
  }

  g_object_unref (parser);
  return 0;
}

gint
main (gint argc, gchar * argv[])
{
  GstBus *bus = NULL;
  GMainLoop *mloop = NULL;
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *app_name = NULL;
  GstAppOptions options = {};
  GstAppContext appctx = {};
  GstStreamSourceType source_type;
  gboolean ret = FALSE;
  gchar help_description[2048];
  guint intrpt_watch_id = 0;
  gchar *config_file = NULL;
  GError *error = NULL;

  options.file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.camera_source = FALSE;
  options.detection_model_path = NULL;
  options.classification_model_path = NULL;
  options.detection_labels_path = NULL;
  options.classification_labels_path = NULL;
  options.detection_constants = NULL;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "config-file", 0, 0, G_OPTION_ARG_STRING,
      &config_file,
      "Path to config file\n",
      NULL
    },
    { NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  gboolean camera_is_available = is_camera_available ();

  gchar camera_description[255] = {};

  if (camera_is_available) {
    snprintf (camera_description, 255,
      "  If neither input-file nor rtsp-ip-port are provided, "
      "then camera input will be selected\n\n"
    );
  }

  snprintf (help_description, 2047, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates Daisy chain of "
      "Object Detection and Classification\n"
      "\nConfig file Fields:\n"

      "  input-file: \"/PATH\"\n"
      "      Input File path\n"
      "  rtsp-ip-port: \"rtsp://<ip>:<port>/<stream>\"\n"
      "      Use this parameter to provide the rtsp input.\n"
      "      Input should be provided as rtsp://<ip>:<port>/<stream>,\n"
      "      eg: rtsp://192.168.1.110:8554/live.mkv\n"
      "  %s"
      "  detection-model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path "
      "for YOLOV5 detection model\n"
      "      Default path for YOLOV5 model: "DEFAULT_TFLITE_YOLOV5_MODEL"\n"
      "  detection-labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path "
      " for YOLOV5 labels\n"
      "      Default path for YOLOV5 labels: "DEFAULT_YOLOV5_LABELS"\n"
      "  classification-model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path "
      "for classification model\n"
      "      Default path for Classification model: "
      DEFAULT_TFLITE_CLASSIFICATION_MODEL"\n"
      "  classification-labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path "
      " for classification labels\n"
      "      Default path for classification labels: "
      DEFAULT_CLASSIFICATION_LABELS"\n"
      "  detection-constants: \"CONSTANTS\"\n"
      "      Constants, offsets and coefficients for YOLOV5 TFLITE model \n"
      "      Default constants for YOLOV5: "YOLOV5_CONSTANT"\n",
      app_name, DEFAULT_CONFIG_FILE, camera_description);
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
      gst_app_context_free (&appctx, &options, config_file);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EFAULT;
  }

  if (config_file == NULL) {
    config_file = DEFAULT_CONFIG_FILE;
  }

  if (!file_exists (config_file)) {
    g_printerr ("Invalid config file path: %s\n", config_file);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (parse_json (config_file, &options) != 0) {
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  // Check for input source
  if (camera_is_available) {
    g_print ("TARGET can support file source, RTSP source and camera source\n");
  } else {
    g_print ("TARGET can only support file source and RTSP source.\n");
    if (options.file_path == NULL && options.rtsp_ip_port == NULL) {
      g_print ("User need to give proper input as source\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if ((options.camera_source && options.file_path) ||
      (options.camera_source && options.rtsp_ip_port) ||
      (options.file_path && options.rtsp_ip_port))
  {
    g_printerr ("Multiple sources are provided as input.\n"
        "Select only one input source\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  } else if (options.camera_source) {
    g_print ("Camera source is selected.\n");
    options.source_type = GST_STREAM_TYPE_CAMERA;
  } else if (options.file_path) {
    g_print ("File source is selected.\n");
    options.source_type = GST_STREAM_TYPE_FILE;
  } else if (options.rtsp_ip_port) {
    g_print ("RTSP source is selected.\n");
    options.source_type = GST_STREAM_TYPE_RTSP;
  } else {
  if (camera_is_available) {
      g_print ("No source is selected. "
          "Camera is set as Default\n");
      options.source_type = GST_STREAM_TYPE_CAMERA;
    } else {
      g_print ("User need to give proper input file as source\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if (options.source_type == GST_STREAM_TYPE_FILE) {
    if (!file_exists (options.file_path)) {
      g_printerr ("Invalid video file source path: %s\n", options.file_path);
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if(options.detection_model_path == NULL) {
    options.detection_model_path = DEFAULT_TFLITE_YOLOV5_MODEL;
  }
  if (!file_exists (options.detection_model_path)) {
    g_printerr ("Invalid detection model file path: %s\n",
        options.detection_model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if(options.classification_model_path == NULL) {
    options.classification_model_path = DEFAULT_TFLITE_CLASSIFICATION_MODEL;
  }
  if (!file_exists (options.classification_model_path)) {
    g_printerr ("Invalid classification model file path: %s\n",
        options.classification_model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if(options.detection_labels_path == NULL) {
    options.detection_labels_path = DEFAULT_YOLOV5_LABELS;
  }
  if (!file_exists (options.detection_labels_path)) {
    g_printerr ("Invalid detection labels file path: %s\n",
        options.detection_labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if(options.classification_labels_path == NULL) {
    options.classification_labels_path = DEFAULT_CLASSIFICATION_LABELS;
  }
  if (!file_exists (options.classification_labels_path)) {
    g_printerr ("Invalid classification labels file path: %s\n",
        options.classification_labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  g_print ("Running app with\n"
      "For Detection model: %s labels: %s\n"
      "For Classification model: %s labels: %s\n",
      options.detection_model_path, options.detection_labels_path,
      options.classification_model_path,
      options.classification_labels_path);

  if (options.detection_constants == NULL) {
    options.detection_constants = YOLOV5_CONSTANT;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx, options);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (&appctx, &options, config_file);
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
  gst_app_context_free (&appctx, &options, config_file);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
