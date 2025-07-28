/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SUITE_CAMERA_PIPELINE_H__
#define __GST_SUITE_CAMERA_PIPELINE_H__

#include "suite-utils.h"

G_BEGIN_DECLS

/**
 * ml_video_inference_pipeline:
 * @minfo: The model information for the pipeline.
 * @vinfo: The video information to verify.
 *
 * Function for creating inference pipeline.
 *
 * return: None
 */
void
ml_video_inference_pipeline (GstMLModelInfo *minfo,
    GstMLVideoInfo *vinfo);

G_END_DECLS

#endif /* __GST_SUITE_CAMERA_PIPELINES_H__ */
