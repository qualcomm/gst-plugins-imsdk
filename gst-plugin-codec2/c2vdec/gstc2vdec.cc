
/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include "gstc2vdec.h"


#define LUMA_PLANE 0
#define CHROMA_PLANE 1


GST_DEBUG_CATEGORY_STATIC (gst_c2vdec_debug_category);
#define GST_CAT_DEFAULT gst_c2vdec_debug_category

/* prototypes */


static void gst_c2vdec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_c2vdec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static gboolean gst_c2vdec_open (GstVideoDecoder * decoder);
static gboolean gst_c2vdec_close (GstVideoDecoder * decoder);
static gboolean gst_c2vdec_stop (GstVideoDecoder * decoder);
static gboolean gst_c2vdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_c2vdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

enum
{
  PROP_0
};

#define NANO_TO_MILLI(x)  ((x) / 1000)
#define EOS_WAITING_TIMEOUT 5
#define QCODEC2_MIN_OUTBUFFERS 6

#define GST_QTI_CODEC2_DEC_OUTPUT_PICTURE_ORDER_MODE_DEFAULT    (0xffffffff)
#define GST_QTI_CODEC2_DEC_LOW_LATENCY_MODE_DEFAULT             (FALSE)
#define GST_QTI_CODEC2_DEC_MAP_OUTBUF_DEFAULT                   (0xffffffff)

/* pad templates */

static GstStaticPadTemplate gst_c2vdec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-h265,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-vp8"
        ";" "video/x-vp9" ";" "video/mpeg," "mpegversion = (int)2")

    );


static GstStaticPadTemplate gst_c2vdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        "video/x-raw, "
        "format = (string) NV12, "
        "width  = (int) [ 32, 8192 ], "
        "height = (int) [ 32, 8192 ]"
        ";"
        "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "
        "format = (string) NV12, "
        "width  = (int) [ 32, 8192 ], "
        "height = (int) [ 32, 8192 ]")
    );

//TODO change to decoder
static G_DEFINE_QUARK (QtiCodec2EncoderQuark, gst_c2_venc_qdata);

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstC2vdec, gst_c2vdec, GST_TYPE_VIDEO_DECODER,
  GST_DEBUG_CATEGORY_INIT (gst_c2vdec_debug_category, "c2vdec", 0,
  "debug category for c2vdec element"));

static config_params_t
make_resolution_param (guint32 width, guint32 height, gboolean isInput)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_RESOLUTION;
  param.is_input = isInput;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static config_params_t
make_pixelFormat_param (guint32 fmt, gboolean isInput)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_PIXELFORMAT;
  param.is_input = isInput;
  param.pixel_fmt = (pixel_format_t)fmt;

  return param;
}

static config_params_t
make_interlace_param (interlace_mode_t mode, gboolean isInput)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_INTERLACE;
  param.is_input = isInput;
  param.interlace_mode = mode;

  return param;
}

static config_params_t
make_output_picture_order_param (guint output_picture_order_mode)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE;
  param.output_picture_order_mode = output_picture_order_mode;

  return param;
}

static config_params_t
make_low_latency_param (gboolean low_latency_mode)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY;
  param.low_latency_mode = low_latency_mode;

  return param;
}

static void
gst_c2_buffer_release (GstStructure * structure)
{
  GstVideoDecoder *decoder = NULL;
  guint64 index = 0;
  gst_structure_get (structure, "decoder", G_TYPE_POINTER, &decoder, NULL);
  gst_structure_get_uint64 (structure, "index", &index);
   GST_ERROR_OBJECT (decoder, "gst_c2_buffer_release index %d", index);
  GstC2vdec *dec = GST_C2VDEC (decoder);
  if (decoder) {
    if (!gst_c2_vdec_wrapper_free_output_buffer (dec->wrapper, index)) {
      GST_ERROR_OBJECT (decoder, "Failed to release the buffer (%lu)", index);
    }
  } else {
    GST_ERROR_OBJECT (decoder, "Null handle");
  }

  gst_structure_free (structure);
}

static gchar *
get_c2_comp_name (GstVideoDecoder * decoder, GstStructure * s,
    gboolean low_latency)
{
  GstC2vdec *dec = GST_C2VDEC (decoder);
  gchar *str = NULL;
  gchar *concat_str = NULL;
  gchar *str_low_latency = g_strdup (".low_latency");
  gboolean supported = FALSE;
  gint mpegversion = 0;

  if (gst_structure_has_name (s, "video/x-h264")) {
    str = g_strdup ("c2.qti.avc.decoder");
  } else if (gst_structure_has_name (s, "video/x-h265")) {
    str = g_strdup ("c2.qti.hevc.decoder");
  } else if (gst_structure_has_name (s, "video/x-vp8")) {
    str = g_strdup ("c2.qti.vp8.decoder");
  } else if (gst_structure_has_name (s, "video/x-vp9")) {
    str = g_strdup ("c2.qti.vp9.decoder");
  } else if (gst_structure_has_name (s, "video/mpeg")) {
    if (gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      if (mpegversion == 2) {
        str = g_strdup ("c2.qti.mpeg2.decoder");
      }
    }
  }
  return str;
}

static guint32
gst_to_c2_pixelformat (GstVideoDecoder * decoder, GstVideoFormat format)
{
  guint32 result = 0;
  GstC2vdec *dec = GST_C2VDEC (decoder);
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      result = PIXEL_FORMAT_NV12_LINEAR;
      break;
    default:
      result = PIXEL_FORMAT_NV12_UBWC;
      GST_WARNING_OBJECT (dec,
          "Invalid pixel format(%d), fallback to NV12 UBWC", format);
      break;
  }

  GST_DEBUG_OBJECT (dec, "to_c2_pixelformat (%s), c2 format: %d",
      gst_video_format_to_string (format), result);

  return result;
}

static GstBuffer *
gst_c2vdec_wrap_output_buffer (GstVideoDecoder * decoder,
    BufferDescriptor * decode_buf)
{
  GstBuffer *out_buf;
  GstVideoCodecState *state;
  GstVideoInfo *vinfo;
  GstStructure *structure = NULL;
  GstC2vdec *dec = GST_C2VDEC (decoder);
  guint output_size = decode_buf->size;
  guint64 *p_modifier = NULL;


  state = gst_video_decoder_get_output_state (decoder);
  if (state) {
    vinfo = &state->info;
  } else {
    GST_ERROR_OBJECT (dec, "Failed to get decoder output state");
    return NULL;
  }

    GstAllocator *allocator = gst_fd_allocator_new ();
    GstMemory *mem = gst_fd_allocator_alloc (allocator,
        decode_buf->fd, decode_buf->size, GST_FD_MEMORY_FLAG_DONT_CLOSE);
    // create a gst buffer
    out_buf = gst_buffer_new ();
    // insert fd memmory into the gstbuffer
    gst_buffer_append_memory (out_buf, mem);
    gst_object_unref (allocator);
    vinfo->stride[LUMA_PLANE] = decode_buf->stride;
    vinfo->offset[LUMA_PLANE] = 0;
    vinfo->stride[CHROMA_PLANE] = decode_buf->stride;
    vinfo->offset[CHROMA_PLANE] = decode_buf->stride * (((decode_buf->size/decode_buf->stride)/3) *2);
    gst_buffer_add_video_meta_full (out_buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_INFO_FORMAT (vinfo), GST_VIDEO_INFO_WIDTH (vinfo),
            GST_VIDEO_INFO_HEIGHT (vinfo), GST_VIDEO_INFO_N_PLANES (vinfo),
            vinfo->offset, vinfo->stride);



done:
  gst_video_codec_state_unref (state);
  return out_buf;

fail:
  if (out_buf) {
    gst_buffer_unref (out_buf);
    out_buf = NULL;
  }
  goto done;
}

static GstFlowReturn
gst_c2vdec_push_frame_downstream (GstVideoDecoder * decoder, BufferDescriptor * decode_buf)
{
  GstC2vdec *dec = GST_C2VDEC (decoder);
  GstBuffer *outbuf;
  GstVideoCodecFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecState *state;
  GstVideoInfo *vinfo;
  GstStructure *structure = NULL;

  GST_DEBUG_OBJECT (dec, "push_frame_downstream");

  state = gst_video_decoder_get_output_state (decoder);
  if (state) {
    vinfo = &state->info;
  } else {
    GST_ERROR_OBJECT (dec, "video codec state is NULL, unexpected!");
    goto out;
  }

  GST_DEBUG_OBJECT (dec,
      "push_frame_downstream, buffer: %p, fd: %d,  timestamp: %lu",
      decode_buf->data, decode_buf->fd, decode_buf->timestamp);

  frame = gst_video_decoder_get_frame (decoder, decode_buf->index);
  if (frame == NULL) {
    GST_ERROR_OBJECT (dec,
        "Error in gst_video_decoder_get_frame, frame number: %lu",
        decode_buf->index);
    goto out;
  }

  outbuf = gst_c2vdec_wrap_output_buffer (decoder, decode_buf);
  if (outbuf) {
    gst_buffer_set_flags (outbuf, GST_BUFFER_FLAG_SYNC_AFTER);
    GST_BUFFER_PTS (outbuf) =
        gst_util_uint64_scale (decode_buf->timestamp, GST_SECOND,
        C2_TICKS_PER_SECOND);

    if (state->info.fps_d != 0 && state->info.fps_n != 0) {
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (GST_SECOND,
          vinfo->fps_d, vinfo->fps_n);
    }
    frame->output_buffer = outbuf;

    GST_DEBUG_OBJECT (dec,
        "out buffer: PTS: %lu, duration: %lu, fps_d: %d, fps_n: %d",
        GST_BUFFER_PTS (outbuf), GST_BUFFER_DURATION (outbuf), vinfo->fps_d,
        vinfo->fps_n);
  }
 /* Decrease the refcount of the frame so that the frame is released by the
   * gst_video_decoder_finish_frame function and so that the output buffer is
   * writable when it's pushed downstream */

   structure = gst_structure_new_empty ("BUFFER");
    gst_structure_set (structure,
        "decoder", G_TYPE_POINTER, decoder,
        "index", G_TYPE_UINT64, decode_buf->index, NULL);
    /* Set a notification function to signal when the buffer is no longer used. */
    gst_mini_object_set_qdata (GST_MINI_OBJECT (outbuf),
        gst_c2_venc_qdata_quark (), structure,
        (GDestroyNotify) gst_c2_buffer_release);

  gst_video_codec_frame_unref (frame);
  ret = gst_video_decoder_finish_frame (decoder, frame);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (dec, "Failed(%d) to push frame downstream", ret);
    goto out;
  }

  gst_video_codec_state_unref (state);
  return GST_FLOW_OK;

out:
  gst_video_codec_state_unref (state);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_c2vdec_setup_output (GstVideoDecoder * decoder, GPtrArray * config)
{
  GstC2vdec *dec = GST_C2VDEC (decoder);
  GstVideoAlignment align;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  config_params_t pixelformat;

  GstCaps *templ_caps, *intersection = NULL;
  GstStructure *s;
  const gchar *format_str;
  gboolean actual_map = FALSE;

  /* Set decoder output format to NV12 by default */
  dec->output_state =
      gst_video_decoder_set_output_state (decoder,
      output_format, dec->width, dec->height, dec->input_state);

  /* state->caps should be NULL */
  if (dec->output_state->caps) {
    gst_caps_unref (dec->output_state->caps);
  }

  /* Fixate decoder output caps */
  templ_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  intersection =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (decoder), templ_caps);
  gst_caps_unref (templ_caps);

  GST_DEBUG_OBJECT (dec, "Allowed downstream caps: %" GST_PTR_FORMAT,
      intersection);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (dec, "Empty caps");
    //oto error_setup_output;
    return GST_FLOW_ERROR;
  }

  /* Fixate color format */
  intersection = gst_caps_truncate (intersection);
  intersection = gst_caps_fixate (intersection);
  GST_DEBUG_OBJECT (dec, "intersection caps: %" GST_PTR_FORMAT, intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");

  if (!format_str || (output_format = gst_video_format_from_string (format_str))
      == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (dec, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    gst_caps_unref (intersection);
    //goto error_setup_output;
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (dec,
      "Set decoder output state: color format: %d, width: %d, height: %d",
      output_format, dec->width, dec->height);

  /* Fill actual width/height into output caps */
  GValue g_width = { 0, };
  GValue g_height = { 0, };
  g_value_init (&g_width, G_TYPE_INT);
  g_value_set_int (&g_width, dec->width);

  g_value_init (&g_height, G_TYPE_INT);
  g_value_set_int (&g_height, dec->height);
  gst_caps_set_value (intersection, "width", &g_width);
  gst_caps_set_value (intersection, "height", &g_height);

  GST_INFO_OBJECT (dec, "DMA output feature is %s",
      (dec->downstream_supports_dma ? "enabled" : "disabled"));
  dec->output_state->caps = intersection;
  GST_INFO_OBJECT (dec, "output caps: %" GST_PTR_FORMAT,
      dec->output_state->caps);

   GST_LOG_OBJECT (dec, "output width: %d, height: %d, format: %d",
      dec->width, dec->height, output_format);

  if (config) {
    pixelformat =
        make_pixelFormat_param (gst_to_c2_pixelformat (decoder, output_format),
        FALSE);
    GST_LOG_OBJECT (dec, "set c2 output format: %d",
        pixelformat.pixel_fmt);
    g_ptr_array_add (config, &pixelformat);
  } else {
    //goto error_setup_output;
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (dec, "Complete setup output");

done:
  return ret;

error_setup_output:
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_qticodec2vdec_decode (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstC2vdec *dec = GST_C2VDEC (decoder);
  GstMapInfo mapinfo = { 0, };
  GstBuffer *buf = NULL;
  BufferDescriptor inBuf;
  gboolean status = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (dec, "decode");
  if (!frame) {
    GST_WARNING_OBJECT (dec, "frame is NULL, ret GST_FLOW_EOS");
    return GST_FLOW_EOS;
  }

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  buf = frame->input_buffer;
  gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
  inBuf.fd = -1;
  inBuf.data = mapinfo.data;
  inBuf.size = mapinfo.size;
  inBuf.pool_type = BUFFER_POOL_BASIC_LINEAR;

  GST_INFO_OBJECT (dec, "frame->pts (%" G_GUINT64_FORMAT ")", frame->pts);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  /* Keep track of queued frame */
  dec->queued_frame[(dec->frame_index) % MAX_QUEUED_FRAME] =
      frame->system_frame_number;

  inBuf.timestamp = NANO_TO_MILLI (frame->pts);
  inBuf.index = frame->system_frame_number;
  GST_WARNING_OBJECT (dec," frame index = %d",inBuf.index);

  /* Queue buffer to Codec2 */
  if (!gst_c2_vdec_wrapper_component_queue (dec->wrapper, &inBuf)) {
    GST_ERROR_OBJECT(dec, "failed to queue input frame to Codec2");
    return GST_FLOW_ERROR;
  }

  gst_buffer_unmap (buf, &mapinfo);

  g_mutex_lock (&(dec->pending_lock));
  dec->frame_index += 1;
  dec->num_input_queued++;
  g_mutex_unlock (&(dec->pending_lock));

out:
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  return ret;
}

static void
handle_video_event (EVENT_TYPE type, void *userdata, void *userdata2)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) userdata2;
  GstC2vdec *c2vdec = GST_C2VDEC (decoder);
  GST_DEBUG_OBJECT (c2vdec, "handle_video_event");
  GstFlowReturn ret = GST_FLOW_OK;

  switch (type) {
    case EVENT_OUTPUTS_DONE: {
      BufferDescriptor *outBuffer = (BufferDescriptor *) userdata;

      GST_DEBUG_OBJECT (c2vdec,
          "Event output done, index: %lu, fd: %u,"
          "filled len: %u, timestamp: %lu, flag: %x",
          outBuffer->index, outBuffer->fd,
          outBuffer->size, outBuffer->timestamp,
          outBuffer->flag);

      if (outBuffer->fd > 0 || outBuffer->size > 0) {
        ret = gst_c2vdec_push_frame_downstream (decoder, outBuffer);
        if (ret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (c2vdec, "Failed to push frame downstream");
        }
      } else if (outBuffer->flag & FLAG_TYPE_END_OF_STREAM) {
        GST_INFO_OBJECT (c2vdec, "Encoder reached EOS");
        g_mutex_lock (&c2vdec->pending_lock);
        c2vdec->eos_reached = TRUE;
        g_cond_signal (&c2vdec->pending_cond);
        g_mutex_unlock (&c2vdec->pending_lock);
      } else {
        GST_ERROR_OBJECT (c2vdec, "Invalid output buffer");
      }
      break;
    }
    case EVENT_TRIPPED:
      GST_ERROR_OBJECT (c2vdec, "EVENT_TRIPPED(%d)", *(gint32 *) userdata);
      break;
    case EVENT_ERROR:
      GST_ERROR_OBJECT (c2vdec, "EVENT_ERROR(%d)", *(gint32 *) userdata);
      break;
    default:
      GST_ERROR_OBJECT (c2vdec, "Invalid Event(%d)", type);
  }

}


static void
gst_c2vdec_class_init (GstC2vdecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS(klass),
      &gst_c2vdec_sink_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS(klass),
      &gst_c2vdec_src_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "Codec2 Decoder", "Generic", "Codec2 Decoder",
      "quic_arinbisw@quicinc.com");

  gobject_class->set_property = gst_c2vdec_set_property;
  gobject_class->get_property = gst_c2vdec_get_property;
  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_c2vdec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_c2vdec_close);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_c2vdec_stop);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_c2vdec_set_format);
  video_decoder_class->handle_frame = GST_DEBUG_FUNCPTR (gst_c2vdec_handle_frame);

}

static void
gst_c2vdec_init (GstC2vdec *c2vdec)
{
  g_cond_init (&c2vdec->pending_cond);
  g_mutex_init (&c2vdec->pending_lock);
  c2vdec->silent = FALSE;
  c2vdec->input_setup = FALSE;
  c2vdec->output_setup = FALSE;
  c2vdec->eos_reached = FALSE;
  c2vdec->comp_store = NULL;
  c2vdec->comp = NULL;
  c2vdec->comp_intf = NULL;
  c2vdec->frame_index = 0;
  c2vdec->num_input_queued = 0;
  c2vdec->num_output_done = 0;
  c2vdec->output_picture_order_mode =
      GST_QTI_CODEC2_DEC_OUTPUT_PICTURE_ORDER_MODE_DEFAULT;
  c2vdec->low_latency_mode = GST_QTI_CODEC2_DEC_LOW_LATENCY_MODE_DEFAULT;

  c2vdec->wrapper = gst_c2_wrapper_new ();
  g_return_if_fail (c2vdec->wrapper != NULL);

}

void
gst_c2vdec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstC2vdec *c2vdec = GST_C2VDEC (object);

  GST_DEBUG_OBJECT (c2vdec, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_c2vdec_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstC2vdec *c2vdec = GST_C2VDEC (object);

  GST_DEBUG_OBJECT (c2vdec, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static gboolean
gst_c2vdec_open (GstVideoDecoder * decoder)
{
  GstC2vdec *c2vdec = GST_C2VDEC (decoder);

  GST_DEBUG_OBJECT (c2vdec, "open");

  return TRUE;
}

static gboolean
gst_c2vdec_close (GstVideoDecoder * decoder)
{
  GstC2vdec *c2vdec = GST_C2VDEC (decoder);

  GST_DEBUG_OBJECT (c2vdec, "close");

  return TRUE;
}


static gboolean
gst_c2vdec_stop (GstVideoDecoder * decoder)
{
  GstC2vdec *c2vdec = GST_C2VDEC (decoder);

  GST_DEBUG_OBJECT (c2vdec, "stop");

 if (!gst_c2_vdec_wrapper_component_stop (c2vdec->wrapper)) {
    GST_ERROR_OBJECT (c2vdec, "Failed to stop component");
 }


  return TRUE;
}

static gboolean
gst_c2vdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstC2vdec *dec = GST_C2VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "set_format");

  GstStructure *structure;
  const gchar *mode;
  gint retval = 0;
  gboolean ret = FALSE;
  gint width = 0;
  gint height = 0;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  interlace_mode_t c2interlace_mode = INTERLACE_MODE_PROGRESSIVE;
  gchar *comp_name;
  GPtrArray *config = NULL;
  config_params_t resolution;
  config_params_t interlace;
  config_params_t output_picture_order_mode;
  config_params_t low_latency_mode;

  GST_DEBUG_OBJECT (dec, "set_format");

  structure = gst_caps_get_structure (state->caps, 0);
  comp_name = get_c2_comp_name (decoder, structure, dec->low_latency_mode);
  if (!comp_name) {
    GST_ERROR_OBJECT (dec, "Failed to get relevant component name, caps:%"
        GST_PTR_FORMAT, state->caps);
    return FALSE;
  }

  retval = gst_structure_get_int (structure, "width", &width);
  retval &= gst_structure_get_int (structure, "height", &height);
  if (!retval) {
    goto error_res;
  }

  if ((mode = gst_structure_get_string (structure, "interlace-mode"))) {
    if (g_str_equal ("progressive", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
      c2interlace_mode = INTERLACE_MODE_PROGRESSIVE;
    } else if (g_str_equal ("interleaved", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      c2interlace_mode = INTERLACE_MODE_INTERLEAVED_TOP_FIRST;
    } else if (g_str_equal ("mixed", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      c2interlace_mode = INTERLACE_MODE_INTERLEAVED_TOP_FIRST;
    } else if (g_str_equal ("fields", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_FIELDS;
      c2interlace_mode = INTERLACE_MODE_FIELD_TOP_FIRST;
    }
  }

  dec->width = width;
  dec->height = height;
  dec->interlace_mode = interlace_mode;
  dec->comp_name = comp_name;


 if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
  }

  dec->input_state = gst_video_codec_state_ref (state);

  if (!gst_c2_vdec_wrapper_create_component (dec->wrapper,
      dec->comp_name, handle_video_event, decoder)) {
    GST_ERROR_OBJECT (dec, "Failed to create a component");
    goto error_set_format;
  }

  config = g_ptr_array_new ();

  resolution = make_resolution_param (width, height, TRUE);
  g_ptr_array_add (config, &resolution);

  interlace = make_interlace_param (c2interlace_mode, FALSE);
  g_ptr_array_add (config, &interlace);

  if (dec->output_picture_order_mode !=
      GST_QTI_CODEC2_DEC_OUTPUT_PICTURE_ORDER_MODE_DEFAULT) {
    output_picture_order_mode =
        make_output_picture_order_param (dec->output_picture_order_mode);
    g_ptr_array_add (config, &output_picture_order_mode);
  }

  if (dec->low_latency_mode) {
    low_latency_mode = make_low_latency_param (dec->low_latency_mode);
    g_ptr_array_add (config, &low_latency_mode);
  }

  /* Negotiate with downstream and setup output */

  if (GST_FLOW_OK != gst_c2vdec_setup_output (decoder, config)) {
    g_ptr_array_free (config, FALSE);
    goto error_set_format;
  }

  if (!gst_c2_vdec_wrapper_config_component (dec->wrapper, config)) {
    GST_ERROR_OBJECT (dec, "Failed to config interface");
  }

  g_ptr_array_free (config, FALSE);

  if (!gst_c2_vdec_wrapper_component_start (dec->wrapper)) {
    GST_ERROR_OBJECT (dec, "Failed to start component");
  }

  dec->input_setup = TRUE;

  GST_DEBUG_OBJECT (dec, "gst_c2_vdec_set_format");

done:
  dec->input_setup = TRUE;
  return TRUE;

  /* Errors */
error_res:
  {
    GST_ERROR_OBJECT (dec, "Unable to get width/height value");
    return FALSE;
  }
error_set_format:
  {
    GST_ERROR_OBJECT (dec, "failed to setup input");
    return FALSE;
  }


  return TRUE;
}


static GstFlowReturn
gst_c2vdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstC2vdec *c2vdec = GST_C2VDEC (decoder);

  GST_DEBUG_OBJECT (c2vdec, "handle_frame");

  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (c2vdec, "handle_frame");

  if (!c2vdec->input_setup) {
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (c2vdec,
      "Frame number : %d, Distance from Sync : %d, Presentation timestamp : %"
      GST_TIME_FORMAT, frame->system_frame_number, frame->distance_from_sync,
      GST_TIME_ARGS (frame->pts));

  /* Decode frame */
  if (frame) {
    return gst_qticodec2vdec_decode (decoder, frame);
  } else {
    GST_DEBUG_OBJECT (c2vdec, "EOS reached in handle_frame");
    return GST_FLOW_EOS;
  }


  return GST_FLOW_OK;
}


static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "c2vdec", GST_RANK_NONE,
      GST_TYPE_C2VDEC);
}


GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    c2vdec,
    "C2Vdec decoding",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)

