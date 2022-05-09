/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QTI_OVERLAY_H__
#define __GST_QTI_OVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#ifdef USE_C2D_CONVERTER
#include <gst/video/c2d-video-converter.h>
#endif // USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
#include <gst/video/gles-video-converter.h>
#endif // USE_GLES_CONVERTER

#include "overlayutils.h"

G_BEGIN_DECLS

#define GST_TYPE_OVERLAY (gst_overlay_get_type())
#define GST_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OVERLAY, GstVOverlay))
#define GST_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OVERLAY, GstVOverlayClass))
#define GST_IS_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OVERLAY))
#define GST_IS_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OVERLAY))
#define GST_OVERLAY_CAST(obj) ((GstOverlay *)(obj))

typedef struct _GstVOverlay GstVOverlay;
typedef struct _GstVOverlayClass GstVOverlayClass;

struct _GstVOverlay {
  GstBaseTransform     parent;

  /// Maximum latency.
  GstClockTime         latency;

  /// Video info extracted from negotiated sink/src caps.
  GstVideoInfo         *vinfo;

  /// Internal intermediary buffer pools, used for drawing image overlays.
  GstBufferPool        *ovlpools[GST_OVERLAY_TYPE_MAX];
  /// Video info for the intermediary buffers produced by the pools.
  GstVideoInfo         *ovlinfos[GST_OVERLAY_TYPE_MAX];

  /// Supported converters.
#ifdef USE_C2D_CONVERTER
  GstC2dVideoConverter *c2dconvert;
#endif // USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
  GstGlesVideoConverter *glesconvert;
#endif // USE_GLES_CONVERTER

  /// Properties.
  GArray               *bboxes;
  GArray               *timestamps;
  GArray               *strings;
  GArray               *simages;
  GArray               *masks;
};

struct _GstVOverlayClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_overlay_get_type (void);

G_END_DECLS

#endif // __GST_QTI_OVERLAY_H__
