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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_CVP_OPTCLFLOW_ENGINE_H__
#define __GST_CVP_OPTCLFLOW_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_WIDTH:
 *
 * #G_TYPE_UINT, video source width
 * Default: 0
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_WIDTH \
    "GstCvOptclFlowEngine.video-width"

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_HEIGHT:
 *
 * #G_TYPE_UINT, video source height
 * Default: 0
 *
 * Not applicable for output
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_HEIGHT \
    "GstCvOptclFlowEngine.video-height"

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_STRIDE:
 *
 * #G_TYPE_UINT, video source aligned width
 * Default: 0
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_STRIDE \
    "GstCvOptclFlowEngine.video-stride"

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_SCANLINE:
 *
 * #G_TYPE_UINT, video source aligned height
 * Default: 0
 *
 * Not applicable for output
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_SCANLINE \
    "GstCvOptclFlowEngine.video-scanline"

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_FORMAT:
 *
 * #GST_TYPE_VIDEO_SOURCE_FORMAT, set the video source format
 * Default: #GST_VIDEO_FORMAT_UNKNOWN.
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_FORMAT \
    "GstCvOptclFlowEngine.video-format"

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_FPS:
 *
 * #G_TYPE_UINT, video source frame rate in frames per second
 * Default: 0
 *
 * Not applicable for output
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_FPS \
    "GstCvOptclFlowEngine.video-fps"

/**
 * GST_CV_OPTCLFLOW_ENGINE_OPT_ENABLE_STATS:
 *
 * #G_TYPE_BOOLEAN, Enable/disable additional motion vector statistics
 * Default: TRUE
 */
#define GST_CV_OPTCLFLOW_ENGINE_OPT_ENABLE_STATS \
    "GstCvOptclFlowEngine.enable-stats"


typedef struct _GstCvOptclFlowEngine GstCvOptclFlowEngine;

GST_API GstCvOptclFlowEngine *
gst_cv_optclflow_engine_new     (GstStructure * settings);

GST_API void
gst_cv_optclflow_engine_free    (GstCvOptclFlowEngine * engine);

GST_API gboolean
gst_cv_optclflow_engine_sizes   (GstCvOptclFlowEngine * engine,
                                 guint * mvsize, guint * statsize);

GST_API gboolean
gst_cv_optclflow_engine_execute (GstCvOptclFlowEngine * engine,
                                 const GstVideoFrame * inframes, guint n_inputs,
                                 GstBuffer * outbuffer);

G_END_DECLS

#endif /* __GST_CVP_OPTCLFLOW_ENGINE_H__ */
