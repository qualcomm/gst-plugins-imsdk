/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_C2_ADEC_H_
#define _GST_C2_ADEC_H_

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include <gst/audio/audio.h>
#include <gst/allocators/allocators.h>
#include <gst/pbutils/codec-utils.h>

#include "c2-engine/c2-engine.h"
#include "c2-engine/c2-engine-params.h"

G_BEGIN_DECLS

#define GST_TYPE_C2_ADEC (gst_c2_adec_get_type())
#define GST_C2_ADEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_C2_ADEC, GstC2adecoder))
#define GST_C2_ADEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_C2_ADEC, GstC2adecoderClass))
#define GST_IS_C2_ADEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_C2_ADEC))
#define GST_IS_C2_ADEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_C2_ADEC))
#define GST_C2_ADEC_CAST(obj) ((GstC2adecoder *)(obj))

typedef struct _GstC2adecoder GstC2adecoder;
typedef struct _GstC2adecoderClass GstC2adecoderClass;

struct _GstC2adecoder {
  GstAudioDecoder parent;

  /// The name of the codec component
  gchar           *name;
  /// Codec engine handle
  GstC2Engine     *engine;
  /// Contains audio info data as bitrate and channels number
  GstAudioInfo    ainfo;
  /// Contains codec data buffer with bitrate and channels number
  GstBuffer       *codec_data_buffer;
  /// Frame number counter
  guint64         framenum;
  /// Output configured flag
  gboolean        configured;

  /// Properties
};

struct _GstC2adecoderClass {
  GstAudioDecoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_adec_get_type (void);

G_END_DECLS

#endif // _GST_C2_ADEC_H_
