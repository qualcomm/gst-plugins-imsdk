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
 * GST_GLES_VIDEO_CONVERTER_OPT_RESIZE_WIDTH
 *
 * #G_TYPE_INT, resized frame width
 * Default: 0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_RESIZE_WIDTH \
    "GstGlesVideoConverter.resize-width"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_RESIZE_HEIGHT
 *
 * #G_TYPE_INT, resized frame height
 * Default: 0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_RESIZE_HEIGHT \
    "GstGlesVideoConverter.resize-height"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_X
 *
 * #G_TYPE_INT: destination rectangle x axis start coordinate
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_X \
    "GstGlesVideoConverter.destination-x"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_Y
 *
 * #G_TYPE_INT: destination rectangle y axis start coordinate
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_Y \
    "GstGlesVideoConverter.destination-y"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_WIDTH
 *
 * #G_TYPE_INT: destination rectangle width
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_WIDTH \
    "GstGlesVideoConverter.destination-width"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_DEST_HEIGHT
 *
 * #G_TYPE_INT: destination rectangle height
 * Default:0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_DEST_HEIGHT \
    "GstGlesVideoConverter.destination-height"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_RSCALE:
 *
 * #G_TYPE_FLOAT, Red color channel scale factor
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_RSCALE \
    "GstGlesVideoConverter.rscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_GSCALE:
 *
 * #G_TYPE_FLOAT, Green color channel scale factor
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_GSCALE \
    "GstGlesVideoConverter.gscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BSCALE
 *
 * #G_TYPE_FLOAT, Blue color channel scale factor
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BSCALE \
    "GstGlesVideoConverter.bscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ASCALE:
 *
 * #G_TYPE_FLOAT, alpha channel scale factor
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ASCALE \
    "GstGlesVideoConverter.ascale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_QSCALE:
 *
 * #G_TYPE_FLOAT, Quantization scale factor
 * Default: 1.0/255.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_QSCALE \
    "GstGlesVideoConverter.qscale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET
 *
 * #G_TYPE_FLOAT, Red Channel offset
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET \
    "GstGlesVideoConverter.roffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET
 *
 * #G_TYPE_FLOAT, Green Channel offset
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET \
    "GstGlesVideoConverter.goffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET
 *
 * #G_TYPE_FLOAT, Blue Channel offset
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET \
    "GstGlesVideoConverter.boffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_QOFFSET
 *
 * #G_TYPE_FLOAT, Quantization offset
 * Default: -128.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_QOFFSET \
    "GstGlesVideoConverter.qoffset"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET
 *
 * #G_TYPE_FLOAT, Alpha Channel offset
 * Default: 0.0
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET \
    "GstGlesVideoConverter.ascale"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_RESIZE
 *
 * #G_TYPE_BOOLEAN: Gles Engine operation resize the
 * input frame  to output dimensions
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_RESIZE \
    "GstGlesVideoConverter.resize"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE
 *
 * #G_TYPE_BOOLEAN: Gles Engine operation normalizing the texture
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE \
    "GstGlesVideoConverter.normalize"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE
 *
 * #G_TYPE_BOOLEAN: Gles Engine operation quantization
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE \
    "GstGlesVideoConverter.quantize"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_CONVERTTOUINT8
 *
 * #G_TYPE_BOOLEAN: Gles Engine operation to convert data to 8 bit uint
 * Default: FALSE
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_CONVERT_TO_UINT8 \
    "GstGlesVideoConverter.convert_to_uint8"

/**
 * GST_GLES_VIDEO_CONVERTER_OPT_CROP
 *
 * #GST_TYPE_LIST: list of GstValueArray-A, encapsulating x, y, width and height
 * of clips identified by
 *            x - - A[0],
 *            y - - A[1],
 *        width - - A[2],
 *       height - - A[3] respectively
 * Default: NULL
 */
#define GST_GLES_VIDEO_CONVERTER_OPT_CROP \
    "GstGlesVideoConverter.crop"

/**
 * gst_gles_converter_new:
 *
 * Initialise instance of Gles converter module
 *
 * return: pointer to Gles converter module on success or NULL on failure
 */
GST_VIDEO_API GstGlesConverter *
gst_gles_video_converter_new     (void);

/**
 * gst_gles_converter_free:
 * @convert: pointer to Gles converter module
 *
 * Deinitialise the Gles converter instance
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_gles_video_converter_free    (GstGlesConverter * convert);

/**
 * gst_gles_converter_set_ops:
 * @convert: pointer to Gles converter instance
 * @opts: pointer to structure containing pipeline ops
 *
 * configure the dataconverter pipeline with the operations
 * specified in opts structure
 *
 * return: TRUE if successfully configures else FALSE
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_set_ops (GstGlesConverter * convert,
                                  GstStructure * opts);
/**
 * gst_gles_video_converter_set_crop_ops:
 * @convert: pointer to Gles converter instance
 * @crop_opts: pointer to structure containing List of crop arrays
 *
 * everytime caller gets new ROI list, use this API to set them and
 * let gles converter use them when clipping
 *
 * return: TRUE if the list of crop arrays is properly set
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_set_crop_ops (GstGlesConverter * convert,
    GstStructure * crop_opts);

/**
 * gst_gles_converter_process:
 * @convert: pointer to Gles converter instance
 * @inframes: Array of input videoframes
 * @n_inframes: number of input frames. In case of cropping, value is 1
 * @cropframe: Intermediate frame to hold output of Clip API, NULL when
 * cropping is disabled. Caller has to allocate memory properly. The
 * cropframe has the same meta and videoinfo like outframe.
 * @outframe: output video frame
 *
 * call DoPreprocess API from QImgConv Library to processs the
 * pipeline configured earlier
 *
 * return TRUE if successfully preprocessed else FALSE
 */
GST_VIDEO_API gboolean
gst_gles_video_converter_process (GstGlesConverter * convert,
                                  GstVideoFrame * inframes, guint n_inframes,
                                  GstVideoFrame * cropframe,
                                  GstVideoFrame * outframe);

G_END_DECLS

#endif // __GST_GLES_VIDEO_CONVERTER_H__
