/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QMMFSRC_AUDIO_PAD_H__
#define __GST_QMMFSRC_AUDIO_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define QMMFSRC_COMMON_AUDIO_CAPS(c, r, b)          \
    "channels = (int) [ 1, " G_STRINGIFY (c) " ], " \
    "rate = (int) [ 1, " G_STRINGIFY (r) " ], "     \
    "bitdepth = (int) [ 1, " G_STRINGIFY (b) " ]"

#define QMMFSRC_AUDIO_AAC_CAPS                              \
    "audio/mpeg, "                                          \
    "mpegversion = (int) { 4 }, "                        \
    "stream-format = (string) { adts, adif, raw, mp4ff }, " \
    QMMFSRC_COMMON_AUDIO_CAPS(2, 128000, 128)

#define QMMFSRC_AUDIO_AMR_CAPS              \
    "audio/AMR, "                           \
    QMMFSRC_COMMON_AUDIO_CAPS(2, 8000, 128)

#define QMMFSRC_AUDIO_AMRWB_CAPS             \
    "audio/AMR-WB, "                         \
    QMMFSRC_COMMON_AUDIO_CAPS(2, 16000, 128)

// Boilerplate cast macros and type check macros for QMMF Source Audio Pad.
#define GST_TYPE_QMMFSRC_AUDIO_PAD (qmmfsrc_audio_pad_get_type())
#define GST_QMMFSRC_AUDIO_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QMMFSRC_AUDIO_PAD,GstQmmfSrcAudioPad))
#define GST_QMMFSRC_AUDIO_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QMMFSRC_AUDIO_PAD,GstQmmfSrcAudioPadClass))
#define GST_IS_QMMFSRC_AUDIO_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QMMFSRC_AUDIO_PAD))
#define GST_IS_QMMFSRC_AUDIO_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QMMFSRC_AUDIO_PAD))

#define GST_QMMFSRC_AUDIO_PAD_GET_LOCK(obj) (&GST_QMMFSRC_AUDIO_PAD(obj)->lock)
#define GST_QMMFSRC_AUDIO_PAD_LOCK(obj) \
  g_mutex_lock(GST_QMMFSRC_AUDIO_PAD_GET_LOCK(obj))
#define GST_QMMFSRC_AUDIO_PAD_UNLOCK(obj) \
  g_mutex_unlock(GST_QMMFSRC_AUDIO_PAD_GET_LOCK(obj))

#define AUDIO_TRACK_ID_OFFSET (0xFF)

typedef enum {
  GST_AUDIO_CODEC_TYPE_UNKNOWN,
  GST_AUDIO_CODEC_TYPE_NONE,
  GST_AUDIO_CODEC_TYPE_AAC,
  GST_AUDIO_CODEC_TYPE_AMR,
  GST_AUDIO_CODEC_TYPE_AMRWB,
} GstAudioCodecType;

typedef struct _GstQmmfSrcAudioPad GstQmmfSrcAudioPad;
typedef struct _GstQmmfSrcAudioPadClass GstQmmfSrcAudioPadClass;

struct _GstQmmfSrcAudioPad {
  /// Inherited parent structure.
  GstPad            parent;

  /// Global mutex lock.
  GMutex            lock;
  /// Index of the audio pad.
  guint             index;

  /// ID of the QMMF Recorder track which belongs to this pad.
  guint             id;
  /// QMMF Recorder track audio device ID, set by the pad capabilities.
  gint              device;
  /// QMMF Recorder track channels, set by the pad capabilities.
  gint              channels;
  /// QMMF Recorder track sample rate, set by the pad capabilities.
  gint              samplerate;
  /// QMMF Recorder track bit rate, set by the pad capabilities.
  gint              bitdepth;
  /// GStreamer audio pad output buffers format.
  GstAudioFormat    format;
  /// Whether the GStreamer stream is uncompressed or compressed and its type.
  GstAudioCodecType codec;
  /// Agnostic structure containing codec specific parameters.
  GstStructure     *params;

  /// QMMF Recorder stream buffers duration, calculated from samplerate.
  guint64           duration;
  /// Timestamp base used to normalize buffer timestamps to running time.
  guint64           tsbase;

  /// Queue for GStreamer buffers wrappers around QMMF Recorder buffers.
  GstDataQueue     *buffers;
};

struct _GstQmmfSrcAudioPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType qmmfsrc_audio_pad_get_type(void);

/// Allocates memory for a source audio pad with given template, name and index.
/// It will also set custom functions for query, event and activatemode.
GstPad * qmmfsrc_request_audio_pad (GstPadTemplate *templ, const gchar *name,
                                    const guint index);

/// Deactivates and releases the memory allocated for the source audio pad.
void     qmmfsrc_release_audio_pad (GstElement *element, GstPad *pad);

/// Sets the GST buffers queue to flushing state if flushing is TRUE.
/// If set to flushing state, any incoming data on the queue will be discarded.
void     qmmfsrc_audio_pad_flush_buffers_queue (GstPad *pad, gboolean flush);

/// Modifies the pad capabilities into a representation with only fixed values.
gboolean qmmfsrc_audio_pad_fixate_caps (GstPad * pad);

G_END_DECLS

#endif // __GST_QMMFSRC_AUDIO_PAD_H__
