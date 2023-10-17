
/*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "c2vdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_c2_vdec_debug_category);
#define GST_CAT_DEFAULT gst_c2_vdec_debug_category

#define gst_c2_vdec_parent_class parent_class
G_DEFINE_TYPE (GstC2VDecoder, gst_c2_vdec, GST_TYPE_VIDEO_DECODER);

#define GPOINTER_CAST(ptr)          ((gpointer) ptr)

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_VIDEO_FORMATS "{ NV12, NV12_10LE32, P010_10LE }"

enum
{
  PROP_0,
  PROP_SECURE
};

static GstStaticPadTemplate gst_c2_vdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au };"
        "video/x-h265,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au };"
        "video/mpeg,"
        "mpegversion = (int)2;"
        "video/x-vp8;"
        "video/x-vp9")
);

static GstStaticPadTemplate gst_c2_vdec_src_pad_template =
GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
);

static gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

static GstVideoFormat
gst_c2_vdec_get_output_format (GstC2VDecoder * c2vdec,
    GstStructure *structure, GstVideoFormat format)
{
  const gchar *chroma_format = NULL;
  guint bit_depth_luma = 0, bit_depth_chroma = 0;

  chroma_format = gst_structure_get_string (structure, "chroma-format");
  gst_structure_get_uint (structure, "bit-depth-luma", &bit_depth_luma);
  gst_structure_get_uint (structure, "bit-depth-chroma", &bit_depth_chroma);

  if (chroma_format == NULL && bit_depth_luma == 0 && bit_depth_chroma == 0) {
    //If static HDR10 info is presentin the caps, then bit-depth is 10
    if (gst_structure_has_field (structure, "mastering-display-info")) {
      bit_depth_luma = 10;
      bit_depth_chroma = 10;
      chroma_format = "4:2:0";
    } else if (gst_structure_has_name (structure, "video/x-vp9") ||
        gst_structure_has_name (structure, "video/x-vp8")) {
      //vp8 and vp9 caps does not have chroma-format, bit-depth fields
      bit_depth_luma = 8;
      bit_depth_chroma = 8;
      chroma_format = "4:2:0";
      //TODO: for vp8 and vp9 code is assuming NV12 which may not be true
      //needs to be fixed
    }
  }

  if (chroma_format == NULL || bit_depth_luma == 0 || bit_depth_chroma == 0) {
    GST_ERROR_OBJECT (c2vdec, "Unable to get chroma-format or bit-depth");
    return GST_VIDEO_FORMAT_UNKNOWN;
  } else if (g_strcmp0 (chroma_format, "4:2:0") != 0) {
    GST_ERROR_OBJECT (c2vdec, "Unsupported chroma-format %s", chroma_format);
    return GST_VIDEO_FORMAT_UNKNOWN;
  }

  if (bit_depth_luma == 8 && bit_depth_chroma == 8) {
    format = GST_VIDEO_FORMAT_NV12;
  } else if (bit_depth_luma == 10 && bit_depth_chroma == 10) {
    if (format != GST_VIDEO_FORMAT_NV12_10LE32 && !c2vdec->isubwc) {
      format = GST_VIDEO_FORMAT_P010_10LE;
    } else if (format == GST_VIDEO_FORMAT_NV12_10LE32 && c2vdec->isubwc) {
      format = GST_VIDEO_FORMAT_NV12_10LE32;
    } else {
      GST_ERROR_OBJECT (c2vdec, "Unsupported format");
      return GST_VIDEO_FORMAT_UNKNOWN;
    }
  }

  return format;
}

static gboolean
gst_c2_vdec_setup_parameters (GstC2VDecoder * c2vdec,
    GstVideoCodecState * instate, GstVideoCodecState * outstate)
{
  GstVideoInfo *info = &outstate->info;
  GstC2PixelInfo pixinfo = { GST_VIDEO_FORMAT_UNKNOWN, FALSE };
  GstC2Resolution resolution = { 0, 0 };
  gboolean success = FALSE;

  pixinfo.format = GST_VIDEO_INFO_FORMAT (info);
  pixinfo.isubwc = c2vdec->isubwc;

  success = gst_c2_engine_set_parameter (c2vdec->engine,
      GST_C2_PARAM_OUT_FORMAT, GPOINTER_CAST (&pixinfo));
  if (!success) {
    GST_ERROR_OBJECT (c2vdec, "Failed to set output format parameter!");
    return FALSE;
  }

  resolution.width = GST_VIDEO_INFO_WIDTH (info);
  resolution.height = GST_VIDEO_INFO_HEIGHT (info);

  success = gst_c2_engine_set_parameter (c2vdec->engine,
      GST_C2_PARAM_OUT_RESOLUTION, GPOINTER_CAST (&resolution));
  if (!success) {
    GST_ERROR_OBJECT (c2vdec, "Failed to set output resolution parameter!");
    return FALSE;
  }

  success = gst_c2_engine_set_parameter (c2vdec->engine,
      GST_C2_PARAM_COLOR_ASPECTS_TUNING, GPOINTER_CAST (&info->colorimetry));
  if (!success) {
    GST_ERROR_OBJECT (c2vdec, "Failed to set Color Aspects parameter!");
    return FALSE;
  }

#if (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)
  GstStructure * structure = gst_caps_get_structure (instate->caps, 0);

  if (gst_structure_has_field (structure, "mastering-display-info") ||
        gst_structure_has_field (structure, "content-light-level")) {
    GstC2HdrStaticMetadata hdrstaticinfo = { 0, };
    gboolean success = FALSE;

    success |=
        gst_video_mastering_display_info_from_caps (&hdrstaticinfo.mdispinfo,
            instate->caps);
    success |=
        gst_video_content_light_level_from_caps(&hdrstaticinfo.clightlevel,
            instate->caps);

    if (success) {
      success = gst_c2_engine_set_parameter (c2vdec->engine,
          GST_C2_PARAM_HDR_STATIC_METADATA, GPOINTER_CAST (&hdrstaticinfo));
      if (!success) {
        GST_ERROR_OBJECT (c2vdec, "Failed to set Hdr static metadata parameter!");
        return FALSE;
      }
    }
  }
#endif // (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)

#if defined(CODEC2_CONFIG_VERSION_1_0)
  gdouble framerate = 0.0;
  gst_util_fraction_to_double (GST_VIDEO_INFO_FPS_N (info),
      GST_VIDEO_INFO_FPS_D (info), &framerate);

  success = gst_c2_engine_set_parameter (c2vdec->engine,
      GST_C2_PARAM_IN_FRAMERATE, GPOINTER_CAST (&framerate));
  if (!success) {
    GST_ERROR_OBJECT (c2vdec, "Failed to set input framerate parameter!");
    return FALSE;
  }
#endif // CODEC2_CONFIG_VERSION_1_0

  return TRUE;
}

static void
gst_c2_vdec_event_handler (guint type, gpointer payload, gpointer userdata)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (userdata);

  if (type == GST_C2_EVENT_EOS) {
    GST_DEBUG_OBJECT (c2vdec, "Received engine EOS");
  } else if (type == GST_C2_EVENT_ERROR) {
    guint32 error = *((guint32*) payload);
    GST_ERROR_OBJECT (c2vdec, "Received engine ERROR: '%u'", error);
  } else if (type == GST_C2_EVENT_DROP) {
    guint64 index = *((guint64*) payload);
    GstVideoCodecFrame *frame = NULL;

    GST_DEBUG_OBJECT (c2vdec, "Received engine drop frame: %lu", index);

    frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (c2vdec), index);
    if (frame == NULL) {
      GST_ERROR_OBJECT (c2vdec, "Failed to get decoder frame with index %"
          G_GUINT64_FORMAT, index);
      return;
    }
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (c2vdec), frame);
    gst_video_codec_frame_unref (frame);
  }
}

static void
gst_c2_vdec_buffer_available (GstBuffer * buffer, gpointer userdata)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (userdata);
  GstVideoCodecFrame *frame = NULL;
  GstVideoMeta *vmeta = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 index = 0;

  // Get the frame index from the buffer offset field.
  index = GST_BUFFER_OFFSET (buffer);

  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (c2vdec), index);
  if (frame == NULL) {
    GST_ERROR_OBJECT (c2vdec, "Failed to get decoder frame with index %"
        G_GUINT64_FORMAT, index);
    gst_buffer_unref (buffer);
    return;
  }

  GST_LOG_OBJECT (c2vdec, "Frame number : %d, pts: %" GST_TIME_FORMAT
      ", dts: %" GST_TIME_FORMAT, frame->system_frame_number,
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->dts));

  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_SYNC_AFTER);

  frame->output_buffer = buffer;
  gst_video_codec_frame_unref (frame);

  // TODO: Renegotiate output state caps for resolution change using video meta
  // as upstream parser plugins are not able to provide this information through
  // sink caps

  vmeta = gst_buffer_get_video_meta (buffer);

  if (vmeta->width != c2vdec->outstate->info.width ||
      vmeta->height != c2vdec->outstate->info.height) {
    GstVideoCodecState *outstate = NULL;
    const GstCapsFeatures *features = NULL;

    GST_DEBUG_OBJECT (c2vdec, "Resolution changed from %dx%d to %ux%u",
        c2vdec->outstate->info.width, c2vdec->outstate->info.height,
        vmeta->width, vmeta->height);

    outstate = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (c2vdec),
        c2vdec->outstate->info.finfo->format, vmeta->width, vmeta->height, NULL);

    gst_caps_unref (outstate->caps);
    outstate->caps = gst_video_info_to_caps (&outstate->info);

    if (features = gst_caps_get_features (c2vdec->outstate->caps, 0))
      gst_caps_set_features (outstate->caps, 0,
          gst_caps_features_copy (features));

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (c2vdec))) {
      GST_ERROR_OBJECT (c2vdec, "Failed to negotiate caps!");
      gst_video_codec_state_unref (outstate);
      gst_buffer_unref (buffer);
      return;
    }

    GST_DEBUG_OBJECT (c2vdec, "Renegotiated output state caps: %" GST_PTR_FORMAT,
        outstate->caps);

    gst_video_codec_state_unref (c2vdec->outstate);
    c2vdec->outstate = outstate;
  }

  GST_TRACE_OBJECT (c2vdec, "Decoded %" GST_PTR_FORMAT, buffer);
  ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (c2vdec), frame);

  if (ret != GST_FLOW_OK) {
    GST_LOG_OBJECT (c2vdec, "Failed to finish frame!");
    return;
  }
}

static GstC2Callbacks callbacks =
    { gst_c2_vdec_event_handler, gst_c2_vdec_buffer_available };

static gboolean
gst_c2_vdec_start (GstVideoDecoder * decoder)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (decoder);
  GST_DEBUG_OBJECT (c2vdec, "Start engine");

  if ((c2vdec->engine != NULL) && !gst_c2_engine_start (c2vdec->engine)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to start engine!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2vdec, "Engine started");
  return TRUE;
}

static gboolean
gst_c2_vdec_stop (GstVideoDecoder * decoder)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (decoder);
  GST_DEBUG_OBJECT (c2vdec, "Stop engine");

  if ((c2vdec->engine != NULL) && !gst_c2_engine_drain (c2vdec->engine, TRUE)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to drain engine");
    return FALSE;
  }

  if ((c2vdec->engine != NULL) && !gst_c2_engine_stop (c2vdec->engine)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to stop engine");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2vdec, "Engine stopped");
  return TRUE;
}

static gboolean
gst_c2_vdec_flush (GstVideoDecoder * decoder)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (decoder);
  GST_DEBUG_OBJECT (c2vdec, "Flush engine");

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  if ((c2vdec->engine != NULL) && !gst_c2_engine_flush (c2vdec->engine)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to flush engine");
    return FALSE;
  }

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  GST_DEBUG_OBJECT (c2vdec, "Engine flushed");
  return TRUE;
}

static gboolean
gst_c2_vdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (decoder);
  GstVideoCodecState *outstate = NULL;
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  gchar *name = NULL;
  const gchar *string = NULL;
  gint width = 0, height = 0;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (c2vdec, "Setting new caps %" GST_PTR_FORMAT, state->caps);

  caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (c2vdec));

  structure = gst_caps_get_structure (caps, 0);
  c2vdec->isubwc = gst_caps_has_compression (caps, "ubwc");

  if ((string = gst_structure_get_string (structure, "format")) != NULL)
    format = gst_video_format_from_string (string);

  if ((caps != NULL) && !gst_caps_is_empty (caps) && gst_caps_is_fixed (caps)) {
    success = (format != GST_VIDEO_FORMAT_UNKNOWN) ? TRUE : FALSE;
    success &= gst_structure_get_int (structure, "width", &width);
    success &= gst_structure_get_int (structure, "height", &height);
  } else {
    structure = gst_caps_get_structure (state->caps, 0);

    success = gst_structure_get_int (structure, "width", &width);
    success &= gst_structure_get_int (structure, "height", &height);
    format = gst_c2_vdec_get_output_format (c2vdec, structure, format);
    success &= (format != GST_VIDEO_FORMAT_UNKNOWN) ? TRUE : FALSE;
  }

  if (caps != NULL)
    gst_caps_unref (caps);

  if (!success) {
    GST_ERROR_OBJECT (c2vdec, "Failed to extract width, height or/and format!");
    return FALSE;
  }

  if (c2vdec->outstate && (format != c2vdec->outstate->info.finfo->format)) {
    GST_INFO_OBJECT (c2vdec, "Format changed from %s to %s",
        gst_video_format_to_string (c2vdec->outstate->info.finfo->format),
        gst_video_format_to_string (format));

    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

    if ((c2vdec->engine != NULL) && !gst_c2_engine_stop (c2vdec->engine)) {
      GST_ERROR_OBJECT (c2vdec, "Failed to stop engine");
      return FALSE;
    }

    GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  }

  GST_DEBUG_OBJECT (c2vdec, "Setting output width: %d, height: %d, format: %s",
      width, height, gst_video_format_to_string (format));

  outstate =
      gst_video_decoder_set_output_state (decoder, format, width, height, state);

  // At this point state->caps is NULL.
  if (outstate->caps)
    gst_caps_unref (outstate->caps);

  // Try to negotiate with caps feature.
  caps = gst_video_info_to_caps (&outstate->info);
  gst_caps_set_features (caps, 0,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));

  outstate->caps = gst_pad_peer_query_caps (decoder->srcpad, caps);
  gst_caps_unref (caps);

  // In case this fails fallback to caps without features.
  if (!outstate->caps || gst_caps_is_empty (outstate->caps)) {
    GST_DEBUG_OBJECT (c2vdec, "Failed to query caps with feature %s",
        GST_CAPS_FEATURE_MEMORY_GBM);

    if (outstate->caps)
      gst_caps_replace (&outstate->caps, NULL);
  }

  if (!gst_video_decoder_negotiate (decoder)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to negotiate caps!");
    gst_video_codec_state_unref (outstate);
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2vdec, "Output state caps: %" GST_PTR_FORMAT, outstate->caps);

  // If there is no change in the caps return immediatelly.
  if ((c2vdec->outstate != NULL) &&
      gst_caps_can_intersect (outstate->caps, c2vdec->outstate->caps)) {
    gst_video_codec_state_unref (outstate);
    return TRUE;
  }

  if (c2vdec->outstate != NULL)
    gst_video_codec_state_unref (c2vdec->outstate);

  c2vdec->outstate = outstate;

  // Extract the component name from the input state caps.
  structure = gst_caps_get_structure (state->caps, 0);

  if (gst_structure_has_name (structure, "video/x-h264"))
    name = "c2.qti.avc.decoder";
  else if (gst_structure_has_name (structure, "video/x-h265"))
    name = "c2.qti.hevc.decoder";
  else if (gst_structure_has_name (structure, "video/x-vp8"))
    name = "c2.qti.vp8.decoder";
  else if (gst_structure_has_name (structure, "video/x-vp9"))
    name = "c2.qti.vp9.decoder";
  else if (gst_structure_has_name (structure, "video/mpeg"))
    name = "c2.qti.mpeg2.decoder";

  if (name == NULL) {
    GST_ERROR_OBJECT (c2vdec, "Unknown component!");
    return FALSE;
  }

  if (c2vdec->secure)
    name = g_strconcat(name, ".secure", NULL);

  if ((c2vdec->name != NULL) && !g_str_equal (c2vdec->name, name)) {
    g_clear_pointer (&(c2vdec->name), g_free);
    g_clear_pointer (&(c2vdec->engine), gst_c2_engine_free);
  }

  if (c2vdec->name == NULL)
    c2vdec->name = g_strdup (name);

  if (c2vdec->engine == NULL) {
    c2vdec->engine = gst_c2_engine_new (c2vdec->name, &callbacks, c2vdec);
    g_return_val_if_fail (c2vdec->engine != NULL, FALSE);
  }

  if (!gst_c2_vdec_setup_parameters (c2vdec, state, c2vdec->outstate)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to setup parameters!");
    return FALSE;
  }

  if (!gst_c2_engine_start (c2vdec->engine)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to start engine!");
    return FALSE;
  }

  if (c2vdec->secure)
    g_free (name);

  return TRUE;
}

static GstFlowReturn
gst_c2_vdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (decoder);

  // This mutex was locked in the base class before call this function
  // Needs to be unlocked in case we reach the maximum number of pending frames.
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  GST_LOG_OBJECT (c2vdec, "Frame number : %d, pts: %" GST_TIME_FORMAT
      ", dts: %" GST_TIME_FORMAT, frame->system_frame_number,
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->dts));

  if (!gst_c2_engine_queue (c2vdec->engine, frame)) {
    GST_ERROR_OBJECT(c2vdec, "Failed to send input frame to be emptied!");
    return GST_FLOW_ERROR;
  }

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  GST_TRACE_OBJECT (c2vdec, "Queued %" GST_PTR_FORMAT, frame->input_buffer);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_c2_vdec_finish (GstVideoDecoder * decoder)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (decoder);

  GST_DEBUG_OBJECT (c2vdec, "Draining component");

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  if (!gst_c2_engine_drain (c2vdec->engine, TRUE)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to drain engine");
    return GST_FLOW_ERROR;
  }

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  GST_DEBUG_OBJECT (c2vdec, "Drain completed");
  return GST_FLOW_OK;
}

static void
gst_c2_vdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
 GstC2VDecoder *c2vdec = GST_C2_VDEC (object);

  switch (prop_id) {
    case PROP_SECURE:
      c2vdec->secure = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_c2_vdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (object);

  switch (prop_id) {
    case PROP_SECURE:
      g_value_set_boolean (value, c2vdec->secure);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_c2_vdec_finalize (GObject * object)
{
  GstC2VDecoder *c2vdec = GST_C2_VDEC (object);

  if (c2vdec->outstate)
    gst_video_codec_state_unref (c2vdec->outstate);

  if (c2vdec->engine != NULL)
    gst_c2_engine_free (c2vdec->engine);

  g_free (c2vdec->name);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (c2vdec));
}

static void
gst_c2_vdec_class_init (GstC2VDecoderClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *vdec_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_c2_vdec_finalize);
  gobject->set_property = GST_DEBUG_FUNCPTR (gst_c2_vdec_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_c2_vdec_get_property);

  gst_element_class_set_static_metadata (element,
      "Codec2 H.264/H.265/VP8/VP9/MPEG Video Decoder", "Codec/Decoder/Video",
      "Decode H.264/H.265/VP8/VP9/MPEG video streams", "QTI");

  gst_element_class_add_static_pad_template (element,
      &gst_c2_vdec_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_c2_vdec_src_pad_template);

  g_object_class_install_property (gobject, PROP_SECURE,
    g_param_spec_boolean ("secure", "Secure", "Secure Playback"
        "If property is enabled it will select the codec2 secure component",
        FALSE, G_PARAM_READWRITE));

  vdec_class->start = GST_DEBUG_FUNCPTR (gst_c2_vdec_start);
  vdec_class->stop = GST_DEBUG_FUNCPTR (gst_c2_vdec_stop);
  vdec_class->flush = GST_DEBUG_FUNCPTR (gst_c2_vdec_flush);
  vdec_class->set_format = GST_DEBUG_FUNCPTR (gst_c2_vdec_set_format);
  vdec_class->handle_frame = GST_DEBUG_FUNCPTR (gst_c2_vdec_handle_frame);
  vdec_class->finish = GST_DEBUG_FUNCPTR (gst_c2_vdec_finish);
}

static void
gst_c2_vdec_init (GstC2VDecoder *c2vdec)
{
  c2vdec->name = NULL;
  c2vdec->engine = NULL;

  c2vdec->outstate = NULL;
  c2vdec->isubwc = FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_c2_vdec_debug_category, "qtic2vdec", 0,
      "QTI c2vdec decoder");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtic2vdec", GST_RANK_PRIMARY,
      GST_TYPE_C2_VDEC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtic2vdec,
    "C2Vdec decoding",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)

