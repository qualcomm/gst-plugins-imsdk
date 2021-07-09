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

#ifndef __GST_QTI_VIDEO_CROP_H__
#define __GST_QTI_VIDEO_CROP_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "videocrop-pad-process.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_CROP \
  (gst_video_crop_get_type())
#define GST_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_CROP,GstRoiVideoCrop))
#define GST_VIDEO_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_CROP,GstRoiVideoCropClass))
#define GST_IS_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_CROP))
#define GST_IS_VIDEO_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_CROP))
#define GST_VIDEO_CROP_CAST(obj)       ((GstRoiVideoCrop *)(obj))

#define GST_VIDEO_CROP_GET_LOCK(obj)   (&GST_VIDEO_CROP(obj)->lock)
#define GST_VIDEO_CROP_LOCK(obj)       g_mutex_lock(GST_VIDEO_CROP_GET_LOCK(obj))
#define GST_VIDEO_CROP_UNLOCK(obj)     g_mutex_unlock(GST_VIDEO_CROP_GET_LOCK(obj))

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
  ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
      : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
          : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
              : (state <= GST_STATE_NULL))))

typedef struct _GstRoiVideoCrop GstRoiVideoCrop;
typedef struct _GstRoiVideoCropClass GstRoiVideoCropClass;

struct _GstRoiVideoCrop {
  GstElement element;
  /// Sink pad
  GstPad         *sinkpad;
  /// Input segment
  GstSegment     segment;
  /// Global mutex lock.
  GMutex         lock;
  /// Next available index for the source pads.
  guint          nextidx;
  /// Containt the curent set crop
  GstVideoRectangle crop;
  /// Used crop engine
  GstVideoCropType  crop_type;
  /// Input capabilities
  GstCaps           *inputcaps;
  /// Received segment
  gboolean          have_segment;
  /// List contains pad process instances
  GList             *pads_process;
  /// Number of maximum output buffers
  guint             maxbuffers;
};

struct _GstRoiVideoCropClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_video_crop_get_type (void);

G_END_DECLS

#endif // __GST_QTI_VIDEO_CROP_H__
