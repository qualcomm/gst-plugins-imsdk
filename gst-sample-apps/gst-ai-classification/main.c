/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Classification on Live stream.
 *
 * Description:
 * The application takes live video stream from camera/file/rtsp and gives same to
 * Classification TensorFlow Lite or SNPE DLC Model or QNN Model for classifying
 * objects and display preview with overlayed AI Model output/classification
 * labels.
 *
 * Pipeline for Gstreamer with Camera:
 * qtiqmmfsrc  -> | qmmfsrc_caps (Preview)    -> qtivcomposer
 *                | qmmfsrc_caps (Inference)  -> Pre-process -> Inference
 *                  ->  Post-process          -> qtivcomposer
 *
 * Pipeline for Gstreamer with File source:
 * filesrc -> qtdemux -> h264parse -> v4l2h264dec -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre-process -> Inference -> Post-process -> qtivcomposer
 *
 * Pipeline for Gstreamer with RTSP source:
 * rtspsrc -> rtph264depay -> h264parse -> v4l2h264dec -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre-process -> Inference -> Post-process -> qtivcomposer
 *
 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite/qtimlqnn
 *     Post process: qtimlvclassification -> classification_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_SNPE_CLASSIFICATION_MODEL "/etc/models/inceptionv3.dlc"
#define DEFAULT_TFLITE_CLASSIFICATION_MODEL \
    "/etc/models/inception_v3_quantized.tflite"
#define DEFAULT_QNN_CLASSIFICATION_MODEL "/etc/models/inception_v3_quantized.bin"
#define DEFAULT_CLASSIFICATION_LABELS "/etc/labels/classification.labels"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_INFERENCE_WIDTH 640
#define DEFAULT_INFERENCE_HEIGHT 360
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1920
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 1080
#define SECONDARY_CAMERA_OUTPUT_WIDTH 1280
#define SECONDARY_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Default path of config file
 */
#define DEFAULT_CONFIG_FILE "/etc/configs/config_classification.json"

/**
 * To enable softmax operation for post processing
 */
#define GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX 1

/**
 * Default constants to dequantize values
 */
#define DEFAULT_CONSTANTS \
    "Mobilenet,q-offsets=<38.0>,q-scales=<0.15008972585201263>;"

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 8

/**
 * Default value of Threshold
 */
#define DEFAULT_THRESHOLD_VALUE 40.0

/**
 * Default value of delegate
 */
#define DEFAULT_SNPE_DELEGATE GST_ML_SNPE_DELEGATE_DSP

/**
 * Structure for various application specific options
 */
typedef struct {
  gchar *file_path;
  gchar *rtsp_ip_port;
  gchar *model_path;
  gchar *labels_path;
  gchar *constants;
  GstCameraSourceType camera_type;
  GstModelType model_type;
  gdouble threshold;
  gint delegate_type;
  gboolean use_cpu;
  gboolean use_gpu;
  gboolean use_dsp;
  gboolean use_file;
  gboolean use_rtsp;
  gboolean use_camera;
} GstAppOptions;

/**
 * Static grid points to display multiple input stream
*/
static GstVideoRectangle position_data[2] = {
  {0, 0, 1920, 1080},
  {30, 30, 480, 270}
};

/**
 * Free Application context:
 *
 * @param appctx Application Context object.
 * @param options Application specific options.
 * @param config_file Path to config file
 */
static void
gst_app_context_free (GstAppContext * appctx, GstAppOptions * options, gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->file_path != NULL) {
    g_free ((gpointer)options->file_path);
  }

  if (options->rtsp_ip_port != NULL) {
    g_free ((gpointer)options->rtsp_ip_port);
  }

  if (options->model_path != (gchar *)(&DEFAULT_SNPE_CLASSIFICATION_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_TFLITE_CLASSIFICATION_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_QNN_CLASSIFICATION_MODEL) &&
      options->model_path != NULL) {
    g_free ((gpointer)options->model_path);
  }

  if (options->labels_path != (gchar *)(&DEFAULT_CLASSIFICATION_LABELS) &&
      options->labels_path != NULL) {
    g_free ((gpointer)options->labels_path);
  }

  if (options->constants != (gchar *)(&DEFAULT_CONSTANTS) &&
      options->constants != NULL) {
    g_free ((gpointer)options->constants);
  }

  if (config_file != NULL &&
      config_file != (gchar *)(&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer)config_file);
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
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *qmmfsrc_caps_preview = NULL;
  GstElement *queue[QUEUE_COUNT], *tee = NULL;
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvclassification = NULL, *classification_filter = NULL;
  GstElement *qtivcomposer = NULL, *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse = NULL;
  GstElement *v4l2h264dec = NULL, *rtspsrc = NULL, *rtph264depay = NULL;
  GstElement *v4l2h264dec_caps = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstPad *vcomposer_sink[2];
  GstStructure *delegate_options = NULL;
  GstPad *qtiqmmfsrc_type = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint pos_vals[2], dim_vals[2];
  gint primary_camera_width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint primary_camera_height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint secondary_camera_width = SECONDARY_CAMERA_OUTPUT_WIDTH;
  gint secondary_camera_height = SECONDARY_CAMERA_OUTPUT_HEIGHT;
  gint inference_width = DEFAULT_INFERENCE_WIDTH;
  gint inference_height = DEFAULT_INFERENCE_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;
  GValue video_type = G_VALUE_INIT;

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }

  // 1. Create the elements or Plugins
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

    v4l2h264dec_caps = gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
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

    v4l2h264dec_caps = gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
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

    // Use capsfilter to define the camera output settings
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
  } else {
    g_printerr ("Invalid source type\n");
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

  // Use tee to send same data buffer for file and rtsp use cases
  // one for AI inferencing, one for Display composition
  if (options->use_rtsp || options->use_file){
    tee = gst_element_factory_make ("tee", "tee");
    if (!tee) {
      g_printerr ("Failed to create tee\n");
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
  } else if (options->model_type == GST_MODEL_TYPE_TFLITE) {
    qtimlelement = gst_element_factory_make ("qtimltflite", "qtimltflite");
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    qtimlelement = gst_element_factory_make ("qtimlqnn", "qtimlqnn");
  } else {
    g_printerr ("Invalid Model Type\n");
    goto error_clean_elements;
  }
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    goto error_clean_elements;
  }

  // Create plugin for ML postprocessing for classification
  qtimlvclassification = gst_element_factory_make ("qtimlvclassification",
      "qtimlvclassification");
  if (!qtimlvclassification) {
    g_printerr ("Failed to create qtimlvclassification\n");
    goto error_clean_elements;
  }

  // Composer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }

  // Used to negotiate between ML post proc o/p and qtivcomposer
  classification_filter = gst_element_factory_make ("capsfilter",
      "classification_filter");
  if (!classification_filter) {
    g_printerr ("Failed to create classification_filter\n");
    goto error_clean_elements;
  }

  // Create Wayland compositor to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink \n");
    goto error_clean_elements;
  }

  // Create fpsdisplaysink to display the current and
  // average framerate as a text overlay
  fpsdisplaysink = gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
  if (!fpsdisplaysink) {
    g_printerr ("Failed to create fpsdisplaysink\n");
    goto error_clean_elements;
  }

  // 2. Set properties for all GST plugin elements
  if (options->use_file) {
    // 2.1 Set the capabilities of file stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (filesrc), "location", options->file_path, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

  } else if (options->use_rtsp) {
    // 2.2 Set the capabilities of RTSP stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (rtspsrc), "location", options->rtsp_ip_port, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

  } else if (options->use_camera) {
    // 2.3 Set user provided Camera ID
    g_object_set (G_OBJECT (qtiqmmfsrc), "camera",options->camera_type, NULL);

    // 2.4 Set the capabilities of camera plugin output
    if (options->camera_type == GST_CAMERA_TYPE_PRIMARY) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12_Q08C",
          "width", G_TYPE_INT, primary_camera_width,
          "height", G_TYPE_INT, primary_camera_height,
          "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    } else {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12_Q08C",
          "width", G_TYPE_INT, secondary_camera_width,
          "height", G_TYPE_INT, secondary_camera_height,
          "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    }
    g_object_set (G_OBJECT (qmmfsrc_caps_preview), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    // 2.4 Set the capabilities of camera plugin output for inference
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, inference_width,
        "height", G_TYPE_INT, inference_height,
        "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else {
    g_printerr ("Invalid Source Type\n");
    goto error_clean_elements;
  }

  // 2.5 Select the HW to DSP/GPU/CPU for model inferencing using
  // delegate property
  if (options->model_type == GST_MODEL_TYPE_SNPE) {
    GstMLSnpeDelegate snpe_delegate;
    if (options->use_cpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_NONE;
      g_print ("Using CPU Delegate\n");
    } else if (options->use_gpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_GPU;
      g_print ("Using GPU Delegate\n");
    } else if (options->use_dsp) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_DSP;
      g_print ("Using DSP Delegate\n");
    } else {
      g_printerr ("Invalid Runtime Selected\n");
      goto error_clean_elements;
    }
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "delegate", snpe_delegate, NULL);
  } else if (options->model_type == GST_MODEL_TYPE_TFLITE) {
    GstMLTFLiteDelegate tflite_delegate;
    if (options->use_cpu) {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_NONE;
      g_print ("Using CPU Delegate\n");
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", tflite_delegate, NULL);
    } else if (options->use_gpu) {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_GPU;
      g_print ("Using GPU Delegate\n");
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", tflite_delegate, NULL);
    } else if (options->use_dsp) {
      g_print ("Using DSP Delegate\n");
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
      g_printerr ("Invalid Runtime Selected\n");
      goto error_clean_elements;
    }
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    g_print ("Using DSP Delegate\n");
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "backend", "/usr/lib/libQnnHtp.so", NULL);
  } else {
    g_printerr ("Invalid Model Type\n");
    goto error_clean_elements;
  }

  // 2.6 Set properties for ML postproc plugins- module, threshold
  module_id = get_enum_value (qtimlvclassification, "module", "mobilenet");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvclassification),
        "threshold", options->threshold, "results", 2,
        "module", module_id, "labels", options->labels_path, NULL);
    if (options->model_type == GST_MODEL_TYPE_TFLITE ||
        options->model_type == GST_MODEL_TYPE_QNN) {
      g_object_set (G_OBJECT (qtimlvclassification),
          "extra-operation", GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX,
          "constants", options->constants, NULL);
    }
  } else {
    g_printerr ("Module mobilenet is not available in qtimlvclassification.\n");
    goto error_clean_elements;
  }

  // 2.7 Set the properties of Wayland compositor
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // 2.8 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "sync", TRUE, NULL);

  // 2.9 Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);

  g_object_set (G_OBJECT (classification_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options->use_file) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc,
        qtdemux, h264parse, v4l2h264dec, v4l2h264dec_caps, tee, NULL);
  } else if (options->use_rtsp) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc,
        rtph264depay, h264parse, v4l2h264dec, v4l2h264dec_caps, tee, NULL);
  } else if (options->use_camera) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,
        qmmfsrc_caps, qmmfsrc_caps_preview, NULL);
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter,
      qtimlelement, qtimlvclassification, classification_filter,
      qtivcomposer, fpsdisplaysink, waylandsink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create Pipeline for Classification
  if (options->use_file) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for filesource->qtdemux\n");
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (queue[0], h264parse, v4l2h264dec,
        v4l2h264dec_caps, queue[1], tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for parse->tee\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_rtsp) {
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
      "rtspsource->rtph264depay\n");
      goto error_clean_pipeline;
    }
  } else {
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps_preview,
        queue[2], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for qmmfsource->composer\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[4], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for qmmfsource->converter\n");
      goto error_clean_pipeline;
    }
  }

  if (options->use_rtsp || options->use_file) {
    ret = gst_element_link_many (tee, queue[2], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for tee->qtivcomposer.\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_camera) {
    ret = gst_element_link_many (queue[2], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qmmfsrc_caps_preview -> qtivcomposer.\n");
      goto error_clean_pipeline;
    }
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  ret = gst_element_link_many (qtivcomposer, queue[3], fpsdisplaysink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "qtivcomposer->fpsdisplaysink\n");
    goto error_clean_pipeline;
  }

  if (options->use_rtsp || options->use_file) {
    ret = gst_element_link_many (tee, queue[4], qtimlvconverter,
        queue[5], qtimlelement, queue[6], qtimlvclassification,
        classification_filter, queue[7], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "pre proc -> ml framework -> post proc.\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_camera) {
    ret = gst_element_link_many (queue[4], qtimlvconverter,
        queue[5], qtimlelement, queue[6], qtimlvclassification,
        classification_filter, queue[7], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "pre proc -> ml framework -> post proc.\n");
      goto error_clean_pipeline;
    }
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  if (options->use_file) {
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), queue[0]);
  }

  if (options->use_rtsp) {
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), queue[0]);
  }

  if (options->use_camera) {
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

  // Set overlay window size for Classification to display text labels
  vcomposer_sink[0] = gst_element_get_static_pad (qtivcomposer, "sink_0");
  if (vcomposer_sink[0] == NULL) {
    g_printerr ("Sink pad 1 of vcomposer couldn't be retrieved\n");
    goto error_clean_pipeline;
  }

  vcomposer_sink[1] = gst_element_get_static_pad (qtivcomposer, "sink_1");
  if (vcomposer_sink[1] == NULL) {
    g_printerr ("Sink pad 1 of vcomposer couldn't be retrieved\n");
    goto error_clean_pipeline;
  }

  for (int i=0; i < 2; i++) {
    GstVideoRectangle *positions = NULL;
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;
    positions = position_data;

    g_value_init (&position, GST_TYPE_ARRAY);
    g_value_init (&dimension, GST_TYPE_ARRAY);

    pos_vals[0] = positions[i].x; pos_vals[1] = positions[i].y;
    dim_vals[0] = positions[i].w; dim_vals[1] = positions[i].h;

    build_pad_property (&position, pos_vals, 2);
    build_pad_property (&dimension, dim_vals, 2);

    g_object_set_property (G_OBJECT (vcomposer_sink[i]), "position", &position);
    g_object_set_property (G_OBJECT (vcomposer_sink[i]), "dimensions",
        &dimension);

    g_value_unset (&position);
    g_value_unset (&dimension);
    gst_object_unref (vcomposer_sink[i]);
  }

  return TRUE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, &qmmfsrc_caps_preview,
      &filesrc, &qtdemux, &h264parse, &v4l2h264dec, &v4l2h264dec_caps, &rtspsrc,
      &rtph264depay, &tee, &qtimlvconverter, &qtimlelement,
      &qtimlvclassification, &qtivcomposer, &classification_filter,
      &waylandsink, &fpsdisplaysink, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }
  return FALSE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
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

  if (json_object_has_member (root_obj, "file-path")) {
    options->file_path =
        g_strdup (json_object_get_string_member (root_obj, "file-path"));
  }

  if (json_object_has_member (root_obj, "rtsp-ip-port")) {
    options->rtsp_ip_port =
        g_strdup (json_object_get_string_member (root_obj, "rtsp-ip-port"));
  }

  if (json_object_has_member (root_obj, "ml-framework")) {
    const gchar* framework =
        json_object_get_string_member (root_obj, "ml-framework");
    if (g_strcmp0 (framework, "snpe") == 0)
      options->model_type = GST_MODEL_TYPE_SNPE;
    else if (g_strcmp0 (framework, "tflite") == 0)
      options->model_type = GST_MODEL_TYPE_TFLITE;
    else if (g_strcmp0 (framework, "qnn") == 0)
      options->model_type = GST_MODEL_TYPE_QNN;
    else {
      gst_printerr ("ml-framework can only be one of "
          "\"snpe\", \"tflite\" or \"qnn\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "model")) {
    options->model_path =
        g_strdup (json_object_get_string_member (root_obj, "model"));
  }

  if (json_object_has_member (root_obj, "labels")) {
    options->labels_path =
        g_strdup (json_object_get_string_member (root_obj, "labels"));
  }

  if (json_object_has_member (root_obj, "constants")) {
    options->constants =
        g_strdup (json_object_get_string_member (root_obj, "constants"));
  }

  if (json_object_has_member (root_obj, "threshold")) {
    options->threshold =
        json_object_get_int_member (root_obj, "threshold");
  }

  if (json_object_has_member (root_obj, "runtime")) {
    const gchar* delegate =
        json_object_get_string_member (root_obj, "runtime");

    if (g_strcmp0 (delegate, "cpu") == 0)
      options->use_cpu = TRUE;
    else if (g_strcmp0 (delegate, "dsp") == 0)
      options->use_dsp = TRUE;
    else if (g_strcmp0 (delegate, "gpu") == 0)
      options->use_gpu = TRUE;
    else {
      gst_printerr ("Runtime can only be one of \"cpu\", \"dsp\" or \"gpu\"\n");
      g_object_unref (parser);
      return -1;
    }
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
  GstAppContext appctx = {};
  gchar help_description[2048];
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;
  GstAppOptions options = {};
  gchar *config_file = NULL;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Set default value
  options.model_path = NULL;
  options.file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.labels_path = DEFAULT_CLASSIFICATION_LABELS;
  options.constants = DEFAULT_CONSTANTS;
  options.use_cpu = FALSE, options.use_gpu = FALSE, options.use_dsp = FALSE;
  options.use_file = FALSE, options.use_rtsp = FALSE, options.use_camera = FALSE;
  options.threshold = DEFAULT_THRESHOLD_VALUE;
  options.delegate_type = DEFAULT_SNPE_DELEGATE;
  options.model_type = GST_MODEL_TYPE_SNPE;
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

  gchar camera_description[255] = {};

  if (camera_is_available) {
    snprintf (camera_description, sizeof (camera_description),
        "  camera: 0 or 1\n"
        "      Select (0) for Primary Camera and (1) for secondary one.\n"
    );
  }

  snprintf (help_description, 2047, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nTThis Sample App demonstrates Classification on Stream\n"
      "\nConfig file Fields:\n"
      "  %s"
      "  file-path: \"/PATH\"\n"
      "      File source path\n"
      "  rtsp-ip-port: \"rtsp://<ip>:<port>/<stream>\"\n"
      "      Use this parameter to provide the rtsp input.\n"
      "      Input should be provided as rtsp://<ip>:<port>/<stream>,\n"
      "      eg: rtsp://192.168.1.110:8554/live.mkv\n"
      "  ml-framework: \"snpe\" or \"tflite\" or \"qnn\"\n"
      "      Execute Model in SNPE DLC or TFlite or QNN format\n"
      "      Default model format: SNPE DLC\n"
      "  model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for SNPE DLC: "
             DEFAULT_SNPE_CLASSIFICATION_MODEL"\n"
      "      Default model path for TFLITE Model: "
             DEFAULT_TFLITE_CLASSIFICATION_MODEL"\n"
      "      Default model path for QNN Model: "
             DEFAULT_QNN_CLASSIFICATION_MODEL"\n"
      "  labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default labels path: "DEFAULT_CLASSIFICATION_LABELS"\n"
      "  constants: CONSTANTS\n"
      "      Constants, offsets and coefficients used by the chosen module \n"
      "      for post-processing of incoming tensors."
      "      Applicable only for some modules\n"
      "      Default constants: \"" DEFAULT_CONSTANTS"\"\n"
      "  threshold: 0 to 100\n"
      "      This is an optional parameter and overides default threshold value 40\n"
      "  runtime: \"cpu\" or \"gpu\" or \"dsp\"\n"
      "      This is an optional parameter. If not filled, "
      "then default dsp runtime is selected\n",
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
    g_print ("TARGET Can support file source, RTSP source and camera source\n");
  } else {
    g_print ("TARGET Can only support file source and RTSP source.\n");
    if (options.file_path == NULL && options.rtsp_ip_port == NULL) {
      g_print ("User need to give proper input file as source\n");
        gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

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
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  // Enable camera flag if user set the camera property
  if (options.camera_type == GST_CAMERA_TYPE_SECONDARY ||
      options.camera_type == GST_CAMERA_TYPE_PRIMARY)
    options.use_camera = TRUE;

  // Terminate if more than one source are there.
  if (options.use_file + options.use_camera + options.use_rtsp > 1) {
    g_printerr ("Select anyone source type either Camera or File or RTSP\n");
    gst_app_context_free (&appctx, &options, config_file);
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
      options.model_type > GST_MODEL_TYPE_QNN) {
    g_printerr ("Invalid ml-framework option selected\n"
        "Available options:\n"
        "    SNPE: %d\n"
        "    TFLite: %d\n"
        "    QNN: %d\n",
        GST_MODEL_TYPE_SNPE, GST_MODEL_TYPE_TFLITE, GST_MODEL_TYPE_QNN);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.model_type == GST_MODEL_TYPE_QNN && (options.use_cpu == TRUE ||
      options.use_gpu == TRUE )) {
    g_printerr ("QNN Serialized binary is demonstrated only with DSP"
        " runtime.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.threshold < 0 || options.threshold > 100) {
    g_printerr ("Invalid threshold value selected\n"
        "Threshold Value lies between: \n"
        "    Min: 0\n"
        "    Max: 100\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if ((options.use_cpu + options.use_gpu + options.use_dsp) > 1) {
    g_print ("Select any one runtime from CPU or GPU or DSP\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_cpu == FALSE && options.use_gpu == FALSE
      && options.use_dsp == FALSE) {
    g_print ("Setting DSP as default Runtime\n");
    options.use_dsp = TRUE;
  }

  // Set model path for execution
  if (options.model_path == NULL) {
    if (options.model_type == GST_MODEL_TYPE_SNPE) {
      options.model_path = DEFAULT_SNPE_CLASSIFICATION_MODEL;
    }
    else if (options.model_type == GST_MODEL_TYPE_QNN) {
      options.model_path = DEFAULT_QNN_CLASSIFICATION_MODEL;
    } else {
      options.model_path = DEFAULT_TFLITE_CLASSIFICATION_MODEL;
    }
  }

  if (!file_exists (options.model_path)) {
    g_print ("Invalid model file path: %s\n", options.model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.labels_path)) {
    g_print ("Invalid labels file path: %s\n", options.labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.file_path != NULL) {
    if (!file_exists (options.file_path)) {
      g_print ("Invalid file source path: %s\n", options.file_path);
      gst_app_context_free (&appctx, &options, config_file);
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
