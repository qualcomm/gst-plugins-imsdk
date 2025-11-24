/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
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
