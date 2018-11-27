/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2006 Zaheer Merali <zaheerabbas at merali dot org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-screencapsrc
 *
 * This element captures your Display and creates raw RGBA8888 video.  It uses
 * the graphical weston screen capture module . By default it will fixate to
 * 25 frames per second.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 qscreencapsrc  num-buffers=100 ! omxh264enc ! h264parse ! qtmux ! filesink location=/usr/scrcap.mp4
 * ]| Encodes your display to a mp4 video
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstqscreencapsrc.h"

#include <string.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ionbuf/gstionbuf_meta.h>

#define GSTBUFFERNUMBER 5

GST_DEBUG_CATEGORY_STATIC (gst_debug_qscreencap_src);
#define GST_CAT_DEFAULT gst_debug_qscreencap_src
#pragma pack(1)
typedef struct
#ifdef __GNUC__
        __attribute__((ms_struct,packed))
#endif
{
    uint8_t     identsize;          // size of ID field that follows 18 byte header (0 usually)
    uint8_t     colourmaptype;      // type of colour map 0=none, 1=has palette
    uint8_t     imagetype;          // type of image 0=none,1=indexed,2=rgb,3=grey,+8=rle packed

    uint8_t     colourmapbits;      // number of bits per palette entry 15,16,24,32
    short     colourmapstart;     // first colour map entry in palette
    short     colourmaplength;    // number of colours in palette


    short     xstart;             // image x origin
    short     ystart;             // image y origin
    short     width;              // image width in pixels
    short     height;             // image height in pixels
    uint8_t     bits;               // image bits per pixel 8,16,24,32
    uint8_t     descriptor;         // image descriptor bits (vh flip bits)

    // pixel data follows header

}TGA_HEADER;
#pragma pack()
#define GST_QSCREENCAP_SRC_TEMPLATE_CAP \
    GST_VIDEO_CAPS_MAKE("RGBA")";"\
    GST_VIDEO_CAPS_MAKE("RGBA_UBWC")

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_QSCREENCAP_SRC_TEMPLATE_CAP));

enum
{
  PROP_0,
  PROP_SHOW_POINTER
};

#define gst_qscreencap_src_parent_class parent_class
G_DEFINE_TYPE (GstQScreenCapSrc, gst_qscreencap_src, GST_TYPE_PUSH_SRC);

static GstCaps *gst_qscreencap_src_fixate (GstBaseSrc * bsrc, GstCaps * caps);
static void gst_qscreencap_src_clear_bufpool (GstQScreenCapSrc * qscreencapsrc);


static gboolean
gst_qscreencap_src_return_buf (GstQScreenCapSrc * qscreencapsrc, GstBuffer * qscreencapbuf)
{
  gboolean ret;
  GstMetaQScreenCap *sm = GST_META_QSCREENCAP_GET (qscreencapbuf);
  /* True will make dispose free the buffer,
     while false will reuse  it */
  g_assert(sm != NULL);

  ret= TRUE;

  /* resolution change */
  if ((sm->width != qscreencapsrc->width) || (sm->height != qscreencapsrc->height)) {
    GST_DEBUG_OBJECT (qscreencapsrc,
        "destroy image %p as its size changed %dx%d vs current %dx%d",
        qscreencapsrc, sm->width, sm->height, qscreencapsrc->width, qscreencapsrc->height);

    g_mutex_lock (&qscreencapsrc->qc_lock);

    gst_qscreencapbuf_destroy (qscreencapsrc->qctx, qscreencapbuf);
    g_mutex_unlock (&qscreencapsrc->qc_lock);

  } else {
    GST_DEBUG_OBJECT (qscreencapsrc,"reuse and push gstbuf %p in pool", qscreencapbuf);
    gst_buffer_ref (qscreencapbuf);
    g_mutex_lock (&qscreencapsrc->buffer_lock);
    GST_BUFFER_FLAGS (GST_BUFFER (qscreencapbuf)) = 0;
    qscreencapsrc->buffer_list = g_slist_prepend (qscreencapsrc->buffer_list, qscreencapbuf);
    g_mutex_unlock (&qscreencapsrc->buffer_lock);
    ret = FALSE;
  }

  return ret;
}


static gboolean
gst_qscreencap_src_open_display (GstQScreenCapSrc * s)
{
  g_return_val_if_fail (GST_IS_QSCREENCAP_SRC (s), FALSE);

  if (s->qctx != NULL)
  {
    g_mutex_lock (&s->qc_lock);
    s->width = s->qctx->width;
    s->height = s->qctx->height;
    g_mutex_unlock (&s->qc_lock);
    return TRUE;
  }
  g_mutex_lock (&s->qc_lock);
  s->qctx = qscreencap_qctx_get (GST_ELEMENT (s));
  if (s->qctx == NULL) {
    g_mutex_unlock (&s->qc_lock);
    GST_ELEMENT_ERROR (s, RESOURCE, OPEN_READ,
        ("Could not open display for reading"),
        ("NULL returned from getting qctx"));
    return FALSE;
  }
  s->width = s->qctx->width;
  s->height = s->qctx->height;
  g_mutex_unlock (&s->qc_lock);

  if (s->qctx == NULL)
    return FALSE;

  return TRUE;
}

static gboolean
gst_qscreencap_src_start (GstBaseSrc * basesrc)
{
  GstQScreenCapSrc *s = GST_QSCREENCAP_SRC (basesrc);
  gboolean ret = FALSE;
  s->last_frame_no = -1;
  GError *err = NULL;

  ret =  gst_qscreencap_src_open_display (s);
  if(!ret)
  {
    GST_ERROR_OBJECT (s,"gst_qscreencap_src_open_display error");
    return ret;
  }
  return TRUE;
}

static gboolean
gst_qscreencap_src_stop (GstBaseSrc * basesrc)
{
  GstQScreenCapSrc *src = GST_QSCREENCAP_SRC (basesrc);
  gst_qscreencap_src_clear_bufpool (src);
  src->width = src->height = -1;
  if (src->qctx) {
      destroy_display(src->qctx->qdisplay);
      src->qctx->qdisplay = NULL;
  }
  return TRUE;
}

static gboolean
gst_qscreencap_src_unlock (GstBaseSrc * basesrc)
{
  GstQScreenCapSrc *src = GST_QSCREENCAP_SRC (basesrc);

  /* Awaken the create() func if it's waiting on the clock */
  GST_OBJECT_LOCK (src);
  if (src->clock_id) {
    GST_DEBUG_OBJECT (src, "Waking up the waiting clock");
    gst_clock_id_unschedule (src->clock_id);
  }
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static void
frame_redraw_callback (void *data, struct wl_callback *callback, uint32_t time)
{
  GstQScreenCapSrc * qscreencapsrc = data;

  GST_DEBUG_OBJECT (qscreencapsrc,"frame_redraw_cb\n");

  g_atomic_int_set (&qscreencapsrc->redraw_pending, FALSE);
  wl_callback_destroy (callback);
}

static const struct wl_callback_listener frame_callback_listener = {
  frame_redraw_callback
};

 /* get  a GstBuffer, and commit the screencap
    */
static GstBuffer *
gst_qscreencap_src_qscreencap_catch (GstQScreenCapSrc * qscreencapsrc)
{
  GstBuffer *qscreencap = NULL;
  GstMetaQScreenCap *meta;
  QDisplay *qdisplay = qscreencapsrc->qctx->qdisplay;
  struct wl_callback *callback;

  if(qscreencapsrc->qctx->gbmhandle == NULL) {
    GST_WARNING_OBJECT (qscreencapsrc,"no gbm lib for screencatch\n");
    return NULL;
  }

  g_mutex_lock (&qscreencapsrc->buffer_lock);

  while (qscreencapsrc->buffer_list != NULL) {
    qscreencap = qscreencapsrc->buffer_list->data;

    meta = GST_META_QSCREENCAP_GET (qscreencap);
    g_assert(meta != NULL);


    qscreencapsrc->buffer_list = g_slist_delete_link (qscreencapsrc->buffer_list,
        qscreencapsrc->buffer_list);

    if ((meta->width == qscreencapsrc->width) ||
        (meta->height == qscreencapsrc->height))
      break;
    GST_DEBUG_OBJECT (qscreencapsrc, "width or heigh not match free the buffer");

    gst_qscreencap_buffer_free (qscreencap);
    qscreencap = NULL;
  }
  g_mutex_unlock (&qscreencapsrc->buffer_lock);

  if (qscreencap == NULL) {

    GError *err = NULL;
    if (qdisplay->thread) {
       GST_WARNING_OBJECT (qscreencapsrc,"create more buffer\n");
    }
    g_mutex_lock (&qscreencapsrc->qc_lock);
    for (int k = 0 ; k < GSTBUFFERNUMBER ; k++  ) {

       qscreencap = gst_qscreencapbuf_new (qscreencapsrc->qctx,
          GST_ELEMENT (qscreencapsrc), qscreencapsrc->width, qscreencapsrc->height,
          (BufferReturnFunc) (gst_qscreencap_src_return_buf));
       if (qscreencap == NULL) {
         GST_ELEMENT_ERROR (qscreencapsrc, RESOURCE, WRITE, (NULL),
           ("could not create a %dx%d qscreencap", qscreencapsrc->width,
              qscreencapsrc->height));
         g_mutex_unlock (&qscreencapsrc->qc_lock);
         return NULL;
       }
       g_mutex_lock (&qscreencapsrc->buffer_lock);
       GST_BUFFER_FLAGS (GST_BUFFER (qscreencap)) = 0;
       qscreencapsrc->buffer_list = g_slist_prepend (qscreencapsrc->buffer_list, qscreencap);
       g_mutex_unlock (&qscreencapsrc->buffer_lock);
    }
    qscreencap = qscreencapsrc->buffer_list->data;
    qscreencapsrc->buffer_list = g_slist_delete_link (qscreencapsrc->buffer_list,
        qscreencapsrc->buffer_list);
    g_mutex_unlock (&qscreencapsrc->qc_lock);

    /*start the dispatch thread  since all the  buffers have been allocated*/
    if(qdisplay->thread == NULL) {
      gst_poll_set_flushing (qdisplay->wl_fd_poll, FALSE);
      qdisplay->thread = g_thread_try_new ("Gstqscreencapdisplay", gst_qscreencap_display_thread_run,
	 qdisplay, &err);
      if (err) {
         GST_ERROR("fail to g_thread_try_new");
         return NULL;
      }
    }
  }
  g_return_val_if_fail (GST_IS_QSCREENCAP_SRC (qscreencapsrc), NULL);

  meta = GST_META_QSCREENCAP_GET (qscreencap);
  g_assert(meta != NULL);

  GST_DEBUG_OBJECT (qscreencapsrc, "screen_capture_commit wlbuf %p",meta->qwlbuf.wlbuf);

  g_hash_table_add (qdisplay->buffers, qscreencap);

  wl_surface_attach(qdisplay->surface, meta->qwlbuf.wlbuf, 0, 0);

  wl_surface_damage(qdisplay->surface, 0, 0, meta->width, meta->height);

  g_atomic_int_set (&qscreencapsrc->redraw_pending, TRUE);

  callback = wl_surface_frame(qdisplay->surface);
  wl_callback_add_listener(callback, &frame_callback_listener, qscreencapsrc);

  wl_surface_commit(qdisplay->surface);


  meta->qwlbuf.busy = TRUE;
  wl_display_flush (qdisplay->display);

  GST_DEBUG_OBJECT (qscreencapsrc, "commit gstbuf %p  end",qscreencap);


  return qscreencap;
}

static void
dump_tga(char *name, uint32_t w, uint32_t h, uint32_t stride, void *data)
{
  TGA_HEADER tga        = {0};
  FILE*      f;

  tga.identsize       = 0;
  tga.colourmaptype   = 0;
  tga.imagetype       = 2;
  tga.colourmapstart  = 0;
  tga.colourmaplength = 0;
  tga.colourmapbits   = 0;
  tga.xstart          = 0;
  tga.ystart          = 0;
  tga.width           = (short)w;
  tga.height          = (short)h;
  tga.bits            = 32;
  tga.descriptor      = 32;
  f=fopen(name, "ab");
  if (f)
  {
    fwrite(&tga, sizeof(TGA_HEADER), 1, f); /* write header */
    fwrite(data, 1, stride*h, f);              /* write data  */
    fclose(f);
  }
}

static GstFlowReturn
gst_qscreencap_src_create (GstPushSrc * bs, GstBuffer ** buf)
{
  GstQScreenCapSrc *qscreencapsrc = GST_QSCREENCAP_SRC (bs);
  gint64 next_frame_no;
  GstClockTime next_screencap_ts;
  GstClockTime frame_duration;
  GstClockTime base_time;
  GstBuffer *gstbuf;
  gint32 counting;

  GST_OBJECT_LOCK (qscreencapsrc);
  if (GST_ELEMENT_CLOCK (qscreencapsrc) == NULL) {
    GST_OBJECT_UNLOCK (qscreencapsrc);
    GST_ELEMENT_ERROR (qscreencapsrc, RESOURCE, FAILED,
        ("Cannot operate without a clock"), (NULL));
    return GST_FLOW_ERROR;
  }
  GST_OBJECT_UNLOCK (qscreencapsrc);

  if (qscreencapsrc->fps_n <= 0) {
	 GST_WARNING_OBJECT (qscreencapsrc, "fps_n is illeagal");
     return GST_FLOW_NOT_NEGOTIATED;
  }

retry:
  counting = 0;
  GST_OBJECT_LOCK (qscreencapsrc);
  base_time = GST_ELEMENT_CAST (qscreencapsrc)->base_time;
  next_screencap_ts = gst_clock_get_time (GST_ELEMENT_CLOCK (qscreencapsrc));

  GST_DEBUG_OBJECT (qscreencapsrc, "timer base %" G_GUINT64_FORMAT,
        base_time);
  GST_DEBUG_OBJECT (qscreencapsrc, "timer cur %" G_GUINT64_FORMAT,
        next_screencap_ts);

  next_screencap_ts -= base_time;

  next_frame_no = gst_util_uint64_scale (next_screencap_ts,
      qscreencapsrc->fps_n, GST_SECOND * qscreencapsrc->fps_d);

  if (next_frame_no == qscreencapsrc->last_frame_no) {
    GstClockID id;
    GstClockReturn ret;

    next_frame_no += 1;

    /* next screencap timestimes */
    next_screencap_ts = gst_util_uint64_scale (next_frame_no,
        qscreencapsrc->fps_d * GST_SECOND, qscreencapsrc->fps_n);

    id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (qscreencapsrc),
        next_screencap_ts + base_time);
    qscreencapsrc->clock_id = id;

    GST_OBJECT_UNLOCK (qscreencapsrc);

    GST_DEBUG_OBJECT (qscreencapsrc, "Waiting for next capture time %" G_GUINT64_FORMAT,
        next_screencap_ts);
    ret = gst_clock_id_wait (id, NULL);
    GST_OBJECT_LOCK (qscreencapsrc);

    gst_clock_id_unref (id);
    qscreencapsrc->clock_id = NULL;
    if (ret == GST_CLOCK_UNSCHEDULED) {

      GST_OBJECT_UNLOCK (qscreencapsrc);
      return GST_FLOW_FLUSHING;
    }
    /* get Duration*/
    frame_duration = gst_util_uint64_scale_int (GST_SECOND, qscreencapsrc->fps_d, qscreencapsrc->fps_n);
  } else {
    GstClockTime next_frame_ts;

    GST_DEBUG_OBJECT (qscreencapsrc, "No need to wait for next frame time %"
        G_GUINT64_FORMAT " next frame = %" G_GINT64_FORMAT " prev = %"
        G_GINT64_FORMAT, next_screencap_ts, next_frame_no, qscreencapsrc->last_frame_no);
    next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
        qscreencapsrc->fps_d * GST_SECOND, qscreencapsrc->fps_n);
    /* Frame duration is from now until the next expected capture time */
    frame_duration = next_frame_ts - next_screencap_ts;
  }
  qscreencapsrc->last_frame_no = next_frame_no;
  GST_OBJECT_UNLOCK (qscreencapsrc);
redraw_checking:
  if (g_atomic_int_get (&qscreencapsrc->redraw_pending) == TRUE)
  {
    counting ++;
    GST_WARNING_OBJECT (qscreencapsrc,"redraw pending bufer len %d ", g_queue_get_length (&qscreencapsrc->qctx->qdisplay->pending_buffers));
    usleep(500); //sleep 500us

    if(counting > 30)
      goto retry;
    else
     goto redraw_checking;

  } else {
    /* commit wlbuf for screen catching */
    gstbuf = gst_qscreencap_src_qscreencap_catch (qscreencapsrc);
    if (!gstbuf)
      return GST_FLOW_ERROR;

    GST_BUFFER_DTS (gstbuf) = GST_CLOCK_TIME_NONE;

    GST_BUFFER_PTS (gstbuf) = next_screencap_ts;

    GST_BUFFER_DURATION (gstbuf) = frame_duration;

  }
  gstbuf = NULL;
  {
    /* get the output gstbuf */
    GstMetaQScreenCap *meta;
    GstIonBufFdMeta *ionmeta;
    QDisplay *qdisplay = qscreencapsrc->qctx->qdisplay;
    g_mutex_lock (&qdisplay->capture_lock);
    if (!g_queue_is_empty (&qdisplay->pending_buffers) ) {
       gstbuf = g_queue_pop_head (&qdisplay->pending_buffers);

       g_hash_table_remove (qdisplay->buffers,gstbuf);
       meta = GST_META_QSCREENCAP_GET (gstbuf);
       g_assert(meta != NULL);

       ionmeta = gst_buffer_add_ionbuf_meta (gstbuf, meta->gbminfo->bo_fd, 0,
	   meta->size, FALSE, meta->gbminfo->meta_fd, 0, 0, 0);

       if (!ionmeta) {
	     GST_ERROR_OBJECT (qscreencapsrc,
                 "Addition of ionBufInfo metadata to decoder output buffer failed.\n");
	     return GST_FLOW_ERROR;

       }
#ifdef DUMPFILE
       dump_tga("/home/root/app-data/abgr.tga", meta->width, meta->height, meta->stride, meta->data);
#endif
    }
    g_mutex_unlock(&qdisplay->capture_lock);

  }
  if(gstbuf == NULL)
  {
      goto retry;

  }

  *buf = gstbuf;
  GST_DEBUG_OBJECT (qscreencapsrc, "frame durartion %" G_GUINT64_FORMAT,
        GST_BUFFER_DURATION (*buf));
  GST_DEBUG_OBJECT (qscreencapsrc, "push time %" G_GUINT64_FORMAT,
        GST_BUFFER_PTS (*buf));
  printf("push gstbuf %p\n",gstbuf);
done:
  return GST_FLOW_OK;
}


static void
gst_qscreencap_src_clear_bufpool (GstQScreenCapSrc * qscreencapsrc)
{

  g_mutex_lock (&qscreencapsrc->buffer_lock);
  while (qscreencapsrc->buffer_list != NULL) {
    GstBuffer *qscreencap = qscreencapsrc->buffer_list->data;
	GST_DEBUG_OBJECT (qscreencapsrc, "free gstbuf %p",qscreencap);
    gst_qscreencap_buffer_free (qscreencap);

    qscreencapsrc->buffer_list = g_slist_delete_link (qscreencapsrc->buffer_list,
        qscreencapsrc->buffer_list);
  }
  g_mutex_unlock (&qscreencapsrc->buffer_lock);
}

static void
gst_qscreencap_src_dispose (GObject * object)
{
  GstQScreenCapSrc *src = GST_QSCREENCAP_SRC (object);
  GST_DEBUG_OBJECT (src, "dispose the src");

  /* Drop references in the buffer_list */

  gst_qscreencap_src_clear_bufpool (src);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_qscreencap_src_finalize (GObject * object)
{
  GstQScreenCapSrc *src = GST_QSCREENCAP_SRC (object);
  GST_DEBUG_OBJECT (src, "Finalizing the src..");
  gst_qscreencap_src_clear_bufpool(src);
  if (src->qctx)
    qscreencap_qctx_clear (src->qctx);

  g_mutex_clear (&src->buffer_lock);
  g_mutex_clear (&src->qc_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_qscreencap_src_get_caps (GstBaseSrc * bs, GstCaps * filter)
{
  GstQScreenCapSrc *s = GST_QSCREENCAP_SRC (bs);
  gint width, height;
  GstQCtx *qctx;
  if ((!s->qctx) && (!gst_qscreencap_src_open_display (s)))
    return gst_pad_get_pad_template_caps (GST_BASE_SRC (s)->srcpad);

  qctx = s->qctx;
  width = qctx->width;
  height = qctx->height;

  GST_DEBUG ("width = %d, height=%d", width, height);

  return gst_caps_new_full (
      gst_structure_new ("video/x-raw",
      "format",G_TYPE_STRING,"RGBA_UBWC",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1,
      1, NULL),
      gst_structure_new ("video/x-raw",
      "format",G_TYPE_STRING,"RGBA",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1,
      1, NULL),
      NULL);
}

static gboolean
gst_qscreencap_src_set_caps (GstBaseSrc * bs, GstCaps * caps)
{
  GstQScreenCapSrc *s = GST_QSCREENCAP_SRC (bs);
  GstStructure *structure;
  const GValue *new_fps;
  char* format = NULL;

  /* If not yet opened, disallow setcaps until later */
  if (!s->qctx)
    return FALSE;

  /* The only thing that can change is the framerate downstream wants */
  structure = gst_caps_get_structure (caps, 0);
  new_fps = gst_structure_get_value (structure, "framerate");
  if (!new_fps)
    return FALSE;

  /* Store this FPS for use when generating buffers */
  s->fps_n = gst_value_get_fraction_numerator (new_fps);
  s->fps_d = gst_value_get_fraction_denominator (new_fps);

  GST_DEBUG_OBJECT (s, "peer wants %d/%d fps", s->fps_n, s->fps_d);
  format = gst_structure_get_string(structure, "format");
  if (!format)
    return FALSE;
  if(strstr(format, "RGBA_UBWC")){
    s->qctx->format = GST_VIDEO_FORMAT_RGBA_UBWC;
  }
  else if(strstr(format, "RGBA")) {
    s->qctx->format = GST_VIDEO_FORMAT_RGBA;
  }
  else
    return FALSE;
  return TRUE;
}

static GstCaps *
gst_qscreencap_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  gint i;
  GstStructure *structure;

  caps = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 25, 1);
  }
  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

  return caps;
}

static void
gst_qscreencap_src_class_init (GstQScreenCapSrcClass * klass)
{
  GObjectClass *gobjclass = G_OBJECT_CLASS (klass);
  GstElementClass *elemclass = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesc = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *push_class = GST_PUSH_SRC_CLASS (klass);

  gobjclass->dispose = gst_qscreencap_src_dispose;
  gobjclass->finalize = GST_DEBUG_FUNCPTR (gst_qscreencap_src_finalize);


  gst_element_class_add_pad_template (elemclass,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_set_static_metadata (elemclass,
      "QScreencap video source",
      "Source/Video",
      "Creates a screencapture video stream",
      "laisheng <laisheng@codeaurora.org>");

  basesc->fixate = gst_qscreencap_src_fixate;
  basesc->get_caps = gst_qscreencap_src_get_caps;
  basesc->set_caps = gst_qscreencap_src_set_caps;
  basesc->start = gst_qscreencap_src_start;
  basesc->stop = gst_qscreencap_src_stop;
  basesc->unlock = gst_qscreencap_src_unlock;
  push_class->create = gst_qscreencap_src_create;
}

static void
gst_qscreencap_src_init (GstQScreenCapSrc * qscreencapsrc)
{
  gst_base_src_set_format (GST_BASE_SRC (qscreencapsrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (qscreencapsrc), TRUE);

  g_mutex_init (&qscreencapsrc->buffer_lock);
  g_mutex_init (&qscreencapsrc->qc_lock);

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  GST_DEBUG_CATEGORY_INIT (gst_debug_qscreencap_src, "qscreencapsrc", 0,
      "qscreencapsrc element debug");

  ret = gst_element_register (plugin, "qscreencapsrc", GST_RANK_NONE,
      GST_TYPE_QSCREENCAP_SRC);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qscreencapsrc,
    "gst screen capture plugin ",
    plugin_init, VERSION, "LGPL", " gstreamer screen capture plugin", "unknown package origin");
