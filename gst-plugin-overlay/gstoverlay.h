/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QTI_OVERLAY_H__
#define __GST_QTI_OVERLAY_H__

#include <gmodule.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <qmmf-sdk/qmmf_overlay.h>

using namespace qmmf::overlay;

G_BEGIN_DECLS

#define GST_TYPE_OVERLAY \
  (gst_overlay_get_type())
#define GST_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OVERLAY,GstOverlay))
#define GST_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OVERLAY,GstOverlayClass))
#define GST_IS_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OVERLAY))
#define GST_IS_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OVERLAY))
#define GST_OVERLAY_CAST(obj)       ((GstOverlay *)(obj))

typedef struct _GstOverlay GstOverlay;
typedef struct _GstOverlayClass GstOverlayClass;

struct _GstOverlay {
  GstVideoFilter      parent;
  Overlay             *overlay;
  TargetBufferFormat  format;
  GSequence           *bbox_id;
  GSequence           *simg_id;
  GSequence           *text_id;

  guint               bbox_color;
  guint               date_color;
  guint               text_color;

  guint               width;
  guint               height;
};

struct _GstOverlayClass {
  GstVideoFilterClass parent;
};

G_GNUC_INTERNAL GType gst_overlay_get_type (void);

G_END_DECLS

#endif // __GST_QTI_OVERLAY_H__
