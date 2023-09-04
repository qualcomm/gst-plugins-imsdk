/*
* Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#ifndef __GST_GLES_VIDEO_CONVERTER_H__
#define __GST_GLES_VIDEO_CONVERTER_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

// Composition flags valid only for the input frame.
#define GST_GLES_FLAG_FLIP_HORIZONTAL   (1 << 0)
#define GST_GLES_FLAG_FLIP_VERTICAL     (1 << 1)
#define GST_GLES_FLAG_ROTATE_90CW       (1 << 2)
#define GST_GLES_FLAG_ROTATE_180        (2 << 2)
#define GST_GLES_FLAG_ROTATE_90CCW      (3 << 2)

// Composition flags valid for both input and output frame.
#define GST_GLES_FLAG_UBWC_FORMAT       (1 << 6)

// Composition flags valid only for the output frame.
#define GST_GLES_FLAG_CLEAR_BACKGROUND  (1 << 7)
#define GST_GLES_FLAG_FLOAT32_FORMAT    (1 << 8)
#define GST_GLES_FLAG_FLOAT16_FORMAT    (2 << 8)

#define GST_GLES_BLIT_INIT { NULL, 255, NULL, NULL, 0, 0 }
#define GST_GLES_COMPOSITION_INIT \
    { NULL, 0, NULL, 0, { 0.0, 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0, 0.0 }, 0 }

typedef struct _GstGlesVideoConverter GstGlesVideoConverter;
typedef struct _GstGlesBlit GstGlesBlit;
typedef struct _GstGlesComposition GstGlesComposition;

/**
 * GstGlesBlit:
 * @frame: Input video frame.
 * @alpha: Global alpha, 0 = fully transparent, 255 = fully opaque.
 * @sources: Source regions in the frame.
 * @destinations: Destination regions in the frame.
 * @n_regions: Number of Source - Destination region pairs.
 * @flags: Bitwise configuration mask for the input frame.
 *
 * Blit source. Input frame along with a possible crop and destination
 * rectangles and configuration mask.
 */
struct _GstGlesBlit
{
  GstVideoFrame     *frame;
  guint8            alpha;

  GstVideoRectangle *sources;
  GstVideoRectangle *destinations;
  guint8            n_regions;

  guint64           flags;
};

/**
 * GstGlesComposition:
 * @blits: Array of blit objects.
 * @n_blits: Number of blit objects.
 * @frame: Output video frame where the blit objects will be placed.
 * @bgcolor: Background color to be applied if CLEAR_BACKGROUND flag is present.
 * @offsets: Channel offset factors, used in normalize operation.
 * @scales: Channel scale factors, used in normalize operation.
 * @flags: Bitwise configuration mask for the output.
 *
 * Blit composition.
 */
struct _GstGlesComposition
{
  GstGlesBlit   *blits;
  guint         n_blits;

  GstVideoFrame *frame;
  guint32       bgcolor;

  gdouble       offsets[4];
  gdouble       scales[4];

  guint64       flags;
};

/**
 * gst_gles_video_converter_new:
 *
 * Initialize instance of GLES converter module.
 *
 * return: Pointer to GLES converter on success or NULL on failure
 */
GST_VIDEO_API GstGlesVideoConverter *
gst_gles_video_converter_new     (void);

/**
 * gst_gles_video_converter_free:
 * @convert: Pointer to GLES converter module
 *
 * Deinitialise the GLES converter instance.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_gles_video_converter_free    (GstGlesVideoConverter * convert);

/**
 * gst_gles_video_converter_submit_request:
 * @convert: Pointer to GLES converter instance
 * @compositions: Array of composition frames
 * @n_compositions: Number of compositions
 *
 * Submit the a number of video composition which will be executed together.
 *
 * return: Pointer to request on success or NULL on failure
 */
GST_VIDEO_API gpointer
gst_gles_video_converter_submit_request (GstGlesVideoConverter * convert,
                                         GstGlesComposition * compositions,
                                         guint n_compositions);

/**
 * gst_gles_video_converter_wait_request:
 * @convert: Pointer to GLES converter instance
 * @request_id: Indentification of the request
 *
 * Wait for the sumbitted to the GPU compositions to finish.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_wait_request (GstGlesVideoConverter * convert,
                                       gpointer request_id);

/**
 * gst_gles_video_converter_flush:
 * @convert: Pointer to GLES converter instance
 *
 * Wait for compositions sumbitted to the GPU to finish and flush cached data.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_gles_video_converter_flush        (GstGlesVideoConverter * convert);

G_END_DECLS

#endif // __GST_GLES_VIDEO_CONVERTER_H__
