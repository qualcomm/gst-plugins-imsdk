/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
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
 */

#ifndef __GST_VIDEO_BLEND_PAD_H__
#define __GST_VIDEO_BLEND_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/base/gstcollectpads.h>
#include "../c2d/c2d_blend.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_BLEND_PAD (gst_videoblend_pad_get_type())
#define GST_VIDEO_BLEND_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_BLEND_PAD, GstVideoBlendPad))
#define GST_VIDEO_BLEND_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_BLEND_PAD, GstVideoBlendPadClass))
#define GST_IS_VIDEO_BLEND_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_BLEND_PAD))
#define GST_IS_VIDEO_BLEND_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_BLEND_PAD))

typedef struct _GstVideoBlendPad GstVideoBlendPad;
typedef struct _GstVideoBlendPadClass GstVideoBlendPadClass;
typedef struct _GstVideoBlendCollect GstVideoBlendCollect;

/**
 * GstVideoBlendPad:
 *
 * The opaque #GstVideoBlendPad structure.
 */
struct _GstVideoBlendPad
{
    GstPad parent;

    /* < private > */

    /* caps */
    GstVideoInfo info;

    /* properties */
    gint xpos, ypos;
    guint type;

    /* Input C2D buffer */
    C2DBuffer c2d_buffer;

    GstVideoBlendCollect *blendcol;
};

struct _GstVideoBlendPadClass
{
    GstPadClass parent_class;
};

GType gst_videoblend_pad_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEO_BLEND_PAD_H__ */
