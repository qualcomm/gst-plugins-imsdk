/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * SECTION:element-qtimlqairt
 * @title: qtimlqairt
 *
 * qtimlqairt provides an interface to run ML models using the unified Qualcomm
 * AI Runtime (QAIRT) SDK. It supersedes the runtime-specific qtimlsnpe and
 * qtimlqnn elements by exposing a single, runtime-independent API. It loads a
 * '.dlc' model container or a cached context '.bin' binary and executes it on
 * the selected hardware backend (CPU, GPU or HTP).
 */

#ifndef __GST_QTI_ML_QAIRT_H__
#define __GST_QTI_ML_QAIRT_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/ml/ml-info.h>

#include "ml-qairt-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_QAIRT \
  (gst_ml_qairt_get_type())
#define GST_ML_QAIRT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_QAIRT,GstMLQairt))
#define GST_ML_QAIRT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_QAIRT,GstMLQairtClass))
#define GST_IS_ML_QAIRT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_QAIRT))
#define GST_IS_ML_QAIRT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_QAIRT))
#define GST_ML_QAIRT_CAST(obj)       ((GstMLQairt *)(obj))

typedef struct _GstMLQairt GstMLQairt;
typedef struct _GstMLQairtClass GstMLQairtClass;

struct _GstMLQairt {
  GstBaseTransform  parent;

  /// Buffer pools.
  GstBufferPool     *outpool;

  /// Machine learning engine.
  GstMLQairtEngine  *engine;

  GstMLInfo         *ininfo;
  GstMLInfo         *outinfo;

  /// Properties.
  GstMLQairtSettings settings;
};

struct _GstMLQairtClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_qairt_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_QAIRT_H__
