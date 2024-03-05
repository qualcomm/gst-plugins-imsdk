/*
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#define GST_CAT_DEFAULT c2_venc_debug
GST_DEBUG_CATEGORY_STATIC (c2_venc_debug);

#define gst_c2_venc_parent_class parent_class
G_DEFINE_TYPE (GstC2VEncoder, gst_c2_venc, GST_TYPE_VIDEO_ENCODER);

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
  ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
      : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
          : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
              : (state <= GST_STATE_NULL))))

#define GPOINTER_CAST(ptr)                ((gpointer) ptr)

#define GST_TYPE_C2_RATE_CONTROL       (gst_c2_rate_control_get_type())
#define GST_TYPE_C2_INTRA_REFRESH_MODE (gst_c2_intra_refresh_get_type())
#define GST_TYPE_C2_ENTROPY_MODE       (gst_c2_entropy_get_type())
#define GST_TYPE_C2_LOOP_FILTER_MODE   (gst_c2_loop_filter_get_type())
#define GST_TYPE_C2_SLICE_MODE         (gst_c2_slice_get_type())
#define GST_TYPE_C2_VIDEO_ROTATION     (gst_c2_video_rotation_get_type())

#define DEFAULT_PROP_ROTATE               (GST_C2_ROTATE_NONE)
#define DEFAULT_PROP_RATE_CONTROL         (GST_C2_RATE_CTRL_DISABLE)
#define DEFAULT_PROP_TARGET_BITRATE       (0xffffffff)
#define DEFAULT_PROP_IDR_INTERVAL         (0xffffffff)
#define DEFAULT_PROP_INTRA_REFRESH_MODE   (0xffffffff)
#define DEFAULT_PROP_INTRA_REFRESH_PERIOD (0)
#define DEFAULT_PROP_B_FRAMES             (0xffffffff)
#define DEFAULT_PROP_QUANT_I_FRAMES       (0xffffffff)
#define DEFAULT_PROP_QUANT_P_FRAMES       (0xffffffff)
#define DEFAULT_PROP_QUANT_B_FRAMES       (0xffffffff)
#define DEFAULT_PROP_MIN_QP_I_FRAMES      (10)
#define DEFAULT_PROP_MAX_QP_I_FRAMES      (51)
#define DEFAULT_PROP_MIN_QP_P_FRAMES      (10)
#define DEFAULT_PROP_MAX_QP_P_FRAMES      (51)
#define DEFAULT_PROP_MIN_QP_B_FRAMES      (10)
#define DEFAULT_PROP_MAX_QP_B_FRAMES      (51)
#define DEFAULT_PROP_ROI_QUANT_MODE       (FALSE)
#define DEFAULT_PROP_ROI_QP_DELTA         (-15)
#define DEFAULT_PROP_SLICE_MODE           (0xffffffff)
#define DEFAULT_PROP_SLICE_SIZE           (0)
#define DEFAULT_PROP_ENTROPY_MODE         (0xffffffff)
#define DEFAULT_PROP_LOOP_FILTER_MODE     (0xffffffff)
#define DEFAULT_PROP_NUM_LTR_FRAMES       (0xffffffff)
#define DEFAULT_PROP_PRIORITY             (0xffffffff)

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_VIDEO_FORMATS "{ NV12, NV12_10LE32, P010_10LE }"

enum
{
  PROP_0,
  PROP_ROTATE,
  PROP_RATE_CONTROL,
  PROP_TARGET_BITRATE,
  PROP_IDR_INTERVAL,
  PROP_INTRA_REFRESH_MODE,
  PROP_INTRA_REFRESH_PERIOD,
  PROP_B_FRAMES,
  PROP_QUANT_I_FRAMES,
  PROP_QUANT_P_FRAMES,
  PROP_QUANT_B_FRAMES,
  PROP_MAX_QP_B_FRAMES,
  PROP_MAX_QP_I_FRAMES,
  PROP_MAX_QP_P_FRAMES,
  PROP_MIN_QP_B_FRAMES,
  PROP_MIN_QP_I_FRAMES,
  PROP_MIN_QP_P_FRAMES,
  PROP_ROI_QUANT_MODE,
  PROP_ROI_QUANT_META_VALUE,
  PROP_ROI_QUANT_BOXES,
  PROP_SLICE_MODE,
  PROP_SLICE_SIZE,
  PROP_ENTROPY_MODE,
  PROP_LOOP_FILTER_MODE,
  PROP_NUM_LTR_FRAMES,
  PROP_PRIORITY,
};

static GstStaticPadTemplate gst_c2_venc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
);

static GstStaticPadTemplate gst_c2_venc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) byte-stream,"
        "alignment = (string) au;"
        "video/x-h265,"
        "stream-format = (string) byte-stream,"
        "alignment = (string) au;"
        "image/heic")
);

static GType
gst_c2_rate_control_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_C2_RATE_CTRL_DISABLE, "Disable bitrate control", "disable" },
    { GST_C2_RATE_CTRL_CONSTANT, "Constant bitrate", "constant" },
    { GST_C2_RATE_CTRL_CBR_VFR, "Constant bitrate, variable framerate", "CBR-VFR" },
    { GST_C2_RATE_CTRL_VBR_CFR, "Variable bitrate, constant framerate", "VBR-CFR" },
    { GST_C2_RATE_CTRL_VBR_VFR, "Variable bitrate, variable framerate", "VBR-VFR" },
    { GST_C2_RATE_CTRL_CQ, "Constant quality", "CQ"},
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2RateControl", variants);

  return gtype;
}

static GType
gst_c2_intra_refresh_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_C2_INTRA_REFRESH_DISABLED, "No intra resfresh", "disable" },
    { GST_C2_INTRA_REFRESH_ARBITRARY, "Arbitrary", "arbitrary" },
    { GST_C2_INTRA_REFRESH_CYCLIC, "Cyclic", "cyclic" },
    { 0xffffffff, "Component Default", "default" },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2IntraRefresh", variants);

  return gtype;
}

static GType
gst_c2_entropy_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_C2_ENTROPY_CAVLC, "CAVLC", "cavlc" },
    { GST_C2_ENTROPY_CABAC, "CABAC", "cabac" },
    { 0xffffffff, "Component Default", "default" },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2EntropyMode", variants);

  return gtype;
}

static GType
gst_c2_loop_filter_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_C2_LOOP_FILTER_ENABLE, "Enable", "enable" },
    { GST_C2_LOOP_FILTER_DISABLE, "Disable", "disable" },
    { GST_C2_LOOP_FILTER_DISABLE_SLICE_BOUNDARY,
        "Disable-slice-boundary", "disable-slice-boundary" },
    { 0xffffffff, "Component Default", "default" },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2LoopFilterMode", variants);

  return gtype;
}

static GType
gst_c2_slice_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_C2_SLICE_MB, "Megabytes slice mode", "MB" },
    { GST_C2_SLICE_BYTES, "Bytes slice mode", "bytes" },
    { 0xffffffff, "Component Default", "default" },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2SliceMode", variants);

  return gtype;
}

static GType
gst_c2_video_rotation_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_C2_ROTATE_NONE, "No rotation", "none" },
    { GST_C2_ROTATE_90_CW, "Rotate 90 degrees clockwise", "90CW" },
    { GST_C2_ROTATE_90_CCW, "Rotate 90 degrees counter-clockwise", "90CCW" },
    { GST_C2_ROTATE_180, "Rotate 180 degrees", "180" },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2VideoRotation", variants);

  return gtype;
}

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

static gboolean
gst_caps_has_subformat (const GstCaps * caps, const gchar * subformat)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "subformat") ?
      gst_structure_get_string (structure, "subformat") : NULL;

  return (g_strcmp0 (string, subformat) == 0) ? TRUE : FALSE;
}

static gboolean
gst_c2_venc_trigger_iframe (GstC2VEncoder * c2venc)
{
  gboolean success = FALSE, enable = TRUE;

  GST_DEBUG_OBJECT (c2venc, "Trigger I frame insertion");

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_TRIGGER_SYNC_FRAME, GPOINTER_CAST (&enable));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set sync frame parameter!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_c2_venc_ltr_mark (GstC2VEncoder * c2venc, guint id)
{
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (c2venc, "LTR Mark index %d", id);

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_LTR_MARK, GPOINTER_CAST (&id));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set ltr mark index!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_c2_venc_setup_parameters (GstC2VEncoder * c2venc,
    GstVideoCodecState * state)
{
  GstVideoInfo *info = &state->info;
  GstC2PixelInfo pixinfo = { GST_VIDEO_FORMAT_UNKNOWN, FALSE };
  GstC2Resolution resolution = { 0, 0 };
  GstC2Gop gop = { 0, 0 };
  GstC2HeaderMode csdmode = GST_C2_PREPEND_HEADER_TO_ALL_SYNC;
  gdouble framerate = 0.0;
  gboolean success = FALSE;

  pixinfo.format = GST_VIDEO_INFO_FORMAT (info);
  pixinfo.isubwc = c2venc->isubwc;

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_IN_PIXEL_FORMAT, GPOINTER_CAST (&pixinfo));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set input format parameter!");
    return FALSE;
  }

  resolution.width = GST_VIDEO_INFO_WIDTH (info);
  resolution.height = GST_VIDEO_INFO_HEIGHT (info);

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_IN_RESOLUTION, GPOINTER_CAST (&resolution));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set input resolution parameter!");
    return FALSE;
  }

  gst_util_fraction_to_double (GST_VIDEO_INFO_FPS_N (info),
      GST_VIDEO_INFO_FPS_D (info), &framerate);

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_OUT_FRAMERATE, GPOINTER_CAST (&framerate));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set output framerate parameter!");
    return FALSE;
  }

#if defined(CODEC2_CONFIG_VERSION_2_0)
  gboolean enable = TRUE;

  // Enable codec2 avg qp info report, only avaiable in h264/h265.
  if (g_str_has_suffix (c2venc->name, "heic.encoder") == FALSE ) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_REPORT_AVG_QP, GPOINTER_CAST (&(enable)));

    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to enable QP report parameter!");
      return FALSE;
    }
  }
#endif // CODEC2_CONFIG_VERSION_2_0

  if (c2venc->priority != DEFAULT_PROP_PRIORITY) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_PRIORITY, GPOINTER_CAST (&(c2venc->priority)));

    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set video priority parameter!");
      return FALSE;
    }
  }

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_RATE_CONTROL, GPOINTER_CAST (&(c2venc->control_rate)));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set rate control parameter!");
    return FALSE;
  }

  if (c2venc->target_bitrate != DEFAULT_PROP_TARGET_BITRATE) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_BITRATE, GPOINTER_CAST (&(c2venc->target_bitrate)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set bitrate parameter!");
      return FALSE;
    }
  }

  if (c2venc->idr_interval != DEFAULT_PROP_IDR_INTERVAL) {
    gint64 key_frame_interval = c2venc->idr_interval * (1000000 / framerate);

    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_KEY_FRAME_INTERVAL, GPOINTER_CAST (&(key_frame_interval)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set key frame interval parameter!");
      return FALSE;
    }
  }

  if (c2venc->intra_refresh.mode != DEFAULT_PROP_INTRA_REFRESH_MODE) {

    if (c2venc->intra_refresh.mode == GST_C2_INTRA_REFRESH_DISABLED) {
      GST_INFO_OBJECT (c2venc, "Intra refresh mode is set to disable, "
        "resetting period to 0");
      c2venc->intra_refresh.period = 0;
    }

    // this configuration just set intra refresh period in codec2 V2
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_INTRA_REFRESH_TUNING,
        GPOINTER_CAST (&(c2venc->intra_refresh)));

    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set intra refresh tuning!");
      return FALSE;
    }

#if defined(CODEC2_CONFIG_VERSION_2_0)
    if (c2venc->intra_refresh.mode != GST_C2_INTRA_REFRESH_DISABLED) {
      success = gst_c2_engine_set_parameter (c2venc->engine,
          GST_C2_PARAM_INTRA_REFRESH_MODE,
          GPOINTER_CAST (&(c2venc->intra_refresh.mode)));

      if (!success) {
        GST_ERROR_OBJECT (c2venc, "Failed to set intra refresh mode!");
        return FALSE;
      }
    }
#endif // CODEC2_CONFIG_VERSION_2_0
  }

  success = gst_c2_engine_get_parameter (c2venc->engine,
      GST_C2_PARAM_GOP_CONFIG, GPOINTER_CAST (&gop));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to get GOP parameter!");
    return FALSE;
  }

  if (c2venc->idr_interval != DEFAULT_PROP_IDR_INTERVAL)
    gop.n_pframes = c2venc->idr_interval;

  if (c2venc->bframes != DEFAULT_PROP_B_FRAMES)
    gop.n_bframes = c2venc->bframes;

  // Overwrite B-Frames if IDR is set to 0 (key frames only)
  if (c2venc->idr_interval == 0)
    gop.n_bframes = 0;

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_GOP_CONFIG, GPOINTER_CAST (&gop));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set GOP parameter!");
    return FALSE;
  }

  if (c2venc->bframes != DEFAULT_PROP_B_FRAMES) {
    gboolean enable = TRUE;

#if !defined(CODEC2_CONFIG_VERSION_2_0)
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_ADAPTIVE_B_FRAMES, GPOINTER_CAST (&enable));

    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set adaptive B frames parameter!");
      return FALSE;
    }
#else
    gfloat ratio = 0.0;
    GstC2TemporalLayer templayer = {2, 2, NULL};

    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_NATIVE_RECORDING, GPOINTER_CAST (&enable));

    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to enable native recording!");
      return FALSE;
    }

    // bitrate ratios are bypassed in component now
    templayer.bitrate_ratios = g_array_new (FALSE, FALSE, sizeof (gfloat));
    ratio = 0.5;
    g_array_append_val (templayer.bitrate_ratios, ratio);
    ratio = 1.0;
    g_array_append_val (templayer.bitrate_ratios, ratio);

    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_TEMPORAL_LAYERING, GPOINTER_CAST (&templayer));

    if (templayer.bitrate_ratios != NULL) {
      g_array_free (templayer.bitrate_ratios, TRUE);
      templayer.bitrate_ratios = NULL;
    }

    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set temporal layering parameter!");
      return FALSE;
    }
#endif // CODEC2_CONFIG_VERSION_2_0
  }

  if (c2venc->entropy_mode != DEFAULT_PROP_ENTROPY_MODE) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_ENTROPY_MODE, GPOINTER_CAST (&(c2venc->entropy_mode)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set key entropy mode parameter!");
      return FALSE;
    }
  }

  if (c2venc->loop_filter_mode != DEFAULT_PROP_LOOP_FILTER_MODE) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_LOOP_FILTER_MODE, GPOINTER_CAST (&(c2venc->loop_filter_mode)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set loop filter parameter!");
      return FALSE;
    }
  }

  if (c2venc->slice_mode == GST_C2_SLICE_MB) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_SLICE_MB, GPOINTER_CAST (&(c2venc->slice_size)));
  } else if (c2venc->slice_mode == GST_C2_SLICE_BYTES) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_SLICE_BYTES, GPOINTER_CAST (&(c2venc->slice_size)));
  }

  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set slice parameter!");
    return FALSE;
  }

  if (c2venc->num_ltr_frames != DEFAULT_PROP_NUM_LTR_FRAMES) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_NUM_LTR_FRAMES, GPOINTER_CAST (&(c2venc->num_ltr_frames)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set LTR frames parameter!");
      return FALSE;
    }
  }

  if (c2venc->rotate != GST_C2_ROTATE_NONE) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_ROTATION, GPOINTER_CAST (&(c2venc->rotate)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set rotation parameter!");
      return FALSE;
    }
  }

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_PREPEND_HEADER_MODE, GPOINTER_CAST (&csdmode));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set prepend SPS/PPS header parameter!");
    return FALSE;
  }

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_QP_RANGES, GPOINTER_CAST (&(c2venc->quant_ranges)));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set QP ranges parameter!");
    return FALSE;
  }

  if ((c2venc->quant_init.i_frames != DEFAULT_PROP_QUANT_I_FRAMES) ||
      (c2venc->quant_init.p_frames != DEFAULT_PROP_QUANT_P_FRAMES) ||
      (c2venc->quant_init.b_frames != DEFAULT_PROP_QUANT_B_FRAMES)) {
    success = gst_c2_engine_set_parameter (c2venc->engine,
        GST_C2_PARAM_QP_INIT, GPOINTER_CAST (&(c2venc->quant_init)));
    if (!success) {
      GST_ERROR_OBJECT (c2venc, "Failed to set QP init parameter!");
      return FALSE;
    }
  }

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_COLOR_ASPECTS_TUNING,
      GPOINTER_CAST (&info->colorimetry));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set Color Aspects parameter!");
    return FALSE;
  }

  return TRUE;
}

static void
gst_c2_venc_handle_region_encode (GstC2VEncoder * c2venc,
    GstVideoCodecFrame * frame)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;
  GstC2QuantRegions *roiparam = NULL;
  gint32 qpdelta = 0;
  guint32 idx = 0;

  // ROI mode is disabled, nothing to do except to return immediately.
  if (!c2venc->roi_quant_mode)
    return;

  roiparam = g_new0 (GstC2QuantRegions, 1);

  if (GST_CLOCK_TIME_IS_VALID (frame->pts))
    roiparam->timestamp = GST_TIME_AS_USECONDS (frame->pts);
  else if (GST_CLOCK_TIME_IS_VALID (frame->dts))
    roiparam->timestamp = GST_TIME_AS_USECONDS (frame->dts);

  while ((meta =
          gst_buffer_iterate_meta_filtered (frame->input_buffer, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {
    GstVideoRegionOfInterestMeta *roimeta = (GstVideoRegionOfInterestMeta *) meta;
    GstStructure *s = NULL;
    const gchar *label = NULL;

    if (roimeta->roi_type != g_quark_from_static_string ("ObjectDetection"))
      continue;

    if (GST_C2_MAX_RECT_ROI_NUM == roiparam->n_rects) {
      GST_WARNING_OBJECT (c2venc, "Received more than the allowed ROI metas, "
          "clipping to %d!", GST_C2_MAX_RECT_ROI_NUM);
      break;
    }

    s = gst_video_region_of_interest_meta_get_param (roimeta, "ObjectDetection");
    label = gst_structure_get_string (s, "label");

    GST_LOG_OBJECT (c2venc, "Input buffer ROI: label=%s id=%d (%d, %d) %dx%d",
        label, roimeta->id, roimeta->x, roimeta->y, roimeta->w, roimeta->h);

    roiparam->rects[roiparam->n_rects].x = roimeta->x;
    roiparam->rects[roiparam->n_rects].y = roimeta->y;
    roiparam->rects[roiparam->n_rects].w = roimeta->w;
    roiparam->rects[roiparam->n_rects].h = roimeta->h;

    if (gst_structure_has_field (c2venc->roi_quant_values, label)){
      if (gst_structure_get_int (c2venc->roi_quant_values, label, &qpdelta) &&
          (qpdelta > -31) && (qpdelta < 30)) {
        GST_LOG_OBJECT (c2venc, "Use encoding QP delta (%d) for '%s'",
            qpdelta, label);
      } else {
        qpdelta = DEFAULT_PROP_ROI_QP_DELTA;
        GST_WARNING_OBJECT (c2venc,"Invalid QP delta for '%s', use default (%d)",
            label, qpdelta);
      }
    } else {
      qpdelta = DEFAULT_PROP_ROI_QP_DELTA;
      GST_LOG_OBJECT (c2venc, "No QP delta specified for '%s', use default (%d)",
          label, qpdelta);
    }

    roiparam->rects[roiparam->n_rects].qp = qpdelta;
    roiparam->n_rects++;
  }

  for (idx = 0; idx < c2venc->roi_quant_boxes->len; idx++) {
    GstC2QuantRectangle *qbox =
        &(g_array_index (c2venc->roi_quant_boxes, GstC2QuantRectangle, idx));

    if (GST_C2_MAX_RECT_ROI_NUM == roiparam->n_rects) {
      GST_WARNING_OBJECT (c2venc, "Received more than the allowed ROI, "
          "clipping to %d!", GST_C2_MAX_RECT_ROI_NUM);
      break;
    }

    GST_LOG_OBJECT (c2venc, "Manual ROI: idx=%u (%d, %d) %dx%d with QP %d",
        idx, qbox->x, qbox->y, qbox->w, qbox->h, qbox->qp);

    roiparam->rects[roiparam->n_rects].x = qbox->x;
    roiparam->rects[roiparam->n_rects].y = qbox->y;
    roiparam->rects[roiparam->n_rects].w = qbox->w;
    roiparam->rects[roiparam->n_rects].h = qbox->h;
    roiparam->rects[roiparam->n_rects].qp = qbox->qp;
    roiparam->n_rects++;
  }

  // Attach ROI info to the codec frame to be consumed by the component.
  gst_video_codec_frame_set_user_data (frame, roiparam, g_free);

  return;
}

static void
gst_c2_venc_event_handler (guint type, gpointer payload, gpointer userdata)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (userdata);

  if (type == GST_C2_EVENT_EOS) {
    GST_DEBUG_OBJECT (c2venc, "Received engine EOS");
  } else if (type == GST_C2_EVENT_ERROR) {
    gint32 error = *((gint32*) userdata);
    GST_ERROR_OBJECT (c2venc, "Received engine ERROR: '%x'", error);
  } else if (type == GST_C2_EVENT_DROP) {
    guint64 index = *((guint64*) payload);
    GstVideoCodecFrame *frame = NULL;

    GST_DEBUG_OBJECT (c2venc, "Received engine drop frame: %" G_GUINT64_FORMAT,
        index);

    frame = gst_video_encoder_get_frame (GST_VIDEO_ENCODER (c2venc), index);
    if (frame == NULL) {
      GST_ERROR_OBJECT (c2venc, "Failed to get encoder frame with index %"
          G_GUINT64_FORMAT, index);
      return;
    }
    frame->output_buffer = NULL;
    // Calling finish_frame with frame->output_buffer == NULL will drop it.
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (c2venc), frame);
    gst_video_codec_frame_unref (frame);
  }
}

static void
gst_c2_venc_buffer_available (GstBuffer * buffer, gpointer userdata)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (userdata);
  GstVideoInfo *vinfo = &(c2venc->instate->info);
  GstVideoCodecFrame *frame = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 index = 0;

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_HEADER)) {
    c2venc->headers = g_list_append (c2venc->headers, buffer);
    return;
  } else if (c2venc->headers != NULL) {
    gst_video_encoder_set_headers (GST_VIDEO_ENCODER (c2venc), c2venc->headers);
    c2venc->headers = NULL;
  } else if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MARKER)) {
    gst_buffer_list_add (c2venc->incomplete_buffers, buffer);
    return;
  }

  // Get the frame index from the buffer offset field.
  index = GST_BUFFER_OFFSET (buffer);

  frame = gst_video_encoder_get_frame (GST_VIDEO_ENCODER (c2venc), index);
  if (frame == NULL) {
    GST_ERROR_OBJECT (c2venc, "Failed to get encoder frame with index %"
        G_GUINT64_FORMAT, index);
    gst_buffer_unref (buffer);
    return;
  }

  GST_LOG_OBJECT (c2venc, "Frame number : %d, pts: %" GST_TIME_FORMAT
      ", dts: %" GST_TIME_FORMAT, frame->system_frame_number,
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->dts));

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_SYNC))
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  else
    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);

  // Unset the custom SYNC flag if present.
  GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_SYNC);
  // Unset the custom UBWC flag if present.
  GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_UBWC);
  // Unset the custom HEIC flag if present.
  GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_HEIC);
  // Unset the custom GBM flag if present.
  GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_GBM);

  // Check for incomplete buffers and merge them into single buffer.
  if (gst_buffer_list_length (c2venc->incomplete_buffers) > 0) {
    GstMemory *memory = NULL;

    // Create a new buffer to hold the memory blocks for all incomplete buffers.
    frame->output_buffer = gst_buffer_new ();

    while (gst_buffer_list_length (c2venc->incomplete_buffers) > 0) {
      GstBuffer *buf = gst_buffer_list_get (c2venc->incomplete_buffers, 0);

      // Append the memory block from input buffer into the new buffer.
      memory = gst_buffer_get_memory (buf, 0);
      gst_buffer_append_memory (frame->output_buffer, memory);

      // Add parent meta, input buffer won't be released until new buffer is freed.
      gst_buffer_add_parent_buffer_meta (frame->output_buffer, buf);

      gst_buffer_list_remove (c2venc->incomplete_buffers, 0, 1);
    }

    memory = gst_buffer_get_memory (buffer, 0);
    gst_buffer_append_memory (frame->output_buffer, memory);

    gst_buffer_add_parent_buffer_meta (frame->output_buffer, buffer);
    gst_buffer_unref (buffer);
  } else {
    // No previous incomplete buffers, simply past current as the output buffer.
    frame->output_buffer = buffer;
  }

  gst_video_codec_frame_unref (frame);

  GST_TRACE_OBJECT (c2venc, "Encoded %" GST_PTR_FORMAT, buffer);
  ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (c2venc), frame);

  if (ret != GST_FLOW_OK) {
    GST_LOG_OBJECT (c2venc, "Failed to finish frame!");
    return;
  }
}

static GstC2Callbacks callbacks =
    { gst_c2_venc_event_handler, gst_c2_venc_buffer_available };

static gboolean
gst_c2_venc_start (GstVideoEncoder * encoder)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "Start engine");

  if ((c2venc->engine != NULL) && !gst_c2_engine_start (c2venc->engine)) {
    GST_ERROR_OBJECT (c2venc, "Failed to start engine!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (c2venc, "Engine started");
  return TRUE;
}

static gboolean
gst_c2_venc_stop (GstVideoEncoder * encoder)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "Stop engine");

  if ((c2venc->engine != NULL) && !gst_c2_engine_stop (c2venc->engine)) {
    GST_ERROR_OBJECT (c2venc, "Failed to stop engine");
    return FALSE;
  }

  g_list_free_full (c2venc->headers, (GDestroyNotify) gst_buffer_unref);
  c2venc->headers = NULL;

  GST_DEBUG_OBJECT (c2venc, "Engine stoped");
  return TRUE;
}

static gboolean
gst_c2_venc_flush (GstVideoEncoder * encoder)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);
  GST_DEBUG_OBJECT (c2venc, "Flush engine");

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if ((c2venc->engine != NULL) && !gst_c2_engine_flush (c2venc->engine)) {
    GST_ERROR_OBJECT (c2venc, "Failed to flush engine");
    return FALSE;
  }

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  g_list_free_full (c2venc->headers, (GDestroyNotify) gst_buffer_unref);
  c2venc->headers = NULL;

  GST_DEBUG_OBJECT (c2venc, "Engine flushed");
  return TRUE;
}

static GstCaps *
gst_c2_venc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);
  GstCaps *caps = NULL, *intermeadiary = NULL;
  GstStructure *structure = NULL;
  const GValue *framerate = NULL, *maxframerate = NULL;
  guint idx = 0, length = 0;

  GST_LOG_OBJECT (c2venc, "Filter caps %" GST_PTR_FORMAT, filter);

  // Create a local copy of the filter caps with removed fps fields.
  if (filter != NULL) {
    intermeadiary = gst_caps_copy (filter);
    length = gst_caps_get_size (intermeadiary);

    // Fetch the ignored framerate and max-framerate fields from the filter caps.
    structure = gst_caps_get_structure (filter, 0);

    if (gst_structure_has_field (structure, "framerate"))
      framerate = gst_structure_get_value (structure, "framerate");

    if (gst_structure_has_field (structure, "max-framerate"))
      maxframerate = gst_structure_get_value (structure, "max-framerate");
  }

  // Remove framerate and max-framerate fields as different fps are supported.
  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (intermeadiary, idx);
    gst_structure_remove_fields (structure, "framerate", "max-framerate", NULL);
  }

  GST_LOG_OBJECT (c2venc, "Intermeadiary caps %" GST_PTR_FORMAT, intermeadiary);
  caps = gst_video_encoder_proxy_getcaps (encoder, NULL, intermeadiary);

  if (intermeadiary != NULL)
    gst_caps_unref (intermeadiary);

  // Restore the framerate and max-framerate fields into the returned caps.
  for (idx = 0; idx < gst_caps_get_size (caps); idx++) {
    structure = gst_caps_get_structure (caps, idx);

    if (framerate != NULL)
      gst_structure_set_value (structure, "framerate", framerate);

    if (maxframerate != NULL)
      gst_structure_set_value (structure, "max-framerate", maxframerate);
  }

  GST_LOG_OBJECT (c2venc, "Returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_c2_venc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);
  GstVideoInfo *info = &state->info;
  GstVideoCodecState *outstate = NULL;
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  const gchar *name = NULL, *string = NULL;
  GstC2Profile profile = GST_C2_PROFILE_INVALID;
  GstC2Level level = GST_C2_LEVEL_INVALID;
  guint32 param = 0;
  gboolean success = FALSE;

  c2venc->isubwc = gst_caps_has_compression (state->caps, "ubwc");

  c2venc->isheif = gst_caps_has_subformat(state->caps, "heif");

  c2venc->isgbm = gst_caps_features_contains
      (gst_caps_get_features (state->caps, 0), GST_CAPS_FEATURE_MEMORY_GBM);

  GST_DEBUG_OBJECT (c2venc, "Setting new format %s%s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)),
      c2venc->isubwc ? " UBWC" : "");

  if ((c2venc->instate != NULL) &&
      !gst_video_info_is_equal (info, &(c2venc->instate->info))) {
    if (!gst_c2_venc_stop (encoder)) {
      GST_ERROR_OBJECT (c2venc, "Failed to stop encoder!");
      return FALSE;
    }
  }

  caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (c2venc));
  if ((caps == NULL) || gst_caps_is_empty (caps)) {
    GST_ERROR_OBJECT (c2venc, "Failed to get output caps!");
    return FALSE;
  }

  // Make sure that caps have only one entry.
  caps = gst_caps_truncate (caps);

  // Get the caps structue and set the component name.
  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "video/x-h264"))
    name = "c2.qti.avc.encoder";
  else if (gst_structure_has_name (structure, "video/x-h265"))
    name = "c2.qti.hevc.encoder";
  else if (gst_structure_has_name (structure, "image/heic"))
    name = "c2.qti.heic.encoder";

  if (name == NULL) {
    GST_ERROR_OBJECT (c2venc, "Unknown component!");
    gst_caps_unref (caps);
    return FALSE;
  }

  if ((c2venc->name != NULL) && !g_str_equal (c2venc->name, name)) {
    g_clear_pointer (&(c2venc->name), g_free);
    g_clear_pointer (&(c2venc->engine), gst_c2_engine_free);
  }

  if (c2venc->name == NULL)
    c2venc->name = g_strdup (name);

  if (c2venc->engine == NULL) {
    c2venc->engine = gst_c2_engine_new (c2venc->name, GST_C2_MODE_VIDEO_ENCODE,
        &callbacks, c2venc);
    g_return_val_if_fail (c2venc->engine != NULL, FALSE);
  }

  // Set profile and level both in caps and component.
  if ((string = gst_structure_get_string (structure, "profile")) != NULL) {
    if (gst_structure_has_name (structure, "video/x-h264"))
      profile = gst_c2_utils_h264_profile_from_string (string);
    else if (gst_structure_has_name (structure, "video/x-h265"))
      profile = gst_c2_utils_h265_profile_from_string (string);

    if (profile == GST_C2_PROFILE_INVALID) {
      GST_ERROR_OBJECT (c2venc, "Unsupported profile '%s'!", string);
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  if ((string = gst_structure_get_string (structure, "level")) != NULL) {
    if (gst_structure_has_name (structure, "video/x-h264")) {
      level = gst_c2_utils_h264_level_from_string (string);
    } else if (gst_structure_has_name (structure, "video/x-h265")) {
      const gchar *tier = gst_structure_get_string (structure, "tier");
      level = gst_c2_utils_h265_level_from_string (string, tier);
    }

    if (level == GST_C2_LEVEL_INVALID) {
      GST_ERROR_OBJECT (c2venc, "Unsupported level '%s'!", string);
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  success = gst_c2_engine_get_parameter (c2venc->engine,
      GST_C2_PARAM_PROFILE_LEVEL, &param);
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to get profile/level parameter!");
    gst_caps_unref (caps);
    return FALSE;
  }

  if (profile != GST_C2_PROFILE_INVALID)
    param = (param & 0xFFFF0000) + (profile & 0xFFFF);
  else
    profile = (param & 0xFFFF);

  if (level != GST_C2_LEVEL_INVALID)
    param = (param & 0xFFFF) + ((level & 0xFFFF) << 16);
  else
    level = (param >> 16) & 0xFFFF;

  success = gst_c2_engine_set_parameter (c2venc->engine,
      GST_C2_PARAM_PROFILE_LEVEL, GPOINTER_CAST (&param));
  if (!success) {
    GST_ERROR_OBJECT (c2venc, "Failed to set profile/level parameter!");
    gst_caps_unref (caps);
    return FALSE;
  }

  if (gst_structure_has_name (structure, "video/x-h264")) {
    if (profile != GST_C2_PROFILE_INVALID) {
      string = gst_c2_utils_h264_profile_to_string (profile);
      gst_structure_set (structure, "profile", G_TYPE_STRING, string, NULL);
    }

    if (level != GST_C2_LEVEL_INVALID) {
      string = gst_c2_utils_h264_level_to_string (level);
      gst_structure_set (structure, "level", G_TYPE_STRING, string, NULL);
    }
  } else if (gst_structure_has_name (structure, "video/x-h265")) {
    if (profile != GST_C2_PROFILE_INVALID) {
      string = gst_c2_utils_h265_profile_to_string (profile);
      gst_structure_set (structure, "profile", G_TYPE_STRING, string, NULL);
    }

    if (level != GST_C2_LEVEL_INVALID) {
      string = gst_c2_utils_h265_level_to_string (level);
      gst_structure_set (structure, "level", G_TYPE_STRING, string, NULL);
    }

    if (level >= GST_C2_LEVEL_HEVC_MAIN_1 && level <= GST_C2_LEVEL_HEVC_MAIN_6_2)
      gst_structure_set (structure, "tier", G_TYPE_STRING, "main", NULL);

    if (level >= GST_C2_LEVEL_HEVC_HIGH_4 && level <= GST_C2_LEVEL_HEVC_HIGH_6_2)
      gst_structure_set (structure, "tier", G_TYPE_STRING, "high", NULL);
  }

  GST_DEBUG_OBJECT (c2venc, "Setting output state caps: %" GST_PTR_FORMAT, caps);

  outstate = gst_video_encoder_set_output_state (encoder, caps, state);
  structure = gst_caps_get_structure (outstate->caps, 0);

  if (gst_structure_has_field (structure, "framerate")) {
    gint32 fps_n = 0, fps_d = 0;

    gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);

    if ((fps_n == 0) && (fps_d == 1))
      outstate->info.flags |= GST_VIDEO_FLAG_VARIABLE_FPS;
    else if ((fps_n != 0) && (fps_d != 0))
      outstate->info.flags &= ~(GST_VIDEO_FLAG_VARIABLE_FPS);
  }

  gst_video_codec_state_unref (outstate);

  if (!gst_video_encoder_negotiate (encoder)) {
    GST_ERROR_OBJECT (c2venc, "Failed to negotiate caps!");
    return FALSE;
  }

  outstate = gst_video_encoder_get_output_state (encoder);

  GST_DEBUG_OBJECT (c2venc, "Output state caps: %" GST_PTR_FORMAT, outstate->caps);

  // Variable input fps and fixed output fps, get the duration for timestamp adjustment.
  if ((state->info.flags & GST_VIDEO_FLAG_VARIABLE_FPS) &&
      !(outstate->info.flags & GST_VIDEO_FLAG_VARIABLE_FPS)) {
    c2venc->duration = gst_util_uint64_scale_int (GST_SECOND,
        GST_VIDEO_INFO_FPS_D (info), GST_VIDEO_INFO_FPS_N (info));
  }

  gst_video_codec_state_unref (outstate);

  if (!gst_c2_venc_setup_parameters (c2venc, state)) {
    GST_ERROR_OBJECT (c2venc, "Failed to setup parameters!");
    return FALSE;
  }

  if (!gst_c2_engine_start (c2venc->engine)) {
    GST_ERROR_OBJECT (c2venc, "Failed to start engine!");
    return FALSE;
  }

  if (c2venc->instate != NULL)
    gst_video_codec_state_unref (c2venc->instate);

  c2venc->instate = gst_video_codec_state_ref (state);

  return TRUE;
}

static GstFlowReturn
gst_c2_venc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);
  GstClockTimeDiff deadline;
  GstC2QueueItem item;

  // GAP input buffer, drop the frame.
  if ((gst_buffer_get_size (frame->input_buffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (frame->input_buffer, GST_BUFFER_FLAG_GAP))
    return gst_video_encoder_finish_frame (encoder, frame);

  if ((deadline = gst_video_encoder_get_max_encode_time (encoder, frame)) < 0) {
    GST_WARNING_OBJECT (c2venc, "Input frame is too late, dropping "
        "(deadline %" GST_TIME_FORMAT ")", GST_TIME_ARGS (-deadline));

    // Calling finish_frame with frame->output_buffer == NULL will drop it.
    return gst_video_encoder_finish_frame (encoder, frame);
  }

  if (c2venc->duration != GST_CLOCK_TIME_NONE) {

    GST_LOG_OBJECT (c2venc, "Adjust timestamp! Expected %" GST_TIME_FORMAT
        " but received frame %u with %" GST_TIME_FORMAT " !",
        GST_TIME_ARGS (c2venc->prevts + c2venc->duration),
        frame->system_frame_number, GST_TIME_ARGS (frame->pts));

    if (c2venc->prevts != GST_CLOCK_TIME_NONE) {
      frame->pts = c2venc->prevts + c2venc->duration;
      frame->abidata.ABI.ts = frame->pts;
    }

    c2venc->prevts = frame->pts;
  }

  GST_LOG_OBJECT (c2venc, "Frame number : %d, pts: %" GST_TIME_FORMAT
      ", dts: %" GST_TIME_FORMAT, frame->system_frame_number,
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->dts));

  gst_c2_venc_handle_region_encode (c2venc, frame);

  if (c2venc->isubwc)
    GST_BUFFER_FLAG_SET (frame->input_buffer, GST_VIDEO_BUFFER_FLAG_UBWC);

  if (c2venc->isheif)
    GST_BUFFER_FLAG_SET (frame->input_buffer, GST_VIDEO_BUFFER_FLAG_HEIC);

  if (c2venc->isgbm)
    GST_BUFFER_FLAG_SET (frame->input_buffer, GST_VIDEO_BUFFER_FLAG_GBM);

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  item.buffer = frame->input_buffer;
  item.index = frame->system_frame_number;
  item.userdata = gst_video_codec_frame_get_user_data (frame);

  if (!gst_c2_engine_queue (c2venc->engine, &item)) {
    GST_ERROR_OBJECT(c2venc, "Failed to send input frame to be emptied!");
    return GST_FLOW_ERROR;
  }

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  GST_TRACE_OBJECT (c2venc, "Queued %" GST_PTR_FORMAT, frame->input_buffer);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_c2_venc_finish (GstVideoEncoder * encoder)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (encoder);

  GST_DEBUG_OBJECT (c2venc, "Draining component");

  // This mutex was locked in the base class before call this function.
  // Needs to be unlocked when waiting for any pending buffers during drain.
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if (!gst_c2_engine_drain (c2venc->engine, TRUE)) {
    GST_ERROR_OBJECT (c2venc, "Failed to drain engine");
    return GST_FLOW_ERROR;
  }

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  GST_DEBUG_OBJECT (c2venc, "Drain completed");
  return GST_FLOW_OK;
}

static void
gst_c2_venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (c2venc);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (c2venc, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (c2venc);

  switch (prop_id) {
    case PROP_ROTATE:
      c2venc->rotate = g_value_get_enum (value);
      break;
    case PROP_RATE_CONTROL:
      c2venc->control_rate = g_value_get_enum (value);
      break;
    case PROP_TARGET_BITRATE: {
      c2venc->target_bitrate = g_value_get_uint (value);

      if ((c2venc->engine != NULL) &&
          (c2venc->target_bitrate != DEFAULT_PROP_TARGET_BITRATE)) {
        gboolean success = gst_c2_engine_set_parameter (c2venc->engine,
            GST_C2_PARAM_BITRATE, GPOINTER_CAST (&(c2venc->target_bitrate)));
        if (!success)
          GST_ERROR_OBJECT (c2venc, "Failed to set bitrate parameter!");
      }
      break;
    }
    case PROP_IDR_INTERVAL: {
      c2venc->idr_interval = g_value_get_uint (value);

      if ((c2venc->engine != NULL) && (c2venc->instate != NULL) &&
          (c2venc->idr_interval != DEFAULT_PROP_IDR_INTERVAL)) {
        GstVideoInfo *info = &(c2venc->instate->info);
        gdouble framerate = 0.0;
        gint64 key_frame_interval = 0;

        gst_util_fraction_to_double (GST_VIDEO_INFO_FPS_N (info),
            GST_VIDEO_INFO_FPS_D (info), &framerate);

        key_frame_interval = c2venc->idr_interval * (1000000 / framerate);

        gboolean success = gst_c2_engine_set_parameter (c2venc->engine,
            GST_C2_PARAM_KEY_FRAME_INTERVAL, GPOINTER_CAST (&(key_frame_interval)));
        if (!success)
          GST_ERROR_OBJECT (c2venc, "Failed to set key frame interval parameter!");
      }
      break;
    }
    case PROP_INTRA_REFRESH_MODE:
      c2venc->intra_refresh.mode = g_value_get_enum (value);
      break;
    case PROP_INTRA_REFRESH_PERIOD:
      c2venc->intra_refresh.period = g_value_get_uint (value);
      break;
    case PROP_B_FRAMES:
      c2venc->bframes = g_value_get_uint (value);
      break;
    case PROP_QUANT_I_FRAMES:
      c2venc->quant_init.i_frames = g_value_get_uint (value);
      c2venc->quant_init.i_frames_enable =
          (c2venc->quant_init.i_frames != DEFAULT_PROP_QUANT_I_FRAMES) ?
              TRUE : FALSE;
      break;
    case PROP_QUANT_P_FRAMES:
      c2venc->quant_init.p_frames = g_value_get_uint (value);
      c2venc->quant_init.p_frames_enable =
          (c2venc->quant_init.i_frames != DEFAULT_PROP_QUANT_P_FRAMES) ?
              TRUE : FALSE;
      break;
    case PROP_QUANT_B_FRAMES:
      c2venc->quant_init.b_frames = g_value_get_uint (value);
      c2venc->quant_init.b_frames_enable =
          (c2venc->quant_init.i_frames != DEFAULT_PROP_QUANT_B_FRAMES) ?
              TRUE : FALSE;
      break;
    case PROP_MIN_QP_I_FRAMES:
      c2venc->quant_ranges.min_i_qp = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_I_FRAMES:
      c2venc->quant_ranges.max_i_qp = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_B_FRAMES:
      c2venc->quant_ranges.min_b_qp = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_B_FRAMES:
      c2venc->quant_ranges.max_b_qp = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_P_FRAMES:
      c2venc->quant_ranges.min_p_qp = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_P_FRAMES:
      c2venc->quant_ranges.max_p_qp = g_value_get_uint (value);
      break;
    case PROP_ROI_QUANT_MODE:
      c2venc->roi_quant_mode = g_value_get_boolean (value);
      break;
    case PROP_ROI_QUANT_META_VALUE:
      if (c2venc->roi_quant_values)
        gst_structure_free (c2venc->roi_quant_values);

      c2venc->roi_quant_values = GST_STRUCTURE_CAST (g_value_dup_boxed (value));
      break;
    case PROP_ROI_QUANT_BOXES:
    {
      guint idx = 0;

      // Remove all old values.
      g_array_set_size (c2venc->roi_quant_boxes, 0);

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        const GValue *v = gst_value_array_get_value (value, idx);
        GstC2QuantRectangle qbox = { 0, 0, 0, 0, 0 };

        if (gst_value_array_get_size (v) != 5) {
          GST_WARNING_OBJECT (c2venc, "Invalid ROI box at index '%u', skip", idx);
          continue;
        }

        qbox.x = g_value_get_int (gst_value_array_get_value (v, 0));
        qbox.y = g_value_get_int (gst_value_array_get_value (v, 1));
        qbox.w = g_value_get_int (gst_value_array_get_value (v, 2));
        qbox.h = g_value_get_int (gst_value_array_get_value (v, 3));
        qbox.qp = g_value_get_int (gst_value_array_get_value (v, 4));

        if (qbox.w == 0 || qbox.h == 0) {
          GST_WARNING_OBJECT (c2venc, "Invalid dimensions for ROI box at "
              "index %u, skip", idx);
          continue;
        } else if ((qbox.qp < -31) || (qbox.qp > 30)) {
          GST_WARNING_OBJECT (c2venc, "Invalid quant value for ROI box at "
              "index %u, skip", idx);
          continue;
        }

        g_array_append_val (c2venc->roi_quant_boxes, qbox);
      }
      break;
    }
    case PROP_SLICE_SIZE:
      c2venc->slice_size = g_value_get_uint (value);
      break;
    case PROP_SLICE_MODE:
      c2venc->slice_mode = g_value_get_enum (value);
      break;
    case PROP_ENTROPY_MODE:
      c2venc->entropy_mode = g_value_get_enum (value);
      break;
    case PROP_LOOP_FILTER_MODE:
      c2venc->loop_filter_mode = g_value_get_enum (value);
      break;
    case PROP_NUM_LTR_FRAMES:
      c2venc->num_ltr_frames = g_value_get_uint (value);
      break;
    case PROP_PRIORITY:
      c2venc->priority = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (c2venc);
}

static void
gst_c2_venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstC2VEncoder *c2venc = GST_C2_VENC (object);

  GST_OBJECT_LOCK (c2venc);

  switch (prop_id) {
    case PROP_ROTATE:
      g_value_set_enum (value, c2venc->rotate);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, c2venc->control_rate);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, c2venc->target_bitrate);
      break;
    case PROP_IDR_INTERVAL:
      g_value_set_uint (value, c2venc->idr_interval);
      break;
    case PROP_INTRA_REFRESH_MODE:
      g_value_set_enum (value, c2venc->intra_refresh.mode);
      break;
    case PROP_INTRA_REFRESH_PERIOD:
      g_value_set_uint (value, c2venc->intra_refresh.period);
      break;
    case PROP_B_FRAMES:
      g_value_set_uint (value, c2venc->bframes);
      break;
    case PROP_QUANT_I_FRAMES:
      g_value_set_uint (value, c2venc->quant_init.i_frames);
      break;
    case PROP_QUANT_P_FRAMES:
      g_value_set_uint (value, c2venc->quant_init.p_frames);
      break;
    case PROP_QUANT_B_FRAMES:
      g_value_set_uint (value, c2venc->quant_init.b_frames);
      break;
    case PROP_MIN_QP_I_FRAMES:
      g_value_set_uint (value, c2venc->quant_ranges.min_i_qp);
      break;
    case PROP_MAX_QP_I_FRAMES:
      g_value_set_uint (value, c2venc->quant_ranges.max_i_qp);
      break;
    case PROP_MIN_QP_P_FRAMES:
      g_value_set_uint (value, c2venc->quant_ranges.min_p_qp);
      break;
    case PROP_MAX_QP_P_FRAMES:
      g_value_set_uint (value, c2venc->quant_ranges.max_p_qp);
      break;
    case PROP_MIN_QP_B_FRAMES:
      g_value_set_uint (value, c2venc->quant_ranges.min_b_qp);
      break;
    case PROP_MAX_QP_B_FRAMES:
      g_value_set_uint (value, c2venc->quant_ranges.max_b_qp);
      break;
    case PROP_ROI_QUANT_MODE:
      g_value_set_boolean (value, c2venc->roi_quant_mode);
      break;
    case PROP_ROI_QUANT_META_VALUE:
      if (c2venc->roi_quant_values)
        g_value_set_boxed (value, c2venc->roi_quant_values);
      break;
    case PROP_ROI_QUANT_BOXES:
    {
      GstC2QuantRectangle *qbox = NULL;
      guint idx = 0;

      for (idx = 0; idx < c2venc->roi_quant_boxes->len; idx++) {
        GValue element = G_VALUE_INIT, val = G_VALUE_INIT;

        g_value_init (&element, GST_TYPE_ARRAY);
        g_value_init (&val, G_TYPE_INT);

        qbox =
            &(g_array_index (c2venc->roi_quant_boxes, GstC2QuantRectangle, idx));

        g_value_set_int (&val, qbox->x);
        gst_value_array_append_value (&element, &val);

        g_value_set_int (&val, qbox->y);
        gst_value_array_append_value (&element, &val);

        g_value_set_int (&val, qbox->w);
        gst_value_array_append_value (&element, &val);

        g_value_set_int (&val, qbox->h);
        gst_value_array_append_value (&element, &val);

        g_value_set_int (&val, qbox->qp);
        gst_value_array_append_value (&element, &val);

        /* Append the rectangle to the output GST array */
        gst_value_array_append_value (value, &element);

        g_value_unset (&val);
        g_value_unset (&element);
      }
      break;
    }
    case PROP_SLICE_SIZE:
      g_value_set_uint (value, c2venc->slice_size);
      break;
    case PROP_SLICE_MODE:
      g_value_set_enum (value, c2venc->slice_mode);
      break;
    case PROP_ENTROPY_MODE:
      g_value_set_enum (value, c2venc->entropy_mode);
      break;
    case PROP_LOOP_FILTER_MODE:
      g_value_set_enum (value, c2venc->loop_filter_mode);
      break;
    case PROP_NUM_LTR_FRAMES:
      g_value_set_uint (value, c2venc->num_ltr_frames);
      break;
    case PROP_PRIORITY:
      g_value_set_int (value, c2venc->priority);
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
  GstC2VEncoder *c2venc = GST_C2_VENC (object);

  g_array_free (c2venc->roi_quant_boxes, TRUE);
  gst_structure_free (c2venc->roi_quant_values);

  if (c2venc->instate)
    gst_video_codec_state_unref (c2venc->instate);

  if (c2venc->engine != NULL)
    gst_c2_engine_free (c2venc->engine);

  g_free (c2venc->name);

  gst_buffer_list_unref (c2venc->incomplete_buffers);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (c2venc));
}

static void
gst_c2_venc_class_init (GstC2VEncoderClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_c2_venc_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_c2_venc_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_c2_venc_finalize);

  g_object_class_install_property (gobject, PROP_ROTATE,
      g_param_spec_enum ("rotate", "Rotate",
          "Rotate video image", GST_TYPE_C2_VIDEO_ROTATION, DEFAULT_PROP_ROTATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_RATE_CONTROL,
      g_param_spec_enum ("control-rate", "Rate Control",
          "Bitrate control method",
          GST_TYPE_C2_RATE_CONTROL, DEFAULT_PROP_RATE_CONTROL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target bitrate",
          "Target bitrate in bits per second (0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_TARGET_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_IDR_INTERVAL,
      g_param_spec_uint ("idr-interval", "IDR Interval",
          "Periodicity of IDR frames. When set to 0 all frames will be I frames "
          "(0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_IDR_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_INTRA_REFRESH_MODE,
      g_param_spec_enum ("intra-refresh-mode", "Intra refresh mode",
          "Intra refresh mode (0xffffffff=component default)."
          "Allow IR only for CBR(_CFR/VFR) RC modes",
          GST_TYPE_C2_INTRA_REFRESH_MODE, DEFAULT_PROP_INTRA_REFRESH_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_INTRA_REFRESH_PERIOD,
      g_param_spec_uint ("intra-refresh-period", "Intra Refresh Period",
          "The period of intra refresh. Only support random mode.",
          0, G_MAXUINT, DEFAULT_PROP_INTRA_REFRESH_PERIOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_B_FRAMES,
      g_param_spec_uint ("b-frames", "B Frames",
          "Number of B-frames between two consecutive I-frames "
          "(0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_QUANT_I_FRAMES,
      g_param_spec_uint ("quant-i-frames", "I-Frame Quantization",
          "Quantization parameter for I-frames (0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_QUANT_I_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_QUANT_P_FRAMES,
      g_param_spec_uint ("quant-p-frames", "P-Frame Quantization",
          "Quantization parameter for P-frames (0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_QUANT_P_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_QUANT_B_FRAMES,
      g_param_spec_uint ("quant-b-frames", "B-Frame Quantization",
          "Quantization parameter for B-frames (0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_QUANT_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MIN_QP_I_FRAMES,
      g_param_spec_uint ("min-quant-i-frames", "Min quant I frames",
          "Minimum quantization parameter allowed for I-frames",
          0, G_MAXUINT, DEFAULT_PROP_MIN_QP_I_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MAX_QP_I_FRAMES,
      g_param_spec_uint ("max-quant-i-frames", "Max quant I frames",
          "Maximum quantization parameter allowed for I-frames",
          0, G_MAXUINT, DEFAULT_PROP_MAX_QP_I_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MIN_QP_P_FRAMES,
      g_param_spec_uint ("min-quant-p-frames", "Min quant P frames",
          "Minimum quantization parameter allowed for P-frames",
          0, G_MAXUINT, DEFAULT_PROP_MIN_QP_P_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MAX_QP_P_FRAMES,
      g_param_spec_uint ("max-quant-p-frames", "Max quant P frames",
          "Maximum quantization parameter allowed for P-frames",
          0, G_MAXUINT, DEFAULT_PROP_MAX_QP_P_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MIN_QP_B_FRAMES,
      g_param_spec_uint ("min-quant-b-frames", "Min quant B frames",
          "Minimum quantization parameter allowed for B-frames",
          0, G_MAXUINT, DEFAULT_PROP_MIN_QP_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_MAX_QP_B_FRAMES,
      g_param_spec_uint ("max-quant-b-frames", "Max quant B frames",
          "Maximum quantization parameter allowed for B-frames",
          0, G_MAXUINT, DEFAULT_PROP_MAX_QP_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_ROI_QUANT_MODE,
      g_param_spec_boolean ("roi-quant-mode", "ROI Quantization Mode",
          "Enable/Disable Adjustment of the quantization parameter according "
          "to ROIs set manually via the 'roi-quant-boxes' property and/or "
          "arriving as GstVideoRegionOfInterestMeta attached to the buffer",
          DEFAULT_PROP_ROI_QUANT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_ROI_QUANT_META_VALUE,
      g_param_spec_boxed ("roi-quant-meta-value", "ROI Meta Quantization Value",
          "Set specific QP value, different then the default value of (-15), "
          "for a GstVideoRegionOfInterestMeta type (e.g. 'roi-meta-qp,"
          "person=-20,cup=10,dog=-5;'). The QP values must be in the range of "
          "-31 (best quality) to 30 (worst quality)", GST_TYPE_STRUCTURE,
          G_PARAM_READWRITE| G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_ROI_QUANT_BOXES,
      gst_param_spec_array ("roi-quant-boxes", "ROI Quantization Boxes",
          "Manually set ROI boxes (e.g. '<<X, Y, W, H, QP>, <X, Y, W, H, QP>>'). "
          "The QP values must be in the range of -31 (best quality) to "
          "30 (worst quality)",
          gst_param_spec_array ("rectangle", "Rectangle", "Rectangle",
              g_param_spec_int ("value", "Rectangle Value",
                  "One of X, Y, WIDTH, HEIGHT or QP", G_MININT, G_MAXINT, 0,
                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "slice mode",
          "Slice mode (0xffffffff=component default)",
          GST_TYPE_C2_SLICE_MODE, DEFAULT_PROP_SLICE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLICE_SIZE,
      g_param_spec_uint ("slice-size", "Slice size",
          "Slice size, just set when slice mode setting to MB or Bytes",
          0, G_MAXUINT, DEFAULT_PROP_SLICE_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_ENTROPY_MODE,
      g_param_spec_enum ("entropy-mode", "Entropy Mode",
          "Entropy mode (0xffffffff=component default)",
          GST_TYPE_C2_ENTROPY_MODE, DEFAULT_PROP_ENTROPY_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_LOOP_FILTER_MODE,
      g_param_spec_enum ("loop-filter-mode", "Loop Filter mode",
          "Deblocking filter mode (0xffffffff=component default)",
          GST_TYPE_C2_LOOP_FILTER_MODE, DEFAULT_PROP_LOOP_FILTER_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_NUM_LTR_FRAMES,
      g_param_spec_uint ("num-ltr-frames", "LTR Frames Count",
          "Number of Long Term Reference Frames (0xffffffff=component default)",
          0, G_MAXUINT, DEFAULT_PROP_NUM_LTR_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject, PROP_PRIORITY,
      g_param_spec_int ("priority", "Priority",
          "The proirity of current video instance among concurrent cases,"
          "(0xffffffff=component default)",
          G_MININT32, G_MAXINT, DEFAULT_PROP_PRIORITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_signal_new_class_handler ("trigger-iframe", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (gst_c2_venc_trigger_iframe),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_BOOLEAN, 0);

  g_signal_new_class_handler ("ltr-mark", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (gst_c2_venc_ltr_mark),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 1, G_TYPE_UINT);

  // TODO: Temporary solution to flush all enqued buffers in the encoder
  // until proper solution is implemented using flush start/stop
  g_signal_new_class_handler ("flush-buffers", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (gst_c2_venc_flush),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 0);

  gst_element_class_set_static_metadata (element,
      "Codec2 H.264/H.265/HEIC Video Encoder", "Codec/Encoder/Video",
      "Encode H.264/H.265/HEIC video streams", "QTI");

  gst_element_class_add_static_pad_template (element,
      &gst_c2_venc_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_c2_venc_src_pad_template);

  venc_class->start = GST_DEBUG_FUNCPTR (gst_c2_venc_start);
  venc_class->stop = GST_DEBUG_FUNCPTR (gst_c2_venc_stop);
  venc_class->flush = GST_DEBUG_FUNCPTR (gst_c2_venc_flush);
  venc_class->getcaps = GST_DEBUG_FUNCPTR (gst_c2_venc_getcaps);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_c2_venc_set_format);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_c2_venc_handle_frame);
  venc_class->finish = GST_DEBUG_FUNCPTR (gst_c2_venc_finish);
}

static void
gst_c2_venc_init (GstC2VEncoder * c2venc)
{
  c2venc->name = NULL;
  c2venc->engine = NULL;

  c2venc->instate = NULL;
  c2venc->isubwc = FALSE;
  c2venc->isheif = FALSE;
  c2venc->isgbm = FALSE;
  c2venc->headers = NULL;

  c2venc->incomplete_buffers = gst_buffer_list_new ();

  c2venc->prevts = GST_CLOCK_TIME_NONE;
  c2venc->duration = GST_CLOCK_TIME_NONE;

  c2venc->rotate = DEFAULT_PROP_ROTATE;
  c2venc->control_rate = DEFAULT_PROP_RATE_CONTROL;
  c2venc->target_bitrate = DEFAULT_PROP_TARGET_BITRATE;
  c2venc->idr_interval = DEFAULT_PROP_IDR_INTERVAL;

  c2venc->intra_refresh.mode = DEFAULT_PROP_INTRA_REFRESH_MODE;
  c2venc->intra_refresh.period = DEFAULT_PROP_INTRA_REFRESH_PERIOD;
  c2venc->bframes = DEFAULT_PROP_B_FRAMES;

  c2venc->slice_mode = DEFAULT_PROP_SLICE_MODE;
  c2venc->slice_size = DEFAULT_PROP_SLICE_SIZE;

  c2venc->quant_init.i_frames = DEFAULT_PROP_QUANT_I_FRAMES;
  c2venc->quant_init.i_frames_enable = FALSE;
  c2venc->quant_init.p_frames = DEFAULT_PROP_QUANT_P_FRAMES;
  c2venc->quant_init.p_frames_enable = FALSE;
  c2venc->quant_init.b_frames = DEFAULT_PROP_QUANT_B_FRAMES;
  c2venc->quant_init.b_frames_enable = FALSE;

  c2venc->quant_ranges.min_i_qp = DEFAULT_PROP_MIN_QP_I_FRAMES;
  c2venc->quant_ranges.max_i_qp = DEFAULT_PROP_MAX_QP_I_FRAMES;
  c2venc->quant_ranges.min_p_qp = DEFAULT_PROP_MIN_QP_P_FRAMES;
  c2venc->quant_ranges.max_p_qp = DEFAULT_PROP_MAX_QP_P_FRAMES;
  c2venc->quant_ranges.min_b_qp = DEFAULT_PROP_MIN_QP_B_FRAMES;
  c2venc->quant_ranges.max_b_qp = DEFAULT_PROP_MAX_QP_B_FRAMES;

  c2venc->roi_quant_mode = DEFAULT_PROP_ROI_QUANT_MODE;
  c2venc->roi_quant_values = gst_structure_new_empty ("roi-meta-qp");
  c2venc->roi_quant_boxes =
      g_array_new (FALSE, FALSE, sizeof (GstC2QuantRectangle));

  c2venc->entropy_mode = DEFAULT_PROP_ENTROPY_MODE;
  c2venc->loop_filter_mode = DEFAULT_PROP_LOOP_FILTER_MODE;
  c2venc->num_ltr_frames = DEFAULT_PROP_NUM_LTR_FRAMES;
  c2venc->priority = DEFAULT_PROP_PRIORITY;

  GST_DEBUG_CATEGORY_INIT (c2_venc_debug, "qtic2venc", 0,
      "QTI c2venc encoder");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtic2venc", GST_RANK_PRIMARY,
      GST_TYPE_C2_VENC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtic2venc,
    "Codec2 Video Encoder",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
