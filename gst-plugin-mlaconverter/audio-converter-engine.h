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
#define DEFAULT_AUDIO_BPS            (4)
#define GST_AUDIO_CONV_MODE_RAW      (0)
#define GST_AUDIO_CONV_MODE_MELSPECTROGRAM (1)
/*
* GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE
*
* G_TYPE_INT
* Pass sample rate to converter
* default value is DEFAULT_AUDIO_SAMPLE_RATE
*/
#define GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE "rate"

/*
* GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER
*
* G_TYPE_INT
* Pass sample numbers that converter has to work on
* default value is DEFAULT_AUDIO_SAMPLE_NUMBER
*/
#define GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER "sample-number"

/*
* GST_ML_AUDIO_CONVERTER_OPT_BPS
*
* G_TYPE_INT
* Pass bytes per sample to converter
* default value is DEFAULT_AUDIO_BPS
*/
#define GST_ML_AUDIO_CONVERTER_OPT_BPS "bps"

/*
* GST_ML_AUDIO_CONVERTER_OPT_FORMAT
*
* G_TYPE_STRING
* Pass format string of sample to converter
*/
#define GST_ML_AUDIO_CONVERTER_OPT_FORMAT "format"

/*
* GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE
*
* G_TYPE_STRING
* Pass type of expected tensors to converter
*/
#define GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE "tensortype"

/*
* GST_ML_AUDIO_CONVERTER_OPT_MODE
*
* G_TYPE_INT
* set the conversion mode
*/
#define GST_ML_AUDIO_CONVERTER_OPT_MODE "mode"

#define DEFAULT_CONVERTER_MODE (GST_AUDIO_CONV_MODE_RAW)

typedef struct _GstAudioConvEngine GstAudioConvEngine;

GST_AUDIO_API GstAudioConvEngine *
gst_mlaconverter_engine_new (const GstStructure * settings);

GST_AUDIO_API void
gst_mlaconverter_engine_free (GstAudioConvEngine * engine);

GST_AUDIO_API gboolean
gst_mlaconverter_engine_process (GstAudioConvEngine * engine,
    GstAudioBuffer * audioframe, GstMLFrame * mlframe);

G_END_DECLS

#endif // __GST_AUDIO_CONVERTER_ENGINE__
