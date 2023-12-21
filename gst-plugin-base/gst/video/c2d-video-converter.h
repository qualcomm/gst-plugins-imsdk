/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_C2D_VIDEO_CONVERTER_H__
#define __GST_C2D_VIDEO_CONVERTER_H__

#include "video-converter-engine.h"

G_BEGIN_DECLS

typedef struct _GstC2dVideoConverter GstC2dVideoConverter;

/**
 * gst_c2d_video_converter_new:
 * @settings: Structure with optional settings.
 *
 * Initialize instance of C2D converter backend.
 *
 * return: Pointer to C2D converter on success or NULL on failure
 */
GST_VIDEO_API GstC2dVideoConverter *
gst_c2d_video_converter_new (GstStructure * settings);

/**
 * gst_c2d_video_converter_free:
 * @convert: Pointer to C2D converter backend.
 *
 * Deinitialise the C2D converter.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_c2d_video_converter_free (GstC2dVideoConverter * convert);

/**
 * gst_c2d_video_converter_compose:
 * @convert: Pointer to C2D converter backend.
 * @compositions: Array of composition frames.
 * @n_compositions: Number of compositions.
 * @fence: Optional fence to be filled if provided and used for async operation.
 *
 * Submit the a number of video composition which will be executed together.
 *
 * return: Pointer to request on success or NULL on failure
 */
GST_VIDEO_API gboolean
gst_c2d_video_converter_compose (GstC2dVideoConverter * convert,
                                 GstVideoComposition * compositions,
                                 guint n_compositions, gpointer * fence);

/**
 * gst_c2d_video_converter_wait_fence:
 * @convert: Pointer to C2D converter backend.
 * @fence: Asynchronously fence object associated with a compose request.
 *
 * Wait for the sumbitted to the GPU compositions to finish.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_c2d_video_converter_wait_fence (GstC2dVideoConverter * convert,
                                    gpointer fence);

/**
 * gst_c2d_video_converter_flush:
 * @convert: Pointer to C2D converter backend.
 *
 * Wait for compositions sumbitted to the GPU to finish and flush cached data.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_c2d_video_converter_flush (GstC2dVideoConverter * convert);

G_END_DECLS

#endif /* __GST_C2D_VIDEO_CONVERTER_H__ */
