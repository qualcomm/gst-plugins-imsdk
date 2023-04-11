/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef _GST_C2_VDEC_H_
#define _GST_C2_VDEC_H_

#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/allocators/allocators.h>

#include "c2-engine/c2-wrapper.h"
#include "c2-engine/common.h"

G_BEGIN_DECLS

#define QTICODEC2VDEC_SINK_WH_CAPS    \
  "width  = (int) [ 32, 8192 ], "     \
  "height = (int) [ 32, 8192 ]"
#define QTICODEC2VDEC_SINK_COMPRESSION_CAPS    \
    "compression = (string) { ubwc, linear }"
#define QTICODEC2VDEC_SINK_FPS_CAPS    \
  "framerate = (fraction) [ 0, 480 ]"
#define QTICODEC2VDEC_RAW_CAPS(formats) \
  "video/x-raw, "                       \
  "format = (string) " formats ", "     \
  QTICODEC2VDEC_SINK_WH_CAPS ", "       \
  QTICODEC2VDEC_SINK_FPS_CAPS ", "      \
  QTICODEC2VDEC_SINK_COMPRESSION_CAPS
#define QTICODEC2VDEC_RAW_CAPS_WITH_FEATURES(features, formats) \
  "video/x-raw(" features "), "                                 \
  "format = (string) " formats ", "                             \
  QTICODEC2VDEC_SINK_WH_CAPS   ", "                             \
  QTICODEC2VDEC_SINK_FPS_CAPS  ", "                             \
  QTICODEC2VDEC_SINK_COMPRESSION_CAPS

#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"

#define GST_TYPE_C2_VDEC   (gst_c2vdec_get_type())
#define GST_C2_VDEC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_C2_VDEC,GstC2VideoDecoder))
#define GST_C2_VDEC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_C2_VDEC,GstC2VideoDecoderClass))
#define GST_IS_C2_VDEC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_C2_VDEC))
#define GST_IS_C2_VDEC_CLASS(obj) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_C2_VDEC))


#define MAX_QUEUED_FRAME  64
#define C2_TICKS_PER_SECOND 1000000

typedef struct _GstC2VideoDecoder GstC2VideoDecoder;
typedef struct _GstC2VideoDecoderClass GstC2VideoDecoderClass;

struct _GstC2VideoDecoder {
  GstVideoDecoder parent;

  gboolean silent;

  void *comp_store;
  void *comp;
  void *comp_intf;
  gchar *comp_name;

  guint64 queued_frame[MAX_QUEUED_FRAME];

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  gboolean eos_reached;
  gboolean input_setup;
  gboolean output_setup;

  gint width;
  gint height;
  guint64 frame_index;
  GstVideoInterlaceMode interlace_mode;
  guint64 num_input_queued;
  guint64 num_output_done;
  gboolean downstream_supports_dma;
  gboolean output_picture_order_mode;
  gboolean low_latency_mode;

  GMutex pending_lock;
  GCond pending_cond;
  GstC2Wrapper *wrapper;
  gboolean is_ubwc;
  GMutex free_buff_lock;
  GCond free_buff_cond;
};

struct _GstC2VideoDecoderClass {
  GstVideoDecoderClass parent;
};

GType gst_c2vdec_get_type (void);

G_END_DECLS

#endif
