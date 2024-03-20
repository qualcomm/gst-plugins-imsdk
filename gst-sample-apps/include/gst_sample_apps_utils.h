/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * This file provides utility functions for gst applications.
 */

#ifndef GST_SAMPLE_APPS_UTILS_H
#define GST_SAMPLE_APPS_UTILS_H

#include <stdio.h>
#include <stdarg.h>

#include <glib-unix.h>
#include <gst/gst.h>

/**
 * Preprocessor to Convert variable value to string
 */
#define _TO_STR(x) #x
#define TO_STR(x) _TO_STR(x)

/**
 * GstAppContext:
 * @pipeline: Pipeline connecting all the elements for Use Case.
 * @plugins : List of all the plugins used in Pipeline.
 * @mloop   : Main Loop for the Application.
 *
 * Application context to pass information between the functions.
 */
typedef struct
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of pipeline plugins
  GList      *plugins;
  // Pointer to the mainloop
  GMainLoop  *mloop;
} GstAppContext;

/**
 * GstModelType:
 * @GST_MODEL_TYPE_NONE  : Invalid Model Type.
 * @GST_MODEL_TYPE_SNPE  : SNPE DLC Container.
 * @GST_MODEL_TYPE_TFLITE: TFLITE Container.
 *
 * Type of Model container for the Runtime.
 */
typedef enum {
  GST_MODEL_TYPE_NONE,
  GST_MODEL_TYPE_SNPE,
  GST_MODEL_TYPE_TFLITE
} GstModelType;

/**
 * GstYoloModelType:
 * @GST_YOLO_TYPE_NONE: Invalid Model Type.
 * @GST_YOLO_TYPE_V5  : Yolov5 Object Detection Model.
 * @GST_YOLO_TYPE_V8  : Yolov8 Object Detection Model.
 * @GST_YOLO_TYPE_NAS: Yolonas Object Detection Model.
 *
 * Type of Yolo Model.
 */
typedef enum {
  GST_YOLO_TYPE_NONE,
  GST_YOLO_TYPE_V5,
  GST_YOLO_TYPE_V8,
  GST_YOLO_TYPE_NAS
} GstYoloModelType;

/**
 * GstInferenceType:
 * @param GST_OBJECT_DETECTION: Object detection Pipeline.
 * @param GST_CLASSIFICATION: Classification Pipeline.
 * @param GST_POSE_DETECTION: Pose detection Pipeline.
 * @param GST_SEGMENTATION: Segmentation Pipeline.
 * @param GST_PIPELINE_CNT
 *
 * Type of Inference.
 */
typedef enum {
  GST_OBJECT_DETECTION,
  GST_CLASSIFICATION,
  GST_POSE_DETECTION,
  GST_SEGMENTATION,
  GST_PIPELINE_CNT
} GstInferenceType;

/**
 * GstMLSnpeDelegate:
 * @GST_ML_SNPE_DELEGATE_NONE: CPU is used for all operations
 * @GST_ML_SNPE_DELEGATE_DSP : Hexagon Digital Signal Processor
 * @GST_ML_SNPE_DELEGATE_GPU : Graphics Processing Unit
 * @GST_ML_SNPE_DELEGATE_AIP : Snapdragon AIX + HVX
 *
 * Different delegates for transferring part or all of the model execution.
 */
typedef enum {
  GST_ML_SNPE_DELEGATE_NONE,
  GST_ML_SNPE_DELEGATE_DSP,
  GST_ML_SNPE_DELEGATE_GPU,
  GST_ML_SNPE_DELEGATE_AIP,
} GstMLSnpeDelegate;

/**
 * GstMLTFLiteDelegate:
 * @GST_ML_TFLITE_DELEGATE_NONE     : CPU is used for all operations
 * @GST_ML_TFLITE_DELEGATE_NNAPI_DSP: DSP through Android NN API
 * @GST_ML_TFLITE_DELEGATE_NNAPI_GPU: GPU through Android NN API
 * @GST_ML_TFLITE_DELEGATE_NNAPI_NPU: NPU through Android NN API
 * @GST_ML_TFLITE_DELEGATE_HEXAGON  : Hexagon DSP is used for all operations
 * @GST_ML_TFLITE_DELEGATE_GPU      : GPU is used for all operations
 * @GST_ML_TFLITE_DELEGATE_XNNPACK  : Prefer to delegate nodes to XNNPACK
 * @GST_ML_TFLITE_DELEGATE_EXTERNAL : Use external delegate
 *
 * Different delegates for transferring part or all of the model execution.
 */
typedef enum {
  GST_ML_TFLITE_DELEGATE_NONE,
  GST_ML_TFLITE_DELEGATE_NNAPI_DSP,
  GST_ML_TFLITE_DELEGATE_NNAPI_GPU,
  GST_ML_TFLITE_DELEGATE_NNAPI_NPU,
  GST_ML_TFLITE_DELEGATE_HEXAGON,
  GST_ML_TFLITE_DELEGATE_GPU,
  GST_ML_TFLITE_DELEGATE_XNNPACK,
  GST_ML_TFLITE_DELEGATE_EXTERNAL,
} GstMLTFLiteDelegate;

/**
 * GstAudioDecodeCodecType:
 * @GST_ADECODE_NONE: Default Audio Decode Codec Type.
 * @GST_ADECODE_MP3: Audio mp3 Codec Type.
 * @GST_ADECODE_WAV: Audio wav Codec Type.
 *
 * Type of Audio Decode Codec.
 */
enum GstAudioDecodeCodecType {
  GST_ADECODE_NONE,
  GST_ADECODE_MP3,
  GST_ADECODE_WAV,
};

/**
 * GstAudioEncodeCodecType:
 * @GST_AENCODE_NONE: Default Audio Encode Codec Type.
 * @GST_AENCODE_FLAC: Audio flac Codec Type.
 * @GST_AENCODE_WAV: Audio wav Codec Type.
 *
 * Type of Audio Encode Codec.
 */
enum GstAudioEncodeCodecType {
  GST_AENCODE_NONE,
  GST_AENCODE_FLAC,
  GST_AENCODE_WAV,
};

/**
 * GstVideoPlayerCodecType:
 * @GST_VCODEC_NONE: Default Video codec Type.
 * @GST_VCODEC_AVC: Video AVC Codec Type.
 * @GST_VCODEC_HEVC: Video HEVC Codec Type.
 *
 * Type of Video Codec for AV Player.
 */
enum GstVideoPlayerCodecType {
  GST_VCODEC_NONE,
  GST_VCODEC_AVC,
  GST_VCODEC_HEVC,
};

/**
 * GstAudioPlayerCodecType:
 * @GST_ACODEC_NONE: Default Audio Codec Type.
 * @GST_ACODEC_FLAC: Audio flac Codec Type.
 * @GST_ACODEC_MP3: Audio wav Codec Type.
 *
 * Type of Audio Codec for AV Player.
 */
enum GstAudioPlayerCodecType {
  GST_ACODEC_NONE,
  GST_ACODEC_FLAC,
  GST_ACODEC_MP3,
};

/**
 * GstSinkType:
 * @GST_WAYLANDSINK: Waylandsink Type.
 * @GST_VIDEO_ENCODE : Video Encode Type.
 * @GST_YUV_DUMP: YUV Filesink Type.
 * @GST_RTSP_STREAMING : RTSP streaming Type.
 * Type of App Sink.
 */
enum GstSinkType {
  GST_WAYLANDSINK,
  GST_VIDEO_ENCODE,
  GST_YUV_DUMP,
  GST_RTSP_STREAMING,
};

/**
 * GstMainMenuOption:
 * @GST_PLAY_OPTION: Option to play video.
 * @GST_PAUSE_OPTION  : Option to pause video.
 * @GST_FAST_FORWARD_OPTION  : Option to fast forward video.
 * @GST_REWIND_OPTION  : Option to rewind video.
 *
 * Options to select from Main Menu.
 */
typedef enum {
  GST_PLAY_OPTION = 1,
  GST_PAUSE_OPTION,
  GST_FAST_FORWARD_OPTION,
  GST_REWIND_OPTION
} GstMainMenuOption;

/**
 * GstFFRMenuOption:
 * @GST_TIME_BASED: Option to seek based on time.
 * @GST_SPEED_BASED  : Option to seek based on speed.
 *
 * Options to select from FFR Menu.
 */
typedef enum {
  GST_TIME_BASED = 1,
  GST_SPEED_BASED
} GstFFRMenuOption;

/**
 * GstAppCompositionType:
 * @GST_PIP_COMPOSE: Option to set composition position and dimension.
 * @GST_SIDE_BY_SIDE_COMPOSE  : Option to set composition side by side.
 *
 * Options to select App composition.
 */
enum GstAppCompositionType {
  GST_PIP_COMPOSE,
  GST_SIDE_BY_SIDE_COMPOSE,
};

/**
 * GstAppComposerOutput:
 * @GST_APP_OUTPUT_WAYLANDSINK: Option to set waylandsink type.
 * @GST_APP_OUTPUT_QTIVCOMPOSER  : Option to set composer type.
 *
 * Options to select composer type.
 */
// Enum to define the type of composer types that user can select
enum GstAppComposerOutput {
  GST_APP_OUTPUT_WAYLANDSINK,
  GST_APP_OUTPUT_QTIVCOMPOSER,
};

/*
 * Check if File Exists
 *
 * @param path file path to check for existence
 * @result TRUE if file exists and can be accessed, FALSE otherwise
 */
static gboolean
file_exists (const gchar * path)
{
  FILE *fp = fopen (path, "r+");
  if (fp) {
    fclose (fp);
    return TRUE;
  }
  return FALSE;
}

/*
 * Check if File Location is Valid
 *
 * @param path file path to check for valid path
 * @result TRUE if path exists and can be accessed, FALSE otherwise
 */
static gboolean
file_location_exists (const gchar * path)
{
  FILE *fp = fopen (path, "ab");
  if (fp) {
    fclose (fp);
    return TRUE;
  }
  return FALSE;
}

/*
 * Get Active Display Mode
 *
 * @param width fill display current width
 * @param height fill display current height
 * @result TRUE if api is able to provide information, FALSE otherwise
 */
static gboolean
get_active_display_mode (gint * width, gint * height)
{
  gchar line[128];
  FILE *fp = fopen ("/sys/class/drm/card0-DSI-1/modes", "rb");
  if (!fp) {
    return FALSE;
  }

  fgets (line, 128, fp);
  fclose (fp);
  if (strlen (line) > 0) {
    if (2 == sscanf (line, "%dx%d", width, height)) {
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * GstFlipVideoType:
 * @GST_FLIP_TYPE_NONE: No video image flip.
 * @GST_FLIP_TYPE_HORIZONTAL: Video image flip horizontal.
 * @GST_FLIP_TYPE_VERTICAL: Video image flip vertical
 * @GST_FLIP_TYPE_BOTH: Video image flip vertical and horizontal
 * Options to select Flip type.
 */
typedef enum {
  GST_FLIP_TYPE_NONE,
  GST_FLIP_TYPE_HORIZONTAL,
  GST_FLIP_TYPE_VERTICAL,
  GST_FLIP_TYPE_BOTH
} GstFlipVideoType;

/**
 * GstRotateVideoType:
 * @GST_ROTATE_TYPE_NONE: No video rotation.
 * @GST_ROTATE_TYPE_90CW: 90 degree video rotation.
 * @GST_ROTATE_TYPE_90CCW: 270 degree video rotation.
 * @GST_ROTATE_TYPE_180: 180 degree video rotation.
 * Options to select Rotate type.
 */
typedef enum {
  GST_ROTATE_TYPE_NONE,
  GST_ROTATE_TYPE_90CW,
  GST_ROTATE_TYPE_90CCW,
  GST_ROTATE_TYPE_180
} GstRotateVideoType;

/**
 * Handles interrupt by CTRL+C.
 *
 * @param userdata pointer to AppContext.
 * @return FALSE if the source should be removed, else TRUE.
 */
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (appctx->mloop);
  }
  return TRUE;
}

/**
 * Handles error events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

/**
 * Handles warning events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

/**
 * Handles End-Of-Stream(eos) Event.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Main Loop for Handling eos.
 */
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

/**
 * Handles state change events for the pipeline
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Application Pipeline.
 */
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  g_print("state change: %d -> %d\n", old, new_st);

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {

    if (gst_element_set_state (pipeline,
        GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr (
          "\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

/**
 * Get enum for property nick name
 *
 * @param element Plugin to query the property.
 * @param prop_name Property Name.
 * @param prop_value_nick Property Nick Name.
 */
static gint
get_enum_value (GstElement * element, const gchar * prop_name,
    const gchar * prop_value_nick)
{
  GParamSpec **property_specs;
  GObject *obj = G_OBJECT (element);
  GObjectClass *obj_class = G_OBJECT_GET_CLASS (element);
  guint num_properties, i;
  gint ret = -1;

  property_specs = g_object_class_list_properties (obj_class, &num_properties);

  for (i = 0; i < num_properties; i++) {
    GParamSpec *param = property_specs[i];
    GEnumValue *values;
    GType owner_type = param->owner_type;
    guint j = 0;

    // We need only pad properties
    if (obj == NULL && (owner_type == G_TYPE_OBJECT
        || owner_type == GST_TYPE_OBJECT || owner_type == GST_TYPE_PAD))
      continue;

    if (strcmp (prop_name, param->name)) {
      continue;
    }

    if (!G_IS_PARAM_SPEC_ENUM (param)) {
      continue;
    }

    values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
    while (values[j].value_name) {
      if (!strcmp (prop_value_nick, values[j].value_nick)) {
        ret = values[j].value;
        break;
      }
      j++;
    }
  }

  g_free (property_specs);
  return ret;
}

/**
 * Unref Gstreamer plugin elements
 *
 * @param element Plugins.
 *
 * Unref all elements if any fails to create.
 */
static void
unref_elements(void *first_elem, ...) {
  va_list args;

  va_start(args, first_elem);

  while (1) {
    if (first_elem) {
      if (!strcmp((char *) first_elem, "NULL"))
        break;
      gst_object_unref (first_elem);
    }

    first_elem = va_arg(args, void *);
  }

  va_end(args);
}

#endif //GST_SAMPLE_APPS_UTILS_H
