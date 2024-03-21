/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Object Detection on Live stream.
 *
 * Description:
 * The application takes video stream from camera/file/rtsp and maximum of
 * 9 streams in parallel and give to Yolo models for object detection and
 * AI Model output (Labels & Bounding Boxes) overlayed on incoming videos
 * are arranged in a grid pattern to be displayed on HDMI Screen, save as
 * h264 encoded mp4 file or streamed over rtsp server running on device.
 * Any combination of inputs and outputs can be configured with commandline
 * options. Camera default resolution is set to 1280x720. Display will be
 * full screen for 1 input stream, divided into 2x2 grid for 2-4 input streams
 * and divided into 3x3 grid for 5-9 streams.
 *
 * Pipeline for Gstreamer:
 * Source -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> Sink
 *     Source: qmmfsrc (Camera)/filesrc/rtspsrc
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post process: qtimlvdetection -> detection_filter
 *     Sink: waylandsink (Display)/filesink/rtsp server
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>

#include <sys/resource.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "include/gst_sample_apps_utils.h"

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_TFLITE_YOLOV5_MODEL "/opt/yolov5.tflite"
#define DEFAULT_YOLOV5_LABELS "/opt/yolov5.labels"

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
#define MAX_FILESRCS 6
#define MAX_RTSPSRCS 6
#define MAX_SRCS_COUNT 6

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 7

/**
 * Structure for various application specific options
 */
typedef struct {
  const gchar *rtsp_ip_port;
  const gchar *mlframework;
  const gchar *model_path;
  const gchar *labels_path;
  const gchar *out_file;
  gint num_camera;
  gint num_file;
  gint num_rtsp;
  gint camera_id;
  gint input_count;
  gboolean out_display;
  gboolean out_rtsp;
} GstAppOptions;

/**
 * Static grid points to display multiple input stream
 *
 * positions_1: Rect for single input
 * positions_4: Rects for input count between 2 to 4 (2x2 Grid)
 * positions_9: Rects for input count between 5 to 9 (3x3 Grid)
 */
static GstVideoRectangle positions_1[1] = {
    {0, 0, 1920, 1080}};
static GstVideoRectangle positions_4[4] = {
    {0, 0, 960, 540}, {960, 0, 960, 540},
    {0, 540, 960, 540}, {960, 540, 960, 540}};
static GstVideoRectangle positions_9[9] = {
    {0, 0, 640, 360}, {640, 0, 640, 360}, {1280, 0, 640, 360},
    {0, 360, 640, 360}, {640, 360, 640, 360}, {1280, 360, 640, 360},
    {0, 720, 640, 360}, {640, 720, 640, 360}, {1280, 720, 640, 360}};

/**
 * Set parameters for ML Framework Elements.
 *
 * @param qtimlelement ML Framework Plugin.
 * @param qtimlvdetection ML Detection Plugin.
 * @param detection_filter Caps Filter Plugin for ML Detection Plugin.
 * @param options Application specific options.
 */
static gboolean
set_ml_params (GstElement * qtimlelement, GstElement * qtimlvdetection,
    GstElement * detection_filter, GstAppOptions * options)
{
  GstStructure *delegate_options;
  GstCaps *pad_filter;
  gint module_id;

  // Set delegate and model for AI framework
  delegate_options = gst_structure_from_string (
      "QNNExternalDelegate,backend_type=htp;", NULL);
  g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
      "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
  g_object_set (G_OBJECT (qtimlelement),
      "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
  g_object_set (G_OBJECT (qtimlelement),
      "external-delegate-options", delegate_options, NULL);
  gst_structure_free (delegate_options);

  // Set properties for ML postproc plugins- labels, module, threshold & constants
  g_object_set (G_OBJECT (qtimlvdetection), "labels", options->labels_path, NULL);

  module_id = get_enum_value (qtimlvdetection, "module", "yolov5");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
  } else {
    g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (qtimlvdetection), "threshold", 50.0, "results", 10,
      "constants", "YoloV5,q-offsets=<3.0>,q-scales=<0.005047998391091824>;",
      NULL);

  // Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);
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
  GstVideoRectangle *positions = NULL;
  gchar element_name[128];

  // Choose Window grid based on number of input streams
  if (options->input_count == 1) {
    positions = positions_1;
  } else if (options->input_count <= 4) {
    positions = positions_4;
  } else {
    positions = positions_9;
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
    for (gint j = 0; j < 2; j++) {
      snprintf (element_name, 127, "sink_%d", (i*2 + j));
      composer_sink[j] = gst_element_get_static_pad (qtivcomposer, element_name);
      if (!composer_sink[j]) {
        g_printerr ("Sink pad %d of vcomposer couldn't be retrieved\n",
            (i*2 + j));
        return FALSE;
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
    g_object_set_property (G_OBJECT (composer_sink[1]),
        "dimensions", &dimension);
    g_value_unset (&position);
    g_value_unset (&dimension);
    gst_object_unref (composer_sink[0]);
    gst_object_unref (composer_sink[1]);
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
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");

  // Link the source pad to the sink pad
  gst_pad_link (pad, sinkpad);
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
  // Elements for camera source
  GstElement *camsrc[options->num_camera], *cam_caps[options->num_camera];
  GstElement *cam_queue[options->num_camera][QUEUE_COUNT];
  GstElement *cam_tee[options->num_camera];
  GstElement *cam_qtimlvconverter[options->num_camera];
  GstElement *cam_qtimlelement[options->num_camera];
  GstElement *cam_qtimlvdetection[options->num_camera];
  GstElement *cam_detection_filter[options->num_camera];
  // Elements for file source
  GstElement *filesrc[options->num_file], *qtdemux[options->num_file];
  GstElement *file_queue[options->num_file][QUEUE_COUNT];
  GstElement *file_dec_h264parse[options->num_file];
  GstElement *file_v4l2h264dec[options->num_file];
  GstElement *file_dec_tee[options->num_file];
  GstElement *file_qtimlvconverter[options->num_file];
  GstElement *file_qtimlelement[options->num_file];
  GstElement *file_qtimlvdetection[options->num_file];
  GstElement *file_detection_filter[options->num_file];
  // Elements for rtsp source
  GstElement *rtspsrc[options->num_rtsp], *rtph264depay[options->num_rtsp];
  GstElement *rtsp_queue[options->num_rtsp][QUEUE_COUNT];
  GstElement *rtsp_dec_h264parse[options->num_rtsp];
  GstElement *rtsp_v4l2h264dec[options->num_rtsp];
  GstElement *rtsp_dec_tee[options->num_rtsp];
  GstElement *rtsp_qtimlvconverter[options->num_rtsp];
  GstElement *rtsp_qtimlelement[options->num_rtsp];
  GstElement *rtsp_qtimlvdetection[options->num_rtsp];
  GstElement *rtsp_detection_filter[options->num_rtsp];
  // Elements for sinks
  GstElement *queue[QUEUE_COUNT], *qtivcomposer, *composer_caps, *composer_tee;
  GstElement *waylandsink;
  GstElement *v4l2h264enc, *enc_h264parse, *enc_tee;
  GstElement *mp4mux, *filesink;
  GstElement *rtph264pay, *udpsink;
  GstCaps *filtercaps;
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gchar element_name[128];
  gboolean ret = FALSE;

  g_print ("IN Options: camera: %d (id: %d), file: %d, rtsp: %d (%s)\n",
      options->num_camera, options->camera_id, options->num_file,
      options->num_rtsp, options->rtsp_ip_port);
  g_print ("OUT Options: display: %d, file: %s, rtsp: %d\n",
      options->out_display, options->out_file, options->out_rtsp);

  // 1. Create the elements or Plugins
  for (gint i = 0; i < options->num_camera; i++) {
    // Create qtiqmmfsrc plugin for camera stream
    snprintf (element_name, 127, "camsrc-%d", i);
    camsrc[i] = gst_element_factory_make ("qtiqmmfsrc", element_name);
    if (!camsrc[i]) {
      g_printerr ("Failed to create camsrc-%d\n", i);
      return FALSE;
    }

    // Use capsfilter to define the camera output settings
    snprintf (element_name, 127, "cam_caps-%d", i);
    cam_caps[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!cam_caps[i]) {
      g_printerr ("Failed to create cam_caps-%d\n", i);
      return FALSE;
    }

    for (gint j=0; j < QUEUE_COUNT; j++) {
      snprintf (element_name, 127, "cam_queue-%d-%d", i, j);
      cam_queue[i][j] = gst_element_factory_make ("queue", element_name);
      if (!cam_queue[i][j]) {
        g_printerr ("Failed to create cam_queue-%d-%d\n", i, j);
        return FALSE;
      }
    }

    snprintf (element_name, 127, "cam_tee-%d", i);
    cam_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!cam_tee[i]) {
      g_printerr ("Failed to create cam_tee-%d\n", i);
      return FALSE;
    }

    // Create pre processing plugin
    snprintf (element_name, 127, "cam_qtimlvconverter-%d", i);
    cam_qtimlvconverter[i] = gst_element_factory_make (
        "qtimlvconverter", element_name);
    if (!cam_qtimlvconverter[i]) {
      g_printerr ("Failed to create cam_qtimlvconverter-%d\n", i);
      return FALSE;
    }

    // Create ML Framework Plugin
    snprintf (element_name, 127, "cam_qtimlelement-%d", i);
    cam_qtimlelement[i] = gst_element_factory_make (
        options->mlframework, element_name);
    if (!cam_qtimlelement[i]) {
      g_printerr ("Failed to create cam_qtimlelement-%d\n", i);
      return FALSE;
    }

    // Create post processing plugin
    snprintf (element_name, 127, "cam_qtimlvdetection-%d", i);
    cam_qtimlvdetection[i] = gst_element_factory_make (
        "qtimlvdetection", element_name);
    if (!cam_qtimlvdetection[i]) {
      g_printerr ("Failed to create cam_qtimlvdetection-%d\n", i);
      return FALSE;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "cam_detection_filter-%d", i);
    cam_detection_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!cam_detection_filter[i]) {
      g_printerr ("Failed to create cam_detection_filter-%d\n", i);
      return FALSE;
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    // Create filesrc plugin for file input
    snprintf (element_name, 127, "filesrc-%d", i);
    filesrc[i] = gst_element_factory_make ("filesrc", element_name);
    if (!filesrc[i]) {
      g_printerr ("Failed to create filesrc-%d\n", i);
      return FALSE;
    }

    // Create demuxer for video container parsing
    snprintf (element_name, 127, "qtdemux-%d", i);
    qtdemux[i] = gst_element_factory_make ("qtdemux", element_name);
    if (!qtdemux[i]) {
      g_printerr ("Failed to create qtdemux-%d\n", i);
      return FALSE;
    }

    for (gint j=0; j < QUEUE_COUNT; j++) {
      snprintf (element_name, 127, "file_queue-%d-%d", i, j);
      file_queue[i][j] = gst_element_factory_make ("queue", element_name);
      if (!file_queue[i][j]) {
        g_printerr ("Failed to create file_queue-%d-%d\n", i, j);
        return FALSE;
      }
    }

    // Create H.264 frame parser plugin
    snprintf (element_name, 127, "file_dec_h264parse-%d", i);
    file_dec_h264parse[i] = gst_element_factory_make ("h264parse", element_name);
    if (!file_dec_h264parse[i]) {
      g_printerr ("Failed to create file_dec_h264parse-%d\n", i);
      return FALSE;
    }

    // Create H.264 Decoder Plugin
    snprintf (element_name, 127, "file_v4l2h264dec-%d", i);
    file_v4l2h264dec[i] = gst_element_factory_make ("v4l2h264dec", element_name);
    if (!file_v4l2h264dec[i]) {
      g_printerr ("Failed to create file_v4l2h264dec-%d\n", i);
      return FALSE;
    }

    snprintf (element_name, 127, "file_dec_tee-%d", i);
    file_dec_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!file_dec_tee[i]) {
      g_printerr ("Failed to create file_dec_tee-%d\n", i);
      return FALSE;
    }

    // Create pre processing plugin
    snprintf (element_name, 127, "file_qtimlvconverter-%d", i);
    file_qtimlvconverter[i] = gst_element_factory_make (
        "qtimlvconverter", element_name);
    if (!file_qtimlvconverter[i]) {
      g_printerr ("Failed to create file_qtimlvconverter-%d\n", i);
      return FALSE;
    }

    // Create ML Framework Plugin
    snprintf (element_name, 127, "file_qtimlelement-%d", i);
    file_qtimlelement[i] = gst_element_factory_make (
        options->mlframework, element_name);
    if (!file_qtimlelement[i]) {
      g_printerr ("Failed to create file_qtimlelement-%d\n", i);
      return FALSE;
    }

    // Create post processing plugin
    snprintf (element_name, 127, "file_qtimlvdetection-%d", i);
    file_qtimlvdetection[i] = gst_element_factory_make (
        "qtimlvdetection", element_name);
    if (!file_qtimlvdetection[i]) {
      g_printerr ("Failed to create file_qtimlvdetection-%d\n", i);
      return FALSE;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "file_detection_filter-%d", i);
    file_detection_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!file_detection_filter[i]) {
      g_printerr ("Failed to create file_detection_filter-%d\n", i);
      return FALSE;
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    // Create rtspsrc plugin for rtsp input
    snprintf (element_name, 127, "rtspsrc-%d", i);
    rtspsrc[i] = gst_element_factory_make ("rtspsrc", element_name);
    if (!rtspsrc[i]) {
      g_printerr ("Failed to create rtspsrc-%d\n", i);
      return FALSE;
    }

    // Create rtph264depay plugin for rtsp payload parsing
    snprintf (element_name, 127, "rtph264depay-%d", i);
    rtph264depay[i] = gst_element_factory_make ("rtph264depay", element_name);
    if (!rtph264depay[i]) {
      g_printerr ("Failed to create rtph264depay-%d\n", i);
      return FALSE;
    }

    for (gint j=0; j < QUEUE_COUNT; j++) {
      snprintf (element_name, 127, "rtsp_queue-%d-%d", i, j);
      rtsp_queue[i][j] = gst_element_factory_make ("queue", element_name);
      if (!rtsp_queue[i][j]) {
        g_printerr ("Failed to create rtsp_queue-%d-%d\n", i, j);
        return FALSE;
      }
    }

    // Create H.264 frame parser plugin
    snprintf (element_name, 127, "rtsp_dec_h264parse-%d", i);
    rtsp_dec_h264parse[i] = gst_element_factory_make ("h264parse", element_name);
    if (!rtsp_dec_h264parse[i]) {
      g_printerr ("Failed to create rtsp_dec_h264parse-%d\n", i);
      return FALSE;
    }

    // Create H.264 Decoder Plugin
    snprintf (element_name, 127, "rtsp_v4l2h264dec-%d", i);
    rtsp_v4l2h264dec[i] = gst_element_factory_make ("v4l2h264dec", element_name);
    if (!rtsp_v4l2h264dec[i]) {
      g_printerr ("Failed to create rtsp_v4l2h264dec-%d\n", i);
      return FALSE;
    }

    snprintf (element_name, 127, "rtsp_dec_tee-%d", i);
    rtsp_dec_tee[i] = gst_element_factory_make ("tee", element_name);
    if (!rtsp_dec_tee[i]) {
      g_printerr ("Failed to create rtsp_dec_tee-%d\n", i);
      return FALSE;
    }

    // Create pre processing plugin
    snprintf (element_name, 127, "rtsp_qtimlvconverter-%d", i);
    rtsp_qtimlvconverter[i] = gst_element_factory_make (
        "qtimlvconverter", element_name);
    if (!rtsp_qtimlvconverter[i]) {
      g_printerr ("Failed to create rtsp_qtimlvconverter-%d\n", i);
      return FALSE;
    }

    // Create ML Framework Plugin
    snprintf (element_name, 127, "rtsp_qtimlelement-%d", i);
    rtsp_qtimlelement[i] = gst_element_factory_make (
        options->mlframework, element_name);
    if (!rtsp_qtimlelement[i]) {
      g_printerr ("Failed to create rtsp_qtimlelement-%d\n", i);
      return FALSE;
    }

    // Create post processing plugin
    snprintf (element_name, 127, "rtsp_qtimlvdetection-%d", i);
    rtsp_qtimlvdetection[i] = gst_element_factory_make (
        "qtimlvdetection", element_name);
    if (!rtsp_qtimlvdetection[i]) {
      g_printerr ("Failed to create rtsp_qtimlvdetection-%d\n", i);
      return FALSE;
    }

    // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "rtsp_detection_filter-%d", i);
    rtsp_detection_filter[i] = gst_element_factory_make (
        "capsfilter", element_name);
    if (!rtsp_detection_filter[i]) {
      g_printerr ("Failed to create rtsp_detection_filter-%d\n", i);
      return FALSE;
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue-%d\n", i);
      return FALSE;
    }
  }

  // Composer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    return FALSE;
  }

  // Use capsfilter to define the composer output settings
  composer_caps = gst_element_factory_make ("capsfilter", "composer_caps");
  if (!composer_caps) {
    g_printerr ("Failed to create composer_caps\n");
    return FALSE;
  }

  composer_tee = gst_element_factory_make ("tee", "composer_tee");
  if (!composer_tee) {
    g_printerr ("Failed to create composer tee\n");
    return FALSE;
  }

  if (options->out_display) {
    // Create Weston compositor to render the output on Display
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("Failed to create waylandsink \n");
      return FALSE;
    }
  }

  if (options->out_file || options->out_rtsp) {
    // Create H.264 Encoder plugin for file and rtsp output
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      return FALSE;
    }

    // Create H.264 frame parser plugin
    enc_h264parse = gst_element_factory_make ("h264parse", "enc_h264parse");
    if (!enc_h264parse) {
      g_printerr ("Failed to create enc_h264parse\n");
      return FALSE;
    }

    enc_tee = gst_element_factory_make ("tee", "enc_tee");
    if (!enc_tee) {
      g_printerr ("Failed to create enc_tee\n");
      return FALSE;
    }

    if (options->out_file) {
      // Create mp4mux plugin to save file in mp4 container
      mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
      if (!mp4mux) {
        g_printerr ("Failed to create mp4mux\n");
        return FALSE;
      }

      // Generic filesink plugin to write file on disk
      filesink = gst_element_factory_make ("filesink", "filesink");
      if (!filesink) {
        g_printerr ("Failed to create filesink\n");
        return FALSE;
      }
    }

    if (options->out_rtsp) {
      // Plugin to create rtsp payload to stream over network
      rtph264pay = gst_element_factory_make ("rtph264pay", "rtph264pay");
      if (!rtph264pay) {
        g_printerr ("Failed to create rtph264pay\n");
        return FALSE;
      }

      // Generic udpsink plugin for streaming
      udpsink = gst_element_factory_make ("udpsink", "udpsink");
      if (!udpsink) {
        g_printerr ("Failed to create udpsink\n");
        return FALSE;
      }
    }
  }

  // 1.1 Append all elements in a list for cleanup
  appctx->plugins = NULL;
  for (gint i = 0; i < options->num_camera; i++) {
    appctx->plugins = g_list_append (appctx->plugins, camsrc[i]);
    appctx->plugins = g_list_append (appctx->plugins, cam_caps[i]);
    appctx->plugins = g_list_append (appctx->plugins, cam_tee[i]);
    appctx->plugins = g_list_append (appctx->plugins, cam_qtimlvconverter[i]);
    appctx->plugins = g_list_append (appctx->plugins, cam_qtimlelement[i]);
    appctx->plugins = g_list_append (appctx->plugins, cam_qtimlvdetection[i]);
    appctx->plugins = g_list_append (appctx->plugins, cam_detection_filter[i]);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      appctx->plugins = g_list_append (appctx->plugins, cam_queue[i][j]);
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    appctx->plugins = g_list_append (appctx->plugins, filesrc[i]);
    appctx->plugins = g_list_append (appctx->plugins, qtdemux[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_dec_h264parse[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_v4l2h264dec[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_dec_tee[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_qtimlvconverter[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_qtimlelement[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_qtimlvdetection[i]);
    appctx->plugins = g_list_append (appctx->plugins, file_detection_filter[i]);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      appctx->plugins = g_list_append (appctx->plugins, file_queue[i][j]);
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    appctx->plugins = g_list_append (appctx->plugins, rtspsrc[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtph264depay[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_dec_h264parse[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_v4l2h264dec[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_dec_tee[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_qtimlvconverter[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_qtimlelement[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_qtimlvdetection[i]);
    appctx->plugins = g_list_append (appctx->plugins, rtsp_detection_filter[i]);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      appctx->plugins = g_list_append (appctx->plugins, rtsp_queue[i][j]);
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    appctx->plugins = g_list_append (appctx->plugins, queue[i]);
  }
  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
  appctx->plugins = g_list_append (appctx->plugins, composer_caps);
  appctx->plugins = g_list_append (appctx->plugins, composer_tee);

  if (options->out_display) {
    appctx->plugins = g_list_append (appctx->plugins, waylandsink);
  }

  if (options->out_file || options->out_rtsp) {
    appctx->plugins = g_list_append (appctx->plugins, v4l2h264enc);
    appctx->plugins = g_list_append (appctx->plugins, enc_h264parse);
    appctx->plugins = g_list_append (appctx->plugins, enc_tee);
    if (options->out_file) {
      appctx->plugins = g_list_append (appctx->plugins, mp4mux);
      appctx->plugins = g_list_append (appctx->plugins, filesink);
    }
    if (options->out_rtsp) {
      appctx->plugins = g_list_append (appctx->plugins, rtph264pay);
      appctx->plugins = g_list_append (appctx->plugins, udpsink);
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
        "framerate", GST_TYPE_FRACTION, framerate, 1,
        "compression", G_TYPE_STRING, "ubwc", NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (cam_caps[i]), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    if (!set_ml_params (cam_qtimlelement[i], cam_qtimlvdetection[i],
        cam_detection_filter[i], options)) {
      goto error;
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    snprintf (element_name, 127, "/opt/video%d.mp4", i+1);
    g_object_set (G_OBJECT (filesrc[i]), "location", element_name, NULL);
    g_object_set (G_OBJECT (file_v4l2h264dec[i]), "capture-io-mode", 5,
        "output-io-mode", 5, NULL);
    if (!set_ml_params (file_qtimlelement[i], file_qtimlvdetection[i],
        file_detection_filter[i], options)) {
      goto error;
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    snprintf (element_name, 127, "rtsp://%s/live%d.mkv", options->rtsp_ip_port,
        i+1);
    g_object_set (G_OBJECT (rtspsrc[i]), "location", element_name,
        NULL);
    g_object_set (G_OBJECT (rtsp_v4l2h264dec[i]), "capture-io-mode", 5,
        "output-io-mode", 5, NULL);
    if (!set_ml_params (rtsp_qtimlelement[i], rtsp_qtimlvdetection[i],
        rtsp_detection_filter[i], options)) {
      goto error;
    }
  }

  // 2.4 Set the properties for composer output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601", NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (composer_caps), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.5 Set the properties for Wayland compositor
  if (options->out_display) {
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);
  }

  // 2.5 Set the properties for file/rtsp sink
  if (options->out_file || options->out_rtsp) {
    g_object_set (G_OBJECT (v4l2h264enc), "capture-io-mode", 5,
        "output-io-mode", 5, NULL);

    if (options->out_file) {
      g_object_set (G_OBJECT (filesink), "location", options->out_file, NULL);
    }

    if (options->out_rtsp) {
      g_object_set (G_OBJECT (enc_h264parse), "config-interval", -1, NULL);
      g_object_set (G_OBJECT (rtph264pay), "pt", 96, NULL);
      g_object_set (G_OBJECT (udpsink), "host", "127.0.0.1",
          "port", 8554, NULL);
    }
  }

  // 3. Setup the pipeline
  g_print ("Add all elements to the pipeline...\n");

  for (gint i = 0; i < options->num_camera; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), camsrc[i], cam_caps[i],
        cam_tee[i], cam_qtimlvconverter[i], cam_qtimlelement[i],
        cam_qtimlvdetection[i], cam_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), cam_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc[i], qtdemux[i],
        file_dec_h264parse[i], file_v4l2h264dec[i], file_dec_tee[i],
        file_qtimlvconverter[i], file_qtimlelement[i], file_qtimlvdetection[i],
        file_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), file_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc[i], rtph264depay[i],
        rtsp_dec_h264parse[i], rtsp_v4l2h264dec[i], rtsp_dec_tee[i],
        rtsp_qtimlvconverter[i], rtsp_qtimlelement[i], rtsp_qtimlvdetection[i],
        rtsp_detection_filter[i], NULL);
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
    gst_bin_add_many (GST_BIN (appctx->pipeline), waylandsink, NULL);
  }

  if (options->out_file || options->out_rtsp) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2h264enc, enc_h264parse,
        enc_tee, NULL);
    if (options->out_file) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), mp4mux, filesink, NULL);
    }
    if (options->out_rtsp) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), rtph264pay, udpsink, NULL);
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
      goto error;
    }
    ret = gst_element_link_many (cam_tee[i], cam_queue[i][1], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " cam_tee -> qtivcomposer.\n", i);
      goto error;
    }
    ret = gst_element_link_many (cam_tee[i], cam_queue[i][2],
        cam_qtimlvconverter[i], cam_queue[i][3], cam_qtimlelement[i],
        cam_queue[i][4], cam_qtimlvdetection[i], cam_detection_filter[i],
        cam_queue[i][5], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " cam: pre proc -> ml framework -> post proc -> composer.\n", i);
      goto error;
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    ret = gst_element_link_many (filesrc[i], qtdemux[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " filesrc -> qtdemux.\n", i);
      goto error;
    }
    // qtdemux -> file_queue[i][0] link is not created here as it is a
    // dymanic link using on_pad_added callback
    ret = gst_element_link_many (file_queue[i][0], file_dec_h264parse[i],
        file_v4l2h264dec[i], file_queue[i][1], file_dec_tee[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " file_queue -> file_dec_tee.\n", i);
      goto error;
    }
    ret = gst_element_link_many (file_dec_tee[i], file_queue[i][2], qtivcomposer,
        NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          "file_dec_tee -> qtivcomposer.\n", i);
      goto error;
    }
    ret = gst_element_link_many (file_dec_tee[i], file_queue[i][3],
        file_qtimlvconverter[i], file_queue[i][4], file_qtimlelement[i],
        file_queue[i][5], file_qtimlvdetection[i], file_detection_filter[i],
        file_queue[i][6], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " file: pre proc -> ml framework -> post proc -> composer.\n", i);
      goto error;
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    // rtspsrc -> rtsp_queue[i][0] link is not created here as it is a
    // dymanic link using on_pad_added callback
    ret = gst_element_link_many (rtsp_queue[i][0], rtph264depay[i],
        rtsp_dec_h264parse[i], rtsp_v4l2h264dec[i], rtsp_queue[i][1],
        rtsp_dec_tee[i], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " rtsp_queue -> rtsp_tee.\n", i);
      goto error;
    }
    ret = gst_element_link_many (rtsp_dec_tee[i], rtsp_queue[i][2], qtivcomposer,
        NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " rtsp_tee -> qtivcomposer.\n", i);
      goto error;
    }
    ret = gst_element_link_many (rtsp_dec_tee[i], rtsp_queue[i][3],
        rtsp_qtimlvconverter[i], rtsp_queue[i][4], rtsp_qtimlelement[i],
        rtsp_queue[i][5], rtsp_qtimlvdetection[i], rtsp_detection_filter[i],
        rtsp_queue[i][6], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for %d"
          " rtsp: pre proc -> ml framework -> post proc -> composer.\n", i);
      goto error;
    }
  }

  ret = gst_element_link_many (
      qtivcomposer, queue[0], composer_caps, composer_tee, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        " qtivcomposer -> composer_tee.\n");
    goto error;
  }

  if (options->out_display) {
    ret = gst_element_link_many (composer_tee, queue[1], waylandsink, NULL);
    if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        " composer_tee -> waylandsink.\n");
      goto error;
    }
  }

  if (options->out_file || options->out_rtsp) {
    ret = gst_element_link_many (composer_tee, queue[2], v4l2h264enc, queue[3],
        enc_h264parse, enc_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " composer_tee -> encoder -> enc_tee.\n");
      goto error;
    }

    if (options->out_file) {
      ret = gst_element_link_many (enc_tee, queue[4], mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " enc_tee -> mp4mux -> filesink.\n");
        goto error;
      }
    }

    if (options->out_rtsp) {
      ret = gst_element_link_many (
          enc_tee, queue[5], rtph264pay, udpsink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " enc_tee -> udpsink.\n");
        goto error;
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
    goto error;
  }

  return TRUE;

error:
  for (gint i = 0; i < options->num_camera; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), camsrc[i], cam_caps[i],
        cam_tee[i], cam_qtimlvconverter[i], cam_qtimlelement[i],
        cam_qtimlvdetection[i], cam_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_remove_many (GST_BIN (appctx->pipeline), cam_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_file; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), filesrc[i], qtdemux[i],
        file_dec_h264parse[i], file_v4l2h264dec[i], file_dec_tee[i],
        file_qtimlvconverter[i], file_qtimlelement[i], file_qtimlvdetection[i],
        file_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_remove_many (GST_BIN (appctx->pipeline), file_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < options->num_rtsp; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), rtspsrc[i], rtph264depay[i],
        rtsp_dec_h264parse[i], rtsp_v4l2h264dec[i], rtsp_dec_tee[i],
        rtsp_qtimlvconverter[i], rtsp_qtimlelement[i], rtsp_qtimlvdetection[i],
        rtsp_detection_filter[i], NULL);
    for (gint j = 0; j < QUEUE_COUNT; j++) {
      gst_bin_remove_many (GST_BIN (appctx->pipeline), rtsp_queue[i][j], NULL);
    }
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  gst_bin_remove_many (GST_BIN (appctx->pipeline), qtivcomposer,
      composer_caps, composer_tee, NULL);

  if (options->out_display) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), waylandsink, NULL);
  }

  if (options->out_file || options->out_rtsp) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), v4l2h264enc, enc_h264parse,
        enc_tee, NULL);
    if (options->out_file) {
      gst_bin_remove_many (GST_BIN (appctx->pipeline), mp4mux, filesink, NULL);
    }
    if (options->out_rtsp) {
      gst_bin_remove_many (GST_BIN (appctx->pipeline), rtph264pay, udpsink, NULL);
    }
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
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *app_name = NULL;
  GstAppContext appctx = {};
  GstAppOptions options = {};
  struct rlimit rl;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  gchar help_description[1024];

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
  setenv ("XDG_RUNTIME_DIR", "/run/user/root", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "num-camera", 0, 0, G_OPTION_ARG_INT,
      &options.num_camera,
      "Number of cameras to be used (range: 0-" TO_STR (MAX_CAMSRCS) ")",
      NULL
    },
    { "camera-id", 'c', 0, G_OPTION_ARG_INT,
      &options.camera_id,
      "Use provided camera id as source\n"
      "      Default input camera 0 if no other input selected\n"
      "      This parameter is ignored if num-camera=" TO_STR (MAX_CAMSRCS),
      "0 or 1"
    },
    { "num-file", 0, 0, G_OPTION_ARG_INT,
      &options.num_file,
      "Number of input files to be used (range: 0-" TO_STR (MAX_FILESRCS) ")\n"
      "      Copy the H.264 encoded files to /opt and name"
      " as video1.mp4, video2.mp4 and so on",
      NULL
    },
    { "num-rtsp", 0, 0, G_OPTION_ARG_INT,
      &options.num_rtsp,
      "Number of input rtsp streams to be used"
      " (range: 0-" TO_STR (MAX_RTSPSRCS) ")\n"
      "      rtsp server should provide H.264 encoded streams"
      " /live1.mkv, /live2.mkv and so on",
      NULL
    },
    { "rtsp-ip-port", 0, 0, G_OPTION_ARG_STRING,
      &options.rtsp_ip_port,
      "This parameter overrides default ip:port\n"
      "      Should be provided as ip:port combination\n"
      "      Default ip:port is 127.0.0.1:8554",
      "ip:port"
    },
    { "model", 'm', 0, G_OPTION_ARG_STRING,
      &options.model_path,
      "This parameter overrides default model file path\n"
      "      Default model path for YOLOV5 TFLITE: " DEFAULT_TFLITE_YOLOV5_MODEL,
      "/PATH"
    },
    { "labels", 'l', 0, G_OPTION_ARG_STRING,
      &options.labels_path,
      "This parameter overrides default labels file path\n"
      "      Default labels path for YOLOV5: " DEFAULT_YOLOV5_LABELS,
      "/PATH"
    },
    { "display", 'd', 0, G_OPTION_ARG_NONE,
      &options.out_display,
      "Display on screen",
      NULL
    },
    { "out-file", 'f', 0, G_OPTION_ARG_STRING,
      &options.out_file,
      "Path to save H.264 Encoded file",
      "/PATH"
    },
    { "out-rtsp", 'r', 0, G_OPTION_ARG_NONE,
      &options.out_rtsp,
      "Encode and stream on rtsp\n"
      "      Run below command on a separate shell to start the rtsp server:\n"
      "          gst-rtsp-server -p 8900 -m /live \"( udpsrc name=pay0 port=8554"
      " caps=\\\"application/x-rtp,media=video,clock-rate=90000,"
      "encoding-name=H264,payload=96\\\" )\"\n"
      "      Live URL on port 8900: rtsp://127.0.0.1:8900/live\n"
      "          Change IP address to match your network settings",
      NULL
    },
    { NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];
  options.mlframework = "qtimltflite";
  options.camera_id = -1;
  options.rtsp_ip_port = "127.0.0.1:8554";
  options.model_path = DEFAULT_TFLITE_YOLOV5_MODEL;
  options.labels_path = DEFAULT_YOLOV5_LABELS;


  snprintf (help_description, 1023, "\nExample:\n"
      "  %s --num-file=6\n"
      "  %s --num-camera=2 --display\n"
      "  %s --model=%s --labels=%s\n"
      "  %s --num-file=4 -d -f /opt/app.mp4 --out-rtsp\n"
      "\nThis Sample App demonstrates Object Detection with various input/output"
      " stream combinations",
      app_name, app_name, app_name, DEFAULT_TFLITE_YOLOV5_MODEL,
      DEFAULT_YOLOV5_LABELS, app_name);
  help_description[1023] = '\0';

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
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    return -EFAULT;
  }

  if (options.num_camera > MAX_CAMSRCS) {
    g_printerr ("Number of camera streams cannot be more than 2\n");
    return -1;
  }

  if (options.num_file > MAX_FILESRCS) {
    g_printerr ("Number of file streams cannot be more than %d\n", MAX_FILESRCS);
    return -1;
  }

  if (options.num_rtsp > MAX_RTSPSRCS) {
    g_printerr ("Number of rtsp streams cannot be more than %d\n", MAX_RTSPSRCS);
    return -1;
  }

  if (options.num_camera < 0 || options.num_file < 0 || options.num_rtsp < 0) {
    g_printerr ("Negative count for any input is not supported\n");
    return -1;
  }

  options.input_count = options.num_camera + options.num_file + options.num_rtsp;

  if (options.input_count != options.num_camera &&
      options.input_count != options.num_file &&
      options.input_count != options.num_rtsp) {
    g_printerr ("use only same kind of input, like %d camera or %d files or"
        " %d rtsp inputs\n", MAX_CAMSRCS, MAX_FILESRCS, MAX_RTSPSRCS);
    return -1;
  }

  if (options.input_count == 0 ||
      (options.camera_id != -1 && options.num_camera == 0)) {
    g_print ("No stream provided in options, defaulting to 1 camera stream.\n");
    options.num_camera = 1;
    options.input_count++;
  }

  if (options.camera_id == -1 || options.num_camera == 2) {
    options.camera_id = 0;
  }

  if (!options.out_display && (NULL == options.out_file) && !options.out_rtsp) {
    g_print ("No sink option provided, defaulting to display sink.\n");
    options.out_display = TRUE;
  }

  for (gint i = 0; i < options.num_file; i++) {
    gchar file_name[128];
    snprintf (file_name, 127, "/opt/video%d.mp4", i+1);
    if (!file_exists (file_name)) {
      g_printerr ("video file doesnot exist at path: %s\n", file_name);
      return -EINVAL;
    }
  }

  if (!file_exists (options.model_path)) {
    g_printerr ("Invalid model file path: %s\n", options.model_path);
    return -EINVAL;
  }

  if (!file_exists (options.labels_path)) {
    g_printerr ("Invalid labels file path: %s\n", options.labels_path);
    return -EINVAL;
  }

  if (options.out_file && !file_location_exists (options.out_file)) {
    g_printerr ("Invalid output file location: %s\n", options.out_file);
    return -EINVAL;
  }

  g_print ("Run app with model: %s and labels: %s\n",
      options.model_path, options.labels_path);

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
  ret = create_pipe (&appctx, &options);
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
