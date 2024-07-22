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

typedef void (* ConvertFunc) (const void * s, void * d, size_t in, size_t out);

#define DEFINE_CONVERTER(srctype, max_val, desttype)                        \
static void                                                                 \
do_convert_##srctype##_##desttype (const void * s, void * d,                \
    size_t in, size_t out)                                                  \
{                                                                           \
  size_t i = (out < in) ? out : in;                                         \
  const srctype *src = (srctype *)s;                                        \
  desttype *dest = (desttype *)d;                                           \
  while (i-- > 0) {                                                         \
    *dest++ = ((desttype)(*src++)) / (max_val);                             \
  }                                                                         \
  while (in < out) {                                                        \
    *dest++ = 0.0;                                                          \
    in++;                                                                   \
  }                                                                         \
} extern void glib_dummy_decl (void)

DEFINE_CONVERTER (gint8, G_MAXINT8, gfloat);
DEFINE_CONVERTER (guint8, G_MAXUINT8, gfloat);
DEFINE_CONVERTER (gint16, G_MAXINT16, gfloat);
DEFINE_CONVERTER (guint16, G_MAXUINT16, gfloat);
DEFINE_CONVERTER (gint32, G_MAXINT32, gfloat);
DEFINE_CONVERTER (guint32, G_MAXUINT32, gfloat);

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

GST_API gpointer
gst_mlaconverter_engine_new (const GstStructure * settings);

GST_API void
gst_mlaconverter_engine_free (gpointer engine);

GST_API gboolean
gst_mlaconverter_engine_process (gpointer engine,
    GstAudioBuffer * audioframe, GstMLFrame * mlframe);

G_END_DECLS

#endif // __GST_AUDIO_CONVERTER_ENGINE__
