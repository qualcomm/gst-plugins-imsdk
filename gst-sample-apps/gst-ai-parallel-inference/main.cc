/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Parallel Classification, Pose Detection, Object Detection and
 * Segmentation on Live stream (4-Streams).
 *
 * Description:
 * The application takes live video stream from camera and gives same to
 * 4 Ch parallel processing by AI models (Classification, Pose detection and
 * Object detection and Segmentation) and display scaled down preview with
 * overlayed AI models output composed as 2x2 matrix
 *
 * Pipeline for Gstreamer Parallel Inferencing (4 Stream) below:
 * qtiqmmfsrc (Camera) -> qmmfsrc_caps -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> waylandsink (Display)
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post process: qtimlvdetection/qtimlclassification/
 *                   qtimlvsegmentation/qtimlvpose -> detection_filter
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "include/gst_sample_apps_utils.h"

/**
 * Default models path and labels path
 */
#define DEFAULT_SNPE_OBJECT_DETECTION_MODEL "/opt/yolov5.dlc"
#define DEFAULT_OBJECT_DETECTION_LABELS "/opt/yolov5.labels"
#define DEFAULT_TFLITE_CLASSIFICATION_MODEL "/opt/resnet50.tflite"
#define DEFAULT_CLASSIFICATION_LABELS "/opt/resnet50.labels"
#define DEFAULT_TFLITE_POSE_DETECTION_MODEL "/opt/posenet_mobilenet_v1.tflite"
#define DEFAULT_POSE_DETECTION_LABELS "/opt/posenet_mobilenet_v1.labels"
#define DEFAULT_SNPE_SEGMENTATION_MODEL "/opt/deeplabv3_resnet50.dlc"
#define DEFAULT_SEGMENTATION_LABELS "/opt/deeplabv3_resnet50.labels"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input size
 */
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1280
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 22

/**
 * PipelineData:
 * @model: Path to model file
 * @labels: Path to label file
 * @mlframework: ML inference plugin
 * @postproc: Post processing plugin
 * @delegate: ML Execution Core.
 * @position: Window Dimension and Co-ordinate.
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
} GstPipelineData;

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
      {960, 0, 960, 540}, GST_ML_TFLITE_DELEGATE_EXTERNAL},
  {DEFAULT_TFLITE_POSE_DETECTION_MODEL, DEFAULT_POSE_DETECTION_LABELS,
      "qtimlvconverter", "qtimltflite", "qtimlvpose",
      {0, 540, 960, 540}, GST_ML_TFLITE_DELEGATE_EXTERNAL},
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

/**
 * Help Menu of the Application.
 *
 * @param app_name Application executable name.
 */
void
help (const gchar *app_name)
{
  g_print ("Usage: %s \n", app_name);
  g_print ("\nThis Sample App demonstrates Classification, Segmemtation\n");
  g_print ("Object Detection, Pose Detection On Live Stream ");
  g_print ("and output 4 Parallel Stream.\n\n");
  g_print ("Default Path for model and labels used are as below:\n");
  g_print ("  ------------------------------------------------------------"
      "--------------------------\n");
  g_print ("  |Algorithm         %-32s  %-32s|\n", "Model", "Labels");
  g_print ("  ------------------------------------------------------------"
      "--------------------------\n");
  g_print ("  |Object detection  %-32s  %-32s|\n",
      DEFAULT_SNPE_OBJECT_DETECTION_MODEL, DEFAULT_OBJECT_DETECTION_LABELS);
  g_print ("  |Pose estimation   %-32s  %-32s|\n",
      DEFAULT_TFLITE_POSE_DETECTION_MODEL, DEFAULT_POSE_DETECTION_LABELS);
  g_print ("  |Segmentation      %-32s  %-32s|\n",
      DEFAULT_SNPE_SEGMENTATION_MODEL, DEFAULT_SEGMENTATION_LABELS);
  g_print ("  |Classification    %-32s  %-32s|\n",
      DEFAULT_TFLITE_CLASSIFICATION_MODEL, DEFAULT_CLASSIFICATION_LABELS);
  g_print ("  ------------------------------------------------------------"
      "--------------------------\n");

  g_print ("\nTo use your own model and labels replace at the default paths\n");
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
create_pipe (GstAppContext * appctx)
{
  GstElement *qtiqmmfsrc, *qmmfsrc_caps, *tee;
  GstElement *qtimlvconverter[GST_PIPELINE_CNT];
  GstElement *qtimlelement[GST_PIPELINE_CNT];
  GstElement *qtimlvpostproc[GST_PIPELINE_CNT];
  GstElement *detection_filter[GST_PIPELINE_CNT];
  GstElement *qtivcomposer, *waylandsink, *queue[QUEUE_COUNT];
  GstStructure *delegate_options;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint module_id;
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;

  // 1. Create the elements or Plugins
  // Create qtiqmmfsrc plugin for camera stream
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  if (!qtiqmmfsrc) {
    g_printerr ("Failed to create qtiqmmfsrc\n");
    return FALSE;
  }

  // Use capsfilter to define the camera output settings
  qmmfsrc_caps = gst_element_factory_make ("capsfilter", "qmmfsrc_caps");
  if (!qmmfsrc_caps) {
    g_printerr ("Failed to create qmmfsrc_caps\n");
    return FALSE;
  }

  // Use tee to send same data buffer
  // one for AI inferencing, one for Display composition
  tee = gst_element_factory_make ("tee", "tee");
  if (!tee) {
    g_printerr ("Failed to create tee\n");
    return FALSE;
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
      return FALSE;
    }

    // Create the ML inferencing plugin SNPE/TFLite
    snprintf (element_name, 127, "%s-%d", mlframework, i);
    qtimlelement[i] = gst_element_factory_make (mlframework, element_name);
    if (!qtimlelement[i]) {
      g_printerr ("Failed to create qtimlelement %d\n", i);
      return FALSE;
    }

    // Plugin for ML postprocessing based on config
    snprintf (element_name, 127, "%s-%d", postproc, i);
    qtimlvpostproc[i] = gst_element_factory_make (postproc, element_name);
    if (!qtimlvpostproc[i]) {
      g_printerr ("Failed to create qtimlvpostproc %d\n", i);
      return FALSE;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "capsfilter-%d", i);
    detection_filter[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!detection_filter[i]) {
      g_printerr ("Failed to create detection_filter %d\n", i);
      return FALSE;
    }
  }

  // Create queue to decouple the processing on sink and source pad.
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      return FALSE;
    }
  }

  // Composer to combine 4 input streams as 2x2 matrix in single display
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    return FALSE;
  }

  // Create Wayland compositor to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink \n");
    return FALSE;
  }

  // 1.1 Append all elements in a list for cleanup
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, tee);
  appctx->plugins = g_list_append (appctx->plugins, qmmfsrc_caps);

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    appctx->plugins = g_list_append (appctx->plugins, qtimlvconverter[i]);
    appctx->plugins = g_list_append (appctx->plugins, qtimlelement[i]);
    appctx->plugins = g_list_append (appctx->plugins, qtimlvpostproc[i]);
    appctx->plugins = g_list_append (appctx->plugins, detection_filter[i]);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    appctx->plugins = g_list_append (appctx->plugins, queue[i]);
  }

  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // 2. Set properties for all GST plugin elements
  // 2.1 set properties for 4 AI pipelines, like HW, Model, Post proc
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
        g_value_set_string (&value, "Conv_198");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Conv_232");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Conv_266");
        gst_value_array_append_value (&layers, &value);
        g_object_set_property (G_OBJECT (qtimlelement[i]), "layers", &layers);
        module_id = get_enum_value (qtimlvpostproc[i], "module", "yolov5");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]),
              "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
          goto error;
        }
        // Set the object detection module threshold limit
        g_object_set (G_OBJECT (qtimlvpostproc[GST_OBJECT_DETECTION]),
            "threshold", 40.0, "results", 10, NULL);
        break;

      case GST_CLASSIFICATION:
        module_id = get_enum_value (qtimlvpostproc[i], "module", "mobilenet");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]), "threshold", 20.0,
              "results", 3, "module", module_id, NULL);
        } else {
          g_printerr ("Module mobilenet not available in qtimlvclassifivation\n");
          goto error;
        }
        break;

      case GST_POSE_DETECTION:
        module_id = get_enum_value (qtimlvpostproc[i], "module", "posenet");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]), "threshold", 51.0,
              "results", 2, "module", module_id,
              "constants", "Posenet,q-offsets=<128.0,128.0,117.0>,q-scales="
              "<0.0784313753247261,0.0784313753247261,1.3875764608383179>;",
              NULL);
        } else {
          g_printerr ("Module posenet is not available in qtimlvpose\n");
          goto error;
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
          goto error;
        }
        break;
      default:
        g_printerr ("Cannot be here");
        goto error;
    }
  }

  // 2.2 Set the capabilities of camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);


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
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,  qmmfsrc_caps,
      tee, qtivcomposer, waylandsink, NULL);

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter[i],
        qtimlelement[i], qtimlvpostproc[i], detection_filter[i], NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // 3.1 Create pipeline for Parallel Inferencing
  ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[0], tee, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for qmmfsource->tee\n");
    goto error;
  }

  // 3.2 Create links for all 4 streams
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    ret = gst_element_link_many (
        tee, queue[5*i + 1], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for object detection 1.\n");
      goto error;
    }

    ret = gst_element_link_many (
        tee, queue[5*i + 2], qtimlvconverter[i], queue[5*i + 3],
        qtimlelement[i], queue[5*i + 4],
        qtimlvpostproc[i],
        detection_filter[i], queue[5*i + 5], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for object detection 2.\n");
      goto error;
    }
  }

  ret = gst_element_link_many (qtivcomposer, queue[QUEUE_COUNT-1], waylandsink,
      NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for composer->wayland.\n");
    goto error;
  }

  g_print ("All elements are linked successfully\n");

  // 3.3 Set position of all inference windows in a grid pattern
  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    GstPad *composer_sink[2];
    GstVideoRectangle pos = pipeline_data[i].position;
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;
    gdouble alpha_value;
    gint pos_vals[2], dim_vals[2];

    // Create 2 composer pads for each pipeline
    // One pad to receive the image from source,
    // other pad to receive the model output to overlay over it.
    for (gint j = 0; j < 2; j++) {
      snprintf (element_name, 127, "sink_%d", (i*2 + j));
      composer_sink[j] = gst_element_get_static_pad (qtivcomposer, element_name);
      if (!composer_sink[j]) {
        g_printerr ("Sink pad %d of vcomposer couldn't be retrieved\n",
            (i*2 + j));
        goto error;
      }
    }

    g_value_init (&position, GST_TYPE_ARRAY);
    g_value_init (&dimension, GST_TYPE_ARRAY);
    pos_vals[0] = pos.x; pos_vals[1] = pos.y;
    dim_vals[0] = pos.w; dim_vals[1] = pos.h;
    build_pad_property (&position, pos_vals, 2);
    build_pad_property (&dimension, dim_vals, 2);

    g_object_set_property (G_OBJECT (composer_sink[0]),
        "position", &position);
    g_object_set_property (G_OBJECT (composer_sink[0]),
        "dimensions", &dimension);
    g_object_set_property (G_OBJECT (composer_sink[1]),
        "position", &position);

    switch (i) {
      case GST_CLASSIFICATION:
        // Reset the value for dimensions to have smaller text on 1/3 screen
        g_value_unset (&dimension);
        g_value_init (&dimension, GST_TYPE_ARRAY);
        dim_vals[0] = pos.w/3; dim_vals[1] = pos.h/3;
        build_pad_property (&dimension, dim_vals, 2);
        break;
      case GST_SEGMENTATION:
        // Set alpha channel value for Segmentation overlay window
        alpha_value = 0.5;
        g_object_set (composer_sink[1], "alpha", &alpha_value, NULL);
        break;
    }

    g_object_set_property (G_OBJECT (composer_sink[1]),
        "dimensions", &dimension);

    g_value_unset (&position);
    g_value_unset (&dimension);
    gst_object_unref (composer_sink[0]);
    gst_object_unref (composer_sink[1]);
  }

  return TRUE;

error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_caps,
      tee, qtivcomposer, waylandsink, NULL);

  for (gint i = 0; i < GST_PIPELINE_CNT; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtimlelement[i],
        qtimlvpostproc[i], qtimlvconverter[i], detection_filter[i], NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  return FALSE;
}

/**
 * Unlinks and removes all elements.
 *
 * @param appctx Application Context Pointer.
 */
static void
destroy_pipe (GstAppContext * appctx)
{
  GstElement *curr = (GstElement *) appctx->plugins->data;
  GstElement *next;
  GList *list = appctx->plugins->next;

  for ( ; list != NULL; list = list->next) {
    next = (GstElement *) list->data;
    gst_element_unlink (curr, next);
    gst_bin_remove (GST_BIN (appctx->pipeline), curr);
    curr = next;
  }
  gst_bin_remove (GST_BIN (appctx->pipeline), curr);

  g_list_free (appctx->plugins);
  appctx->plugins = NULL;
  gst_object_unref (appctx->pipeline);
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

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  if (argc > 1) {
    help (app_name);
    return -1;
  }

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
  ret = create_pipe (&appctx);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    destroy_pipe (&appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    destroy_pipe (&appctx);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    destroy_pipe (&appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);

  // Register respective callback function based on message
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);

  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);

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
  destroy_pipe (&appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
