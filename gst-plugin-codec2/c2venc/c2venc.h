/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
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
  GstC2VideoFlip       flip;
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
  gint32               vbv_delay;
#if (CODEC2_CONFIG_VERSION_MAJOR == 2 && CODEC2_CONFIG_VERSION_MINOR == 1)
  GstC2HdrMode         hdr_mode;
#endif // (CODEC2_CONFIG_VERSION_MAJOR == 2 && CODEC2_CONFIG_VERSION_MINOR == 1)
};

struct _GstC2VEncoderClass {
  GstVideoEncoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_venc_get_type (void);

G_END_DECLS

#endif // _GST_C2_VENC_H_
