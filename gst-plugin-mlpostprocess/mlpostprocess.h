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
*
* Changes from Qualcomm Technologies, Inc. are provided under the following license:
*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QTI_ML_POST_PROCESS_H__
#define __GST_QTI_ML_POST_PROCESS_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "ml-post-process-utils.h"
#include "ml-post-process-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_POST_PROCESS (gst_ml_post_process_get_type())
#define GST_ML_POST_PROCESS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ML_POST_PROCESS, \
                              GstMLPostProcess))
#define GST_ML_POST_PROCESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ML_POST_PROCESS, \
                           GstMLPostProcessClass))
#define GST_IS_ML_POST_PROCESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ML_POST_PROCESS))
#define GST_IS_ML_POST_PROCESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ML_POST_PROCESS))
#define GST_ML_POST_PROCESS_CAST(obj) ((GstMLPostProcess *)(obj))

typedef struct _GstMLPostProcess GstMLPostProcess;
typedef struct _GstMLPostProcessClass GstMLPostProcessClass;

// Convient pointer to either module or signal post-process function.
typedef gboolean (*GstMLProcess) \
    (GstMLPostProcess*, guint, GstMLFrame*, GstStructure*, gpointer);

struct _GstMLPostProcess {
  GstBaseTransform parent;

  /// Input ML info.
  GstMLInfo        *mlinfo;
  /// Output Video info.
  GstVideoInfo     *vinfo;
  /// Buffer pools.
  GstBufferPool    *outpool;

  /// The ID of this stage of ML inference.
  guint            stage_id;

  /// Output mode (Video, Text or Tensors)
  guint            outmode;
  /// Post processing type.
  GQuark           type;
  /// Post process module module engine.
  GstMLEngine      *engine;
  /// Array with ML params for each batch.
  GPtrArray        *mlparams;

  // Stashed ML predictions used fot stabilization
  GList            *stashedpredictions;

  /// Processing function chosen based on post-process type.
  GstMLProcess     process;

  /// Properties.
  gint             mdlenum;
  gchar            *labels;
  guint            n_results;
  gchar            *settings;
  gboolean         stabilization;
};

struct _GstMLPostProcessClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_post_process_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_H__
