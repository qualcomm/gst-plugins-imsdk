/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_JPEG_ENC_H__
#define __GST_QTI_JPEG_ENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/base/gstdataqueue.h>

#include "jpegenc-context.h"

G_BEGIN_DECLS

#define GST_TYPE_JPEG_ENC \
  (gst_jpeg_enc_get_type())
#define GST_JPEG_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JPEG_ENC,GstJPEGEncoder))
#define GST_JPEG_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JPEG_ENC,GstJPEGEncoderClass))
#define GST_IS_JPEG_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JPEG_ENC))
#define GST_IS_JPEG_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JPEG_ENC))
#define GST_JPEG_ENC_CAST(obj)       ((GstJPEGEncoder *)(obj))

typedef struct _GstJPEGEncoder GstJPEGEncoder;
typedef struct _GstJPEGEncoderClass GstJPEGEncoderClass;
typedef struct _GstVideoFrameData GstVideoFrameData;

struct _GstJPEGEncoder {
  GstVideoEncoder          parent;

  /// Properties.
  gint                     quality;
  GstJpegEncodeOrientation orientation;
  /// Camera id to process
  guint                    camera_id;

  // Output buffer pool
  GstBufferPool            *outpool;
  /// Jpeg encoder context
  GstJPEGEncoderContext    *context;
  /// Jpeg encoder input frames queue
  GstDataQueue             *inframes;
  /// Worker task.
  GstTask                  *worktask;
  /// Worker task mutex.
  GRecMutex                worklock;
};

struct _GstJPEGEncoderClass {
  GstVideoEncoderClass parent;
};

G_GNUC_INTERNAL GType gst_jpeg_enc_get_type (void);

G_END_DECLS

#endif // __GST_QTI_JPEG_ENC_H__
