/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
  GstElement       parent;

  /// Buffer pools.
  GstBufferPool    *outpool;

  /// Machine learning engine.
  GstMLAicEngine   *engine;

  /// Properties.
  gchar             *model;
  GArray            *devices;
  guint             n_activations;
};

struct _GstMLAicClass {
  GstElementClass parent;
};

G_GNUC_INTERNAL GType gst_ml_aic_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_AIC_H__
