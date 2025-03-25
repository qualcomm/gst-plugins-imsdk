/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_AUDIO_CONVERTER_H__
#define __GST_QTI_ML_AUDIO_CONVERTER_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>
#include <gst/ml/ml-frame.h>

#include "audio-converter-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_AUDIO_CONVERTER (gst_ml_audio_converter_get_type())

#define GST_ML_AUDIO_CONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ML_AUDIO_CONVERTER, \
                              GstMLAudioConverter))

#define GST_ML_AUDIO_CONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ML_AUDIO_CONVERTER, \
                           GstMLAudioConverterClass))

#define GST_IS_ML_AUDIO_CONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ML_AUDIO_CONVERTER))

#define GST_IS_ML_AUDIO_CONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ML_AUDIO_CONVERTER))

#define GST_ML_AUDIO_CONVERTER_CAST(obj) ((GstMLAudioConverter *)(obj))

#define GST_TYPE_ML_AUDIO_CONVERSION_FEATURE \
  (gst_ml_audio_conversion_feature_get_type())

typedef struct _GstMLAudioConverter GstMLAudioConverter;
typedef struct _GstMLAudioConverterClass GstMLAudioConverterClass;

struct _GstMLAudioConverter {
  GstBaseTransform            parent;

  /// input audio information
  GstAudioInfo                *audio_info;
  /// output ml information
  GstMLInfo                   *ml_info;

  /// Buffer pool
  GstBufferPool               *outpool;

  /// Inference pipeline Stage ID
  guint                       stage_id;

  /// Conversion Engine handle
  GstAudioConvEngine          *engine;

  ///property
  guint                       sample_rate;
  GstAudioFeature             feature;
  GstStructure                *params;
};

struct _GstMLAudioConverterClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_audio_converter_get_type(void);

G_GNUC_INTERNAL GType gst_ml_audio_conversion_feature_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_AUDIO_CONVERTER_H__
