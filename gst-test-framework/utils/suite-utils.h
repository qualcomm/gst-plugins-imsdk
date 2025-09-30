/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SUITE_UTILS_H__
#define __GST_SUITE_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_VIDEO_CODEC_H264           "H.264"
#define GST_VIDEO_CODEC_H265           "H.265"
#define TF_TO_STRING(x)                #x
#define TF_MODELS_PREFIX               "/etc/models/"
#define TF_LABELS_PREFIX               "/etc/labels/"
#define TF_MEDIA_PREFIX                "/etc/media/"
#define TF_FILE_LOCATION(name)         TF_MEDIA_PREFIX TF_TO_STRING(name)
#define TF_MODEL_LOCATION(name)        TF_MODELS_PREFIX TF_TO_STRING(name)
#define TF_LABEL_LOCATION(name)        TF_LABELS_PREFIX TF_TO_STRING(name)

#define TF_ML_QNN_CPU_BACKEND         "/usr/lib/libQnnCpu.so"
#define TF_ML_QNN_GPU_BACKEND         "/usr/lib/libQnnGpu.so"
#define TF_ML_QNN_HTP_BACKEND         "/usr/lib/libQnnHtp.so"

typedef struct _GstCapsParameters GstCapsParameters;
typedef struct _GstMLFrameInfo GstMLFrameInfo;
typedef struct _GstMLVideoInfo GstMLVideoInfo;
typedef struct _GstMLModelInfo GstMLModelInfo;

/**
 * GstInferenceType:
 * @param GST_ML_OBJECT_DETECTION: Object detection.
 * @param GST_ML_CLASSIFICATION: Classification.
 * @param GST_ML_POSE_DETECTION: Pose detection.
 * @param GST_ML_SEGMENTATION: Segmentation.
 *
 * Type of Inference.
 */
typedef enum {
  GST_ML_OBJECT_DETECTION,
  GST_ML_CLASSIFICATION,
  GST_ML_POSE_DETECTION,
  GST_ML_SEGMENTATION,
} GstMLInferenceType;

/**
 * GstModelType:
 * @GST_ML_MODEL_NONE: Invalid Model Type.
 * @GST_ML_MODEL_TFLITE: TFLITE Container.
 * @GST_ML_MODEL_QNN: QNN Container.
 * @GST_ML_MODEL_SNPE: SNPE DLC Container.
 *
 * Type of Model container for the Runtime.
 */
typedef enum {
  GST_ML_MODEL_NONE,
  GST_ML_MODEL_TFLITE,
  GST_ML_MODEL_QNN,
  GST_ML_MODEL_SNPE
} GstMLModelType;

typedef enum  {
  GST_ML_DELEGATE_CPU,
  GST_ML_DELEGATE_GPU,
  GST_ML_DELEGATE_DSP
} GstMLDelegate;

/**
 * GstMLModuleType:
 * @GST_ML_MODULE_NONE: Invalid module type.
 * @GST_ML_MODULE_YOLO_V5: Yolov5 object detection Model.
 * @GST_ML_MODULE_YOLO_V8: Yolov8 object detection module.
 * @GST_ML_MODULE_MOBILENET: Mobilenet module.
 * @GST_ML_MODULE_SSD_MOBILENET: SSD-MOBILENET detection module.
 * @GST_ML_MODULE_MOBILENET_SOFTMAX: Mobilenet with softmax classification module.
 *
 * Type of inference module.
 */
typedef enum {
  GST_ML_MODULE_NONE,
  GST_ML_MODULE_YOLO_V5,
  GST_ML_MODULE_YOLO_V8,
  GST_ML_MODULE_MOBILENET,
  GST_ML_MODULE_SSD_MOBILENET,
  GST_ML_MODULE_MOBILENET_SOFTMAX,
} GstMLModuleType;

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

struct _GstCapsParameters {
  const gchar *format;
  gint        width;
  gint        height;
  gint        fps;
};

struct _GstMLFrameInfo {
  // Frame index, starts from 0.
  gint        index;
  // The meta number that the buffer contains.
  gint        metanum;
};

struct _GstMLVideoInfo {
  const gchar    *file;
  GstMLFrameInfo frameinfo[10];
};

struct _GstMLModelInfo {
  GstMLInferenceType inferencetype;
  GstMLModelType     type;
  gchar              *modelpath;
  gchar              *labelspath;
  gchar              *settings;
  gint               moduletype;
  gint               results;
  GstMLDelegate      delegate;
};

/**
 * gst_mp4_verification:
 * @location: File location.
 * @width: Mp4 expected width if none zero.
 * @height: Mp4 expected height if none zero.
 * @framerate: Mp4 expected height if none zero.
 * @diff: The tolerable deviation between expected and actual FPS.
 * @induration: Mp4 expected playing time if none zero.
 * @codec: if it is MP4 file, return video-codec type by tag.
 *
 * Function for verify Mp4 info with expected parameters.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_mp4_verification (gchar *location, gint width, gint height,
    gdouble framerate, gdouble diff, guint induration, gchar **codec);

/**
 * gst_element_on_pad_added:
 * @element: GStreamer source element.
 * @pad: GStreamer source element pad.
 * @data: GStreamer sink element.
 *
 * Function for verify Mp4 info with expected parameters.
 *
 * return: None
 */
GST_API void
gst_element_on_pad_added (GstElement *element, GstPad *pad,
    gpointer data);

/**
 * gst_element_send_eos:
 * @element: GStreamer element.
 *
 * Function for send eos event and check return message.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_element_send_eos (GstElement *element);

/**
 * gst_destroy_pipeline:
 * @pipeline: GStreamer bin.
 * @plugins: GStreamer plugins that bin contains.
 *
 * Function for unlink elements and remove them from bin.
 *
 * return: None
 */
GST_API void
gst_destroy_pipeline (GstElement **pipeline, GList **plugins);

G_END_DECLS

#endif /* __GST_SUITE_UTILS_H__ */
