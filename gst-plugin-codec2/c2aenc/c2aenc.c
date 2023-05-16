/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "c2aenc.h"

#define GST_CAT_DEFAULT c2_aenc_debug
GST_DEBUG_CATEGORY_STATIC (c2_aenc_debug);

#define gst_c2_aenc_parent_class parent_class
G_DEFINE_TYPE (GstC2AEncoder, gst_c2_aenc, GST_TYPE_AUDIO_ENCODER);

#define GPOINTER_CAST(ptr)                ((gpointer) ptr)

#define GST_AUDIO_FORMATS "{ S16LE }"
#define SAMPLES_CNT_IN_BUFFER 1024

static GstStaticPadTemplate gst_c2_aenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS))
);

static GstStaticPadTemplate gst_c2_aenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg,"
        "mpegversion = (int) 4,"
        "stream-format = (string) { raw, adts }")
);

static gboolean
gst_c2_aenc_setup_parameters (GstC2AEncoder * c2aenc, GstAudioInfo * info)
{
  gboolean success = FALSE;

  guint32 samplerate = info->rate;
  guint32 channels = info->channels;
  GstC2Bitdepth depth = GST_C2_PCM_16;
  GstC2AACStreamFormat streamformat = GST_C2_AAC_PACKAGING_ADTS;

  GST_TRACE_OBJECT (c2aenc, "samplerate - %d", samplerate);
  GST_TRACE_OBJECT (c2aenc, "channels - %d", channels);

  success = gst_c2_engine_set_parameter (c2aenc->engine,
      GST_C2_PARAM_IN_SAMPLE_RATE, GPOINTER_CAST (&samplerate));
  if (!success) {
    GST_ERROR_OBJECT (c2aenc, "Failed to set output samplerate parameter!");
    return FALSE;
  }

  success = gst_c2_engine_set_parameter (c2aenc->engine,
      GST_C2_PARAM_IN_CHANNELS_COUNT, GPOINTER_CAST (&channels));
  if (!success) {
    GST_ERROR_OBJECT (c2aenc, "Failed to set output channels parameter!");
    return FALSE;
  }

  success = gst_c2_engine_set_parameter (c2aenc->engine,
      GST_C2_PARAM_IN_BITDEPTH, GPOINTER_CAST (&depth));
  if (!success) {
    GST_ERROR_OBJECT (c2aenc, "Failed to set output depth parameter!");
    return FALSE;
  }

  success = gst_c2_engine_set_parameter (c2aenc->engine,
      GST_C2_PARAM_OUT_AAC_FORMAT, GPOINTER_CAST (&streamformat));
  if (!success) {
    GST_ERROR_OBJECT (c2aenc, "Failed to set output streamformat parameter!");
    return FALSE;
  }

  return TRUE;
}

static void
gst_c2_aenc_event_handler (guint type, gpointer payload, gpointer userdata)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (userdata);

  if (type == GST_C2_EVENT_EOS) {
    GST_DEBUG_OBJECT (c2aenc, "Received engine EOS");
  } else if (type == GST_C2_EVENT_ERROR) {
    gint32 error = *((gint32*) userdata);
    GST_ERROR_OBJECT (c2aenc, "Received engine ERROR: '%x'", error);
  }
}

static void
gst_c2_aenc_buffer_available (GstBuffer * buffer, gpointer userdata)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (userdata);
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 index = 0;

  // Get the frame index from the buffer offset field.
  index = GST_BUFFER_OFFSET (buffer);

  guint samples_count = GPOINTER_TO_SIZE (
      g_hash_table_lookup (c2aenc->framesmap, GSIZE_TO_POINTER (index)));
  g_hash_table_remove (c2aenc->framesmap, GSIZE_TO_POINTER (index));

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_CORRUPTED)) {
    gst_buffer_unref (buffer);
    gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (c2aenc),
        NULL, samples_count);
    GST_LOG_OBJECT (c2aenc, "Buffer dropped");
    return;
  }

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_HEADER)) {
    c2aenc->headers = g_list_append (c2aenc->headers, buffer);
    return;
  } else if (c2aenc->headers != NULL) {
    gst_video_encoder_set_headers (GST_VIDEO_ENCODER (c2aenc), c2aenc->headers);
    c2aenc->headers = NULL;
  }

  // Unset the custom SYNC flag if present.
  GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_SYNC);

  if (gst_buffer_get_size (buffer) == 0) {
    GST_WARNING_OBJECT (c2aenc, "Buffer size is zero - skipping");
    gst_buffer_unref (buffer);
    gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (c2aenc),
        NULL, samples_count);
    return;
  }

  ret = gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (c2aenc),
      buffer, samples_count);
  if (ret != GST_FLOW_OK) {
    GST_LOG_OBJECT (c2aenc, "Failed to finish frame! - ret - %d", ret);
    return;
  }

  GST_TRACE_OBJECT (c2aenc, "Encoded samples - %d", samples_count);
}

static GstC2Callbacks callbacks =
    { gst_c2_aenc_event_handler, gst_c2_aenc_buffer_available };

static gboolean
gst_c2_aenc_start (GstAudioEncoder * encoder)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (encoder);
  GST_DEBUG_OBJECT (c2aenc, "Start engine");

  if ((c2aenc->engine != NULL) && !gst_c2_engine_start (c2aenc->engine)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to start engine!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2aenc, "Engine started");
  return TRUE;
}

static gboolean
gst_c2_aenc_stop (GstAudioEncoder * encoder)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (encoder);
  GST_DEBUG_OBJECT (c2aenc, "Stop engine");

  if ((c2aenc->engine != NULL) && !gst_c2_engine_drain (c2aenc->engine, TRUE)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to flush engine");
    return FALSE;
  }

  if ((c2aenc->engine != NULL) && !gst_c2_engine_stop (c2aenc->engine)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to stop engine");
    return FALSE;
  }

  g_list_free_full (c2aenc->headers, (GDestroyNotify) gst_buffer_unref);
  c2aenc->headers = NULL;

  GST_DEBUG_OBJECT (c2aenc, "Engine stoped");
  return TRUE;
}

static void
gst_c2_aenc_flush (GstAudioEncoder * encoder)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (encoder);
  GST_DEBUG_OBJECT (c2aenc, "Flush engine");

  if ((c2aenc->engine != NULL) && !gst_c2_engine_flush (c2aenc->engine)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to flush engine");
    return;
  }

  g_list_free_full (c2aenc->headers, (GDestroyNotify) gst_buffer_unref);
  c2aenc->headers = NULL;

  GST_DEBUG_OBJECT (c2aenc, "Engine flushed");
  return;
}

static gboolean
gst_c2_aenc_set_format (GstAudioEncoder * encoder, GstAudioInfo * info)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (encoder);
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  const gchar *name = NULL, *string = NULL;
  GstC2Profile profile = GST_C2_PROFILE_INVALID;
  GstC2Level level = GST_C2_LEVEL_INVALID;
  gint sample_rate_idx;
  guint8 codec_data[2];
  guint32 param = 0;
  gboolean success = FALSE;
  GstBuffer *codec_data_buffer;

  if (!gst_audio_info_is_equal (info, &c2aenc->ainfo)) {
    if (!gst_c2_aenc_stop (encoder)) {
      GST_ERROR_OBJECT (c2aenc, "Failed to stop encoder!");
      return FALSE;
    }
  }

  caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (c2aenc));
  if ((caps == NULL) || gst_caps_is_empty (caps)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to get output caps!");
    return FALSE;
  }

  // Make sure that caps have only one entry.
  caps = gst_caps_truncate (caps);

  // Get the caps structue and set the component name.
  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "audio/mpeg"))
    name = "c2.qti.aac.hw.encoder";

  if (name == NULL) {
    GST_ERROR_OBJECT (c2aenc, "Unknown component!");
    gst_caps_unref (caps);
    return FALSE;
  }

  if ((c2aenc->name != NULL) && !g_str_equal (c2aenc->name, name)) {
    g_clear_pointer (&(c2aenc->name), g_free);
    g_clear_pointer (&(c2aenc->engine), gst_c2_engine_free);
  }

  if (c2aenc->name == NULL)
    c2aenc->name = g_strdup (name);

  if (c2aenc->engine == NULL) {
    c2aenc->engine = gst_c2_engine_new (c2aenc->name, GST_C2_MODE_AUDIO_ENCODE,
        &callbacks, c2aenc);
    g_return_val_if_fail (c2aenc->engine != NULL, FALSE);
  }

  // Set the codec_data data according to AudioSpecificConfig,
  // ISO/IEC 14496-3, 1.6.2.1
  sample_rate_idx =
      gst_codec_utils_aac_get_index_from_sample_rate (info->rate);
  // LC profile only
  codec_data[0] = ((0x02 << 3) | (sample_rate_idx >> 1));
  codec_data[1] = ((sample_rate_idx & 0x01) << 7) | (info->channels << 3);
  gst_codec_utils_aac_caps_set_level_and_profile (caps, codec_data, 2);

  codec_data_buffer = gst_buffer_new_and_alloc (2);
  gst_buffer_fill (codec_data_buffer, 0, codec_data, 2);
  gst_structure_set (structure, "codec_data", GST_TYPE_BUFFER,
      codec_data_buffer, NULL);
  gst_buffer_unref (codec_data_buffer);

  gst_structure_set (structure, "rate", G_TYPE_INT, info->rate, NULL);
  gst_structure_set (structure, "channels", G_TYPE_INT, info->channels, NULL);

  // Set profile and level both in caps and component.
  if ((string = gst_structure_get_string (structure, "profile")) != NULL) {
    if (gst_structure_has_name (structure, "audio/mpeg"))
      profile = gst_c2_utils_aac_profile_from_string (string);

    if (profile == GST_C2_PROFILE_INVALID) {
      GST_ERROR_OBJECT (c2aenc, "Unsupported profile '%s'!", string);
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  if ((string = gst_structure_get_string (structure, "level")) != NULL) {
    if (gst_structure_has_name (structure, "audio/mpeg")) {
      level = gst_c2_utils_aac_level_from_string (string);
    }

    if (level == GST_C2_LEVEL_INVALID) {
      GST_ERROR_OBJECT (c2aenc, "Unsupported level '%s'!", string);
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  success = gst_c2_engine_get_parameter (c2aenc->engine,
      GST_C2_PARAM_PROFILE_LEVEL, &param);
  if (!success) {
    GST_ERROR_OBJECT (c2aenc, "Failed to get profile/level parameter!");
    gst_caps_unref (caps);
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2aenc, "profile - %d", profile);
  GST_DEBUG_OBJECT (c2aenc, "level - %d", level);

  if (profile != GST_C2_PROFILE_INVALID)
    param = (param & 0xFFFF0000) + (profile & 0xFFFF);
  else
    profile = (param & 0xFFFF);

  if (level != GST_C2_LEVEL_INVALID)
    param = (param & 0xFFFF) + ((level & 0xFFFF) << 16);
  else
    level = (param >> 16) & 0xFFFF;

  success = gst_c2_engine_set_parameter (c2aenc->engine,
      GST_C2_PARAM_PROFILE_LEVEL, GPOINTER_CAST (&param));
  if (!success) {
    GST_ERROR_OBJECT (c2aenc, "Failed to set profile/level parameter!");
    gst_caps_unref (caps);
    return FALSE;
  }

  if (gst_structure_has_name (structure, "audio/mpeg")) {
    if (profile != GST_C2_PROFILE_INVALID) {
      string = gst_c2_utils_aac_profile_to_string (profile);
      gst_structure_set (structure, "profile", G_TYPE_STRING, string, NULL);
    }

    if (level != GST_C2_LEVEL_INVALID) {
      string = gst_c2_utils_aac_level_to_string (level);
      gst_structure_set (structure, "level", G_TYPE_STRING, string, NULL);
    }
  }

  caps = gst_caps_fixate (caps);

  GST_DEBUG_OBJECT (c2aenc, "Setting output state caps: %" GST_PTR_FORMAT, caps);

  if (!gst_audio_encoder_set_output_format (encoder, caps)){
    GST_ERROR_OBJECT (c2aenc, "Failed to set output format!");
    return FALSE;
  }

  gst_audio_encoder_set_frame_samples_min (GST_AUDIO_ENCODER (encoder),
      SAMPLES_CNT_IN_BUFFER);
  gst_audio_encoder_set_frame_samples_max (GST_AUDIO_ENCODER (encoder),
      SAMPLES_CNT_IN_BUFFER);
  gst_audio_encoder_set_frame_max (GST_AUDIO_ENCODER (encoder), 1);

  if (!gst_audio_encoder_negotiate (encoder)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to negotiate caps!");
    return FALSE;
  }

  if (!gst_c2_aenc_setup_parameters (c2aenc, info)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to setup parameters!");
    return FALSE;
  }

  c2aenc->ainfo = *info;

  if (!gst_c2_engine_start (c2aenc->engine)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to start engine!");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_c2_aenc_finish (GstAudioEncoder * encoder)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (encoder);

  GST_DEBUG_OBJECT (c2aenc, "Draining component");

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);

  if (!gst_c2_engine_drain (c2aenc->engine, TRUE)) {
    GST_ERROR_OBJECT (c2aenc, "Failed to drain engine");
    return GST_FLOW_ERROR;
  }

  GST_AUDIO_ENCODER_STREAM_LOCK (encoder);

  GST_DEBUG_OBJECT (c2aenc, "Drain completed");
  return GST_FLOW_OK;
}

guint32 num = 0;
static GstFlowReturn
gst_c2_aenc_handle_frame (GstAudioEncoder * encoder, GstBuffer * inbuf)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (encoder);
  guint samples = 0;
  guint bytes_per_sample = 0;
  GstC2QueueItem item;

  // If NULL buffer is received the encoder is set in draining mode
  // Check this methon "gst_audio_encoder_set_drainable"
  // At this point we should wait all queued buffers to be processed
  if (!inbuf) {
    GST_INFO_OBJECT(c2aenc, "Encoder is draining");
    gst_c2_aenc_finish (encoder);
    return GST_FLOW_OK;
  }

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);

  // Currentrly supported only S16LE
  // If additional are added this needs to be calculated dynamically
  bytes_per_sample = 2;

  samples = gst_buffer_get_size (inbuf) / bytes_per_sample;
  samples /= c2aenc->ainfo.channels;
  GST_TRACE_OBJECT (c2aenc, "Samples queued - %d", samples);

  item.buffer = inbuf;
  item.index = c2aenc->framenum;
  item.userdata = NULL;

  if (!gst_c2_engine_queue (c2aenc->engine, &item)) {
    GST_ERROR_OBJECT(c2aenc, "Failed to send input inbuf to be emptied!");
    return GST_FLOW_ERROR;
  }

  g_hash_table_insert (c2aenc->framesmap, GSIZE_TO_POINTER (c2aenc->framenum),
      GUINT_TO_POINTER (samples));
  c2aenc->framenum++;

  GST_TRACE_OBJECT (c2aenc, "Queued %" GST_PTR_FORMAT, inbuf);

  GST_AUDIO_ENCODER_STREAM_LOCK (encoder);

  return GST_FLOW_OK;
}

static void
gst_c2_aenc_finalize (GObject * object)
{
  GstC2AEncoder *c2aenc = GST_C2_AENC (object);

  if (c2aenc->engine != NULL)
    gst_c2_engine_free (c2aenc->engine);

  g_hash_table_destroy (c2aenc->framesmap);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (c2aenc));
}

static void
gst_c2_aenc_class_init (GstC2AEncoderClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstAudioEncoderClass *aenc_class = GST_AUDIO_ENCODER_CLASS (klass);

  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_c2_aenc_finalize);

  gst_element_class_set_static_metadata (element,
      "Codec2 AAC Audio Encoder", "Codec/Encoder/Audio",
      "Encode AAC audio streams", "QTI");

  gst_element_class_add_static_pad_template (element,
      &gst_c2_aenc_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_c2_aenc_src_pad_template);

  aenc_class->start = GST_DEBUG_FUNCPTR (gst_c2_aenc_start);
  aenc_class->stop = GST_DEBUG_FUNCPTR (gst_c2_aenc_stop);
  aenc_class->flush = GST_DEBUG_FUNCPTR (gst_c2_aenc_flush);
  aenc_class->set_format = GST_DEBUG_FUNCPTR (gst_c2_aenc_set_format);
  aenc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_c2_aenc_handle_frame);
}

static void
gst_c2_aenc_init (GstC2AEncoder * c2aenc)
{
  c2aenc->name = NULL;
  c2aenc->engine = NULL;

  c2aenc->framenum = 0;
  c2aenc->framesmap = g_hash_table_new (NULL, NULL);

  gst_audio_encoder_set_drainable (GST_AUDIO_ENCODER (c2aenc), TRUE);

  GST_DEBUG_CATEGORY_INIT (c2_aenc_debug, "qtic2aenc", 0,
      "QTI c2aenc encoder");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtic2aenc", GST_RANK_PRIMARY,
      GST_TYPE_C2_AENC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtic2aenc,
    "Codec2 Audio Encoder",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
