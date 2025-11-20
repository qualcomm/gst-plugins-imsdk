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

#ifndef __GST_QMMFSRC_IMAGE_PAD_H__
#define __GST_QMMFSRC_IMAGE_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define QMMFSRC_COMMON_IMAGE_CAPS                             \
    "width = (int) [ 16, " GST_IMAGE_MAX_WIDTH " ], "         \
    "height = (int) [ 16," GST_IMAGE_MAX_HEIGHT " ], "        \
    "framerate = (fraction) [ 0/1, 30/1 ]"

#define QMMFSRC_IMAGE_JPEG_CAPS \
    "image/jpeg, "              \
    QMMFSRC_COMMON_IMAGE_CAPS

#define QMMFSRC_IMAGE_RAW_CAPS(formats) \
    "video/x-raw, "                     \
    "format = (string) " formats ", "   \
    QMMFSRC_COMMON_IMAGE_CAPS

#define QMMFSRC_IMAGE_RAW_CAPS_WITH_FEATURES(features, formats) \
    "video/x-raw(" features "), "                               \
    "format = (string) " formats ", "                           \
    QMMFSRC_COMMON_IMAGE_CAPS

#define QMMFSRC_IMAGE_BAYER_CAPS(formats, bpps) \
    "video/x-bayer, "                           \
    "format = (string) " formats ", "           \
    "bpp = (string) " bpps ", "                 \
    QMMFSRC_COMMON_IMAGE_CAPS

// Boilerplate cast macros and type check macros for QMMF Source Image Pad.
#define GST_TYPE_QMMFSRC_IMAGE_PAD (qmmfsrc_image_pad_get_type())
#define GST_QMMFSRC_IMAGE_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QMMFSRC_IMAGE_PAD,GstQmmfSrcImagePad))
#define GST_QMMFSRC_IMAGE_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QMMFSRC_IMAGE_PAD,GstQmmfSrcImagePadClass))
#define GST_IS_QMMFSRC_IMAGE_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QMMFSRC_IMAGE_PAD))
#define GST_IS_QMMFSRC_IMAGE_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QMMFSRC_IMAGE_PAD))

#define GST_QMMFSRC_IMAGE_PAD_GET_LOCK(obj) (&GST_QMMFSRC_IMAGE_PAD(obj)->lock)
#define GST_QMMFSRC_IMAGE_PAD_LOCK(obj) \
  g_mutex_lock(GST_QMMFSRC_IMAGE_PAD_GET_LOCK(obj))
#define GST_QMMFSRC_IMAGE_PAD_UNLOCK(obj) \
  g_mutex_unlock(GST_QMMFSRC_IMAGE_PAD_GET_LOCK(obj))

#define GST_TYPE_QMMFSRC_IMAGE_PAD_MODE (gst_qmmfsrc_image_pad_mode_get_type())

typedef enum {
  GST_IMAGE_CODEC_UNKNOWN,
  GST_IMAGE_CODEC_NONE,
  GST_IMAGE_CODEC_JPEG,
} GstImageCodec;

typedef enum {
  GST_IMAGE_SUBFORMAT_NONE,
  GST_IMAGE_SUBFORMAT_HEIF,
} GstImageSubFormat;

typedef enum {
  GST_IMAGE_MODE_VIDEO,
  GST_IMAGE_MODE_CONTINUOUS,
} GstImageMode;

typedef struct _GstQmmfSrcImagePad GstQmmfSrcImagePad;
typedef struct _GstQmmfSrcImagePadClass GstQmmfSrcImagePadClass;

struct _GstQmmfSrcImagePad {
  /// Inherited parent structure.
  GstPad            parent;

  /// Synchronization segment.
  GstSegment        segment;

  /// To send stream start event
  gboolean          stream_start;

  /// Global mutex lock.
  GMutex            lock;
  /// Index of the image pad.
  guint             index;

  /// QMMF Recorder image stream width, set by the pad capabilities.
  gint              width;
  /// QMMF Recorder image stream height, set by the pad capabilities.
  gint              height;
  /// QMMF Recorder image stream framerate, set by the pad capabilities.
  gfloat            framerate;
  /// GStreamer image pad output buffers format.
  gint              format;
  /// GStreamer image pad output bayer format bits per pixel.
  guint             bpp;
  /// Whether the GStreamer stream is uncompressed or compressed and its type.
  GstImageCodec     codec;
  /// Agnostic structure containing codec specific parameters.
  GstStructure      *params;
  /// GStreamer image pad sub format.
  GstImageSubFormat subformat;

  /// QMMF Recorder image stream buffers duration, calculated from framerate.
  GstClockTime      duration;

  /// Queue for GStreamer buffers wrappers around QMMF Recorder buffers.
  GstDataQueue      *buffers;

  ///rotate property for stream orientation
  gint              rotate;

  /// Buffer pool
  GstBufferPool     *pool;

  /// Select physical camera or layout to stitch images
  glong             log_stream_type;
};

struct _GstQmmfSrcImagePadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType qmmfsrc_image_pad_get_type(void);

/// Allocates memory for a source image pad with given template, name and index.
/// It will also set custom functions for query, event and activatemode.
GstPad * qmmfsrc_request_image_pad (GstPadTemplate * templ, const gchar * name,
                                    const guint index);

/// Deactivates and releases the memory allocated for the source image pad.
void     qmmfsrc_release_image_pad (GstElement *element, GstPad *pad);

/// Sets the GST buffers queue to flushing state if flushing is TRUE.
/// If set to flushing state, any incoming data on the queue will be discarded.
void     qmmfsrc_image_pad_flush_buffers_queue (GstPad *pad, gboolean flush);

/// Modifies the pad capabilities into a representation with only fixed values.
gboolean qmmfsrc_image_pad_fixate_caps (GstPad * pad);

GType
gst_qmmfsrc_image_pad_mode_get_type();

G_END_DECLS

#endif // __GST_QMMFSRC_IMAGE_PAD_H__
