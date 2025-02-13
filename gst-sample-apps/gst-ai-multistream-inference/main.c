/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Multi Stream parallel inference on Live stream.
 *
 * Description:
 * The application takes video stream from camera/file/rtsp and maximum of
 * 32 streams in parallel and give to AI models for inference and
 * AI Model output overlayed on incoming videos are arranged in a grid pattern
 * to be displayed on HDMI Screen, save as h264 encoded mp4 file or streamed
 * over rtsp server running on device.
 * Any combination of inputs and outputs can be configured with commandline
 * options. Camera default resolution is set to 1280x720. Display will be
 * full screen for 1 input stream, divided into 2x2 grid for 2-4 input streams,
 * divided into 3x3 grid for 5-9 streams and divided into 4x4 grid for 10-16
 * and 5x5 for 17-24 stream and 6x6 for 25-32 stream
 *
 * Pipeline for Gstreamer:
 * Source -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> Sink
 *     Source: qmmfsrc (Camera)/filesrc/rtspsrc
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimltflite
 *     Post process: qtimlvdetection -> detection_filter
 *     Sink: waylandsink (Display)/filesink/rtsp server
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
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_TFLITE_YOLOV8_MODEL "/etc/models/yolov8_det_quantized.tflite"
#define DEFAULT_YOLOV8_LABELS "/etc/labels/yolov8.labels"
#define DEFAULT_TFLITE_INCEPTIONV3_MODEL \
    "/etc/models/inception_v3_quantized.tflite"
#define DEFAULT_CLASSIFICATION_LABELS "/etc/labels/classification.labels"

/**
 * Default constants to dequantize values
 */
#define DEFAULT_DETECTION_CONSTANTS \
    "YOLOv8,q-offsets=<21.0, 0.0, 0.0>,\
    q-scales=<3.093529462814331, 0.00390625, 1.0>;"
#define DEFAULT_CLASSIFICATION_CONSTANTS \
    "Mobilenet,q-offsets=<38.0>,q-scales=<0.17039915919303894>;"

/**
 * To enable softmax operation for post processing
 */
#define GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX 1

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1280
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Maximum count of various sources possible to configure
 */
#define MAX_CAMSRCS 2
#define MAX_FILESRCS 32
#define MAX_RTSPSRCS 32
#define MAX_SRCS_COUNT 32
#define COMPOSER_SINK_COUNT 2

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 32

/**
 * Default threshold values
 */
#define DEFAULT_THRESHOLD_VALUE 40.0

/**
 * Default output filter width and height
 */
#define DEFAULT_FILTER_WIDTH 640
#define DEFAULT_FILTER_HEIGHT 360

/**
 * Default wayland display width and height
 */
#define DEFAULT_DISPLAY_WIDTH 1920
#define DEFAULT_DISPLAY_HEIGHT 1080

/**
 * rstp sink configuration
 */
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT "8554"

/**
 * Default path for config file
 */
#define DEFAULT_CONFIG_FILE "/etc/configs/config-multistream-inference.json"

/**
 * Structure for various application specific options
 */
typedef struct {
  gchar *mlframework;
  gchar *model_path;
  GstInputStreamType input_type;
  gchar *labels_path;
  gchar *out_file;
  gchar *constants;
  gchar *ip_address;
  gchar *port_num;
  gint num_camera;
  gint num_file;
  gint num_rtsp;
  gint camera_id;
  gint input_count;
  gboolean out_display;
  gboolean out_rtsp;
  gint use_case;
  gchar *input_file_path[MAX_FILESRCS];
  gchar *input_rtsp_path[MAX_RTSPSRCS];
} GstAppOptions;

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

  for (guint i = 0; i < x; i++) {
    for (guint j = 0; j < y; j++) {
      GstVideoRectangle window = {win_w*j, win_h*i, win_w, win_h};
      positions[i*x+j] = window;
    }
  }
}

/*
 * Read HTP core count
 */
static gint
get_num_cdsp_backends ()
{
  gint num_cdsp_backends = 1;

  if (access ("/dev/fastrpc-cdsp1", F_OK) == 0)
    num_cdsp_backends = 2;

  return num_cdsp_backends;
}

/**
 * Set parameters for ML Framework Elements.
 *
 * @param qtimlelement ML Framework Plugin.
 * @param qtimlpostprocess ML Detection Plugin.
 * @param detection_filter Caps Filter Plugin for ML Detection Plugin.
 * @param options Application specific options.
 */
static gboolean
set_ml_params (GstElement * qtimlelement, GstElement * qtimlpostprocess,
    GstElement * detection_filter, GstAppOptions * options, guint htp_id)
{
  GstStructure *delegate_options;
  GstCaps *pad_filter;
  gint module_id;
  const gchar *module = NULL;
  gchar delegate_string[128];

  snprintf (delegate_string, 127, "QNNExternalDelegate,backend_type=htp,"
      "htp_device_id=(string)%u,htp_performance_mode=(string)2,"
      "htp_precision=(string)1;", htp_id);

  delegate_options = gst_structure_from_string (
    delegate_string, NULL);

  g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
      "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
  g_object_set (G_OBJECT (qtimlelement),
      "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
  g_object_set (G_OBJECT (qtimlelement),
      "external-delegate-options", delegate_options, NULL);
  gst_structure_free (delegate_options);

  // Set properties for ML postproc plugins- labels, module, threshold & constants
  g_object_set (G_OBJECT (qtimlpostprocess),
      "labels", options->labels_path, NULL);

  if (options->use_case == GST_OBJECT_DETECTION) {
    module = "yolov8";
  } else if (options->use_case == GST_CLASSIFICATION) {
    module = "mobilenet";
  }
  module_id = get_enum_value (qtimlpostprocess, "module", module);
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlpostprocess), "module", module_id, NULL);
  } else {
    g_printerr ("Module %s is not available in qtimlpostprocess\n", module);
    return FALSE;
  }

  g_object_set (G_OBJECT (qtimlpostprocess), "threshold", DEFAULT_THRESHOLD_VALUE,
    "results", 2, "constants", options->constants, NULL);
  if (options->use_case == GST_CLASSIFICATION) {
    g_object_set (G_OBJECT (qtimlpostprocess), "extra-operation",
        GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX, NULL);
  }

  // Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, DEFAULT_FILTER_WIDTH,
      "height", G_TYPE_INT, DEFAULT_FILTER_HEIGHT, NULL);
  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
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
set_composer_params (GstElement * qtivcomposer, GstAppOptions * options)
{
  GstVideoRectangle positions[MAX_SRCS_COUNT];
  gchar element_name[128];

  if (options->input_count == 1) {
    update_window_grid (positions, 1, 1);
  } else if (options->input_count <= 4) {
    update_window_grid (positions, 2, 2);
  } else if (options->input_count <= 9) {
    update_window_grid (positions, 3, 3);
  } else if (options->input_count <= 16) {
    update_window_grid (positions, 4, 4);
  } else if (options->input_count <= 25) {
    update_window_grid (positions, 5, 5);
  } else {
    update_window_grid (positions, 6, 6);
  }

  // Set Window Position and Size for each input stream
  for (gint i = 0; i < options->input_count; i++) {
    GstPad *composer_sink[2];
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
            (i*2 + j));
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
 */
static void
gst_app_context_free
(GstAppContext * appctx, GstAppOptions * options, gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->model_path != NULL &&
    options->model_path != (gchar *)(&DEFAULT_TFLITE_YOLOV8_MODEL) &&
    options->model_path != (gchar *)(&DEFAULT_TFLITE_INCEPTIONV3_MODEL)) {
    g_free ((gpointer)options->model_path);
    options->model_path = NULL;
  }

  if (options->labels_path != NULL &&
    options->labels_path != (gchar *)(&DEFAULT_YOLOV8_LABELS) &&
    options->labels_path != (gchar *)(&DEFAULT_CLASSIFICATION_LABELS)) {
    g_free ((gpointer)options->labels_path);
    options->labels_path = NULL;
  }

  if (options->out_file != NULL) {
    g_free ((gpointer)options->out_file);
    options->out_file = NULL;
  }

  if (options->constants != NULL &&
    options->constants != (gchar *)(&DEFAULT_DETECTION_CONSTANTS) &&
    options->constants != (gchar *)(&DEFAULT_CLASSIFICATION_CONSTANTS)) {
    g_free ((gpointer)options->constants);
    options->constants = NULL;
  }

  if (options->ip_address != NULL &&
    options->ip_address != (gchar *)(&DEFAULT_IP)) {
    g_free ((gpointer)options->ip_address);
    options->ip_address = NULL;
  }

  if (options->port_num != (gchar *)(&DEFAULT_PORT) &&
      options->port_num != NULL) {
    g_free ((gpointer)options->port_num);
    options->port_num = NULL;
  }

  if (config_file != NULL &&
      config_file != (gchar *)(&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer)config_file);
    config_file = NULL;
  }

  for (gint i = 0; i < options->num_file; i++) {
    g_free ((gpointer)options->input_file_path[i]);
    options->input_file_path[i] = NULL;
  }
  for (gint i = 0; i < options->num_rtsp; i++) {
    g_free ((gpointer)options->input_rtsp_path[i]);
    options->input_rtsp_path[i] = NULL;
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
 * @param options Application specific options.
 */
static gboolean
create_pipe (GstAppContext * appctx, GstAppOptions * options, guint htp_count)
{
  // Elements for camera source
  GstElement *camsrc[options->num_camera], *cam_caps[options->num_camera];
  GstElement *cam_queue[options->num_camera][QUEUE_COUNT];
  GstElement *cam_tee[options->num_camera];
  GstElement *cam_qtimlvconverter[options->num_camera];
  GstElement *cam_qtimlelement[options->num_camera];
  GstElement *cam_qtimlpostprocess[options->num_camera];
  GstElement *cam_detection_filter[options->num_camera];

  // Elements for file source
  GstElement *filesrc[options->num_file], *qtdemux[options->num_file];
  GstElement *file_queue[options->num_file][QUEUE_COUNT];
  GstElement *file_dec_parse[options->num_file];
  GstElement *file_v4l2_decoder[options->num_file];
  GstElement *file_decode_caps[options->num_file];
  GstElement *file_dec_tee[options->num_file];
  GstElement *file_qtimlvconverter[options->num_file];
  GstElement *file_qtimlelement[options->num_file];
  GstElement *file_qtimlpostprocess[options->num_file];
  GstElement *file_detection_filter[options->num_file];

  // Elements for rtsp source
  GstElement *rtspsrc[options->num_rtsp], *rtph_depay[options->num_rtsp];
  GstElement *rtsp_queue[options->num_rtsp][QUEUE_COUNT];
  GstElement *rtsp_dec_parse[options->num_rtsp];
  GstElement *rtsp_v4l2_dec[options->num_rtsp];
  GstElement *rtsp_decode_caps[options->num_rtsp];
  GstElement *rtsp_dec_tee[options->num_rtsp];
  GstElement *rtsp_qtimlvconverter[options->num_rtsp];
  GstElement *rtsp_qtimlelement[options->num_rtsp];
  GstElement *rtsp_qtimlpostprocess[options->num_rtsp];
  GstElement *rtsp_detection_filter[options->num_rtsp];

  // Elements for sinks
  GstElement *queue[QUEUE_COUNT] = {NULL}, *qtivcomposer = NULL;
  GstElement *waylandsink = NULL, *composer_caps = NULL, *composer_tee = NULL;
  GstElement *v4l2_encoder = NULL, *enc_tee = NULL;
  GstElement *file_encoder_parse = NULL, *rtsp_encoder_parse = NULL;
  GstElement *mp4mux = NULL, *filesink = NULL, *fpsdisplaysink = NULL;
  GstElement *qtirtspbin = NULL;
  GstCaps *filtercaps = NULL;
  GstStructure *fcontrols = NULL;
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gchar element_name[128];
  gboolean ret = FALSE;

  g_print ("IN Options: camera: %d (id: %d), file: %d, rtsp: %d\n",
      options->num_camera, options->camera_id, options->num_file,
      options->num_rtsp);
  g_print ("OUT Options: display: %d, file: %s, rtsp: %d\n",
      options->out_display, options->out_file, options->out_rtsp);

  // 1. Create the elements or Plugins
  for (gint i = 0; i < options->num_camera; i++) {
    // Create qtiqmmfsrc plugin for camera stream
    snprintf (element_name, 127, "camsrc-%d", i);
    camsrc[i] = gst_element_factory_make ("qtiqmmfsrc", element_name);
    if (!camsrc[i]) {
      g_printerr ("Failed to create camsrc-%d\n", i);
      goto error_clean_elements;
    }

    // Use capsfilter to define the camera output settings
    snprintf (element_name, 127, "cam_caps-%d", i);
    cam_caps[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!cam_caps[i]) {
      g_printerr ("Failed to create cam_caps-%d\n", i);
      goto error_clean_elements;
    }

    for (gint j=0; j < QUEUE_COUNT; j++) {
      snprintf (element_name, 127, "cam_queue-%d-%d", i, j);
      cam_queue[i][j] = gst_element_factory_make ("queue", element_name);
      if (!cam_queue[i][j]) {
        g_printerr ("Failed to create cam_queue-%d-%d\n", i, j);
        goto error_clean_elements;
      }
    }

    snprintf (element_name, 127, "cam_tee-%d", i);
    cam_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!cam_tee[i]) {
      g_printerr ("Failed to create cam_tee-%d\n", i);
      goto error_clean_elements;
    }

    // Create pre processing plugin
    snprintf (element_name, 127, "cam_qtimlvconverter-%d", i);
    cam_qtimlvconverter[i] = gst_element_factory_make (
        "qtimlvconverter", element_name);
    if (!cam_qtimlvconverter[i]) {
      g_printerr ("Failed to create cam_qtimlvconverter-%d\n", i);
      goto error_clean_elements;
    }

    // Create ML Framework Plugin
    snprintf (element_name, 127, "cam_qtimlelement-%d", i);
    cam_qtimlelement[i] = gst_element_factory_make (
        options->mlframework, element_name);
    if (!cam_qtimlelement[i]) {
      g_printerr ("Failed to create cam_qtimlelement-%d\n", i);
      goto error_clean_elements;
    }

    // Create post processing plugin
    snprintf (element_name, 127, "cam_qtimlpostprocess-%d", i);
    if (options->use_case == GST_OBJECT_DETECTION) {
      cam_qtimlpostprocess[i] = gst_element_factory_make (
          "qtimlvdetection", element_name);
    } else if (options->use_case == GST_CLASSIFICATION) {
      cam_qtimlpostprocess[i] = gst_element_factory_make (
          "qtimlvclassification", element_name);
    } else {
      g_printerr ("Invalid use case for cam_qtimlpostprocess-%d\n", i);
    }
    if (!cam_qtimlpostprocess[i]) {
      g_printerr ("Failed to create cam_qtimlpostprocess-%d\n", i);
      goto error_clean_elements;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "cam_detection_filter-%d", i);
    cam_detection_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!cam_detection_filter[i]) {
      g_printerr ("Failed to create cam_detection_filter-%d\n", i);
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
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

    // Create H.264/H265 frame parser plugin
    snprintf (element_name, 127, "file_dec_parse-%d", i);
    if (options->input_type == GST_INPUT_STREAM_H264) {
      file_dec_parse[i] = gst_element_factory_make ("h264parse", element_name);
      if (!file_dec_parse[i]) {
        g_printerr ("Failed to create H264 file_dec_parse-%d\n", i);
        goto error_clean_elements;
      }
    } else if (options->input_type == GST_INPUT_STREAM_H265) {
      file_dec_parse[i] = gst_element_factory_make ("h265parse", element_name);
      if (!file_dec_parse[i]) {
        g_printerr ("Failed to create H265 file_dec_parse-%d\n", i);
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Failed to create file_dec_parse-%d Invalid input-type \n", i);
      goto error_clean_elements;
    }

    // Create H.264/H265 Decoder Plugin
    snprintf (element_name, 127, "file_v4l2_decoder-%d", i);
    if (options->input_type == GST_INPUT_STREAM_H264) {
      file_v4l2_decoder[i] = gst_element_factory_make ("v4l2h264dec", element_name);
      if (!file_v4l2_decoder[i]) {
        g_printerr ("Failed to create file_v4l2_decoder-%d\n", i);
        goto error_clean_elements;
      }
    } else if (options->input_type == GST_INPUT_STREAM_H265) {
      file_v4l2_decoder[i] = gst_element_factory_make ("v4l2h265dec", element_name);
      if (!file_v4l2_decoder[i]) {
        g_printerr ("Failed to create file_v4l2_decoder-%d\n", i);
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Failed to create file_v4l2_decoder-%d Invalid input-type \n", i);
      goto error_clean_elements;
    }

    // Create caps for H.264/H265 Decoder
    snprintf (element_name, 127, "file_decode_caps-%d", i);
    file_decode_caps[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!file_decode_caps[i]) {
      g_printerr ("Failed to create file_decode_caps-%d\n", i);
      goto error_clean_elements;
    }

    snprintf (element_name, 127, "file_dec_tee-%d", i);
    file_dec_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!file_dec_tee[i]) {
      g_printerr ("Failed to create file_dec_tee-%d\n", i);
      goto error_clean_elements;
    }

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
        options->mlframework, element_name);
    if (!file_qtimlelement[i]) {
      g_printerr ("Failed to create file_qtimlelement-%d\n", i);
      goto error_clean_elements;
    }

    // Create post processing plugin
    snprintf (element_name, 127, "file_qtimlpostprocess-%d", i);

    if (options->use_case == GST_OBJECT_DETECTION) {
      file_qtimlpostprocess[i] = gst_element_factory_make (
          "qtimlvdetection", element_name);
    } else if (options->use_case == GST_CLASSIFICATION) {
      file_qtimlpostprocess[i] = gst_element_factory_make (
          "qtimlvclassification", element_name);
    } else {
      g_printerr ("Invalid use case for file_qtimlpostprocess-%d\n", i);
    }
    if (!file_qtimlpostprocess[i]) {
      g_printerr ("Failed to create file_qtimlpostprocess-%d\n", i);
      goto error_clean_elements;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "file_detection_filter-%d", i);
    file_detection_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!file_detection_filter[i]) {
      g_printerr ("Failed to create file_detection_filter-%d\n", i);
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    // Create rtspsrc plugin for rtsp input
    snprintf (element_name, 127, "rtspsrc-%d", i);
    rtspsrc[i] = gst_element_factory_make ("rtspsrc", element_name);
    if (!rtspsrc[i]) {
      g_printerr ("Failed to create rtspsrc-%d\n", i);
      goto error_clean_elements;
    }

    // Create rtph_depay plugin for rtsp payload parsing
    snprintf (element_name, 127, "rtp_depay-%d", i);
    if (options->input_type == GST_INPUT_STREAM_H264) {
      rtph_depay[i] = gst_element_factory_make ("rtph264depay", element_name);
      if (!rtph_depay[i]) {
        g_printerr ("Failed to create rtp_depay-%d\n", i);
        goto error_clean_elements;
      }
    } else if (options->input_type == GST_INPUT_STREAM_H265) {
      rtph_depay[i] = gst_element_factory_make ("rtph265depay", element_name);
      if (!rtph_depay[i]) {
        g_printerr ("Failed to create rtp_depay-%d\n", i);
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Failed to create rtp_depay-%d due to invalid input-type\n", i);
      goto error_clean_elements;
    }

    for (gint j=0; j < QUEUE_COUNT; j++) {
      snprintf (element_name, 127, "rtsp_queue-%d-%d", i, j);
      rtsp_queue[i][j] = gst_element_factory_make ("queue", element_name);
      if (!rtsp_queue[i][j]) {
        g_printerr ("Failed to create rtsp_queue-%d-%d\n", i, j);
        goto error_clean_elements;
      }
    }

    // Create H.264/H265 frame parser plugin
    snprintf (element_name, 127, "rtsp_dec_parse-%d", i);
    if (options->input_type == GST_INPUT_STREAM_H264) {
      rtsp_dec_parse[i] = gst_element_factory_make ("h264parse", element_name);
      if (!rtsp_dec_parse[i]) {
        g_printerr ("Failed to create rtsp_dec_parse-%d\n", i);
        goto error_clean_elements;
      }
    } else if (options->input_type == GST_INPUT_STREAM_H265) {
      rtsp_dec_parse[i] = gst_element_factory_make ("h265parse", element_name);
      if (!rtsp_dec_parse[i]) {
        g_printerr ("Failed to create rtsp_dec_parse-%d\n", i);
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Failed to create rtsp_dec_parse-%d\n Invalid input_type", i);
      goto error_clean_elements;
    }

    // Create H.264/H265 Decoder Plugin
    snprintf (element_name, 127, "rtsp_v4l2_dec-%d", i);
    if (options->input_type == GST_INPUT_STREAM_H264) {
      rtsp_v4l2_dec[i] = gst_element_factory_make ("v4l2h264dec", element_name);
      if (!rtsp_v4l2_dec[i]) {
        g_printerr ("Failed to create rtsp_v4l2_dec-%d\n", i);
        goto error_clean_elements;
      }
    } else if (options->input_type == GST_INPUT_STREAM_H265) {
      rtsp_v4l2_dec[i] = gst_element_factory_make ("v4l2h265dec", element_name);
      if (!rtsp_v4l2_dec[i]) {
        g_printerr ("Failed to create rtsp_v4l2_dec-%d\n", i);
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Failed to create rtsp_v4l2_dec-%d\n Invalid input-type", i);
      goto error_clean_elements;
    }

    // Create caps for H.264 Decoder Plugin
    snprintf (element_name, 127, "rtsp_decode_caps-%d", i);
    rtsp_decode_caps[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!rtsp_decode_caps[i]) {
      g_printerr ("Failed to create rtsp_decode_caps-%d\n", i);
      goto error_clean_elements;
    }

    snprintf (element_name, 127, "rtsp_dec_tee-%d", i);
    rtsp_dec_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!rtsp_dec_tee[i]) {
      g_printerr ("Failed to create rtsp_dec_tee-%d\n", i);
      goto error_clean_elements;
    }

    // Create pre processing plugin
    snprintf (element_name, 127, "rtsp_qtimlvconverter-%d", i);
    rtsp_qtimlvconverter[i] = gst_element_factory_make (
        "qtimlvconverter", element_name);
    if (!rtsp_qtimlvconverter[i]) {
      g_printerr ("Failed to create rtsp_qtimlvconverter-%d\n", i);
      goto error_clean_elements;
    }

    // Create ML Framework Plugin
    snprintf (element_name, 127, "rtsp_qtimlelement-%d", i);
    rtsp_qtimlelement[i] = gst_element_factory_make (
        options->mlframework, element_name);
    if (!rtsp_qtimlelement[i]) {
      g_printerr ("Failed to create rtsp_qtimlelement-%d\n", i);
      goto error_clean_elements;
    }

    // Create post processing plugin
    snprintf (element_name, 127, "rtsp_qtimlpostprocess-%d", i);
    if (options->use_case == GST_OBJECT_DETECTION) {
      rtsp_qtimlpostprocess[i] = gst_element_factory_make (
          "qtimlvdetection", element_name);
    } else if (options->use_case == GST_CLASSIFICATION) {
      rtsp_qtimlpostprocess[i] = gst_element_factory_make (
          "qtimlvclassification", element_name);
    } else {
      g_printerr ("Invalid use case for rtsp_qtimlpostprocess-%d\n", i);
    }
    if (!rtsp_qtimlpostprocess[i]) {
      g_printerr ("Failed to create rtsp_qtimlpostprocess-%d\n", i);
      goto error_clean_elements;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "rtsp_detection_filter-%d", i);
    rtsp_detection_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!rtsp_detection_filter[i]) {
      g_printerr ("Failed to create rtsp_detection_filter-%d\n", i);
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue-%d\n", i);
      goto error_clean_elements;
    }
  }

  // Composer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }

  // Use capsfilter to define the composer output settings
  composer_caps = gst_element_factory_make ("capsfilter", "composer_caps");
  if (!composer_caps) {
    g_printerr ("Failed to create composer_caps\n");
    goto error_clean_elements;
  }

  composer_tee = gst_element_factory_make ("tee", "composer_tee");
  if (!composer_tee) {
    g_printerr ("Failed to create composer tee\n");
    goto error_clean_elements;
  }

  if (options->out_display) {
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

  if (options->out_file || options->out_rtsp) {
    // Create H.264 Encoder plugin for file and rtsp output
    if (options->input_type == GST_INPUT_STREAM_H264) {
      v4l2_encoder = gst_element_factory_make ("v4l2h264enc", "v4l2_encoder");
      if (!v4l2_encoder) {
        g_printerr ("Failed to create v4l2_encoder\n");
        goto error_clean_elements;
      }
    } else if (options->input_type == GST_INPUT_STREAM_H265) {
      v4l2_encoder = gst_element_factory_make ("v4l2h265enc", "v4l2_encoder");
      if (!v4l2_encoder) {
        g_printerr ("Failed to create v4l2_encoder\n");
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Failed to create v4l2_encoder, Invalid input-type\n");
      goto error_clean_elements;
    }

    enc_tee = gst_element_factory_make ("tee", "enc_tee");
    if (!enc_tee) {
      g_printerr ("Failed to create enc_tee\n");
      goto error_clean_elements;
    }

    if (options->out_file) {
      // Create mp4mux plugin to save file in mp4 container
      mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
      if (!mp4mux) {
        g_printerr ("Failed to create mp4mux\n");
        goto error_clean_elements;
      }

      // Create H.264 frame parser plugin
      if (options->input_type == GST_INPUT_STREAM_H264) {
        file_encoder_parse = gst_element_factory_make ("h264parse",
            "file_encoder_parse");
        if (!file_encoder_parse) {
          g_printerr ("Failed to create file_encoder_parse\n");
          goto error_clean_elements;
        }
      } else if (options->input_type == GST_INPUT_STREAM_H265) {
        file_encoder_parse = gst_element_factory_make ("h265parse",
            "file_encoder_parse");
        if (!file_encoder_parse) {
          g_printerr ("Failed to create file_encoder_parse\n");
          goto error_clean_elements;
        }
      } else {
        g_printerr ("Failed to create file_encoder_parse\n");
        goto error_clean_elements;
      }

      // Generic filesink plugin to write file on disk
      filesink = gst_element_factory_make ("filesink", "filesink");
      if (!filesink) {
        g_printerr ("Failed to create filesink\n");
        goto error_clean_elements;
      }
    }

    if (options->out_rtsp) {
      // Create H.264 frame parser plugin
      if (options->input_type == GST_INPUT_STREAM_H264) {
        rtsp_encoder_parse = gst_element_factory_make ("h264parse",
            "rtsp_encoder_parse");
        if (!rtsp_encoder_parse) {
          g_printerr ("Failed to create rtsp_encoder_parse\n");
          goto error_clean_elements;
        }
      } else if (options->input_type == GST_INPUT_STREAM_H265) {
        rtsp_encoder_parse = gst_element_factory_make ("h265parse",
            "rtsp_encoder_parse");
        if (!rtsp_encoder_parse) {
          g_printerr ("Failed to create rtsp_encoder_parse\n");
          goto error_clean_elements;
        }
      } else {
        g_printerr ("Failed to create rtsp_encoder_parse, Invalid input-type\n");
        goto error_clean_elements;
      }

      // Generic qtirtspbin plugin for streaming
      qtirtspbin = gst_element_factory_make ("qtirtspbin", "qtirtspbin");
      if (!qtirtspbin) {
        g_printerr ("Failed to create qtirtspbin\n");
        goto error_clean_elements;
      }
    }
  }

  // 2. Set properties for all GST plugin elements
  // 2.1 Settings for Source Plugin
  for (gint i = 0; i < options->num_camera; i++) {
    // Set user provided Camera ID
    g_object_set (G_OBJECT (camsrc[i]), "camera", options->camera_id + i,
        NULL);
    // Set the capabilities of camera plugin output
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    g_object_set (G_OBJECT (cam_caps[i]), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    if (!set_ml_params (cam_qtimlelement[i], cam_qtimlpostprocess[i],
        cam_detection_filter[i], options, i%htp_count)) {
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    g_object_set (G_OBJECT (filesrc[i]), "location",
        options->input_file_path[i], NULL);
    gst_element_set_enum_property (file_v4l2_decoder[i], "capture-io-mode",
        "dmabuf");
    gst_element_set_enum_property (file_v4l2_decoder[i], "output-io-mode",
        "dmabuf");
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (file_decode_caps[i]), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    if (!set_ml_params (file_qtimlelement[i], file_qtimlpostprocess[i],
        file_detection_filter[i], options, i%htp_count)) {
      goto error_clean_elements;
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    g_object_set (G_OBJECT (rtspsrc[i]), "location",
        options->input_rtsp_path[i], NULL);
    gst_element_set_enum_property (rtsp_v4l2_dec[i], "capture-io-mode",
        "dmabuf");
    gst_element_set_enum_property (rtsp_v4l2_dec[i], "output-io-mode",
        "dmabuf");
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (rtsp_decode_caps[i]), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    if (!set_ml_params (rtsp_qtimlelement[i], rtsp_qtimlpostprocess[i],
        rtsp_detection_filter[i], options, i%htp_count)) {
      goto error_clean_elements;
    }
  }

  // 2.4 Set the properties for composer output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12", NULL);
  g_object_set (G_OBJECT (composer_caps), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.5 Set the properties for Wayland compositor and fpsdisplay
  if (options->out_display) {
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);
    g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);

    g_object_set (G_OBJECT (fpsdisplaysink), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", TRUE,
        NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  }

  // 2.5 Set the properties for file/rtsp sink
  if (options->out_file || options->out_rtsp) {
    gst_element_set_enum_property (v4l2_encoder, "capture-io-mode",
        "dmabuf");
    gst_element_set_enum_property (v4l2_encoder, "output-io-mode",
        "dmabuf-import");
    // Set bitrate for streaming usecase
    fcontrols = gst_structure_from_string (
        "fcontrols,video_bitrate=6000000,video_bitrate_mode=0", NULL);
    g_object_set (G_OBJECT (v4l2_encoder), "extra-controls", fcontrols, NULL);

    if (options->out_file) {
      g_object_set (G_OBJECT (filesink), "location", options->out_file, NULL);
    }

    if (options->out_rtsp) {
      g_print (" ip = %s, port = %s\n", options->ip_address, options->port_num);
      g_object_set (G_OBJECT (rtsp_encoder_parse), "config-interval", 1, NULL);
      g_object_set (G_OBJECT (qtirtspbin), "address", options->ip_address,
          "port", options->port_num, NULL);
    }
  }

  // 3. Setup the pipeline
  g_print ("Add all elements to the pipeline...\n");

  for (gint i = 0; i < options->num_camera; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), camsrc[i], cam_caps[i],
        cam_tee[i], cam_qtimlvconverter[i], cam_qtimlelement[i],
        cam_qtimlpostprocess[i], cam_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), cam_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc[i], qtdemux[i],
        file_dec_parse[i], file_v4l2_decoder[i], file_decode_caps[i],
        file_dec_tee[i], file_qtimlvconverter[i], file_qtimlelement[i],
        file_qtimlpostprocess[i], file_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), file_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc[i], rtph_depay[i],
        rtsp_dec_parse[i], rtsp_v4l2_dec[i], rtsp_decode_caps[i],
        rtsp_dec_tee[i], rtsp_qtimlvconverter[i], rtsp_qtimlelement[i],
        rtsp_qtimlpostprocess[i], rtsp_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), rtsp_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtivcomposer,
      composer_caps, composer_tee, NULL);

  if (options->out_display) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), waylandsink,
        fpsdisplaysink, NULL);
  }

  if (options->out_file || options->out_rtsp) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2_encoder,
        enc_tee, NULL);
    if (options->out_file) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), mp4mux, file_encoder_parse,
          filesink, NULL);
    }
    if (options->out_rtsp) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), rtsp_encoder_parse,
          qtirtspbin, NULL);
    }
  }

  g_print ("Link elements...\n");

  // Create Pipeline for Object Detection
  for (gint i = 0; i < options->num_camera; i++) {
    ret = gst_element_link_many (camsrc[i], cam_caps[i], cam_queue[i][0],
        cam_tee[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " camsrc -> cam_tee.\n", i);
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (cam_tee[i], cam_queue[i][1], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " cam_tee -> qtivcomposer.\n", i);
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (cam_tee[i], cam_queue[i][2],
        cam_qtimlvconverter[i], cam_queue[i][3], cam_qtimlelement[i],
        cam_queue[i][4], cam_qtimlpostprocess[i], cam_detection_filter[i],
        cam_queue[i][5], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " cam: pre proc -> ml framework -> post proc -> composer.\n", i);
      goto error_clean_pipeline;
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    ret = gst_element_link_many (filesrc[i], qtdemux[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " filesrc -> qtdemux.\n", i);
      goto error_clean_pipeline;
    }
    // qtdemux -> file_queue[i][0] link is not created here as it is a
    // dymanic link using on_pad_added callback
    ret = gst_element_link_many (file_queue[i][0], file_dec_parse[i],
        file_v4l2_decoder[i], file_decode_caps[i],
        file_queue[i][1], file_dec_tee[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " file_queue -> file_dec_tee.\n", i);
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (file_dec_tee[i], file_queue[i][2], qtivcomposer,
        NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          "file_dec_tee -> qtivcomposer.\n", i);
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (file_dec_tee[i], file_queue[i][3],
        file_qtimlvconverter[i], file_queue[i][4], file_qtimlelement[i],
        file_queue[i][5], file_qtimlpostprocess[i], file_detection_filter[i],
        file_queue[i][6], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " file: pre proc -> ml framework -> post proc -> composer.\n", i);
      goto error_clean_pipeline;
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    // rtspsrc -> rtsp_queue[i][0] link is not created here as it is a
    // dymanic link using on_pad_added callback
    ret = gst_element_link_many (rtsp_queue[i][0], rtph_depay[i],
        rtsp_dec_parse[i], rtsp_v4l2_dec[i], rtsp_decode_caps[i],
        rtsp_queue[i][1], rtsp_dec_tee[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " rtsp_queue -> rtsp_tee.\n", i);
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (rtsp_dec_tee[i], rtsp_queue[i][2], qtivcomposer,
        NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " rtsp_tee -> qtivcomposer.\n", i);
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (rtsp_dec_tee[i], rtsp_queue[i][3],
        rtsp_qtimlvconverter[i], rtsp_queue[i][4], rtsp_qtimlelement[i],
        rtsp_queue[i][5], rtsp_qtimlpostprocess[i], rtsp_detection_filter[i],
        rtsp_queue[i][6], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " rtsp: pre proc -> ml framework -> post proc -> composer.\n", i);
      goto error_clean_pipeline;
    }
  }

  ret = gst_element_link_many (
      qtivcomposer, queue[0], composer_caps, composer_tee, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        " qtivcomposer -> composer_tee.\n");
    goto error_clean_pipeline;
  }

  if (options->out_display) {
    ret = gst_element_link_many (composer_tee, queue[1], fpsdisplaysink, NULL);
    if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        " composer_tee -> waylandsink.\n");
      goto error_clean_pipeline;
    }
  }

  if (options->out_file || options->out_rtsp) {
    ret = gst_element_link_many (composer_tee, queue[2], v4l2_encoder, queue[3],
        enc_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " composer_tee -> encoder -> enc_tee.\n");
      goto error_clean_pipeline;
    }

    if (options->out_file) {
      ret = gst_element_link_many (enc_tee, queue[4], file_encoder_parse,
          mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " enc_tee -> mp4mux -> filesink.\n");
        goto error_clean_pipeline;
      }
    }

    if (options->out_rtsp) {
      ret = gst_element_link_many (enc_tee, queue[5], rtsp_encoder_parse, queue[6],
          qtirtspbin, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " enc_tee -> qtirtspbin.\n");
        goto error_clean_pipeline;
      }
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    g_signal_connect (qtdemux[i], "pad-added", G_CALLBACK (on_pad_added),
        file_queue[i][0]);
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    g_signal_connect (rtspsrc[i], "pad-added", G_CALLBACK (on_pad_added),
        rtsp_queue[i][0]);
  }

  if (!set_composer_params (qtivcomposer, options)) {
    g_printerr ("failed to set composer params.\n");
    goto error_clean_pipeline;
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  for (gint i = 0; i < options->num_camera; i++) {
    cleanup_gst (&camsrc[i], &cam_caps[i],
        &cam_tee[i], &cam_qtimlvconverter[i], &cam_qtimlelement[i],
        &cam_qtimlpostprocess[i], &cam_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      cleanup_gst (&cam_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    cleanup_gst (&filesrc[i], &qtdemux[i],
        &file_dec_parse[i], &file_v4l2_decoder[i], &file_dec_tee[i],
        &file_qtimlvconverter[i], &file_qtimlelement[i], &file_decode_caps[i],
        &file_qtimlpostprocess[i], &file_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      cleanup_gst (&file_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    cleanup_gst (&rtspsrc[i], &rtph_depay[i],
        &rtsp_dec_parse[i], &rtsp_v4l2_dec[i], &rtsp_dec_tee[i],
        &rtsp_qtimlvconverter[i], &rtsp_qtimlelement[i], &rtsp_decode_caps[i],
         &rtsp_qtimlpostprocess[i], &rtsp_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      cleanup_gst (&rtsp_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    cleanup_gst (&queue[i], NULL);
  }

  cleanup_gst (&qtivcomposer,
      &composer_caps, &composer_tee, NULL);

  if (options->out_display) {
    cleanup_gst (&waylandsink, NULL);
  }

  if (options->out_file || options->out_rtsp) {
    cleanup_gst (&v4l2_encoder, &enc_tee, NULL);
    if (options->out_file) {
      cleanup_gst (&mp4mux, &file_encoder_parse, &filesink, NULL);
    }
    if (options->out_rtsp) {
      cleanup_gst (&rtsp_encoder_parse, &qtirtspbin, NULL);
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
  JsonArray *files_info = NULL;
  JsonArray *rtsp_info = NULL;

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
    if (json_object_has_member (root_obj, "num-camera"))
      options->num_camera = json_object_get_int_member (root_obj, "num-camera");

    if (json_object_has_member (root_obj, "camera-id"))
      options->camera_id = json_object_get_int_member (root_obj, "camera-id");
  }

  if (json_object_has_member (root_obj, "input-file-path")) {
    files_info = json_object_get_array_member (root_obj, "input-file-path");
    options->num_file = json_array_get_length (files_info);
    if (options->num_file > MAX_FILESRCS) {
      gst_printerr ("Number of input files has to be <= %d\n", MAX_FILESRCS);
      g_object_unref (parser);
      return -1;
    }
    for (gint i = 0; i < options->num_file; i++) {
      options->input_file_path[i] =
          g_strdup (json_array_get_string_element (files_info, i));
    }
  }

  if (json_object_has_member (root_obj, "input-rtsp-path")) {
    rtsp_info = json_object_get_array_member (root_obj, "input-rtsp-path");
    options->num_rtsp = json_array_get_length (rtsp_info);
    if (options->num_rtsp > MAX_RTSPSRCS) {
      gst_printerr ("Number of rtsp sources has to be <= %d\n", MAX_RTSPSRCS);
      g_object_unref (parser);
      return -1;
    }
    for (gint i = 0; i < options->num_rtsp; i++) {
      options->input_rtsp_path[i] =
          g_strdup (json_array_get_string_element (rtsp_info, i));
    }
  }

  if (json_object_has_member (root_obj, "input-type")) {
    const gchar* input_type =
        json_object_get_string_member (root_obj, "input-type");
    if (g_strcmp0 (input_type, "h264") == 0)
      options->input_type = GST_INPUT_STREAM_H264;
    else if (g_strcmp0 (input_type, "h265") == 0)
      options->input_type = GST_INPUT_STREAM_H265;
    else {
      gst_printerr ("Input stream type can only be one of "
          "\"h264\" or \"h265\" \n");
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

  if (json_object_has_member (root_obj, "output-file-path")) {
    options->out_file =
        g_strdup (json_object_get_string_member (root_obj, "output-file-path"));
  }

  if (json_object_has_member (root_obj, "output-ip-address")) {
    options->out_rtsp = TRUE;
    options->ip_address =
        g_strdup (json_object_get_string_member (root_obj, "output-ip-address"));
  }

  if (json_object_has_member (root_obj, "output-port-number")) {
    options->out_rtsp = TRUE;
    options->port_num =
        g_strdup (json_object_get_string_member (root_obj, "output-port-number"));
  }

  if (json_object_has_member (root_obj, "output-display")) {
    options->out_display =
        json_object_get_boolean_member (root_obj, "output-display");
  }

  if (json_object_has_member (root_obj, "use-case")) {
    options->use_case =
        json_object_get_int_member (root_obj, "use-case");
  }

  g_object_unref (parser);
  return 0;
}

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *app_name = NULL;
  gchar *config_file = NULL;
  GstAppContext appctx = {};
  GstAppOptions options = {};
  struct rlimit rl;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  gchar help_description[4096];
  gint htp_count = 1;

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

  //Set default IP and Port
  options.ip_address = DEFAULT_IP;
  options.port_num = DEFAULT_PORT;
  options.use_case = GST_OBJECT_DETECTION;
  options.mlframework = "qtimltflite";
  options.camera_id = -1;
  options.input_type = GST_INPUT_STREAM_H264;

  gboolean camera_is_available = is_camera_available ();

  htp_count = get_num_cdsp_backends();
  g_print ("HTP Core Count = %d\n", htp_count);

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

  gchar camera_description[256] = {};

  if (camera_is_available) {
    snprintf (camera_description, sizeof (camera_description),
      "  num-camera: 1 or 2\n"
      "      Number of camera streams (max: %d)\n"
      "  camera-id: 0 or 1\n"
      "      Use provided camera id as source\n"
      "      Default input camera=0 if no other input is selected\n"
      "      This parameter is ignored if num-camera=%d\n",
      MAX_CAMSRCS, MAX_CAMSRCS);
  }

  snprintf (help_description, 4095, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates Object Detection on upto 32 stream "
      "with various input/output stream combinations\n"
      "\nConfig file fields:\n"
      "%s"
      "  input-file-path: <json array>\n"
      "      json array of input files. Eg:\n"
      "      [\"/etc/media/video1.mp4\", \"/etc/media/video2.mp4\"]\n"
      "      max number of input files: %d\n"
      "  input-rtsp-path: <json array>\n"
      "      json array of input rtsp streams. Eg:\n"
      "      [\"rtsp://127.0.0.1:8554/live1.mkv\", "
      "\"rtsp://127.0.0.1:8554/live2.mkv\"]\n"
      "      max number of rtsp input streams: %d\n"
      "  Maximum number of input streams: %d\n"
      "  model: path to model file\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default detection model path: " DEFAULT_TFLITE_YOLOV8_MODEL "\n"
      "      Default classification model path: "
      DEFAULT_TFLITE_INCEPTIONV3_MODEL "\n"
      "  labels: path to labels file\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default detection labels path: " DEFAULT_YOLOV8_LABELS "\n"
      "      Default classification model path: "
      DEFAULT_CLASSIFICATION_LABELS "\n"
      "  constants: \"CONSTANTS\"\n"
      "      Constants, offsets and coefficients used by the chosen module "
      "      for post-processing of incoming tensors.\n"
      "      Applicable only for some modules\n"
      "      Default detection constants: " DEFAULT_DETECTION_CONSTANTS "\n"
      "      Default classification path: "
      DEFAULT_CLASSIFICATION_CONSTANTS "\n"
      "  output-file-path: /PATH\n"
      "      Path to save H.264 Encoded file\n"
      "  output-ip-address: valid IP address\n"
      "      RTSP server listening address.\n"
      "      default IP address: " DEFAULT_IP "\n"
      "  output-port-number: \"port number\"\n"
      "      RTSP server listening port number.\n"
      "      default port number: " DEFAULT_PORT "\n"
      "  adding either output-ip-address or output-port-number or both\n"
      "  enables output through rtsp stream\n"
      "  output-display: boolean\n"
      "      Put value as true to enable output on wayland display\n"
      "  If no output is selected, wayland output is selected as default\n"
      "  use-case: 0 or 1\n"
      "      0: detection, 1: classification\n"
      "  input-type: \"h264\" or \"h265\"\n"
      "      Encoding type for file/rtsp input\n"
      "      Default encoding: H264\n",
      app_name, DEFAULT_CONFIG_FILE, camera_description,
      MAX_FILESRCS, MAX_RTSPSRCS, MAX_SRCS_COUNT);
  help_description[4095] = '\0';

  // Parse command line entries.
  if ((ctx = g_option_context_new (help_description)) != NULL) {
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
      gst_app_context_free (&appctx, &options, config_file);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (&appctx, &options, config_file);
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
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (parse_json (config_file, &options) != 0) {
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if ((options.use_case != GST_OBJECT_DETECTION) &&
      (options.use_case != GST_CLASSIFICATION)) {
    g_printerr ("Invalid usecase selected, Select Detection or Classification\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_case == GST_OBJECT_DETECTION) {
    if (options.model_path ==  NULL)
      options.model_path = DEFAULT_TFLITE_YOLOV8_MODEL;
    if (options.labels_path ==  NULL)
      options.labels_path = DEFAULT_YOLOV8_LABELS;
    if (options.constants ==  NULL)
      options.constants = DEFAULT_DETECTION_CONSTANTS;
  }

  g_print ("model_path=%s labels_path=%s\n",
      options.model_path, options.labels_path);

  if (options.use_case == GST_CLASSIFICATION) {
    if (options.model_path ==  NULL)
      options.model_path = DEFAULT_TFLITE_INCEPTIONV3_MODEL;
    if (options.labels_path ==  NULL)
      options.labels_path = DEFAULT_CLASSIFICATION_LABELS;
    if (options.constants ==  NULL)
      options.constants = DEFAULT_CLASSIFICATION_CONSTANTS;
  }

  if (options.num_camera > MAX_CAMSRCS) {
    g_printerr ("Number of camera streams cannot be more than 2\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  if (options.num_file > MAX_FILESRCS) {
    g_printerr ("Number of file streams cannot be more than %d\n", MAX_FILESRCS);
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  if (options.num_rtsp > MAX_RTSPSRCS) {
    g_printerr ("Number of rtsp streams cannot be more than %d\n", MAX_RTSPSRCS);
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  if (options.num_camera < 0 || options.num_file < 0 || options.num_rtsp < 0) {
    g_printerr ("Negative count for any input is not supported\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  options.input_count = options.num_camera + options.num_file + options.num_rtsp;

  if (options.input_count > MAX_SRCS_COUNT) {
    g_printerr ("Maximum supported streams: %d\n", MAX_SRCS_COUNT);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.camera_id < -1 || options.camera_id > 1) {
    g_printerr ("invalid camera id: %d\n", options.camera_id);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.input_count == 0 ||
      (options.camera_id != -1 && options.num_camera == 0)) {
    if (camera_is_available) {
      g_print ("No stream provided in options, defaulting to 1 camera stream.\n");
      options.num_camera = 1;
      options.input_count++;
    } else {
      g_printerr ("Select either File or RTSP source\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if (options.camera_id == -1 || options.num_camera == 2) {
    options.camera_id = 0;
  }

  if (!options.out_display && (NULL == options.out_file) && !options.out_rtsp) {
    g_print ("No sink option provided, defaulting to display sink.\n");
    options.out_display = TRUE;
  }

  for (gint i = 0; i < options.num_file; i++) {
    if (!file_exists (options.input_file_path[i])) {
      g_printerr ("video file does not exist at path: %s\n",
          options.input_file_path[i]);
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if (!file_exists (options.model_path)) {
    g_printerr ("Invalid model file path: %s\n", options.model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.labels_path)) {
    g_printerr ("Invalid labels file path: %s\n", options.labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.out_file && !file_location_exists (options.out_file)) {
    g_printerr ("Invalid output file location: %s\n", options.out_file);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  g_print ("Run app with model: %s and labels: %s and use case: %s\n",
      options.model_path, options.labels_path,
      (options.use_case ? "Classification" : "Detection"));

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
  ret = create_pipe (&appctx, &options, htp_count);
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
