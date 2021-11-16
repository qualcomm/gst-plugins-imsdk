/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QTI_ML_VIDEO_CONVERTER_H__
#define __GST_QTI_ML_VIDEO_CONVERTER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/ml/ml-info.h>

#ifdef USE_C2D_CONVERTER
#include <gst/video/c2d-video-converter.h>
#endif //USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
#include <gst/video/gles-video-converter.h>
#endif //USE_GLES_CONVERTER

G_BEGIN_DECLS

#define GST_TYPE_ML_VIDEO_CONVERTER (gst_ml_video_converter_get_type())
#define GST_ML_VIDEO_CONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ML_VIDEO_CONVERTER, \
                              GstMLVideoConverter))
#define GST_ML_VIDEO_CONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ML_VIDEO_CONVERTER, \
                           GstMLVideoConverterClass))
#define GST_IS_ML_VIDEO_CONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ML_VIDEO_CONVERTER))
#define GST_IS_ML_VIDEO_CONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ML_VIDEO_CONVERTER))
#define GST_ML_VIDEO_CONVERTER_CAST(obj) ((GstMLVideoConverter *)(obj))

#define GST_TYPE_ML_VIDEO_PIXEL_LAYOUT (gst_ml_video_pixel_layout_get_type())

typedef struct _GstMLVideoConverter GstMLVideoConverter;
typedef struct _GstMLVideoConverterClass GstMLVideoConverterClass;

typedef enum {
  GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR,
  GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE,
} GstVideoPixelLayout;

struct _GstMLVideoConverter {
  GstBaseTransform     parent;

  /// Input video info.
  GstVideoInfo         *ininfo;

  /// Output ML info and corresponding video info.
  GstVideoInfo         *vinfo;
  GstMLInfo            *mlinfo;

  /// Buffer pools.
  GstBufferPool        *outpool;

  /// Source aspect ratio, extracted from input caps.
  gint                 sar_n;
  gint                 sar_d;

  /// Supported converters.
#ifdef USE_C2D_CONVERTER
  GstC2dVideoConverter *c2dconvert;
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  GstGlesConverter     *glesconvert;
#endif // USE_GLES_CONVERTER

  /// Properties.
  GstVideoPixelLayout  pixlayout;
  GArray               *mean;
  GArray               *sigma;
};

struct _GstMLVideoConverterClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_video_converter_get_type (void);

G_GNUC_INTERNAL GType gst_ml_video_pixel_layout_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_VIDEO_CONVERTER_H__
