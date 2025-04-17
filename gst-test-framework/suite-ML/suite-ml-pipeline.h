/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SUITE_CAMERA_PIPELINE_H__
#define __GST_SUITE_CAMERA_PIPELINE_H__

#include "suite-utils.h"

G_BEGIN_DECLS

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
 * ml_video_object_detection_inference_pipeline:
 * @model: The model to run.
 * @vinfo: The video to verify, also contains meta infomation.
 *
 * Function for creating detection inference pipeline.
 *
 * return: None
 */
void
ml_video_object_detection_inference_pipeline (GstMLModel model,
    GstMLVideoInfo vinfo);

G_END_DECLS

#endif /* __GST_SUITE_CAMERA_PIPELINES_H__ */
