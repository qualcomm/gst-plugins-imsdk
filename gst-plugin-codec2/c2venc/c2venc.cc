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

#include "c2venc.h"
#include "c2-engine/common.h"

#define GST_CAT_DEFAULT c2_venc_debug
GST_DEBUG_CATEGORY_STATIC (c2_venc_debug);

#define GST_TYPE_CODEC2_ENC_RATE_CONTROL (gst_c2_venc_rate_control_get_type ())
#define GST_TYPE_CODEC2_ENC_INTRA_REFRESH_MODE (gst_c2_venc_intra_refresh_mode_get_type ())
#define GST_TYPE_CODEC2_ENC_SLICE_MODE (gst_c2_venc_slice_mode_get_type ())
#define GST_TYPE_CODEC2_ENC_ENTROPY_MODE (gst_c2_venc_entropy_mode_get_type ())
#define GST_TYPE_CODEC2_ENC_LOOP_FILTER_MODE (gst_c2_venc_loop_filter_get_type ())
#define GST_TYPE_CODEC2_ENC_ROTATE (gst_c2_venc_rotate_get_type ())

#define gst_c2_venc_parent_class parent_class
G_DEFINE_TYPE (GstC2_VENCEncoder, gst_c2_venc, GST_TYPE_VIDEO_ENCODER);

#define EOS_WAITING_TIMEOUT 5

#define DEFAULT_PROP_ROI_QP_DELTA -15
#define GST_CODEC2_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT (0xffffffff)
#define GST_CODEC2_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT (0xffffffff)
#define GST_CODEC2_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT (0xffffffff)
#define GST_CODEC2_VIDEO_ENC_NUM_LTR_FRAMES_DEFAULT (0xffffffff)

// Caps formats.
#define GST_VIDEO_FORMATS "{ NV12, NV21 }"

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
  ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
      : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
          : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
              : (state <= GST_STATE_NULL))))

static GstStaticPadTemplate gst_c2_venc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS))
);

static GstStaticPadTemplate gst_c2_venc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-h265,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-heic,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }")
    );

// Function will be named gst_c2_venc_qdata_quark()
static G_DEFINE_QUARK (QtiCodec2EncoderQuark, gst_c2_venc_qdata);

enum
{
  PROP_0,
  PROP_RATE_CONTROL,
  PROP_INTRA_REFRESH_MODE,
  PROP_INTRA_REFRESH_MBS,
  PROP_TARGET_BITRATE,
  PROP_I_FRAME_ONLY,
  PROP_IDR_FRAME_INTERVAL,
  PROP_SLICE_MODE,
  PROP_SLICE_SIZE,
  PROP_MAX_QP_B_FRAMES,
  PROP_MAX_QP_I_FRAMES,
  PROP_MAX_QP_P_FRAMES,
  PROP_MIN_QP_B_FRAMES,
  PROP_MIN_QP_I_FRAMES,
  PROP_MIN_QP_P_FRAMES,
  PROP_ROI,
  PROP_ROI_QP_DELTA,
  PROP_ENTROPY_MODE,
  PROP_LOOP_FILTER_MODE,
  PROP_QUANT_I_FRAMES,
  PROP_QUANT_P_FRAMES,
  PROP_QUANT_B_FRAMES,
  PROP_NUM_LTR_FRAMES,
  PROP_ROTATE,
};

static guint32
gst_to_c2_pixelformat (GstVideoFormat format)
{
  guint32 result = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      result = PIXEL_FORMAT_NV12_LINEAR;
      break;
    default:
      break;
  }

  return result;
}

static GType
gst_c2_venc_slice_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {SLICE_MODE_DISABLE, "Slice Mode Disable", "disable"},
      {SLICE_MODE_MB, "Slice Mode MB", "MB"},
      {SLICE_MODE_BYTES, "Slice Mode Bytes", "bytes"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencSliceMode", values);
  }
  return qtype;
}

static GType
gst_c2_venc_rate_control_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {RC_MODE_OFF, "Disable RC", "disable"},
      {RC_MODE_CONST, "Constant", "constant"},
      {RC_MODE_CBR_VFR, "Constant bitrate, variable framerate", "CBR-VFR"},
      {RC_MODE_VBR_CFR, "Variable bitrate, constant framerate", "VBR-CFR"},
      {RC_MODE_VBR_VFR, "Variable bitrate, variable framerate", "VBR-VFR"},
      {RC_MODE_CQ, "Constant quality", "CQ"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencRateControl", values);
  }
  return qtype;
}

static GType
gst_c2_venc_intra_refresh_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {IR_MODE_NONE, "None", "none"},
      {IR_MODE_RANDOM, "Random", "random"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencIntraRefreshMode", values);
  }
  return qtype;
}

static GType
gst_c2_venc_entropy_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {ENTROPY_MODE_NONE, "None", "none"},
      {ENTROPY_MODE_CAVLC, "CAVLC", "cavlc"},
      {ENTROPY_MODE_CABAC, "CABAC", "cabac"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencEntropyMode", values);
  }
  return qtype;
}

static GType
gst_c2_venc_loop_filter_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {LOOP_FILTER_NONE, "None", "none"},
      {LOOP_FILTER_ENABLE, "Enable", "enable"},
      {LOOP_FILTER_DISABLE, "Disable", "disable"},
      {LOOP_FILTER_DISABLE_SLICE_BOUNDARY, "Disable-slice-boundary", "disable-slice-boundary"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencLoopFilterMode", values);
  }
  return qtype;
}

static GType
gst_c2_venc_rotate_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {ROTATE_NONE, "No rotation", "none"},
      {ROTATE_90_CW, "Rotate 90 degrees clockwise", "90CW"},
      {ROTATE_180, "Rotate 180 degrees", "180"},
      {ROTATE_90_CCW, "Rotate 90 degrees counter-clockwise", "90CCW"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencRotate", values);
  }
  return qtype;
}

static config_params_t
make_profileLevel_param (video_profile_t profile, video_level_t level)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_PROFILE_LEVEL;
  if (profile != VIDEO_PROFILE_MAX)
    param.profile = profile;
  if (level != VIDEO_LEVEL_MAX)
    param.level = level;

  return param;
}

static config_params_t
make_bitrate_param (guint32 bitrate, gboolean is_input)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_BITRATE;
  param.is_input = is_input;
  param.val.u32 = bitrate;

  return param;
}

static config_params_t
make_framerate_param (gfloat framerate, gboolean is_input)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_FRAMERATE;
  param.is_input = is_input;
  param.val.fl = framerate;

  return param;
}

static config_params_t
make_resolution_param (guint32 width, guint32 height, gboolean is_input)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_RESOLUTION;
  param.is_input = is_input;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static config_params_t
make_qp_ranges_param (guint32 miniqp, guint32 maxiqp, guint32 minpqp,
    guint32 maxpqp, guint32 minbqp, guint32 maxbqp)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_QP_RANGES;
  param.qp_ranges.miniqp = miniqp;
  param.qp_ranges.maxiqp = maxiqp;
  param.qp_ranges.minpqp = minpqp;
  param.qp_ranges.maxpqp = maxpqp;
  param.qp_ranges.minbqp = minbqp;
  param.qp_ranges.maxbqp = maxbqp;

  return param;
}

static config_params_t
make_pixelFormat_param (guint32 fmt, gboolean is_input)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_PIXELFORMAT;
  param.is_input = is_input;
  param.pixel_fmt = (pixel_format_t) fmt;

  return param;
}

static config_params_t
make_rateControl_param (rc_mode_t mode)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_RATECONTROL;
  param.rc_mode = mode;

  return param;
}

static config_params_t
make_sync_frame_interval_param (int64_t period_ms)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_SYNC_FRAME_INT;
  param.val.i64 = period_ms;

  return param;
}

static config_params_t
make_request_sync_frame_param (bool request)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_REQUEST_SYNC_FRAME;
  param.val.bl = request;

  return param;
}

static config_params_t
make_roi_encoding (gint64 timestampUs, char *rectPayload, char *rectPayloadExt)
{
  config_params_t param;
  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_ROI_ENCODING;
  param.roi.timestampUs = timestampUs;

  param.roi.rectPayload = rectPayload;
  param.roi.rectPayloadExt = rectPayloadExt;

  return param;
}

static void
config_roi_encoding (GstC2_VENCEncoder *c2venc, gint64 timestampUs)
{
  GPtrArray *config = NULL;
  config_params_t roi_encoding;

  if (c2venc->roi_encoding.top != 0 || c2venc->roi_encoding.left != 0 ||
      c2venc->roi_encoding.bottom != 0 || c2venc->roi_encoding.right != 0) {

    config = g_ptr_array_new ();

    char rectPayload[128];
    char rectPayloadExt[128];

    snprintf(rectPayload, sizeof(rectPayload), "%d,%d-%d,%d=%d",
        c2venc->roi_encoding.top, c2venc->roi_encoding.left,
        c2venc->roi_encoding.bottom, c2venc->roi_encoding.right,
        c2venc->roi_encoding_qp_delta);

    snprintf(rectPayloadExt, sizeof(rectPayloadExt), "%d,%d-%d,%d=%d",
        c2venc->roi_encoding.top, c2venc->roi_encoding.left,
        c2venc->roi_encoding.bottom, c2venc->roi_encoding.right,
        c2venc->roi_encoding_qp_delta);

    roi_encoding = make_roi_encoding (timestampUs, rectPayload, rectPayloadExt);
    g_ptr_array_add (config, &roi_encoding);

    // Config component
    if (!gst_c2_venc_wrapper_config_component (c2venc->wrapper, config)) {
      GST_ERROR_OBJECT (c2venc, "Failed to config interface");
    }

    g_ptr_array_free (config, FALSE);
  }
}

static config_params_t
make_slicemode_param (guint32 size, slice_mode_t mode)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_SLICE_MODE;
  param.val.u32 = size;
  param.slice_mode = mode;

  return param;
}

static config_params_t
make_intraRefresh_param (ir_mode_t mode, guint32 intra_refresh_mbs)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_INTRAREFRESH;
  param.ir_mode.type = mode;
  param.ir_mode.intra_refresh_mbs = (float) intra_refresh_mbs;

  return param;
}

static config_params_t
make_entropy_mode_param (entropy_mode_t mode)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_ENTROPY_MODE;
  param.entropy_mode = mode;

  return param;
}

static config_params_t
make_loop_filter_mode_param (loop_filter_mode_t mode)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_LOOP_FILTER_MODE;
  param.loop_filter_mode = mode;

  return param;
}

static config_params_t
make_qp_Init_param (guint32 quant_i_frames, guint32 quant_p_frames,
                    guint32 quant_b_frames)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_QP_INIT;
  if (quant_i_frames != 0xFFFFFFFF) {
    param.qp_init.quant_i_frames_enable = TRUE;
    param.qp_init.quant_i_frames = quant_i_frames;
  }
  if (quant_p_frames != 0xFFFFFFFF) {
    param.qp_init.quant_p_frames_enable = TRUE;
    param.qp_init.quant_p_frames = quant_p_frames;
  }
  if (quant_b_frames != 0xFFFFFFFF) {
    param.qp_init.quant_b_frames_enable = TRUE;
    param.qp_init.quant_b_frames = quant_b_frames;
  }

  return param;
}

static config_params_t
make_num_ltr_frames_param (guint32 num_ltr_frames)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_NUM_LTR_FRAMES;
  param.val.u32 = num_ltr_frames;

  return param;
}

static config_params_t
make_rotate_param (rotate_t rotate)
{
  config_params_t param;

  memset (&param, 0, sizeof (config_params_t));

  param.config_name = CONFIG_FUNCTION_KEY_ROTATE;
  param.rotate = rotate;

  return param;
}

static gboolean
gst_c2_venc_trigger_iframe (GstC2_VENCEncoder *c2venc)
{
  GST_DEBUG_OBJECT (c2venc, "Trigger I frame insertion");

  GPtrArray *config = g_ptr_array_new ();
  config_params_t request_sync_frame;
  request_sync_frame = make_request_sync_frame_param (true);
  g_ptr_array_add (config, &request_sync_frame);
  // Config component
  if (!gst_c2_venc_wrapper_config_component (c2venc->wrapper, config)) {
    GST_ERROR_OBJECT (c2venc, "Failed to config interface");
  }
  g_ptr_array_free (config, FALSE);

  return TRUE;
}

static gchar *
gst_c2_venc_get_c2_comp_name (GstStructure * structure)
{
  gchar *ret = NULL;

  if (gst_structure_has_name (structure, "video/x-h264")) {
    ret = g_strdup ("c2.qti.avc.encoder");
  } else if (gst_structure_has_name (structure, "video/x-h265")) {
    ret = g_strdup ("c2.qti.hevc.encoder");
  } else if (gst_structure_has_name (structure, "video/x-heic")) {
    ret = g_strdup ("c2.qti.heic.encoder");
  }

  return ret;
}

static GstFlowReturn
gst_c2_venc_setup_output (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *outcaps;

  GST_DEBUG_OBJECT (c2venc, "setup_output");

  if (c2venc->output_state) {
    gst_video_codec_state_unref (c2venc->output_state);
  }

  outcaps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (outcaps) {
    GstStructure *structure;
    gchar *comp_name;

    if (gst_caps_is_empty (outcaps)) {
      gst_caps_unref (outcaps);
      GST_ERROR_OBJECT (c2venc, "Unsupported format in caps: %" GST_PTR_FORMAT,
          outcaps);
      return GST_FLOW_ERROR;
    }

    outcaps = gst_caps_make_writable (outcaps);
    outcaps = gst_caps_fixate (outcaps);
    structure = gst_caps_get_structure (outcaps, 0);

    // Fill actual width/height into output caps
    GValue g_width = { 0, };
    GValue g_height = { 0, };
    g_value_init (&g_width, G_TYPE_INT);
    g_value_set_int (&g_width, c2venc->width);

    g_value_init (&g_height, G_TYPE_INT);
    g_value_set_int (&g_height, c2venc->height);

    gst_caps_set_value (outcaps, "width", &g_width);
    gst_caps_set_value (outcaps, "height", &g_height);

    GST_INFO_OBJECT (c2venc, "Fixed output caps: %" GST_PTR_FORMAT, outcaps);

    comp_name = gst_c2_venc_get_c2_comp_name (structure);
    if (!comp_name) {
      GST_ERROR_OBJECT (c2venc, "Unsupported format in caps: %" GST_PTR_FORMAT,
          outcaps);
      gst_caps_unref (outcaps);
      return GST_FLOW_ERROR;
    }

    c2venc->comp_name = comp_name;
    c2venc->output_state =
        gst_video_encoder_set_output_state (encoder, outcaps, state);
    if (!c2venc->output_state) {
      GST_ERROR_OBJECT (c2venc, "set output state error");
      gst_caps_unref (outcaps);
      g_free(comp_name);
      return GST_FLOW_ERROR;
    }
    c2venc->output_setup = TRUE;
  }

  return ret;
}

static gboolean
gst_c2_venc_start (GstVideoEncoder * encoder)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "Encoder start");

  if (!c2venc->output_setup) {
    if (GST_FLOW_OK != gst_c2_venc_setup_output (encoder, c2venc->input_state)) {
      GST_ERROR_OBJECT (c2venc, "fail to setup output");
      return FALSE;
    }
  }

  if (c2venc->input_setup &&
      !gst_c2_venc_wrapper_component_start (c2venc->wrapper)) {
    GST_ERROR_OBJECT (c2venc, "Failed to start component");
  }

  return TRUE;
}

static gboolean
gst_c2_venc_stop (GstVideoEncoder * encoder)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "Encoder stop");

  if (c2venc->output_state) {
    gst_video_codec_state_unref (c2venc->output_state);
    c2venc->output_state = NULL;
    c2venc->output_setup = FALSE;
  }

  if (!gst_c2_venc_wrapper_component_stop (c2venc->wrapper)) {
    GST_ERROR_OBJECT (c2venc, "Failed to stop component");
  }

  c2venc->eos_reached = FALSE;

  return TRUE;
}

static void
gst_c2_venc_buffer_release (GstStructure * structure)
{
  GstVideoEncoder *encoder = NULL;
  guint64 index = 0;

  gst_structure_get (structure, "encoder", G_TYPE_POINTER, &encoder, NULL);
  gst_structure_get_uint64 (structure, "index", &index);
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  if (encoder) {
    if (!gst_c2_venc_wrapper_free_output_buffer (c2venc->wrapper, index)) {
      GST_ERROR_OBJECT (c2venc, "Failed to release the buffer (%lu)", index);
    }
  } else {
    GST_ERROR_OBJECT (c2venc, "Null handle");
  }

  gst_structure_free (structure);
}

static GstFlowReturn
push_frame_downstream (GstVideoEncoder * encoder, BufferDescriptor * encode_buf)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *outbuf = NULL;
  GstVideoCodecState *state = NULL;
  GstVideoInfo *vinfo = NULL;
  GstStructure *structure = NULL;

  GST_DEBUG_OBJECT (c2venc, "push_frame_downstream");

  state = gst_video_encoder_get_output_state (encoder);
  if (state) {
    vinfo = &state->info;
  } else {
    GST_ERROR_OBJECT (c2venc, "video codec state is NULL, unexpected!");
    return GST_FLOW_ERROR;
  }

  frame = gst_video_encoder_get_frame (encoder, encode_buf->index);
  if (frame == NULL) {
    GST_ERROR_OBJECT (c2venc,
        "Error in gst_video_encoder_get_frame, frame number: %lu",
        encode_buf->index);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (c2venc, "fill codec config size:%d frame size:%d",
      encode_buf->config_size, encode_buf->size);

  if (encode_buf->flag & FLAG_TYPE_CODEC_CONFIG) {
    GST_DEBUG_OBJECT (c2venc, "fill codec config size:%d first frame size:%d",
        encode_buf->config_size, encode_buf->size);
    outbuf =
        gst_buffer_new_and_alloc (encode_buf->size + encode_buf->config_size);
    gst_buffer_fill (outbuf, 0, encode_buf->config_data,
        encode_buf->config_size);


    GstMapInfo info;
    GstAllocator *allocator = gst_fd_allocator_new ();
    GstMemory *mem = gst_fd_allocator_alloc (allocator,
        encode_buf->fd, encode_buf->size, GST_FD_MEMORY_FLAG_DONT_CLOSE);
    // create a gst buffer
    GstBuffer *buf = gst_buffer_new ();
    // insert fd memmory into the gstbuffer
    gst_buffer_append_memory (buf, mem);
    gst_object_unref (allocator);

    if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
      GST_ERROR_OBJECT (c2venc, "ERROR: Failed to map the buffer!");
      return GST_FLOW_ERROR;
    }

    gst_buffer_fill (outbuf, encode_buf->config_size, info.data,
        encode_buf->size);

    gst_buffer_unmap (buf, &info);
    gst_buffer_unref (buf);
  } else {

    GstAllocator *allocator = gst_fd_allocator_new ();
    GstMemory *mem = gst_fd_allocator_alloc (allocator,
        encode_buf->fd, encode_buf->size, GST_FD_MEMORY_FLAG_DONT_CLOSE);
    // create a gst buffer
    outbuf = gst_buffer_new ();
    // insert fd memmory into the gstbuffer
    gst_buffer_append_memory (outbuf, mem);
    gst_object_unref (allocator);
  }

  if (outbuf) {
    gst_buffer_set_flags (outbuf, GST_BUFFER_FLAG_SYNC_AFTER);
    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (encode_buf->timestamp, GST_SECOND,
        1000000);
    if (vinfo->fps_n > 0) {
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (GST_SECOND,
          vinfo->fps_d, vinfo->fps_n);
    }

    GST_DEBUG_OBJECT (c2venc,
        "out buffer: %p, PTS: %lu, duration: %lu, fps_d: %d, fps_n: %d", outbuf,
        GST_BUFFER_PTS (outbuf), GST_BUFFER_DURATION (outbuf), vinfo->fps_d,
        vinfo->fps_n);

    /* Creates a new, empty GstStructure with the given name */
    structure = gst_structure_new_empty ("BUFFER");
    gst_structure_set (structure,
        "encoder", G_TYPE_POINTER, encoder,
        "index", G_TYPE_UINT64, encode_buf->index, NULL);
    /* Set a notification function to signal when the buffer is no longer used. */
    gst_mini_object_set_qdata (GST_MINI_OBJECT (outbuf),
        gst_c2_venc_qdata_quark (), structure,
        (GDestroyNotify) gst_c2_venc_buffer_release);

    frame->output_buffer = outbuf;

    gst_video_codec_frame_unref (frame);
    ret = gst_video_encoder_finish_frame (encoder, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (c2venc, "Failed to finish frame, outbuf: %p", outbuf);
      return GST_FLOW_ERROR;
    }

    GST_INFO_OBJECT (c2venc, "Finish frame");
  } else {
    GST_ERROR_OBJECT (c2venc, "Failed to create outbuf");
    return GST_FLOW_ERROR;
  }

  return ret;
}

static void
handle_video_event (EVENT_TYPE type, void * userdata, void * userdata2)
{
  GstVideoEncoder *encoder = (GstVideoEncoder *) userdata2;
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "handle_video_event");
  GstFlowReturn ret = GST_FLOW_OK;

  switch (type) {
    case EVENT_OUTPUTS_DONE: {
      BufferDescriptor *outBuffer = (BufferDescriptor *) userdata;

      GST_DEBUG_OBJECT (c2venc,
          "Event output done, index: %lu, fd: %u,"
          "filled len: %u, timestamp: %lu, flag: %x",
          outBuffer->index, outBuffer->fd,
          outBuffer->size, outBuffer->timestamp,
          outBuffer->flag);

      if (outBuffer->fd > 0 || outBuffer->size > 0) {
        ret = push_frame_downstream (encoder, outBuffer);
        if (ret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (c2venc, "Failed to push frame downstream");
        }
      } else if (outBuffer->flag & FLAG_TYPE_END_OF_STREAM) {
        GST_INFO_OBJECT (c2venc, "Encoder reached EOS");
        g_mutex_lock (&c2venc->pending_lock);
        c2venc->eos_reached = TRUE;
        g_cond_signal (&c2venc->pending_cond);
        g_mutex_unlock (&c2venc->pending_lock);
      } else {
        GST_ERROR_OBJECT (c2venc, "Invalid output buffer");
      }
      break;
    }
    case EVENT_TRIPPED:
      GST_ERROR_OBJECT (c2venc, "EVENT_TRIPPED(%d)", *(gint32 *) userdata);
      break;
    case EVENT_ERROR:
      GST_ERROR_OBJECT (c2venc, "EVENT_ERROR(%d)", *(gint32 *) userdata);
      break;
    default:
      GST_ERROR_OBJECT (c2venc, "Invalid Event(%d)", type);
  }
}

static gboolean
gst_c2_venc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GstStructure *structure;
  const gchar *fmt;
  gint retval = 0;
  gint width = 0;
  gint height = 0;
  gint rate_denominator = 0;
  GstVideoFormat input_format = GST_VIDEO_FORMAT_UNKNOWN;
  GPtrArray *config = NULL;
  GstCaps *peercaps;
  const gchar *profile_string, *level_string;
  video_profile_t profile = VIDEO_PROFILE_MAX;
  video_level_t level = VIDEO_LEVEL_MAX;
  config_params_t resolution;
  config_params_t pixelformat;
  config_params_t rate_control;
  config_params_t sync_frame_int;
  config_params_t roi_encoding;
  config_params_t intra_refresh;
  config_params_t bitrate;
  config_params_t slice_mode;
  config_params_t qp_ranges;
  config_params_t framerate;
  config_params_t entropy_mode;
  config_params_t loop_filter_mode;
  config_params_t qp_init;
  config_params_t num_ltr_frames;
  config_params_t profileLevel;
  config_params_t rotate;

  structure = gst_caps_get_structure (state->caps, 0);
  retval = gst_structure_get_int (structure, "width", &width);
  retval &= gst_structure_get_int (structure, "height", &height);
  if (!retval) {
    GST_ERROR_OBJECT (c2venc, "Unable to get width/height value");
    return FALSE;
  }

  retval = gst_structure_get_fraction (structure, "framerate",
      &c2venc->rate_numerator, &rate_denominator);
  if (!retval) {
    GST_ERROR_OBJECT (c2venc, "Unable to get framerate value");
    return FALSE;
  }

  fmt = gst_structure_get_string (structure, "format");
  if (fmt) {
    input_format = gst_video_format_from_string (fmt);
    if (input_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (c2venc, "Unsupported format in caps: %" GST_PTR_FORMAT,
        state->caps);
      return FALSE;
    }
  }

  if (c2venc->input_setup) {
    // Already setup, check to see if something has changed on input caps...
    if ((c2venc->width == width) && (c2venc->height == height)) {
      // Nothing to do
      c2venc->input_setup = TRUE;
      return TRUE;
    } else {
      gst_c2_venc_stop (encoder);
    }
  }

  c2venc->width = width;
  c2venc->height = height;
  c2venc->input_format = input_format;

  if (c2venc->input_state) {
    gst_video_codec_state_unref (c2venc->input_state);
  }

  c2venc->input_state = gst_video_codec_state_ref (state);

  if (GST_FLOW_OK != gst_c2_venc_setup_output (encoder, state)) {
    GST_ERROR_OBJECT (c2venc, "fail to setup output");
    return FALSE;
  }

  if (!gst_video_encoder_negotiate (encoder)) {
    GST_ERROR_OBJECT (c2venc, "Failed to negotiate with downstream");
    return FALSE;
  }

  // Create component
  if (!gst_c2_venc_wrapper_create_component (c2venc->wrapper,
      c2venc->comp_name, handle_video_event, encoder)) {
    GST_ERROR_OBJECT (c2venc, "Failed to create a component");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2venc,
      "set graphic pool with: %d, height: %d, format: %x",
      c2venc->width, c2venc->height, c2venc->input_format);

  // Set profile and level
  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder)));
  if (peercaps) {
    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (c2venc, "Empty caps");
      return FALSE;
    }

    structure = gst_caps_get_structure (peercaps, 0);
    profile_string = gst_structure_get_string (structure, "profile");
    if (profile_string) {
      profile = gst_codec2_get_profile_from_str(profile_string);
      if (profile == VIDEO_PROFILE_MAX) {
        GST_ERROR_OBJECT (c2venc, "Unsupported profile %s", profile_string);
        gst_caps_unref (peercaps);
        return FALSE;
      }
    }
    level_string = gst_structure_get_string (structure, "level");
    if (level_string) {
      level = gst_codec2_get_level_from_str(level_string);
      if (level == VIDEO_PROFILE_MAX) {
        GST_ERROR_OBJECT (c2venc, "Unsupported level %s", level_string);
        gst_caps_unref (peercaps);
        return FALSE;
      }
    }
    gst_caps_unref (peercaps);
  }

  config = g_ptr_array_new ();

  if ((profile != VIDEO_PROFILE_MAX) || (level != VIDEO_LEVEL_MAX)) {
    profileLevel = make_profileLevel_param (profile, level);
    g_ptr_array_add (config, &profileLevel);
    GST_DEBUG_OBJECT (c2venc, "setting profile %u and level %u", profile, level);
  }

  if (c2venc->target_bitrate > 0) {
    bitrate = make_bitrate_param (c2venc->target_bitrate, FALSE);
    g_ptr_array_add (config, &bitrate);
    GST_DEBUG_OBJECT (c2venc, "set target bitrate:%u", c2venc->target_bitrate);
  }

  if (c2venc->rate_numerator > 0 && rate_denominator > 0) {
    c2venc->framerate = c2venc->rate_numerator / rate_denominator;
    framerate = make_framerate_param (c2venc->framerate, FALSE);
    g_ptr_array_add (config, &framerate);
    GST_DEBUG_OBJECT (c2venc, "set target framerate:%f", c2venc->framerate);
  }

  resolution = make_resolution_param (width, height, TRUE);
  g_ptr_array_add (config, &resolution);

  if (c2venc->iframe_only) {
    sync_frame_int =
        make_sync_frame_interval_param (1 * 1e6 / c2venc->rate_numerator);
    g_ptr_array_add (config, &sync_frame_int);
    GST_DEBUG_OBJECT (c2venc, "I frame only mode");
  } else if (c2venc->idr_interval != 0) {
    sync_frame_int =
        make_sync_frame_interval_param (c2venc->idr_interval * 1000);
    g_ptr_array_add (config, &sync_frame_int);
    GST_DEBUG_OBJECT (c2venc, "IDR frame interval - %d", c2venc->idr_interval);
  }

  pixelformat =
      make_pixelFormat_param (gst_to_c2_pixelformat (input_format), TRUE);
  g_ptr_array_add (config, &pixelformat);

  rate_control = make_rateControl_param (c2venc->rcMode);
  g_ptr_array_add (config, &rate_control);

  if (c2venc->slice_mode != SLICE_MODE_DISABLE) {
    slice_mode = make_slicemode_param (c2venc->slice_size, c2venc->slice_mode);
    g_ptr_array_add (config, &slice_mode);
  }

  qp_ranges = make_qp_ranges_param (
      c2venc->min_qp_i_frames,c2venc->max_qp_i_frames,
      c2venc->min_qp_p_frames, c2venc->max_qp_p_frames,
      c2venc->min_qp_b_frames, c2venc->max_qp_b_frames);
  g_ptr_array_add (config, &qp_ranges);

  if (c2venc->intra_refresh_mode && c2venc->intra_refresh_mbs) {
    GST_DEBUG_OBJECT (c2venc, "set intra refresh mode: %d, mbs:%d",
        c2venc->intra_refresh_mode, c2venc->intra_refresh_mbs);
    intra_refresh =
        make_intraRefresh_param (c2venc->intra_refresh_mode,
        c2venc->intra_refresh_mbs);
    g_ptr_array_add (config, &intra_refresh);
  }

  if (c2venc->entropy_mode != ENTROPY_MODE_NONE) {
    entropy_mode = make_entropy_mode_param(c2venc->entropy_mode);
    g_ptr_array_add (config, &entropy_mode);
    GST_DEBUG_OBJECT (c2venc, "set entropy_mode - %d", c2venc->entropy_mode);
  }

  if (c2venc->loop_filter_mode != LOOP_FILTER_NONE) {
    loop_filter_mode = make_loop_filter_mode_param(c2venc->loop_filter_mode);
    g_ptr_array_add (config, &loop_filter_mode);
    GST_DEBUG_OBJECT (c2venc, "set loop_filter_mode - %d", c2venc->loop_filter_mode);
  }

  if ((c2venc->quant_i_frames != 0xFFFFFFFF)
   || (c2venc->quant_p_frames != 0xFFFFFFFF)
   || (c2venc->quant_b_frames != 0xFFFFFFFF)) {
    qp_init = make_qp_Init_param (c2venc->quant_i_frames,
        c2venc->quant_p_frames, c2venc->quant_b_frames);
    g_ptr_array_add (config, &qp_init);
    GST_DEBUG_OBJECT (c2venc, "set quant I frames: %d, quant P frames: %d, quant B frmes: %d",
    c2venc->quant_i_frames, c2venc->quant_p_frames, c2venc->quant_b_frames);
  }

  if (c2venc->num_ltr_frames != GST_CODEC2_VIDEO_ENC_NUM_LTR_FRAMES_DEFAULT) {
    num_ltr_frames = make_num_ltr_frames_param(c2venc->num_ltr_frames);
    g_ptr_array_add (config, &num_ltr_frames);
    GST_DEBUG_OBJECT (c2venc, "set LTR frames number - %d", c2venc->num_ltr_frames);
  }

  if (c2venc->rotate != ROTATE_NONE) {
    rotate = make_rotate_param(c2venc->rotate);
    g_ptr_array_add (config, &rotate);
    GST_DEBUG_OBJECT (c2venc, "set rotate - %d", c2venc->rotate);
  }

  // Config component
  if (!gst_c2_venc_wrapper_config_component (c2venc->wrapper, config)) {
    GST_ERROR_OBJECT (c2venc, "Failed to config interface");
  }

  g_ptr_array_free (config, FALSE);

  if (!gst_c2_venc_wrapper_component_start (c2venc->wrapper)) {
    GST_ERROR_OBJECT (c2venc, "Failed to start component");
  }

  c2venc->input_setup = TRUE;

  GST_DEBUG_OBJECT (c2venc, "gst_c2_venc_set_format");
  return TRUE;
}

static gboolean
gst_c2_venc_open (GstVideoEncoder * encoder)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "gst_c2_venc_open");

  return TRUE;
}

static gboolean
gst_c2_venc_close (GstVideoEncoder * encoder)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "gst_c2_venc_close");

  return TRUE;
}

static GstFlowReturn
gst_c2_venc_finish (GstVideoEncoder * encoder)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "gst_c2_venc_finish");

  gint64 timeout;
  BufferDescriptor inBuf;

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  inBuf.fd = -1;
  inBuf.size = 0;
  inBuf.timestamp = 0;
  inBuf.index = c2venc->frame_index;
  inBuf.flag = FLAG_TYPE_END_OF_STREAM;
  inBuf.pool_type = BUFFER_POOL_BASIC_GRAPHIC;

  // Setup EOS work
  if (!gst_c2_venc_wrapper_component_queue (c2venc->wrapper, &inBuf)) {
    GST_ERROR_OBJECT(c2venc, "failed to queue input frame to Codec2");
    return GST_FLOW_ERROR;
  }

  // This mutex was locked in the base class before call this function
  // Needs to be unlocked when waiting for a EOS signal
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  g_mutex_lock (&c2venc->pending_lock);
  if (!c2venc->eos_reached) {
    GST_DEBUG_OBJECT (c2venc, "wait until EOS signal is triggered");

    timeout =
        g_get_monotonic_time () + (EOS_WAITING_TIMEOUT * G_TIME_SPAN_SECOND);
    if (!g_cond_wait_until (&c2venc->pending_cond, &c2venc->pending_lock, timeout)) {
      GST_ERROR_OBJECT (c2venc, "Timed out on wait, exiting!");
    }
  } else {
    GST_DEBUG_OBJECT (c2venc, "EOS reached on output, finish the encoding");
  }

  g_mutex_unlock (&c2venc->pending_lock);
  // Lock the mutex again and return to the base class
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_c2_venc_encode (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "gst_c2_venc_encode");

  BufferDescriptor inBuf;
  GstBuffer *buf = NULL;
  GstMemory *mem;
  GstMapInfo mapinfo = { 0, };

  if (!frame) {
    GST_WARNING_OBJECT (c2venc, "frame is NULL, ret GST_FLOW_EOS");
    return GST_FLOW_EOS;
  }

  // This mutex was locked in the base class before call this function
  // Needs to be unlocked in case we run out of free pending works of
  // the encoder. If not unlocked here it can cause a dead lock because the
  // gst_video_encoder_finish_frame cannot be called if gst_c2_venc_handle_frame
  // is not returned.
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  buf = frame->input_buffer;
  mem = gst_buffer_get_memory (buf, 0);
  if (gst_is_fd_memory (mem)) {
    GST_DEBUG_OBJECT (c2venc, "FD buffer");

    inBuf.fd = gst_fd_memory_get_fd (mem);
    inBuf.size = gst_memory_get_sizes (mem, NULL, NULL);
  } else {
    GST_DEBUG_OBJECT(c2venc, "Not FD memory");
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    inBuf.fd = -1;
    inBuf.data = mapinfo.data;
    inBuf.size = mapinfo.size;
  }

  inBuf.timestamp = frame->pts / 1000;
  inBuf.index = frame->system_frame_number;
  inBuf.pool_type = BUFFER_POOL_BASIC_GRAPHIC;
  inBuf.width = c2venc->width;
  inBuf.height = c2venc->height;
  inBuf.format = c2venc->input_format;

  gst_memory_unref (mem);

  GST_DEBUG_OBJECT (c2venc,
      "input buffer: fd: %d, size: %d, timestamp: %lu, index: %ld",
      inBuf.fd, inBuf.size, inBuf.timestamp, inBuf.index);

  // Keep track of queued frame
  c2venc->queued_frame[(c2venc->frame_index) % MAX_QUEUED_FRAME] =
      frame->system_frame_number;

  config_roi_encoding (c2venc, inBuf.timestamp);

  // Queue buffer to Codec2
  if (!gst_c2_venc_wrapper_component_queue (c2venc->wrapper, &inBuf)) {
    GST_ERROR_OBJECT(c2venc, "failed to queue input frame to Codec2");
    // Lock the mutex again and return to the base class
    GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
    return GST_FLOW_ERROR;
  }

  if (inBuf.fd == -1) {
    gst_buffer_unmap (buf, &mapinfo);
  }

  g_mutex_lock (&(c2venc->pending_lock));
  c2venc->frame_index += 1;
  g_mutex_unlock (&(c2venc->pending_lock));

  // Lock the mutex again and return to the base class
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_c2_venc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (encoder);

  if (!c2venc->input_setup) {
    GST_ERROR_OBJECT (c2venc, "Input setup is NULL");
    return GST_FLOW_OK;
  }

  if (!c2venc->output_setup) {
    GST_ERROR_OBJECT (c2venc, "Output setup is NULL");
    return GST_FLOW_ERROR;
  }

  if (!frame) {
    GST_INFO_OBJECT (c2venc, "EOS received");
    return GST_FLOW_EOS;
  }

  GST_DEBUG ("Frame number : %d, pts: %" GST_TIME_FORMAT,
      frame->system_frame_number, GST_TIME_ARGS (frame->pts));

  // Encode frame
  return gst_c2_venc_encode (encoder, frame);
}

static void
gst_c2_venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (c2venc);
  GPtrArray *config = g_ptr_array_new ();
  gboolean config_apply = FALSE;

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (c2venc, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (c2venc);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
      c2venc->rcMode = (rc_mode_t) g_value_get_enum (value);
      break;
    case PROP_INTRA_REFRESH_MODE:
      c2venc->intra_refresh_mode = (ir_mode_t) g_value_get_enum (value);
      break;
    case PROP_INTRA_REFRESH_MBS:
      c2venc->intra_refresh_mbs = g_value_get_uint (value);
      break;
    case PROP_TARGET_BITRATE:

      c2venc->target_bitrate = g_value_get_uint (value);

      config_params_t bitrate;
      if (c2venc->target_bitrate > 0) {
        bitrate = make_bitrate_param (c2venc->target_bitrate, FALSE);
        g_ptr_array_add (config, &bitrate);
        GST_DEBUG_OBJECT (c2venc, "set target bitrate:%u", c2venc->target_bitrate);
      }
      config_apply = TRUE;

      break;
    case PROP_I_FRAME_ONLY:
      c2venc->iframe_only = g_value_get_boolean (value);

      config_params_t sync_frame_int;
      if (c2venc->iframe_only) {
        sync_frame_int =
            make_sync_frame_interval_param (1 * 1e6 / c2venc->rate_numerator);
        g_ptr_array_add (config, &sync_frame_int);
        GST_DEBUG_OBJECT (c2venc, "I frame only mode");
      }
      config_apply = TRUE;

      break;
    case PROP_IDR_FRAME_INTERVAL:
      c2venc->idr_interval = g_value_get_uint (value);

      if (c2venc->idr_interval != 0) {
        sync_frame_int =
            make_sync_frame_interval_param (c2venc->idr_interval * 1000);
        g_ptr_array_add (config, &sync_frame_int);
        GST_DEBUG_OBJECT (c2venc, "IDR frame interval - %d", c2venc->idr_interval);
      }
      config_apply = TRUE;

      break;
    case PROP_SLICE_SIZE:
      c2venc->slice_size = g_value_get_uint (value);
      break;
    case PROP_SLICE_MODE:
      c2venc->slice_mode = (slice_mode_t) g_value_get_enum (value);
      break;
    case PROP_MAX_QP_B_FRAMES:
      c2venc->max_qp_b_frames = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_I_FRAMES:
      c2venc->max_qp_i_frames = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_P_FRAMES:
      c2venc->max_qp_p_frames = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_B_FRAMES:
      c2venc->min_qp_b_frames = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_I_FRAMES:
      c2venc->min_qp_i_frames = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_P_FRAMES:
      c2venc->min_qp_p_frames = g_value_get_uint (value);
      break;
    case PROP_ROI:
      g_return_if_fail (gst_value_array_get_size (value) == 4);

      c2venc->roi_encoding.top =
          g_value_get_int (gst_value_array_get_value (value, 0));
      c2venc->roi_encoding.left =
          g_value_get_int (gst_value_array_get_value (value, 1));
      c2venc->roi_encoding.bottom =
          g_value_get_int (gst_value_array_get_value (value, 2));
      c2venc->roi_encoding.right =
          g_value_get_int (gst_value_array_get_value (value, 3));

      break;
    case PROP_ROI_QP_DELTA:
      c2venc->roi_encoding_qp_delta = g_value_get_int (value);
      break;
    case PROP_ENTROPY_MODE:
      c2venc->entropy_mode = (entropy_mode_t) g_value_get_enum (value);
      break;
    case PROP_LOOP_FILTER_MODE:
      c2venc->loop_filter_mode = (loop_filter_mode_t) g_value_get_enum (value);
      break;
    case PROP_QUANT_I_FRAMES:
      c2venc->quant_i_frames = g_value_get_uint (value);
      break;
    case PROP_QUANT_P_FRAMES:
      c2venc->quant_p_frames = g_value_get_uint (value);
      break;
    case PROP_QUANT_B_FRAMES:
      c2venc->quant_b_frames = g_value_get_uint (value);
      break;
    case PROP_NUM_LTR_FRAMES:
      c2venc->num_ltr_frames = g_value_get_uint (value);
      break;
    case PROP_ROTATE:
      c2venc->rotate = (rotate_t) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  // Config component
  if (config_apply) {
    if (!gst_c2_venc_wrapper_config_component (c2venc->wrapper, config)) {
      GST_ERROR_OBJECT (c2venc, "Failed to config interface");
    }
  }

  g_ptr_array_free (config, FALSE);

  GST_OBJECT_UNLOCK (c2venc);
}

static void
gst_c2_venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (object);

  GST_OBJECT_LOCK (c2venc);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, c2venc->rcMode);
      break;
    case PROP_INTRA_REFRESH_MODE:
      g_value_set_enum (value, c2venc->intra_refresh_mode);
      break;
    case PROP_INTRA_REFRESH_MBS:
      g_value_set_uint (value, c2venc->intra_refresh_mbs);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, c2venc->target_bitrate);
      break;
    case PROP_I_FRAME_ONLY:
      g_value_set_boolean (value, c2venc->iframe_only);
      break;
    case PROP_IDR_FRAME_INTERVAL:
      g_value_set_uint (value, c2venc->idr_interval);
      break;
    case PROP_SLICE_SIZE:
      g_value_set_uint (value, c2venc->slice_size);
      break;
    case PROP_SLICE_MODE:
      g_value_set_enum (value, c2venc->slice_mode);
      break;
    case PROP_MAX_QP_B_FRAMES:
      g_value_set_uint (value, c2venc->max_qp_b_frames);
      break;
    case PROP_MAX_QP_I_FRAMES:
      g_value_set_uint (value, c2venc->max_qp_i_frames);
      break;
    case PROP_MAX_QP_P_FRAMES:
      g_value_set_uint (value, c2venc->max_qp_p_frames);
      break;
    case PROP_MIN_QP_B_FRAMES:
      g_value_set_uint (value, c2venc->min_qp_b_frames);
      break;
    case PROP_MIN_QP_I_FRAMES:
      g_value_set_uint (value, c2venc->min_qp_i_frames);
      break;
    case PROP_MIN_QP_P_FRAMES:
      g_value_set_uint (value, c2venc->min_qp_p_frames);
      break;
    case PROP_ROI:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, c2venc->roi_encoding.top);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, c2venc->roi_encoding.left);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, c2venc->roi_encoding.bottom);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, c2venc->roi_encoding.right);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_ROI_QP_DELTA:
      g_value_set_int (value, c2venc->roi_encoding_qp_delta);
      break;
    case PROP_ENTROPY_MODE:
      g_value_set_enum (value, c2venc->entropy_mode);
      break;
    case PROP_LOOP_FILTER_MODE:
      g_value_set_enum (value, c2venc->loop_filter_mode);
      break;
    case PROP_QUANT_I_FRAMES:
      g_value_set_uint (value, c2venc->quant_i_frames);
      break;
    case PROP_QUANT_P_FRAMES:
      g_value_set_uint (value, c2venc->quant_p_frames);
      break;
    case PROP_QUANT_B_FRAMES:
      g_value_set_uint (value, c2venc->quant_b_frames);
      break;
    case PROP_NUM_LTR_FRAMES:
      g_value_set_uint (value, c2venc->num_ltr_frames);
      break;
    case PROP_ROTATE:
      g_value_set_enum (value, c2venc->rotate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (c2venc);
}

static void
gst_c2_venc_finalize (GObject * object)
{
  GstC2_VENCEncoder *c2venc = GST_C2_VENC_ENC (object);

  if (c2venc->input_state) {
    gst_video_codec_state_unref (c2venc->input_state);
    c2venc->input_state = NULL;
  }

  g_mutex_clear (&c2venc->pending_lock);
  g_cond_clear (&c2venc->pending_cond);

  if (c2venc->comp_name) {
    g_free (c2venc->comp_name);
    c2venc->comp_name = NULL;
  }

  gst_c2_venc_wrapper_delete_component (c2venc->wrapper);

  if (c2venc->wrapper != NULL) {
    gst_c2_wrapper_free (c2venc->wrapper);
    c2venc->wrapper = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (c2venc));
}

static void
gst_c2_venc_class_init (GstC2_VENCEncoderClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_c2_venc_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_c2_venc_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_c2_venc_finalize);

  g_object_class_install_property (gobject, PROP_RATE_CONTROL,
      g_param_spec_enum ("control-rate", "Rate Control",
          "Bitrate control method",
          GST_TYPE_CODEC2_ENC_RATE_CONTROL,
          RC_MODE_OFF,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_INTRA_REFRESH_MODE,
      g_param_spec_enum ("intra-refresh-mode", "Intra refresh mode",
          "Intra refresh mode, only support random mode. Allow IR only for CBR(_CFR/VFR) RC modes",
          GST_TYPE_CODEC2_ENC_INTRA_REFRESH_MODE,
          IR_MODE_NONE,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_INTRA_REFRESH_MBS,
      g_param_spec_uint ("intra-refresh-mbs", "Intra refresh mbs/period",
          "For random modes, it means period of intra refresh. Only support random mode.",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target bitrate",
          "Target bitrate in bits per second (0 means not explicitly set bitrate)",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_I_FRAME_ONLY,
      g_param_spec_boolean ("iframe-only", "I Frame only",
          "I Frame only mode", FALSE,
          static_cast<GParamFlags>(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_IDR_FRAME_INTERVAL,
      g_param_spec_uint ("idr-interval", "IDR frame interval",
          "IDR frame interval (0 means not explicitly set IDR interval)",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "slice mode",
          "Slice mode, support MB and BYTES mode",
          GST_TYPE_CODEC2_ENC_SLICE_MODE,
          SLICE_MODE_DISABLE,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLICE_SIZE,
      g_param_spec_uint ("slice-size", "Slice size",
          "Slice size, just set when slice mode setting to MB or Bytes",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_MAX_QP_B_FRAMES,
      g_param_spec_uint ("max-quant-b-frames", "Max quant B frames",
          "Maximum quantization parameter allowed for B-frames",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_MAX_QP_I_FRAMES,
      g_param_spec_uint ("max-quant-i-frames", "Max quant I frames",
          "Maximum quantization parameter allowed for I-frames",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_MAX_QP_P_FRAMES,
      g_param_spec_uint ("max-quant-p-frames", "Max quant P frames",
          "Maximum quantization parameter allowed for P-frames",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_MIN_QP_B_FRAMES,
      g_param_spec_uint ("min-quant-b-frames", "Min quant B frames",
          "Minimum quantization parameter allowed for B-frames",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_MIN_QP_I_FRAMES,
      g_param_spec_uint ("min-quant-i-frames", "Min quant I frames",
          "Minimum quantization parameter allowed for I-frames",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_MIN_QP_P_FRAMES,
      g_param_spec_uint ("min-quant-p-frames", "Min quant P frames",
          "Minimum quantization parameter allowed for P-frames",
          0, G_MAXUINT, 0,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_ROI,
      gst_param_spec_array ("roi", "ROI Encoding",
          "The ROI rectangle inside the input ('<Top, Left, Bottom, Right>')",
          g_param_spec_int ("value", "ROI Value",
              "One of Top, Left, Bottom or Right value.", 0, G_MAXINT, 0,
              static_cast<GParamFlags>(G_PARAM_WRITABLE |
              G_PARAM_STATIC_STRINGS)),
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_ROI_QP_DELTA,
      g_param_spec_int ("roi-qp", "ROI Encoding QP delta",
          "ROI encoding QP delta",
          G_MININT, G_MAXINT, DEFAULT_PROP_ROI_QP_DELTA,
          static_cast<GParamFlags>(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  // Signal for trigger I frame insertion
  g_signal_new_class_handler ("trigger-iframe", G_TYPE_FROM_CLASS (klass),
      static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_c2_venc_trigger_iframe),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  g_object_class_install_property (gobject, PROP_ENTROPY_MODE,
      g_param_spec_enum ("entropy-mode", "Entropy Mode",
          "Entropy mode for encoding process",
          GST_TYPE_CODEC2_ENC_ENTROPY_MODE,
          ENTROPY_MODE_NONE,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_LOOP_FILTER_MODE,
      g_param_spec_enum ("loop-filter-mode", "Loop Filter mode",
          "Enable or disable the deblocking filter",
          GST_TYPE_CODEC2_ENC_LOOP_FILTER_MODE,
          LOOP_FILTER_NONE,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_QUANT_I_FRAMES,
      g_param_spec_uint ("quant-i-frames", "I-Frame Quantization",
          "Quantization parameter for I-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_CODEC2_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_QUANT_P_FRAMES,
      g_param_spec_uint ("quant-p-frames", "P-Frame Quantization",
          "Quantization parameter for P-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_CODEC2_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_QUANT_P_FRAMES,
      g_param_spec_uint ("quant-b-frames", "B-Frame Quantization",
          "Quantization parameter for B-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_CODEC2_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_NUM_LTR_FRAMES,
      g_param_spec_uint ("num-ltr-frames", "LTR Frames Count",
          "Number of Long Term Reference Frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_CODEC2_VIDEO_ENC_NUM_LTR_FRAMES_DEFAULT,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject, PROP_ROTATE,
      g_param_spec_enum ("rotate", "Rotate frames",
          "Rotate video image",
          GST_TYPE_CODEC2_ENC_ROTATE,
          ROTATE_NONE,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_static_metadata (element,
      "C2Venc encoder", "C2_VENC/Encoder",
      "C2Venc encoding", "QTI");

  gst_element_class_add_static_pad_template (element,
      &gst_c2_venc_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_c2_venc_src_pad_template);

  venc_class->start = gst_c2_venc_start;
  venc_class->stop = gst_c2_venc_stop;
  venc_class->set_format = gst_c2_venc_set_format;
  venc_class->handle_frame = gst_c2_venc_handle_frame;
  venc_class->finish = gst_c2_venc_finish;
  venc_class->open = gst_c2_venc_open;
  venc_class->close = gst_c2_venc_close;
}

static void
gst_c2_venc_init (GstC2_VENCEncoder * c2venc)
{
  c2venc->input_setup = FALSE;
  c2venc->output_setup = FALSE;
  c2venc->input_state = NULL;
  c2venc->output_state = NULL;
  c2venc->width = 0;
  c2venc->height = 0;
  c2venc->frame_index = 0;
  c2venc->eos_reached = FALSE;
  c2venc->rate_numerator = 0;

  c2venc->rcMode = RC_MODE_OFF;
  c2venc->target_bitrate = 0;
  c2venc->framerate = 0;
  c2venc->slice_size = 0;
  c2venc->iframe_only = FALSE;
  c2venc->idr_interval = 0;

  c2venc->max_qp_b_frames = 0;
  c2venc->max_qp_i_frames = 0;
  c2venc->max_qp_p_frames = 0;
  c2venc->min_qp_b_frames = 0;
  c2venc->min_qp_i_frames = 0;
  c2venc->min_qp_p_frames = 0;

  c2venc->roi_encoding.top = 0;
  c2venc->roi_encoding.left = 0;
  c2venc->roi_encoding.bottom = 0;
  c2venc->roi_encoding.right = 0;
  c2venc->roi_encoding_qp_delta = 0;

  c2venc->entropy_mode = ENTROPY_MODE_NONE;
  c2venc->loop_filter_mode = LOOP_FILTER_NONE;
  c2venc->quant_i_frames = GST_CODEC2_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT;
  c2venc->quant_p_frames = GST_CODEC2_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT;
  c2venc->quant_b_frames = GST_CODEC2_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT;
  c2venc->num_ltr_frames = GST_CODEC2_VIDEO_ENC_NUM_LTR_FRAMES_DEFAULT;
  c2venc->rotate = ROTATE_NONE;

  memset (c2venc->queued_frame, 0, sizeof (c2venc->queued_frame));

  g_cond_init (&c2venc->pending_cond);
  g_mutex_init (&c2venc->pending_lock);

  c2venc->wrapper = gst_c2_wrapper_new ();
  g_return_if_fail (c2venc->wrapper != NULL);

  GST_DEBUG_CATEGORY_INIT (c2_venc_debug, "qtic2venc", 0,
      "QTI c2venc encoder");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtic2venc", GST_RANK_PRIMARY,
      GST_TYPE_C2_VENC_ENC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtic2venc,
    "C2Venc encoding",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
