/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_JPEGENC_CONTEXT_H__
#define __GST_JPEGENC_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/gstimagepool.h>

G_BEGIN_DECLS

#define GST_JPEGENC_CONTEXT_CAST(obj)   ((GstJPEGEncoderContext*)(obj))

typedef struct _GstJPEGEncoderContext GstJPEGEncoderContext;

typedef void (*GstJPEGEncoderCallback) (GstVideoCodecFrame * frame,
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

/**
 * GST_JPEG_ENC_CAMERA_ID:
 *
 * #G_TYPE_UINT, camera id
 */
#define GST_JPEG_ENC_CAMERA_ID \
    "GstJPEGEncoder.camera-id"

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

typedef struct _GstJPEGEncoderInParams {
  guint camera_id;
  guint width;
  guint height;
} GstJPEGEncoderInParams;

typedef struct _GstJPEGEncoderOutParams {
  guint jpeg_size;
} GstJPEGEncoderOutParams;

GST_API GstJPEGEncoderContext *
gst_jpeg_enc_context_new (GstJPEGEncoderCallback callback, gpointer userdata);

GST_API void
gst_jpeg_enc_context_free (GstJPEGEncoderContext * context);

GST_API gboolean
gst_jpeg_enc_context_get_params (GstJPEGEncoderContext * context,
    const GstJPEGEncoderInParams in_params, GstJPEGEncoderOutParams * out_params);

GST_API gboolean
gst_jpeg_enc_context_create (GstJPEGEncoderContext * context,
    GstStructure * params);

GST_API gboolean
gst_jpeg_enc_context_destroy (GstJPEGEncoderContext * context);

GST_API gboolean
gst_jpeg_enc_context_execute (GstJPEGEncoderContext * context,
    GstVideoCodecFrame * frame, gint quality);

G_END_DECLS

#endif // __GST_JPEGENC_CONTEXT_H__
