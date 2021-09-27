/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CVP_IMGPYRAMID_ENGINE_H__
#define __GST_CVP_IMGPYRAMID_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

typedef struct _GstCvpImgPyramidSettings GstCvpImgPyramidSettings;
typedef struct _GstCvpImgPyramidEngine GstCvpImgPyramidEngine;

struct _GstCvpImgPyramidSettings {
  guint          width;
  guint          height;
  guint          stride;
  guint          scanline;
  guint          framerate;
  GstVideoFormat format;
  guint          n_octaves;
  guint          n_scales;
  GArray         *div2coef;
};

GST_API GstCvpImgPyramidSettings *
gst_cvp_imgpyramid_settings ();

GST_API GstCvpImgPyramidEngine *
gst_cvp_imgpyramid_engine_new     (GstCvpImgPyramidSettings * settings,
                                   GArray * sizes);

GST_API void
gst_cvp_imgpyramid_engine_free    (GstCvpImgPyramidEngine * engine);

GST_API gboolean
gst_cvp_imgpyramid_engine_execute (GstCvpImgPyramidEngine * engine,
                                   const GstVideoFrame * inframe,
                                   GstBufferList * outbuffers);

G_END_DECLS

#endif /* __GST_CVP_IMGPYRAMID_ENGINE_H__ */