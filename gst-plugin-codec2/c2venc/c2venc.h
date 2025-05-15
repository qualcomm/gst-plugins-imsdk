/*
* Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef _GST_C2_VENC_H_
#define _GST_C2_VENC_H_

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/allocators/allocators.h>

#include "c2-engine/c2-engine.h"
#include "c2-engine/c2-engine-params.h"

G_BEGIN_DECLS

#define GST_TYPE_C2_VENC (gst_c2_venc_get_type())
#define GST_C2_VENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_C2_VENC, GstC2VEncoder))
#define GST_C2_VENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_C2_VENC, GstC2VEncoderClass))
#define GST_IS_C2_VENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_C2_VENC))
#define GST_IS_C2_VENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_C2_VENC))
#define GST_C2_VENC_CAST(obj) ((GstC2VEncoder *)(obj))

typedef struct _GstC2VEncoder GstC2VEncoder;
typedef struct _GstC2VEncoderClass GstC2VEncoderClass;

struct _GstC2VEncoder {
  GstVideoEncoder      parent;

  gchar                *name;
  GstC2Engine          *engine;

  /// Negotiated input resolution, format, etc.
  GstVideoCodecState   *instate;
  /// TRUE if the negotiated input subformat is heif.
  gboolean             isheif;
  /// TRUE if the negotiated input feature is GBM.
  gboolean             isgbm;
  /// Get the buffer duration if input is variable fps and output is fixed fps.
  GstClockTime         duration;
  /// Previous timestamp saved for variable fps.
  GstClockTime         prevts;
  /// Current profile.
  GstC2Profile         profile;

  /// SPS/PPS/VPS NALs headers.
  GList                *headers;
  /// List of incomplete buffers.
  GstBufferList        *incomplete_buffers;

  /// Number of subframes contained in one buffer.
  guint32              n_subframes;

  /// Properties
  GstC2VideoRotate     rotate;
  GstC2RateControl     control_rate;
  guint32              target_bitrate;

  gint                 idr_interval;
  GstC2IntraRefresh    intra_refresh;
  guint32              bframes;

  GstC2SliceMode       slice_mode;
  guint32              slice_size;

  GstC2QuantInit       quant_init;
  GstC2QuantRanges     quant_ranges;

  gboolean             roi_quant_mode;
  GstStructure         *roi_quant_values;
  GArray               *roi_quant_boxes;

  GstC2EntropyMode     entropy_mode;
  GstC2LoopFilterMode  loop_filter_mode;
  guint32              num_ltr_frames;
  gint32               priority;
  GstC2TemporalLayer   temp_layer;
};

struct _GstC2VEncoderClass {
  GstVideoEncoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_venc_get_type (void);

G_END_DECLS

#endif // _GST_C2_VENC_H_
