/*
* Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_JPEGENC_CONTEXT_H__
#define __GST_JPEGENC_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/gstimagepool.h>

G_BEGIN_DECLS

#define GST_JPEGENC_CONTEXT_CAST(obj)   ((GstJPEGEncoderContext*)(obj))

typedef struct _GstJPEGEncoderContext GstJPEGEncoderContext;

typedef void (*GstJPEGEncoderCallback) (gint buf_fd, guint encoded_size,
    gpointer userdata);

/**
 * GST_JPEG_ENC_INPUT_WIDTH:
 *
 * #G_TYPE_UINT, input width
 */
#define GST_JPEG_ENC_INPUT_WIDTH \
    "GstJPEGEncoder.input-width"

/**
 * GST_JPEG_ENC_INPUT_HEIGHT:
 *
 * #G_TYPE_UINT, input height
 */
#define GST_JPEG_ENC_INPUT_HEIGHT \
    "GstJPEGEncoder.input-height"

/**
 * GST_JPEG_ENC_INPUT_FORMAT:
 *
 * #G_TYPE_UINT, input format
 */
#define GST_JPEG_ENC_INPUT_FORMAT \
    "GstJPEGEncoder.input-format"

/**
 * GST_JPEG_ENC_OUTPUT_WIDTH:
 *
 * #G_TYPE_UINT, output width
 */
#define GST_JPEG_ENC_OUTPUT_WIDTH \
    "GstJPEGEncoder.output-width"

/**
 * GST_JPEG_ENC_OUTPUT_HEIGHT:
 *
 * #G_TYPE_UINT, output height
 */
#define GST_JPEG_ENC_OUTPUT_HEIGHT \
    "GstJPEGEncoder.output-height"

/**
 * GST_JPEG_ENC_OUTPUT_FORMAT:
 *
 * #G_TYPE_UINT, output format
 */
#define GST_JPEG_ENC_OUTPUT_FORMAT \
    "GstJPEGEncoder.output-format"

/**
 * GST_JPEG_ENC_QUALITY:
 *
 * #G_TYPE_UINT, quality
 */
#define GST_JPEG_ENC_QUALITY \
    "GstJPEGEncoder.quality"

/**
 * GST_JPEG_ENC_ORIENTATION:
 *
 * #GST_TYPE_JPEG_ENC_ORIENTATION, set the orientation of Jpeg encoder
 * Default: #GST_JPEG_ENC_ORIENTATION_0.
 */
#define GST_JPEG_ENC_ORIENTATION \
    "GstJPEGEncoder.orientation"

enum {
  EVENT_UNKNOWN,
  EVENT_SERVICE_DIED,
};

typedef enum {
  GST_JPEG_ENC_ORIENTATION_0,
  GST_JPEG_ENC_ORIENTATION_90,
  GST_JPEG_ENC_ORIENTATION_180,
  GST_JPEG_ENC_ORIENTATION_270,
} GstJpegEncodeOrientation;

GST_API GstJPEGEncoderContext *
gst_jpeg_enc_context_new (GstJPEGEncoderCallback callback, gpointer userdata);

GST_API void
gst_jpeg_enc_context_free (GstJPEGEncoderContext * context);

GST_API gboolean
gst_jpeg_enc_context_config (GstJPEGEncoderContext * context,
    GstStructure * params);

GST_API gint
gst_jpeg_enc_context_execute (GstJPEGEncoderContext * context,
    GstBuffer * inbuff, GstBuffer * outbuff);

G_END_DECLS

#endif // __GST_JPEGENC_CONTEXT_H__
