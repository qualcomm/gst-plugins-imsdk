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

#include "c2-config.h"

#include <C2Config.h>
#include "QC2V4L2Config.h"
#include <map>

#define GST_CAT_DEFAULT gst_c2_venc_context_debug_category()
static GstDebugCategory *
gst_c2_venc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtic2venc", 0,
        "C2 encoder config");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

struct char_cmp {
  bool operator () (const char * a, const char * b) const
  {
    return strcmp(a, b) < 0;
  }
};

typedef std::unique_ptr<C2Param> (*configFunction)(gpointer data);

// Give a comparison functor to the map to avoid comparing the pointer
typedef std::map<const char*, configFunction, char_cmp> configFunctionMap;

std::unique_ptr<C2Param> setVideoPixelformat (gpointer param);
std::unique_ptr<C2Param> setVideoResolution (gpointer param);
std::unique_ptr<C2Param> setVideoBitrate (gpointer param);
std::unique_ptr<C2Param> setVideoFramerate (gpointer param);
std::unique_ptr<C2Param> setRotation (gpointer param);
std::unique_ptr<C2Param> setMirrorType (gpointer param);
std::unique_ptr<C2Param> setRateControl (gpointer param);
std::unique_ptr<C2Param> setSyncFrameInterval (gpointer param);
std::unique_ptr<C2Param> setOutputPictureOrderMode (gpointer param);
std::unique_ptr<C2Param> setROIEncoding (gpointer param);
std::unique_ptr<C2Param> setDecLowLatency (gpointer param);
std::unique_ptr<C2Param> setDownscale (gpointer param);
std::unique_ptr<C2Param> setEncColorSpaceConv (gpointer param);
std::unique_ptr<C2Param> setColorAspectsInfo (gpointer param);
std::unique_ptr<C2Param> setIntraRefresh (gpointer param);
std::unique_ptr<C2Param> setSliceMode (gpointer param);
std::unique_ptr<C2Param> setBlurMode (gpointer param);
std::unique_ptr<C2Param> setBlurResolution (gpointer param);
std::unique_ptr<C2Param> setQPRanges (gpointer param);

// Function map for parameter configuration
static configFunctionMap sConfigFunctionMap = {
  { CONFIG_FUNCTION_KEY_PIXELFORMAT, setVideoPixelformat },
  { CONFIG_FUNCTION_KEY_RESOLUTION, setVideoResolution },
  { CONFIG_FUNCTION_KEY_BITRATE, setVideoBitrate },
  { CONFIG_FUNCTION_KEY_FRAMERATE, setVideoFramerate },
  { CONFIG_FUNCTION_KEY_ROTATION, setRotation },
  { CONFIG_FUNCTION_KEY_MIRROR, setMirrorType },
  { CONFIG_FUNCTION_KEY_RATECONTROL, setRateControl },
  { CONFIG_FUNCTION_KEY_SYNC_FRAME_INT, setSyncFrameInterval },
  { CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE, setOutputPictureOrderMode },
  { CONFIG_FUNCTION_KEY_ROI_ENCODING, setROIEncoding },
  { CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY, setDecLowLatency },
  { CONFIG_FUNCTION_KEY_DOWNSCALE, setDownscale },
  { CONFIG_FUNCTION_KEY_ENC_CSC, setEncColorSpaceConv },
  { CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO, setColorAspectsInfo },
  { CONFIG_FUNCTION_KEY_INTRAREFRESH, setIntraRefresh },
  { CONFIG_FUNCTION_KEY_SLICE_MODE, setSliceMode },
  { CONFIG_FUNCTION_KEY_BLUR_MODE, setBlurMode },
  { CONFIG_FUNCTION_KEY_BLUR_RESOLUTION, setBlurResolution },
  { CONFIG_FUNCTION_KEY_QP_RANGES, setQPRanges },
};

uint32_t
toC2PixelFormat (pixel_format_t pixel)
{
  uint32_t result = 0;

  switch (pixel) {
    case PIXEL_FORMAT_NV12_LINEAR: {
      result = C2_PIXEL_FORMAT_VENUS_NV12;
      break;
    }
    case PIXEL_FORMAT_NV12_UBWC: {
      result = C2_PIXEL_FORMAT_VENUS_NV12_UBWC;
      break;
    }
    case PIXEL_FORMAT_RGBA_8888: {
      result = C2_PIXEL_FORMAT_RGBA8888;
      break;
    }
    case PIXEL_FORMAT_YV12: {
      result = C2_PIXEL_FORMAT_YV12;
      break;
    }
    case PIXEL_FORMAT_P010: {
      result = C2_PIXEL_FORMAT_VENUS_P010;
      break;
    }
    case PIXEL_FORMAT_TP10_UBWC: {
      result = C2_PIXEL_FORMAT_VENUS_TP10;
      break;
    }
    default: {
      GST_ERROR ("unsupported pixel format!");
      break;
    }
  }

  return result;
}

uint32_t
toC2RateControlMode (rc_mode_t mode)
{
  uint32_t rcMode = 0x7F000000; //RC_MODE_EXT_DISABLE

  switch (mode) {
    case rc_mode_t::RC_MODE_OFF: {
      rcMode = 0x7F000000; //RC_MODE_EXT_DISABLE
      break;
    }
    case rc_mode_t::RC_MODE_CONST: {
      rcMode = C2Config::BITRATE_CONST;
      break;
    }
    case rc_mode_t::RC_MODE_CBR_VFR: {
      rcMode = C2Config::BITRATE_CONST_SKIP_ALLOWED;
      break;
    }
    case rc_mode_t::RC_MODE_VBR_CFR: {
      rcMode = C2Config::BITRATE_VARIABLE;
      break;
    }
    case rc_mode_t::RC_MODE_VBR_VFR: {
      rcMode = C2Config::BITRATE_VARIABLE_SKIP_ALLOWED;
      break;
    }
    case rc_mode_t::RC_MODE_CQ: {
      rcMode = C2Config::BITRATE_IGNORE;
      break;
    }
    default: {
      GST_ERROR ("Invalid RC Mode: %d", mode);
    }
  }

  return rcMode;
}

C2Color::primaries_t
toC2Primaries (color_primaries_t pixel)
{
  C2Color::primaries_t ret = C2Color::PRIMARIES_UNSPECIFIED;
  switch (pixel) {
  case COLOR_PRIMARIES_BT709:
    ret = C2Color::PRIMARIES_BT709;
    break;
  case COLOR_PRIMARIES_BT470_M:
    ret = C2Color::PRIMARIES_BT470_M;
    break;
  case COLOR_PRIMARIES_BT601_625:
    ret = C2Color::PRIMARIES_BT601_625;
    break;
  case COLOR_PRIMARIES_BT601_525:
    ret = C2Color::PRIMARIES_BT601_525;
    break;
  case COLOR_PRIMARIES_GENERIC_FILM:
    ret = C2Color::PRIMARIES_GENERIC_FILM;
    break;
  case COLOR_PRIMARIES_BT2020:
    ret = C2Color::PRIMARIES_BT2020;
    break;
  case COLOR_PRIMARIES_RP431:
    ret = C2Color::PRIMARIES_RP431;
    break;
  case COLOR_PRIMARIES_EG432:
    ret = C2Color::PRIMARIES_EG432;
    break;
  case COLOR_PRIMARIES_EBU3213:
    ret = C2Color::PRIMARIES_EBU3213;
    break;
  default:
    ret = C2Color::PRIMARIES_UNSPECIFIED;
    break;
  }

  return ret;
}

C2Color::transfer_t
toC2TransferChar (color_transfer_t color_transfer)
{
  C2Color::transfer_t ret = C2Color::TRANSFER_UNSPECIFIED;
  switch (color_transfer) {
  case COLOR_TRANSFER_LINEAR:
    ret = C2Color::TRANSFER_LINEAR;
    break;
  case COLOR_TRANSFER_SRGB:
    ret = C2Color::TRANSFER_SRGB;
    break;
  case COLOR_TRANSFER_170M:
    ret = C2Color::TRANSFER_170M;
    break;
  case COLOR_TRANSFER_GAMMA22:
    ret = C2Color::TRANSFER_GAMMA22;
    break;
  case COLOR_TRANSFER_GAMMA28:
    ret = C2Color::TRANSFER_GAMMA28;
    break;
  case COLOR_TRANSFER_ST2084:
    ret = C2Color::TRANSFER_ST2084;
    break;
  case COLOR_TRANSFER_HLG:
    ret = C2Color::TRANSFER_HLG;
    break;
  case COLOR_TRANSFER_240M:
    ret = C2Color::TRANSFER_240M;
    break;
  case COLOR_TRANSFER_XVYCC:
    ret = C2Color::TRANSFER_XVYCC;
    break;
  case COLOR_TRANSFER_BT1361:
    ret = C2Color::TRANSFER_BT1361;
    break;
  case COLOR_TRANSFER_ST428:
    ret = C2Color::TRANSFER_ST428;
    break;
  default:
    ret = C2Color::TRANSFER_UNSPECIFIED;
    break;
  }

  return ret;
}
C2Color::matrix_t
toC2Matrix (color_matrix_t matrix)
{
  C2Color::matrix_t ret = C2Color::MATRIX_UNSPECIFIED;
  switch (matrix) {
  case COLOR_MATRIX_BT709:
    ret = C2Color::MATRIX_BT709;
    break;
  case COLOR_MATRIX_FCC47_73_682:
    ret = C2Color::MATRIX_FCC47_73_682;
    break;
  case COLOR_MATRIX_BT601:
    ret = C2Color::MATRIX_BT601;
    break;
  case COLOR_MATRIX_240M:
    ret = C2Color::MATRIX_240M;
    break;
  case COLOR_MATRIX_BT2020:
    ret = C2Color::MATRIX_BT2020;
    break;
  case COLOR_MATRIX_BT2020_CONSTANT:
    ret = C2Color::MATRIX_BT2020_CONSTANT;
    break;
  default:
    ret = C2Color::MATRIX_UNSPECIFIED;
    break;
  }
  return ret;
}
C2Color::range_t
toC2FullRange (color_range_t color_range)
{
  C2Color::range_t ret = C2Color::RANGE_UNSPECIFIED;
  switch (color_range) {
  case COLOR_RANGE_FULL:
    ret = C2Color::RANGE_FULL;
    break;
  case COLOR_RANGE_LIMITED:
    ret = C2Color::RANGE_LIMITED;
    break;
  default:
    ret = C2Color::RANGE_UNSPECIFIED;
    break;
  }
  return ret;
}

std::unique_ptr<C2Param>
setVideoPixelformat (gpointer param)
{
  if (param == NULL) {
    return nullptr;
  }

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    C2StreamPixelFormatInfo::input inputColorFmt;

    inputColorFmt.value = toC2PixelFormat (config->pixel_fmt);

    return C2Param::Copy (inputColorFmt);
  } else {
    C2StreamPixelFormatInfo::output outputColorFmt;
    outputColorFmt.value = toC2PixelFormat (config->pixel_fmt);
    return C2Param::Copy (outputColorFmt);
  }
}

std::unique_ptr<C2Param>
setVideoResolution (gpointer param)
{
  if (param == NULL) {
    return nullptr;
  }

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    C2StreamPictureSizeInfo::input size;

    size.width = config->resolution.width;
    size.height = config->resolution.height;

    return C2Param::Copy (size);
  } else {
    C2StreamPictureSizeInfo::output size;

    size.width = config->resolution.width;
    size.height = config->resolution.height;

    return C2Param::Copy (size);
  }
}

std::unique_ptr<C2Param>
setVideoBitrate (gpointer param)
{
  if (param == NULL) {
    return nullptr;
  }

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    GST_WARNING("setVideoBitrate input not implemented");

  } else {
    C2StreamBitrateInfo::output bitrate;
    bitrate.value = config->val.u32;
    return C2Param::Copy (bitrate);
  }

  return nullptr;
}

std::unique_ptr<C2Param>
setVideoFramerate (gpointer param)
{
  if (param == NULL) {
    return nullptr;
  }

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    GST_WARNING ("setVideoFramerate input not implemented");

  } else {
    C2StreamFrameRateInfo::output framerate;
    framerate.value = config->val.fl;
    return C2Param::Copy (framerate);
  }

  return nullptr;
}

std::unique_ptr<C2Param>
setMirrorType (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    qc2::C2VideoMirrorTuning::input mirror;
    mirror.mirrorType = qc2::QCMirrorType (config->mirror_type);
    return C2Param::Copy (mirror);
  } else {
    GST_WARNING ("setMirrorType output not implemented");
  }

  return nullptr;
}

std::unique_ptr<C2Param>
setRotation (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*)param;

  if (config->is_input) {
    qc2::C2VideoRotation::input rotation;
    rotation.angle = config->val.u32;
    return C2Param::Copy (rotation);
  } else {
    GST_WARNING ("setRotation output not implemented");
  }

  return nullptr;
}

std::unique_ptr<C2Param>
setRateControl (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  C2StreamBitrateModeTuning::output bitrateMode;
  bitrateMode.value =
      (C2Config::bitrate_mode_t) toC2RateControlMode (config->rc_mode);
  return C2Param::Copy (bitrateMode);
}

std::unique_ptr<C2Param>
setSyncFrameInterval (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  C2StreamSyncFrameIntervalTuning::output syncFrameInterval;
  syncFrameInterval.value = config->val.i64;
  return C2Param::Copy (syncFrameInterval);
}

std::unique_ptr<C2Param>
setOutputPictureOrderMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  qc2::C2VideoPictureOrder::output outputPictureOrderMode;
  if (config->output_picture_order_mode == OUTPUT_PICTURE_ORDER_DECODER)
    outputPictureOrderMode.enable = C2_TRUE;
  return C2Param::Copy (outputPictureOrderMode);
}

std::unique_ptr<C2Param>
setROIEncoding (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  GST_INFO ("Set ROI encoding - %s %s", config->roi.rectPayload,
      config->roi.rectPayloadExt);

  qc2::QC2VideoROIRegionInfo::output roiRegion;
  auto clip = [](std::size_t len) { return len > 128 ? 128 : len; };
  roiRegion.timestampUs = config->roi.timestampUs;

  memcpy(roiRegion.type_, "rect", 4);
  memcpy(roiRegion.rectPayload, config->roi.rectPayload,
      clip(std::strlen(config->roi.rectPayload)));
  memcpy(roiRegion.rectPayloadExt, config->roi.rectPayloadExt,
      clip(std::strlen(config->roi.rectPayloadExt)));

  return C2Param::Copy (roiRegion);
}

std::unique_ptr<C2Param>
setSliceMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;
  if (config->slice_mode == SLICE_MODE_BYTES) {
    qc2::C2VideoSliceSizeBytes::output SliceModeBytes;
    SliceModeBytes.value = config->val.u32;
    return C2Param::Copy (SliceModeBytes);
  } else if (config->slice_mode == SLICE_MODE_MB) {
    qc2::C2VideoSliceSizeMBCount::output SliceModeMb;
    SliceModeMb.value = config->val.u32;
    return C2Param::Copy (SliceModeMb);
  } else {
    return nullptr;
  }
}

std::unique_ptr<C2Param>
setQPRanges (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;
  qc2::C2VideoQPRangeSetting::output qp_ranges;
  qp_ranges.miniqp = config->qp_ranges.miniqp;
  qp_ranges.maxiqp = config->qp_ranges.maxiqp;
  qp_ranges.minpqp = config->qp_ranges.minpqp;
  qp_ranges.maxpqp = config->qp_ranges.maxpqp;
  qp_ranges.minbqp = config->qp_ranges.minbqp;
  qp_ranges.maxbqp = config->qp_ranges.maxbqp;

  return C2Param::Copy (qp_ranges);
}

std::unique_ptr<C2Param>
setDecLowLatency (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  C2GlobalLowLatencyModeTuning lowLatencyMode;
  lowLatencyMode.value = C2_TRUE;

  return C2Param::Copy (lowLatencyMode);
}

std::unique_ptr<C2Param>
setDownscale (gpointer param)
{
  if (param == NULL) {
    return nullptr;
  }

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    GST_WARNING ("setDownscale input not implemented");
  } else {
    qc2::C2VideoDownScalarSetting::output scale;

    scale.width = config->resolution.width;
    scale.height = config->resolution.height;

    return C2Param::Copy (scale);
  }

  return nullptr;
}

std::unique_ptr<C2Param>
setEncColorSpaceConv (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  qc2::C2VideoCSC::input colorSpaceConv;
  colorSpaceConv.value = config->color_space_conversion;
  return C2Param::Copy (colorSpaceConv);
}

std::unique_ptr<C2Param>
setColorAspectsInfo (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  C2StreamColorAspectsInfo::input color_aspects;
  color_aspects.primaries = toC2Primaries (config->color_aspects.primaries);
  color_aspects.transfer = toC2TransferChar (config->color_aspects.color_transfer);
  color_aspects.matrix = toC2Matrix (config->color_aspects.matrix);
  color_aspects.range = toC2FullRange (config->color_aspects.full_range);
  return C2Param::Copy (color_aspects);
}

std::unique_ptr<C2Param>
setIntraRefresh (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  C2StreamIntraRefreshTuning::output intraRefreshMode;
  intraRefreshMode.mode = (C2Config::intra_refresh_mode_t) config->ir_mode.type;
  intraRefreshMode.period = config->ir_mode.intra_refresh_mbs;
  return C2Param::Copy (intraRefreshMode);
}

std::unique_ptr<C2Param>
setBlurMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    qc2::C2VideoBlurInfo::input blur;
    blur.info = qc2::QCBlurMode (config->blur_mode);
    return C2Param::Copy (blur);
  } else {
    GST_WARNING ("setBlurMode output not implemented");
  }

  return nullptr;
}

std::unique_ptr<C2Param>
setBlurResolution (gpointer param)
{
  if (param == NULL)
    return nullptr;

  config_params_t *config = (config_params_t*) param;

  if (config->is_input) {
    qc2::C2VideoBlurInfo::input blur;
    blur.info = ((config->resolution.width << 16) |
        (config->resolution.height & 0xFFFF));
    return C2Param::Copy (blur);
  } else {
    GST_WARNING ("setBlurResolution output not implemented");
  }

  return nullptr;
}

void
push_to_settings (gpointer data, gpointer user_data)
{
  std::list<std::unique_ptr<C2Param>> *settings =
      (std::list<std::unique_ptr<C2Param> >*) user_data;
  config_params_t *conf_param = (config_params_t*) data;

  auto iter = sConfigFunctionMap.find (conf_param->config_name);
  if (iter != sConfigFunctionMap.end ()) {
    auto param = (*iter->second) (conf_param);
    settings->push_back (C2Param::Copy (*param));
  }
}
