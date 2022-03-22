/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_ML_AIC_ENGINE_H__
#define __GST_ML_AIC_ENGINE_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS

#define GST_ML_AIC_INVALID_ID  (-1)

/**
 * GST_ML_AIC_ENGINE_OPT_MODEL:
 *
 * #G_TYPE_STRING, neural network model file path and name
 * Default: NULL
 */
#define GST_ML_AIC_ENGINE_OPT_MODEL \
    "GstMLAicEngine.model"

/**
 * GST_ML_AIC_ENGINE_OPT_DEVICES:
 *
 * #GST_TYPE_ARRAY, list of AIC100 device IDs to use in the engine
 * Default: NULL
 */
#define GST_ML_AIC_ENGINE_OPT_DEVICES \
    "GstMLAicEngine.devices"

/**
 * GST_ML_AIC_ENGINE_OPT_NUM_ACTIVATIONS:
 *
 * #G_TYPE_UINT, number of activation available to the engine
 * Default: 1
 */
#define GST_ML_AIC_ENGINE_OPT_NUM_ACTIVATIONS \
    "GstMLAicEngine.num-activations"

typedef struct _GstMLAicEngine GstMLAicEngine;

GST_API GstMLAicEngine *
gst_ml_aic_engine_new              (GstStructure * settings);

GST_API void
gst_ml_aic_engine_free             (GstMLAicEngine * engine);

GST_API const GstMLInfo *
gst_ml_aic_engine_get_input_info   (GstMLAicEngine * engine);

GST_API const GstMLInfo *
gst_ml_aic_engine_get_output_info  (GstMLAicEngine * engine);

GST_API gint
gst_ml_aic_engine_submit_request   (GstMLAicEngine * engine,
                                    GstMLFrame * inframe,
                                    GstMLFrame * outframe);

GST_API gboolean
gst_ml_aic_engine_wait_request     (GstMLAicEngine * engine,
                                    gint request_id);

G_END_DECLS

#endif /* __GST_ML_AIC_ENGINE_H__ */
