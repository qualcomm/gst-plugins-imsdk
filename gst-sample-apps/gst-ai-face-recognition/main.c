/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Face Recognition on video stream.
 *
 * Description:
 * The application takes video stream from camera/rtsp and gives same to
 * Tflite/QNN Model for face detection and splits frame based on bounding box for
 * 3DMM and is further split for face recognition, displays preview with
 * overlayed AI Model output.
 *
 * Pipeline for Gstreamer Face recognition using camera source below:-
 *
 * source (camera) -> qmmfsrc_caps -> tee (SPLIT) ->
 *
 * Pipeline for Gstreamer Face detection using RTSP source below:-
 *
 * source (RTSP) -> rtph264depay -> h264parse -> v4l2h264dec -> tee (SPLIT) ->
 *
 * Pipeline after tee is common for all
 * sources (qtiqmmfsrc/rtspsrc) ->
 *
 *  | tee -> qtimetamux[0]
 *        -> Pre process-> qtimltflite -> qtimlvdetection -> qtimetamux[0]
 *  | qtimetamux[0] -> tee
 *  | tee -> qtimetamux[1]
 *        -> Pre process-> qtimltflite -> qtimlvpose -> qtimetamux[1]
 *  | qtimetamux[1] -> tee
 *  | tee -> qtimetamux[2]
 *        -> Pre process-> qtimltflite -> qtimlvclassification -> qtimetamux[2]
 *  | qtimetamux[2] -> waylandsink
 *
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimltflite/qtimlqnn
 *     Post process: qtimlvdetection/ qtimlvpose/ qtimlvclassification ->
 *     detection_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <stdarg.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_QNN_FACE_DETECTION_MODEL "/etc/models/face_det_lite_quantized.bin"
#define DEFAULT_QNN_FACE_LANDMARK_MODEL "/etc/models/facemap_3dmm_quantized.bin"
#define DEFAULT_QNN_FACE_RECOGNITION_MODEL "/etc/models/face_attrib_net_quantized.bin"
#define DEFAULT_TFLITE_FACE_DETECTION_MODEL "/etc/models/face_det_lite_quantized.tflite"
#define DEFAULT_TFLITE_FACE_LANDMARK_MODEL "/etc/models/facemap_3dmm_quantized.tflite"
#define DEFAULT_TFLITE_FACE_RECOGNITION_MODEL "/etc/models/face_attrib_net_quantized.tflite"
#define DEFAULT_FACE_DETECTION_LABELS "/etc/labels/face_detection.labels"
#define DEFAULT_FACE_LANDMARK_LABELS "/etc/labels/face_landmark.labels"
#define DEFAULT_FACE_RECOGNITION_LABELS "/etc/labels/face_recognition.labels"

/**
 * Default constants to dequantize values
 */
#define DEFAULT_FACE_DETECTION_MODEL_CONSTANTS \
    "DET,q-offsets=<178.0, 0.0, 102.0>,\
    q-scales=<0.03400895744562149, 0.21995200216770172, 0.1414264440536499>;"

/**
 * Default constants to dequantize values
 */
#define DEFAULT_FACE_LANDMARK_MODEL_CONSTANTS \
    "DMM,q-offsets=<211.0>,q-scales=<0.06002333015203476>;"

/**
 * Default constants to dequantize values
 */
#define DEFAULT_FACE_RECOGNITION_MODEL_CONSTANTS \
    "qfr,q-offsets=<124.0, 153.0, 125.0, 133.0, 126.0, 0.0>,\
    q-scales=<0.1948956549167633, 0.00791067536920309, 0.06736132502555847, 0.029019491747021675, 0.08928389847278595, 0.00390625>;"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define PRIMARY_CAMERA_PREVIEW_OUTPUT_WIDTH 1280
#define PRIMARY_CAMERA_PREVIEW_OUTPUT_HEIGHT 720
#define SECONDARY_CAMERA_PREVIEW_OUTPUT_WIDTH 1280
#define SECONDARY_CAMERA_PREVIEW_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Default path of config file
 */
#define DEFAULT_CONFIG_FILE "/etc/configs/config-face-recognition.json"

/**
 * Default value of Threshold for qtimlvdetection Plugin
 */
#define DEFAULT_DETECTION_THRESHOLD_VALUE 51.0

/**
 * Default value of Threshold for qtimlvpose plugin
 */
#define DEFAULT_POSE_THRESHOLD_VALUE 51.0

/**
 * Default value of Threshold for qtimlvclassification plugin
 */
#define DEFAULT_CLASSIFICATION_THRESHOLD_VALUE 60.0

/**
 * Maximum count of various sources possible to configure
 */
#define QUEUE_COUNT 21
#define TEE_COUNT 3
#define DETECTION_FILTER_COUNT 3
#define INFERENCE_ELEMENT_COUNT 3

/**
 * GstConversionMode:
 * @IMAGE_BATCH_NON_CUMULATIVE : ROI meta is ignored.
 *     Immediately process incoming buffers.
 * @IMAGE_BATCH_CUMULATIVE     : ROI meta is ignored.
 *     Accumulate buffers until there are enough image memory blocks
 * @ROI_BATCH_NON_CUMULATIVE   : Use only ROI metas
 *     Immediately process incoming buffers
 * @ROI_BATCH_CUMULATIVE       : Use only ROI metas
 *     Accumulate buffers until there are enough ROI metas
 *
 * Mode of Conversion.
 */
typedef enum
{
  IMAGE_BATCH_NON_CUMULATIVE,
  IMAGE_BATCH_CUMULATIVE,
  ROI_BATCH_NON_CUMULATIVE,
  ROI_BATCH_CUMULATIVE
} GstConversionMode;

/**
 * GstDaisyChainModelType:
 * @GST_FACE_DETECTION       : Face Detection Model.
 * @GST_FACE_LANDMARK        : Face Landmark Model.
 * @GST_FACE_RECOGNITION     : Face Recognition Model.
 *
 * Type of Usecase.
 */
typedef enum
{
  GST_FACE_DETECTION,
  GST_FACE_LANDMARK,
  GST_FACE_RECOGNITION
} GstDaisyChainModelType;

/**
 * Structure for various application specific options
 */
typedef struct
{
  gchar *rtsp_ip_port;
  gchar *face_detection_model_path;
  gchar *face_landmark_model_path;
  gchar *face_recognition_model_path;
  gchar *face_detection_labels_path;
  gchar *face_landmark_labels_path;
  gchar *face_recognition_labels_path;
  gchar *face_detection_model_constants;
  gchar *face_landmark_model_constants;
  gchar *face_recognition_model_constants;
  GstCameraSourceType camera_type;
  GstModelType model_type;
  gboolean use_rtsp;
  gboolean use_camera;
} GstAppOptions;

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstAppContext * appctx, GstAppOptions * options,
    gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->rtsp_ip_port != NULL) {
    g_free ((gpointer)options->rtsp_ip_port);
  }

  if (options->face_detection_model_path !=
      (gchar *) (&DEFAULT_QNN_FACE_DETECTION_MODEL)
      && options->face_detection_model_path != NULL) {
    g_free ((gpointer) options->face_detection_model_path);
  }

  if (options->face_landmark_model_path !=
      (gchar *) (&DEFAULT_QNN_FACE_LANDMARK_MODEL)
      && options->face_landmark_model_path != NULL) {
    g_free ((gpointer) options->face_landmark_model_path);
  }

  if (options->face_recognition_model_path !=
      (gchar *) (&DEFAULT_QNN_FACE_RECOGNITION_MODEL)
      && options->face_recognition_model_path != NULL) {
    g_free ((gpointer) options->face_recognition_model_path);
  }

  if (options->face_detection_labels_path !=
      (gchar *) (&DEFAULT_FACE_DETECTION_LABELS)
      && options->face_detection_labels_path != NULL) {
    g_free ((gpointer) options->face_detection_labels_path);
  }

  if (options->face_landmark_labels_path !=
      (gchar *) (&DEFAULT_FACE_LANDMARK_LABELS)
      && options->face_landmark_labels_path != NULL) {
    g_free ((gpointer) options->face_landmark_labels_path);
  }

  if (options->face_recognition_labels_path !=
      (gchar *) (&DEFAULT_FACE_RECOGNITION_LABELS)
      && options->face_recognition_labels_path != NULL) {
    g_free ((gpointer) options->face_recognition_labels_path);
  }

  if (options->face_detection_model_constants !=
      (gchar *) (&DEFAULT_FACE_DETECTION_MODEL_CONSTANTS)
      && options->face_detection_model_constants != NULL) {
    g_free ((gpointer) options->face_detection_model_constants);
  }

  if (options->face_landmark_model_constants !=
      (gchar *) (&DEFAULT_FACE_LANDMARK_MODEL_CONSTANTS)
      && options->face_landmark_model_constants != NULL) {
    g_free ((gpointer) options->face_landmark_model_constants);
  }

  if (options->face_recognition_model_constants !=
      (gchar *) (&DEFAULT_FACE_RECOGNITION_MODEL_CONSTANTS)
      && options->face_recognition_model_constants != NULL) {
    g_free ((gpointer) options->face_recognition_model_constants);
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
  if (!ret) {
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
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *tee[TEE_COUNT] = { NULL };
  GstElement *qtimlvconverter[INFERENCE_ELEMENT_COUNT] = { NULL };
  GstElement *queue[QUEUE_COUNT] = { NULL };
  GstElement *qtimlelement[INFERENCE_ELEMENT_COUNT] = { NULL };
  GstElement *qtimetamux[INFERENCE_ELEMENT_COUNT] = { NULL };
  GstElement *qtimlvdetection = NULL, *qtimlvpose = NULL;
  GstElement *qtimlvclassification = NULL;
  GstElement *detection_filter[DETECTION_FILTER_COUNT] = { NULL };
  GstElement *rtspsrc = NULL, *rtph264depay = NULL, *qtivoverlay = NULL;
  GstElement *v4l2h264dec_caps = NULL;
  GstElement *h264parse = NULL, *v4l2h264dec = NULL, *waylandsink = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint primary_camera_preview_width = PRIMARY_CAMERA_PREVIEW_OUTPUT_WIDTH;
  gint primary_camera_preview_height = PRIMARY_CAMERA_PREVIEW_OUTPUT_HEIGHT;
  gint secondary_camera_preview_width = SECONDARY_CAMERA_PREVIEW_OUTPUT_WIDTH;
  gint secondary_camera_preview_height = SECONDARY_CAMERA_PREVIEW_OUTPUT_HEIGHT;
  gint camera_framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;
  GValue value = G_VALUE_INIT;

  // 1. Create the elements or Plugins
  if (options->use_rtsp) {
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

    v4l2h264dec_caps =
        gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
      goto error_clean_elements;
    }
  } else if (options->use_camera) {
    // Create qtiqmmfsrc plugin for camera stream
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
    if (!qtiqmmfsrc) {
      g_printerr ("Failed to create qtiqmmfsrc\n");
      goto error_clean_elements;
    }
    // Use capsfilter to define the preview stream camera output settings
    qmmfsrc_caps = gst_element_factory_make ("capsfilter", "qmmfsrc_caps");
    if (!qmmfsrc_caps) {
      g_printerr ("Failed to create qmmfsrc_caps\n");
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

  // Use tee to send same data buffer
  // one for AI inferencing, one for Display composition
  for (gint i = 0; i < TEE_COUNT; i++) {
    snprintf (element_name, 127, "tee-%d", i);
    tee[i] = gst_element_factory_make ("tee", element_name);
    if (!tee[i]) {
      g_printerr ("Failed to create tee %d\n", i);
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < INFERENCE_ELEMENT_COUNT; i++) {
    // Create qtimlvconverter for Input preprocessing
    snprintf (element_name, 127, "qtimlvconverter-%d", i);
    qtimlvconverter[i] = gst_element_factory_make ("qtimlvconverter",
        element_name);
    if (!qtimlvconverter[i]) {
      g_printerr ("Failed to create qtimlvconverter %d\n", i);
      goto error_clean_elements;
    }
    // Create the ML inferencing plugin TFLITE/QNN
    if (options->model_type == GST_MODEL_TYPE_TFLITE) {
      snprintf (element_name, 127, "qtimltflite-%d", i);
      qtimlelement[i] = gst_element_factory_make ("qtimltflite", element_name);
    } else if (options->model_type == GST_MODEL_TYPE_QNN) {
      snprintf (element_name, 127, "qtimlqnn-%d", i);
      qtimlelement[i] = gst_element_factory_make ("qtimlqnn", element_name);
    } else {
      g_printerr ("Invalid Model Type\n");
      goto error_clean_elements;
    }
    if (!qtimlelement[i]) {
      g_printerr ("Failed to create qtimlelement\n");
      goto error_clean_elements;
    }
    // To associate/attach ML string based postprocessing results
    snprintf (element_name, 127, "qtimetamux-%d", i);
    qtimetamux[i] = gst_element_factory_make ("qtimetamux", element_name);
    if (!qtimetamux[i]) {
      g_printerr ("Failed to create qtimetamux %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create plugin for ML postprocessing for Detection
  qtimlvdetection = gst_element_factory_make ("qtimlvdetection",
      "qtimlvdetection");
  if (!qtimlvdetection) {
    g_printerr ("Failed to create qtimlvdetection \n");
    goto error_clean_elements;
  }
  // Create plugin for ML postprocessing for Pose estimation
  qtimlvpose = gst_element_factory_make ("qtimlvpose", "qtimlvpose");
  if (!qtimlvpose) {
    g_printerr ("Failed to create qtimlvpose \n");
    goto error_clean_elements;
  }
  // Create plugin for ML postprocessing for Classification
  qtimlvclassification = gst_element_factory_make ("qtimlvclassification",
      "qtimlvclassification");
  if (!qtimlvclassification) {
    g_printerr ("Failed to create qtimlvclassification \n");
    goto error_clean_elements;
  }
  // Used to negotiate between ML post proc o/p and qtimetamux
  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    snprintf (element_name, 127, "detection_filter-%d", i);
    detection_filter[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!detection_filter[i]) {
      g_printerr ("Failed to create detection_filter %d\n", i);
      goto error_clean_elements;
    }
  }
  // Hardware accelerated in-place image draw plug-in for drawing
  // overlays on top of images.
  qtivoverlay = gst_element_factory_make ("qtivoverlay", "qtivoverlay");
  if (!qtivoverlay) {
    g_printerr ("Failed to create qtivoverlay\n");
    goto error_clean_elements;
  }
  // Create Wayland compositor to render preview output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink\n");
    goto error_clean_elements;
  }
  // 2. Set properties for all GST plugin elements
  if (options->use_rtsp) {
    // 2.1 Set the capabilities of RTSP stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (rtspsrc), "location", options->rtsp_ip_port, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->use_camera) {
    // 2.2 Set user provided Camera ID
    g_object_set (G_OBJECT (qtiqmmfsrc), "camera", options->camera_type, NULL);
    // 2.4 Set the capabilities of camera plugin output
    if (options->camera_type == GST_CAMERA_TYPE_PRIMARY) {
      // 2.3 Set the capabilities of primary and secondary camera preview
      // stream camera plugin output
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, primary_camera_preview_width,
          "height", G_TYPE_INT, primary_camera_preview_height,
          "framerate", GST_TYPE_FRACTION, camera_framerate, 1, NULL);
      g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    } else {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, secondary_camera_preview_width,
          "height", G_TYPE_INT, secondary_camera_preview_height,
          "framerate", GST_TYPE_FRACTION, camera_framerate, 1, NULL);
      g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    }
  } else {
    g_printerr ("Invalid Source Type\n");
    goto error_clean_elements;
  }

  // 2.4 Set the properties of qtimlvconverter of pose plugin- mode
  // and image-disposition
  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, IMAGE_BATCH_NON_CUMULATIVE);
  g_object_set_property (G_OBJECT (qtimlvconverter[GST_FACE_DETECTION]),
      "mode", &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, ROI_BATCH_CUMULATIVE);
  g_object_set_property (G_OBJECT (qtimlvconverter[GST_FACE_LANDMARK]),
      "mode", &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, ROI_BATCH_CUMULATIVE);
  g_object_set_property (G_OBJECT (qtimlvconverter[GST_FACE_RECOGNITION]),
      "mode", &value);
  g_value_unset (&value);

  // 2.5 set the property of qtimlvtetection
  module_id = get_enum_value (qtimlvdetection, "module", "qfd");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvdetection),
        "threshold", DEFAULT_DETECTION_THRESHOLD_VALUE, "results", 6,
        "stabilization", FALSE, "module", module_id, "labels",
        options->face_detection_labels_path, "constants",
        options->face_detection_model_constants, NULL);
  } else {
    g_printerr ("Module qfd is not available in qtimlvdetection.\n");
    goto error_clean_elements;
  }

  module_id = get_enum_value (qtimlvpose, "module", "lite-3dmm");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvpose),
        "threshold", DEFAULT_POSE_THRESHOLD_VALUE, "results", 6,
        "module", module_id, "labels", options->face_landmark_labels_path,
        "constants", options->face_landmark_model_constants, NULL);
  } else {
    g_printerr ("Module lite-3dmm is not available in qtimlvpose.\n");
    goto error_clean_elements;
  }

  module_id = get_enum_value (qtimlvclassification, "module", "qfr");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvclassification),
        "threshold", DEFAULT_CLASSIFICATION_THRESHOLD_VALUE, "results", 6,
        "module", module_id, "labels", options->face_recognition_labels_path,
        "constants", options->face_recognition_model_constants, NULL);
  } else {
    g_printerr ("Module qfr is not available in qtimlvclassification.\n");
    goto error_clean_elements;
  }

  // 2.6 set the property of qtimlelement
  g_object_set (G_OBJECT (qtimlelement[GST_FACE_DETECTION]), "model",
      options->face_detection_model_path, NULL);
  g_object_set (G_OBJECT (qtimlelement[GST_FACE_LANDMARK]), "model",
      options->face_landmark_model_path, NULL);
  g_object_set (G_OBJECT (qtimlelement[GST_FACE_RECOGNITION]), "model",
      options->face_recognition_model_path, NULL);

  for (gint i = 0; i < INFERENCE_ELEMENT_COUNT; i++) {
    if (options->model_type == GST_MODEL_TYPE_QNN) {
      g_object_set (G_OBJECT (qtimlelement[i]), "backend",
          "/usr/lib/libQnnHtp.so", NULL);
    } else {
      g_print ("Using DSP Delegate\n");
      delegate_options =
          gst_structure_from_string ("QNNExternalDelegate,backend_type=htp;",
          NULL);
      g_object_set (G_OBJECT (qtimlelement[i]), "delegate",
          GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
      g_object_set (G_OBJECT (qtimlelement[i]), "external-delegate-path",
          "libQnnTFLiteDelegate.so", NULL);
      g_object_set (G_OBJECT (qtimlelement[i]), "external-delegate-options",
          delegate_options, NULL);
      gst_structure_free (delegate_options);
    }
  }

  // 2.7 Set the properties of waylandsink
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // 2.8 Set the caps filter for detection_filter
  pad_filter = gst_caps_new_simple ("text/x-raw", NULL, NULL);
  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    g_object_set (G_OBJECT (detection_filter[i]), "caps", pad_filter, NULL);
  }
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  // 3.1 Adding elements to pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options->use_rtsp) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc, rtph264depay,
        h264parse, v4l2h264dec, v4l2h264dec_caps, NULL);
  } else if (options->use_camera) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
        qmmfsrc_caps, NULL);
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvdetection,
      qtimlvclassification, qtimlvpose, qtivoverlay, waylandsink, NULL);

  for (gint i = 0; i < INFERENCE_ELEMENT_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter[i],
        qtimlelement[i], qtimetamux[i], NULL);
  }

  for (gint i = 0; i < TEE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), tee[i], NULL);
  }

  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), detection_filter[i], NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // 3.2 Create Pipeline for Face recognition
  if (options->use_rtsp) {
    // Linking RTSP source Stream
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee[GST_FACE_DETECTION], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->tee_face_detection\n");
      goto error_clean_pipeline;
    }

  } else {
    // Linking Camera Stream
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[0],
        tee[GST_FACE_DETECTION], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for preview Stream, from"
          "qtiqmmfsrc->tee_face_detection\n");
      goto error_clean_pipeline;
    }
  }

  ret = gst_element_link_many (tee[GST_FACE_DETECTION], queue[2],
      qtimetamux[GST_FACE_DETECTION], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "tee_face_detection->qtimetamux_face_detection\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[GST_FACE_DETECTION], queue[3],
      qtimlvconverter[GST_FACE_DETECTION], queue[4],
      qtimlelement[GST_FACE_DETECTION], queue[5], qtimlvdetection,
      detection_filter[GST_FACE_DETECTION], queue[6],
      qtimetamux[GST_FACE_DETECTION], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "tee_face_detection->face_detection_inference->"
        "qtimetamux_face_detection\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (qtimetamux[GST_FACE_DETECTION],
      queue[7], tee[GST_FACE_LANDMARK], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "qtimetamux_face_detection->tee_face_mm\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[GST_FACE_LANDMARK], queue[8],
      qtimetamux[GST_FACE_LANDMARK], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "tee_face_mm->qtimetamux_face_mm\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[GST_FACE_LANDMARK], queue[9],
      qtimlvconverter[GST_FACE_LANDMARK], queue[10],
      qtimlelement[GST_FACE_LANDMARK], queue[11], qtimlvpose,
      detection_filter[GST_FACE_LANDMARK], queue[12],
      qtimetamux[GST_FACE_LANDMARK], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "tee_face_mm->face_mm_inference->qtimetamux_face_mm\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (qtimetamux[GST_FACE_LANDMARK], queue[13],
      tee[GST_FACE_RECOGNITION], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "qtimetamux_face_mm->tee_face_recognition\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[GST_FACE_RECOGNITION], queue[14],
      qtimetamux[GST_FACE_RECOGNITION], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "tee_face_recognition->qtimetamux_face_recognition\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[GST_FACE_RECOGNITION], queue[15],
      qtimlvconverter[GST_FACE_RECOGNITION], queue[16],
      qtimlelement[GST_FACE_RECOGNITION], queue[17], qtimlvclassification,
      detection_filter[GST_FACE_RECOGNITION], queue[18],
      qtimetamux[GST_FACE_RECOGNITION], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "tee_face_recognition->face_recognition_inference->"
        "qtimetamux_face_recognition\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (qtimetamux[GST_FACE_RECOGNITION],
      queue[19], qtivoverlay, queue[20], waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked from"
        "qtimetamux_face_recognition->waylandsink\n");
    goto error_clean_pipeline;
  }

  if (options->use_rtsp) {
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, &qtimlvdetection, &qtimlvpose,
      &qtimlvclassification, &rtspsrc, &rtph264depay, &h264parse,
      &v4l2h264dec, &v4l2h264dec_caps, &qtivoverlay, &waylandsink, NULL);

  for (gint i = 0; i < TEE_COUNT; i++) {
    if (tee[i]) {
      gst_object_unref (tee[i]);
    }
  }

  for (gint i = 0; i < INFERENCE_ELEMENT_COUNT; i++) {
    if (qtimlvconverter[i]) {
      gst_object_unref (qtimlvconverter[i]);
    }

    if (qtimlelement[i]) {
      gst_object_unref (qtimlelement[i]);
    }

    if (qtimetamux[i]) {
      gst_object_unref (qtimetamux[i]);
    }
  }

  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    if (detection_filter[i]) {
      gst_object_unref (detection_filter[i]);
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
parse_json (gchar * config_file, GstAppOptions * options)
{
  JsonParser *parser = NULL;
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

  gboolean camera_is_available = is_camera_available ();

  if (camera_is_available) {
    if (json_object_has_member (root_obj, "camera"))
      options->camera_type = json_object_get_int_member (root_obj, "camera");
  }

  if (json_object_has_member (root_obj, "rtsp-ip-port")) {
    options->rtsp_ip_port =
        g_strdup (json_object_get_string_member (root_obj, "rtsp-ip-port"));
  }

  if (json_object_has_member (root_obj, "ml-framework")) {
    const gchar *framework =
        json_object_get_string_member (root_obj, "ml-framework");
    if (g_strcmp0 (framework, "tflite") == 0)
      options->model_type = GST_MODEL_TYPE_TFLITE;
    else if (g_strcmp0 (framework, "qnn") == 0)
      options->model_type = GST_MODEL_TYPE_QNN;
    else {
      gst_printerr ("ml-framework can only be one of "
          "\"tflite\" or \"qnn\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "face-detection-model")) {
    options->face_detection_model_path =
        g_strdup (json_object_get_string_member (root_obj,
            "face-detection-model"));
  }

  if (json_object_has_member (root_obj, "face-landmark-model")) {
    options->face_landmark_model_path =
        g_strdup (json_object_get_string_member (root_obj,
            "face-landmark-model"));
  }

  if (json_object_has_member (root_obj, "face-recognition-model")) {
    options->face_recognition_model_path =
        g_strdup (json_object_get_string_member (root_obj,
            "face-recognition-model"));
  }

  if (json_object_has_member (root_obj, "face-detection-labels")) {
    options->face_detection_labels_path =
        g_strdup (json_object_get_string_member (root_obj,
            "face-detection-labels"));
  }

  if (json_object_has_member (root_obj, "face-landmark-labels")) {
    options->face_landmark_labels_path =
        g_strdup (json_object_get_string_member (root_obj,
            "face-landmark-labels"));
  }

  if (json_object_has_member (root_obj, "face-recognition-labels")) {
    options->face_recognition_labels_path =
        g_strdup (json_object_get_string_member (root_obj,
            "face-recognition-labels"));
  }

  if (json_object_has_member (root_obj, "face-detection-constants")) {
    options->face_detection_model_constants =
        g_strdup (json_object_get_string_member (root_obj,
            "face-detection-constants"));
  }

  if (json_object_has_member (root_obj, "face-landmark-constants")) {
    options->face_landmark_model_constants =
        g_strdup (json_object_get_string_member (root_obj,
            "face-landmark-constants"));
  }

  if (json_object_has_member (root_obj, "face-recognition-constants")) {
    options->face_recognition_model_constants =
        g_strdup (json_object_get_string_member (root_obj,
            "face-recognition-constants"));
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
  GstAppContext appctx = { };
  gboolean ret = FALSE;
  gchar help_description[8192];
  guint intrpt_watch_id = 0;
  GstAppOptions options = { };
  gchar *config_file = NULL;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Set default value
  options.rtsp_ip_port = NULL;
  options.face_detection_model_path = NULL;
  options.face_landmark_model_path = NULL;
  options.face_recognition_model_path = NULL;
  options.face_detection_labels_path = DEFAULT_FACE_DETECTION_LABELS;
  options.face_landmark_labels_path = DEFAULT_FACE_LANDMARK_LABELS;
  options.face_recognition_labels_path = DEFAULT_FACE_RECOGNITION_LABELS;
  options.face_detection_model_constants =
      DEFAULT_FACE_DETECTION_MODEL_CONSTANTS;
  options.face_landmark_model_constants = DEFAULT_FACE_LANDMARK_MODEL_CONSTANTS;
  options.face_recognition_model_constants =
      DEFAULT_FACE_RECOGNITION_MODEL_CONSTANTS;
  options.use_rtsp = FALSE, options.use_camera = FALSE;
  options.model_type = GST_MODEL_TYPE_TFLITE;
  options.camera_type = GST_CAMERA_TYPE_NONE;

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

  gchar camera_description[255] = { };

  if (camera_is_available) {
    snprintf (camera_description, sizeof (camera_description),
        "  camera: 0 or 1\n"
        "      Select (0) for Primary Camera and (1) for secondary one.\n");
  }

  snprintf (help_description, 8191, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates Face Recognition on Live Stream\n"
      "\nConfig file Fields:\n"
      "  %s"
      "  rtsp-ip-port: \"rtsp://<ip>:<port>/<stream>\"\n"
      "      Use this parameter to provide the rtsp input.\n"
      "      Input should be provided as rtsp://<ip>:<port>/<stream>,\n"
      "      eg: rtsp://192.168.1.110:8554/live.mkv\n"
      "  ml-framework: \"tflite\" or \"qnn\"\n"
      "      Execute Model in TFlite [Default] or QNN format\n"
      "  Tflite Face detection model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for Face detection TFLITE Model: "
      DEFAULT_TFLITE_FACE_DETECTION_MODEL "\n"
      "  Tflite Face landmark model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for Face landmark TFLITE Model: "
      DEFAULT_TFLITE_FACE_LANDMARK_MODEL "\n"
      "  Tflite Face recognition model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for Face landmark TFLITE Model: "
      DEFAULT_TFLITE_FACE_RECOGNITION_MODEL "\n"
      "  QNN Face detection model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for Face detection QNN Model: "
      DEFAULT_QNN_FACE_DETECTION_MODEL "\n"
      "  QNN Face landmark model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for Face landmark QNN Model: "
      DEFAULT_QNN_FACE_LANDMARK_MODEL "\n"
      "  QNN Face recognition model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for Face recognition QNN Model: "
      DEFAULT_QNN_FACE_RECOGNITION_MODEL "\n"
      "  Face detection labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default Face detection labels path: " DEFAULT_FACE_DETECTION_LABELS
      "\n" "  Face landmark labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default Face landmark labels path: " DEFAULT_FACE_LANDMARK_LABELS
      "\n" "  Face recognition labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default Face recognition labels path: "
      DEFAULT_FACE_RECOGNITION_LABELS "\n"
      "  Face detection constants: \"CONSTANTS\"\n"
      "      Constants, offsets and coefficients used by the chosen module\n"
      "      for post-processing of incoming tensors.\n"
      "      Applicable only for some modules.\n"
      "      Default Face detection constants: "
      DEFAULT_FACE_DETECTION_MODEL_CONSTANTS "\n"
      "  Face landmark constants: \"CONSTANTS\"\n"
      "      Constants, offsets and coefficients used by the chosen module\n"
      "      for post-processing of incoming tensors.\n"
      "      Applicable only for some modules.\n"
      "      Default Face landmark constants: "
      DEFAULT_FACE_LANDMARK_MODEL_CONSTANTS "\n"
      "  Face recognition constants: \"CONSTANTS\"\n"
      "      Constants, offsets and coefficients used by the chosen module\n"
      "      for post-processing of incoming tensors.\n"
      "      Applicable only for some modules.\n"
      "      Default Face recognition constants: "
      DEFAULT_FACE_RECOGNITION_MODEL_CONSTANTS "\n", app_name,
      DEFAULT_CONFIG_FILE, camera_description);
  help_description[8191] = '\0';

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
    g_print ("TARGET Can support file source, RTSP source and camera source\n");
  } else {
    g_print ("TARGET Can only support file source and RTSP source.\n");
    if (options.rtsp_ip_port == NULL) {
      g_print ("User need to give RTSP as source\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if (options.rtsp_ip_port != NULL) {
    options.use_rtsp = TRUE;
  }
  // Use camera by default if user does not set anything
  if (!((options.camera_type != GST_CAMERA_TYPE_NONE) || options.use_rtsp)) {
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
        GST_CAMERA_TYPE_PRIMARY, GST_CAMERA_TYPE_SECONDARY);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }
  // Enable camera flag if user set the camera property
  if (options.camera_type == GST_CAMERA_TYPE_SECONDARY ||
      options.camera_type == GST_CAMERA_TYPE_PRIMARY)
    options.use_camera = TRUE;

  // Terminate if more than one source are there.
  if (options.use_camera + options.use_rtsp > 1) {
    g_printerr ("Select anyone source type either Camera or File or RTSP\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_rtsp) {
    g_print ("RTSP Source is Selected\n");
  } else {
    g_print ("Camera Source is Selected\n");
  }

  if (options.model_type < GST_MODEL_TYPE_TFLITE ||
      options.model_type > GST_MODEL_TYPE_QNN) {
    g_printerr ("Invalid ml-framework option selected\n"
        "Available options:\n"
        "    TFLite: %d\n"
        "    QNN: %d\n", GST_MODEL_TYPE_TFLITE, GST_MODEL_TYPE_QNN);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }
  // Set model path for execution
  if (options.face_detection_model_path == NULL) {
    if (options.model_type == GST_MODEL_TYPE_QNN) {
      options.face_detection_model_path = DEFAULT_QNN_FACE_DETECTION_MODEL;
    } else {
      options.face_detection_model_path = DEFAULT_TFLITE_FACE_DETECTION_MODEL;
    }
  }

  if (options.face_landmark_model_path == NULL) {
    if (options.model_type == GST_MODEL_TYPE_QNN) {
      options.face_landmark_model_path = DEFAULT_QNN_FACE_LANDMARK_MODEL;
    } else {
      options.face_landmark_model_path = DEFAULT_TFLITE_FACE_LANDMARK_MODEL;
    }
  }

  if (options.face_recognition_model_path == NULL) {
    if (options.model_type == GST_MODEL_TYPE_QNN) {
      options.face_recognition_model_path = DEFAULT_QNN_FACE_RECOGNITION_MODEL;
    } else {
      options.face_recognition_model_path =
          DEFAULT_TFLITE_FACE_RECOGNITION_MODEL;
    }
  }

  if (!file_exists (options.face_detection_model_path)) {
    g_print ("Invalid model file path: %s\n",
        options.face_detection_model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.face_landmark_model_path)) {
    g_print ("Invalid model file path: %s\n", options.face_landmark_model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.face_recognition_model_path)) {
    g_print ("Invalid model file path: %s\n",
        options.face_recognition_model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.face_detection_labels_path)) {
    g_print ("Invalid labels file path: %s\n",
        options.face_detection_labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.face_landmark_labels_path)) {
    g_print ("Invalid labels file path: %s\n",
        options.face_landmark_labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.face_recognition_labels_path)) {
    g_print ("Invalid labels file path: %s\n",
        options.face_recognition_labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  g_print ("Running app with Face detection model: %s and labels: %s\n",
      options.face_detection_model_path, options.face_detection_labels_path);

  g_print ("Running app with Face landmark model: %s and labels: %s\n",
      options.face_landmark_model_path, options.face_landmark_labels_path);

  g_print ("Running app with Face recognition model: %s and labels: %s\n",
      options.face_recognition_model_path,
      options.face_recognition_labels_path);

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
  ret = create_pipe (&appctx, &options);
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

  g_print ("Destroy pipeline\n");
  gst_app_context_free (&appctx, &options, config_file);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
