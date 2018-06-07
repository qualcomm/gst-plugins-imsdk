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

#ifndef __GST_VIDEO_BLEND_H__
#define __GST_VIDEO_BLEND_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "../c2d/c2d_blend.h"
#include <gst/base/gstcollectpads.h>
#include <gst/ionbuf/gstionbuf_meta.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_BLEND (gst_videoblend_get_type())
#define GST_VIDEO_BLEND(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_BLEND, GstVideoBlend))
#define GST_VIDEO_BLEND_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_BLEND, GstVideoBlendClass))
#define GST_IS_VIDEO_BLEND(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_BLEND))
#define GST_IS_VIDEO_BLEND_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_BLEND))

typedef struct _GstVideoBlend GstVideoBlend;
typedef struct _GstVideoBlendClass GstVideoBlendClass;

/**
 * GstVideoBlend:
 *
 * The opaque #GstVideoBlend structure.
 */
struct _GstVideoBlend
{
    GstElement element;

    /* < private > */
    c2d_blend *c2d;
    gboolean c2d_loaded;

    /* pad */
    GstPad *srcpad;

    /* Lock to prevent the state to change while blending */
    GMutex lock;

    /* Lock to prevent two src setcaps from happening at the same time  */
    GMutex setcaps_lock;

    /* Sink pads using Collect Pads 2*/
    GstCollectPads *collect;

    /* sinkpads, a GSList of GstVideoBlendPads */
    GSList *sinkpads;
    gint numpads;
    /* Next available sinkpad index */
    guint next_sinkpad;

    /* Output caps */
    GstVideoInfo info;

    /* Output meta */
    GstIonBufFdMeta meta;
    void *meta_ptr;
    gboolean update_blend;

    /* current caps */
    GstCaps *current_caps;
    gboolean send_caps;

    gboolean newseg_pending;

    /* Current downstream segment */
    GstSegment segment;
    guint64 nframes;

    gboolean send_stream_start;
};

struct _GstVideoBlendClass
{
    GstElementClass parent_class;
};

GType gst_videoblend_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEO_BLEND_H__ */
