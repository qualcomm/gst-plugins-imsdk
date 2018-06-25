/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

/*
 * Copyright (C) 2004, 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstvideoblend.h"
#include "gstvideoblendpad.h"
#include <sys/mman.h>

GST_DEBUG_CATEGORY_STATIC (gst_videoblend_debug);
#define GST_CAT_DEFAULT gst_videoblend_debug

#define GST_VIDEO_BLEND_GET_LOCK(blend)          (&GST_VIDEO_BLEND(blend)->lock)
#define GST_VIDEO_BLEND_LOCK(blend)              (g_mutex_lock(GST_VIDEO_BLEND_GET_LOCK (blend)))
#define GST_VIDEO_BLEND_UNLOCK(blend)            (g_mutex_unlock(GST_VIDEO_BLEND_GET_LOCK (blend)))
#define GST_VIDEO_BLEND_GET_SETCAPS_LOCK(blend)  (&GST_VIDEO_BLEND(blend)->setcaps_lock)
#define GST_VIDEO_BLEND_SETCAPS_LOCK(blend)      (g_mutex_lock(GST_VIDEO_BLEND_GET_SETCAPS_LOCK (blend)))
#define GST_VIDEO_BLEND_SETCAPS_UNLOCK(blend)    (g_mutex_unlock(GST_VIDEO_BLEND_GET_SETCAPS_LOCK (blend)))

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ( " { ARGB, RGBA } " ))
                                                                  );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_REQUEST,
                                                                    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ( " { ARGB, RGBA } " ))
                                                                   );

static void gst_videoblend_child_proxy_init (gpointer g_iface, gpointer iface_data);
static void gst_videoblend_release_pad (GstElement * element, GstPad * pad);
static gboolean gst_videoblend_src_setcaps (GstPad * pad, GstVideoBlend * blend, GstCaps * caps);

struct _GstVideoBlendCollect
{
    GstCollectData collect;       /* we extend the CollectData */

    GstVideoBlendPad *blendpad;

    GstBuffer *queued;            /* buffer for which we don't know the end time yet */
    GstVideoInfo queued_vinfo;

    GstBuffer *buffer;            /* buffer that should be blended now */
    GstVideoInfo buffer_vinfo;

    GstClockTime start_time;
    GstClockTime end_time;
};

#define DEFAULT_PAD_TYPE   0
#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0

enum
{
    PROP_PAD_0,
    PROP_PAD_TYPE,
    PROP_PAD_XPOS,
    PROP_PAD_YPOS,
};

G_DEFINE_TYPE (GstVideoBlendPad, gst_videoblend_pad, GST_TYPE_PAD);

static void
gst_videoblend_collect_free (GstCollectData * data)
{
    GstVideoBlendCollect *cdata = (GstVideoBlendCollect *) data;

    gst_buffer_replace (&cdata->buffer, NULL);
}

static gboolean
gst_videoblend_init_gbm_buffers (GstVideoBlend * blend)
{
    GSList *l;
    c2d_blend *c2d = blend->c2d;

    GST_VIDEO_BLEND_LOCK (blend);

    for (l = blend->sinkpads; l; l = l->next)
    {
        GstVideoBlendPad *bpad = (GstVideoBlendPad *)l->data;
        GstVideoFormat pad_format = GST_VIDEO_INFO_FORMAT (&bpad->info);
        gint width, height;

        width = GST_VIDEO_INFO_WIDTH (&bpad->info);
        height = GST_VIDEO_INFO_HEIGHT (&bpad->info);

        if (width == 0 || height == 0)
            continue;

        bpad->c2d_buffer.width = width;
        bpad->c2d_buffer.height = height;

        switch (pad_format)
        {
        case GST_VIDEO_FORMAT_NV12:
            {
                bpad->c2d_buffer.gbm_format = GBM_FORMAT_NV12;
                GST_DEBUG_OBJECT (blend, "%p NV12 input", bpad);
                break;
            }
        case GST_VIDEO_FORMAT_ARGB:
            {
                bpad->c2d_buffer.gbm_format = GBM_FORMAT_ARGB8888;
                GST_DEBUG_OBJECT (blend, "%p ARGB input", bpad);
                break;
            }
        case GST_VIDEO_FORMAT_RGBA:
            {
                bpad->c2d_buffer.gbm_format = GBM_FORMAT_RGBA8888;
                GST_DEBUG_OBJECT (blend, "%p RGBA input", bpad);
                break;
            }
        default:
            {
                GST_ERROR_OBJECT (blend, "unsupport format %s", gst_video_format_to_string (pad_format));
                return FALSE;
            }
        }

        if (bpad->c2d_buffer.fd > 0)
        {
            c2d->FreeBuffer(&bpad->c2d_buffer);
            GST_DEBUG_OBJECT (blend, "%p Free c2d buffer done!", bpad);
        }

        if (bpad->type == 0)
        {
            if (!c2d->AllocateBuffer(C2D_OUTPUT, &bpad->c2d_buffer))
            {
                GST_DEBUG_OBJECT (blend, "Alocate c2d output buffer failed!");
            }
            GST_DEBUG_OBJECT (blend, "%p Alocate c2d output buffer done!", bpad);
        }
        else
        {
            if (!c2d->AllocateBuffer(C2D_INPUT, &bpad->c2d_buffer))
            {
                GST_DEBUG_OBJECT (blend, "Alocate c2d input buffer failed!");
            }
            GST_DEBUG_OBJECT (blend, "%p Alocate c2d input buffer done!", bpad);
        }

        GST_DEBUG_OBJECT (blend, "fd %d, size %d, ptr: %p, pitch: %d", bpad->c2d_buffer.fd, bpad->c2d_buffer.size, bpad->c2d_buffer.ptr, bpad->c2d_buffer.pitch);
    }

    GST_VIDEO_BLEND_UNLOCK (blend);

    return TRUE;
}

static gboolean
gst_videoblend_update_src_caps (GstVideoBlend * blend)
{
    GSList *tmp;
    GstVideoBlendPad *pad;
    gboolean ret = TRUE;
    GstCaps *caps;

    GST_VIDEO_BLEND_SETCAPS_LOCK (blend);

    /* Then browse the sinks once more, setting or unsetting conversion if needed */
    for (tmp = blend->sinkpads; tmp; tmp = tmp->next)
    {
        pad = (GstVideoBlendPad *)tmp->data;

        GST_INFO_OBJECT (pad, "0x%p, pad-x: %d, pad->y: %d, pad->type: %d", pad, pad->xpos, pad->ypos, pad->type);

        if (!pad->info.finfo)
            continue;

        if (GST_VIDEO_INFO_FORMAT (&pad->info) == GST_VIDEO_FORMAT_UNKNOWN)
            continue;

        if (pad->type == 0)
        {
            if ((pad->xpos != 0) || (pad->ypos != 0))
            {
                GST_ELEMENT_ERROR (blend, RESOURCE, SETTINGS, ("Invalid settings for sink_0 position"), ("Background stream position should be (0, 0)"));
                return FALSE;
            }
            blend->info = pad->info;
        }
    }

    if (blend->info.finfo && (GST_VIDEO_INFO_FORMAT (&blend->info) != GST_VIDEO_FORMAT_UNKNOWN))
    {
        caps = gst_video_info_to_caps (&blend->info);

        GST_DEBUG_OBJECT (blend, "set src caps: %" GST_PTR_FORMAT, caps);
        ret = gst_videoblend_src_setcaps (blend->srcpad, blend, caps);
        gst_caps_unref (caps);
    }

    GST_VIDEO_BLEND_SETCAPS_UNLOCK (blend);

    return ret;
}

static gboolean
gst_videoblend_pad_sink_setcaps (GstPad * pad, GstObject * parent, GstCaps * caps)
{
    GstVideoBlend *blend;
    GstVideoBlendPad *blendpad;
    GstVideoInfo info;
    gboolean ret = FALSE;

    GST_INFO_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

    blend = GST_VIDEO_BLEND (parent);
    blendpad = GST_VIDEO_BLEND_PAD (pad);

    if (!gst_video_info_from_caps (&info, caps))
    {
        GST_ERROR_OBJECT (pad, "Failed to parse caps");
        goto beach;
    }

    GST_VIDEO_BLEND_LOCK (blend);
    if (GST_VIDEO_INFO_FORMAT (&blend->info) != GST_VIDEO_FORMAT_UNKNOWN)
    {
        if (GST_VIDEO_INFO_PAR_N (&blend->info) != GST_VIDEO_INFO_PAR_N (&info) || GST_VIDEO_INFO_PAR_D (&blend->info) != GST_VIDEO_INFO_PAR_D (&info) || GST_VIDEO_INFO_INTERLACE_MODE (&blend->info) != GST_VIDEO_INFO_INTERLACE_MODE (&info))
        {
            GST_DEBUG_OBJECT (pad, "got input caps %" GST_PTR_FORMAT ", but " "current caps are %"GST_PTR_FORMAT, caps, blend->current_caps);
            GST_VIDEO_BLEND_UNLOCK (blend);
            return FALSE;
        }
    }

    blendpad->info = info;

    GST_COLLECT_PADS_STREAM_LOCK (blend->collect);

    GST_VIDEO_BLEND_UNLOCK (blend);

    if (gst_videoblend_update_src_caps (blend) && blend->c2d_loaded)
        ret = gst_videoblend_init_gbm_buffers (blend);

    GST_COLLECT_PADS_STREAM_UNLOCK (blend->collect);

beach:
    return ret;
}

static GstCaps *
gst_videoblend_pad_sink_getcaps (GstPad * pad, GstVideoBlend * blend, GstCaps * filter)
{
    GstCaps *srccaps;
    GstCaps *template_caps;
    GstCaps *filtered_caps;
    GstCaps *returned_caps;
    gboolean had_current_caps = TRUE;

    template_caps = gst_pad_get_pad_template_caps (GST_PAD (blend->srcpad));

    srccaps = gst_pad_get_current_caps (GST_PAD (blend->srcpad));
    if (srccaps == NULL)
    {
        had_current_caps = FALSE;
        srccaps = template_caps;
    }

    srccaps = gst_caps_make_writable (srccaps);

    filtered_caps = srccaps;
    if (filter)
        filtered_caps = gst_caps_intersect (srccaps, filter);
    returned_caps = gst_caps_intersect (filtered_caps, template_caps);

    gst_caps_unref (srccaps);
    if (filter)
        gst_caps_unref (filtered_caps);
    if (had_current_caps)
        gst_caps_unref (template_caps);

    return returned_caps;
}

static gboolean
gst_videoblend_pad_sink_acceptcaps (GstPad * pad, GstVideoBlend * blend, GstCaps * caps)
{
    gboolean ret;
    GstCaps *modified_caps;
    GstCaps *accepted_caps;
    GstCaps *template_caps;
    gboolean had_current_caps = TRUE;
    gint i, n;
    GstStructure *s;

    GST_DEBUG_OBJECT (pad, "%" GST_PTR_FORMAT, caps);

    accepted_caps = gst_pad_get_current_caps (GST_PAD (blend->srcpad));

    template_caps = gst_pad_get_pad_template_caps (GST_PAD (blend->srcpad));

    if (accepted_caps == NULL)
    {
        accepted_caps = template_caps;
        had_current_caps = FALSE;
    }

    accepted_caps = gst_caps_make_writable (accepted_caps);

    GST_LOG_OBJECT (pad, "src caps %" GST_PTR_FORMAT, accepted_caps);

    n = gst_caps_get_size (accepted_caps);
    for (i = 0; i < n; i++)
    {
        s = gst_caps_get_structure (accepted_caps, i);
        gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
        if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
            gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

        gst_structure_remove_fields (s, "colorimetry", "chroma-site", "format", NULL);
    }

    modified_caps = gst_caps_intersect (accepted_caps, template_caps);

    ret = gst_caps_can_intersect (caps, accepted_caps);
    GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT, (ret ? "" : "not "), caps);
    GST_DEBUG_OBJECT (pad, "acceptable caps are %" GST_PTR_FORMAT, accepted_caps);
    gst_caps_unref (accepted_caps);
    gst_caps_unref (modified_caps);
    if (had_current_caps)
        gst_caps_unref (template_caps);
    return ret;
}

static gboolean
gst_videoblend_sink_query (GstCollectPads * pads, GstCollectData * cdata, GstQuery * query, GstVideoBlend * blend)
{
    GstVideoBlendPad *pad = GST_VIDEO_BLEND_PAD (cdata->pad);
    gboolean ret = FALSE;

    switch (GST_QUERY_TYPE (query))
    {
    case GST_QUERY_CAPS:
        {
            GstCaps *filter, *caps;

            gst_query_parse_caps (query, &filter);
            caps = gst_videoblend_pad_sink_getcaps (GST_PAD (pad), blend, filter);
            gst_query_set_caps_result (query, caps);
            gst_caps_unref (caps);
            ret = TRUE;
            break;
        }
    case GST_QUERY_ACCEPT_CAPS:
        {
            GstCaps *caps;

            gst_query_parse_accept_caps (query, &caps);
            ret = gst_videoblend_pad_sink_acceptcaps (GST_PAD (pad), blend, caps);
            gst_query_set_accept_caps_result (query, ret);
            ret = TRUE;
            break;
        }
    default:
        ret = gst_collect_pads_query_default (pads, cdata, query, FALSE);
        break;
    }
    return ret;
}

static void
gst_videoblend_pad_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
    GstVideoBlendPad *pad = GST_VIDEO_BLEND_PAD (object);

    switch (prop_id)
    {
    case PROP_PAD_TYPE:
        g_value_set_uint (value, pad->type);
        break;
    case PROP_PAD_XPOS:
        g_value_set_int (value, pad->xpos);
        break;
    case PROP_PAD_YPOS:
        g_value_set_int (value, pad->ypos);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_videoblend_pad_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
    GstVideoBlendPad *pad = GST_VIDEO_BLEND_PAD (object);
    GstVideoBlend *blend = GST_VIDEO_BLEND (gst_pad_get_parent (GST_PAD (pad)));

    switch (prop_id)
    {
    case PROP_PAD_XPOS:
        pad->xpos = g_value_get_int (value);
        break;
    case PROP_PAD_YPOS:
        pad->ypos = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }

    gst_object_unref (blend);
}

static void
gst_videoblend_pad_class_init (GstVideoBlendPadClass * klass)
{
    GObjectClass *gobject_class = (GObjectClass *) klass;

    gobject_class->set_property = gst_videoblend_pad_set_property;
    gobject_class->get_property = gst_videoblend_pad_get_property;

    g_object_class_install_property (gobject_class, PROP_PAD_TYPE,
                                     g_param_spec_uint ("type", "Background or Foreground", "0 for background video and 1 for foreground video",
                                                        0, 1, DEFAULT_PAD_TYPE,
                                                        (GParamFlags)(G_PARAM_READABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
                                     g_param_spec_int ("xpos", "X Position", "X Position of the picture",
                                                       G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
                                                       (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
                                     g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
                                                       G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
                                                       (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_videoblend_pad_init (GstVideoBlendPad * blendpad)
{
    blendpad->type = DEFAULT_PAD_TYPE;
    blendpad->xpos = DEFAULT_PAD_XPOS;
    blendpad->ypos = DEFAULT_PAD_YPOS;
}

/* GstVideoBlend */
#define gst_videoblend_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVideoBlend, gst_videoblend, GST_TYPE_ELEMENT,
                         G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY, gst_videoblend_child_proxy_init));

static void
gst_videoblend_reset (GstVideoBlend * blend)
{
    GSList *l;

    gst_video_info_init (&blend->info);
    blend->nframes = 0;

    gst_segment_init (&blend->segment, GST_FORMAT_TIME);
    blend->segment.position = -1;

    for (l = blend->sinkpads; l; l = l->next)
    {
        GstVideoBlendPad *p = (GstVideoBlendPad *)l->data;
        GstVideoBlendCollect *blendcol = p->blendcol;

        gst_buffer_replace (&blendcol->buffer, NULL);
        blendcol->start_time = -1;
        blendcol->end_time = -1;

        gst_video_info_init (&p->info);
    }

    blend->newseg_pending = TRUE;
}

/*  1 == OK
 *  0 == need more data
 * -1 == EOS
 * -2 == error
 */
static gint
gst_videoblend_fill_queues (GstVideoBlend * blend, GstClockTime output_start_time, GstClockTime output_end_time)
{
    GSList *l;
    gboolean eos = TRUE;
    gboolean need_more_data = FALSE;

    for (l = blend->sinkpads; l; l = l->next)
    {
        GstVideoBlendPad *pad = (GstVideoBlendPad *)l->data;
        GstVideoBlendCollect *blendcol = pad->blendcol;
        GstSegment *segment = &pad->blendcol->collect.segment;
        GstBuffer *buf;
        GstVideoInfo *vinfo;

        buf = gst_collect_pads_peek (blend->collect, &blendcol->collect);
        if (buf)
        {
            GstClockTime start_time, end_time;

            start_time = GST_BUFFER_TIMESTAMP (buf);
            if (start_time == GST_CLOCK_TIME_NONE)
            {
                gst_buffer_unref (buf);
                GST_ERROR_OBJECT (pad, "Need timestamped buffers!");
                return -2;
            }

            vinfo = &pad->info;

            if ((blendcol->buffer && start_time < GST_BUFFER_TIMESTAMP (blendcol->buffer)) || (blendcol->queued && start_time < GST_BUFFER_TIMESTAMP (blendcol->queued)))
            {
                GST_WARNING_OBJECT (pad, "Buffer from the past, dropping");
                gst_buffer_unref (buf);
                buf = gst_collect_pads_pop (blend->collect, &blendcol->collect);
                gst_buffer_unref (buf);
                need_more_data = TRUE;
                continue;
            }

            if (blendcol->queued)
            {
                end_time = start_time - GST_BUFFER_TIMESTAMP (blendcol->queued);
                start_time = GST_BUFFER_TIMESTAMP (blendcol->queued);
                gst_buffer_unref (buf);
                buf = gst_buffer_ref (blendcol->queued);
                vinfo = &blendcol->queued_vinfo;
            }
            else
            {
                end_time = GST_BUFFER_DURATION (buf);

                if (end_time == GST_CLOCK_TIME_NONE)
                {
                    blendcol->queued = buf;
                    buf = gst_collect_pads_pop (blend->collect, &blendcol->collect);
                    gst_buffer_unref (buf);
                    blendcol->queued_vinfo = pad->info;
                    need_more_data = TRUE;
                    continue;
                }
            }

            g_assert (start_time != GST_CLOCK_TIME_NONE && end_time != GST_CLOCK_TIME_NONE);
            end_time += start_time;   /* convert from duration to position */

            /* Check if it's inside the segment */
            if (start_time >= segment->stop || end_time < segment->start)
            {
                GST_DEBUG_OBJECT (pad, "Buffer outside the segment");

                if (buf == blendcol->queued)
                {
                    gst_buffer_unref (buf);
                    gst_buffer_replace (&blendcol->queued, NULL);
                }
                else
                {
                    gst_buffer_unref (buf);
                    buf = gst_collect_pads_pop (blend->collect, &blendcol->collect);
                    gst_buffer_unref (buf);
                }

                need_more_data = TRUE;
                continue;
            }

            /* Clip to segment and convert to running time */
            start_time = MAX (start_time, segment->start);
            if (segment->stop != GST_CLOCK_TIME_NONE)
                end_time = MIN (end_time, segment->stop);
            start_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME, start_time);
            end_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME, end_time);
            g_assert (start_time != GST_CLOCK_TIME_NONE && end_time != GST_CLOCK_TIME_NONE);

            if (blendcol->end_time != GST_CLOCK_TIME_NONE && blendcol->end_time > end_time)
            {
                GST_DEBUG_OBJECT (pad, "Buffer from the past, dropping");
                if (buf == blendcol->queued)
                {
                    gst_buffer_unref (buf);
                    gst_buffer_replace (&blendcol->queued, NULL);
                }
                else
                {
                    gst_buffer_unref (buf);
                    buf = gst_collect_pads_pop (blend->collect, &blendcol->collect);
                    gst_buffer_unref (buf);
                }

                need_more_data = TRUE;
                continue;
            }

            if (end_time >= output_start_time && start_time < output_end_time)
            {
                GST_DEBUG_OBJECT (pad, "Taking new buffer with start time %" GST_TIME_FORMAT, GST_TIME_ARGS (start_time));
                gst_buffer_replace (&blendcol->buffer, buf);
                blendcol->buffer_vinfo = *vinfo;
                blendcol->start_time = start_time;
                blendcol->end_time = end_time;

                if (buf == blendcol->queued)
                {
                    gst_buffer_unref (buf);
                    gst_buffer_replace (&blendcol->queued, NULL);
                }
                else
                {
                    gst_buffer_unref (buf);
                    buf = gst_collect_pads_pop (blend->collect, &blendcol->collect);
                    gst_buffer_unref (buf);
                }
                eos = FALSE;
            }
            else
                if (start_time >= output_end_time)
                {
                    GST_DEBUG_OBJECT (pad, "Keeping buffer until %" GST_TIME_FORMAT, GST_TIME_ARGS (start_time));
                    gst_buffer_unref (buf);
                    eos = FALSE;
                }
                else
                {
                    GST_DEBUG_OBJECT (pad, "Too old buffer -- dropping");
                    if (buf == blendcol->queued)
                    {
                        gst_buffer_unref (buf);
                        gst_buffer_replace (&blendcol->queued, NULL);
                    }
                    else
                    {
                        gst_buffer_unref (buf);
                        buf = gst_collect_pads_pop (blend->collect, &blendcol->collect);
                        gst_buffer_unref (buf);
                    }

                    need_more_data = TRUE;
                    continue;
                }
        }
        else
        {
            if (blendcol->end_time != GST_CLOCK_TIME_NONE)
            {
                if (blendcol->end_time <= output_start_time)
                {
                    gst_buffer_replace (&blendcol->buffer, NULL);
                    blendcol->start_time = blendcol->end_time = -1;
                    if (!GST_COLLECT_PADS_STATE_IS_SET (blendcol, GST_COLLECT_PADS_STATE_EOS))
                        need_more_data = TRUE;
                }
                else
                    if (!GST_COLLECT_PADS_STATE_IS_SET (blendcol, GST_COLLECT_PADS_STATE_EOS))
                    {
                        eos = FALSE;
                    }
            }
        }
        if (eos && pad->type == 0)
            return -1;
    }

    if (need_more_data)
        return 0;

    return 1;
}

static gboolean
gst_videoblend_do_buffer_copy (GstVideoBlend * blend, GstVideoBlendPad * pad)
{
    GstVideoFormat pad_format = GST_VIDEO_INFO_FORMAT (&pad->info);
    guint width = GST_VIDEO_INFO_WIDTH (&pad->info);
    guint height = GST_VIDEO_INFO_HEIGHT (&pad->info);
    guint stride;
    guint h;
    GstMapInfo map_source;
    GstVideoBlendCollect *blendcol = pad->blendcol;

    GST_DEBUG_OBJECT (blend, "format %d (%dx%d)", pad_format, width, height);
    gst_buffer_map (blendcol->buffer, &map_source, GST_MAP_READ);

    switch (pad_format)
    {
    case GST_VIDEO_FORMAT_NV12:
        {
            guint stride_w = pad->c2d_buffer.pitch;
            guint stride_h = ALIGN(height, ALIGN32);
            for (h = 0; h < height; h++)
            {
                memcpy ((guint8 *) pad->c2d_buffer.ptr + h * stride_w, map_source.data + h * width, width);
            }
            for (h = 0; h < height/2; h++)
            {
                memcpy ((guint8 *) pad->c2d_buffer.ptr + stride_w * stride_h + h * stride_w, map_source.data + width * height + h * width, width);
            }
            break;
        }
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_RGBA:
        {
            stride = ALIGN(4 * width, ALIGN128);
            for (h = 0; h < height; h++)
            {
                memcpy ((guint8 *) pad->c2d_buffer.ptr + h * stride, map_source.data + h * 4 * width, 4 * width);
            }
            break;
        }
    default:
        {
            GST_ERROR_OBJECT (blend, "unsupport format %s", gst_video_format_to_string (pad_format));
            gst_buffer_unmap (blendcol->buffer, &map_source);
            return FALSE;
        }
    }
    gst_buffer_unmap (blendcol->buffer, &map_source);
    return TRUE;
}

static GstFlowReturn
gst_videoblend_blend_buffers (GstVideoBlend * blend, GstClockTime output_start_time, GstClockTime output_end_time, GstBuffer ** outbuf)
{
    GSList *l;
    GstVideoFormat pad_format;
    gint target_width, target_height, source_width, source_height;
    ColorConvertFormat target_format, source_format;
    c2d_blend *c2d = blend->c2d;
    GstIonBufFdMeta *meta;
    gint ion_fd = 0;
    guint ion_offset = 0, ion_size = 0;
    void *ion_ptr = NULL;

    /* default to blending */
    for (l = blend->sinkpads; l; l = l->next)
    {
        GstVideoBlendPad *pad = (GstVideoBlendPad *)l->data;
        GstVideoBlendCollect *blendcol = pad->blendcol;

        if (blend->c2d_loaded)
            GST_DEBUG_OBJECT (blend, "fd %d, size %d, ptr: %p, pitch: %d", pad->c2d_buffer.fd, pad->c2d_buffer.size, pad->c2d_buffer.ptr, pad->c2d_buffer.pitch);

        if (blendcol->buffer != NULL)
        {
            GstClockTime timestamp;
            gint64 stream_time;
            GstSegment *seg;

            GST_DEBUG_OBJECT(blend, "video_frame %p, pos (%d, %d), size (%dx%d), type %d", blendcol->buffer, pad->xpos, pad->ypos, GST_VIDEO_INFO_WIDTH(&pad->info), GST_VIDEO_INFO_HEIGHT(&pad->info), pad->type);
            meta = gst_buffer_get_ionfd_meta (blendcol->buffer);

            if (pad->type == 0)
            {
                gst_buffer_replace(outbuf, blendcol->buffer);
                if (meta)
                {
                    blend->meta.fd = meta->fd;
                    blend->meta.offset = meta->offset;
                    blend->meta.size = meta->size;
                    blend->meta.meta_fd = meta->meta_fd;
                    blend->info = pad->info;
                }
                else
                {
                    GST_ELEMENT_ERROR (blend, STREAM, WRONG_TYPE, ("No meta found"), ("Buffer of background stream should be allocated by GBM"));
                }

                if (blend->c2d_loaded)
		{
                    if (pad->c2d_buffer.fd)
                    {
                        c2d->FreeBuffer(&pad->c2d_buffer);
                    }
		}
		else
                    goto beach;
            }
            else
            {
                if (blend->meta.fd <= 0)
                    goto beach;

                target_width =  GST_VIDEO_INFO_WIDTH (&blend->info);
                target_height = GST_VIDEO_INFO_HEIGHT (&blend->info);
                source_width =  GST_VIDEO_INFO_WIDTH (&pad->info);
                source_height = GST_VIDEO_INFO_HEIGHT (&pad->info);

                pad_format = GST_VIDEO_INFO_FORMAT (&blend->info);
                switch (pad_format)
                {
                case GST_VIDEO_FORMAT_NV12:
                    {
                        target_format = NV12_128m;
                        GST_DEBUG_OBJECT (blend, "NV12 target");
                        break;
                    }
                case GST_VIDEO_FORMAT_ARGB:
                    {
                        target_format = ARGB8888;
                        GST_DEBUG_OBJECT (blend, "ARGB target");
                        break;
                    }
                case GST_VIDEO_FORMAT_RGBA:
                    {
                        target_format = RGBA8888;
                        GST_DEBUG_OBJECT (blend, "RGBA target");
                        break;
                    }
                default:
                    {
                        GST_ERROR_OBJECT (blend, "unsupport format %s", gst_video_format_to_string (pad_format));
                        return GST_FLOW_ERROR;
                    }
                }

                pad_format = GST_VIDEO_INFO_FORMAT (&pad->info);
                switch (pad_format)
                {
                case GST_VIDEO_FORMAT_NV12:
                    {
                        source_format = NV12_128m;
                        GST_DEBUG_OBJECT (blend, "NV12 source");
                        break;
                    }
                case GST_VIDEO_FORMAT_ARGB:
                    {
                        if (target_format == NV12_128m)
                            source_format = ARGB8888_NO_PREMULTIPLIED;
                        else
                            source_format = ARGB8888;
                        GST_DEBUG_OBJECT (blend, "ARGB source");
                        break;
                    }
                case GST_VIDEO_FORMAT_RGBA:
                    {
                        if (target_format == NV12_128m)
                            source_format = RGBA8888_NO_PREMULTIPLIED;
                        else
                            source_format = RGBA8888;
                        GST_DEBUG_OBJECT (blend, "RGBA source");
                        break;
                    }
                default:
                    {
                        GST_ERROR_OBJECT (blend, "unsupport format %s", gst_video_format_to_string (pad_format));
                        return GST_FLOW_ERROR;
                    }
                }

                if((pad->xpos + source_width <= 0) || (pad->xpos >= target_width) || (pad->ypos + source_height <= 0) || (pad->ypos >=target_height))
                    goto beach;

                if (blend->update_blend)
                {
                    c2d->Blend(pad->xpos, pad->ypos, source_width, source_height, target_width, target_height, source_format, target_format);
                    blend->update_blend = FALSE;
                }

                blend->meta_ptr = mmap(NULL, blend->meta.size, PROT_READ|PROT_WRITE, MAP_SHARED, blend->meta.fd, blend->meta.offset);
                if (meta)
                {
                    if (pad->c2d_buffer.fd)
                    {
                        c2d->FreeBuffer(&pad->c2d_buffer);
                    }

                    ion_fd = meta->fd;
                    ion_size = meta->size;
                    ion_offset = meta->offset;
                    ion_ptr = mmap(NULL, ion_size, PROT_READ|PROT_WRITE, MAP_SHARED, ion_fd, ion_offset);
                    GST_DEBUG_OBJECT(blend, "fd %d, size %d, offset %d, ptr %p", ion_fd, ion_size, ion_offset, ion_ptr);
                    if (!c2d->Convert (ion_fd, ion_ptr, ion_ptr, blend->meta.fd, blend->meta_ptr, blend->meta_ptr))
                    {
                        GST_ERROR_OBJECT (blend, "conversion failed");
                        goto exit1;
                    }
                }
                else
                {
                    GST_DEBUG_OBJECT(blend, "NO GBM buffer, do copy");
                    gst_videoblend_do_buffer_copy (blend, pad);
                    if (!c2d->Convert (pad->c2d_buffer.fd, pad->c2d_buffer.ptr, pad->c2d_buffer.ptr, blend->meta.fd, blend->meta_ptr, blend->meta_ptr))
                    {
                        GST_ERROR_OBJECT (blend, "conversion failed");
                        goto exit2;
                    }
                }
            }

            seg = &blendcol->collect.segment;

            timestamp = GST_BUFFER_TIMESTAMP (blendcol->buffer);

            stream_time =  gst_segment_to_stream_time (seg, GST_FORMAT_TIME, timestamp);

            /* sync object properties on stream time */
            if (GST_CLOCK_TIME_IS_VALID (stream_time))
                gst_object_sync_values (GST_OBJECT (pad), stream_time);
        }
    }

exit1:
    if (ion_ptr)
        munmap(ion_ptr, ion_size);

exit2:
    if (blend->meta_ptr)
        munmap(blend->meta_ptr, blend->meta.size);

beach:
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_videoblend_collected (GstCollectPads * pads, GstVideoBlend * blend)
{
    GstFlowReturn ret;
    GstClockTime output_start_time, output_end_time;
    GstBuffer *outbuf = NULL;
    gint res;

    /* If we're not negotiated yet... */
    if (GST_VIDEO_INFO_FORMAT (&blend->info) == GST_VIDEO_FORMAT_UNKNOWN)
        return GST_FLOW_NOT_NEGOTIATED;

    if (blend->send_stream_start)
    {
        gchar s_id[32];

        /* stream-start (FIXME: create id based on input ids) */
        g_snprintf (s_id, sizeof (s_id), "blend-%08x", g_random_int ());
        if (!gst_pad_push_event (blend->srcpad, gst_event_new_stream_start (s_id)))
        {
            GST_WARNING_OBJECT (blend->srcpad, "Sending stream start event failed");
        }
        blend->send_stream_start = FALSE;
    }

    if (gst_pad_check_reconfigure (blend->srcpad))
        if (gst_videoblend_update_src_caps (blend) && blend->c2d_loaded)
            gst_videoblend_init_gbm_buffers(blend);

    if (blend->send_caps)
    {
        if (!gst_pad_push_event (blend->srcpad, gst_event_new_caps (blend->current_caps)))
        {
            GST_WARNING_OBJECT (blend->srcpad, "Sending caps event failed");
        }
        blend->send_caps = FALSE;
    }

    GST_VIDEO_BLEND_LOCK (blend);

    if (blend->newseg_pending)
    {
        GST_DEBUG_OBJECT (blend, "Sending NEWSEGMENT event");
        GST_VIDEO_BLEND_UNLOCK (blend);
        if (!gst_pad_push_event (blend->srcpad, gst_event_new_segment (&blend->segment)))
        {
            ret = GST_FLOW_ERROR;
            goto done_unlocked;
        }
        GST_VIDEO_BLEND_LOCK (blend);
        blend->newseg_pending = FALSE;
    }

    if (blend->segment.position == GST_CLOCK_TIME_NONE)
        output_start_time = blend->segment.start;
    else
        output_start_time = blend->segment.position;

    output_end_time = gst_util_uint64_scale_round (blend->nframes + 1, GST_SECOND * GST_VIDEO_INFO_FPS_D (&blend->info), GST_VIDEO_INFO_FPS_N (&blend->info)) + blend->segment.start;

    if (output_end_time >= blend->segment.stop)
    {
        GST_DEBUG_OBJECT (blend, "Segment done");
        if (!(blend->segment.flags & GST_SEGMENT_FLAG_SEGMENT))
        {
            GST_VIDEO_BLEND_UNLOCK (blend);
            gst_pad_push_event (blend->srcpad, gst_event_new_eos ());

            ret = GST_FLOW_EOS;
            goto done_unlocked;
        }
    }

    if (blend->segment.stop != GST_CLOCK_TIME_NONE)
        output_end_time = MIN (output_end_time, blend->segment.stop);

    res = gst_videoblend_fill_queues (blend, output_start_time, output_end_time);

    if (res == 0)
    {
        GST_DEBUG_OBJECT (blend, "Need more data for decisions");
        ret = GST_FLOW_OK;
        goto done;
    }
    else if (res == -1)
    {
        GST_VIDEO_BLEND_UNLOCK (blend);
        GST_DEBUG_OBJECT (blend, "Sinkpad 0 receives EOS -- forwarding");
        gst_pad_push_event (blend->srcpad, gst_event_new_eos ());
        ret = GST_FLOW_EOS;
        goto done_unlocked;
    }
    else if (res == -2)
    {
        GST_ERROR_OBJECT (blend, "Error collecting buffers");
        ret = GST_FLOW_ERROR;
        goto done;
    }

    ret = gst_videoblend_blend_buffers (blend, output_start_time, output_end_time, &outbuf);

    blend->segment.position = output_end_time;
    blend->nframes++;

    GST_VIDEO_BLEND_UNLOCK (blend);
    if (outbuf)
    {
        GST_LOG_OBJECT (blend, "Pushing buffer with ts %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)), GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
        ret = gst_pad_push (blend->srcpad, outbuf);
    }
    goto done_unlocked;

done:
    GST_VIDEO_BLEND_UNLOCK (blend);

done_unlocked:
    return ret;
}

static gboolean
gst_videoblend_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstVideoBlend *blend = GST_VIDEO_BLEND (parent);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, format,
              gst_segment_to_stream_time (&blend->segment, GST_FORMAT_TIME,
                  blend->segment.position));
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_CAPS:
      res = gst_pad_query_default (pad, parent, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_videoblend_src_setcaps (GstPad * pad, GstVideoBlend * blend, GstCaps * caps)
{
    gboolean ret = TRUE;
    if (blend->current_caps == NULL || gst_caps_is_equal (caps, blend->current_caps) == FALSE)
    {
        gst_caps_replace (&blend->current_caps, caps);
        blend->send_caps = TRUE;
    }

    return ret;
}

static gboolean
gst_videoblend_sink_event (GstCollectPads * pads, GstCollectData * cdata, GstEvent * event, GstVideoBlend * blend)
{
    GstVideoBlendPad *pad = GST_VIDEO_BLEND_PAD (cdata->pad);
    gboolean ret = TRUE, discard = FALSE;

    GST_DEBUG_OBJECT (pad, "Got %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME (event), event);

    switch (GST_EVENT_TYPE (event))
    {
    case GST_EVENT_CAPS:
        {
            GstCaps *caps;

            gst_event_parse_caps (event, &caps);
            ret = gst_videoblend_pad_sink_setcaps (GST_PAD (pad), GST_OBJECT (blend), caps);
            gst_event_unref (event);
            event = NULL;
            break;
        }
    default:
        break;
    }

    if (event != NULL)
        return gst_collect_pads_event_default (pads, cdata, event, discard);

    return ret;
}

/* GstElement vmethods */
static GstStateChangeReturn
gst_videoblend_change_state (GstElement * element, GstStateChange transition)
{
    GstVideoBlend *blend = GST_VIDEO_BLEND (element);
    GstStateChangeReturn ret;

    switch (transition)
    {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        blend->send_stream_start = TRUE;
        blend->send_caps = TRUE;
        gst_segment_init (&blend->segment, GST_FORMAT_TIME);
        gst_caps_replace (&blend->current_caps, NULL);
        GST_LOG_OBJECT (blend, "starting collectpads");
        gst_collect_pads_start (blend->collect);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        GST_LOG_OBJECT (blend, "stopping collectpads");
        gst_collect_pads_stop (blend->collect);
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_videoblend_reset (blend);
        break;
    default:
        break;
    }

    return ret;
}

static GstPad *
gst_videoblend_request_new_pad (GstElement * element, GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
    GstVideoBlend *blend;
    GstVideoBlendPad *blendpad;
    GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

    blend = GST_VIDEO_BLEND (element);

    if (blend->numpads >= 2)
    {
        GST_ERROR_OBJECT (blend, "Only accept 2 sinkpads");
        return NULL;
    }

    if (templ == gst_element_class_get_pad_template (klass, "sink_%u"))
    {
        guint serial = 0;
        gchar *name = NULL;
        GstVideoBlendCollect *blendcol = NULL;

        GST_VIDEO_BLEND_LOCK (blend);
        if (req_name == NULL || strlen (req_name) < 6 || !g_str_has_prefix (req_name, "sink_"))
        {
            /* no name given when requesting the pad, use next available int */
            serial = blend->next_sinkpad++;
        }
        else
        {
            /* parse serial number from requested padname */
            serial = g_ascii_strtoull (&req_name[5], NULL, 10);
            if (serial >= blend->next_sinkpad)
                blend->next_sinkpad = serial + 1;
        }
        /* create new pad with the name */
        name = g_strdup_printf ("sink_%u", serial);
        blendpad = (GstVideoBlendPad *)g_object_new (GST_TYPE_VIDEO_BLEND_PAD, "name", name, "direction", templ->direction, "template", templ, NULL);
        g_free (name);

        blendpad->type = blend->numpads;
        blendpad->xpos = DEFAULT_PAD_XPOS;
        blendpad->ypos = DEFAULT_PAD_YPOS;

        blendcol = (GstVideoBlendCollect *) gst_collect_pads_add_pad (blend->collect, GST_PAD (blendpad), sizeof (GstVideoBlendCollect), (GstCollectDataDestroyNotify) gst_videoblend_collect_free, TRUE);

        /* Keep track of each other */
        blendcol->blendpad = blendpad;
        blendpad->blendcol = blendcol;

        blendcol->start_time = -1;
        blendcol->end_time = -1;

        /* Keep an internal list of blendpads for type */
        blend->sinkpads = g_slist_insert (blend->sinkpads, blendpad, blendpad->type);
        blend->numpads++;
        GST_VIDEO_BLEND_UNLOCK (blend);
    }
    else
    {
        return NULL;
    }

    GST_DEBUG_OBJECT (element, "Adding pad %s", GST_PAD_NAME (blendpad));

    /* add the pad to the element */
    gst_element_add_pad (element, GST_PAD (blendpad));
    gst_child_proxy_child_added (GST_CHILD_PROXY (blend), G_OBJECT (blendpad), GST_OBJECT_NAME (blendpad));

    return GST_PAD (blendpad);
}

static void
gst_videoblend_release_pad (GstElement * element, GstPad * pad)
{
    GstVideoBlend *blend = NULL;
    GstVideoBlendPad *blendpad;
    gboolean update_caps;

    blend = GST_VIDEO_BLEND (element);

    GST_VIDEO_BLEND_LOCK (blend);
    if (G_UNLIKELY (g_slist_find (blend->sinkpads, pad) == NULL))
    {
        g_warning ("Unknown pad %s", GST_PAD_NAME (pad));
        goto error;
    }

    blendpad = GST_VIDEO_BLEND_PAD (pad);

    blend->sinkpads = g_slist_remove (blend->sinkpads, pad);
    gst_child_proxy_child_removed (GST_CHILD_PROXY (blend), G_OBJECT (blendpad), GST_OBJECT_NAME (blendpad));
    blend->numpads--;

    update_caps = GST_VIDEO_INFO_FORMAT (&blend->info) != GST_VIDEO_FORMAT_UNKNOWN;
    GST_VIDEO_BLEND_UNLOCK (blend);

    gst_collect_pads_remove_pad (blend->collect, pad);

    if (update_caps)
        gst_videoblend_update_src_caps (blend);

    gst_element_remove_pad (element, pad);
    return;
error:
    GST_VIDEO_BLEND_UNLOCK (blend);
}

/* GObject vmethods */
static void
gst_videoblend_finalize (GObject * o)
{
    GstVideoBlend *blend = GST_VIDEO_BLEND (o);

    if (blend->c2d != NULL)
    {
        delete (blend->c2d);
        blend->c2d = NULL;
        blend->c2d_loaded = FALSE;
    }

    gst_object_unref (blend->collect);
    g_mutex_clear (&blend->lock);
    g_mutex_clear (&blend->setcaps_lock);

    G_OBJECT_CLASS (parent_class)->finalize (o);
}

static void
gst_videoblend_dispose (GObject * o)
{
    GstVideoBlend *blend = GST_VIDEO_BLEND (o);
    c2d_blend *c2d = blend->c2d;
    GSList *tmp;

    if (blend->c2d_loaded)
    {
        for (tmp = blend->sinkpads; tmp; tmp = tmp->next)
        {
            GstVideoBlendPad *blendpad = (GstVideoBlendPad *)tmp->data;

            if (blendpad->c2d_buffer.fd)
            {
                c2d->FreeBuffer(&blendpad->c2d_buffer);
            }
        }
    }

    gst_caps_replace (&blend->current_caps, NULL);

    G_OBJECT_CLASS (parent_class)->dispose (o);
}

/* GstChildProxy implementation */
static GObject *
gst_videoblend_child_proxy_get_child_by_index (GstChildProxy * child_proxy, guint index)
{
    GstVideoBlend *blend = GST_VIDEO_BLEND (child_proxy);
    GObject *obj;

    GST_VIDEO_BLEND_LOCK (blend);
    if ((obj = (GObject *)g_slist_nth_data (blend->sinkpads, index)))
        g_object_ref (obj);
    GST_VIDEO_BLEND_UNLOCK (blend);
    return obj;
}

static guint
gst_videoblend_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
    guint count = 0;
    GstVideoBlend *blend = GST_VIDEO_BLEND (child_proxy);

    GST_VIDEO_BLEND_LOCK (blend);
    count = blend->numpads;
    GST_VIDEO_BLEND_UNLOCK (blend);
    GST_INFO_OBJECT (blend, "Children Count: %d", count);
    return count;
}

static void
gst_videoblend_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
    GstChildProxyInterface *iface = (GstChildProxyInterface *)g_iface;

    GST_INFO ("intializing child proxy interface");
    iface->get_child_by_index = gst_videoblend_child_proxy_get_child_by_index;
    iface->get_children_count = gst_videoblend_child_proxy_get_children_count;
}


/* GObject boilerplate */
static void
gst_videoblend_class_init (GstVideoBlendClass * klass)
{
    GObjectClass *gobject_class = (GObjectClass *) klass;
    GstElementClass *gstelement_class = (GstElementClass *) klass;

    gobject_class->finalize = gst_videoblend_finalize;
    gobject_class->dispose = gst_videoblend_dispose;

    gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_videoblend_request_new_pad);
    gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_videoblend_release_pad);
    gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_videoblend_change_state);

    gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&src_factory));
    gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&sink_factory));

    gst_element_class_set_static_metadata (gstelement_class, "Video blend",
                                           "Filter/Editor/Video/Compositor",
                                           "mix foreground video stream with background video stream", "Jingtao Chen <jingtaoc@qti.qualcomm.com>");

    /* Register the pad class */
    g_type_class_ref (GST_TYPE_VIDEO_BLEND_PAD);
}

static void
gst_videoblend_init (GstVideoBlend * blend)
{
    GstElementClass *klass = GST_ELEMENT_GET_CLASS (blend);
    c2d_blend *c2d;

    blend->srcpad = gst_pad_new_from_template (gst_element_class_get_pad_template (klass, "src"), "src");
    gst_pad_set_query_function (GST_PAD (blend->srcpad),
      GST_DEBUG_FUNCPTR (gst_videoblend_src_query));
    gst_element_add_pad (GST_ELEMENT (blend), blend->srcpad);

    blend->collect = gst_collect_pads_new ();
    blend->current_caps = NULL;
    blend->update_blend = TRUE;

    gst_collect_pads_set_function (blend->collect, (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_videoblend_collected), blend);
    gst_collect_pads_set_event_function (blend->collect, (GstCollectPadsEventFunction) gst_videoblend_sink_event, blend);
    gst_collect_pads_set_query_function (blend->collect, (GstCollectPadsQueryFunction) gst_videoblend_sink_query, blend);

    g_mutex_init (&blend->lock);
    g_mutex_init (&blend->setcaps_lock);
    /* initialize variables */
    gst_videoblend_reset (blend);

    c2d = (c2d_blend *)new c2d_blend();
    if (!c2d)
    {
        GST_ERROR_OBJECT (blend, "failed to instantiate c2d object");
        blend->c2d = NULL;
        blend->c2d_loaded = FALSE;
        return;
    }

    GST_DEBUG_OBJECT (blend, "get c2d_conv instance %p", c2d);

    if (!c2d->Init())
    {
        GST_ERROR_OBJECT (blend, "failed to initialize color converter");
        delete c2d;
        blend->c2d = NULL;
        blend->c2d_loaded = FALSE;
        return;
    }

    /* open c2d with default params */
    c2d->Open(300, 100, 800, 480, ARGB8888, NV12_128m, 0, 0);

    blend->c2d = c2d;
    blend->c2d_loaded = TRUE;
}

/* Element registration */
static gboolean
plugin_init (GstPlugin * plugin)
{
    GST_DEBUG_CATEGORY_INIT (gst_videoblend_debug, "videoblend", 0, "video blend");

    return gst_element_register (plugin, "videoblend", GST_RANK_PRIMARY, GST_TYPE_VIDEO_BLEND);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, videoblend, "Video blend", plugin_init, VERSION, GST_LICENSE, "Qualcomm Technologies Inc", "http://www.qualcomm.com")
