/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "audio-converter-engine.h"

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
DEFINE_CONVERTER (gfloat, 1.0, gfloat);

#define GST_CAT_DEFAULT mlac_engine_debug
GST_DEBUG_CATEGORY (mlac_engine_debug);

struct _GstAudioConvEngine {
  // audio sample rate
  gint                sample_rate;
  // audio sample number
  gint                sample_number;
  // audio bytes per sample
  gint                bps;
  // mode
  gint                mode;

  // tensor type
  GstMLType           tensor_type;
  // Input Audioformat
  GstAudioFormat      format;

  // data converter function
  ConvertFunc         convert;
};

static inline void
gst_mlaconverter_engine_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (mlac_engine_debug, "mlac-engine-debug", 0,
        "audio converter engine");
    g_once_init_leave (&catonce, TRUE);
  }
}

GstAudioConvEngine *
gst_mlaconverter_engine_new (const GstStructure * settings)
{
  GstAudioConvEngine *engine;

  g_return_val_if_fail (settings != NULL, NULL);

  gst_mlaconverter_engine_initialize_debug_category ();

  engine = g_slice_new0 (GstAudioConvEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_FORMAT))
    engine->format = gst_audio_format_from_string (
        gst_structure_get_string (settings, GST_ML_AUDIO_CONVERTER_OPT_FORMAT));

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE))
    engine->tensor_type = gst_ml_type_from_string (
        gst_structure_get_string (settings,
        GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE));

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_BPS))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_BPS,
        &engine->bps);

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE,
        &engine->sample_rate);
  else
    engine->sample_rate = DEFAULT_AUDIO_SAMPLE_RATE;

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER,
        &engine->sample_number);
  else
    engine->sample_number = DEFAULT_AUDIO_SAMPLE_NUMBER;

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_MODE))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_MODE,
        &engine->mode);
  else
    engine->mode = DEFAULT_CONVERTER_MODE;

  if (engine->tensor_type == GST_ML_TYPE_FLOAT32)
    switch (engine->format)
    {
      case GST_AUDIO_FORMAT_S8:
        engine->convert = do_convert_gint8_gfloat;
        break;
      case GST_AUDIO_FORMAT_U8:
        engine->convert = do_convert_guint8_gfloat;
        break;
      case GST_AUDIO_FORMAT_S16LE:
        engine->convert = do_convert_gint16_gfloat;
        break;
      case GST_AUDIO_FORMAT_U16LE:
        engine->convert = do_convert_guint16_gfloat;
        break;
      case GST_AUDIO_FORMAT_S32LE:
        engine->convert = do_convert_gint32_gfloat;
        break;
      case GST_AUDIO_FORMAT_U32LE:
        engine->convert = do_convert_guint32_gfloat;
        break;
      case GST_AUDIO_FORMAT_F32LE:
        engine->convert = do_convert_gfloat_gfloat;
        break;
      default:
        break;
    }

  return engine;
}

void
gst_mlaconverter_engine_free (GstAudioConvEngine * engine)
{
  if (NULL == engine)
    return;

  g_slice_free (GstAudioConvEngine, engine);

  return;
}

gboolean
gst_mlaconverter_engine_process (GstAudioConvEngine * engine,
    GstAudioBuffer *audioframe, GstMLFrame *mlframe)
{
  size_t audio_num, tensor_num, process_num;
  GstMapInfo *audioinfo = &(audioframe->map_infos[0]);
  gpointer outdata = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (audioframe != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);

  audio_num = audioinfo->size / engine->bps;

  mltype = GST_ML_FRAME_TYPE (mlframe);
  outdata = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);

  if (mltype == GST_ML_TYPE_FLOAT32 &&
      engine->mode == GST_AUDIO_CONV_MODE_RAW) {
    tensor_num = GST_ML_FRAME_BLOCK_SIZE (mlframe, 0) /
        gst_ml_type_get_size (mltype);

    process_num = (audio_num >= tensor_num) ? (tensor_num) : (audio_num);

    engine->convert ((const void *)audioinfo->data, (void * )outdata,
        process_num, process_num);
  } else {
    GST_ERROR ("Supporting only FLOAT32 Type tensors");
    // not supporting other input types to model right now!
    return FALSE;
  }
  return TRUE;
}
