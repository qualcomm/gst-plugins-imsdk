/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based daisy chain Object Detection and Pose Estimation
 *
 * Description:
 * The application takes camera/file/rtsp stream and gives same to
 * Yolo model for object detection and splits frame based on bounding box
 * for pose, displays preview with overlayed bounding boxes and pose estimation
 *
 * Pipeline for Gstreamer with daisychain below:
 *
 * Buffer handling for different sources:
 * 1. For camera source:
 * qtiqmmfsrc (Camera) -> qmmfsrc_caps -> tee (2 SPLIT)
 *
 * 2. For File source:
 * filesrc -> qtdemux -> h264parse -> tee (2 SPLIT)
 *
 * 3. For RTSP source:
 * rtspsrc -> rtph264depay -> h264parse -> tee (2 SPLIT)
 *
 * Pipeline after tee is common for all
 * sources (qtiqmmfsrc/filesrc/rtspsrc)
 *
 *  | tee -> qtimetamux[0]
 *        -> Pre process-> qtimltflite -> qtimlvdetection -> qtimetamux[0]
 *  | qtimetamux[0] -> tee
 *  | tee -> qtimetamux[1]
 *        -> Pre process-> qtimltflite -> qtimlvpose -> qtimetamux[1]
 *  | qtimetamux[1] -> tee
 *  | tee -> qtivcomposer
 *        -> qtivsplit (2 SPLIT) -> filter -> qtivcomposer
 *                               -> filter -> qtivcomposer
 *  | qtivcomposer (COMPOSITION) -> qtivoverlay -> fpsdisplaysink (Display)
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_TFLITE_YOLOV8_MODEL "/etc/models/YOLOv8-Detection-Quantized.tflite"
#define DEFAULT_TFLITE_POSE_MODEL \
    "/etc/models/hrnet_pose_quantized.tflite"
#define DEFAULT_YOLOV8_LABELS "/etc/labels/yolov8.labels"
#define DEFAULT_POSE_LABELS "/etc/labels/hrnet_pose.labels"

/**
 * Default Scale and Offset constants
 */
#define DEFAULT_YOLOV8_CONSTANT "YoloV8,q-offsets=<21.0, 0.0, 0.0>,\
    q-scales=<3.093529462814331, 0.00390625, 1.0>"
#define DEFAULT_HRNET_CONSTANT "hrnet,q-offsets=<8.0>,\
    q-scales=<0.0040499246679246426>;"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_CAMERA_PREVIEW_OUTPUT_WIDTH 1920
#define DEFAULT_CAMERA_PREVIEW_OUTPUT_HEIGHT 1080
#define DEFAULT_CAMERA_FRAME_RATE 30
#define DEFAULT_DAISYCHAIN_OUTPUT_WIDTH 240
#define DEFAULT_DAISYCHAIN_OUTPUT_HEIGHT 480

/**
 * Dimensions of output display/file
*/
#define DEFAULT_OUTPUT_WIDTH 1920
#define DEFAULT_OUTPUT_HEIGHT 1080

/**
 * Maximum count of various sources possible to configure
 */
#define QUEUE_COUNT 20
#define TEE_COUNT 3
#define DETECTION_COUNT 1
#define DETECTION_FILTER_COUNT 2
#define POSE_COUNT 1
#define TFLITE_ELEMENT_COUNT 2
#define SPLIT_COUNT 2
#define COMPOSER_SINK_COUNT 3

/**
 * GstDaisyChainModelType:
 * @GST_DETECTION_TYPE_YOLO : Yolo Object Detection Model.
 * @GST_POSE_TYPE_HRNET     : HRNET Pose Estimation Model.
 *
 * Type of Usecase.
 */
typedef enum {
  GST_DETECTION_TYPE_YOLO,
  GST_POSE_TYPE_HRNET
} GstDaisyChainModelType;

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
typedef enum {
  IMAGE_BATCH_NON_CUMULATIVE,
  IMAGE_BATCH_CUMULATIVE,
  ROI_BATCH_NON_CUMULATIVE,
  ROI_BATCH_CUMULATIVE
} GstConversionMode;

/**
 * GstVideoDisposition:
 * @TOP_LEFT : Preserve the source Aspect Ratio during scaledown and
 *     place it in the top-left corner of the output tensor.
 * @CENTRE   : Preserve the source Aspect Ratio during scaledown and
 *     place it in the centre of the output tensor.
 * @STRETCH  :  Ignore the source image Aspect Ratio and if required
 *     stretch its Aspect Ratio in order to fit completely inside the
 *     output tensor
 *
 * Type of Video Disposition.
 */
typedef enum {
  TOP_LEFT,
  CENTRE,
  STRETCH
} GstVideoDisposition;

/**
 * GstVideoSplitMode:
 * @NONE            : Buffer is rescaled and color conversion.
 * @FORCE_TRANSFORM : Buffer is rescaled and color conversion.
 * @SINGLE_ROI_META : crop, rescale and color conversion
 * @BATCH_ROI_META  : For each ROI crop, rescale and color conversion
 *
 * Type of Split Mode.
 */
typedef enum {
  NONE,
  FORCE_TRANSFORM,
  SINGLE_ROI_META,
  BATCH_ROI_META
} GstVideoSplitMode;

/**
 * GstVideoConvBackend:
 * @C2D    : Use C2D based video converter
 * @GLES   : Use OpenGLES based video converter.
 * @FCV    : Use FastCV based video converter.
 *
 * The backend of the video converter engine.
 */
typedef enum {
  C2D,
  GLES,
  FCV
} GstVideoConvBackend;

/**
 * Structure for various application specific options
 */
typedef struct {
  gboolean camera_source;
  gchar *input_file_path;
  gchar *output_file_path;
  gchar *rtsp_ip_port;
  gchar *yolov8_model_path;
  gchar *hrnet_model_path;
  gchar *yolov8_labels_path;
  gchar *hrnet_labels_path;
  gchar *yolov8_constants;
  gchar *hrnet_constants;
  enum GstSinkType sink_type;
  GstStreamSourceType source_type;
  gboolean display;
} GstAppOptions;

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
gst_app_context_free (GstAppContext * appctx, GstAppOptions * options)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->input_file_path != NULL) {
    g_free ((gpointer)options->input_file_path);
    options->input_file_path = NULL;
  }

  if (options->rtsp_ip_port != NULL) {
    g_free ((gpointer)options->rtsp_ip_port);
    options->rtsp_ip_port = NULL;
  }

  if (options->yolov8_model_path != (gchar *)(&DEFAULT_TFLITE_YOLOV8_MODEL) &&
      options->yolov8_model_path != NULL) {
    g_free ((gpointer)options->yolov8_model_path);
  }

  if (options->hrnet_model_path != (gchar *)(&DEFAULT_TFLITE_POSE_MODEL) &&
      options->hrnet_model_path != NULL) {
    g_free ((gpointer)options->hrnet_model_path);
  }

  if (options->yolov8_labels_path != (gchar *)(&DEFAULT_YOLOV8_LABELS) &&
      options->yolov8_labels_path != NULL) {
    g_free ((gpointer)options->yolov8_labels_path);
  }

  if (options->hrnet_labels_path != (gchar *)(&DEFAULT_POSE_LABELS) &&
      options->hrnet_labels_path != NULL) {
    g_free ((gpointer)options->hrnet_labels_path);
  }

  if (options->yolov8_constants != (gchar *)(&DEFAULT_YOLOV8_CONSTANT) &&
      options->yolov8_constants != NULL) {
    g_free ((gpointer)options->yolov8_constants);
  }

  if (options->hrnet_constants != (gchar *)(&DEFAULT_HRNET_CONSTANT) &&
      options->hrnet_constants != NULL) {
    g_free ((gpointer)options->hrnet_constants);
  }

  if (options->output_file_path != NULL) {
    g_free (options->output_file_path);
    options->output_file_path = NULL;
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
 * @param options GstAppOptions object
 */
static gboolean
create_pipe (GstAppContext * appctx, const GstAppOptions *options)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *queue[QUEUE_COUNT] = {NULL};
  GstElement *tee[TEE_COUNT] = {NULL};
  GstElement *qtimlvconverter[TFLITE_ELEMENT_COUNT] = {NULL};
  GstElement *qtimlelement[TFLITE_ELEMENT_COUNT] = {NULL};
  GstElement *filter[SPLIT_COUNT] = {NULL};
  GstElement *qtimlvdetection[DETECTION_COUNT] = {NULL} ;
  GstElement *detection_filter[DETECTION_FILTER_COUNT] = {NULL};
  GstElement *qtimlvpose[POSE_COUNT] = {NULL};
  GstElement *qtimetamux[TFLITE_ELEMENT_COUNT] = {NULL};
  GstElement  *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstElement *qtivsplit = NULL, *qtivcomposer = NULL, *qtivoverlay = NULL;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse_decode = NULL;
  GstElement *rtspsrc = NULL, *rtph264depay = NULL, *v4l2h264dec = NULL;
  GstElement *v4l2h264dec_caps = NULL;
  GstElement *v4l2h264enc = NULL, *mp4mux = NULL, *filesink = NULL;
  GstElement *h264parse_encode = NULL, *sink_filter = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint daisychain_width = DEFAULT_DAISYCHAIN_OUTPUT_WIDTH;
  gint daisychain_height = DEFAULT_DAISYCHAIN_OUTPUT_HEIGHT;
  gint preview_width = DEFAULT_CAMERA_PREVIEW_OUTPUT_WIDTH;
  gint preview_height = DEFAULT_CAMERA_PREVIEW_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;
  gint pos_vals[2], dim_vals[2];
  GValue value = G_VALUE_INIT;

  // 1. Create the elements or Plugins
  if (options->source_type == GST_STREAM_TYPE_CAMERA) {
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
  } else if (options->source_type == GST_STREAM_TYPE_FILE) {
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
    h264parse_decode = gst_element_factory_make ("h264parse",
        "h264parse_decode");
    if (!h264parse_decode) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    // Create v4l2h264dec element for decoding the stream
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
  } else if (options->source_type == GST_STREAM_TYPE_RTSP) {
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
    h264parse_decode = gst_element_factory_make ("h264parse",
        "h264parse_decode");
    if (!h264parse_decode) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    // Create v4l2h264dec element for decoding the stream
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
  }

  // Create qtimetamux element to attach postprocessing string results
  // on original frame
  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    snprintf (element_name, 127, "qtimetamux-%d", i);
    qtimetamux[i] = gst_element_factory_make ("qtimetamux", element_name);
    if (!qtimetamux[i]) {
      g_printerr ("Failed to create qtimetamux\n");
      goto error_clean_elements;
    }
  }

  // Create qtivcomposer to combine source output with ML post proc output
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

  // Create tee to send same data buffer to multiple elements
  for (gint i = 0; i < TEE_COUNT; i++) {
    snprintf (element_name, 127, "tee-%d", i);
    tee[i] = gst_element_factory_make ("tee", element_name);
    if (!tee[i]) {
      g_printerr ("Failed to create tee %d\n", i);
      goto error_clean_elements;
    }
  }

  // Capsfilter to get matching params of ML post proc o/p and qtivcomposer
  for (gint i = 0; i < SPLIT_COUNT; i++) {
    snprintf (element_name, 127, "filter-%d", i);
    filter[i] =
        gst_element_factory_make ("capsfilter", element_name);
    if (!filter[i]) {
      g_printerr ("Failed to create filter %d\n", i);
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

  // Capsfilter for format conversion
  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    snprintf (element_name, 127, "detection_filter-%d", i);
    detection_filter[i] =
        gst_element_factory_make ("capsfilter", element_name);
    if (!detection_filter[i]) {
      g_printerr ("Failed to create detection_filter %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create plugin for ML postprocessing for pose
  for (gint i = 0; i < POSE_COUNT; i++) {
    snprintf (element_name, 127, "qtimlvpose-%d", i);
    qtimlvpose[i] =
        gst_element_factory_make ("qtimlvpose", element_name);
    if (!qtimlvpose[i]) {
      g_printerr ("Failed to create qtimlvpose %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create qtivoverlay to draw bounding box and pose estimation
  qtivoverlay = gst_element_factory_make ("qtivoverlay", "qtivoverlay");
  if (!qtivoverlay) {
    g_printerr ("Failed to create qtivoverlay \n");
    goto error_clean_elements;
  }

  if (options->sink_type == GST_WAYLANDSINK) {
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
  } else if (options->sink_type == GST_VIDEO_ENCODE) {
     // Create h264parse element for parsing the stream
    h264parse_encode = gst_element_factory_make ("h264parse", "h264parse_encode");
    if (!h264parse_encode) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    sink_filter = gst_element_factory_make ("capsfilter", "capsfilter-sink");
    if (!sink_filter) {
      g_printerr ("Failed to create filter\n");
      goto error_clean_elements;
    }

    // Create H.264 Encoder plugin for file and rtsp output
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }

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

  // 2. Set properties for all GST plugin elements
  if (options->source_type == GST_STREAM_TYPE_CAMERA) {
    // 2.1 Set the capabilities of camera stream
    filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, preview_width,
      "height", G_TYPE_INT, preview_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->source_type == GST_STREAM_TYPE_FILE) {
    // 2.2 Set the capabilities of file stream
    g_object_set (G_OBJECT (filesrc), "location", options->input_file_path, NULL);
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->source_type == GST_STREAM_TYPE_RTSP) {
    // 2.3 Set the capabilities of rtsp stream
    g_object_set (G_OBJECT (rtspsrc), "location", options->rtsp_ip_port, NULL);
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  }

  // 2.4 Set the properties of pad_filter for pose
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, daisychain_width,
      "height", G_TYPE_INT, daisychain_height,
      "format", G_TYPE_STRING, "RGBA", NULL);
  for (gint i = 0; i < SPLIT_COUNT; i++) {
    g_object_set (G_OBJECT (filter[i]), "caps", pad_filter, NULL);
  }
  gst_caps_unref (pad_filter);

  // 2.5 Set the properties of pad_filter for detection
  pad_filter = gst_caps_new_simple ("text/x-raw", NULL, NULL);
  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    g_object_set (G_OBJECT (detection_filter[i]), "caps", pad_filter, NULL);
  }
  gst_caps_unref (pad_filter);

  // 2.6 Select the HW to DSP for model inferencing using delegate property
  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    if (i == GST_DETECTION_TYPE_YOLO)
    {
      g_object_set (G_OBJECT (qtimlelement[i]),
          "model", options->yolov8_model_path,
          "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
    }
    else
    {
      g_object_set (G_OBJECT (qtimlelement[i]),
          "model", options->hrnet_model_path,
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

  // 2.7 Set the properties of qtimlvconverter of pose plugin- mode
  // and image-disposition
  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, IMAGE_BATCH_NON_CUMULATIVE);
  g_object_set_property (G_OBJECT (qtimlvconverter[GST_DETECTION_TYPE_YOLO]),
      "mode", &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, ROI_BATCH_CUMULATIVE);
  g_object_set_property (G_OBJECT (qtimlvconverter[GST_POSE_TYPE_HRNET]),
      "mode", &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, CENTRE);
  g_object_set_property (G_OBJECT (qtimlvconverter[GST_POSE_TYPE_HRNET]),
      "image-disposition", &value);
  g_value_unset (&value);

  // 2.8 Set properties for detection postproc plugins- module, labels,
  // threshold, constants
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    module_id = get_enum_value (qtimlvdetection[i], "module", "yolov8");
    if (module_id != -1) {
      g_object_set (G_OBJECT (qtimlvdetection[i]),
          "threshold", 40.0, "results", 4,
          "module", module_id, "labels", options->yolov8_labels_path,
          "constants", options->yolov8_constants,
          NULL);
      }
    else {
      g_printerr ("Module yolov8 is not available in qtimlvdetection.\n");
      goto error_clean_elements;
    }
  }

  // 2.9 Set properties for pose postproc plugins- module, labels,
  // threshold, constants
  for (gint i = 0; i < POSE_COUNT; i++) {
    module_id = get_enum_value (qtimlvpose[i], "module", "hrnet");
    if (module_id != -1) {
      g_object_set (G_OBJECT (qtimlvpose[i]),
          "threshold", 51.0, "results", 1,
          "module", module_id, "labels", options->hrnet_labels_path,
          "constants", options->hrnet_constants,
          NULL);
      }
    else {
      g_printerr ("Module hrnet is not available in qtimlvpose.\n");
      goto error_clean_elements;
    }
  }

  // 2.10 Set properties backend engine
  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, GLES);
  g_object_set_property (G_OBJECT (qtivoverlay), "engine", &value);
  g_value_unset (&value);

  if (options->sink_type == GST_WAYLANDSINK) {
    // 2.11 Set the properties of Wayland compositor
    g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    // 2.12 Set the properties of fpsdisplaysink plugin- sync,
    // signal-fps-measurements, text-overlay and video-sink
    g_object_set (G_OBJECT (fpsdisplaysink), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements",
        TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  } else if (options->sink_type == GST_VIDEO_ENCODE) {
    // 2.13 Set the properties of filesink
    gst_element_set_enum_property (v4l2h264enc, "capture-io-mode",
        "dmabuf");
    gst_element_set_enum_property (v4l2h264enc, "output-io-mode",
        "dmabuf-import");

    pad_filter = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, DEFAULT_OUTPUT_WIDTH,
        "height", G_TYPE_INT, DEFAULT_OUTPUT_HEIGHT,
        "interlace-mode", G_TYPE_STRING, "progressive",
        "colorimetry", G_TYPE_STRING, "bt601", NULL);
    g_object_set (G_OBJECT (sink_filter), "caps", pad_filter, NULL);
    gst_caps_unref (pad_filter);

    g_object_set (G_OBJECT (filesink), "location", options->output_file_path,
        NULL);
  }

  // 3. Setup pipeline
  // 3.1 Adding elements to pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options->source_type == GST_STREAM_TYPE_CAMERA) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_caps,
        NULL);
  } else if (options->source_type == GST_STREAM_TYPE_FILE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        filesrc, qtdemux, h264parse_decode, v4l2h264dec, v4l2h264dec_caps, NULL);
  } else if (options->source_type == GST_STREAM_TYPE_RTSP) {
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        rtspsrc, rtph264depay, h264parse_decode,
        v4l2h264dec, v4l2h264dec_caps, NULL);
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtivsplit, qtivoverlay, qtivcomposer, NULL);

  if (options->sink_type == GST_WAYLANDSINK) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), fpsdisplaysink,
        waylandsink, NULL);
  } else if (options->sink_type == GST_VIDEO_ENCODE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), sink_filter,
        v4l2h264enc, h264parse_encode, mp4mux, filesink, NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  for (gint i = 0; i < TEE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), tee[i], NULL);
  }

  for (gint i = 0; i < SPLIT_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filter[i], NULL);
  }

  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlelement[i], NULL);
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter[i], NULL);
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimetamux[i], NULL);
  }

  for (gint i = 0; i < DETECTION_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvdetection[i], NULL);
  }

  for (gint i = 0; i < DETECTION_FILTER_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), detection_filter[i], NULL);
  }

  for (gint i = 0; i < POSE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvpose[i], NULL);
  }

  // 3.2 Link pipeline elements for Inferencing
  g_print ("Linking elements...\n");
  if (options->source_type == GST_STREAM_TYPE_CAMERA) {
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[0],
        tee[0], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtiqmmfsrc -> qmmfsrc_caps"
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (tee[0], queue[1], qtimetamux[0], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee and queue cannot be linked."
          "Exiting.\n");
      goto error_clean_pipeline;
    }
  } else if (options->source_type == GST_STREAM_TYPE_FILE) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements filesrc -> qtdemux elements "
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[0], h264parse_decode, v4l2h264dec,
        v4l2h264dec_caps, queue[1], tee[0], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtdemux -> h264parse -> v4l2h264dec"
          " ->qtimetamux  cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (tee[0], queue[2], qtimetamux[0], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee -> queue cannot be linked."
          "Exiting.\n");
      goto error_clean_pipeline;
    }
  } else if (options->source_type == GST_STREAM_TYPE_RTSP) {
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse_decode,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee[0], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements rtph264depay -> h264parse -> "
          "v4l2h264dec -> qtimetamux cannot be linked.Exiting.\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (tee[0], queue[2], qtimetamux[0], NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee and queue cannot be linked."
          "Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  ret = gst_element_link_many (tee[0], queue[3], qtimlvconverter[0], queue[4],
      qtimlelement[0], queue[5], qtimlvdetection[0], detection_filter[0],
      queue[6], qtimetamux[0], NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements src -> qtimlvconverter -> qtimlelement "
        " -> qtimlvdetection -> qtimetamux  cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (qtimetamux[0], queue[7], tee[1], NULL);
  if (!ret) {
    g_printerr ("\n pipeline element qtimetamux -> tee "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[1], queue[8], qtimetamux[1], NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements qtdemux -> h264parse -> v4l2h264dec"
        " ->qtimetamux  cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[1], queue[9], qtimlvconverter[1], queue[10],
      qtimlelement[1], queue[11], qtimlvpose[0], detection_filter[1], queue[12],
      qtimetamux[1], NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements src -> qtimlvconverter -> qtimlelement "
        " -> qtimlvdetection cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (qtimetamux[1], queue[13], tee[2], NULL);
  if (!ret) {
    g_printerr ("\n pipeline element qtimetamux -> tee "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[2], queue[14], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements tee -> qtivcomposer "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee[2], queue[15], qtivsplit, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements tee -> qtivsplit "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  for (gint i = 0; i < SPLIT_COUNT; i++) {
    ret = gst_element_link_many (qtivsplit, filter[i], queue[16 + i],
        qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtivsplit -> filter -> qtivcomposer "
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  if (options->sink_type == GST_WAYLANDSINK) {
    ret = gst_element_link_many (qtivcomposer, queue[18], qtivoverlay, queue[19],
    fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements qtivcomposer -> qtivoverlay-> "
          "fpsdisplaysink cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  } else if (options->sink_type == GST_VIDEO_ENCODE) {
    ret = gst_element_link_many (qtivcomposer, queue[18], qtivoverlay,
        sink_filter, v4l2h264enc, h264parse_encode, mp4mux, queue[19], filesink,
        NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee -> qtivcomposer -> encode"
          " ->filesink cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  g_print ("All elements are linked successfully\n");

  if (options->source_type == GST_STREAM_TYPE_FILE) {
    // 3.3 Setup dynamic pad to link qtdemux to queue
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  } else if (options->source_type == GST_STREAM_TYPE_RTSP) {
    // 3.4 Setup dynamic pad to link rtspsrc to queue
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  }

  // 3.5 Set src properties of qtivsplit for all splits
  for (gint i = 0; i < SPLIT_COUNT; i++) {
    GstPad *vsplit_src = NULL;
    GValue roi_mode = G_VALUE_INIT;
    g_value_init (&roi_mode, G_TYPE_INT);

    snprintf (element_name, 127, "src_%d", i);
    vsplit_src = gst_element_get_static_pad (qtivsplit, element_name);
    if (vsplit_src == NULL) {
      g_printerr ("src pad %d of qtivsplit couldn't be retrieved\n", i);
      goto error_clean_pipeline;
    }
    // Set split mode as single-roi-meta
    g_value_set_int (&roi_mode, SINGLE_ROI_META);
    g_object_set_property (G_OBJECT (vsplit_src), "mode", &roi_mode);

    g_value_unset (&roi_mode);
    gst_object_unref (vsplit_src);
  }

  for (gint i = 0; i < COMPOSER_SINK_COUNT; i++) {
    GstPad *vcomposer_sink = NULL;
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;
    GstVideoRectangle composer_sink_position[COMPOSER_SINK_COUNT] = {
      {0, 0, DEFAULT_OUTPUT_WIDTH, DEFAULT_OUTPUT_HEIGHT},
      {0, 0, DEFAULT_DAISYCHAIN_OUTPUT_WIDTH,
          DEFAULT_DAISYCHAIN_OUTPUT_HEIGHT},
      {DEFAULT_OUTPUT_WIDTH - DEFAULT_DAISYCHAIN_OUTPUT_WIDTH, 0,
          DEFAULT_DAISYCHAIN_OUTPUT_WIDTH, DEFAULT_DAISYCHAIN_OUTPUT_HEIGHT},
    };

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
  if (options->source_type == GST_STREAM_TYPE_CAMERA) {
    cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, NULL);
  } else if (options->source_type == GST_STREAM_TYPE_FILE) {
    cleanup_gst (&filesrc, &qtdemux, &h264parse_decode,
        &v4l2h264dec, &v4l2h264dec_caps, NULL);
  } else if (options->source_type == GST_STREAM_TYPE_RTSP) {
    cleanup_gst (&rtspsrc, &rtph264depay, &h264parse_decode,
    &v4l2h264dec, &v4l2h264dec_caps, NULL);
  }

  if (options->sink_type == GST_WAYLANDSINK) {
    cleanup_gst (&fpsdisplaysink, &waylandsink, NULL);
  } else if (options->sink_type == GST_VIDEO_ENCODE) {
    cleanup_gst (&sink_filter, &v4l2h264enc, &h264parse_encode, &mp4mux,
        &filesink, NULL);
  }

  cleanup_gst (&qtivsplit, &qtivcomposer, NULL);

  for (gint i = 0; i < SPLIT_COUNT; i++) {
    if (filter[i]) {
      gst_object_unref (filter[i]);
    }
  }

  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    if (qtimlelement[i]) {
      gst_object_unref (qtimlelement[i]);
    }
  }

  for (gint i = 0; i < TFLITE_ELEMENT_COUNT; i++) {
    if (qtimetamux[i]) {
      gst_object_unref (qtimetamux[i]);
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
    if (detection_filter[i]) {
      gst_object_unref (detection_filter[i]);
    }
  }

  for (gint i = 0; i < POSE_COUNT; i++) {
    if (qtimlvpose[i]) {
      gst_object_unref (qtimlvpose[i]);
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
  gboolean ret = FALSE;
  gchar help_description[1024];
  guint intrpt_watch_id = 0;

  options.input_file_path = NULL;
  options.output_file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.yolov8_model_path = DEFAULT_TFLITE_YOLOV8_MODEL;
  options.hrnet_model_path = DEFAULT_TFLITE_POSE_MODEL;
  options.yolov8_labels_path = DEFAULT_YOLOV8_LABELS;
  options.hrnet_labels_path = DEFAULT_POSE_LABELS;
  options.yolov8_constants = DEFAULT_YOLOV8_CONSTANT;
  options.hrnet_constants = DEFAULT_HRNET_CONSTANT;
  options.camera_source = FALSE;
  options.display = FALSE;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  GOptionEntry camera_entry = {};

  gboolean camera_is_available = is_camera_available ();

  if (camera_is_available) {
    GOptionEntry temp_camera_entry = {
      "camera", 'c', 0, G_OPTION_ARG_NONE,
      &options.camera_source,
      "Camera source (Default)",
      NULL
    };

    camera_entry = temp_camera_entry;
  } else {
    GOptionEntry temp_camera_entry = { NULL };

    camera_entry = temp_camera_entry;
  }

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "input-file", 's', 0, G_OPTION_ARG_STRING,
      &options.input_file_path,
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
    { "object-detection-model", 0, 0, G_OPTION_ARG_STRING,
      &options.yolov8_model_path,
      "This is an optional parameter and overrides default path\n"
      "      Default model path for Object detection TFLITE Model: "
      DEFAULT_TFLITE_YOLOV8_MODEL,
      "/PATH"
    },
    { "pose-detection-model", 0, 0, G_OPTION_ARG_STRING,
      &options.hrnet_model_path,
      "This is an optional parameter and overrides default path\n"
      "      Default model path for Pose detection TFLITE Model: "
      DEFAULT_TFLITE_POSE_MODEL,
      "/PATH"
    },
    { "object-detection-labels", 0, 0, G_OPTION_ARG_STRING,
      &options.yolov8_labels_path,
      "This is an optional parameter and overrides default path\n"
      "      Default Object detection labels path: "
      DEFAULT_YOLOV8_LABELS,
      "/PATH"
    },
    { "pose-detection-labels", 0, 0, G_OPTION_ARG_STRING,
      &options.hrnet_labels_path,
      "This is an optional parameter and overrides default path\n"
      "      Default Pose detection labels path: "
      DEFAULT_POSE_LABELS,
      "/PATH"
    },
    { "object-detection-constants", 0, 0, G_OPTION_ARG_STRING,
      &options.yolov8_constants,
      "Constants, offsets and coefficients used by detection module \n"
      "      for post-processing of incoming tensors.\n"
      "      Default constants: \"" DEFAULT_YOLOV8_CONSTANT "\"",
      "/CONSTANTS"
    },
    { "pose-detection-constants", 0, 0, G_OPTION_ARG_STRING,
      &options.hrnet_constants,
      "Constants, offsets and coefficients used pose module \n"
      "      for post-processing of incoming tensors.\n"
      "      Default constants: \"" DEFAULT_HRNET_CONSTANT "\"",
      "/CONSTANTS"
    },
    { "display", 'd', 0, G_OPTION_ARG_NONE,
      &options.display,
      "Display stream on wayland (Default).",
      "enable flag"
    },
    { "output-file", 'o', 0, G_OPTION_ARG_STRING,
      &options.output_file_path,
      "Output file path.\n",
      "/PATH"
    },
    camera_entry,
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  gchar camera_description[255] = {};

  if (camera_is_available) {
    snprintf (camera_description, sizeof (camera_description),
      "  %s \n"
      "  %s --camera --display\n"
      "  %s --camera --output-file=/etc/media/out.mp4\n",
      app_name, app_name, app_name);
  }

  snprintf (help_description, 1023, "\nExample:\n"
      "  %s\n"
      "  %s --input-file=/etc/media/video.mp4 --display\n"
      "  %s --input-file=/etc/media/video.mp4 --output-file=/etc/media/out.mp4\n"
      "  %s --rtsp-ip-port=\"rtsp://<ip>:port/<stream>\" --display\n"
      "  %s --rtsp-ip-port=\"rtsp://<ip>:port/<stream>\""
      " --output-file=/etc/media/out.mp4\n"
      "\nThis Sample App demonstrates Daisy chain of "
      "Object Detection and Pose\n"
      "\nDefault Path for model and labels used are as below:\n"
      "Object detection:  %-32s  %-32s\n"
      "Pose  :  %-32s  %-32s\n"
      "\nTo use your own model and labels replace at the default paths\n",
      camera_description,
      app_name, app_name, app_name, app_name,
      DEFAULT_TFLITE_YOLOV8_MODEL, DEFAULT_YOLOV8_LABELS,
      DEFAULT_TFLITE_POSE_MODEL, DEFAULT_POSE_LABELS);
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

  if (options.display && options.output_file_path) {
    g_printerr ("Both Display and Output file are provided as input! - "
        "Select either Display or Output file\n");
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  } else if (options.display) {
    options.sink_type = GST_WAYLANDSINK;
    g_print ("Selected sink type as Wayland Display\n");
  } else if (options.output_file_path) {
    options.sink_type = GST_VIDEO_ENCODE;
    g_print ("Selected sink type as Output file with path = %s\n",
        options.output_file_path);
  } else {
    options.sink_type = GST_WAYLANDSINK;
    g_print ("Using Wayland Display as Default\n");
  }

  if ((options.camera_source && options.input_file_path) ||
      (options.camera_source && options.rtsp_ip_port) ||
      (options.input_file_path && options.rtsp_ip_port))
  {
    g_printerr ("Multiple sources are provided as input.\n");

    if (camera_is_available) {
      g_printerr ("Select either Camera or File or RTSP source\n");
    } else {
      g_printerr ("Select either File or RTSP source\n");
    }
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  } else if (camera_is_available && options.camera_source) {
    g_print ("Camera source is selected.\n");
    options.source_type = GST_STREAM_TYPE_CAMERA;
  } else if (options.input_file_path) {
    g_print ("File source is selected.\n");
    options.source_type = GST_STREAM_TYPE_FILE;
  } else if (options.rtsp_ip_port) {
    g_print ("RTSP source is selected.\n");
    options.source_type = GST_STREAM_TYPE_RTSP;
  } else {
    if (camera_is_available) {
      g_print ("No source is selected."
          "Camera is set as Default\n");
      options.source_type = GST_STREAM_TYPE_CAMERA;
    } else {
      g_printerr ("Select File or RTSP source\n");
      gst_app_context_free (&appctx, &options);
      return -EINVAL;
    }
  }

  if (options.source_type == GST_STREAM_TYPE_FILE) {
    if (!file_exists (options.input_file_path)) {
      g_printerr ("Invalid video file source path: %s\n",
          options.input_file_path);
      gst_app_context_free (&appctx, &options);
      return -EINVAL;
    }
  }

  if (!file_exists (options.yolov8_model_path)) {
    g_printerr ("Invalid detection model file path: %s\n",
        options.yolov8_model_path);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if (!file_exists (options.hrnet_model_path)) {
    g_printerr ("Invalid pose model file path: %s\n",
        options.hrnet_model_path);
    return -EINVAL;
  }

  if (!file_exists (options.yolov8_labels_path)) {
    g_printerr ("Invalid detection labels file path: %s\n",
        options.yolov8_labels_path);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if (!file_exists (options.hrnet_labels_path)) {
    g_printerr ("Invalid pose labels file path: %s\n",
        options.hrnet_labels_path);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  if (options.output_file_path &&
      !file_location_exists (options.output_file_path)) {
    g_printerr ("Invalid output file location: %s\n",
        options.output_file_path);
    gst_app_context_free (&appctx, &options);
    return -EINVAL;
  }

  g_print ("Running app with\n"
      "For Detection model: %s labels: %s\n"
      "For Pose model: %s labels: %s\n",
      options.yolov8_model_path, options.yolov8_labels_path,
      options.hrnet_model_path, options.hrnet_labels_path);

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
