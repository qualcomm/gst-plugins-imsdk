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

#include "c2-config.h"

#include <map>

#include <C2Config.h>
#include <QC2V4L2Config.h>

#define GST_CAT_DEFAULT gst_c2_venc_context_debug_category()
static GstDebugCategory *
gst_c2_venc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtic2engine", 0,
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
std::unique_ptr<C2Param> requestSyncFrame (gpointer param);
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
std::unique_ptr<C2Param> setEntroyMode (gpointer param);
std::unique_ptr<C2Param> setLoopFilterMode (gpointer param);
std::unique_ptr<C2Param> setQPInit (gpointer param);
std::unique_ptr<C2Param> setNumLtrFrames (gpointer param);
std::unique_ptr<C2Param> setProfileLevel (gpointer param);
std::unique_ptr<C2Param> setRotate (gpointer param);
std::unique_ptr<C2Param> setOutputBlockPoolId (gpointer param);

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
  { CONFIG_FUNCTION_KEY_REQUEST_SYNC_FRAME, requestSyncFrame },
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
  { CONFIG_FUNCTION_KEY_ENTROPY_MODE, setEntroyMode },
  { CONFIG_FUNCTION_KEY_LOOP_FILTER_MODE, setLoopFilterMode },
  { CONFIG_FUNCTION_KEY_QP_INIT, setQPInit },
  { CONFIG_FUNCTION_KEY_NUM_LTR_FRAMES, setNumLtrFrames },
  { CONFIG_FUNCTION_KEY_PROFILE_LEVEL, setProfileLevel },
  { CONFIG_FUNCTION_KEY_ROTATE, setRotate },
  { CONFIG_FUNCTION_KEY_BLOCK_POOL, setOutputBlockPoolId },
};

uint32_t
toC2PixelFormat (GstC2PixelFormat pixel)
{
  uint32_t result = 0;

  switch (pixel) {
    case GST_C2_PIXEL_FORMAT_NV12_LINEAR: {
      result = C2_PIXEL_FORMAT_VENUS_NV12;
      break;
    }
    case GST_C2_PIXEL_FORMAT_NV12_UBWC: {
      result = C2_PIXEL_FORMAT_VENUS_NV12_UBWC;
      break;
    }
    case GST_C2_PIXEL_FORMAT_RGBA_8888: {
      result = C2_PIXEL_FORMAT_RGBA8888;
      break;
    }
    case GST_C2_PIXEL_FORMAT_YV12: {
      result = C2_PIXEL_FORMAT_YV12;
      break;
    }
    case GST_C2_PIXEL_FORMAT_P010: {
      result = C2_PIXEL_FORMAT_VENUS_P010;
      break;
    }
    case GST_C2_PIXEL_FORMAT_TP10_UBWC: {
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
toC2RateControlMode (GstC2ControlRate mode)
{
  uint32_t rcMode = 0x7F000000; //GST_C2_RATE_CTRLMODE_EXT_DISABLE

  switch (mode) {
    case GST_C2_RATE_CTRLMODE_OFF: {
      rcMode = 0x7F000000; //GST_C2_RATE_CTRLMODE_EXT_DISABLE
      break;
    }
    case GST_C2_RATE_CTRLMODE_CONST: {
      rcMode = C2Config::BITRATE_CONST;
      break;
    }
    case GST_C2_RATE_CTRLMODE_CBR_VFR: {
      rcMode = C2Config::BITRATE_CONST_SKIP_ALLOWED;
      break;
    }
    case GST_C2_RATE_CTRLMODE_VBR_CFR: {
      rcMode = C2Config::BITRATE_VARIABLE;
      break;
    }
    case GST_C2_RATE_CTRLMODE_VBR_VFR: {
      rcMode = C2Config::BITRATE_VARIABLE_SKIP_ALLOWED;
      break;
    }
    case GST_C2_RATE_CTRLMODE_CQ: {
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
toC2Primaries (GstC2ColorPrimaries pixel)
{
  C2Color::primaries_t ret = C2Color::PRIMARIES_UNSPECIFIED;
  switch (pixel) {
  case GST_C2_COLOR_PRIMARIES_BT709:
    ret = C2Color::PRIMARIES_BT709;
    break;
  case GST_C2_COLOR_PRIMARIES_BT470_M:
    ret = C2Color::PRIMARIES_BT470_M;
    break;
  case GST_C2_COLOR_PRIMARIES_BT601_625:
    ret = C2Color::PRIMARIES_BT601_625;
    break;
  case GST_C2_COLOR_PRIMARIES_BT601_525:
    ret = C2Color::PRIMARIES_BT601_525;
    break;
  case GST_C2_COLOR_PRIMARIES_GENERIC_FILM:
    ret = C2Color::PRIMARIES_GENERIC_FILM;
    break;
  case GST_C2_COLOR_PRIMARIES_BT2020:
    ret = C2Color::PRIMARIES_BT2020;
    break;
  case GST_C2_COLOR_PRIMARIES_RP431:
    ret = C2Color::PRIMARIES_RP431;
    break;
  case GST_C2_COLOR_PRIMARIES_EG432:
    ret = C2Color::PRIMARIES_EG432;
    break;
  case GST_C2_COLOR_PRIMARIES_EBU3213:
    ret = C2Color::PRIMARIES_EBU3213;
    break;
  default:
    ret = C2Color::PRIMARIES_UNSPECIFIED;
    break;
  }

  return ret;
}

C2Color::transfer_t
toC2TransferChar (GstC2ColorTransfer color_transfer)
{
  C2Color::transfer_t ret = C2Color::TRANSFER_UNSPECIFIED;
  switch (color_transfer) {
  case GST_C2_COLOR_TRANSFER_LINEAR:
    ret = C2Color::TRANSFER_LINEAR;
    break;
  case GST_C2_COLOR_TRANSFER_SRGB:
    ret = C2Color::TRANSFER_SRGB;
    break;
  case GST_C2_COLOR_TRANSFER_170M:
    ret = C2Color::TRANSFER_170M;
    break;
  case GST_C2_COLOR_TRANSFER_GAMMA22:
    ret = C2Color::TRANSFER_GAMMA22;
    break;
  case GST_C2_COLOR_TRANSFER_GAMMA28:
    ret = C2Color::TRANSFER_GAMMA28;
    break;
  case GST_C2_COLOR_TRANSFER_ST2084:
    ret = C2Color::TRANSFER_ST2084;
    break;
  case GST_C2_COLOR_TRANSFER_HLG:
    ret = C2Color::TRANSFER_HLG;
    break;
  case GST_C2_COLOR_TRANSFER_240M:
    ret = C2Color::TRANSFER_240M;
    break;
  case GST_C2_COLOR_TRANSFER_XVYCC:
    ret = C2Color::TRANSFER_XVYCC;
    break;
  case GST_C2_COLOR_TRANSFER_BT1361:
    ret = C2Color::TRANSFER_BT1361;
    break;
  case GST_C2_COLOR_TRANSFER_ST428:
    ret = C2Color::TRANSFER_ST428;
    break;
  default:
    ret = C2Color::TRANSFER_UNSPECIFIED;
    break;
  }

  return ret;
}
C2Color::matrix_t
toC2Matrix (GstC2ColorMatrix matrix)
{
  C2Color::matrix_t ret = C2Color::MATRIX_UNSPECIFIED;
  switch (matrix) {
  case GST_C2_COLOR_MATRIX_BT709:
    ret = C2Color::MATRIX_BT709;
    break;
  case GST_C2_COLOR_MATRIX_FCC47_73_682:
    ret = C2Color::MATRIX_FCC47_73_682;
    break;
  case GST_C2_COLOR_MATRIX_BT601:
    ret = C2Color::MATRIX_BT601;
    break;
  case GST_C2_COLOR_MATRIX_240M:
    ret = C2Color::MATRIX_240M;
    break;
  case GST_C2_COLOR_MATRIX_BT2020:
    ret = C2Color::MATRIX_BT2020;
    break;
  case GST_C2_COLOR_MATRIX_BT2020_CONSTANT:
    ret = C2Color::MATRIX_BT2020_CONSTANT;
    break;
  default:
    ret = C2Color::MATRIX_UNSPECIFIED;
    break;
  }
  return ret;
}
C2Color::range_t
toC2FullRange (GstC2ColorRange color_range)
{
  C2Color::range_t ret = C2Color::RANGE_UNSPECIFIED;
  switch (color_range) {
  case GST_C2_COLOR_RANGE_FULL:
    ret = C2Color::RANGE_FULL;
    break;
  case GST_C2_COLOR_RANGE_LIMITED:
    ret = C2Color::RANGE_LIMITED;
    break;
  default:
    ret = C2Color::RANGE_UNSPECIFIED;
    break;
  }
  return ret;
}

uint32_t
toC2EntropyMode (GstC2EntropyMode mode)
{
  uint32_t entropy_mode = ENTROPYMODE_CAVLC;

  switch (mode) {
    case GST_C2_ENTROPY_MODE_CAVLC: {
      entropy_mode = ENTROPYMODE_CAVLC;
      break;
    }
    case GST_C2_ENTROPY_MODE_CABAC: {
      entropy_mode = ENTROPYMODE_CABAC;
      break;
    }
    default: {
      GST_ERROR ("Invalid Entropy Mode: %d", mode);
    }
  }

  return entropy_mode;
}

uint32_t
toC2LoopFilterMode (GstC2LoopFilterMode mode)
{
  uint32_t loop_filter_mode = Qc2AvcLoopFilterEnable;

  switch (mode) {
    case GST_C2_LOOP_FILTER_ENABLE: {
      loop_filter_mode = Qc2AvcLoopFilterEnable;
      break;
    }
    case GST_C2_LOOP_FILTER_DISABLE: {
      loop_filter_mode = Qc2AvcLoopFilterDisable;
      break;
    }
    case GST_C2_LOOP_FILTER_DISABLE_SLICE_BOUNDARY: {
      loop_filter_mode = Qc2AvcLoopFilterDisableSliceBoundary;
      break;
    }
    default: {
      GST_ERROR ("Invalid Loop Filter Mode: %d", mode);
    }
  }

  return loop_filter_mode;
}

uint32_t
toC2Rotate (GstC2Rotate rotate)
{
  uint32_t GstC2Rotateype = ROTATION_NONE;

  switch (rotate) {
    case GST_C2_ROTATE_NONE: {
      GstC2Rotateype = GST_C2_ROTATE_NONE;
      break;
    }
    case GST_C2_ROTATE_90_CW: {
      GstC2Rotateype = ROTATION_90;
      break;
    }
    case GST_C2_ROTATE_180: {
      GstC2Rotateype = ROTATION_180;
      break;
    }
    case GST_C2_ROTATE_90_CCW: {
      GstC2Rotateype = ROTATION_270;
      break;
    }
    default: {
      GST_ERROR ("Invalid Rotate: %d", rotate);
    }
  }

  return GstC2Rotateype;
}

uint32_t
toC2Profile (GstC2VideoProfile profile)
{
  uint32_t c2_profile = C2Config::profile_t::PROFILE_AVC_BASELINE;

  switch (profile) {
    case GST_C2_AVC_PROFILE_BASELINE: {
      c2_profile = C2Config::profile_t::PROFILE_AVC_BASELINE;
      break;
    }
    case GST_C2_AVC_PROFILE_CONSTRAINT_BASELINE: {
      c2_profile = C2Config::profile_t::PROFILE_AVC_CONSTRAINED_BASELINE;
      break;
    }
    case GST_C2_AVC_PROFILE_CONSTRAINT_HIGH: {
      c2_profile = C2Config::profile_t::PROFILE_AVC_CONSTRAINED_HIGH;
      break;
    }
    case GST_C2_AVC_PROFILE_HIGH: {
      c2_profile = C2Config::profile_t::PROFILE_AVC_HIGH;
      break;
    }
    case GST_C2_AVC_PROFILE_MAIN: {
      c2_profile = C2Config::profile_t::PROFILE_AVC_MAIN;
      break;
    }
    case GST_C2_HEVC_PROFILE_MAIN: {
      c2_profile = C2Config::profile_t::PROFILE_HEVC_MAIN;
      break;
    }
    case GST_C2_HEVC_PROFILE_MAIN10: {
      c2_profile = C2Config::profile_t::PROFILE_HEVC_MAIN_10;
      break;
    }
    case GST_C2_HEVC_PROFILE_MAIN_STILL_PIC: {
      c2_profile = C2Config::profile_t::PROFILE_HEVC_MAIN_STILL;
      break;
    }
    default: {
      GST_ERROR ("Invalid profile: %d", c2_profile);
    }
  }

  return c2_profile;
}

uint32_t
toC2Level (GstC2VideoLevel level)
{
  uint32_t c2_level = C2Config::level_t::LEVEL_AVC_1;

  switch (level) {
    case GST_C2_AVC_LEVEL_1: {
      c2_level = C2Config::level_t::LEVEL_AVC_1;
      break;
    }
    case GST_C2_AVC_LEVEL_1b: {
      c2_level = C2Config::level_t::LEVEL_AVC_1B;
      break;
    }
    case GST_C2_AVC_LEVEL_11: {
      c2_level = C2Config::level_t::LEVEL_AVC_1_1;
      break;
    }
    case GST_C2_AVC_LEVEL_12: {
      c2_level = C2Config::level_t::LEVEL_AVC_1_2;
      break;
    }
    case GST_C2_AVC_LEVEL_13: {
      c2_level = C2Config::level_t::LEVEL_AVC_1_3;
      break;
    }
    case GST_C2_AVC_LEVEL_2: {
      c2_level = C2Config::level_t::LEVEL_AVC_2;
      break;
    }
    case GST_C2_AVC_LEVEL_21: {
      c2_level = C2Config::level_t::LEVEL_AVC_2_1;
      break;
    }
    case GST_C2_AVC_LEVEL_22: {
      c2_level = C2Config::level_t::LEVEL_AVC_2_2;
      break;
    }
    case GST_C2_AVC_LEVEL_3: {
      c2_level = C2Config::level_t::LEVEL_AVC_3;
      break;
    }
    case GST_C2_AVC_LEVEL_31: {
      c2_level = C2Config::level_t::LEVEL_AVC_3_1;
      break;
    }
    case GST_C2_AVC_LEVEL_32: {
      c2_level = C2Config::level_t::LEVEL_AVC_3_2;
      break;
    }
    case GST_C2_AVC_LEVEL_4: {
      c2_level = C2Config::level_t::LEVEL_AVC_4;
      break;
    }
    case GST_C2_AVC_LEVEL_41: {
      c2_level = C2Config::level_t::LEVEL_AVC_4_1;
      break;
    }
    case GST_C2_AVC_LEVEL_42: {
      c2_level = C2Config::level_t::LEVEL_AVC_4_2;
      break;
    }
    case GST_C2_AVC_LEVEL_5: {
      c2_level = C2Config::level_t::LEVEL_AVC_5;
      break;
    }
    case GST_C2_AVC_LEVEL_51: {
      c2_level = C2Config::level_t::LEVEL_AVC_5_1;
      break;
    }
    case GST_C2_AVC_LEVEL_52: {
      c2_level = C2Config::level_t::LEVEL_AVC_5_2;
      break;
    }
    case GST_C2_AVC_LEVEL_6: {
      c2_level = C2Config::level_t::LEVEL_AVC_6;
      break;
    }
    case GST_C2_AVC_LEVEL_61: {
      c2_level = C2Config::level_t::LEVEL_AVC_6_1;
      break;
    }
    case GST_C2_AVC_LEVEL_62: {
      c2_level = C2Config::level_t::LEVEL_AVC_6_2;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL1: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL2: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_2;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL21: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_2_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL3: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_3;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL31: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_3_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL4: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_4;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL41: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_4_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL5: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_5;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL51: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_5_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL52: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_5_2;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL6: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_6;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL61: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_6_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL62: {
      c2_level = C2Config::level_t::LEVEL_HEVC_MAIN_6_2;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL1: {
      c2_level = C2_PROFILE_LEVEL_VENDOR_START + 0x100;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL2: {
      c2_level = C2_PROFILE_LEVEL_VENDOR_START + 0x101;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL21: {
      c2_level = C2_PROFILE_LEVEL_VENDOR_START + 0x102;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL3: {
      c2_level = C2_PROFILE_LEVEL_VENDOR_START + 0x103;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL31: {
      c2_level = C2_PROFILE_LEVEL_VENDOR_START + 0x104;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL4: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_4;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL41: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_4_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL5: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_5;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL51: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_5_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL52: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_5_2;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL6: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_6;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL61: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_6_1;
      break;
    }
    case GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL62: {
      c2_level = C2Config::level_t::LEVEL_HEVC_HIGH_6_2;
      break;
    }
    default: {
      GST_ERROR ("Invalid level: %d", c2_level);
    }
  }

  return c2_level;
}

std::unique_ptr<C2Param>
setVideoPixelformat (gpointer param)
{
  if (param == NULL) {
    return nullptr;
  }

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  if (config->is_input) {
    qc2::C2VideoMirrorTuning::input mirror;
    mirror.mirrorType = qc2::QCMirrorType (config->GstC2Mirrorype);
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

  GstC2ConfigParams *config = (GstC2ConfigParams*)param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  C2StreamSyncFrameIntervalTuning::output syncFrameInterval;
  syncFrameInterval.value = config->val.i64;
  return C2Param::Copy (syncFrameInterval);
}

std::unique_ptr<C2Param>
requestSyncFrame (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  C2StreamRequestSyncFrameTuning::output requestSyncFrame;
  requestSyncFrame.value = config->val.bl;
  return C2Param::Copy (requestSyncFrame);
}

std::unique_ptr<C2Param>
setOutputPictureOrderMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  qc2::C2VideoPictureOrder::output outputPictureOrderMode;
  if (config->output_picture_order_mode == GST_C2_OUTPUT_PICTURE_ORDER_DECODER)
    outputPictureOrderMode.enable = C2_TRUE;
  return C2Param::Copy (outputPictureOrderMode);
}

std::unique_ptr<C2Param>
setROIEncoding (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  GST_INFO ("Set ROI encoding - %s %s", config->roi.payload,
      config->roi.payload_ext);

#ifndef CODEC2_CONFIG_VERSION_2_0
  qc2::QC2VideoROIRegionInfo::output roiRegion;
#else
  qc2::QC2VideoROIRegionInfo::input roiRegion;
#endif

  auto clip = [](std::size_t len) { return len > 128 ? 128 : len; };
  roiRegion.timestampUs = config->roi.timestamp;

  memcpy(roiRegion.type_, "rect", 4);
  memcpy(roiRegion.rectPayload, config->roi.payload,
      clip(std::strlen(config->roi.payload)));
  memcpy(roiRegion.rectPayloadExt, config->roi.payload_ext,
      clip(std::strlen(config->roi.payload_ext)));

  return C2Param::Copy (roiRegion);
}

std::unique_ptr<C2Param>
setSliceMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;
  if (config->slice_mode == GST_C2_SLICE_MODE_BYTES) {
    qc2::C2VideoSliceSizeBytes::output SliceModeBytes;
    SliceModeBytes.value = config->val.u32;
    return C2Param::Copy (SliceModeBytes);
  } else if (config->slice_mode == GST_C2_SLICE_MODE_MB) {
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

#ifndef CODEC2_CONFIG_VERSION_2_0
  GstC2ConfigParams *config = (GstC2ConfigParams*) param;
  qc2::C2VideoQPRangeSetting::output qp_ranges;
  qp_ranges.miniqp = config->qp_ranges.miniqp;
  qp_ranges.maxiqp = config->qp_ranges.maxiqp;
  qp_ranges.minpqp = config->qp_ranges.minpqp;
  qp_ranges.maxpqp = config->qp_ranges.maxpqp;
  qp_ranges.minbqp = config->qp_ranges.minbqp;
  qp_ranges.maxbqp = config->qp_ranges.maxbqp;

  return C2Param::Copy (qp_ranges);
#else
  GstC2ConfigParams *config = (GstC2ConfigParams*) param;
  auto qp_ranges = C2StreamPictureQuantizationTuning::output::AllocUnique(3,0u);
  qp_ranges->m.values[0].type_ = I_FRAME;
  qp_ranges->m.values[0].min  = config->qp_ranges.miniqp;
  qp_ranges->m.values[0].max  = config->qp_ranges.maxiqp;
  qp_ranges->m.values[1].type_ = P_FRAME;
  qp_ranges->m.values[1].min  = config->qp_ranges.minpqp;
  qp_ranges->m.values[1].max  = config->qp_ranges.maxpqp;
  qp_ranges->m.values[2].type_ = B_FRAME;
  qp_ranges->m.values[2].min  = config->qp_ranges.minbqp;
  qp_ranges->m.values[2].max  = config->qp_ranges.maxbqp;

  return C2Param::Copy (*qp_ranges);
#endif
}

std::unique_ptr<C2Param>
setDecLowLatency (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  qc2::C2VideoCSC::input colorSpaceConv;
  colorSpaceConv.value = config->color_space_conversion;
  return C2Param::Copy (colorSpaceConv);
}

std::unique_ptr<C2Param>
setColorAspectsInfo (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  C2StreamIntraRefreshTuning::output intraRefreshMode;
  if (config->ir_mode.type == GST_C2_INTRA_REFRESH_MODE_ARBITRARY) {
    intraRefreshMode.mode = C2Config::INTRA_REFRESH_ARBITRARY;
    intraRefreshMode.period = config->ir_mode.intra_refresh_mbs;
  } else if (config->ir_mode.type == GST_C2_INTRA_REFRESH_MODE_DISABLE) {
    intraRefreshMode.mode = C2Config::INTRA_REFRESH_DISABLED;
  } else {
    return nullptr;
  }
  return C2Param::Copy (intraRefreshMode);
}

std::unique_ptr<C2Param>
setBlurMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

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

std::unique_ptr<C2Param>
setEntroyMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  qc2::C2VideoEntropyMode::output entropy;
  entropy.value =
      (qc2::EntropyMode) toC2EntropyMode (config->entropy_mode);
  return C2Param::Copy (entropy);
}

std::unique_ptr<C2Param>
setLoopFilterMode (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  qc2::C2VideoDeblockFilter::output loop_filter;
  loop_filter.value =
      (qc2::QCDeblockFilter) toC2LoopFilterMode (config->loop_filter_mode);
  return C2Param::Copy (loop_filter);
}

std::unique_ptr<C2Param>
setQPInit (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;
  qc2::C2VideoInitQPSetting::output qp_init;
  qp_init.qpI = config->qp_init.quant_i_frames;
  qp_init.qpIEnable = config->qp_init.quant_i_frames_enable;
  qp_init.qpP = config->qp_init.quant_p_frames;
  qp_init.qpPEnable = config->qp_init.quant_p_frames_enable;
  qp_init.qpB = config->qp_init.quant_b_frames;
  qp_init.qpBEnable = config->qp_init.quant_b_frames_enable;

  return C2Param::Copy (qp_init);
}

std::unique_ptr<C2Param>
setNumLtrFrames (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  qc2::C2VideoLTRCountSetting::input num_ltr_frames;
  num_ltr_frames.count = config->val.u32;

  return C2Param::Copy (num_ltr_frames);
}

std::unique_ptr<C2Param>
setProfileLevel (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  C2StreamProfileLevelInfo::output profileLevel;
  profileLevel.profile = (C2Config::profile_t)toC2Profile(config->profile);
  profileLevel.level = (C2Config::level_t)toC2Level(config->level);

  return C2Param::Copy (profileLevel);
}

std::unique_ptr<C2Param>
setRotate (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;

  qc2::C2VideoRotation::input rotate;
  rotate.angle = (qc2::RotationType)toC2Rotate(config->rotate);

  return C2Param::Copy (rotate);
}

std::unique_ptr<C2Param>
setOutputBlockPoolId (gpointer param)
{
  if (param == NULL)
    return nullptr;

  GstC2ConfigParams *config = (GstC2ConfigParams*) param;
  C2BlockPool::local_id_t id = config->val.u32;

  auto blockPoolTuning =
        C2PortBlockPoolsTuning::output::AllocUnique({id});

  return C2Param::Copy (*blockPoolTuning);
}

void
push_to_settings (gpointer data, gpointer user_data)
{
  std::list<std::unique_ptr<C2Param>> *settings =
      (std::list<std::unique_ptr<C2Param> >*) user_data;
  GstC2ConfigParams *conf_param = (GstC2ConfigParams*) data;

  auto iter = sConfigFunctionMap.find (conf_param->config_name);
  if (iter != sConfigFunctionMap.end ()) {
    auto param = (*iter->second) (conf_param);
    settings->push_back (C2Param::Copy (*param));
  }
}

GstC2VideoProfile
gst_c2_utils_h264_profile_from_string (const gchar * profile)
{
  if (g_str_equal (profile, "baseline"))
    return GST_C2_AVC_PROFILE_BASELINE;
  else if (g_str_equal (profile, "constraint-baseline"))
    return GST_C2_AVC_PROFILE_CONSTRAINT_BASELINE;
  else if (g_str_equal (profile, "main"))
    return GST_C2_AVC_PROFILE_MAIN;
  else if (g_str_equal (profile, "high"))
    return GST_C2_AVC_PROFILE_HIGH;
  else if (g_str_equal (profile, "constraint-high"))
    return GST_C2_AVC_PROFILE_CONSTRAINT_HIGH;

  return GST_C2_VIDEO_PROFILE_MAX;
}

GstC2VideoProfile
gst_c2_utils_h265_profile_from_string (const gchar * profile)
{
  if (g_str_equal (profile, "main"))
    return GST_C2_HEVC_PROFILE_MAIN;
  else if (g_str_equal (profile, "main-10"))
    return GST_C2_HEVC_PROFILE_MAIN10;
  else if (g_str_equal (profile, "main-still-picture"))
    return GST_C2_HEVC_PROFILE_MAIN_STILL_PIC;

  return GST_C2_VIDEO_PROFILE_MAX;
}

GstC2VideoLevel
gst_c2_utils_h264_level_from_string (const gchar * level)
{
  if (g_str_equal (level, "1"))
    return GST_C2_AVC_LEVEL_1;
  else if (g_str_equal (level, "1b"))
    return GST_C2_AVC_LEVEL_1b;
  else if (g_str_equal (level, "1.1"))
    return GST_C2_AVC_LEVEL_11;
  else if (g_str_equal (level, "1.2"))
    return GST_C2_AVC_LEVEL_12;
  else if (g_str_equal (level, "1.3"))
    return GST_C2_AVC_LEVEL_13;
  else if (g_str_equal (level, "2"))
    return GST_C2_AVC_LEVEL_2;
  else if (g_str_equal (level, "2.1"))
    return GST_C2_AVC_LEVEL_21;
  else if (g_str_equal (level, "2.2"))
    return GST_C2_AVC_LEVEL_22;
  else if (g_str_equal (level, "3"))
    return GST_C2_AVC_LEVEL_3;
  else if (g_str_equal (level, "3.1"))
    return GST_C2_AVC_LEVEL_31;
  else if (g_str_equal (level, "3.2"))
    return GST_C2_AVC_LEVEL_32;
  else if (g_str_equal (level, "4"))
    return GST_C2_AVC_LEVEL_4;
  else if (g_str_equal (level, "4.1"))
    return GST_C2_AVC_LEVEL_41;
  else if (g_str_equal (level, "4.2"))
    return GST_C2_AVC_LEVEL_42;
  else if (g_str_equal (level, "5"))
    return GST_C2_AVC_LEVEL_5;
  else if (g_str_equal (level, "5.1"))
    return GST_C2_AVC_LEVEL_51;
  else if (g_str_equal (level, "5.2"))
    return GST_C2_AVC_LEVEL_52;
  else if (g_str_equal (level, "6.0"))
    return GST_C2_AVC_LEVEL_6;
  else if (g_str_equal (level, "6.1"))
    return GST_C2_AVC_LEVEL_61;
  else if (g_str_equal (level, "6.2"))
    return GST_C2_AVC_LEVEL_62;

  return GST_C2_VIDEO_LEVEL_MAX;
}

GstC2VideoLevel
gst_c2_utils_h265_level_from_string (const gchar * level, const gchar * tier)
{
  if (g_str_equal (tier, "main")) {
    if (g_str_equal (level, "1"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL1;
    else if (g_str_equal (level, "2"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL2;
    else if (g_str_equal (level, "2.1"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL21;
    else if (g_str_equal (level, "3"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL3;
    else if (g_str_equal (level, "3.1"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL31;
    else if (g_str_equal (level, "4"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL4;
    else if (g_str_equal (level, "4.1"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL41;
    else if (g_str_equal (level, "5"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL5;
    else if (g_str_equal (level, "5.1"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL51;
    else if (g_str_equal (level, "5.2"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL52;
    else if (g_str_equal (level, "6"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL6;
    else if (g_str_equal (level, "6.1"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL61;
    else if (g_str_equal (level, "6.2"))
      return GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL62;
  } else if (g_str_equal (tier, "high")) {
    if (g_str_equal (level, "4"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL4;
    else if (g_str_equal (level, "4.1"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL41;
    else if (g_str_equal (level, "5"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL5;
    else if (g_str_equal (level, "5.1"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL51;
    else if (g_str_equal (level, "5.2"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL52;
    else if (g_str_equal (level, "6"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL6;
    else if (g_str_equal (level, "6.1"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL61;
    else if (g_str_equal (level, "6.2"))
      return GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL62;
  }
  return GST_C2_VIDEO_LEVEL_MAX;
}