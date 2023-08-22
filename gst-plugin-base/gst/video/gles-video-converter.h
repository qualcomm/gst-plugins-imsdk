/*
* Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES
 *
 * #G_TYPE_ARRAY: Array of source GstVideoRectangle.
 * Default: NULL
 *
 * Not applicable for output.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES \
    "GstGlesVideoConverter.source-rectangles"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES
 *
 * #G_TYPE_ARRAY: Array of destination GstVideoRectangle.
 * Default: NULL
 *
 * Not applicable for output.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES \
    "GstGlesVideoConverter.destination-rectangles"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL:
 *
 * #G_TYPE_BOOLEAN, flip output horizontally
 * Default: FALSE
 *
 * Not applicable for output
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL \
    "GstGlesVideoConverter.flip-horizontal"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_FLIP_VERTICAL:
 *
 * #G_TYPE_BOOLEAN, flip output horizontally
 * Default: FALSE
 *
 * Not applicable for output
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_FLIP_VERTICAL \
    "GstGlesVideoConverter.flip-vertical"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ALPHA:
 *
 * #G_TYPE_DOUBLE, alpha channel occupancy
 * Default: 1.0
 *
 * Not applicable for output
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ALPHA \
    "GstGlesVideoConverter.alpha"

/**
 * GstGlesVideoRotate:
 * @GST_GLES_VIDEO_ROTATE_NONE: disable rotation of the output
 * @GST_GLES_VIDEO_ROTATE_90_CW: rotate output 90 degrees clockwise
 * @GST_GLES_VIDEO_ROTATE_90_CCW: rotate output 90 degrees counter-clockwise
 * @GST_GLES_VIDEO_ROTATE_180: rotate output 180 degrees
 *
 * Different output rotation modes
 */
typedef enum {
  GST_GLES_VIDEO_ROTATE_NONE,
  GST_GLES_VIDEO_ROTATE_90_CW,
  GST_GLES_VIDEO_ROTATE_90_CCW,
  GST_GLES_VIDEO_ROTATE_180,
} GstGlesVideoRotate;

GST_VIDEO_API GType gst_gles_video_rotation_get_type (void);
#define GST_TYPE_GLES_VIDEO_ROTATION (gst_gles_video_rotation_get_type())

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ROTATION:
 *
 * #GST_TYPE_GLES_VIDEO_ROTATION, set the output rotation flags
 * Default: #GST_GLES_VIDEO_ROTATE_NONE.
 *
 * Not applicable for output
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ROTATION \
    "GstGlesVideoConverter.rotation"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BACKGROUND:
 *
 * #G_TYPE_UINT, background color
 * Default: 0x00000000
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BACKGROUND \
    "GstGlesVideoConverter.background"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_CLEAR:
 *
 * #G_TYPE_BOOLEAN, clear image pixels and apply background color
 * Default: TRUE
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_CLEAR \
    "GstGlesVideoConverter.clear-background"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_RSCALE:
 *
 * #G_TYPE_FLOAT, Red color channel scale factor, used in normalize operation.
 * Default: 128.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_RSCALE \
    "GstGlesVideoConverter.rscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_GSCALE:
 *
 * #G_TYPE_FLOAT, Green color channel scale factor, used in normalize operation.
 * Default: 128.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_GSCALE \
    "GstGlesVideoConverter.gscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BSCALE
 *
 * #G_TYPE_FLOAT, Blue color channel scale factor, used in normalize operation.
 * Default: 128.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BSCALE \
    "GstGlesVideoConverter.bscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ASCALE:
 *
 * #G_TYPE_FLOAT, Alpha channel scale factor, used in normalize operation.
 * Default: 128.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ASCALE \
    "GstGlesVideoConverter.ascale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET
 *
 * #G_TYPE_FLOAT, Red channel offset, used in normalize operation.
 * Default: 0.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET \
    "GstGlesVideoConverter.roffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET
 *
 * #G_TYPE_FLOAT, Green channel offset, used in normalize operation.
 * Default: 0.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET \
    "GstGlesVideoConverter.goffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET
 *
 * #G_TYPE_FLOAT, Blue channel offset, used in normalize operation.
 * Default: 0.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET \
    "GstGlesVideoConverter.boffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET
 *
 * #G_TYPE_FLOAT, Alpha channel offset, used in normalize operation.
 * Default: 0.0
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET \
    "GstGlesVideoConverter.aoffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_FLOAT16_FORMAT
 *
 * #G_TYPE_BOOLEAN: whether buffer pixels are represented as 16 bit float values
 * Default: FALSE
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_FLOAT16_FORMAT \
    "GstGlesVideoConverter.float16-format"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_FLOAT32_FORMAT
 *
 * #G_TYPE_BOOLEAN: whether buffer pixels are represented as 32 bit float values
 * Default: FALSE
 *
 * Not applicable for input.
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_FLOAT32_FORMAT \
    "GstGlesVideoConverter.float32-format"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT:
 *
 * #G_TYPE_BOOLEAN, whether buffers have UBWC (Universal Bandwidth Compression)
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT \
    "GstGlesVideoConverter.ubwc-format"

typedef struct _GstGlesVideoConverter GstGlesVideoConverter;
typedef struct _GstGlesComposition GstGlesComposition;

/**
 * GstGlesComposition:
 * @inframes: Array of input video frames
 * @n_inputs: Number of input frames
 * @outframe: Output video frame
 *
 * Blit composition. Input frames will be placed in the output frame based
 * on a previously set configuration.
 */
struct _GstGlesComposition
{
  GstVideoFrame *inframes;
  guint         n_inputs;
  GstVideoFrame *outframe;
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
 * gst_gles_video_converter_set_input_opts:
 * @convert: Pointer to GLES converter instance
 * @index: Input frame index
 * @opts: Pointer to structure containing options
 *
 * Configure source and destination rectangles, rotation, and other parameters
 * that are going to be used on the input frame with given index. Index is
 * accumulative across all compositions. If there are 3 compositions each with
 * 2 input frames then the indexes for the 3rd composition will be 5 and 6.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_set_input_opts (GstGlesVideoConverter * convert,
                                         guint index, GstStructure * opts);

/**
 * gst_gles_video_converter_set_process_opts:
 * @convert: Pointer to GLES converter instance
 * @index: Output frame index.
 * @opts: Pointer to structure containing options
 *
 * Configure a set of operations that will be performed on the output frame
 * for each composition. The index will corresppond to the index of the
 * video frame composition submited in submit_request API as each one contains
 * a single output frame.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_set_output_opts (GstGlesVideoConverter * convert,
                                          guint index, GstStructure * opts);

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
 * Wait for compositions sumbitted to the GPU to finish.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_gles_video_converter_flush        (GstGlesVideoConverter * convert);

G_END_DECLS

#endif // __GST_GLES_VIDEO_CONVERTER_H__
