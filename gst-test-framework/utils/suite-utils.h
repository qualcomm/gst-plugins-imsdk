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

typedef struct _GstCapsParameters GstCapsParameters;
typedef struct _GstMLFrameInfo GstMLFrameInfo;
typedef struct _GstMLVideoInfo GstMLVideoInfo;
typedef struct _GstMLModel GstMLModel;

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
 * GstDetectionModuleType:
 * @GST_YOLO_TYPE_V8  : Yolov8 Object Detection Model.
 *
 * Type of Yolo Model.
 */
typedef enum {
  GST_YOLO_TYPE_V8,
} GstDetectionModuleType;

struct _GstCapsParameters {
  const gchar *format;
  gint        width;
  gint        height;
  gint        fps;
};

struct _GstMLFrameInfo {
  gint        index;
  gint        metanum;
};

struct _GstMLVideoInfo {
  const gchar    *file;
  GstMLFrameInfo frameinfo[10];
};

typedef enum  {
  GST_ML_DELEGATE_CPU,
  GST_ML_DELEGATE_GPU,
  GST_ML_DELEGATE_DSP
} GstMLDelegate;

struct _GstMLModel {
  GstModelType   type;
  gchar          *modelpath;
  gchar          *labelspath;
  gboolean       use_constants;
  gchar          *constants;
  gint           moduletype;
  gfloat         threshold;
  GstMLDelegate  delegate;
};

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
gst_element_on_pad_added (GstElement *element, GstPad *pad, gpointer data);

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

G_END_DECLS

#endif /* __GST_SUITE_UTILS_H__ */
