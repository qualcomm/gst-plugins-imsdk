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
 *     --Store the Video encoder output
 *     --Dump the Camera YUV to a file
 *     --Live RTSP streaming
*      --Object detection and Live Camera Preview on Display
 *
 * Usage:
 * gst-ai-usb-camera-app --od-config-file=/etc/config/config-usb-camera-app.json
 *
 * Help:
 * gst-ai-usb-camera-app --help
 *
 * *******************************************************************************
 * Dump the Camera YUV to a filesink:
 *     camerasrc->qtivtransform->capsfilter->filesink
 * Live Camera Preview on Display:
 *     camerasrc->qtivtransform->capsfilter->waylandsink
 * Pipeline For the Video Encoding:
 * camerasrc->qtivtransform->capsfilter->v4l2h264enc->h264parse->mp4mux->filesink
 * Pipeline For the RTSPSTREAMING:
 * camerasrc->qtivtransform->capsfilter->v4l2h264enc->h264parse->qtirtspbin
 *Object detection and Live Camera Preview on Display
 *camerasrc->capsfilter->tee->qtivcomposer-->waylandsink
 *  tee->qtivtransform-->qtimlvconverter-->qtimlplugin-->qtimlvdetection-->caps
 * *******************************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_OP_YUV_FILENAME "/etc/media/yuv_dump%d.yuv"
#define DEFAULT_OP_MP4_FILENAME "/etc/media/video.mp4"
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_FRAMERATE 30
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT "8900"
#define DEFAULT_PROP_MPOINT "/live"
#define DEFAULT_CONFIG_FILE "/etc/configs/config-usb-camera-app.json"
#define MAX_VID_DEV_CNT 64

#define GST_APP_SUMMARY                                                       \
  "This app enables the users to use USB camera with different o/p          "\
  "  as PREVIEW,encode(MP4),YUVDUMP & RTSP or object-detection with PREVIEW \n"\
  "\nCommand:\n"                                                              \
  "  gst-ai-usb-camera-app --od-config-file=/etc/config/config-usb-camera-app.json"\
  "\nOutput:\n"                                                               \
  "  Upon execution, application will generates output as user selected. \n"  \
  "  In case of a PREVIEW, the output video will be displayed. \n"            \
  "  In case of a object detection enable, the o/p video will be displayed. \n"\
  "  In case Video Encoding(MP4) the o/p stored at /etc/media/video.mp4 \n" \
  "  In RTSP Streaming the o/p video stream is generated to play on host.\n"\
  "  In case YUVDUMP the output video stored at /etc/media/yuv_dump%d.yuv"

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
  GstCameraSourceType camera_type;
  GstModelType model_type;
  GstYoloModelType yolo_model_type;
  gdouble threshold;
  gint delegate_type;
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
  ctx->output_file = NULL;
  ctx->ip_address = NULL;
  ctx->port_num = NULL;
  ctx->sinktype = GST_WAYLANDSINK;
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

  if (options->model_path != NULL) {
    g_free ((gpointer) options->model_path);
  }

  if (options->labels_path != NULL) {
    g_free ((gpointer) options->labels_path);
  }

  if (options->constants != NULL) {
    g_free ((gpointer) options->constants);
  }

  if (config_file != NULL) {
    g_free ((gpointer) config_file);
  }

  g_free (appctx->ip_address);
  g_free (appctx->port_num);

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx != NULL)
    g_free (appctx);
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

  gboolean camera_is_available = is_camera_available ();

  if (camera_is_available) {
    if (json_object_has_member (root_obj, "camera"))
      options->camera_type = json_object_get_int_member (root_obj, "camera");
  }

  if (json_object_has_member (root_obj, "file-path")) {
    options->file_path =
        g_strdup (json_object_get_string_member (root_obj, "file-path"));
  }

  if (json_object_has_member (root_obj, "width")) {
    appctx->width = json_object_get_int_member (root_obj, "width");
  }

  if (json_object_has_member (root_obj, "height")) {
    appctx->height = json_object_get_int_member (root_obj, "height");
  }

  if (json_object_has_member (root_obj, "framerate")) {
    appctx->framerate = json_object_get_int_member (root_obj, "framerate");
  }

  if (json_object_has_member (root_obj, "output")) {
    const gchar *output_type =
        g_strdup (json_object_get_string_member (root_obj, "output"));
    if (g_strcmp0 (output_type, "PREVIEW") == 0)
      appctx->sinktype = GST_WAYLANDSINK;
    else if (g_strcmp0 (output_type, "MP4") == 0 )
      appctx->sinktype = GST_VIDEO_ENCODE;
    else if (g_strcmp0 (output_type, "YUVDUMP") == 0 )
      appctx->sinktype = GST_YUV_DUMP;
    else if (g_strcmp0 (output_type, "RTSP") == 0 )
      appctx->sinktype = GST_RTSP_STREAMING;
  }

  if (json_object_has_member (root_obj, "ip-address")) {
    appctx->ip_address =
        g_strdup (json_object_get_string_member (root_obj, "ip-address"));
  }

  if (json_object_has_member (root_obj, "port")) {
    appctx->port_num =
        g_strdup (json_object_get_string_member (root_obj, "port"));
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
      else {
        gst_printerr ("yolo-model-type can only be one of "
            "\"yolov5\", \"yolov8\" or \"yolonas\" or \"yolov7\"\n");
        g_object_unref (parser);
        return -1;
      }
      g_print ("yolo-model-type : %s\n", yolo_model_type);
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
      const gchar *delegate =
          json_object_get_string_member (root_obj, "runtime");

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
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_pipe (GstCameraAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *camerasrc, *capsfilter, *waylandsink, *filesink, *v4l2h264enc;
  GstElement *qtivtransform, *h264parse, *mp4mux, *queue, *qtirtspbin;
  GstCaps *filtercaps;
  GstStructure *fcontrols;
  gboolean ret = FALSE;

  // Create comman element
  camerasrc = gst_element_factory_make ("v4l2src", "camerasrc");
  qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  if (!camerasrc || !capsfilter || !qtivtransform) {
    g_printerr ("\n YUV dump elements could not be created. Exiting.\n");
    return FALSE;
  }

  // Set properties for element
  g_object_set (G_OBJECT (camerasrc), "io-mode", "dmabuf-import", NULL);
  g_object_set (G_OBJECT (camerasrc), "device", appctx->dev_video, NULL);
  g_object_set (G_OBJECT (qtivtransform), "rotate", 0, NULL);

  // Set the source elements capability
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, appctx->framerate, 1, NULL);

  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // check the sink type and create the sink elements
  if (appctx->sinktype == GST_WAYLANDSINK) {
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("\n waylandsink element not created. Exiting.\n");
      return FALSE;
    }
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
        capsfilter, waylandsink, NULL);

    g_print ("\n Link pipeline for display elements ..\n");

    ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter,
        waylandsink, NULL);
    if (!ret) {
      g_printerr ("\n Display Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
          qtivtransform, waylandsink, NULL);
      return FALSE;
    }
  } else if (appctx->sinktype == GST_YUV_DUMP) {
    // set the output file location for filesink element
    appctx->output_file = DEFAULT_OP_YUV_FILENAME;
    filesink = gst_element_factory_make ("multifilesink", "filesink");
    if (!filesink) {
      g_printerr ("\n YUV dump elements could not be created. Exiting.\n");
      return FALSE;
    }
    g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);
    g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);
    g_object_set (G_OBJECT (filesink), "max-files", 2, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
        capsfilter, filesink, NULL);

    g_print ("\n Link pipeline elements for yuv dump..\n");

    ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter, filesink,
        NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
          qtivtransform, filesink, NULL);
      return FALSE;
    }
  } else if (appctx->sinktype == GST_VIDEO_ENCODE ||
      appctx->sinktype == GST_RTSP_STREAMING) {
    // Create v4l2h264enc element and set the properties
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    // Create h264parse element for parsing the stream
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!v4l2h264enc || !h264parse) {
      g_printerr ("\n Video Encoder elements could not be created \n");
      return FALSE;
    }
    g_object_set (G_OBJECT (v4l2h264enc), "capture-io-mode", GST_V4L2_IO_DMABUF,
        NULL);
    g_object_set (G_OBJECT (v4l2h264enc), "output-io-mode",
        GST_V4L2_IO_DMABUF_IMPORT, NULL);
    g_object_set (G_OBJECT (h264parse), "config-interval", -1, NULL);

    if (appctx->sinktype == GST_RTSP_STREAMING) {
      // Set bitrate for streaming usecase
      fcontrols =
          gst_structure_from_string
          ("fcontrols,video_bitrate=10000000,video_bitrate_mode=0", NULL);
      g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);

      queue = gst_element_factory_make ("queue", "queue");
      qtirtspbin = gst_element_factory_make ("qtirtspbin", "qtirtspbin");
      if (!queue || !qtirtspbin) {
        g_printerr ("\n Video Streaming elements could not be created \n");
        return FALSE;
      }
      g_object_set (G_OBJECT (qtirtspbin), "address", appctx->ip_address, NULL);
      g_object_set (G_OBJECT (qtirtspbin), "port", appctx->port_num, NULL);

      gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
          capsfilter, v4l2h264enc, h264parse, queue, qtirtspbin, NULL);

      g_print ("\n Link pipeline for video streaming elements ..\n");

      ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter,
          v4l2h264enc, h264parse, queue, qtirtspbin, NULL);
      if (!ret) {
        g_printerr
            ("\n Pipeline video streaming elements cannot be linked. Exiting.\n");
        gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
            v4l2h264enc, qtivtransform, h264parse, queue, qtirtspbin, NULL);
        return FALSE;
      }
    } else if (appctx->sinktype == GST_VIDEO_ENCODE) {
      fcontrols = gst_structure_from_string ("fcontrols,video_bitrate_mode=0",
          NULL);
      g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);
      // Create mp4mux element for muxing the stream
      mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

      // Create filesink element for storing the encoding stream
      filesink = gst_element_factory_make ("filesink", "filesink");
      if (!mp4mux || !filesink) {
        g_printerr ("\n Elements for video storage could not be created \n");
        return FALSE;
      }
      appctx->output_file = DEFAULT_OP_MP4_FILENAME;
      g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

      gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
          capsfilter, v4l2h264enc, h264parse, mp4mux, filesink, NULL);

      g_print ("\n Link pipeline elements for encoder..\n");

      // Linking the encoder stream
      ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter,
          v4l2h264enc, h264parse, mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr
            ("\n Video Encoder Pipeline elements cannot be linked. Exiting.\n");
        gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
            qtivtransform, v4l2h264enc, h264parse, mp4mux, filesink, NULL);
        return FALSE;
      }
    }
  }

  g_print ("\n All elements are linked successfully\n");
  return TRUE;
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
create_od_pipe (GstCameraAppContext * appctx, GstAppOptions * options)
{
  GstElement *camsrc = NULL, *camsrc_caps = NULL, *qtivtransform = NULL;
  GstElement *camsrc_caps_preview = NULL;
  GstElement *queue[QUEUE_COUNT], *tee = NULL;
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvdetection = NULL, *detection_filter = NULL;
  GstElement *qtivcomposer = NULL, *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstPad *vcomposer_sink;
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  gdouble alpha_value;
  gint pos_vals[2], dim_vals[2];
  gint primary_camera_width = DEFAULT_WIDTH;
  gint primary_camera_height = DEFAULT_HEIGHT;
  gint inference_width = DEFAULT_WIDTH;
  gint inference_height = DEFAULT_HEIGHT;
  gint framerate = DEFAULT_FRAMERATE;
  gint module_id;

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }

  // 1. Create the elements or Plugins
  camsrc = gst_element_factory_make ("v4l2src", "camsrc");
  if (!camsrc) {
    g_printerr ("Failed to create camsrc\n");
    goto error_clean_elements;
  }

  // Use capsfilter to define the camera output settings for inference
  camsrc_caps = gst_element_factory_make ("capsfilter", "camsrc_caps");
  if (!camsrc_caps) {
    g_printerr ("Failed to create camsrc_caps\n");
    goto error_clean_elements;
  }

  // Use capsfilter to define the camera output settings for preview
  camsrc_caps_preview = gst_element_factory_make ("capsfilter",
      "camsrc_caps_preview");
  if (!camsrc_caps_preview) {
    g_printerr ("Failed to create camsrc_caps_preview\n");
    goto error_clean_elements;
  }

  qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");

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
    g_printerr ("Invalid model type for plugin SNPE/TFLITE \n");
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

  // 2. Set properties for all GST plugin elements
  g_object_set (G_OBJECT (camsrc), "io-mode", "dmabuf-import", NULL);
  g_object_set (G_OBJECT (camsrc), "device", appctx->dev_video, NULL);

  // 2.4 Set the capabilities of camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "YUY2",
      "width", G_TYPE_INT, primary_camera_width,
      "height", G_TYPE_INT, primary_camera_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
  g_object_set (G_OBJECT (camsrc_caps_preview), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.4 Set the capabilities of camera plugin output for inference
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, inference_width,
      "height", G_TYPE_INT, inference_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
  g_object_set (G_OBJECT (camsrc_caps), "caps", filtercaps, NULL);
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
    switch (options->yolo_model_type) {
        // YOLO_V5 specific settings
      case GST_YOLO_TYPE_V5:
        g_print ("Using GST_YOLO_TYPE_V5 \n");
        g_object_set (G_OBJECT (qtimlelement), "model",
            options->model_path, NULL);
        g_value_set_string (&value, "Conv_198");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Conv_232");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Conv_266");
        gst_value_array_append_value (&layers, &value);
        g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);
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
        g_value_set_string (&value, "Mul_248");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Sigmoid_249");
        gst_value_array_append_value (&layers, &value);
        g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);
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
        g_value_set_string (&value, "/heads/Mul");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "/heads/Sigmoid");
        gst_value_array_append_value (&layers, &value);
        g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);
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

  // 2.7 Set the properties for Wayland compositer
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // 2.8 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements",
      TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);

  // 2.9 Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);

  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline), camsrc,
      camsrc_caps, camsrc_caps_preview, tee, qtivtransform, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter,
      qtimlelement, qtimlvdetection, detection_filter,
      qtivcomposer, fpsdisplaysink, waylandsink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create pipeline for object detection
  ret = gst_element_link_many (camsrc, camsrc_caps_preview,
      queue[1], tee, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "qmmfsource->composer\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee, qtivtransform, camsrc_caps, queue[4], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "qmmfsource->converter\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (tee, queue[2], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        " camsrc_caps_preview -> qtivcomposer.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (queue[4], qtimlvconverter,
      queue[5], qtimlelement, queue[6], qtimlvdetection,
      detection_filter, queue[7], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "pre proc -> ml framework -> post proc.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (qtivcomposer, queue[3], fpsdisplaysink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "qtivcomposer->fpsdisplaysink.\n");
    goto error_clean_pipeline;
  }

  // Set overlay window size for Detection to display text labels
  vcomposer_sink = gst_element_get_static_pad (qtivcomposer, "sink_1");
  if (vcomposer_sink == NULL) {
    g_printerr ("Sink pad 0 of vcomposer couldnt' be retrieved\n");
    goto error_clean_pipeline;
  }

  GValue position = G_VALUE_INIT;
  GValue dimension = G_VALUE_INIT;

  g_value_init (&position, GST_TYPE_ARRAY);
  g_value_init (&dimension, GST_TYPE_ARRAY);

  pos_vals[0] = 0; pos_vals[1] = 0;
  dim_vals[0] = 640; dim_vals[1] = 480;

  build_pad_property (&position, pos_vals, 2);
  build_pad_property (&dimension, dim_vals, 2);

  g_object_set_property (G_OBJECT (vcomposer_sink), "position", &position);
  g_object_set_property (G_OBJECT (vcomposer_sink), "dimensions", &dimension);

  // Set the alpha channel value for object segmentation.
  alpha_value = 0.5;
  g_object_set (vcomposer_sink, "alpha", &alpha_value, NULL);

  g_value_unset (&position);
  g_value_unset (&dimension);
  gst_object_unref (vcomposer_sink);

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  appctx->pipeline = NULL;
  return FALSE;

error_clean_elements:
  cleanup_gst (&camsrc, &camsrc_caps, &camsrc_caps_preview,
      &tee, &qtimlvconverter, &qtimlelement,
      &qtimlvdetection, &qtivcomposer, &detection_filter,
      &waylandsink, &fpsdisplaysink, NULL);
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

  // Configure input parameters
  GOptionEntry entries[] = {
    { "od-config-file", 'c', 0, G_OPTION_ARG_STRING, &config_file,
      "Path to config file for object detection\n",
      "/etc/configs/config-usb-camera-app.json", },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
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

  if (g_strcmp0 (appctx->enable_ml, "TRUE") == 0 ) {
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
        options.yolo_model_type > GST_YOLO_TYPE_V7) {
      g_printerr ("Invalid model-version option selected\n"
          "Available options:\n"
          "    Yolov5: %d\n"
          "    Yolov8: %d\n"
          "    YoloNas: %d\n"
          "    Yolov7: %d\n",
          GST_YOLO_TYPE_V5, GST_YOLO_TYPE_V8, GST_YOLO_TYPE_NAS,
          GST_YOLO_TYPE_V7);
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

    g_print ("Running app with model: %s and labels: %s\n",
        options.model_path, options.labels_path);
    ret = create_od_pipe (appctx, &options);
  } else {
    ret = create_pipe (appctx);
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
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("\n Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx, &options, config_file);
    return -1;
  }

  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("\n Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx, &options, config_file);
      return -1;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\n Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("\n Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\n Pipeline state change was successful\n");
      break;
  }

  // Start the main loop
  g_print ("\n Application is running... \n");

  if (appctx->sinktype == GST_RTSP_STREAMING)
    g_print ("\n Stream ready at rtsp://%s:%s%s \n",
        appctx->ip_address, appctx->port_num, DEFAULT_PROP_MPOINT);

  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
  if (appctx->output_file)
    g_print ("\n Video file will be stored at %s\n", appctx->output_file);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx, &options, config_file);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
