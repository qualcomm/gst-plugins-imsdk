/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Multi Stream parallel inference on Live stream.
 *
 * Description:
 * The application takes video stream from file and maximum of
 * 24 streams in parallel and give to AI models for inference and
 * AI Model output overlayed on incoming videos are arranged in a grid pattern
 * to be displayed on HDMI Screen, save as h264 encoded mp4 file or streamed
 * over server running on device.
 * Any combination of inputs and outputs can be configured with commandline
 * options.Display will be full screen for 1 input stream,
 * divided into 2x2 grid for 2-4 streams,
 * divided into 3x3 grid for 5-9 streams,
 * divided into 4x4 grid for 10-16 streams,
 * divided into 5x5 grid for 17-24 streams.
 *
 * Pipeline for Gstreamer:
 * Source -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Batch -> Pre process-> ML Framework -> Demux batch ->
 *              Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> Sink
 *     Source: filesrc
 *     Batch: qtibatch
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimltflite
 *     Demux batch: qtimldemux
 *     Post process: qtimlvdetection/qtimlvsegmentation -> filter
 *     Sink: waylandsink (Display)/filesink
 *
 * Sample config file template:
   {
    # Output sink wayland/filesink
    "output-type":"wayland",

    # Output path to save file, if "output-type":"filesink"
    "out-file":"/opt/out.mp4",

    "pipeline-info":[
        {
          # Stream id (0-5)
          "id":0,

          # input source as file
          "input-type":"file",

          # 4 file stream path for batching
          "input-file-path":[
            {
                "stream-0":"/opt/Draw_720p_180s_30FPS.mp4",
                "stream-1":"/opt/Animals_000_720p_180s_30FPS.mp4",
                "stream-2":"/opt/Draw_720p_180s_30FPS.mp4",
                "stream-3":"/opt/Street_Bridge_720p_180s_30FPS.mp4"
            }
          ],

          # Inference plugin
          "mlframework":"qtimltflite",

          # Batch model path for Inference
          "model-path":"/opt/deeplabv3_plus_mobilenet_quantized.tflite",

          # Labels path
          "labels-path":"/opt/deeplabv3_resnet50.labels",

          # Constant values from model
          "constants":"deeplab,q-offsets=<92.0>,q-scales=<0.04518842324614525>;",

          # Post process plugin qtimlvsegmentation/qtimlvdetection
          "post-process-plugin": "qtimlvsegmentation"
        }
      ]
    }
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <sys/resource.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Maximum count of various sources possible to configure
 */
#define MAX_SRCS_COUNT 24
#define COMPOSER_SINK_COUNT 2
#define DEFAULT_BATCH_SIZE 4

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 5

/**
 * Default threshold values
 */
#define DEFAULT_THRESHOLD_VALUE 40.0

/**
 * Default wayland display width and height
 */
#define DEFAULT_DISPLAY_WIDTH 1920
#define DEFAULT_DISPLAY_HEIGHT 1080

#define DEFAULT_CONFIG_FILE "/opt/batch_config.json"

/**
 * Structure for various application specific options
 */
typedef struct {
  gchar *mlframework;
  gchar *model_path;
  gchar *labels_path;
  gchar *post_process;
  gchar *constants;
  gchar *file_path[DEFAULT_BATCH_SIZE];
} GstAppOptions;

/**
 * Structure for source count and type
 */
typedef struct {
  gint num_file;
  gchar *output_type;
  gboolean out_display;
  gchar *out_file;
} GstSourceCount;


/*
 * Update Window Grid
 * Change position of grid as per display resolution
 */
static void
update_window_grid (GstVideoRectangle *positions, guint x, guint y)
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

  win_w = width / x;
  win_h = height / y;

  for (gint i = 0; i < x; i++) {
    for (gint j = 0; j < y; j++) {
      positions[i*x+j].x = win_w*j;
      positions[i*x+j].y = win_h*i;
      positions[i*x+j].w = win_w;
      positions[i*x+j].h = win_h;
    }
  }
}

/**
 * Set parameters for ML Framework Elements.
 *
 * @param qtimlelement ML Framework Plugin.
 * @param qtimlpostprocess ML Postprocess Plugin.
 * @param filter Caps Filter Plugin for ML Postprocess Plugin.
 * @param options Application specific options.
 */
static gboolean
set_ml_params (GstElement * qtimlpostprocess,
    GstElement * filter, GstElement * qtielement, GstAppOptions options,
    guint htp_id)
{
  GstCaps *pad_filter;
  const gchar *module = NULL;
  GstStructure *delegate_options = NULL;
  gint module_id;
  gchar delegate_string[128];

  snprintf (delegate_string, 127, "QNNExternalDelegate,backend_type=htp,\
    htp_device_id=(string)%u,htp_performance_mode=(string)2,\
    htp_precision=(string)1;", htp_id);

  delegate_options = gst_structure_from_string (
    delegate_string, NULL);

  g_object_set (G_OBJECT (qtielement), "model", options.model_path,
      "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
  g_object_set (G_OBJECT (qtielement),
      "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
  g_object_set (G_OBJECT (qtielement),
      "external-delegate-options", delegate_options, NULL);
  gst_structure_free (delegate_options);

  // Set properties for ML postproc plugins- labels, module, threshold & constants
  g_object_set (G_OBJECT (qtimlpostprocess),
      "labels", options.labels_path, NULL);

  if (g_strcmp0 (options.post_process, "qtimlvsegmentation") == 0) {
    module = "deeplab-argmax";
  } else if (g_strcmp0 (options.post_process, "qtimlvdetection") == 0) {
    module = "yolov8";
  }

  module_id = get_enum_value (qtimlpostprocess, "module", module);
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlpostprocess), "module", module_id, NULL);
  } else {
    g_printerr ("Module %s is not available in qtimlpostprocess\n", module);
    return FALSE;
  }

  g_object_set (G_OBJECT (qtimlpostprocess),
      "constants", options.constants, NULL);

  // Set the properties of pad_filter for negotiation with qtivcomposer
  if (g_strcmp0 (options.post_process, "qtimlvsegmentation") == 0) {
    // set qtimlvsegmentation properties
    pad_filter = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "BGRA",
        "width", G_TYPE_INT, 256,
        "height", G_TYPE_INT, 144, NULL);
    g_object_set (G_OBJECT (filter), "caps", pad_filter, NULL);
  } else if (g_strcmp0 (options.post_process, "qtimlvdetection") == 0) {
    // set qtimlvdetection properties
    g_object_set (G_OBJECT (qtimlpostprocess),
        "labels", options.labels_path, NULL);
    g_object_set (G_OBJECT (qtimlpostprocess), "threshold",
      DEFAULT_THRESHOLD_VALUE, NULL);
    g_object_set (G_OBJECT (qtimlpostprocess), "results", 10, NULL);
    pad_filter = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "BGRA",
        "width", G_TYPE_INT, 640,
        "height", G_TYPE_INT, 360, NULL);
    g_object_set (G_OBJECT (filter), "caps", pad_filter, NULL);
  }

  gst_caps_unref (pad_filter);
  return TRUE;
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
 * Set parameters for Composer Element.
 *
 * @param qtivcomposer Composer Plugin.
 * @param options Application specific options.
 * @return TRUE on success, FALSE otherwise.
 */
static gboolean
set_composer_params (GstElement * qtivcomposer, GstSourceCount *source_count)
{
  GstVideoRectangle positions[MAX_SRCS_COUNT];
  gchar element_name[128];
  gint input_count;

  input_count = source_count->num_file;
  if (input_count == 1) {
    update_window_grid (positions, 1, 1);
  } else if (input_count <= 4) {
    update_window_grid (positions, 2, 2);
  } else if (input_count <= 9) {
    update_window_grid (positions, 3, 3);
  } else if (input_count <= 16) {
    update_window_grid (positions, 4, 4);
  } else {
    update_window_grid (positions, 5, 5);
  }

  // Set Window Position and Size for each input stream
  for (gint i = 0; i < input_count; i++) {
    GstPad *composer_sink[COMPOSER_SINK_COUNT];
    GstVideoRectangle pos = positions[i];
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;
    gint pos_vals[2], dim_vals[2];

    // Create 2 composer pads for each pipeline
    // One pad to receive the image from source,
    // other pad to receive the model output to overlay over it.
    for (gint j = 0; j < COMPOSER_SINK_COUNT; j++) {
      snprintf (element_name, 127, "sink_%d", (i*COMPOSER_SINK_COUNT + j));
      composer_sink[j] = gst_element_get_static_pad (qtivcomposer, element_name);
      if (!composer_sink[j]) {
        g_printerr ("Sink pad %d of vcomposer couldn't be retrieved\n",
            (i*COMPOSER_SINK_COUNT + j));
        return FALSE;
      }

      g_value_init (&position, GST_TYPE_ARRAY);
      g_value_init (&dimension, GST_TYPE_ARRAY);
      pos_vals[0] = pos.x; pos_vals[1] = pos.y;
      dim_vals[0] = pos.w; dim_vals[1] = pos.h;
      build_pad_property (&position, pos_vals, 2);
      build_pad_property (&dimension, dim_vals, 2);

      g_object_set_property (G_OBJECT (composer_sink[j]),
          "position", &position);
      g_object_set_property (G_OBJECT (composer_sink[j]),
          "dimensions", &dimension);

      // Set the alpha channel value for object segmentation.
      if (j == 1) {
        g_object_set (composer_sink[j], "alpha", 0.5, NULL);
      }
      g_value_unset (&position);
      g_value_unset (&dimension);
      gst_object_unref (composer_sink[j]);
    }
  }

  return TRUE;
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
 * Free Application context:
 *
 * @param appctx Application Context object
 * @param options Application specific options
 * @param source_count sources and their count
 * @param streams Total batched streams
 */
static void
gst_app_context_free (GstAppContext * appctx, GstAppOptions options[],
    GstSourceCount source_count, gint streams)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (source_count.out_file != NULL) {
    g_free (source_count.out_file);
    source_count.out_file = NULL;
  }

  for (gint i = 0; i < streams; i++) {
    if (options[i].mlframework != NULL) {
      g_free (options[i].mlframework);
      options[i].mlframework = NULL;
    }

    if (options[i].model_path != NULL) {
      g_free (options[i].model_path);
      options[i].model_path = NULL;
    }

    if (options[i].labels_path != NULL) {
      g_free (options[i].labels_path);
      options[i].labels_path = NULL;
    }

    if (options[i].constants != NULL) {
      g_free (options[i].constants);
      options[i].constants = NULL;
    }

    if (options[i].post_process != NULL) {
      g_free (options[i].post_process);
      options[i].post_process = NULL;
    }

    for (gint j = 0;j < DEFAULT_BATCH_SIZE; j++) {
      if (options[i].file_path[j] != NULL) {
        g_free (options[i].file_path[j]);
        options[i].file_path[j] = NULL;
      }
    }

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
 * @param options Application specific options
 * @param source_count sources and their count.
 * @param batch_elements Number of batch elements
 */
static gboolean
create_pipe (GstAppContext * appctx, const GstAppOptions  options[],
    GstSourceCount *source_count, gint batch_elements)
{
  // Elements for file source
  GstElement *filesrc[source_count->num_file], *qtdemux[source_count->num_file];
  GstElement *file_queue[source_count->num_file][QUEUE_COUNT];
  GstElement *file_dec_h264parse[source_count->num_file];
  GstElement *file_v4l2h264dec[source_count->num_file];
  GstElement *file_dec_tee[source_count->num_file];
  GstElement *file_qtimlvconverter[batch_elements];
  GstElement *file_qtimlelement[batch_elements];
  GstElement *file_qtimlpostprocess[source_count->num_file];
  GstElement *file_filter[source_count->num_file];

  // Elements for sinks
  GstElement *queue[QUEUE_COUNT] = {NULL}, *qtivcomposer = NULL;
  GstElement *waylandsink = NULL, *composer_caps = NULL;
  GstElement *composer_tee = NULL, *fpsdisplaysink = NULL;
  GstElement *v4l2h264enc = NULL, *enc_h264parse = NULL, *enc_tee = NULL;
  GstElement *mp4mux = NULL, *filesink = NULL;
  GstElement *qtibatch[batch_elements], *qtimldemux[batch_elements];
  GstCaps *filtercaps = NULL;
  GstStructure *fcontrols = NULL;
  gchar element_name[128];
  gboolean ret = FALSE;

  g_print ("IN Options: file: %d\n", source_count->num_file);
  g_print ("OUT Options: display: %d, file: %s\n",
      source_count->out_display, source_count->out_file);

  for (gint i = 0; i < source_count->num_file; i++) {
    // Create filesrc plugin for file input
    snprintf (element_name, 127, "filesrc-%d", i);
    filesrc[i] = gst_element_factory_make ("filesrc", element_name);
    if (!filesrc[i]) {
      g_printerr ("Failed to create filesrc-%d\n", i);
      goto error_clean_elements;
    }

    // Create demuxer for video container parsing
    snprintf (element_name, 127, "qtdemux-%d", i);
    qtdemux[i] = gst_element_factory_make ("qtdemux", element_name);
    if (!qtdemux[i]) {
      g_printerr ("Failed to create qtdemux-%d\n", i);
      goto error_clean_elements;
    }

    for (gint j=0; j < QUEUE_COUNT; j++) {
      snprintf (element_name, 127, "file_queue-%d-%d", i, j);
      file_queue[i][j] = gst_element_factory_make ("queue", element_name);
      if (!file_queue[i][j]) {
        g_printerr ("Failed to create file_queue-%d-%d\n", i, j);
        goto error_clean_elements;
      }
    }

    // Create H.264 frame parser plugin
    snprintf (element_name, 127, "file_dec_h264parse-%d", i);
    file_dec_h264parse[i] = gst_element_factory_make ("h264parse",
        element_name);
    if (!file_dec_h264parse[i]) {
      g_printerr ("Failed to create file_dec_h264parse-%d\n", i);
      goto error_clean_elements;
    }

    // Create H.264 Decoder Plugin
    snprintf (element_name, 127, "file_v4l2h264dec-%d", i);
    file_v4l2h264dec[i] = gst_element_factory_make ("v4l2h264dec",
        element_name);
    if (!file_v4l2h264dec[i]) {
      g_printerr ("Failed to create file_v4l2h264dec-%d\n", i);
      goto error_clean_elements;
    }

    snprintf (element_name, 127, "file_dec_tee-%d", i);
    file_dec_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!file_dec_tee[i]) {
      g_printerr ("Failed to create file_dec_tee-%d\n", i);
      goto error_clean_elements;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "file_filter-%d", i);
    file_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!file_filter[i]) {
      g_printerr ("Failed to create file_filter-%d\n", i);
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < batch_elements; i++) {
    // Create pre processing plugin
    snprintf (element_name, 127, "file_qtimlvconverter-%d", i);
    file_qtimlvconverter[i] = gst_element_factory_make (
        "qtimlvconverter", element_name);
    if (!file_qtimlvconverter[i]) {
      g_printerr ("Failed to create file_qtimlvconverter-%d\n", i);
      goto error_clean_elements;
    }

    // Create ML Framework Plugin
    snprintf (element_name, 127, "file_qtimlelement-%d", i);
    file_qtimlelement[i] = gst_element_factory_make (
        options[i].mlframework, element_name);
    if (!file_qtimlelement[i]) {
      g_printerr ("Failed to create file_qtimlelement-%d\n", i);
      goto error_clean_elements;
    }

    for (gint j = 0; j < DEFAULT_BATCH_SIZE; j++) {
      // Create post processing plugin
      // Each batch will have same postprocess plugin
      snprintf (element_name, 127, "file_qtimlpostprocess-%d",
          i * DEFAULT_BATCH_SIZE + j);
      file_qtimlpostprocess[i * DEFAULT_BATCH_SIZE + j] =
          gst_element_factory_make (options[i].post_process, element_name);
      if (!file_qtimlpostprocess[i * DEFAULT_BATCH_SIZE + j]) {
        g_printerr ("Failed to create file_qtimlpostprocess-%d\n", i);
        goto error_clean_elements;
      }
    }
  }

  for (gint i = 0; i < batch_elements; i++) {
    // plugin to combine buffers for batching
    snprintf (element_name, 127, "qtibatch-%d", i);
    qtibatch[i] = gst_element_factory_make ("qtibatch", element_name);
    if (!qtibatch[i]) {
      g_printerr ("Failed to create qtibatch\n");
      goto error_clean_elements;
    }

    // plugin to demux batched buffers for post-process
    snprintf (element_name, 127, "qtimldemux-%d", i);
    qtimldemux[i] = gst_element_factory_make ("qtimldemux", element_name);
    if (!qtimldemux[i]) {
      g_printerr ("Failed to create qtimldemux\n");
      goto error_clean_elements;
    }
  }

  // Create queue to decouple the processing on sink and source pad
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue-%d\n", i);
      goto error_clean_elements;
    }
  }

  // Composer to combine output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }

  if (source_count->out_file) {
    // Use capsfilter to define the composer output settings
    composer_caps = gst_element_factory_make ("capsfilter", "composer_caps");
    if (!composer_caps) {
      g_printerr ("Failed to create composer_caps\n");
      goto error_clean_elements;
    }
  }

  composer_tee = gst_element_factory_make ("tee", "composer_tee");
  if (!composer_tee) {
    g_printerr ("Failed to create composer tee\n");
    goto error_clean_elements;
  }

  if (source_count->out_display) {
    // Create Weston compositor to render the output on Display
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("Failed to create waylandsink \n");
      goto error_clean_elements;
    }

    // Create fpsdisplaysink to display the current and
    // average framerate as a text overlay
    fpsdisplaysink = gst_element_factory_make ("fpsdisplaysink",
        "fpsdisplaysink");
    if (!fpsdisplaysink) {
      g_printerr ("Failed to create fpsdisplaysink\n");
      goto error_clean_elements;
    }
  }

  if (source_count->out_file) {
    // Create H.264 Encoder plugin for file output
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }

    // Create H.264 frame parser plugin
    enc_h264parse = gst_element_factory_make ("h264parse", "enc_h264parse");
    if (!enc_h264parse) {
      g_printerr ("Failed to create enc_h264parse\n");
      goto error_clean_elements;
    }

    enc_tee = gst_element_factory_make ("tee", "enc_tee");
    if (!enc_tee) {
      g_printerr ("Failed to create enc_tee\n");
      goto error_clean_elements;
    }

    if (source_count->out_file) {
      // Create mp4mux plugin to save file in mp4 container
      mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
      if (!mp4mux) {
        g_printerr ("Failed to create mp4mux\n");
        goto error_clean_elements;
      }

      // Generic filesink plugin to write file on disk
      filesink = gst_element_factory_make ("filesink", "filesink");
      if (!filesink) {
        g_printerr ("Failed to create filesink\n");
        goto error_clean_elements;
      }
    }
  }

  // 2. Set properties for all GST plugin elements
  // 2.1 Settings for Source Plugin
  for (gint i = 0; i < batch_elements; i++) {
    for (gint j = 0; j < DEFAULT_BATCH_SIZE; j++) {
      g_object_set (G_OBJECT (filesrc[i*DEFAULT_BATCH_SIZE + j]), "location",
          options[i].file_path[j], NULL);
      g_object_set (G_OBJECT (file_v4l2h264dec[i*DEFAULT_BATCH_SIZE + j]),
          "capture-io-mode", 5,"output-io-mode", 5, NULL);
      if (!set_ml_params (file_qtimlpostprocess[i*DEFAULT_BATCH_SIZE + j],
          file_filter[i*DEFAULT_BATCH_SIZE + j],
          file_qtimlelement[i], options[i], i%2)) {
        g_printerr ("Failed to set_ml_params\n");
        goto error_clean_elements;
      }
    }
  }

  // 2.2 Set the properties for composer output
  if (source_count->out_file) {
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "interlace-mode", G_TYPE_STRING, "progressive",
        "colorimetry", G_TYPE_STRING, "bt601", NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (composer_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  }

  // 2.3 Set the properties for Wayland and fpsdisplay
  if (source_count->out_display) {
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);
    g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);

    g_object_set (G_OBJECT (fpsdisplaysink), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", TRUE,
        NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  }

  // 2.4 Set the properties for file sink
  if (source_count->out_file) {
    g_object_set (G_OBJECT (v4l2h264enc), "capture-io-mode", 5,
        "output-io-mode", 5, NULL);
    // Set bitrate for streaming usecase
    fcontrols = gst_structure_from_string (
        "fcontrols,video_bitrate=6000000,video_bitrate_mode=0", NULL);
    g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);

    if (source_count->out_file) {
      g_object_set (G_OBJECT (filesink), "location", source_count->out_file,
          NULL);
    }
  }

  // 3. Setup the pipeline
  g_print ("Add all elements to the pipeline...\n");

  for (gint i = 0; i < source_count->num_file; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc[i], qtdemux[i],
        file_dec_h264parse[i], file_v4l2h264dec[i], file_dec_tee[i],
        file_qtimlpostprocess[i], file_filter[i],
        NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), file_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < batch_elements; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), file_qtimlvconverter[i],
        file_qtimlelement[i], NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtivcomposer,
      composer_tee, NULL);

  for (gint i = 0; i < batch_elements; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtibatch[i], qtimldemux[i],
        NULL);
  }
  if (source_count->out_display) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), waylandsink, fpsdisplaysink,
        NULL);
  }

  if (source_count->out_file) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2h264enc, enc_h264parse,
        enc_tee, composer_caps, mp4mux, filesink, NULL);
  }

  g_print ("Link elements...\n");

  // Create Pipeline for file
  for (gint i = 0; i < batch_elements; i++) {
    for (gint j = 0; j < DEFAULT_BATCH_SIZE; j++) {
      ret = gst_element_link_many (filesrc[i*DEFAULT_BATCH_SIZE + j],
          qtdemux[i*DEFAULT_BATCH_SIZE + j], NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for %d"
            " filesrc -> qtdemux.\n", i);
        goto error_clean_pipeline;
      }

      // qtdemux -> file_queue[i][0] link is not created here as it is a
      // dymanic link using on_pad_added callback
      ret = gst_element_link_many (file_queue[i*DEFAULT_BATCH_SIZE + j][0],
          file_dec_h264parse[i*DEFAULT_BATCH_SIZE + j],
          file_v4l2h264dec[i*DEFAULT_BATCH_SIZE + j],
          file_queue[i*DEFAULT_BATCH_SIZE + j][1],
          file_dec_tee[i*DEFAULT_BATCH_SIZE + j], NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for %d"
            " file_queue -> file_dec_tee.\n", i);
        goto error_clean_pipeline;
      }

      ret = gst_element_link_many (file_dec_tee[i*DEFAULT_BATCH_SIZE + j],
          file_queue[i*DEFAULT_BATCH_SIZE + j][2], qtivcomposer, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for %d"
            "file_dec_tee -> qtivcomposer.\n", i);
        goto error_clean_pipeline;
      }

      ret = gst_element_link_many (file_dec_tee[i*DEFAULT_BATCH_SIZE + j],
          file_queue[i*DEFAULT_BATCH_SIZE + j][3], qtibatch[i], NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for %d"
            " file:  -> file_queue.\n", i);
        goto error_clean_pipeline;
      }

      ret = gst_element_link_many (qtimldemux[i],
          file_qtimlpostprocess[i*DEFAULT_BATCH_SIZE + j], NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for %d"
            " file: qtimldemux -> post proc\n", i);
        goto error_clean_pipeline;
      }

      ret = gst_element_link_many (
          file_qtimlpostprocess[i*DEFAULT_BATCH_SIZE + j],
          file_filter[i*DEFAULT_BATCH_SIZE + j],
          file_queue[i*DEFAULT_BATCH_SIZE + j][4], qtivcomposer, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for %d"
            " file: post proc -> composer.\n", i);
        goto error_clean_pipeline;
      }
    }

    gst_element_link_many (qtibatch[i], file_qtimlvconverter[i],
        file_qtimlelement[i], qtimldemux[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " file: qtibatch-> file_qtimlelement.\n", i);
      goto error_clean_pipeline;
    }
  }

  if (source_count->out_display) {
    ret = gst_element_link_many (
        qtivcomposer, queue[0], composer_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qtivcomposer -> composer_tee.\n");
      goto error_clean_pipeline;
    }
  } else {
    ret = gst_element_link_many (
        qtivcomposer, queue[0], composer_caps, composer_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qtivcomposer -> composer_tee.\n");
      goto error_clean_pipeline;
    }
  }

  if (source_count->out_display) {
    ret = gst_element_link_many (composer_tee, queue[1], fpsdisplaysink, NULL);
    if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        " composer_tee -> waylandsink.\n");
      goto error_clean_pipeline;
    }
  }

  if (source_count->out_file) {
    ret = gst_element_link_many (composer_tee, queue[2], v4l2h264enc, queue[3],
        enc_h264parse, enc_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " composer_tee -> encoder -> enc_tee.\n");
      goto error_clean_pipeline;
    }

    if (source_count->out_file) {
      ret = gst_element_link_many (enc_tee, queue[4], mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " enc_tee -> mp4mux -> filesink.\n");
        goto error_clean_pipeline;
      }
    }
  }

  for (gint i = 0; i < source_count->num_file; i++) {
    g_signal_connect (qtdemux[i], "pad-added", G_CALLBACK (on_pad_added),
        file_queue[i][0]);
  }

  if (!set_composer_params (qtivcomposer, source_count)) {
    g_printerr ("failed to set composer params.\n");
    goto error_clean_pipeline;
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  for (gint i = 0; i < source_count->num_file; i++) {
    cleanup_gst (&filesrc[i], &qtdemux[i],
        &file_dec_h264parse[i], &file_v4l2h264dec[i], &file_dec_tee[i],
        &file_qtimlpostprocess[i], &file_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      cleanup_gst (&file_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < batch_elements; i++) {
    cleanup_gst (&file_qtimlvconverter[i], &file_qtimlelement[i],
        &qtibatch[i], &qtimldemux[i],
        NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    cleanup_gst (&queue[i], NULL);
  }

  cleanup_gst (&qtivcomposer,
      &composer_caps, &composer_tee, NULL);

  if (source_count->out_display) {
    cleanup_gst (&waylandsink, &fpsdisplaysink,NULL);
  }

  if (source_count->out_file) {
    cleanup_gst (&v4l2h264enc, &enc_h264parse,
        &enc_tee, &mp4mux, &filesink, NULL);
  }

  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *app_name = NULL;
  JsonParser *parser = NULL;
  JsonArray *pipeline_info = NULL;
  JsonNode *root = NULL;
  JsonObject *root_obj = NULL;
  gchar *config_file = NULL;
  GError *error = NULL;
  GstAppContext appctx = {};
  GstAppOptions options[MAX_SRCS_COUNT] = {{ 0 }};
  GstSourceCount source_count = {{ 0 }};
  struct rlimit rl;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  gchar help_description[1024];
  gint streams = 0;

  // Define the new limit
  rl.rlim_cur = 4096; // Soft limit
  rl.rlim_max = 4096; // Hard limit

  // Set the new limit
  if (setrlimit (RLIMIT_NOFILE, &rl) != 0) {
    g_printerr ("Failed to set setrlimit\n");
  }

  // Get the current limit
  if (getrlimit (RLIMIT_NOFILE, &rl) != 0) {
    g_printerr ("Failed to get getrlimit\n");
  }

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

  snprintf (help_description, 1023, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates multistream inference with various "
      " input/output stream combinations",
      app_name, DEFAULT_CONFIG_FILE);
  help_description[1023] = '\0';

  // Parse command line entries.
  if ((ctx = g_option_context_new (help_description)) != NULL) {
    gboolean success = FALSE;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    return -EFAULT;
  }

  if (config_file == NULL) {
    config_file = DEFAULT_CONFIG_FILE;
  }

  if (!file_exists (config_file)) {
    g_printerr ("Invalid config file path: %s\n", config_file);
    gst_app_context_free (&appctx, options, source_count, streams);
    return -EINVAL;
  }

  parser = json_parser_new ();
  // Load the JSON file
  if (!json_parser_load_from_file (parser, config_file, &error)) {
      g_printerr ("Unable to parse JSON file: %s\n", error->message);
      g_error_free (error);
      g_object_unref (parser);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
  }

  // Get the root object
  root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    gst_printerr ("Failed to load json object\n");
    g_object_unref (parser);
    gst_app_context_free (&appctx, options, source_count, streams);
    return -EINVAL;
  }

  root_obj = json_node_get_object (root);
  // Extract pipeline-info array
  pipeline_info = json_object_get_array_member (root_obj, "pipeline-info");
  streams = json_array_get_length (pipeline_info);
  for (guint i = 0; i < streams; i++) {
    gchar file_name[1024] = {NULL};
    JsonObject *info = NULL, *input_file_info = NULL;
    gint id;
    const gchar *input_type = NULL;
    JsonArray *files_info = NULL;

    info = json_array_get_object_element (pipeline_info, i);
    id = json_object_get_int_member (info, "id");
    g_print ("ID: %d\n", id);

    if ((id < 0) || (id > (MAX_SRCS_COUNT / DEFAULT_BATCH_SIZE) -1)) {
      g_printerr ("Invalid id %d\n", id);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }

    input_type = json_object_get_string_member (info, "input-type");
    g_print ("Input Type: %s\n", input_type);
    if (g_strcmp0 (input_type, "file") != 0) {
      g_printerr ("Invalid input-type %s\n", input_type);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }

    files_info = json_object_get_array_member (info, "input-file-path");
    input_file_info = json_array_get_object_element (files_info, 0);

    for (guint file_id = 0; file_id < DEFAULT_BATCH_SIZE; file_id++) {
      snprintf (file_name, 1024, "stream-%d", file_id);
      options[id].file_path[file_id] =
          g_strdup (json_object_get_string_member (input_file_info, file_name));
      g_print ("file_path-%d: %s\n", file_id, options[id].file_path[file_id]);
      source_count.num_file++;
    }

    options[id].model_path =
        g_strdup (json_object_get_string_member (info, "model-path"));
    options[id].labels_path =
        g_strdup (json_object_get_string_member (info, "labels-path"));
    options[id].post_process =
        g_strdup (json_object_get_string_member (info, "post-process-plugin"));
    options[id].mlframework = g_strdup (json_object_get_string_member (info,
        "mlframework"));
    options[id].constants =
        g_strdup (json_object_get_string_member (info, "constants"));

    g_print ("MLframework: %s\n", options[id].mlframework);
    g_print ("Model Path: %s\n", options[id].model_path);
    g_print ("Labels path: %s\n", options[id].labels_path);
    g_print ("Post process: %s\n", options[id].post_process);
    g_print ("Constants: %s\n\n", options[id].constants);

    if ((g_strcmp0 (options[id].mlframework, "qtimltflite") != 0)) {
      g_printerr ("Only qtimltflite is supported\n");
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }

    if ((g_strcmp0 (options[id].post_process, "qtimlvsegmentation") != 0) &&
        (g_strcmp0 (options[id].post_process, "qtimlvdetection") != 0)) {
      g_printerr ("Only qtimlvsegmentation and "
          "qtimlvdetection are supported\n");
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }
  }

  source_count.output_type =
      g_strdup (json_object_get_string_member (root_obj, "output-type"));
  if (g_strcmp0 (source_count.output_type,"wayland") == 0) {
    source_count.out_display = TRUE;
  } else if (g_strcmp0 (source_count.output_type, "filesink") == 0) {
    source_count.out_file =
        g_strdup (json_object_get_string_member (root_obj, "out-file"));
  } else {
    g_printerr ("Invalid output type\n");
    gst_app_context_free (&appctx, options, source_count, streams);
    return -EINVAL;
  }
  g_object_unref (parser);

  if (source_count.num_file > MAX_SRCS_COUNT) {
    g_printerr ("Maximum supported streams : %d\n", MAX_SRCS_COUNT);
    gst_app_context_free (&appctx, options, source_count, streams);
    return -EINVAL;
  }

  if ((source_count.num_file == MAX_SRCS_COUNT) && source_count.out_file) {
    g_printerr ("Cannot encode into file as only %d Max streams are "
    "suppported. Use Wayland\n", MAX_SRCS_COUNT);
    gst_app_context_free (&appctx, options, source_count, streams);
    return -EINVAL;
  }

  for (guint id = 0; id < streams; id++) {
    if (!file_exists (options[id].model_path)) {
      g_printerr ("Invalid model file path: %s\n", options[id].model_path);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }

    if (!file_exists (options[id].labels_path)) {
      g_printerr ("Invalid labels file path: %s\n", options[id].labels_path);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }

    if (source_count.out_file &&
        !file_location_exists (source_count.out_file)) {
      g_printerr ("Invalid output file location: %s\n", source_count.out_file);
      gst_app_context_free (&appctx, options, source_count, streams);
      return -EINVAL;
    }

    for (guint file_id = 0; file_id < DEFAULT_BATCH_SIZE; file_id++) {
      if (options[id].file_path[file_id] &&
          !file_exists (options[id].file_path[file_id])) {
        g_printerr ("Invalid input file location: %s\n",
            options[id].file_path[file_id]);
        gst_app_context_free (&appctx, options, source_count, streams);
        return -EINVAL;
      }
    }
    g_print ("Run app with model: %s and labels: %s \n",
        options[id].model_path, options[id].labels_path);
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    gst_app_context_free (&appctx, options, source_count, streams);
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx, options, &source_count, streams);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    gst_app_context_free (&appctx, options, source_count, streams);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (&appctx, options, source_count, streams);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (&appctx, options, source_count, streams);
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
  gst_app_context_free (&appctx, options, source_count, streams);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
