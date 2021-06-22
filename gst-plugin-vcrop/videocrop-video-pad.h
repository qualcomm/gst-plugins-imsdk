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

#ifndef __GST_VIDEOCROP_VIDEO_PAD_H__
#define __GST_VIDEOCROP_VIDEO_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define VIDEOCROP_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

// Boilerplate cast macros and type check macros for VIDEOCROP Source Video Pad.
#define GST_TYPE_VIDEOCROP_VIDEO_PAD (videocrop_video_pad_get_type())
#define GST_VIDEOCROP_VIDEO_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOCROP_VIDEO_PAD,GstVideoCropVideoPad))
#define GST_VIDEOCROP_VIDEO_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOCROP_VIDEO_PAD,GstVideoCropVideoPadClass))
#define GST_IS_VIDEOCROP_VIDEO_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOCROP_VIDEO_PAD))
#define GST_IS_VIDEOCROP_VIDEO_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOCROP_VIDEO_PAD))

#define GST_VIDEOCROP_VIDEO_PAD_GET_LOCK(obj) (&GST_VIDEOCROP_VIDEO_PAD(obj)->lock)
#define GST_VIDEOCROP_VIDEO_PAD_LOCK(obj) \
  g_mutex_lock(GST_VIDEOCROP_VIDEO_PAD_GET_LOCK(obj))
#define GST_VIDEOCROP_VIDEO_PAD_UNLOCK(obj) \
  g_mutex_unlock(GST_VIDEOCROP_VIDEO_PAD_GET_LOCK(obj))

#define VIDEO_TRACK_ID_OFFSET (0x01)

typedef void (*GstVideoParamCb) (GstPad * pad, guint param_id, gpointer data);

typedef struct _GstVideoCropVideoPad GstVideoCropVideoPad;
typedef struct _GstVideoCropVideoPadClass GstVideoCropVideoPadClass;

struct _GstVideoCropVideoPad {
  /// Inherited parent structure.
  GstPad              parent;

  /// Synchronization segment.
  GstSegment          segment;

  /// Global mutex lock.
  GMutex              lock;
  /// Index of the video pad.
  guint               index;

  /// ID of the VIDEOCROP Recorder track which belongs to this pad.
  guint               id;
  /// VIDEOCROP Recorder track width, set by the pad capabilities.
  gint                width;
  /// VIDEOCROP Recorder track height, set by the pad capabilities.
  gint                height;
  /// VIDEOCROP Recorder track framerate, set by the pad capabilities.
  gdouble             framerate;
  /// GStreamer video pad output buffers format.
  gint                format;
  /// VIDEOCROP Recorder track buffers duration, calculated from framerate.
  GstClockTime        duration;

  /// Queue for GStreamer buffers wrappers around VIDEOCROP Recorder buffers.
  GstDataQueue        *buffers;
};

struct _GstVideoCropVideoPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType videocrop_video_pad_get_type (void);

/// Allocates memory for a source video pad with given template, name and index.
/// It will also set custom functions for query, event and activatemode.
GstPad * videocrop_request_video_pad (GstPadTemplate * templ,
    const gchar * name, const guint index);

/// Deactivates and releases the memory allocated for the source video pad.
void videocrop_release_video_pad (GstElement * element, GstPad * pad);

/// Sets the GST buffers queue to flushing state if flushing is TRUE.
/// If set to flushing state, any incoming data on the queue will be discarded.
void videocrop_video_pad_flush_buffers_queue (GstPad * pad, gboolean flush);

/// Modifies the pad capabilities into a representation with only fixed values.
gboolean videocrop_video_pad_fixate_caps (GstPad * pad);

G_END_DECLS

#endif // __GST_VIDEOCROP_VIDEO_PAD_H__
