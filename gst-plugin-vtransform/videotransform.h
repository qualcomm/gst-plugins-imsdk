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

#ifndef __GST_QTI_VIDEO_TRANSFORM_H__
#define __GST_QTI_VIDEO_TRANSFORM_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#ifdef USE_C2D_CONVERTER
#include <gst/video/c2d-video-converter.h>
#endif // USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
#include <gst/video/gles-video-converter.h>
#endif // USE_GLES_CONVERTER
#ifdef USE_FCV_CONVERTER
#include <gst/video/fcv-video-converter.h>
#endif // USE_FCV_CONVERTER

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

#define GST_VIDEO_TRANSFORM_GET_LOCK(obj) (&GST_VIDEO_TRANSFORM(obj)->lock)
#define GST_VIDEO_TRANSFORM_LOCK(obj) \
  g_mutex_lock(GST_VIDEO_TRANSFORM_GET_LOCK(obj))
#define GST_VIDEO_TRANSFORM_UNLOCK(obj) \
  g_mutex_unlock(GST_VIDEO_TRANSFORM_GET_LOCK(obj))

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
  ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
      : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
          : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
              : (state <= GST_STATE_NULL))))

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

  /// Global mutex lock.
  GMutex                  lock;

  GstVideoInfo            *ininfo;
  GstVideoInfo            *outinfo;

  // Features of the negotiated input and output caps.
  GQuark                  infeature;
  GQuark                  outfeature;

  // Whether input and output caps have UBWC compression.
  gboolean                inubwc;
  gboolean                outubwc;

  // Output buffer pool
  GstBufferPool           *outpool;

  /// Supported converters.
#ifdef USE_C2D_CONVERTER
  GstC2dVideoConverter *c2dconvert;
#endif // USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
  GstGlesVideoConverter *glesconvert;
#endif // USE_GLES_CONVERTER
#ifdef USE_FCV_CONVERTER
  GstFcvVideoConverter *fcvconvert;
#endif // USE_FCV_CONVERTER

  /// Properties.
  gboolean                flip_v;
  gboolean                flip_h;
  GstVideoTransformRotate rotation;
  GstVideoRectangle       crop;
  GstVideoRectangle       destination;
  guint                   background;
};

struct _GstVideoTransformClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_video_transform_get_type (void);

G_END_DECLS

#endif // __GST_QTI_VIDEO_TRANSFORM_H__
