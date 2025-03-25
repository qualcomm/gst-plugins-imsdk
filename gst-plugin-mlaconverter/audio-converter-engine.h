/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_AUDIO_CONVERTER_ENGINE_H__
#define __GST_AUDIO_CONVERTER_ENGINE_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS


#define DEFAULT_AUDIO_SAMPLE_NUMBER  (15600)
#define DEFAULT_AUDIO_SAMPLE_RATE    (16000)

#define GST_AUDIO_FEATURE_RAW_NAME    "raw"
#define GST_AUDIO_FEATURE_STFT_NAME   "stft"
#define GST_AUDIO_FEATURE_SPECTROGRAM_NAME "spectrogram"
#define GST_AUDIO_FEATURE_MFE_NAME         "mfe"
#define GST_AUDIO_FEATURE_LMFE_NAME        "lmfe"
#define GST_AUDIO_FEATURE_MFCC_NAME        "mfcc"

/**
 * GST_AUDIO_CONVERTER_OPT_INCAPS
 * #GST_TYPE_CAPS: A fixated set of Audio input caps. converter will expect
 *                 to receive Audio frames with the fixated caps layout for
 *                 processing.
 * Default: NULL
 */
#define GST_AUDIO_CONVERTER_OPT_INCAPS "audiocaps"

/**
 * GST_AUDIO_CONVERTER_OPT_MLCAPS
 * #GST_TYPE_CAPS: A fixated set of ML output caps. converter will expect
 *                 to figure out some of the preprocessing information like
 *                 output tensor type etc.
 * Default: NULL
 */
#define GST_AUDIO_CONVERTER_OPT_MLCAPS "mlcaps"

/**
 * GST_AUDIO_CONVERTER_OPT_FEATURE
 * #G_TYPE_STRING
 * Audio Preprocessing feature that converter needs to run on the Audio samples.
 */

#define GST_AUDIO_CONVERTER_OPT_FEATURE "feature"
/**
 * GST_AUDIO_CONVERTER_OPT_PARAMS
 *
 * #G_TYPE_STRING
 * preprocessor specific parameters. Converted back to GstStructure
 */
#define GST_AUDIO_CONVERTER_OPT_PARAMS "parameters"

/**
 * GstAudioFeature
 * GST_AUDIO_FEATURE_RAW : the raw samples are produced
 * GST_AUDIO_FEATURE_STFT : Short time Fourier Transformation of raw audio
 * GST_AUDIO_FEATURE_SPECTROGRAM: Energy mel bank representation of Audio signal
 * GST_AUDIO_FEATURE_MFE: Energy representation filtered with coefficients
 * GST_AUDIO_FEATURE_LMFE: log representation of mfe, representation
 *    closer to human auditory system.
 * GST_AUDIO_FEATURE_MFCC: Compute MFCC features from an audio signal
 *
 * Defines the kind of audio features available to a preprocessor
 */
typedef enum
{
  GST_AUDIO_FEATURE_UNKNOWN,
  GST_AUDIO_FEATURE_RAW,
  GST_AUDIO_FEATURE_STFT,
  GST_AUDIO_FEATURE_SPECTROGRAM,
  // MFE is also known as MelSpectrogram
  GST_AUDIO_FEATURE_MFE,
  // LMFE is also known as LogMelSpectrogram
  GST_AUDIO_FEATURE_LMFE,
  GST_AUDIO_FEATURE_MFCC,
} GstAudioFeature;

typedef struct _GstAudioConvEngine GstAudioConvEngine;

GST_AUDIO_API
GstAudioConvEngine * gst_mlaconverter_engine_new (const GstStructure * settings);

GST_AUDIO_API
void gst_mlaconverter_engine_free (GstAudioConvEngine * engine);

GST_AUDIO_API
gboolean gst_mlaconverter_engine_process (GstAudioConvEngine * engine,
    GstAudioBuffer * audioframe, GstMLFrame * mlframe);

GST_AUDIO_API
const gchar * gst_audio_feature_to_string (GstAudioFeature);

GST_AUDIO_API
GstAudioFeature gst_audio_feature_from_string (const gchar * feature);

G_END_DECLS

#endif // __GST_AUDIO_CONVERTER_ENGINE__
