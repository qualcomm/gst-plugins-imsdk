/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_ML_ONNX_ENGINE_H__
#define __GST_ML_ONNX_ENGINE_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>
#include <gst/ml/gstmlmeta.h>

G_BEGIN_DECLS

/**
 * GST_ML_ONNX_ENGINE_OPT_MODEL:
 *
 * #G_TYPE_STRING, neural network model file path and name
 * Default: NULL
 */
#define GST_ML_ONNX_ENGINE_OPT_MODEL \
    "GstMLOnnxEngine.model"

/**
 * GstMLOnnxExecutionProvider:
 * @GST_ML_ONNX_EXECUTION_PROVIDER_CPU     : CPU execution provider
 * @GST_ML_ONNX_EXECUTION_PROVIDER_QNN     : Qualcomm QNN execution provider
 *
 * Different execution providers for ONNX Runtime.
 */
typedef enum {
  GST_ML_ONNX_EXECUTION_PROVIDER_CPU,
  GST_ML_ONNX_EXECUTION_PROVIDER_QNN,
} GstMLOnnxExecutionProvider;

GST_API GType gst_ml_onnx_execution_provider_get_type (void);
#define GST_TYPE_ML_ONNX_EXECUTION_PROVIDER \
    (gst_ml_onnx_execution_provider_get_type())

/**
 * GST_ML_ONNX_ENGINE_OPT_EXECUTION_PROVIDER:
 *
 * #GST_TYPE_ML_ONNX_EXECUTION_PROVIDER, set the execution provider
 * Default: #GST_ML_ONNX_EXECUTION_PROVIDER_CPU.
 */
#define GST_ML_ONNX_ENGINE_OPT_EXECUTION_PROVIDER \
    "GstMLOnnxEngine.execution-provider"

/**
 * GST_ML_ONNX_ENGINE_OPT_QNN_BACKEND_PATH:
 *
 * #GST_TYPE_ML_ONNX_QNN_BACKEND_PATH, file path to QNN backend library.
 * Default: NULL
 */
#define GST_ML_ONNX_ENGINE_OPT_QNN_BACKEND_PATH \
    "GstMLOnnxEngine.qnn-backend-path"

/**
 * GstMLOnnxOptimizationLevel:
 * @GST_ML_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL     : Disable all optimizations
 * @GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC    : Enable basic optimizations
 * @GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED : Enable extended optimizations
 * @GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL      : Enable all optimizations
 *
 * Graph optimization levels for ONNX Runtime.
 */
typedef enum {
  GST_ML_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL,
  GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
  GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
  GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
} GstMLOnnxOptimizationLevel;

GST_API GType gst_ml_onnx_optimization_level_get_type (void);
#define GST_TYPE_ML_ONNX_OPTIMIZATION_LEVEL \
    (gst_ml_onnx_optimization_level_get_type())

/**
 * GST_ML_ONNX_ENGINE_OPT_OPTIMIZATION_LEVEL:
 *
 * #GST_TYPE_ML_ONNX_OPTIMIZATION_LEVEL, set the optimization level
 * Default: #GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED.
 */
#define GST_ML_ONNX_ENGINE_OPT_OPTIMIZATION_LEVEL \
    "GstMLOnnxEngine.optimization-level"

/**
 * GST_ML_ONNX_ENGINE_OPT_THREADS:
 *
 * #G_TYPE_UINT, number of threads available to the interpreter
 * Default: 1
 */
#define GST_ML_ONNX_ENGINE_OPT_THREADS \
    "GstMLOnnxEngine.threads"

typedef struct _GstMLOnnxEngine GstMLOnnxEngine;

GST_API GstMLOnnxEngine *
gst_ml_onnx_engine_new              (GstStructure * settings);

GST_API void
gst_ml_onnx_engine_free             (GstMLOnnxEngine * engine);

GST_API GstCaps *
gst_ml_onnx_engine_get_input_caps   (GstMLOnnxEngine * engine);

GST_API GstCaps *
gst_ml_onnx_engine_get_output_caps  (GstMLOnnxEngine * engine);

GST_API gboolean
gst_ml_onnx_engine_execute          (GstMLOnnxEngine * engine,
                                     GstMLFrame * inframe,
                                     GstMLFrame * outframe);

G_END_DECLS

#endif /* __GST_ML_ONNX_ENGINE_H__ */
