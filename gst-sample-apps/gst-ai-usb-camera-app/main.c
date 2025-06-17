/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for USB Camera usecases with different possible
 * outputs
 *
 * Description:
 * This application Demonstrates USB camera usecases with below possible
 * outputs:
 *     --Live Camera Preview on Display
 *     --Dump the Camera Preview to a file
 *     --Stream Camera Preview to RTSP
 *     --Object detection output on Display
 *     --Dump Object detection output to file
 *     --Stream Object detection output to RTSP
 *
 * Usage:
 * gst-ai-usb-camera-app --config-file=/etc/configs/config-usb-camera-app.json
 *
 * Help:
 * gst-ai-usb-camera-app --help
 *
 * *******************************************************************************
 * Live Camera Preview on Display:
 *     camerasrc->capsfilter->waylandsink
 * Dump the Camera YUV to a filesink:
 *     camerasrc->capsfilter->qtivtransform->v4l2h264enc->h264parse->filesink
 * Pipeline For the Rtsp Streaming:
 *     camerasrc->capsfilter->qtivtransform ->v4l2h264enc->h264parse->qtirtspbin
 * Object detection and Live Camera Preview on Display
 * camerasrc->capsfilter->tee-> |  qtivcomposer-->waylandsink
 *                              |  tee-->qtimlvconverter-->qtimlplugin-->
 *                              |   qtimlvdetection-->caps-->qtivcomposer
 * *******************************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_FRAMERATE 30
#define DEFAULT_OUTPUT_FILENAME "/etc/media/video.mp4"
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT "8900"
#define DEFAULT_PROP_MPOINT "/live"
#define DEFAULT_CONFIG_FILE "/etc/configs/config-usb-camera-app.json"
#define MAX_VID_DEV_CNT 64

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_SNPE_YOLOV5_MODEL "/etc/models/yolov5.dlc"
#define DEFAULT_YOLOV5_LABELS "/etc/labels/yolov5.labels"
#define DEFAULT_SNPE_YOLOV8_MODEL "/etc/models/yolov8.dlc"
#define DEFAULT_YOLOV8_LABELS "/etc/labels/yolov8.labels"
#define DEFAULT_YOLOX_LABELS "/etc/labels/yolox.labels"
#define DEFAULT_SNPE_YOLONAS_MODEL "/etc/models/yolonas.dlc"
#define DEFAULT_YOLONAS_LABELS "/etc/labels/yolonas.labels"
#define DEFAULT_TFLITE_YOLOV8_MODEL "/etc/models/yolov8_det_quantized.tflite"
#define DEFAULT_TFLITE_YOLOX_MODEL "/etc/models/yolox_quantized.tflite"
#define DEFAULT_TFLITE_YOLOV5_MODEL "/etc/models/yolov5.tflite"
#define DEFAULT_TFLITE_YOLONAS_MODEL "/etc/models/yolonas_quantized.tflite"
#define DEFAULT_YOLOV7_LABELS "/etc/labels/yolov7.labels"
#define DEFAULT_TFLITE_YOLOV7_MODEL "/etc/models/yolov7_quantized.tflite"
#define DEFAULT_QNN_YOLOV8_MODEL "/etc/models/yolov8_det_quantized.bin"

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 8

/**
 * Defalut value of threshold
 */
#define DEFAULT_THRESHOLD_VALUE  75.0

/**
 * default value of delegate
 */
#define DEFAULT_SNPE_DELEGATE GST_ML_SNPE_DELEGATE_DSP

/**
 * Default constants to dequantize values
 */
#define DEFAULT_CONSTANTS_YOLOV8 \
 "YOLOv8,q-offsets=<12.0, 0.0, 0.0>,q-scales=<2.8047633171081543, \
 0.00390625, 0.0>;"

/**
 * Default constants to dequantize values
 */
#define DEFAULT_CONSTANTS_YOLOX "YOLOx,q-offsets=<38.0, 0.0, 0.0>,\
    q-scales=<3.6124823093414307, 0.003626860911026597, 1.0>;"

/**
 * Default constants to dequantize values
 */
#define DEFAULT_CONSTANTS_YOLOV5 \
 "YoloV5,q-offsets=<3.0>,q-scales=<0.005047998391091824>;"

/**
* Default constants to dequantize values
*/
#define DEFAULT_CONSTANTS_YOLONAS \
 "YoloNas,q-offsets=<37.0, 0.0, 0.0>,q-scales=<3.416602611541748, \
 0.00390625, 1.0>;"

/**
* Default constants to dequantize values
*/
#define DEFAULT_CONSTANTS_YOLOV7 \
 "Yolov7,q-offsets=<30.0, 0.0, 0.0>,q-scales=<3.320857286453247, \
 0.0037717572413384914, 1.0>;"

// Structure to hold the application context
struct GstCameraAppCtx
{
  GstElement *pipeline;
  GMainLoop *mloop;
  gchar *output_file;
  gchar *ip_address;
  gchar *port_num;
  gchar *enable_ml;
  gchar dev_video[16];
  enum GstSinkType sinktype;
  enum GstVideoFormat video_format;
  gint width;
  gint height;
  gint framerate;
};
typedef struct GstCameraAppCtx GstCameraAppContext;

/**
 * Structure for various application specific options
 */
typedef struct
{
  gchar *file_path;
  gchar *model_path;
  gchar *labels_path;
  gchar *constants;
  gchar **snpe_layers;
  GstCameraSourceType camera_type;
  GstModelType model_type;
  GstYoloModelType yolo_model_type;
  gdouble threshold;
  gint delegate_type;
  gint snpe_layer_count;
  gboolean use_cpu;
  gboolean use_gpu;
  gboolean use_dsp;
} GstAppOptions;

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstCameraAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstCameraAppContext *ctx =
      (GstCameraAppContext *) g_new0 (GstCameraAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }
  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->output_file = DEFAULT_OUTPUT_FILENAME;
  ctx->ip_address = DEFAULT_IP;
  ctx->port_num = DEFAULT_PORT;
  ctx->sinktype = GST_WAYLANDSINK;
  ctx->video_format = GST_YUV2_VIDEO_FORMAT;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->framerate = DEFAULT_FRAMERATE;
  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
    gst_app_context_free
    (GstCameraAppContext * appctx, GstAppOptions * options, gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->file_path != NULL) {
    g_free ((gpointer) options->file_path);
  }

  if (options->model_path != (gchar *) (&DEFAULT_SNPE_YOLOV5_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_SNPE_YOLOV8_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_SNPE_YOLONAS_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_TFLITE_YOLOV8_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_TFLITE_YOLOX_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_TFLITE_YOLOV5_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_TFLITE_YOLONAS_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_TFLITE_YOLOV7_MODEL) &&
      options->model_path != (gchar *) (&DEFAULT_QNN_YOLOV8_MODEL) &&
      options->model_path != NULL) {
    g_free ((gpointer) options->model_path);
  }

  if (options->labels_path != (gchar *) (&DEFAULT_YOLOV5_LABELS) &&
      options->labels_path != (gchar *) (&DEFAULT_YOLOV8_LABELS) &&
      options->labels_path != (gchar *) (&DEFAULT_YOLOX_LABELS) &&
      options->labels_path != (gchar *) (&DEFAULT_YOLONAS_LABELS) &&
      options->labels_path != (gchar *) (&DEFAULT_YOLOV7_LABELS) &&
      options->labels_path != NULL) {
    g_free ((gpointer) options->labels_path);
  }

  if (options->constants != (gchar *) (&DEFAULT_CONSTANTS_YOLOV5) &&
      options->constants != (gchar *) (&DEFAULT_CONSTANTS_YOLOV8) &&
      options->constants != (gchar *) (&DEFAULT_CONSTANTS_YOLONAS) &&
      options->constants != (gchar *) (&DEFAULT_CONSTANTS_YOLOV7) &&
      options->constants != NULL) {
    g_free ((gpointer) options->constants);
  }

  if (options->snpe_layers != NULL) {
    for (gint i = 0; i < options->snpe_layer_count; i++) {
      g_free ((gpointer)options->snpe_layers[i]);
    }
    g_free ((gpointer) options->snpe_layers);
  }

  if (config_file != NULL && config_file != (gchar *) (&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer) config_file);
    config_file = NULL;
  }

  if (appctx->ip_address != (gchar *) (&DEFAULT_IP) &&
      appctx->ip_address != NULL) {
    g_free ((gpointer) appctx->ip_address);
  }

  if (appctx->port_num != (gchar *) (&DEFAULT_PORT) && appctx->port_num != NULL) {
    g_free ((gpointer) appctx->port_num);
  }

  if (config_file != NULL && config_file != (gchar *) (&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer) config_file);
    config_file = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }
}

/**
 * Parse JSON file to read input parameters
 *
 * @param config_file Path to config file
 * @param options Application specific options
 */
gint
parse_json (gchar * file, GstAppOptions * options, GstCameraAppContext * appctx)
{
  JsonParser *parser = NULL;
  JsonNode *root = NULL;
  JsonObject *root_obj = NULL;
  JsonArray *snpe_layers = NULL;
  GError *error = NULL;

  parser = json_parser_new ();

  // Load the JSON file
  if (!json_parser_load_from_file (parser, file, &error)) {
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

  if (json_object_has_member (root_obj, "width")) {
    appctx->width = json_object_get_int_member (root_obj, "width");
    g_print ("Width : %d\n", appctx->width);
  }

  if (json_object_has_member (root_obj, "height")) {
    appctx->height = json_object_get_int_member (root_obj, "height");
    g_print ("Height : %d\n", appctx->height);
  }

  if (json_object_has_member (root_obj, "framerate")) {
    appctx->framerate = json_object_get_int_member (root_obj, "framerate");
    g_print ("Frame Rate : %d\n", appctx->framerate);
  }

  if (json_object_has_member (root_obj, "output")) {
    const gchar *output_type =
        g_strdup (json_object_get_string_member (root_obj, "output"));
    if (g_strcmp0 (output_type, "waylandsink") == 0)
      appctx->sinktype = GST_WAYLANDSINK;
    else if (g_strcmp0 (output_type, "filesink") == 0)
      appctx->sinktype = GST_VIDEO_ENCODE;
    else if (g_strcmp0 (output_type, "rtspsink") == 0)
      appctx->sinktype = GST_RTSP_STREAMING;
    else {
      gst_printerr ("output can only be one of "
          "\"waylandsink\", \"filesink\" or \"rtspsink\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "video-format")) {
    const gchar *video_format_type =
        json_object_get_string_member (root_obj, "video-format");
    if (g_strcmp0 (video_format_type, "nv12") == 0) {
      appctx->video_format = GST_NV12_VIDEO_FORMAT;
      g_print ("Selected Video Format : NV12 \n");
    } else if (g_strcmp0 (video_format_type, "yuy2") == 0) {
      appctx->video_format = GST_YUV2_VIDEO_FORMAT;
      g_print ("Selected Video Format : YUY2\n");
    } else if (g_strcmp0 (video_format_type, "mjpeg") == 0) {
      appctx->video_format = GST_MJPEG_VIDEO_FORMAT;
      g_print ("Selected Video Format : MJPEG\n");
    } else {
      gst_printerr ("video-format can only be one of "
          "\"nv12\", \"yuy2\" or \"mjpeg\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "output-file")) {
    appctx->output_file =
        g_strdup (json_object_get_string_member (root_obj, "output-file"));
    g_print ("Output File Name : %s\n", appctx->output_file);
  }

  if (json_object_has_member (root_obj, "ip-address")) {
    appctx->ip_address =
        g_strdup (json_object_get_string_member (root_obj, "ip-address"));
    g_print ("Ip Address : %s\n", appctx->ip_address);
  }

  if (json_object_has_member (root_obj, "port")) {
    appctx->port_num =
        g_strdup (json_object_get_string_member (root_obj, "port"));
    g_print ("Port Number : %s\n", appctx->port_num);
  }

  if (json_object_has_member (root_obj, "enable-object-detection")) {
    appctx->enable_ml =
        g_strdup (json_object_get_string_member (root_obj,
            "enable-object-detection"));
  }

  if (g_strcmp0 (appctx->enable_ml, "TRUE") == 0) {
    if (json_object_has_member (root_obj, "yolo-model-type")) {
      const gchar *yolo_model_type =
          json_object_get_string_member (root_obj, "yolo-model-type");
      if (g_strcmp0 (yolo_model_type, "yolov5") == 0)
        options->yolo_model_type = GST_YOLO_TYPE_V5;
      else if (g_strcmp0 (yolo_model_type, "yolov8") == 0)
        options->yolo_model_type = GST_YOLO_TYPE_V8;
      else if (g_strcmp0 (yolo_model_type, "yolonas") == 0)
        options->yolo_model_type = GST_YOLO_TYPE_NAS;
      else if (g_strcmp0 (yolo_model_type, "yolov7") == 0)
        options->yolo_model_type = GST_YOLO_TYPE_V7;
      else if (g_strcmp0 (yolo_model_type, "yolox") == 0)
        options->yolo_model_type = GST_YOLO_TYPE_X;
      else {
        gst_printerr ("yolo-model-type can only be one of "
            "\"yolov5\", \"yolov8\" or \"yolox\" or \"yolonas\" or \"yolov7\"\n");
        g_object_unref (parser);
        return -1;
      }
      g_print ("yolo-model-type : %s\n", yolo_model_type);
    }
  }

  if (json_object_has_member (root_obj, "ml-framework")) {
    const gchar *framework =
        json_object_get_string_member (root_obj, "ml-framework");
    if (g_strcmp0 (framework, "snpe") == 0)
      options->model_type = GST_MODEL_TYPE_SNPE;
    else if (g_strcmp0 (framework, "tflite") == 0)
      options->model_type = GST_MODEL_TYPE_TFLITE;
    else if (g_strcmp0 (framework, "qnn") == 0) {
      options->model_type = GST_MODEL_TYPE_QNN;
    } else {
      gst_printerr ("ml-framework can only be one of "
          "\"snpe\", \"tflite\" or \"qnn\"\n");
      g_object_unref (parser);
      return -1;
    }
    g_print ("ml-framework : %s\n", framework);
  }

  if (json_object_has_member (root_obj, "model")) {
    options->model_path =
        g_strdup (json_object_get_string_member (root_obj, "model"));
    g_print ("model_path : %s\n", options->model_path);
  }

  if (json_object_has_member (root_obj, "labels")) {
    options->labels_path =
        g_strdup (json_object_get_string_member (root_obj, "labels"));
  }

  if (json_object_has_member (root_obj, "constants")) {
    options->constants =
        g_strdup (json_object_get_string_member (root_obj, "constants"));
    g_print ("constants : %s\n", options->constants);
  }

  if (json_object_has_member (root_obj, "threshold")) {
    options->threshold = json_object_get_int_member (root_obj, "threshold");
    g_print ("threshold : %f\n", options->threshold);
  }

  if (json_object_has_member (root_obj, "runtime")) {
    const gchar *delegate = json_object_get_string_member (root_obj, "runtime");

    if (g_strcmp0 (delegate, "cpu") == 0)
      options->use_cpu = TRUE;
    else if (g_strcmp0 (delegate, "dsp") == 0)
      options->use_dsp = TRUE;
    else if (g_strcmp0 (delegate, "gpu") == 0)
      options->use_gpu = TRUE;
    else {
      gst_printerr
          ("Runtime can only be one of \"cpu\", \"dsp\" and \"gpu\"\n");
      g_object_unref (parser);
      return -1;
    }
    g_print ("delegate : %s\n", delegate);
  }

  if (json_object_has_member (root_obj, "snpe-layers")) {
    snpe_layers = json_object_get_array_member (root_obj, "snpe-layers");
    options->snpe_layer_count = json_array_get_length (snpe_layers);
    options->snpe_layers = (gchar **) g_malloc (
        sizeof (gchar *) * options->snpe_layer_count);

    for (gint i = 0; i < options->snpe_layer_count; i++) {
      options->snpe_layers[i] =
          g_strdup (json_array_get_string_element (snpe_layers, i));
    }
  }

  g_object_unref (parser);
  return 0;
}

/**
 * Find USB camera node:
 *
 * @param appctx Application Context object
 */
static gboolean
find_usb_camera_node (GstCameraAppContext * appctx)
{
  struct v4l2_capability v2cap;
  gint idx = 0, ret = 0, mFd = -1;

  while (idx < MAX_VID_DEV_CNT) {
    memset (appctx->dev_video, 0, sizeof (appctx->dev_video));

    ret =
        snprintf (appctx->dev_video, sizeof (appctx->dev_video), "/dev/video%d",
        idx);
    if (ret <= 0) {
      return FALSE;
    }

    g_print ("open USB camera device: %s\n", appctx->dev_video);
    mFd = open (appctx->dev_video, O_RDWR);
    if (mFd < 0) {
      mFd = -1;
      g_printerr ("Failed to open USB camera device: %s (%s)\n",
          appctx->dev_video, strerror (errno));
      idx++;
      continue;
    }

    if (ioctl (mFd, VIDIOC_QUERYCAP, &v2cap) == 0) {
      g_print ("ID_V4L_CAPABILITIES=: %s", v2cap.driver);
      if (strcmp ((const char *) v2cap.driver, "uvcvideo") != 0) {
        idx++;
        close (mFd);
        continue;
      }
    } else {
      g_printerr ("Failed to QUERYCAP device: %s (%s)\n", appctx->dev_video,
          strerror (errno));
      idx++;
      close (mFd);
      continue;
    }
    break;
  }

  if (idx >= MAX_VID_DEV_CNT || mFd < 0 || ret < 0) {
    g_printerr ("Failed to open video device");
    close (mFd);
    return FALSE;
  }

  close (mFd);
  g_print ("open %s successful \n", appctx->dev_video);
  return TRUE;
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_preview_pipe (GstCameraAppContext * appctx)
{
  GstElement *v4l2src = NULL, *capsfilter = NULL, *waylandsink = NULL;
  GstElement *filesink = NULL, *v4l2h264enc = NULL, *h264parse = NULL;
  GstElement *mp4mux = NULL, *queue[QUEUE_COUNT] = { NULL }, *qtirtspbin = NULL;
  GstElement *qtivtransform = NULL, *transform_capsfilter = NULL;
  GstElement *jpegdec = NULL, *videoconvert = NULL;
  GstCaps *filtercaps = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];

  // 1. Create the camsrc plugin
  v4l2src = gst_element_factory_make ("v4l2src", "v4l2src");
  if (!v4l2src) {
    g_printerr ("Failed to create v4l2src\n");
    goto error_clean_elements;
  }
  // Use capsfilter to define the camera output settings
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  if (!capsfilter) {
    g_printerr ("Failed to create capsfilter\n");
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
  if (appctx->sinktype == GST_VIDEO_ENCODE) {
    // Generic filesink plugin to write file on disk
    filesink = gst_element_factory_make ("filesink", "filesink");
    if (!filesink) {
      g_printerr ("Failed to create filesink\n");
      goto error_clean_elements;
    }
    // Create Encoder plugin
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }
    // Create frame parser plugin
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }
    // Create mp4mux plugin to save file in mp4 container
    mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
    if (!mp4mux) {
      g_printerr ("Failed to create mp4mux\n");
      goto error_clean_elements;
    }
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
      if (!qtivtransform) {
        g_printerr ("Failed to create qtivtransform\n");
        goto error_clean_elements;
      }
      //transform filter caps
      transform_capsfilter =
          gst_element_factory_make ("capsfilter", "transform_capsfilter");
      if (!transform_capsfilter) {
        g_printerr ("Failed to create transform_capsfilter\n");
        goto error_clean_elements;
      }
    } else if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      jpegdec = gst_element_factory_make ("jpegdec", "jpegdec");
      if (!jpegdec) {
        g_printerr ("Failed to create jpegdec\n");
        goto error_clean_elements;
      }
      videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");
      if (!videoconvert) {
        g_printerr ("Failed to create videoconvert\n");
        goto error_clean_elements;
      }
    } else {
        g_printerr ("Invalid Video Format Selected\n");
        goto error_clean_elements;
    }
  } else if (appctx->sinktype == GST_RTSP_STREAMING) {
    // Create Encoder plugin
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }
    // Create frame parser plugin
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }
    // Generic qtirtspbin plugin for streaming
    qtirtspbin = gst_element_factory_make ("qtirtspbin", "qtirtspbin");
    if (!qtirtspbin) {
      g_printerr ("Failed to create qtirtspbin\n");
      goto error_clean_elements;
    }
    qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
    if (!qtivtransform) {
      g_printerr ("Failed to create qtivtransform\n");
      goto error_clean_elements;
    }
    //transform filter caps
    transform_capsfilter =
        gst_element_factory_make ("capsfilter", "transform_capsfilter");
    if (!transform_capsfilter) {
      g_printerr ("Failed to create transform_capsfilter\n");
      goto error_clean_elements;
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      jpegdec = gst_element_factory_make ("jpegdec", "jpegdec");
      if (!jpegdec) {
        g_printerr ("Failed to create jpegdec\n");
        goto error_clean_elements;
      }
      videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");
      if (!videoconvert) {
        g_printerr ("Failed to create videoconvert\n");
        goto error_clean_elements;
      }
    }
  } else {
    // Create Weston compositor to render the output on Display
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("Failed to create waylandsink\n");
      goto error_clean_elements;
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      jpegdec = gst_element_factory_make ("jpegdec", "jpegdec");
      if (!jpegdec) {
        g_printerr ("Failed to create jpegdec\n");
        goto error_clean_elements;
      }
      videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");
      if (!videoconvert) {
        g_printerr ("Failed to create videoconvert\n");
        goto error_clean_elements;
      }
    }
  }

  // check the sink type and set properties of elements
  if (appctx->sinktype == GST_RTSP_STREAMING) {
    // Set properties for v4l2src
    g_object_set (G_OBJECT (v4l2src), "io-mode", "dmabuf", NULL);
    g_object_set (G_OBJECT (v4l2src), "device", appctx->dev_video, NULL);
    // Set properties for qtirtspbin
    g_object_set (G_OBJECT (qtirtspbin), "address", appctx->ip_address, "port",
        appctx->port_num, NULL);
    // Set the capabilities of camera plugin output
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "YUY2",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    } else if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("image/jpeg",
        "width", G_TYPE_INT, appctx->width,
        "height", G_TYPE_INT, appctx->height,
        "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    } else if(appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    } else {
        g_printerr ("Invalid Video Format Selected\n");
        goto error_clean_elements;
    }
    g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    // Set the capabilities of transform filter
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12", NULL);
    }
    if (appctx->video_format == GST_NV12_VIDEO_FORMAT ||
        appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, 1920,
          "height", G_TYPE_INT, 1088, NULL);
    }
    g_object_set (G_OBJECT (transform_capsfilter), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    // set the property of v4l2h264enc
    gst_element_set_enum_property (v4l2h264enc, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264enc, "output-io-mode",
        "dmabuf-import");
    // set the property of h264parse
    g_object_set (G_OBJECT (h264parse), "config-interval", 1, NULL);
    // Setup the pipeline
    g_print ("Adding all elements to the pipeline...\n");
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT ||
        appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, capsfilter,
          qtivtransform, transform_capsfilter, v4l2h264enc, h264parse, qtirtspbin,
          NULL);
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, capsfilter,
          jpegdec, videoconvert, qtivtransform, transform_capsfilter, v4l2h264enc,
          h264parse, qtirtspbin,
          NULL);
    }
    for (gint i = 0; i < QUEUE_COUNT; i++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
    }
    g_print ("Linking elements...\n");
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT ||
        appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      ret =
          gst_element_link_many (v4l2src, capsfilter, qtivtransform,
          transform_capsfilter, queue[0], v4l2h264enc, queue[1], h264parse,
          queue[2], qtirtspbin, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to qtirtspbin.\n");
        goto error_clean_pipeline;
      }
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      ret =
          gst_element_link_many (v4l2src, capsfilter, jpegdec, videoconvert,
          queue[0], qtivtransform, transform_capsfilter, v4l2h264enc, queue[1],
          h264parse, queue[2], qtirtspbin, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to qtirtspbin.\n");
        goto error_clean_pipeline;
      }
    }
  } else if (appctx->sinktype == GST_VIDEO_ENCODE) {
    // Set properties for v4l2src
    g_object_set (G_OBJECT (v4l2src), "io-mode", "dmabuf", NULL);
    g_object_set (G_OBJECT (v4l2src), "device", appctx->dev_video, NULL);
    // set the output file location for filesink element
    g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);
    // Set the camera source elements capability
    if (appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    }
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "YUY2",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("image/jpeg",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    }
    g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    // Set the property of transform filter
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12", NULL);
      g_object_set (G_OBJECT (transform_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    }
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT ||
        appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      // set the property of v4l2h264enc
      gst_element_set_enum_property (v4l2h264enc, "capture-io-mode", "dmabuf");
      gst_element_set_enum_property (v4l2h264enc, "output-io-mode",
          "dmabuf-import");
    }
    // Setup the pipeline
    g_print ("Adding all elements to the pipeline...\n");
    if (appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, capsfilter,
          v4l2h264enc, h264parse, mp4mux, filesink, NULL);
    }
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, capsfilter,
          qtivtransform, transform_capsfilter, v4l2h264enc, h264parse, mp4mux,
          filesink, NULL);
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, capsfilter,
          jpegdec, videoconvert, v4l2h264enc, h264parse, mp4mux, filesink, NULL);
    }

    for (gint i = 0; i < QUEUE_COUNT; i++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
    }

    g_print ("Linking elements...\n");
    if (appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      ret =
          gst_element_link_many (v4l2src, capsfilter, queue[0], v4l2h264enc,
              h264parse, queue[1], mp4mux, queue[2], filesink, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to filesink.\n");
        goto error_clean_pipeline;
      }
    }
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      ret =
          gst_element_link_many (v4l2src, capsfilter, qtivtransform,
          transform_capsfilter, queue[0], v4l2h264enc, h264parse, queue[1],
          mp4mux, queue[2], filesink, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to filesink.\n");
        goto error_clean_pipeline;
      }
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      ret =
          gst_element_link_many (v4l2src, capsfilter, jpegdec,
          videoconvert, queue[0], v4l2h264enc, h264parse, queue[1],
          mp4mux, queue[2], filesink, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to filesink.\n");
        goto error_clean_pipeline;
      }
    }
  } else {
    // Set properties for v4l2src
    g_object_set (G_OBJECT (v4l2src), "io-mode", "dmabuf-import", NULL);
    g_object_set (G_OBJECT (v4l2src), "device", appctx->dev_video, NULL);
    // Set the camera source output property
    // Set the camera source elements capability
    if (appctx->video_format == GST_NV12_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    }
    if (appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "YUY2",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    }
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("image/jpeg",
          "width", G_TYPE_INT, appctx->width,
          "height", G_TYPE_INT, appctx->height,
          "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
    }
    g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);
    // Setup the pipeline
    g_print ("Adding all elements to the pipeline...\n");
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src,
        capsfilter, waylandsink, NULL);
    if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), jpegdec, videoconvert, NULL);
    }
    for (gint i = 0; i < QUEUE_COUNT; i++) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
    }

    g_print ("Linking elements...\n");
    if (appctx->video_format == GST_NV12_VIDEO_FORMAT ||
        appctx->video_format == GST_YUV2_VIDEO_FORMAT) {
      ret = gst_element_link_many (v4l2src, capsfilter, waylandsink, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to waylandsink. \n");
        goto error_clean_pipeline;
      }
    } else if (appctx->video_format == GST_MJPEG_VIDEO_FORMAT) {
      ret = gst_element_link_many (v4l2src, capsfilter, jpegdec, videoconvert,
          queue[0],waylandsink, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline elements cannot be linked from v4l2src to waylandsink. \n");
        goto error_clean_pipeline;
      }
    }
  }

  g_print ("\n All elements are linked successfully\n");
  return TRUE;
error_clean_elements:
  cleanup_gst (&v4l2src, &capsfilter, &qtivtransform, &transform_capsfilter,
      &waylandsink, &filesink, &v4l2h264enc, &h264parse, &mp4mux, &queue,
      &qtirtspbin, &videoconvert, &jpegdec, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }
  return FALSE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST plugins
 * 2. Set Parameters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer
 * @param options Application specific options
 */
static gboolean
create_pipe (GstCameraAppContext * appctx, GstAppOptions * options)
{
  GstElement *v4l2src = NULL, *v4l2src_caps = NULL;
  GstElement *queue[QUEUE_COUNT], *tee = NULL;
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvdetection = NULL, *detection_filter = NULL;
  GstElement *qtivcomposer = NULL, *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstElement *filesink = NULL, *v4l2h264enc = NULL, *h264parse = NULL;
  GstElement *mp4mux = NULL, *qtirtspbin = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstStructure *fcontrols = NULL;
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  gint module_id;

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }

  // 1. Create v4l2src plugin
  v4l2src = gst_element_factory_make ("v4l2src", "v4l2src");
  if (!v4l2src) {
    g_printerr ("Failed to create v4l2src\n");
    goto error_clean_elements;
  }
  // Use capsfilter to define the camera output settings
  v4l2src_caps = gst_element_factory_make ("capsfilter", "v4l2src_caps");
  if (!v4l2src_caps) {
    g_printerr ("Failed to create v4l2src_caps\n");
    goto error_clean_elements;
  }
  // Create queue to decouple the processing on sink and source pad
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // Use tee to send data same data buffer one for AI, one for Display
  tee = gst_element_factory_make ("tee", "tee");
  if (!tee) {
    g_printerr ("Failed to create tee\n");
    goto error_clean_elements;
  }
  // Create qtimlconverter for Input preprocessing
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter",
      "qtimlvconverter");
  if (!qtimlvconverter) {
    g_printerr ("Failed to create qtimlvconverter\n");
    goto error_clean_elements;
  }
  // Create the ML inferencing plugin SNPE/TFLITE
  if (options->model_type == GST_MODEL_TYPE_SNPE) {
    qtimlelement = gst_element_factory_make ("qtimlsnpe", "qtimlelement");
  } else if (options->model_type == GST_MODEL_TYPE_TFLITE) {
    qtimlelement = gst_element_factory_make ("qtimltflite", "qtimlelement");
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    qtimlelement = gst_element_factory_make ("qtimlqnn", "qtimlelement");
  } else {
    g_printerr ("Invalid model type for plugin SNPE/TFLITE/QNN \n");
    goto error_clean_elements;
  }
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    goto error_clean_elements;
  }
  // Create plugin for ML postprocessing for object detection
  qtimlvdetection = gst_element_factory_make ("qtimlvdetection",
      "qtimlvdetection");
  if (!qtimlvdetection) {
    g_printerr ("Failed to create qtimlvdetection\n");
    goto error_clean_elements;
  }

  // Composer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }
  // Used to negotiate between ML post proc o/p and qtivcomposer
  detection_filter =
      gst_element_factory_make ("capsfilter", "detection_filter");
  if (!detection_filter) {
    g_printerr ("Failed to create detection_filter\n");
    goto error_clean_elements;
  }

  if (appctx->sinktype == GST_VIDEO_ENCODE) {
    // Generic filesink plugin to write file on disk
    filesink = gst_element_factory_make ("filesink", "filesink");
    if (!filesink) {
      g_printerr ("Failed to create filesink\n");
      goto error_clean_elements;
    }
    // Create H.264 Encoder plugin for file output
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }
    // Create H.264 frame parser plugin
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }
    // Create mp4mux plugin to save file in mp4 container
    mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
    if (!mp4mux) {
      g_printerr ("Failed to create mp4mux\n");
      goto error_clean_elements;
    }

  } else if (appctx->sinktype == GST_RTSP_STREAMING) {
    // Create H.264 Encoder plugin for file output
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }
    // Create H.264 frame parser plugin
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }
    // Generic qtirtspbin plugin for streaming
    qtirtspbin = gst_element_factory_make ("qtirtspbin", "qtirtspbin");
    if (!qtirtspbin) {
      g_printerr ("Failed to create qtirtspbin\n");
      goto error_clean_elements;
    }

  } else {
    // Create Wayland composer compositor to render output on Display
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("Failed to create waylandsink");
      goto error_clean_elements;
    }
    // Create fpsdisplaysink to display the current and
    // average framerate as a text overlay
    fpsdisplaysink =
        gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
    if (!fpsdisplaysink) {
      g_printerr ("Failed to create fpsdisplaysink\n");
      goto error_clean_elements;
    }
  }

  // 2. Set properties for all GST plugin elements
  g_object_set (G_OBJECT (v4l2src), "io-mode", "dmabuf-import", NULL);
  g_object_set (G_OBJECT (v4l2src), "device", appctx->dev_video, NULL);

  if (appctx->sinktype == GST_VIDEO_ENCODE
      || appctx->sinktype == GST_RTSP_STREAMING) {
    gst_element_set_enum_property (v4l2h264enc, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264enc, "output-io-mode",
        "dmabuf-import");
    // Set bitrate for streaming usecase
    fcontrols =
        gst_structure_from_string
        ("fcontrols,video_bitrate=6000000,video_bitrate_mode=0", NULL);
    g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);
    if (appctx->sinktype == GST_VIDEO_ENCODE) {
      g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);
    }
    if (appctx->sinktype == GST_RTSP_STREAMING) {
      g_object_set (G_OBJECT (h264parse), "config-interval", 1, NULL);
      g_object_set (G_OBJECT (qtirtspbin), "address", appctx->ip_address,
          "port", appctx->port_num, NULL);
    }
  } else {
    // 2.7 Set the properties for Wayland compositer
    g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    // 2.8 Set the properties of fpsdisplaysink plugin- sync,
    // signal-fps-measurements, text-overlay and video-sink
    g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  }

  // 2.4 Set the capabilities of camera plugin output for inference
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);
  g_object_set (G_OBJECT (v4l2src_caps), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.5 Select the HW to DSP/GPU/CPU for model inferencing using
  // delegate property
  if (options->model_type == GST_MODEL_TYPE_SNPE) {
    GstMLSnpeDelegate snpe_delegate;
    if (options->use_cpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_NONE;
      g_print ("Using CPU delegate\n");
    } else if (options->use_gpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_GPU;
      g_print ("Using GPU delegate\n");
    } else {
      snpe_delegate = GST_ML_SNPE_DELEGATE_DSP;
      g_print ("Using DSP delegate with SNPE\n");
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
      g_print ("Using GPU delegate\n");
      tflite_delegate = GST_ML_TFLITE_DELEGATE_GPU;
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", tflite_delegate, NULL);
    } else if (options->use_dsp) {
      g_print ("Using DSP delegate with TFLITE\n");
      delegate_options =
          gst_structure_from_string ("QNNExternalDelegate,backend_type=htp",
          NULL);
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
      g_object_set (G_OBJECT (qtimlelement), "external_delegate_path",
          "libQnnTFLiteDelegate.so", NULL);
      g_object_set (G_OBJECT (qtimlelement), "external_delegate_options",
          delegate_options, NULL);
      gst_structure_free (delegate_options);
    } else {
      g_printerr ("Invalid Runtime Selected\n");
      goto error_clean_elements;
    }
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    g_print ("Using DSP delegate with QNN\n");
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "backend", "/usr/lib/libQnnHtp.so", NULL);
  } else {
    g_printerr ("Invalid model type for inferencing \n");
    goto error_clean_elements;
  }
  g_print ("delegate : %d\n", options->model_type);

  // 2.6 Set properties for ML postproc plugins - module, layers, threshold
  g_value_init (&layers, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_STRING);

  if (options->model_type == GST_MODEL_TYPE_SNPE) {
    for (gint i = 0; i < options->snpe_layer_count; i++) {
      g_value_set_string (&value, options->snpe_layers[i]);
      gst_value_array_append_value (&layers, &value);
    }
    g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);
    switch (options->yolo_model_type) {
        // YOLO_V5 specific settings
      case GST_YOLO_TYPE_V5:
        g_print ("Using GST_YOLO_TYPE_V5 \n");
        g_object_set (G_OBJECT (qtimlelement), "model",
            options->model_path, NULL);
        // get enum values of module properties from qtimlvdetection plugin
        module_id = get_enum_value (qtimlvdetection, "module", "yolov5");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        break;

        // YOLO_V8 specific settings
      case GST_YOLO_TYPE_V8:
        g_print ("Using GST_YOLO_TYPE_V8 \n");
        g_object_set (G_OBJECT (qtimlelement), "model",
            options->model_path, NULL);
        // get enum values of module property frrom qtimlvdetection plugin
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        break;

        // YOLO_NAS specific settings
      case GST_YOLO_TYPE_NAS:
        g_print ("Using GST_YOLO_TYPE_NAS \n");
        g_object_set (G_OBJECT (qtimlelement), "model",
            options->model_path, NULL);
        // get enum values of module property frrom qtimlvdetection plugin
        module_id = get_enum_value (qtimlvdetection, "module", "yolo-nas");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolo-nas is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        break;

      default:
        g_printerr ("Invalid Yolo Model type\n");
        goto error_clean_elements;
    }
  } else if (options->model_type == GST_MODEL_TYPE_TFLITE) {
    switch (options->yolo_model_type) {
        // YOLO_V8 specific settings
      case GST_YOLO_TYPE_V8:
        // set qtimlvdetection properties
        g_print ("Using TFLITE GST_YOLO_TYPE_V8 \n");
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "constants",
            options->constants, NULL);
        g_printerr ("%s\n", options->constants);
        break;
      // YOLO_X specific settings
      case GST_YOLO_TYPE_X:
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
            g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "constants",
            options->constants, NULL);
        break;
        // YOLO_V5 specific settings
      case GST_YOLO_TYPE_V5:
        // set qtimlvdetection properties
        g_print ("Using TFLITE GST_YOLO_TYPE_V5 \n");
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov5");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "constants",
            options->constants, NULL);
        break;
        // YOLO_NAS specific settings
      case GST_YOLO_TYPE_NAS:
        // set qtimlvdetection properties
        g_print ("Using TFLITE GST_YOLO_TYPE_NAS \n");
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolo-nas");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolonas is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "constants",
            options->constants, NULL);
        break;
        // YOLOV7 specific settings
      case GST_YOLO_TYPE_V7:
        // set qtimlvdetection properties
        g_print ("Using TFLITE GST_YOLO_TYPE_V7 \n");
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "constants",
            options->constants, NULL);
        break;
      default:
        g_printerr ("Unsupported TFLITE model, Use YoloV5 or "
            "YoloV8 or YoloNas or Yolov7 TFLITE model\n");
        goto error_clean_elements;
    }
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    switch (options->yolo_model_type) {
        // YOLOv8 specific settings
      case GST_YOLO_TYPE_V8:
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "threshold",
            options->threshold, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "constants",
            options->constants, NULL);
        break;

      default:
        g_printerr ("Unsupported QNN model, use YoloV8 QNN model\n");
        goto error_clean_elements;
    }
  } else {
    g_printerr ("Invalid model_type or yolo_model_type\n");
    goto error_clean_elements;
  }

  // 2.9 Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640, "height", G_TYPE_INT, 360, NULL);

  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");
  if (appctx->sinktype == GST_VIDEO_ENCODE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesink, v4l2h264enc,
        h264parse, mp4mux, NULL);
  } else if (appctx->sinktype == GST_RTSP_STREAMING) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2h264enc, h264parse,
        qtirtspbin, NULL);
  } else {
    gst_bin_add_many (GST_BIN (appctx->pipeline), fpsdisplaysink, waylandsink,
        NULL);
  }
  gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, v4l2src_caps,
      tee, qtimlvconverter, qtimlelement, qtimlvdetection, detection_filter,
      qtivcomposer, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create pipeline for object detection
  ret = gst_element_link_many (v4l2src, v4l2src_caps, queue[0], tee, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "qmmfsource->composer\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee, queue[1], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "qmmfsource->converter\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee, queue[2], qtimlvconverter,
      queue[3], qtimlelement, queue[4], qtimlvdetection,
      detection_filter, queue[5], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "pre proc -> ml framework -> post proc.\n");
    goto error_clean_pipeline;
  }

  if (appctx->sinktype == GST_VIDEO_ENCODE) {
    ret = gst_element_link_many (qtivcomposer, queue[6], v4l2h264enc, h264parse,
        mp4mux, filesink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "qtivcomposer->fpsdisplaysink.\n");
      goto error_clean_pipeline;
    }
  } else if (appctx->sinktype == GST_RTSP_STREAMING) {
    ret = gst_element_link_many (qtivcomposer, queue[6], v4l2h264enc, h264parse,
        qtirtspbin, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "qtivcomposer->fpsdisplaysink.\n");
      goto error_clean_pipeline;
    }
  } else {
    ret = gst_element_link_many (qtivcomposer, queue[6], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "qtivcomposer->fpsdisplaysink.\n");
      goto error_clean_pipeline;
    }
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  appctx->pipeline = NULL;
  return FALSE;

error_clean_elements:
  cleanup_gst (&v4l2src, &v4l2src_caps, &tee, &qtimlvconverter,
      &qtimlelement, &qtimlvdetection, &qtivcomposer, &detection_filter,
      &waylandsink, &fpsdisplaysink, &filesink, &mp4mux, &qtirtspbin,
      &h264parse, &v4l2h264enc, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }
  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstCameraAppContext *appctx = NULL;
  const gchar *app_name = NULL;
  gchar help_description[4096];
  GstAppOptions options = { };
  gchar *config_file = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }
  // set default value
  options.file_path = NULL;
  options.use_cpu = FALSE, options.use_gpu = FALSE, options.use_dsp = FALSE;
  options.threshold = DEFAULT_THRESHOLD_VALUE;
  options.delegate_type = DEFAULT_SNPE_DELEGATE;
  options.model_type = GST_MODEL_TYPE_SNPE;
  options.camera_type = GST_CAMERA_TYPE_NONE;
  options.yolo_model_type = GST_YOLO_TYPE_NAS;
  options.model_path = NULL;
  options.labels_path = NULL;
  options.constants = NULL;
  options.snpe_layers = NULL;

  // Structure to define the user options selected
  GOptionEntry entries[] = {
    {"config-file", 0, 0, G_OPTION_ARG_STRING,
          &config_file,
          "Path to config file\n",
        NULL},
    {NULL}
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  snprintf (help_description, 4095, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates Object Detection or Preview on"
      " Input Stream from USB Camera\n"
      "\nConfig file Fields:\n"
      "  width: USB Camera Resolution width\n"
      "  height: USB Camera Resolution Height\n"
      "  framerate: USB Camera Frame Rate\n"
      "  video-type: Video Type format can be nv12, yuy2 or mjpeg\n"
      "      It is applicable only when enable-object-detection is set false"
      "  output: It can be either be waylandsink, filesink or rtspsink\n"
      "  output-file: Use this Parameter to set output file path\n"
      "      Default output file path is:" DEFAULT_OUTPUT_FILENAME "\n"
      "  ip-address: Use this parameter to provide the rtsp output address.\n"
      "      eg: 127.0.0.1\n"
      "      Default ip is:" DEFAULT_IP "\n"
      "  port: Use this parameter to provide the rtsp output port.\n"
      "      eg: 8900\n"
      "      Default port is:" DEFAULT_PORT "\n"
      "  enable-object-detection: Use this parameter to enable object detection.\n"
      "      eg: TRUE or FALSE\n"
      "  yolo-model-type: \"yolov5\" or \"yolov8\" or \"yolonas\"\n"
      "      Yolo Model version to Execute: Yolov5, Yolov8 or YoloNas\n"
      "      Yolov7 Tflite Model works with yolov8 yolo-model-type\n"
      "  ml-framework: \"snpe\" or \"tflite\" or \"qnn\"\n"
      "      Execute Model in SNPE DLC or TFlite [Default] or QNN format\n"
      "  model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for YOLOV5 DLC: " DEFAULT_SNPE_YOLOV5_MODEL "\n"
      "      Default model path for YOLOV8 DLC: " DEFAULT_SNPE_YOLOV8_MODEL "\n"
      "      Default model path for YOLO NAS DLC: " DEFAULT_SNPE_YOLONAS_MODEL
      "\n" "      Default model path for YOLOV5 TFLITE: "
      DEFAULT_TFLITE_YOLOV5_MODEL "\n"
      "      Default model path for YOLOV8 TFLITE: " DEFAULT_TFLITE_YOLOV8_MODEL
      "\n" "      Default model path for YOLO NAS TFLITE: "
      DEFAULT_TFLITE_YOLONAS_MODEL "\n"
      "      Default model path for YOLO_V7 TFLITE: "
      DEFAULT_TFLITE_YOLOV7_MODEL "\n"
      "      Default model path for YOLOV8 QNN: " DEFAULT_QNN_YOLOV8_MODEL "\n"
      "  labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default labels path for YOLOV5: " DEFAULT_YOLOV5_LABELS "\n"
      "      Default labels path for YOLOV8: " DEFAULT_YOLOV8_LABELS "\n"
      "      Default labels path for YOLOX: "DEFAULT_YOLOX_LABELS"\n"
      "      Default labels path for YOLO NAS: " DEFAULT_YOLONAS_LABELS "\n"
      "      Default labels path for YOLOV7: " DEFAULT_YOLOV7_LABELS "\n"
      "  constants: \"CONSTANTS\"\n"
      "      Constants, offsets and coefficients used by the chosen module \n"
      "      for post-processing of incoming tensors."
      "  Applicable only for some modules\n"
      "      Default constants for YOLOV5: " DEFAULT_CONSTANTS_YOLOV5 "\n"
      "      Default constants for YOLOV8: " DEFAULT_CONSTANTS_YOLOV8 "\n"
      "      Default constants for YOLOX: " DEFAULT_CONSTANTS_YOLOX"\n"
      "      Default constants for YOLO NAS: " DEFAULT_CONSTANTS_YOLONAS "\n"
      "      Default constants for YOLOV7: " DEFAULT_CONSTANTS_YOLOV7 "\n"
      "  threshold: 0 to 100\n"
      "      This is an optional parameter and overides "
      "  default threshold value 40\n"
      "  runtime: \"cpu\" or \"gpu\" or \"dsp\"\n"
      "      This is an optional parameter. If not filled, "
      "  then default dsp runtime is selected\n"
      "  snpe-layers: <json array>\n"
      "      Set output layers for SNPE model. Example:\n"
      "      [\"/heads/Mul\", \"/heads/Sigmoid\"]\n", app_name,
      DEFAULT_CONFIG_FILE);
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
      g_printerr ("\n Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx, &options, config_file);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\n Initializing: Unknown error!\n");
      gst_app_context_free (appctx, &options, config_file);
      return -1;
    }
  } else {
    g_printerr ("\n Failed to create options context!\n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }

  if (config_file == NULL) {
    config_file = DEFAULT_CONFIG_FILE;
  }
  // Initialize GST library.
  gst_init (&argc, &argv);

  if (appctx->sinktype < GST_WAYLANDSINK ||
      appctx->sinktype > GST_RTSP_STREAMING) {
    g_printerr ("\n Invalid user Input:gst-ai-usb-camera-app --help \n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }
  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }

  appctx->pipeline = pipeline;
  // Check for avaiable USB camera
  ret = find_usb_camera_node (appctx);
  if (!ret) {
    g_printerr ("\n Failed to find the USB camera.\n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }
  // Build the pipeline
  if (!file_exists (config_file)) {
    g_printerr ("Invalid config file path: %s\n", config_file);
    gst_app_context_free (appctx, &options, config_file);
    return -EINVAL;
  }

  if (parse_json (config_file, &options, appctx) != 0) {
    gst_app_context_free (appctx, &options, config_file);
    return -EINVAL;
  }

  if (g_strcmp0 (appctx->enable_ml, "TRUE") == 0) {
    // check file config param
    if (options.model_type < GST_MODEL_TYPE_SNPE ||
        options.model_type > GST_MODEL_TYPE_QNN) {
      g_printerr ("Invalid ml-framework option selected\n"
          "Available options:\n"
          "    SNPE: %d\n"
          "    TFLite: %d\n"
          "    QNN: %d\n",
          GST_MODEL_TYPE_SNPE, GST_MODEL_TYPE_TFLITE, GST_MODEL_TYPE_QNN);
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }
    if (options.yolo_model_type < GST_YOLO_TYPE_V5 ||
        options.yolo_model_type > GST_YOLO_TYPE_X) {
      g_printerr ("Invalid model-version option selected\n"
          "Available options:\n"
          "    Yolov5: %d\n"
          "    Yolov8: %d\n"
          "    YoloNas: %d\n"
          "    Yolov7: %d\n"
          "    Yolox: %d\n",
          GST_YOLO_TYPE_V5, GST_YOLO_TYPE_V8, GST_YOLO_TYPE_NAS,
          GST_YOLO_TYPE_V7, GST_YOLO_TYPE_X);
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }
    if (options.threshold < 0 || options.threshold > 100) {
      g_printerr ("Invalid threshold value selected\n"
          "Threshold Value lies between: \n" "    Min: 0\n" "    Max: 100\n");
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }
    if (options.model_type == GST_MODEL_TYPE_QNN && (options.use_cpu == TRUE ||
            options.use_gpu == TRUE)) {
      g_printerr ("QNN Serialized binary is demonstrated only with DSP"
          " runtime.\n");
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }
    if ((options.use_cpu + options.use_gpu + options.use_dsp) > 1) {
      g_print ("Select any one runtime from CPU or GPU or DSP\n");
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }

    if(options.constants == NULL) {
      g_print ("Using default Constants\n");
      options.constants = DEFAULT_CONSTANTS_YOLOV8;
    }

    if (options.model_path == NULL) {
      if (options.model_type == GST_MODEL_TYPE_SNPE) {
        options.model_path =
            (options.yolo_model_type == GST_YOLO_TYPE_V5 ?
            DEFAULT_SNPE_YOLOV5_MODEL :
            (options.yolo_model_type == GST_YOLO_TYPE_V8 ?
                DEFAULT_SNPE_YOLOV8_MODEL : DEFAULT_SNPE_YOLONAS_MODEL));
      } else if (options.model_type == GST_MODEL_TYPE_TFLITE) {
        if (options.yolo_model_type == GST_YOLO_TYPE_V5) {
          options.model_path = DEFAULT_TFLITE_YOLOV5_MODEL;
        } else if (options.yolo_model_type == GST_YOLO_TYPE_NAS) {
          options.model_path = DEFAULT_TFLITE_YOLONAS_MODEL;
        } else if (options.yolo_model_type == GST_YOLO_TYPE_V7) {
          options.model_path = DEFAULT_TFLITE_YOLOV7_MODEL;
        } else if (options.yolo_model_type == GST_YOLO_TYPE_V8) {
          options.model_path = DEFAULT_TFLITE_YOLOV8_MODEL;
        } else {
          g_print ("No tflite model provided, Using default Yolox Model\n");
          options.model_path = DEFAULT_TFLITE_YOLOX_MODEL;
          options.yolo_model_type = GST_YOLO_TYPE_X;
        }
      } else if (options.model_type == GST_MODEL_TYPE_QNN) {
        if (options.yolo_model_type == GST_YOLO_TYPE_V8) {
          options.model_path = DEFAULT_QNN_YOLOV8_MODEL;
        } else {
          g_printerr ("Only YOLOV8 model is supported with QNN runtime\n");
          gst_app_context_free (appctx, &options, config_file);
          return -EINVAL;
        }
      } else {
        g_printerr ("Invalid ml_framework\n");
        gst_app_context_free (appctx, &options, config_file);
        return -EINVAL;
      }
    }

  // Set default layers for SNPE models if not provided
  if (options.snpe_layers == NULL && options.model_type == GST_MODEL_TYPE_SNPE) {
    if (options.yolo_model_type == GST_YOLO_TYPE_V5) {
      options.snpe_layer_count = 3;
      options.snpe_layers = (gchar **) g_malloc (
          sizeof (gchar **) * options.snpe_layer_count);
      options.snpe_layers[0] = g_strdup ("Conv_198");
      options.snpe_layers[1] = g_strdup ("Conv_232");
      options.snpe_layers[2] = g_strdup ("Conv_266");
    } else if (options.yolo_model_type == GST_YOLO_TYPE_V8) {
      options.snpe_layer_count = 2;
      options.snpe_layers = (gchar **) g_malloc (
          sizeof (gchar **) * options.snpe_layer_count);
      options.snpe_layers[0] = g_strdup ("Mul_248");
      options.snpe_layers[1] = g_strdup ("Sigmoid_249");
    } else if (options.yolo_model_type == GST_YOLO_TYPE_NAS) {
      options.snpe_layer_count = 2;
      options.snpe_layers = (gchar **) g_malloc (
          sizeof (gchar **) * options.snpe_layer_count);
      options.snpe_layers[0] = g_strdup ("/heads/Mul");
      options.snpe_layers[1] = g_strdup ("/heads/Sigmoid");
    } else {
      g_printerr ("Given YOLO model type is not supported by SNPE framework\n");
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }
  }

    // Set default label path for execution
    if (options.labels_path == NULL) {
      options.labels_path =
          (options.yolo_model_type == GST_YOLO_TYPE_V5 ? DEFAULT_YOLOV5_LABELS :
          (options.yolo_model_type == GST_YOLO_TYPE_V8 ? DEFAULT_YOLOV8_LABELS :
          (options.yolo_model_type == GST_YOLO_TYPE_V7 ? DEFAULT_YOLOV7_LABELS :
          (options.yolo_model_type == GST_YOLO_TYPE_X ? DEFAULT_YOLOX_LABELS :
          DEFAULT_YOLONAS_LABELS))));
    }

    if (options.model_type == GST_MODEL_TYPE_TFLITE
        && options.constants == NULL) {
      options.constants =
          (options.yolo_model_type == GST_YOLO_TYPE_V5 ? DEFAULT_CONSTANTS_YOLOV5:
          options.yolo_model_type == GST_YOLO_TYPE_NAS ? DEFAULT_CONSTANTS_YOLONAS:
          options.yolo_model_type == GST_YOLO_TYPE_V7 ? DEFAULT_CONSTANTS_YOLOV7:
          options.yolo_model_type == GST_YOLO_TYPE_X ? DEFAULT_CONSTANTS_YOLOX:
          DEFAULT_CONSTANTS_YOLOV8);
    }

    if (!file_exists (options.model_path)) {
      g_print ("Invalid model file path: %s\n", options.model_path);
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }

    if (!file_exists (options.labels_path)) {
      g_print ("Invalid labels file path: %s\n", options.labels_path);
      gst_app_context_free (appctx, &options, config_file);
      return -EINVAL;
    }

    if (options.file_path != NULL) {
      if (!file_exists (options.file_path)) {
        g_print ("Invalid file source path: %s\n", options.file_path);
        gst_app_context_free (appctx, &options, config_file);
        return -EINVAL;
      }
    }

    g_print ("Running app with model: %s and labels: %s\n",
        options.model_path, options.labels_path);
    ret = create_pipe (appctx, &options);
  } else {
    ret = create_preview_pipe (appctx);
  }

  if (!ret) {
    g_printerr ("\n Failed to create GST pipe.\n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }
  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("\n Failed to create Main loop!\n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx, &options, config_file);
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
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

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
  gst_app_context_free (appctx, &options, config_file);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
