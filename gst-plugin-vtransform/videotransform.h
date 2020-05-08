/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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
*/

#ifndef __GST_QTI_VIDEO_TRANSFORM_H__
#define __GST_QTI_VIDEO_TRANSFORM_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/video/c2d-video-converter.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_TRANSFORM \
  (gst_video_transform_get_type())
#define GST_VIDEO_TRANSFORM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_TRANSFORM,GstVideoTransform))
#define GST_VIDEO_TRANSFORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_TRANSFORM,GstVideoTransformClass))
#define GST_IS_VIDEO_TRANSFORM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_TRANSFORM))
#define GST_IS_VIDEO_TRANSFORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_TRANSFORM))
#define GST_VIDEO_TRANSFORM_CAST(obj)       ((GstVideoTransform *)(obj))

typedef enum {
  GST_VIDEO_TRANSFORM_ROTATE_NONE,
  GST_VIDEO_TRANSFORM_ROTATE_90_CW,
  GST_VIDEO_TRANSFORM_ROTATE_90_CCW,
  GST_VIDEO_TRANSFORM_ROTATE_180,
} GstVideoTransformRotate;

typedef struct _GstVideoTransform GstVideoTransform;
typedef struct _GstVideoTransformClass GstVideoTransformClass;

struct _GstVideoTransform {
  GstBaseTransform        parent;

  /// Properties.
  gboolean                flip_v;
  gboolean                flip_h;
  GstVideoTransformRotate rotation;
  GstVideoRectangle       crop;

  GstVideoInfo            *ininfo;
  GstVideoInfo            *outinfo;

  // Output buffer pool
  GstBufferPool           *outpool;

  /// Supported converters.
  GstC2dVideoConverter    *c2dconvert;
};

struct _GstVideoTransformClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_video_transform_get_type (void);

G_END_DECLS

#endif // __GST_QTI_VIDEO_TRANSFORM_H__
