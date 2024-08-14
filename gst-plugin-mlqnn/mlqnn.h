/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * SECTION:element-qtimlqnn
 * @title: qtimlqnn
 *
 * qtimlqnn provides an interface to run ML models using QNN SDK. It provides
 * properties to take model path as well as the backend path.
 */

#ifndef __GST_QTI_ML_QNN_H__
#define __GST_QTI_ML_QNN_H__

#include <gst/base/gstbasetransform.h>
#include <gst/ml/ml-info.h>

#include "ml-qnn-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_QNN \
  (gst_ml_qnn_get_type())
#define GST_ML_QNN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_QNN,GstMLQnn))
#define GST_ML_QNN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_QNN,GstMLQnnClass))
#define GST_IS_ML_QNN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_QNN))
#define GST_IS_ML_QNN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_QNN))
#define GST_ML_QNN_CAST(obj)       ((GstMLQnn *)(obj))

typedef struct _GstMLQnn GstMLQnn;
typedef struct _GstMLQnnClass GstMLQnnClass;

struct _GstMLQnn {
  GstBaseTransform  parent;

  /// Buffer pools.
  GstBufferPool     *outpool;

  GstMLQnnEngine    *engine;

  /// Properties.
  gchar             *model;
  gchar             *backend;
  gchar             *syslib;
  guint             backend_device_id;
};

struct _GstMLQnnClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_qnn_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_QNN_H__
