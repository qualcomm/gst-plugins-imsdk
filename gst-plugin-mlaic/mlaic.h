/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_AIC_H__
#define __GST_QTI_ML_AIC_H__

#include <gst/gst.h>
#include <gst/ml/ml-info.h>

#include "mlaicpads.h"
#include "ml-aic-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_AIC (gst_ml_aic_get_type())
#define GST_ML_AIC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_AIC,GstMLAic))
#define GST_ML_AIC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_AIC,GstMLAicClass))
#define GST_IS_ML_AIC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_AIC))
#define GST_IS_ML_AIC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_AIC))
#define GST_ML_AIC_CAST(obj)       ((GstMLAic *)(obj))

typedef struct _GstMLAic GstMLAic;
typedef struct _GstMLAicClass GstMLAicClass;

struct _GstMLAic {
  GstElement     parent;

  /// Machine learning engine.
  GstMLAicEngine *engine;

  /// Properties.
  gchar          *model;
  GArray         *devices;
  guint          n_activations;
};

struct _GstMLAicClass {
  GstElementClass parent;
};

G_GNUC_INTERNAL GType gst_ml_aic_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_AIC_H__
