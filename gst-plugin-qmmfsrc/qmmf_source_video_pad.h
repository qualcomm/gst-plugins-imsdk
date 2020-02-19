/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QMMFSRC_VIDEO_PAD_H__
#define __GST_QMMFSRC_VIDEO_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define QMMFSRC_COMMON_VIDEO_CAPS \
    "width = (int) [ 16, " MAX_WIDTH " ], "   \
    "height = (int) [ 16," MAX_HEIGHT " ], "  \
    "framerate = (fraction) [ 0/1, 30/1 ]"

#define QMMFSRC_VIDEO_H264_PROFILES \
    "baseline, main, high"

#define QMMFSRC_VIDEO_H264_LEVELS \
    "1, 1.3, 2, 2.1, 2.2, 3, 3.1, 3.2, 4, 4.1, 4.2, 5, 5.1, 5.2"

#define QMMFSRC_VIDEO_H264_CAPS                                \
    "video/x-h264, "                                           \
    "profile = (string) { " QMMFSRC_VIDEO_H264_PROFILES " }, " \
    "level = (string) { " QMMFSRC_VIDEO_H264_LEVELS " }, "     \
    QMMFSRC_COMMON_VIDEO_CAPS

#define QMMFSRC_VIDEO_H264_CAPS_WITH_FEATURES(features)        \
    "video/x-h264(" features "), "                             \
    "profile = (string) { " QMMFSRC_VIDEO_H264_PROFILES " }, " \
    "level = (string) { " QMMFSRC_VIDEO_H264_LEVELS " }, "     \
    QMMFSRC_COMMON_VIDEO_CAPS

#define QMMFSRC_VIDEO_RAW_CAPS(formats) \
    "video/x-raw, "                     \
    "format = (string) " formats ", "   \
    QMMFSRC_COMMON_VIDEO_CAPS

#define QMMFSRC_VIDEO_RAW_CAPS_WITH_FEATURES(features, formats) \
    "video/x-raw(" features "), "                               \
    "format = (string) " formats ", "                           \
    QMMFSRC_COMMON_VIDEO_CAPS

// Boilerplate cast macros and type check macros for QMMF Source Video Pad.
#define GST_TYPE_QMMFSRC_VIDEO_PAD (qmmfsrc_video_pad_get_type())
#define GST_QMMFSRC_VIDEO_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QMMFSRC_VIDEO_PAD,GstQmmfSrcVideoPad))
#define GST_QMMFSRC_VIDEO_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QMMFSRC_VIDEO_PAD,GstQmmfSrcVideoPadClass))
#define GST_IS_QMMFSRC_VIDEO_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QMMFSRC_VIDEO_PAD))
#define GST_IS_QMMFSRC_VIDEO_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QMMFSRC_VIDEO_PAD))

#define GST_QMMFSRC_VIDEO_PAD_GET_LOCK(obj) (&GST_QMMFSRC_VIDEO_PAD(obj)->lock)
#define GST_QMMFSRC_VIDEO_PAD_LOCK(obj) \
  g_mutex_lock(GST_QMMFSRC_VIDEO_PAD_GET_LOCK(obj))
#define GST_QMMFSRC_VIDEO_PAD_UNLOCK(obj) \
  g_mutex_unlock(GST_QMMFSRC_VIDEO_PAD_GET_LOCK(obj))

#define VIDEO_TRACK_ID_OFFSET (0x01)

typedef enum {
  GST_VIDEO_CODEC_UNKNOWN,
  GST_VIDEO_CODEC_NONE,
  GST_VIDEO_CODEC_H264,
} GstVideoCodec;

enum
{
  GST_VIDEO_CONTROL_RATE_DISABLE,
  GST_VIDEO_CONTROL_RATE_VARIABLE,
  GST_VIDEO_CONTROL_RATE_CONSTANT,
  GST_VIDEO_CONTROL_RATE_MAXBITRATE,
  GST_VIDEO_CONTROL_RATE_VARIABLE_SKIP_FRAMES,
  GST_VIDEO_CONTROL_RATE_CONSTANT_SKIP_FRAMES,
  GST_VIDEO_CONTROL_RATE_MAXBITRATE_SKIP_FRAMES,
};

typedef void (*GstVideoParamCb) (GstPad * pad, guint param_id, gpointer data);

typedef struct _GstQmmfSrcVideoPad GstQmmfSrcVideoPad;
typedef struct _GstQmmfSrcVideoPadClass GstQmmfSrcVideoPadClass;

struct _GstQmmfSrcVideoPad {
  /// Inherited parent structure.
  GstPad            parent;

  /// Synchronization segment.
  GstSegment        segment;

  /// Global mutex lock.
  GMutex            lock;
  /// Index of the video pad.
  guint             index;
  /// QMMF Recorder master track index, set by the pad capabilities.
  gint              srcidx;

  /// ID of the QMMF Recorder track which belongs to this pad.
  guint             id;
  /// QMMF Recorder track width, set by the pad capabilities.
  gint              width;
  /// QMMF Recorder track height, set by the pad capabilities.
  gint              height;
  /// QMMF Recorder track framerate, set by the pad capabilities.
  gdouble           framerate;
  /// GStreamer video pad output buffers format.
  GstVideoFormat    format;
  /// Whether the GStreamer stream is uncompressed or compressed and its type.
  GstVideoCodec     codec;
  /// Agnostic structure containing codec specific parameters.
  GstStructure     *params;

  /// QMMF Recorder track buffers duration, calculated from framerate.
  GstClockTime      duration;

  /// Queue for GStreamer buffers wrappers around QMMF Recorder buffers.
  GstDataQueue     *buffers;
};

struct _GstQmmfSrcVideoPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType qmmfsrc_video_pad_get_type (void);

/// Allocates memory for a source video pad with given template, name and index.
/// It will also set custom functions for query, event and activatemode.
GstPad * qmmfsrc_request_video_pad (GstPadTemplate * templ, const gchar * name,
                                    const guint index);

/// Deactivates and releases the memory allocated for the source video pad.
void qmmfsrc_release_video_pad (GstElement * element, GstPad * pad);

/// Sets the GST buffers queue to flushing state if flushing is TRUE.
/// If set to flushing state, any incoming data on the queue will be discarded.
void qmmfsrc_video_pad_flush_buffers_queue (GstPad * pad, gboolean flush);

/// Modifies the pad capabilities into a representation with only fixed values.
gboolean qmmfsrc_video_pad_fixate_caps (GstPad * pad);

G_END_DECLS

#endif // __GST_QMMFSRC_VIDEO_PAD_H__
