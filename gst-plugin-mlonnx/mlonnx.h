/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_ONNX_H__
#define __GST_QTI_ML_ONNX_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/ml/ml-info.h>

#include "ml-onnx-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_ONNX \
  (gst_ml_onnx_get_type())
#define GST_ML_ONNX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_ONNX,GstMLOnnx))
#define GST_ML_ONNX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_ONNX,GstMLOnnxClass))
#define GST_IS_ML_ONNX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_ONNX))
#define GST_IS_ML_ONNX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_ONNX))
#define GST_ML_ONNX_CAST(obj)       ((GstMLOnnx *)(obj))

typedef struct _GstMLOnnx GstMLOnnx;
typedef struct _GstMLOnnxClass GstMLOnnxClass;

struct _GstMLOnnx {
  GstBaseTransform            parent;

  /// Buffer pools.
  GstBufferPool               *outpool;

  /// Machine learning engine.
  GstMLOnnxEngine             *engine;

  GstMLInfo                   *ininfo;
  GstMLInfo                   *outinfo;

  /// Properties.
  gchar                       *model;
  GstMLOnnxExecutionProvider  execution_provider;
  gchar                       *backend_path;
  GstMLOnnxOptimizationLevel  optimization_level;
  guint                       n_threads;
};

struct _GstMLOnnxClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_onnx_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_ONNX_H__
