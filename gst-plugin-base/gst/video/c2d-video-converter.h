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

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

// Composition flags valid only for the input frame.
#define GST_C2D_FLAG_FLIP_HORIZONTAL  (1 << 0)
#define GST_C2D_FLAG_FLIP_VERTICAL    (1 << 1)
#define GST_C2D_FLAG_ROTATE_90CW      (1 << 2)
#define GST_C2D_FLAG_ROTATE_180       (2 << 2)
#define GST_C2D_FLAG_ROTATE_90CCW     (3 << 2)

// Composition flags valid for both input and output frame.
#define GST_C2D_FLAG_UBWC_FORMAT      (1 << 6)

// Composition flags valid only for the output frame.
#define GST_C2D_FLAG_CLEAR_BACKGROUND (1 << 7)

#define GST_C2D_BLIT_INIT { NULL, 255, NULL, NULL, 0, 0 }
#define GST_C2D_COMPOSITION_INIT { NULL, 0, NULL, 0, 0 }

typedef struct _GstC2dVideoConverter GstC2dVideoConverter;
typedef struct _GstC2dBlit GstC2dBlit;
typedef struct _GstC2dComposition GstC2dComposition;


/**
 * GstC2dBlit:
 * @frame: Input video frame.
 * @alpha: Global alpha, 0 = fully transparent, 255 = fully opaque.
 * @sources: Source regions in the frame.
 * @destinations: Destination regions in the frame.
 * @n_regions: Number of Source - Destination region pairs.
 * @flags: Bitwise configuration mask for the input frame.
 *
 * Blit entry. Contains input frame along with a possible crop and destination
 * rectanbgle and configuration mask.
 */
struct _GstC2dBlit
{
  GstVideoFrame     *frame;
  guint8            alpha;

  GstVideoRectangle *sources;
  GstVideoRectangle *destinations;
  guint8            n_regions;

  guint64           flags;
};

/**
 * GstC2dComposition:
 * @blits: Array of blit entries.
 * @n_blits: Number of blit entries.
 * @frame: Output video frame where the blit entries will be placed.
 * @bgcolor: Background color to be applied if CLEAR_BACKGROUND flag is present.
 * @flags: Bitwise configuration mask for the output.
 *
 * Blit composition.
 */
struct _GstC2dComposition
{
  GstC2dBlit    *blits;
  guint         n_blits;

  GstVideoFrame *frame;
  guint32       bgcolor;

  guint64       flags;
};

/**
 * gst_c2d_video_converter_new:
 *
 * Initialize instance of C2D converter module.
 *
 * return: Pointer to C2D converter on success or NULL on failure
 */
GST_VIDEO_API GstC2dVideoConverter *
gst_c2d_video_converter_new             (void);

/**
 * gst_c2d_video_converter_free:
 * @convert: Pointer to C2D converter module
 *
 * Deinitialise the C2D converter instance.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_c2d_video_converter_free            (GstC2dVideoConverter *convert);

/**
 * gst_c2d_video_converter_submit_request:
 * @convert: Pointer to C2D converter instance
 * @compositions: Array of composition frames
 * @n_compositions: Number of compositions
 *
 * Submit the a number of video composition which will be executed together.
 *
 * return: Pointer to request on success or NULL on failure
 */
GST_VIDEO_API gpointer
gst_c2d_video_converter_submit_request  (GstC2dVideoConverter *convert,
                                         GstC2dComposition * compositions,
                                         guint n_compositions);

/**
 * gst_c2d_video_converter_wait_request:
 * @convert: Pointer to C2D converter instance
 * @request_id: Indentification of the request
 *
 * Wait for the sumbitted to the GPU compositions to finish.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_c2d_video_converter_wait_request    (GstC2dVideoConverter *convert,
                                         gpointer request_id);

/**
 * gst_c2d_video_converter_flush:
 * @convert: Pointer to C2D converter instance
 *
 * Wait for compositions sumbitted to the GPU to finish and flush cached data.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_c2d_video_converter_flush           (GstC2dVideoConverter *convert);

G_END_DECLS

#endif /* __GST_C2D_VIDEO_CONVERTER_H__ */
