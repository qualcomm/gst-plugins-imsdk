/*
* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QMMFSRC_VIDEO_PAD_H__
#define __GST_QMMFSRC_VIDEO_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define QMMFSRC_COMMON_VIDEO_CAPS \
    "width = (int) [ 16, " GST_VIDEO_MAX_WIDTH " ], "       \
    "height = (int) [ 16," GST_VIDEO_MAX_HEIGHT " ], "      \
    "framerate = (fraction) [ 0/1, " GST_VIDEO_MAX_FPS " ]"

#define QMMFSRC_VIDEO_JPEG_CAPS \
    "image/jpeg, "              \
    QMMFSRC_COMMON_VIDEO_CAPS

#define QMMFSRC_VIDEO_RAW_CAPS(formats) \
    "video/x-raw, "                     \
    "format = (string) " formats ", "   \
    QMMFSRC_COMMON_VIDEO_CAPS

#define QMMFSRC_VIDEO_RAW_CAPS_WITH_FEATURES(features, formats) \
    "video/x-raw(" features "), "                               \
    "format = (string) " formats ", "                           \
    QMMFSRC_COMMON_VIDEO_CAPS

#define QMMFSRC_VIDEO_BAYER_CAPS(formats, bpps) \
    "video/x-bayer, "                           \
    "format = (string) " formats ", "           \
    "bpp = (string) " bpps ", "                 \
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
  GST_VIDEO_CODEC_JPEG,
} GstVideoCodec;

typedef enum {
  GST_VIDEO_COMPRESSION_NONE,
  GST_VIDEO_COMPRESSION_UBWC,
} GstVideoCompression;

#ifdef FEATURE_VIDEO_PREVIEW_TYPE_SUPPORT
enum
{
  VIDEO_TYPE_VIDEO,
  VIDEO_TYPE_PREVIEW,
};
#endif

typedef void (*GstVideoParamCb) (GstPad * pad, guint param_id, gpointer data);

typedef struct _GstQmmfSrcVideoPad GstQmmfSrcVideoPad;
typedef struct _GstQmmfSrcVideoPadClass GstQmmfSrcVideoPadClass;

struct _GstQmmfSrcVideoPad {
  /// Inherited parent structure.
  GstPad              parent;

  /// Synchronization segment.
  GstSegment          segment;

  /// To send stream start event
  gboolean            stream_start;

  /// Global mutex lock.
  GMutex              lock;
  /// Index of the video pad.
  guint               index;
  /// QMMF Recorder master track index, set by the pad capabilities.
  gint                srcidx;

  /// ID of the QMMF Recorder track which belongs to this pad.
  guint               id;
  /// Session ID of the QMMF Recorder track which belongs to this pad.
  guint               session_id;
  /// QMMF Recorder track width, set by the pad capabilities.
  gint                width;
  /// QMMF Recorder track height, set by the pad capabilities.
  gint                height;
  /// QMMF Recorder track framerate, set by the pad capabilities.
  gdouble             framerate;
  /// GStreamer video pad output buffers format.
  gint                format;
  /// GStreamer video pad output bayer format bits per pixel.
  guint               bpp;
  /// Video format compression (none or ubwc).
  GstVideoCompression compression;
  /// Whether the GStreamer stream is uncompressed or compressed and its type.
  GstVideoCodec       codec;

  /// Crop region.
  GstVideoRectangle   crop;
  /// Additional buffers that are going to be allocated.
  guint               xtrabufs;

  /// QMMF Recorder track buffers duration, calculated from framerate.
  GstClockTime        duration;

  /// Queue for GStreamer buffers wrappers around QMMF Recorder buffers.
  GstDataQueue        *buffers;

#ifdef FEATURE_VIDEO_PREVIEW_TYPE_SUPPORT
  /// QMMF Recorder Video Type
  gint                type;
#endif
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
