/*
* Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

typedef struct _GstGlesConverter GstGlesConverter;

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH
 *
 * #G_TYPE_INT, Output width.
 * Default: 0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH \
    "GstGlesVideoConverter.output-width"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT
 *
 * #G_TYPE_INT, Output height.
 * Default: 0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT \
    "GstGlesVideoConverter.output-height"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_X
 *
 * #G_TYPE_INT: Destination rectangle x axis start coordinate.
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_X \
    "GstGlesVideoConverter.destination-x"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_Y
 *
 * #G_TYPE_INT: Destination rectangle y axis start coordinate.
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_Y \
    "GstGlesVideoConverter.destination-y"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_WIDTH
 *
 * #G_TYPE_INT: Destination rectangle width.
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_WIDTH \
    "GstGlesVideoConverter.destination-width"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_HEIGHT
 *
 * #G_TYPE_INT: Destination rectangle height.
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_HEIGHT \
    "GstGlesVideoConverter.destination-height"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_RSCALE:
 *
 * #G_TYPE_FLOAT, Red color channel scale factor, used in normalize operation.
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_RSCALE \
    "GstGlesVideoConverter.rscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_GSCALE:
 *
 * #G_TYPE_FLOAT, Green color channel scale factor, used in normalize operation.
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_GSCALE \
    "GstGlesVideoConverter.gscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BSCALE
 *
 * #G_TYPE_FLOAT, Blue color channel scale factor, used in normalize operation.
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BSCALE \
    "GstGlesVideoConverter.bscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ASCALE:
 *
 * #G_TYPE_FLOAT, Alpha channel scale factor, used in normalize operation.
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ASCALE \
    "GstGlesVideoConverter.ascale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET
 *
 * #G_TYPE_FLOAT, Red channel offset, used in normalize operation.
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET \
    "GstGlesVideoConverter.roffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET
 *
 * #G_TYPE_FLOAT, Green channel offset, used in normalize operation.
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET \
    "GstGlesVideoConverter.goffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET
 *
 * #G_TYPE_FLOAT, Blue channel offset, used in normalize operation.
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET \
    "GstGlesVideoConverter.boffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET
 *
 * #G_TYPE_FLOAT, Alpha channel offset, used in normalize operation.
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET \
    "GstGlesVideoConverter.ascale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_QSCALE:
 *
 * #G_TYPE_FLOAT, Quantization scale factor, used in quantize operation.
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_QSCALE \
    "GstGlesVideoConverter.qscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_QOFFSET
 *
 * #G_TYPE_FLOAT, Quantization offset, used in quantize operation.
 * Default: 0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_QOFFSET \
    "GstGlesVideoConverter.qoffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE
 *
 * #G_TYPE_BOOLEAN: Engine operation normalizing input data to FLOAT.
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE \
    "GstGlesVideoConverter.normalize"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE
 *
 * #G_TYPE_BOOLEAN: Engine operation for quantizing the input data.
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE \
    "GstGlesVideoConverter.quantize"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_CONVERT_TO_UINT8
 *
 * #G_TYPE_BOOLEAN: Engine operation to convert input data to 8 bit UINT.
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_CONVERT_TO_UINT8 \
    "GstGlesVideoConverter.convert_to_uint8"

/**
 * gst_gles_converter_new:
 *
 * Initialize instance of GLES converter module.
 *
 * return: pointer to GLES converter on success or NULL on failure
 */
GST_VIDEO_API GstGlesConverter *
gst_gles_video_converter_new     (void);

/**
 * gst_gles_converter_free:
 * @convert: pointer to GLES converter module
 *
 * Deinitialise the GLES converter instance.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_gles_video_converter_free    (GstGlesConverter * convert);

/**
 * gst_gles_converter_set_ops:
 * @convert: pointer to GLES converter instance
 * @opts: pointer to structure containing options
 *
 * Configure the converter with the operations specified in opts structure.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_set_ops (GstGlesConverter * convert,
                                  GstStructure * opts);

/**
 * gst_gles_converter_process:
 * @convert: pointer to GLES converter instance
 * @inframes: Array of input video frames
 * @n_inputs: number of input frames
 * @outframe: output video frame
 *
 * Process input frames with the given options and place the result
 * into the provided output frame.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_process (GstGlesConverter * convert,
                                  GstVideoFrame * inframes, guint n_inputs,
                                  GstVideoFrame * outframe);

G_END_DECLS

#endif // __GST_GLES_VIDEO_CONVERTER_H__
