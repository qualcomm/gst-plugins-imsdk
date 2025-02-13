/*
* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-3-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QTI_ML_AUDIO_CLASSIFICATION_H__
#define __GST_QTI_ML_AUDIO_CLASSIFICATION_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/ml/ml-info.h>
#include <gst/video/video.h>
#include <gst/ml/ml-module-audio-classification.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_AUDIO_CLASSIFICATION (gst_ml_audio_classification_get_type())
#define GST_ML_AUDIO_CLASSIFICATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ML_AUDIO_CLASSIFICATION, \
                              GstMLAudioClassification))
#define GST_ML_AUDIO_CLASSIFICATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ML_AUDIO_CLASSIFICATION, \
                           GstMLAudioClassificationClass))
#define GST_IS_ML_AUDIO_CLASSIFICATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ML_AUDIO_CLASSIFICATION))
#define GST_IS_ML_AUDIO_CLASSIFICATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ML_AUDIO_CLASSIFICATION))
#define GST_ML_AUDIO_CLASSIFICATION_CAST(obj) ((GstMLAudioClassification *)(obj))

typedef struct _GstMLAudioClassification GstMLAudioClassification;
typedef struct _GstMLAudioClassificationClass GstMLAudioClassificationClass;

struct _GstMLAudioClassification {
  GstBaseTransform  parent;

  GstMLInfo         *mlinfo;

  // Output mode (video or text)
  guint             mode;

  /// Buffer pools.
  GstBufferPool     *outpool;

  /// Tensor processing module.
  GstMLModule       *module;
  /// Array with predictions from the module post-processing.
  GArray            *predictions;

  /// Cairo surfaces and contexts mapped for each buffer.
  GHashTable        *surfaces;
  GHashTable        *contexts;

  /// Properties.
  gint              mdlenum;
  gchar             *labels;
  guint             n_results;
  gdouble           threshold;
};

struct _GstMLAudioClassificationClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_audio_classification_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_AUDIO_CLASSIFICATION_H__
