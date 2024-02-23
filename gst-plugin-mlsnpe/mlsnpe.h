/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /* Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_SNPE_H__
#define __GST_QTI_ML_SNPE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/ml/ml-info.h>

#include "ml-snpe-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_SNPE \
  (gst_ml_snpe_get_type())
#define GST_ML_SNPE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_SNPE,GstMLSnpe))
#define GST_ML_SNPE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_SNPE,GstMLSnpeClass))
#define GST_IS_ML_SNPE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_SNPE))
#define GST_IS_ML_SNPE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_SNPE))
#define GST_ML_SNPE_CAST(obj)       ((GstMLSnpe *)(obj))

typedef struct _GstMLSnpe GstMLSnpe;
typedef struct _GstMLSnpeClass GstMLSnpeClass;

struct _GstMLSnpe {
  GstBaseTransform  parent;

  /// Buffer pools.
  GstBufferPool     *outpool;

  /// Machine learning engine.
  GstMLSnpeEngine   *engine;

  GstMLInfo         *ininfo;
  GstMLInfo         *outinfo;

  /// Properties.
  gchar             *model;
  GstMLSnpeDelegate delegate;
  gboolean          is_tensor;
  GList             *outputs;
};

struct _GstMLSnpeClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_snpe_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_SNPE_H__
