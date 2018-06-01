/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
 * (IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/* screencapsrc: Screencap plugin for GStreamer*/
#ifndef __GST_QSCREENCAP_SRC_H__
#define __GST_QSCREENCAP_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include "qscreencaputil.h"

G_BEGIN_DECLS

#define GST_TYPE_QSCREENCAP_SRC (gst_qscreencap_src_get_type())
#define GST_QSCREENCAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QSCREENCAP_SRC,GstQScreenCapSrc))
#define GST_QSCREENCAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QSCREENCAP_SRC,GstQScreenCapSrc))
#define GST_IS_QSCREENCAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QSCREENCAP_SRC))
#define GST_IS_QSCREENCAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QSCREENCAP_SRC))

typedef struct _GstQScreenCapSrc GstQScreenCapSrc;
typedef struct _GstQScreenCapSrcClass GstQScreenCapSrcClass;

GType gst_qscreencap_src_get_type (void) G_GNUC_CONST;

struct _GstQScreenCapSrc
{
  GstPushSrc parent;

  /* Information on display */
  GstQCtx *qctx;
  gint width;
  gint height;

  gint fps_n;
  gint fps_d;

  GstClockID clock_id;
  gint64 last_frame_no;

  GMutex  qc_lock;

  GMutex  buffer_lock;
  GSList *buffer_list;

  gboolean redraw_pending;
};

struct _GstQScreenCapSrcClass
{
  GstPushSrcClass parent_class;
};


G_END_DECLS

#endif /* __GST_QSCREENCAP_SRC_H__ */
