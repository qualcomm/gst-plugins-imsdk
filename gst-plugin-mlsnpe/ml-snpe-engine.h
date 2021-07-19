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

#ifndef __GST_ML_SNPE_ENGINE_H__
#define __GST_ML_SNPE_ENGINE_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/ml/ml-info.h>

G_BEGIN_DECLS

/**
 * GST_ML_SNPE_ENGINE_OPT_MODEL:
 *
 * #G_TYPE_STRING, neural network model file path and name
 * Default: NULL
 */
#define GST_ML_SNPE_ENGINE_OPT_MODEL \
    "GstMlSnpeEngine.model"

/**
 * GstMLSnpeDelegate:
 * @GST_ML_SNPE_DELEGATE_NONE: CPU is used for all operations
 * @GST_ML_SNPE_DELEGATE_DSP: Hexagon Digital Signal Processor
 * @GST_ML_SNPE_DELEGATE_GPU: Graphics Processing Unit
 * @GST_ML_SNPE_DELEGATE_AIP: Snapdragon AIX + HVX
 *
 * Different delegates for transfering part of all of the work
 */
typedef enum {
  GST_ML_SNPE_DELEGATE_NONE,
  GST_ML_SNPE_DELEGATE_DSP,
  GST_ML_SNPE_DELEGATE_GPU,
  GST_ML_SNPE_DELEGATE_AIP,
} GstMLSnpeDelegate;

GST_API GType gst_ml_snpe_delegate_get_type (void);
#define GST_TYPE_ML_SNPE_DELEGATE (gst_ml_snpe_delegate_get_type())

/**
 * GST_ML_SNPE_ENGINE_OPT_DELEGATE:
 *
 * #GST_TYPE_ML_SNPE_DELEGATE, set the delegate
 * Default: #GST_ML_SNPE_DELEGATE_NONE.
 */
#define GST_ML_SNPE_ENGINE_OPT_DELEGATE \
    "GstMlSnpeEngine.delegate"

/**
 * GST_ML_SNPE_ENGINE_OPT_LAYERS:
 *
 * #GST_TYPE_ARRAY, set the delegate
 * Default: NULL.
 */
#define GST_ML_SNPE_ENGINE_OPT_LAYERS \
    "GstMlSnpeEngine.layers"

typedef struct _GstMLSnpeEngine GstMLSnpeEngine;

GST_API GstMLSnpeEngine *
gst_ml_snpe_engine_new              (GstStructure * settings);

GST_API void
gst_ml_snpe_engine_free             (GstMLSnpeEngine * engine);

GST_API const GstMLInfo *
gst_ml_snpe_engine_get_input_info   (GstMLSnpeEngine * engine);

GST_API const GstMLInfo *
gst_ml_snpe_engine_get_output_info  (GstMLSnpeEngine * engine);

GST_API gboolean
gst_ml_snpe_engine_execute          (GstMLSnpeEngine * engine,
                                     GstBuffer * inbuffer,
                                     GstBuffer * outbuffer);

G_END_DECLS

#endif /* __GST_ML_SNPE_ENGINE_H__ */
