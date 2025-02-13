/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "mlaconverter-engine.h"

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
  ConvertFunc         do_convert;
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

gpointer
gst_mlaconverter_engine_new (const GstStructure * settings)
{
  GstAudioConvEngine *instance;

  g_return_val_if_fail (settings != NULL, NULL);

  gst_mlaconverter_engine_initialize_debug_category ();

  instance = g_slice_new0 (GstAudioConvEngine);
  g_return_val_if_fail (instance != NULL, NULL);

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_FORMAT))
    instance->format = gst_audio_format_from_string (
        gst_structure_get_string (settings, GST_ML_AUDIO_CONVERTER_OPT_FORMAT));

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE))
    instance->tensor_type = gst_ml_type_from_string (
        gst_structure_get_string (settings,
        GST_ML_AUDIO_CONVERTER_OPT_TENSORTYPE));

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_BPS))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_BPS,
        &instance->bps);

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_RATE,
        &instance->sample_rate);
  else
    instance->sample_rate = DEFAULT_AUDIO_SAMPLE_RATE;

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_SAMPLE_NUMBER,
        &instance->sample_number);
  else
    instance->sample_number = DEFAULT_AUDIO_SAMPLE_NUMBER;

  if (gst_structure_has_field (settings, GST_ML_AUDIO_CONVERTER_OPT_MODE))
    gst_structure_get_int (settings, GST_ML_AUDIO_CONVERTER_OPT_MODE,
        &instance->mode);
  else
    instance->mode = DEFAULT_CONVERTER_MODE;

  if (instance->tensor_type == GST_ML_TYPE_FLOAT32)
    switch (instance->format)
    {
      case GST_AUDIO_FORMAT_S8:
        instance->do_convert = do_convert_gint8_gfloat;
        break;
      case GST_AUDIO_FORMAT_U8:
        instance->do_convert = do_convert_guint8_gfloat;
        break;
      case GST_AUDIO_FORMAT_S16LE:
        instance->do_convert = do_convert_gint16_gfloat;
        break;
      case GST_AUDIO_FORMAT_U16LE:
        instance->do_convert = do_convert_guint16_gfloat;
        break;
      case GST_AUDIO_FORMAT_S32LE:
        instance->do_convert = do_convert_gint32_gfloat;
        break;
      case GST_AUDIO_FORMAT_U32LE:
        instance->do_convert = do_convert_guint32_gfloat;
        break;
      default:
        break;
    }

  return (gpointer) instance;
}

void
gst_mlaconverter_engine_free (gpointer engine)
{
  if (NULL == engine)
    return;

  GstAudioConvEngine * instance = (GstAudioConvEngine *)engine;

  g_slice_free (GstAudioConvEngine, instance);

  return;
}

gboolean
gst_mlaconverter_engine_process (gpointer engine,
    GstAudioBuffer *audioframe, GstMLFrame *mlframe)
{
  size_t audio_num, tensor_num, process_num;
  GstMapInfo *audioinfo = &(audioframe->map_infos[0]);
  GstAudioConvEngine *instance = (GstAudioConvEngine *)engine;
  gpointer outdata = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;

  g_return_val_if_fail (instance != NULL, FALSE);
  g_return_val_if_fail (audioframe != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);

  audio_num = audioinfo->size / instance->bps;

  mltype = GST_ML_FRAME_TYPE (mlframe);
  outdata = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);

  if (mltype == GST_ML_TYPE_FLOAT32 &&
      instance->mode == GST_AUDIO_CONV_MODE_RAW) {
    tensor_num = GST_ML_FRAME_BLOCK_SIZE (mlframe, 0) /
        gst_ml_type_get_size (mltype);

    process_num = (audio_num >= tensor_num) ? (tensor_num) : (audio_num);

    instance->do_convert ((const void *)audioinfo->data, (void * )outdata,
        process_num, process_num);
  } else {
    GST_ERROR ("Supporting only FLOAT32 Type tensors");
    // not supporting other input types to model right now!
    return FALSE;
  }
  return TRUE;
}
