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

G_BEGIN_DECLS

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
 * @GST_MODEL_TYPE_QNN   : QNN Container.
 *
 * Type of Model container for the Runtime.
 */
typedef enum {
  GST_MODEL_TYPE_NONE,
  GST_MODEL_TYPE_SNPE,
  GST_MODEL_TYPE_TFLITE,
  GST_MODEL_TYPE_QNN
} GstModelType;

/**
 * GstModelFormatType:
 * @GST_MODEL_FORMAT_NONE   : Invalid Model Format.
 * @GST_MODEL_FORMAT_UINT8  : INT8 Model.
 * @GST_MODEL_FORMAT_INT8   : UINT8 Model.
 *
 * Type of Model format.
 */
typedef enum {
  GST_MODEL_FORMAT_NONE,
  GST_MODEL_FORMAT_UINT8,
  GST_MODEL_FORMAT_INT8
} GstModelFormatType;

/**
 * GstYoloModelType:
 * @GST_YOLO_TYPE_NONE: Invalid Model Type.
 * @GST_YOLO_TYPE_V5  : Yolov5 Object Detection Model.
 * @GST_YOLO_TYPE_V8  : Yolov8 Object Detection Model.
 * @GST_YOLO_TYPE_NAS : Yolonas Object Detection Model.
 * @GST_YOLO_TYPE_V7  : YoloV7 Object Detection Model.
 * @GST_YOLO_TYPE_X   : Yolox Object Detection Model.
 *
 * Type of Yolo Model.
 */
typedef enum {
  GST_YOLO_TYPE_NONE,
  GST_YOLO_TYPE_V5,
  GST_YOLO_TYPE_V8,
  GST_YOLO_TYPE_NAS,
  GST_YOLO_TYPE_V7,
  GST_YOLO_TYPE_X
} GstYoloModelType;

/**
 * GstStreamSourceType:
 * @GST_STREAM_TYPE_NONE    : Invalid Stream Type.
 * @GST_STREAM_TYPE_CAMERA  : Camera Stream.
 * @GST_STREAM_TYPE_FILE    : Video File Stream.
 * @GST_STREAM_TYPE_RTSP    : RTSP Stream.
 *
 * Type of Stream.
 */
typedef enum {
  GST_STREAM_TYPE_NONE,
  GST_STREAM_TYPE_CAMERA,
  GST_STREAM_TYPE_FILE,
  GST_STREAM_TYPE_RTSP
} GstStreamSourceType;

/**
 * GstStreamSourceType:
 * @GST_CAMERA_TYPE_NONE       : Invalid Stream Type.
 * @GST_CAMERA_TYPE_PRIMARY    : Camera Stream.
 * @GST_CAMERA_TYPE_SECONDARY  : Video File Stream.
 *
 * Type of CameraSource.
 */
typedef enum {
  GST_CAMERA_TYPE_NONE = -1,
  GST_CAMERA_TYPE_PRIMARY,
  GST_CAMERA_TYPE_SECONDARY
} GstCameraSourceType;

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
 * GstQmmfSrcStreamType:
 * @GST_SOURCE_STREAM_TYPE_VIDEO   : Stream fitted for encoding the buffer.
 * @GST_SOURCE_STREAM_TYPE_PREVIEW : Stream fitted for visualizing the buffers.
 *
 * Type of qmmfsrc stream.
 */
typedef enum {
  GST_SOURCE_STREAM_TYPE_VIDEO,
  GST_SOURCE_STREAM_TYPE_PREVIEW
} GstQmmfSrcStreamType;

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
 * @GST_ADECODE_FLAC: Audio flac Codec Type.
 * Type of Audio Decode Codec.
 */
enum GstAudioDecodeCodecType {
  GST_ADECODE_NONE,
  GST_ADECODE_MP3,
  GST_ADECODE_WAV,
  GST_ADECODE_FLAC,
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
 * GstV4l2IOMode:
 * @GST_V4L2_IO_AUTO: Default IO Mode.
 * @GST_V4L2_IO_RW: RW IO Mode.
 * @GST_V4L2_IO_MMAP: MMAP IO Mode.
 * @GST_V4L2_IO_USERPTR: USERPTR IO Mode.
 * @GST_V4L2_IO_DMABUF: DMABUF IO Mode.
 * @GST_V4L2_IO_DMABUF_IMPORT: DMABUF_IMPORT IO Mode.
 *
 * Type of Video Codec for AV Player.
 */
typedef enum {
  GST_V4L2_IO_AUTO          = 0,
  GST_V4L2_IO_RW            = 1,
  GST_V4L2_IO_MMAP          = 2,
  GST_V4L2_IO_USERPTR       = 3,
  GST_V4L2_IO_DMABUF        = 4,
  GST_V4L2_IO_DMABUF_IMPORT = 5
} GstV4l2IOMode;

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
 * GstMLVideoDisposition:
 * @GST_ML_VIDEO_DISPOSITION_TOP_LEFT: Top Left disposition.
 * @GST_ML_VIDEO_DISPOSITION_CENTRE: Centre disposition.
 * @GST_ML_VIDEO_DISPOSITION_STRETCH: Stretch disposition
 * Options to select Video disposition type.
 */
typedef enum
{
  GST_ML_VIDEO_DISPOSITION_TOP_LEFT,
  GST_ML_VIDEO_DISPOSITION_CENTRE,
  GST_ML_VIDEO_DISPOSITION_STRETCH
} GstVideoDisposition;

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
 * GstInputStreamType:
 * @GST_INPUT_STREAM_H264: H264 input stream encoding
 * @GST_INPUT_STREAM_H265: H265 input stream encoding
 */
typedef enum {
  GST_INPUT_STREAM_H264,
  GST_INPUT_STREAM_H265
} GstInputStreamType;

/*
 * Check if File Exists
 *
 * @param path file path to check for existence
 * @result TRUE if file exists and can be accessed, FALSE otherwise
 */
gboolean
file_exists (const gchar * path);

/*
 * Check if File Location is Valid
 *
 * @param path file path to check for valid path
 * @result TRUE if path exists and can be accessed, FALSE otherwise
 */
gboolean
file_location_exists (const gchar * path);

/*
 * Get Active Display Mode
 *
 * @param width fill display current width
 * @param height fill display current height
 * @result TRUE if api is able to provide information, FALSE otherwise
 */
gboolean
get_active_display_mode (gint * width, gint * height);

/**
 * Handles interrupt by CTRL+C.
 *
 * @param userdata pointer to AppContext.
 * @return FALSE if the source should be removed, else TRUE.
 */
gboolean
handle_interrupt_signal (gpointer userdata);

/**
 * Handles error events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata);

/**
 * Handles warning events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata);

/**
 * Handles End-Of-Stream(eos) Event.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Main Loop for Handling eos.
 */
void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata);

/**
 * Handles state change events for the pipeline
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Application Pipeline.
 */
void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata);

/**
 * Sets an enum property on a GstElement
 *
 * @param element The GstElement on which to set the property.
 * @param propname The name of the property to set.
 * @param valname The value to set the property to.
 *
 */
void
gst_element_set_enum_property (GstElement * element, const gchar * propname,
    const gchar * valname);

/**
 * Get enum for property nick name
 *
 * @param element Plugin to query the property.
 * @param prop_name Property Name.
 * @param prop_value_nick Property Nick Name.
 */
gint
get_enum_value (GstElement * element, const gchar * prop_name,
    const gchar * prop_value_nick);

/**
 * Unref Gstreamer plugin elements
 *
 * @param element Plugins.
 *
 * Unref all elements if any fails to create.
 */
void
unref_elements(void *first_elem, ...);

// Recieves a list of pointers to variable containing pointer to gst element
// and unrefs the gst element if needed
void
cleanup_gst (void * first_elem, ...);

gboolean
is_camera_available ();

G_END_DECLS

#endif //GST_SAMPLE_APPS_UTILS_H
