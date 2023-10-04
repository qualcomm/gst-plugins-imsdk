/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_IMGPYRAMID_ENGINE_H__
#define __GST_IMGPYRAMID_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

typedef struct _GstImgPyramidEngine GstImgPyramidEngine;
typedef struct _GstImgPyramidSettings GstImgPyramidSettings;

struct _GstImgPyramidSettings {
  guint          width;
  guint          height;
  guint          stride;
  guint          scanline;
  guint          framerate;
  GstVideoFormat format;
  guint          n_octaves;
  guint          n_scales;
#ifdef HAVE_CVP_IMGPYRAMID_H
  GArray         *div2coef;
#endif // HAVE_CVP_IMGPYRAMID_H
  gboolean       is_ubwc;
};

GST_API GstImgPyramidEngine *
gst_imgpyramid_engine_new     (GstImgPyramidSettings * settings,
                               GArray * sizes);

GST_API void
gst_imgpyramid_engine_free    (GstImgPyramidEngine * engine);

GST_API gboolean
gst_imgpyramid_engine_execute (GstImgPyramidEngine * engine,
                               const GstVideoFrame * inframe,
                               GstBufferList * outbuffers);

G_END_DECLS

#endif /* __GST_IMGPYRAMID_ENGINE_H__ */