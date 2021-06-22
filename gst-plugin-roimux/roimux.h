/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QTI_ROIMUX_H__
#define __GST_QTI_ROIMUX_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_ROIMUX \
  (gst_roimux_get_type())
#define GST_ROIMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ROIMUX,GstRoiMux))
#define GST_ROIMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ROIMUX,GstRoiMuxClass))
#define GST_IS_ROIMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ROIMUX))
#define GST_IS_ROIMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ROIMUX))
#define GST_ROIMUX_CAST(obj)       ((GstRoiMux *)(obj))

#define GST_ROIMUX_GET_LOCK(obj)   (&GST_ROIMUX(obj)->lock)
#define GST_ROIMUX_LOCK(obj)       g_mutex_lock(GST_ROIMUX_GET_LOCK(obj))
#define GST_ROIMUX_UNLOCK(obj)     g_mutex_unlock(GST_ROIMUX_GET_LOCK(obj))

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
  ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
      : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
          : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
              : (state <= GST_STATE_NULL))))

typedef struct _GstRoiMux GstRoiMux;
typedef struct _GstRoiMuxClass GstRoiMuxClass;

struct _GstRoiMux {
  GstElement element;
  /// Src pad
  GstPad *srcpad;
  /// Video sink pad
  GstPad *vidsinkpad;
  /// Text sink pad
  GstPad *textsinkpad;
  /// Video sink EOS flag
  gboolean vidsink_eos;
  /// Text sink EOS flag
  gboolean textsink_eos;
  /// Global mutex lock.
  GMutex lock;
  /// Caps received flag
  gboolean  have_caps;
  /// Segment received flag
  gboolean  have_segment;
  /// Incoming caps
  GstCaps *caps;
  /// Src segment
  GstSegment src_segment;
  /// ROI data list
  GList *roi_data_list;
  /// Incoming text
  gchar *config_data;
  /// Config roi size
  gsize config_size;
  /// Config parsed flag
  gboolean is_config_parsed;
};

struct _GstRoiMuxClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_roimux_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ROIMUX_H__
