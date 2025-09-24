/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Object Detection on Live stream.
 *
 * Description:
 * The application takes live video stream from camera/file/rtsp/USB camera and
 * gives same to Detection TensorFlow Lite or SNPE DLC Model for object detection
 * and display preview with overlayed AI Model output with detection labels.
 *
 * Pipeline for Gstreamer with Camera:
 * qtiqmmfsrc  -> | qmmfsrc_caps (Preview)    -> qtivcomposer
 *                | qmmfsrc_caps (Inference)  -> Pre-process -> Inference
 *                  ->  Post-process          -> qtivcomposer
 *
 * Pipeline for Gstreamer with File source:
 * filesrc -> qtdemux -> h264parse -> v4l2h264dec -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Preprocess -> Inference -> Post-process -> qtivcomposer
 *
 * Pipeline for Gstreamer with RTSP source:
 * rtspsrc -> rtph264depay -> h264parse -> v4l2h264dec -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre-process -> Inference -> Post-process -> qtivcomposer
 * Pipeline for Gstreamer with USB Camera source:
 * v4l2src -> v4l2src_caps -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre-process -> Inference -> Post-process -> qtivcomposer
 *
 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *     Pre-process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post-process: qtimlvdetection -> detection_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_SNPE_YOLOV5_MODEL "/etc/models/yolov5.dlc"
#define DEFAULT_SNPE_YOLOV8_MODEL "/etc/models/yolov8.dlc"
#define DEFAULT_SNPE_YOLONAS_MODEL "/etc/models/yolonas.dlc"
#define DEFAULT_TFLITE_YOLOV8_MODEL "/etc/models/yolov8_det_quantized.tflite"
#define DEFAULT_TFLITE_YOLOX_MODEL "/etc/models/yolox_quantized.tflite"
#define DEFAULT_TFLITE_YOLOV5_MODEL "/etc/models/yolov5.tflite"
#define DEFAULT_TFLITE_YOLONAS_MODEL "/etc/models/yolonas_quantized.tflite"
#define DEFAULT_TFLITE_YOLOV7_MODEL "/etc/models/Yolo-v7-Quantized.tflite"
#define DEFAULT_QNN_YOLOV8_MODEL "/etc/models/yolov8_det_quantized.bin"
#define DEFAULT_YOLOV5_LABELS "/etc/labels/yolov5.json"
#define DEFAULT_YOLOV8_LABELS "/etc/labels/yolov8.json"
#define DEFAULT_YOLOX_LABELS "/etc/labels/yolox.json"
#define DEFAULT_YOLONAS_LABELS "/etc/labels/yolonas.json"
#define DEFAULT_YOLOV7_LABELS "/etc/labels/yolov7.json"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_INFERENCE_WIDTH 640
#define DEFAULT_INFERENCE_HEIGHT 360
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1280
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 720
#define SECONDARY_CAMERA_OUTPUT_WIDTH 1280
#define SECONDARY_CAMERA_OUTPUT_HEIGHT 720
#define USB_CAMERA_OUTPUT_WIDTH 1280
#define USB_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30
#define DEFAULT_OUTPUT_FILENAME "/etc/media/output_object_detection.mp4"
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT "8900"
#define MAX_VID_DEV_CNT 64

/**
 * Default path of config file
 */
#define DEFAULT_CONFIG_FILE "/etc/configs/config_detection.json"

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 8

/**
 * Defalut value of threshold
 */
#define DEFAULT_THRESHOLD_VALUE  40.0

/**
 * default value of delegate
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
  gchar *output_file;
  gchar *output_ip_address;
  gchar *port_num;
  gchar *enable_usb_camera;
  gchar dev_video[16];
  enum GstSinkType sinktype;
  enum GstVideoFormat video_format;
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
  gboolean use_file;
  gboolean use_rtsp;
  gboolean use_usb;
  gboolean use_camera;
  gint width;
  gint height;
  gint framerate;
} GstAppOptions;

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

  if (options->file_path != NULL) {
    g_free ((gpointer)options->file_path);
  }

  if (options->rtsp_ip_port != NULL) {
    g_free ((gpointer)options->rtsp_ip_port);
  }

  if (options->model_path != (gchar *)(&DEFAULT_SNPE_YOLOV5_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_SNPE_YOLOV8_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_SNPE_YOLONAS_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_TFLITE_YOLOV8_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_TFLITE_YOLOX_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_TFLITE_YOLOV5_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_TFLITE_YOLONAS_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_TFLITE_YOLOV7_MODEL) &&
      options->model_path != (gchar *)(&DEFAULT_QNN_YOLOV8_MODEL) &&
      options->model_path != NULL) {
    g_free ((gpointer)options->model_path);
  }

  if (options->labels_path != (gchar *)(&DEFAULT_YOLOV5_LABELS) &&
      options->labels_path != (gchar *)(&DEFAULT_YOLOV8_LABELS) &&
      options->labels_path != (gchar *)(&DEFAULT_YOLOX_LABELS) &&
      options->labels_path != (gchar *)(&DEFAULT_YOLONAS_LABELS) &&
      options->labels_path != (gchar *)(&DEFAULT_YOLOV7_LABELS) &&
      options->labels_path != NULL) {
    g_free ((gpointer)options->labels_path);
  }

  if (options->snpe_layers != NULL) {
    for (gint i = 0; i < options->snpe_layer_count; i++) {
      g_free ((gpointer)options->snpe_layers[i]);
    }
    g_free ((gpointer)options->snpe_layers);
  }


  if (options->output_file != (gchar *)(&DEFAULT_OUTPUT_FILENAME) &&
      options->output_file != NULL) {
    g_free ((gpointer)options->output_file);
  }

  if (options->output_ip_address != (gchar *)(&DEFAULT_IP) &&
      options->output_ip_address != NULL) {
    g_free ((gpointer)options->output_ip_address);
  }

  if (options->port_num != (gchar *)(&DEFAULT_PORT) &&
      options->port_num != NULL) {
    g_free ((gpointer)options->port_num);
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
 * Function to link the dynamic video pad of demux to queue:
 *
 * @param element Gstreamer source element
 * @param pad Gstreamer source element pad
 * @param data sink element data
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;
  GstPadLinkReturn ret;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");
  ret = gst_pad_link (pad, sinkpad);
  if (!ret){
    g_printerr ("Failed to link pad to sinkpad\n");
  }

  gst_object_unref (sinkpad);
}

/**
 * Find USB camera node:
 *
 * @param appctx Application Context object
 */
static gboolean
find_usb_camera_node (GstAppOptions * appctx)
{
  struct v4l2_capability v2cap;
  gint idx = 0, ret = 0, mFd = -1;

  while (idx < MAX_VID_DEV_CNT) {
    memset (appctx->dev_video, 0, sizeof (appctx->dev_video));

    ret = snprintf (appctx->dev_video, sizeof (appctx->dev_video), "/dev/video%d",
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
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST plugins
 * 2. Set Parameters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer
 * @param options Application specific options
 */
static gboolean
create_pipe (GstAppContext * appctx, GstAppOptions * options)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *qmmfsrc_caps_preview = NULL;
  GstElement *queue[QUEUE_COUNT], *tee = NULL;
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvdetection = NULL, *detection_filter = NULL;
  GstElement *qtivcomposer = NULL, *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse = NULL;
  GstElement *v4l2h264dec = NULL, *rtspsrc = NULL, *rtph264depay = NULL;
  GstElement *v4l2h264dec_caps = NULL, *v4l2src = NULL, *v4l2src_caps = NULL;
  GstElement *v4l2h264enc_file = NULL, *v4l2h264enc_rtsp = NULL;
  GstElement *h264parse_enc_rtsp = NULL, *mp4mux = NULL;
  GstElement *filesink = NULL, *qtirtspbin = NULL;
  GstElement *qtivtransform = NULL, *qtivtransform_capsfilter = NULL;
  GstElement *videoconvert = NULL, *jpegdec = NULL, *h264parse_enc_file = NULL;;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstPad *vcomposer_sink;
  GstStructure *delegate_options = NULL;
  GstPad *qtiqmmfsrc_type = NULL;
  gboolean ret = FALSE;
  gchar element_name[128], settings[128];
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
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
    if (!filesrc) {
      g_printerr ("Failed to create filesrc\n");
      goto error_clean_elements;
    }

    // Create qtdemux or demuxing the filesrc
    qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");
    if (!qtdemux) {
      g_printerr ("Failed to create qtdemux\n");
      goto error_clean_elements;
    }

    // Create h264parse elment for parsing the stream
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

    v4l2h264dec_caps = gst_element_factory_make (
        "capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
      goto error_clean_elements;
    }

  } else if (options->use_rtsp) {
    // create rtspsrc plugin for rtsp input
    rtspsrc = gst_element_factory_make ("rtspsrc", "rtspsrc");
    if (!rtspsrc) {
      g_printerr ("Failed to create rtspsrc\n");
      goto error_clean_elements;
    }

    // rtph264depay plugin for rtsp payload parsing
    rtph264depay = gst_element_factory_make ("rtph264depay", "rtph264depay");
    if (!rtph264depay) {
      g_printerr ("Failed to create rtph264depay\n");
      goto error_clean_elements;
    }

    // Create h264parse elment for parsing the stream
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

    v4l2h264dec_caps = gst_element_factory_make (
        "capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
      goto error_clean_elements;
    }

  } else if (options->use_camera) {
    // Create plugin for camera stream
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
    if (!qtiqmmfsrc) {
      g_printerr ("Failed to create qtiqmmfsrc\n");
      goto error_clean_elements;
    }

    // Use capsfilter to define the camera output settings for inference
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
  } else if (options->use_usb) {
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
    if (options->video_format == GST_MJPEG_VIDEO_FORMAT) {
      // 1. Create qtivtransform plugin
      qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
      if (!qtivtransform) {
        g_printerr ("Failed to create qtivtransform\n");
        goto error_clean_elements;
      }
      //transform filter caps
      qtivtransform_capsfilter = gst_element_factory_make ("capsfilter",
          "qtivtransform_capsfilter");
      if (!qtivtransform_capsfilter) {
        g_printerr ("Failed to create qtivtransform_capsfilter\n");
        goto error_clean_elements;
      }
      videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");
      if (!videoconvert) {
        g_printerr ("Failed to create videoconvert\n");
        goto error_clean_elements;
      }
      jpegdec = gst_element_factory_make ("jpegdec", "jpegdec");
      if (!jpegdec) {
        g_printerr ("Failed to create jpegdec\n");
        goto error_clean_elements;
      }
    }
  } else {
    g_printerr ("Invalid source type\n");
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

  // Use tee to send data same data buffer for file and rtsp use cases
  // one for AI inferencing, one for Display composition
  if (options->use_rtsp || options->use_file || options->use_usb) {
    tee = gst_element_factory_make ("tee", "tee");
    if (!tee) {
      g_printerr ("Failed to create tee\n");
      goto error_clean_elements;
    }
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
    qtimlelement = gst_element_factory_make ("qtimlsnpe", "qtimlsnpe");
  } else if (options->model_type == GST_MODEL_TYPE_TFLITE) {
    qtimlelement = gst_element_factory_make ("qtimltflite", "qtimlelement");
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    qtimlelement = gst_element_factory_make ("qtimlqnn", "qtimlelement");
  } else {
    g_printerr ("Invalid model type\n");
    goto error_clean_elements;
  }
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    goto error_clean_elements;
  }

  // Create plugin for ML postprocessing for object detection
  qtimlvdetection = gst_element_factory_make ("qtimlpostprocess",
      "qtimlpostprocess");
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
  detection_filter = gst_element_factory_make ("capsfilter", "detection_filter");
  if (!detection_filter) {
    g_printerr ("Failed to create detection_filter\n");
    goto error_clean_elements;
  }

  if (options->sinktype == GST_WAYLANDSINK) {
  // Create Wayland compositor to render output on Display
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
  } else if (options->sinktype == GST_VIDEO_ENCODE) {
    // Create Encoder plugin
    v4l2h264enc_file = gst_element_factory_make ("v4l2h264enc",
        "v4l2h264enc_file");
    if (!v4l2h264enc_file) {
      g_printerr ("Failed to create v4l2h264enc_file\n");
      goto error_clean_elements;
    }
    // Create frame parser plugin
    h264parse_enc_file = gst_element_factory_make ("h264parse", "h264parse_enc_file");
    if (!h264parse_enc_file) {
      g_printerr ("Failed to create h264parse_enc_file\n");
      goto error_clean_elements;
    }
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
  } else if (options->sinktype == GST_RTSP_STREAMING) {
    // Create Encoder plugin
    v4l2h264enc_rtsp = gst_element_factory_make ("v4l2h264enc",
        "v4l2h264enc_rtsp");
    if (!v4l2h264enc_rtsp) {
      g_printerr ("Failed to create v4l2h264enc_rtsp\n");
      goto error_clean_elements;
    }
    // Create frame parser plugin
    h264parse_enc_rtsp = gst_element_factory_make ("h264parse",
        "h264parse_enc_rtsp");
    if (!h264parse_enc_rtsp) {
      g_printerr ("Failed to create h264parse_enc_rtsp\n");
      goto error_clean_elements;
    }
    // Generic qtirtspbin plugin for streaming
    qtirtspbin = gst_element_factory_make ("qtirtspbin", "qtirtspbin");
    if (!qtirtspbin) {
      g_printerr ("Failed to create qtirtspbin\n");
      goto error_clean_elements;
    }
  } else {
    g_printerr ("Invalid output Sink Type\n");
    goto error_clean_elements;
  }

  // 2. Set properties for all GST plugin elements
  if (options->use_file) {
    // 2.1 Set up the capabilities of file stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (filesrc), "location", options->file_path, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->use_rtsp) {
    //2.2 Set the capabilities of RTSP stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (rtspsrc), "location", options->rtsp_ip_port, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->use_camera) {
    //2.3 Set user provided Camera ID
    g_object_set (G_OBJECT (qtiqmmfsrc), "camera", options->camera_type, NULL);

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
  } else if(options->use_usb) {
    g_object_set (G_OBJECT (v4l2src), "io-mode", "dmabuf", NULL);
    g_object_set (G_OBJECT (v4l2src), "device", options->dev_video, NULL);

    // 2.4 Set the capabilities of USB camera plugin output for inference
    if (options->video_format == GST_NV12_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12",
          "width", G_TYPE_INT, options->width,
          "height", G_TYPE_INT, options->height,
          "framerate", GST_TYPE_FRACTION, options->framerate, 1, NULL);
      g_object_set (G_OBJECT (v4l2src_caps), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    }
    else if (options->video_format == GST_MJPEG_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("image/jpeg",
          "width", G_TYPE_INT, options->width,
          "height", G_TYPE_INT, options->height,
          "framerate", GST_TYPE_FRACTION, options->framerate, 1, NULL);
      g_object_set (G_OBJECT (v4l2src_caps), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
      filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12", NULL);
      g_object_set (G_OBJECT (qtivtransform_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    } else if (options->video_format == GST_YUV2_VIDEO_FORMAT) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "YUY2",
          "width", G_TYPE_INT, options->width,
          "height", G_TYPE_INT, options->height,
          "framerate", GST_TYPE_FRACTION, options->framerate, 1, NULL);
      g_object_set (G_OBJECT (v4l2src_caps), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
    } else {
      g_printerr ("Invalid Video Format Type\n");
      goto error_clean_elements;
    }
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
      g_print ("Using CPU delegate\n");
    } else if (options->use_gpu) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_GPU;
      g_print ("Using GPU delegate\n");
    } else {
      snpe_delegate = GST_ML_SNPE_DELEGATE_DSP;
      g_print ("Using DSP delegate\n");
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
      g_print ("Using DSP delegate\n");
      delegate_options = gst_structure_from_string (
          "QNNExternalDelegate,backend_type=htp", NULL);
      g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
          "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
      g_object_set (G_OBJECT (qtimlelement),
          "external_delegate_path", "libQnnTFLiteDelegate.so", NULL);
      g_object_set (G_OBJECT (qtimlelement),
          "external_delegate_options", delegate_options, NULL);
      gst_structure_free (delegate_options);
    } else {
      g_printerr ("Invalid Runtime Selected\n");
      goto error_clean_elements;
    }
  } else if (options->model_type == GST_MODEL_TYPE_QNN) {
    g_print ("Using DSP delegate\n");
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "backend", "/usr/lib/libQnnHtp.so", NULL);
  } else {
    g_printerr ("Invalid model type\n");
    goto error_clean_elements;
  }

  // 2.6 Set properties for ML postproc plugins - module, layers, threshold
  if (options->model_type == GST_MODEL_TYPE_SNPE) {
    g_object_set (G_OBJECT (qtimlelement), "model",
        options->model_path, NULL);

    g_value_init (&layers, GST_TYPE_ARRAY);
    g_value_init (&value, G_TYPE_STRING);
    for (gint i = 0; i < options->snpe_layer_count; i++) {
      g_value_set_string (&value, options->snpe_layers[i]);
      gst_value_array_append_value (&layers, &value);
    }
    g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);

    // set qtimlvdetection properties
    g_object_set (G_OBJECT (qtimlvdetection), "labels",
        options->labels_path, NULL);
    g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);

    switch (options->yolo_model_type) {
      // YOLO_V5 specific settings
      case GST_YOLO_TYPE_V5:
        // get enum values of module properties from qtimlvdetection plugin
        module_id = get_enum_value (qtimlvdetection, "module", "yolov5");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        break;

      // YOLO_V8 specific settings
      case GST_YOLO_TYPE_V8:
        // get enum values of module property frrom qtimlvdetection plugin
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1){
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        break;

      // YOLO_NAS specific settings
      case GST_YOLO_TYPE_NAS:
        // get enum values of module property frrom qtimlvdetection plugin
        module_id = get_enum_value (qtimlvdetection, "module", "yolo-nas");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolo-nas is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
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
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
        break;
      // YOLO_X specific settings
      case GST_YOLO_TYPE_X:
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
        break;
      // YOLO_V5 specific settings
      case GST_YOLO_TYPE_V5:
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov5");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        break;
        // YOLO_NAS specific settings
      case GST_YOLO_TYPE_NAS:
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolo-nas");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolonas is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        break;
        // YOLOV7 specific settings
      case GST_YOLO_TYPE_V7:
        // set qtimlvdetection properties
        g_object_set (G_OBJECT (qtimlvdetection), "labels",
            options->labels_path, NULL);
        module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
        if (module_id != -1) {
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        break;
      default:
        g_printerr ("Unsupported TFLITE model, Use YoloV5 or "
            "YoloV8 or YoloNas or Yolov7 or Yolox TFLITE model\n");
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
          snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
          g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
        } else {
          g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
          goto error_clean_elements;
        }
        g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
        g_object_set (G_OBJECT (qtimlvdetection), "settings", settings, NULL);
        break;

      default:
        g_printerr ("Unsupported QNN model, use YoloV8 QNN model\n");
        goto error_clean_elements;
    }
  } else {
    g_printerr ("Invalid model_type or yolo_model_type\n");
    goto error_clean_elements;
  }

  if (options->sinktype == GST_WAYLANDSINK) {
    // 2.7 Set the properties for Wayland compositer
    g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    // 2.8 Set the properties of fpsdisplaysink plugin- sync,
    // signal-fps-measurements, text-overlay and video-sink
    g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements",
        TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  } else if (options->sinktype == GST_VIDEO_ENCODE) {
    gst_element_set_enum_property (v4l2h264enc_file, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264enc_file, "output-io-mode",
        "dmabuf-import");
    g_object_set (G_OBJECT (filesink), "location", options->output_file, NULL);
  } else if (options->sinktype == GST_RTSP_STREAMING) {
    gst_element_set_enum_property (v4l2h264enc_rtsp, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264enc_rtsp, "output-io-mode",
        "dmabuf-import");
    g_object_set (G_OBJECT (h264parse_enc_rtsp), "config-interval", 1, NULL);
    g_object_set (G_OBJECT (qtirtspbin), "address", options->output_ip_address,
        "port", options->port_num, NULL);
  } else {
    g_printerr ("Incorrect output sink type\n");
    goto error_clean_elements;
  }

  // 2.9 Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);

  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
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
  } else if (options->use_usb) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2src, v4l2src_caps, tee,
        NULL);
    if (options->video_format == GST_MJPEG_VIDEO_FORMAT) {
      gst_bin_add_many (GST_BIN (appctx->pipeline), qtivtransform,
          qtivtransform_capsfilter, videoconvert, jpegdec, NULL);
    }
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  if (options->sinktype == GST_WAYLANDSINK) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), fpsdisplaysink, NULL);
  } else if (options->sinktype == GST_VIDEO_ENCODE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2h264enc_file, h264parse_enc_file,
        mp4mux, filesink, NULL);
  } else if (options->sinktype == GST_RTSP_STREAMING) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), v4l2h264enc_rtsp,
        h264parse_enc_rtsp, qtirtspbin, NULL);
  } else {
    g_printerr ("Incorrect output sink type\n");
    goto error_clean_elements;
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter,
      qtimlelement, qtimlvdetection, detection_filter,
      qtivcomposer, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create pipeline for object detection
  if (options->use_file) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements could not be linked"
          "for filesrc->qtdemux\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[0], h264parse,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements could not be linked for parse->tee\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_rtsp) {
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->rtph264depay->tee\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_usb) {
    if (options->video_format == GST_YUV2_VIDEO_FORMAT ||
        options->video_format == GST_NV12_VIDEO_FORMAT) {
      ret = gst_element_link_many (v4l2src, v4l2src_caps, tee, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " usbsource->tee\n");
        goto error_clean_pipeline;
      }
    } else if (options->video_format == GST_MJPEG_VIDEO_FORMAT) {
      ret = gst_element_link_many (v4l2src, v4l2src_caps, jpegdec, videoconvert,
          qtivtransform_capsfilter, qtivtransform, tee, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked for"
            " usbsource->jpegdec->tee\n");
        goto error_clean_pipeline;
      }
    }
  } else {
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps_preview,
        queue[2], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "qmmfsource->qmmfsrc_caps_preview\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[4], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "qmmfsource->qmmfsrc_caps\n");
      goto error_clean_pipeline;
    }
  }

  if (options->use_rtsp || options->use_file || options->use_usb) {
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

  if (options->sinktype == GST_WAYLANDSINK) {
    ret = gst_element_link_many (qtivcomposer, queue[3], fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qtivcomposer->fpsdisplaysink.\n");
      goto error_clean_pipeline;
    }
  } else if (options->sinktype == GST_VIDEO_ENCODE) {
    ret = gst_element_link_many (qtivcomposer, queue[3], v4l2h264enc_file,
        h264parse_enc_file, mp4mux, filesink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qtivcomposer->filesink\n");
      goto error_clean_pipeline;
    }
  } else if (options->sinktype == GST_RTSP_STREAMING) {
    ret = gst_element_link_many (qtivcomposer, queue[3], v4l2h264enc_rtsp,
        h264parse_enc_rtsp, qtirtspbin, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qtivcomposer->qtirtspbin\n");
      goto error_clean_pipeline;
    }
  } else {
      g_printerr ("Invalid output sink type\n");
      goto error_clean_pipeline;
  }

  if (options->use_rtsp || options->use_file || options->use_usb) {
    ret = gst_element_link_many (tee, queue[4], qtimlvconverter,
        queue[5], qtimlelement, queue[6], qtimlvdetection,
        detection_filter, queue[7], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " pre proc -> ml framework -> post proc.\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_camera) {
    ret = gst_element_link_many (queue[4], qtimlvconverter,
        queue[5], qtimlelement, queue[6], qtimlvdetection,
        detection_filter, queue[7], qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
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
    g_signal_connect (rtspsrc ,"pad-added", G_CALLBACK (on_pad_added), queue[0]);
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

  // Set overlay window size for Detection to display text labels
  vcomposer_sink = gst_element_get_static_pad (qtivcomposer, "sink_0");
  if (vcomposer_sink == NULL) {
    g_printerr ("Sink pad 0 of vcomposer couldnt' be retrieved\n");
    goto error_clean_pipeline;
  }

  GValue position = G_VALUE_INIT;
  GValue dimension = G_VALUE_INIT;

  g_value_init (&position, GST_TYPE_ARRAY);
  g_value_init (&dimension, GST_TYPE_ARRAY);

  pos_vals[0] = 0; pos_vals[1] = 0;
  dim_vals[0] = 1920; dim_vals[1] = 1080;

  build_pad_property (&position, pos_vals, 2);
  build_pad_property (&dimension, dim_vals, 2);

  g_object_set_property (G_OBJECT (vcomposer_sink), "position", &position);
  g_object_set_property (G_OBJECT (vcomposer_sink), "dimensions", &dimension);

  g_value_unset (&position);
  g_value_unset (&dimension);
  gst_object_unref (vcomposer_sink);

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  appctx->pipeline = NULL;
  return FALSE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, &qmmfsrc_caps_preview,
      &filesrc, &qtdemux, &h264parse, &v4l2h264dec, &v4l2h264dec_caps,
      &rtspsrc, &rtph264depay, &v4l2src, &v4l2src_caps, &tee, &qtimlvconverter,
      &qtimlelement, &qtimlvdetection, &qtivcomposer, &detection_filter,
      &waylandsink, &fpsdisplaysink, &filesink, &qtirtspbin,
      &v4l2h264enc_file, &v4l2h264enc_rtsp, h264parse_enc_rtsp, &mp4mux,
      &qtivtransform, &qtivtransform_capsfilter, &videoconvert, &jpegdec, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
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
  JsonArray *snpe_layers = NULL;
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

  if (json_object_has_member (root_obj, "enable-usb-camera")) {
    const gchar* usb_camera =
        json_object_get_string_member (root_obj, "enable-usb-camera");
    if (g_strcmp0 (usb_camera, "TRUE") == 0) {
      options->use_usb = TRUE;
    } else if (g_strcmp0 (usb_camera, "FALSE") == 0) {
      options->use_usb = FALSE;
    } else {
      gst_printerr ("enable-usb-camera can only be one of "
          "\"TRUE\", \"FALSE\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "yolo-model-type")) {
    const gchar* yolo_model_type =
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
          "\"yolov5\", \"yolov8\" or \"yolonas\" or \"yolov7\" or \"yolox\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "ml-framework")) {
    const gchar* framework =
        json_object_get_string_member (root_obj, "ml-framework");
    if (g_strcmp0 (framework, "snpe") == 0)
      options->model_type = GST_MODEL_TYPE_SNPE;
    else if (g_strcmp0 (framework, "tflite") == 0)
      options->model_type = GST_MODEL_TYPE_TFLITE;
    else if (g_strcmp0 (framework, "qnn") == 0) {
      options->model_type = GST_MODEL_TYPE_QNN;
    }
    else {
      gst_printerr ("ml-framework can only be one of "
          "\"snpe\", \"tflite\" or \"qnn\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "output-file")) {
    options->output_file =
        g_strdup (json_object_get_string_member (root_obj, "output-file"));
    g_print ("Output File Name : %s\n", options->output_file);
  }

  if (json_object_has_member (root_obj, "model")) {
    options->model_path =
        g_strdup (json_object_get_string_member (root_obj, "model"));
  }

  if (json_object_has_member (root_obj, "labels")) {
    options->labels_path =
        g_strdup (json_object_get_string_member (root_obj, "labels"));
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
      gst_printerr ("Runtime can only be one of \"cpu\", \"dsp\" and \"gpu\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "snpe-layers")) {
    snpe_layers = json_object_get_array_member (root_obj, "snpe-layers");
    options->snpe_layer_count = json_array_get_length (snpe_layers);
    options->snpe_layers = (gchar **) g_malloc (
        sizeof (gchar **) * options->snpe_layer_count);

    for (gint i = 0; i < options->snpe_layer_count; i++) {
      options->snpe_layers[i] =
          g_strdup (json_array_get_string_element (snpe_layers, i));
    }
  }

  if (json_object_has_member (root_obj, "video-format")) {
    const gchar *video_format_type =
        json_object_get_string_member (root_obj, "video-format");
    if (g_strcmp0 (video_format_type, "nv12") == 0) {
      options->video_format = GST_NV12_VIDEO_FORMAT;
      g_print ("Selected Video Format : NV12 \n");
    } else if (g_strcmp0 (video_format_type, "yuy2") == 0) {
      options->video_format = GST_YUV2_VIDEO_FORMAT;
      g_print ("Selected Video Format : YUY2\n");
    } else if (g_strcmp0 (video_format_type, "mjpeg") == 0) {
      options->video_format = GST_MJPEG_VIDEO_FORMAT;
      g_print ("Selected Video Format : MJPEG\n");
    } else {
      gst_printerr ("video-format can only be one of "
          "\"nv12\", \"yuy2\" or \"mjpeg\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "output-type")) {
    const gchar *output_type =
        json_object_get_string_member (root_obj, "output-type");
    if (g_strcmp0 (output_type, "waylandsink") == 0)
      options->sinktype = GST_WAYLANDSINK;
    else if (g_strcmp0 (output_type, "filesink") == 0)
      options->sinktype = GST_VIDEO_ENCODE;
    else if (g_strcmp0 (output_type, "rtspsink") == 0)
      options->sinktype = GST_RTSP_STREAMING;
    else {
      gst_printerr ("output-type can only be one of "
          "\"waylandsink\", \"filesink\" or \"rtspsink\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  if (json_object_has_member (root_obj, "output-ip-address")) {
    options->output_ip_address =
        g_strdup (json_object_get_string_member (root_obj, "output-ip-address"));
    g_print ("Output Ip Address : %s\n", options->output_ip_address);
  }

  if (json_object_has_member (root_obj, "port")) {
    options->port_num =
        g_strdup (json_object_get_string_member (root_obj, "port"));
    g_print ("Port Number : %s\n", options->port_num);
  }

  if (json_object_has_member (root_obj, "width")) {
    options->width = json_object_get_int_member (root_obj, "width");
    g_print ("Width : %d\n", options->width);
  }

  if (json_object_has_member (root_obj, "height")) {
    options->height = json_object_get_int_member (root_obj, "height");
    g_print ("Height : %d\n", options->height);
  }

  if (json_object_has_member (root_obj, "framerate")) {
    options->framerate = json_object_get_int_member (root_obj, "framerate");
    g_print ("Frame Rate : %d\n", options->framerate);
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
  gchar help_description[4096];
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;
  GstAppOptions options = {};
  gchar *config_file = NULL;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // set default value
  options.file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.use_cpu = FALSE, options.use_gpu = FALSE, options.use_dsp = FALSE;
  options.use_file = FALSE, options.use_rtsp = FALSE, options.use_camera = FALSE;
  options.use_usb = FALSE;
  options.threshold = DEFAULT_THRESHOLD_VALUE;
  options.delegate_type = DEFAULT_SNPE_DELEGATE;
  options.model_type = GST_MODEL_TYPE_SNPE;
  options.camera_type = GST_CAMERA_TYPE_NONE;
  options.yolo_model_type = GST_YOLO_TYPE_NAS;
  options.model_path = NULL;
  options.labels_path = NULL;
  options.snpe_layers = NULL;
  options.width = USB_CAMERA_OUTPUT_WIDTH;
  options.height = USB_CAMERA_OUTPUT_HEIGHT;
  options.video_format = GST_NV12_VIDEO_FORMAT;
  options.sinktype = GST_WAYLANDSINK;
  options.output_file = DEFAULT_OUTPUT_FILENAME;
  options.output_ip_address = DEFAULT_IP;
  options.port_num = DEFAULT_PORT;
  options.framerate = DEFAULT_CAMERA_FRAME_RATE;

  // Structure to define the user options selected
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

  gchar camera_description[128] = {};

  if (camera_is_available) {
    snprintf (camera_description, sizeof (camera_description),
      "camera: 0 or 1\n"
      "      Select (0) for Primary Camera and (1) for secondary one.\n"
    );
  }

  snprintf (help_description, 4095, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates Object Detection on Input Stream\n"
      "\nConfig file Fields:\n"
      "  %s"
      "  file-path: \"/PATH\"\n"
      "      File source path\n"
      "  rtsp-ip-port: \"rtsp://<ip>:<port>/<stream>\"\n"
      "      Use this parameter to provide the rtsp input.\n"
      "      Input should be provided as rtsp://<ip>:<port>/<stream>,\n"
      "      eg: rtsp://192.168.1.110:8554/live.mkv\n"
      "  enable-usb-camera: Use this Parameter to enable-usb-camera\n"
      "      It can take TRUE or FALSE as input\n"
      "  yolo-model-type: \"yolov5\" or \"yolov8\" or \"yolox\" or \"yolonas\"\n"
      "      Yolo Model version to Execute: Yolov5, Yolov8 or YoloNas "
      "or Yolox [Default]\n"
      "  ml-framework: \"snpe\" or \"tflite\" or \"qnn\"\n"
      "      Execute Model in SNPE DLC [Default] or TFlite format\n"
      "  model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for YOLOV5 DLC: "DEFAULT_SNPE_YOLOV5_MODEL"\n"
      "      Default model path for YOLOV8 DLC: "DEFAULT_SNPE_YOLOV8_MODEL"\n"
      "      Default model path for YOLO NAS DLC: "DEFAULT_SNPE_YOLONAS_MODEL"\n"
      "      Default model path for YOLOV5 TFLITE: "
      DEFAULT_TFLITE_YOLOV5_MODEL"\n"
      "      Default model path for YOLOV8 TFLITE: "
      DEFAULT_TFLITE_YOLOV8_MODEL"\n"
      "      Default model path for YOLOX TFLITE: "
      DEFAULT_TFLITE_YOLOX_MODEL"\n"
      "      Default model path for YOLO NAS TFLITE: "
      DEFAULT_TFLITE_YOLONAS_MODEL"\n"
      "      Default model path for YOLO_V7 TFLITE: "
      DEFAULT_TFLITE_YOLOV7_MODEL"\n"
      "      Default model path for YOLOV8 QNN: "
      DEFAULT_QNN_YOLOV8_MODEL"\n"
      "  labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default labels path for YOLOV5: "DEFAULT_YOLOV5_LABELS"\n"
      "      Default labels path for YOLOV8: "DEFAULT_YOLOV8_LABELS"\n"
      "      Default labels path for YOLOX: "DEFAULT_YOLOX_LABELS"\n"
      "      Default labels path for YOLO NAS: "DEFAULT_YOLONAS_LABELS"\n"
      "      Default labels path for YOLOV7: "DEFAULT_YOLOV7_LABELS"\n"
      "  threshold: 0 to 100\n"
      "      This is an optional parameter and overides "
      "default threshold value 40\n"
      "  output-type: It can be either be waylandsink, filesink or rtspsink\n"
      "  output-file: Use this Parameter to set output file path\n"
      "      Default output file path is:" DEFAULT_OUTPUT_FILENAME "\n"
      "  video-format: Video Type format can be nv12, yuy2 or mjpeg\n"
      "      It is applicable only for USB Camera Source\n"
      "  width: USB Camera Resolution width\n"
      "  height: USB Camera Resolution Height\n"
      "  framerate: USB Camera Frame Rate\n"
      "  runtime: \"cpu\" or \"gpu\" or \"dsp\"\n"
      "      This is an optional parameter. If not filled, "
      "then default dsp runtime is selected\n"
      "  snpe-layers: <json array>\n"
      "      Set output layers for SNPE model. Example:\n"
      "      [\"/heads/Mul\", \"/heads/Sigmoid\"]\n"
      "  output-ip-address: Use this parameter to provide the rtsp output address.\n"
      "      eg: 127.0.0.1\n"
      "      Default ip is:" DEFAULT_IP "\n"
      "  port: Use this parameter to provide the rtsp output port.\n"
      "      eg: 8900\n"
      "      Default port is:" DEFAULT_PORT "\n",
      app_name, DEFAULT_CONFIG_FILE, camera_description);
  help_description[4095] = '\0';

    // Parse command line entries
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

  if (! (options.use_file || (options.camera_type != GST_CAMERA_TYPE_NONE) ||
      options.use_rtsp || options.use_usb == TRUE)) {
    options.use_camera = TRUE;
    options.camera_type = GST_CAMERA_TYPE_PRIMARY;
    g_print ("Using PRIMARY camera by default, Not valid camera id selected\n");
  }

  // Checking camera ID passed by the user.
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
  if (options.use_file + options.use_camera + options.use_rtsp +
      options.use_usb> 1) {
    g_printerr ("Select anyone source type either Camera or File or RTSP\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_file) {
    g_print ("File Source is Selected\n");
  } else if (options.use_rtsp) {
    g_print ("RTSP Source is Selected\n");
  } else if (options.use_usb) {
    g_print ("USB Camera Source is Selected\n");
  }
  else {
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

  if (options.yolo_model_type < GST_YOLO_TYPE_V5 ||
      options.yolo_model_type > GST_YOLO_TYPE_X) {
    g_printerr ("Invalid model-version option selected\n"
        "Available options:\n"
        "    Yolov5: %d\n"
        "    Yolov8: %d\n"
        "    YoloNas: %d\n"
        "    Yolov7: %d\n"
        "    Yolox: %d\n",
        GST_YOLO_TYPE_V5, GST_YOLO_TYPE_V8, GST_YOLO_TYPE_NAS, GST_YOLO_TYPE_V7,
        GST_YOLO_TYPE_X);
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

  if (options.model_type == GST_MODEL_TYPE_QNN && (options.use_cpu == TRUE ||
      options.use_gpu == TRUE)) {
    g_printerr ("QNN Serialized binary is demonstrated only with DSP"
        " runtime.\n");
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
      options.model_path =
          (options.yolo_model_type == GST_YOLO_TYPE_V5 ?
          DEFAULT_SNPE_YOLOV5_MODEL :
          (options.yolo_model_type == GST_YOLO_TYPE_V8 ?
          DEFAULT_SNPE_YOLOV8_MODEL :
          DEFAULT_SNPE_YOLONAS_MODEL));
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
        gst_app_context_free (&appctx, &options, config_file);
        return -EINVAL;
      }
    }
    else {
      g_printerr ("Invalid ml_framework\n");
      gst_app_context_free (&appctx, &options, config_file);
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
      gst_app_context_free (&appctx, &options, config_file);
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
  if (options.use_usb == TRUE) {
    ret = find_usb_camera_node (&options);
    if (!ret) {
      g_printerr ("\n Failed to find the USB camera.\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -1;
    }
  }

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
