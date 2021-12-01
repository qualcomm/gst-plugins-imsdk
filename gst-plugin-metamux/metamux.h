/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QTI_METAMUX_H__
#define __GST_QTI_METAMUX_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_METAMUX (gst_meta_mux_get_type())
#define GST_METAMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_METAMUX,GstMetaMux))
#define GST_METAMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_METAMUX,GstMetaMuxClass))
#define GST_IS_METAMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_METAMUX))
#define GST_IS_METAMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_METAMUX))
#define GST_METAMUX_CAST(obj)       ((GstMetaMux *)(obj))

#define GST_METAMUX_GET_LOCK(obj)   (&GST_METAMUX(obj)->lock)
#define GST_METAMUX_LOCK(obj)       g_mutex_lock(GST_METAMUX_GET_LOCK(obj))
#define GST_METAMUX_UNLOCK(obj)     g_mutex_unlock(GST_METAMUX_GET_LOCK(obj))

typedef struct _GstMetaMux GstMetaMux;
typedef struct _GstMetaMuxClass GstMetaMuxClass;

struct _GstMetaMux
{
  /// Inherited parent structure.
  GstElement     parent;

  /// Global mutex lock.
  GMutex         lock;

  /// Next available index for the sink pads.
  guint          nextidx;

  /// Convenient local reference to media sink pad.
  GstPad         *sinkpad;
  /// Convenient local reference to data sink pads.
  GList          *metapads;
  /// Convenient local reference to source pad.
  GstPad         *srcpad;

  /// Source segment.
  GstSegment     segment;

  /// Info regarding the negotiated audio/video caps.
  GstVideoInfo   *vinfo;
  GstAudioInfo   *ainfo;

  /// Condition for push/pop buffers from the queues.
  GCond          wakeup;
};

struct _GstMetaMuxClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_meta_mux_get_type (void);

G_END_DECLS

#endif // __GST_QTI_METAMUX_H__

