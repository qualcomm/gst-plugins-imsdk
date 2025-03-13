/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "c2adec.h"

#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT c2_adec_debug
GST_DEBUG_CATEGORY_STATIC (c2_adec_debug);

#define gst_c2_adec_parent_class parent_class
G_DEFINE_TYPE (GstC2adecoder, gst_c2_adec, GST_TYPE_AUDIO_DECODER);

#define GST_AUDIO_FORMATS "{ S16LE }"

static GstStaticPadTemplate gst_c2_adec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg,"
        "mpegversion = (int) { 2, 4 },"
        "stream-format = (string) { raw, adts },")
);

static GstStaticPadTemplate gst_c2_adec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS))
);

static gboolean
gst_c2_adec_config (GstC2adecoder *c2adec)
{
  gboolean success = FALSE;
  gint rate = 0;
  gint channels = 0;
  GstAudioFormat out_format = GST_AUDIO_FORMAT_S16LE;
  GstAudioChannelPosition pos[64] = { 0, };
  GstAudioLayout layout = GST_AUDIO_LAYOUT_INTERLEAVED;

  success = gst_c2_engine_get_parameter (c2adec->engine,
      GST_C2_PARAM_OUT_SAMPLE_RATE, GPOINTER_CAST (&rate));
  if (!success) {
    GST_ERROR_OBJECT (c2adec, "Failed to get samplerate parameter!");
    return FALSE;
  }

  success = gst_c2_engine_get_parameter (c2adec->engine,
      GST_C2_PARAM_OUT_CHANNELS_COUNT, GPOINTER_CAST (&channels));
  if (!success) {
    GST_ERROR_OBJECT (c2adec, "Failed to get channels parameter!");
    return FALSE;
  }

  gst_audio_info_set_format (&c2adec->ainfo, out_format,
      rate, channels, pos);
  c2adec->ainfo.layout = layout;

  if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (c2adec),
      &c2adec->ainfo)){
    GST_ERROR_OBJECT (c2adec, "Failed to set output format!");
    return FALSE;
  }

  if (!gst_audio_decoder_negotiate (GST_AUDIO_DECODER (c2adec))) {
    GST_ERROR_OBJECT (c2adec, "Failed to negotiate caps!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2adec, "Update rate - %d, channels - %d",
      rate, channels);

  c2adec->configured = TRUE;

  return TRUE;
}

static void
gst_c2_adec_event_handler (guint type, gpointer payload, gpointer userdata)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (userdata);

  if (type == GST_C2_EVENT_EOS) {
    GST_DEBUG_OBJECT (c2adec, "Received engine EOS");
  } else if (type == GST_C2_EVENT_ERROR) {
    gint32 error = *((gint32*) userdata);
    GST_ELEMENT_ERROR (c2adec, RESOURCE, FAILED,
        ("Codec2 encountered an un-recovarable error '%x' !", error), (NULL));
  }
}

static void
gst_c2_adec_buffer_available (GstBuffer * buffer, gpointer userdata)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (userdata);
  GstFlowReturn ret = GST_FLOW_OK;

  // Unset the custom SYNC flag if present.
  GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_SYNC);

  if (gst_buffer_get_size (buffer) == 0) {
    GST_WARNING_OBJECT (c2adec, "Buffer size is zero - skipping");
    gst_buffer_unref (buffer);
    gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (c2adec), NULL, 1);
    return;
  }

  // Config the output
  // There is an issue that the aacparse reports half sample rate value
  // and one channel (in case of two channels) for
  // HE or HEv2 (high efficiency) format.
  // To solve this issue we are using the automatic detection for sample rate
  // and channels count in the decoder. At least one buffer needs to be decoded
  // to be able to read the correct values.
  if (!c2adec->configured && !gst_c2_adec_config (c2adec)) {
    GST_ERROR_OBJECT (c2adec, "Config failed!");
    return;
  }

  ret = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (c2adec), buffer, 1);
  if (ret != GST_FLOW_OK) {
    GST_LOG_OBJECT (c2adec, "Failed to finish frame! - ret - %d", ret);
    return;
  }

  GST_TRACE_OBJECT (c2adec, "Decoded samples - %d", 1);
}

static GstC2Callbacks callbacks =
    { gst_c2_adec_event_handler, gst_c2_adec_buffer_available };

static gboolean
gst_c2_adec_start (GstAudioDecoder * decoder)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (decoder);
  GST_DEBUG_OBJECT (c2adec, "Start engine");

  if ((c2adec->engine != NULL) && !gst_c2_engine_start (c2adec->engine)) {
    GST_ERROR_OBJECT (c2adec, "Failed to start engine!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2adec, "Engine started");
  return TRUE;
}

static gboolean
gst_c2_adec_stop (GstAudioDecoder * decoder)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (decoder);
  GST_DEBUG_OBJECT (c2adec, "Stop engine");

  if ((c2adec->engine != NULL) && !gst_c2_engine_stop (c2adec->engine)) {
    GST_ERROR_OBJECT (c2adec, "Failed to stop engine");
    return FALSE;
  }

  gst_audio_info_init (&c2adec->ainfo);
  c2adec->framenum = 0;

  GST_DEBUG_OBJECT (c2adec, "Engine stoped");
  return TRUE;
}

static void
gst_c2_adec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (decoder);
  GST_DEBUG_OBJECT (c2adec, "Flush engine");

  if ((c2adec->engine != NULL) && !gst_c2_engine_flush (c2adec->engine)) {
    GST_ERROR_OBJECT (c2adec, "Failed to flush engine");
    return;
  }

  GST_DEBUG_OBJECT (c2adec, "Engine flushed");
  return;
}

static gboolean
gst_c2_adec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (decoder);
  GstStructure *structure = NULL;
  const gchar *name = "c2.qti.aac.hw.decoder", *string = NULL;
  gint rate = 0;
  gint channels = 0;
  gint sample_rate_idx;
  guint8 codec_data[2];
  gboolean success = FALSE;
  GstC2Bitdepth depth = GST_C2_PCM_16;
  GstC2AACStreamFormat streamformat = GST_C2_AAC_PACKAGING_RAW;

  GST_DEBUG_OBJECT (c2adec, "Setting input caps: %" GST_PTR_FORMAT, caps);

  // Get the caps structue and set the component name.
  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (c2adec,
      "Setting input structure: %" GST_PTR_FORMAT, structure);

  if (c2adec->name != NULL) {
    g_clear_pointer (&(c2adec->name), g_free);
    g_clear_pointer (&(c2adec->engine), gst_c2_engine_free);
  }

  if (c2adec->engine == NULL) {
    c2adec->engine = gst_c2_engine_new (name, GST_C2_MODE_AUDIO_DECODE,
        &callbacks, c2adec);
    g_return_val_if_fail (c2adec->engine != NULL, FALSE);

    c2adec->name = g_strdup (name);
  }

  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);

  if ((string = gst_structure_get_string (structure, "stream-format")) != NULL) {
    if (g_str_equal (string, "adts"))
      streamformat = GST_C2_AAC_PACKAGING_ADTS;
  }

  // Get input codec data which should be send as a config buffer
  // to the decoder
  // The codec_data data is according to AudioSpecificConfig,
  // ISO/IEC 14496-3, 1.6.2.1
  if (gst_structure_has_field (structure, "codec_data")) {
    gst_structure_get (structure, "codec_data", GST_TYPE_BUFFER,
        &c2adec->codec_data_buffer, NULL);
  } else {
    // This data is needed for the codec2 decoder
    // Set in case the codec_data is not provided by caps
    sample_rate_idx =
        gst_codec_utils_aac_get_index_from_sample_rate (rate);
    // LC profile only
    codec_data[0] = ((0x02 << 3) | (sample_rate_idx >> 1));
    codec_data[1] = ((sample_rate_idx & 0x01) << 7) | (channels << 3);

    c2adec->codec_data_buffer = gst_buffer_new_and_alloc (2);
    gst_buffer_fill (c2adec->codec_data_buffer, 0, codec_data, 2);
  }

  success = gst_c2_engine_set_parameter (c2adec->engine,
      GST_C2_PARAM_OUT_BITDEPTH, GPOINTER_CAST (&depth));
  if (!success) {
    GST_ERROR_OBJECT (c2adec, "Failed to set output depth parameter!");
    return FALSE;
  }

  success = gst_c2_engine_set_parameter (c2adec->engine,
      GST_C2_PARAM_IN_AAC_FORMAT, GPOINTER_CAST (&streamformat));
  if (!success) {
    GST_ERROR_OBJECT (c2adec, "Failed to set input streamformat parameter!");
    return FALSE;
  }

  if (!gst_c2_engine_start (c2adec->engine)) {
    GST_ERROR_OBJECT (c2adec, "Failed to start engine!");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_c2_adec_finish (GstAudioDecoder * decoder)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (decoder);

  GST_DEBUG_OBJECT (c2adec, "Draining component");

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_AUDIO_DECODER_STREAM_UNLOCK (decoder);

  if (!gst_c2_engine_drain (c2adec->engine, TRUE)) {
    GST_ERROR_OBJECT (c2adec, "Failed to drain engine");
    return GST_FLOW_ERROR;
  }

  GST_AUDIO_DECODER_STREAM_LOCK (decoder);

  GST_DEBUG_OBJECT (c2adec, "Drain completed");
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_c2_adec_handle_frame (GstAudioDecoder * decoder, GstBuffer * inbuf)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (decoder);
  GstC2QueueItem item;

  // If NULL buffer is received the decoder is set in draining mode
  // Check this methon "gst_audio_decoder_set_drainable"
  // At this point we should wait all queued buffers to be processed
  if (!inbuf) {
    GST_INFO_OBJECT(c2adec, "Decoder is draining");
    gst_c2_adec_finish (decoder);
    return GST_FLOW_OK;
  }

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_AUDIO_DECODER_STREAM_UNLOCK (decoder);

  // Send the codec data buffer to the codec at the begining of the stream
  if (c2adec->framenum == 0 && c2adec->codec_data_buffer) {
    GST_BUFFER_FLAG_SET (c2adec->codec_data_buffer, GST_BUFFER_FLAG_HEADER);

    item.buffer = c2adec->codec_data_buffer;
    item.index = c2adec->framenum;
    item.userdata = NULL;

    if (!gst_c2_engine_queue (c2adec->engine, &item)) {
      GST_ERROR_OBJECT(c2adec, "Failed to send input inbuf to be emptied!");
      return GST_FLOW_ERROR;
    }
    c2adec->framenum++;
  }

  item.buffer = inbuf;
  item.index = c2adec->framenum;
  item.userdata = NULL;

  if (!gst_c2_engine_queue (c2adec->engine, &item)) {
    GST_ERROR_OBJECT(c2adec, "Failed to send input inbuf to be emptied!");
    return GST_FLOW_ERROR;
  }
  c2adec->framenum++;

  GST_TRACE_OBJECT (c2adec, "Queued %" GST_PTR_FORMAT, inbuf);

  GST_AUDIO_DECODER_STREAM_LOCK (decoder);

  return GST_FLOW_OK;
}

static void
gst_c2_adec_finalize (GObject * object)
{
  GstC2adecoder *c2adec = GST_C2_ADEC (object);

  if (c2adec->engine != NULL)
    gst_c2_engine_free (c2adec->engine);

  c2adec->engine = NULL;
  c2adec->framenum = 0;

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (c2adec));
}

static void
gst_c2_adec_class_init (GstC2adecoderClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *adec_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_c2_adec_finalize);

  gst_element_class_set_static_metadata (element,
      "Codec2 AAC Audio Decoder", "Codec/Decoder/Audio",
      "Decode AAC audio streams", "QTI");

  gst_element_class_add_static_pad_template (element,
      &gst_c2_adec_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_c2_adec_src_pad_template);

  adec_class->start = GST_DEBUG_FUNCPTR (gst_c2_adec_start);
  adec_class->stop = GST_DEBUG_FUNCPTR (gst_c2_adec_stop);
  adec_class->flush = GST_DEBUG_FUNCPTR (gst_c2_adec_flush);
  adec_class->set_format = GST_DEBUG_FUNCPTR (gst_c2_adec_set_format);
  adec_class->handle_frame = GST_DEBUG_FUNCPTR (gst_c2_adec_handle_frame);
}

static void
gst_c2_adec_init (GstC2adecoder * c2adec)
{
  c2adec->name = NULL;
  c2adec->engine = NULL;
  c2adec->framenum = 0;
  c2adec->codec_data_buffer = NULL;
  c2adec->configured = FALSE;

  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (c2adec), TRUE);
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (c2adec), TRUE);

  gst_audio_info_init (&c2adec->ainfo);

  GST_DEBUG_CATEGORY_INIT (c2_adec_debug, "qtic2adec", 0,
      "QTI c2adec decoder");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtic2adec", GST_RANK_PRIMARY,
      GST_TYPE_C2_ADEC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtic2adec,
    "Codec2 Audio Decoder",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
