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

#include "c2-engine-utils.h"

#include <C2PlatformSupport.h>
#include <C2BlockInternal.h>

#ifdef HAVE_MMM_COLOR_FMT_H
#include <display/media/mmm_color_fmt.h>
#else
#include <vidc/media/msm_media_info.h>
#define MMM_COLOR_FMT_NV12             COLOR_FMT_NV12
#define MMM_COLOR_FMT_NV12_UBWC        COLOR_FMT_NV12_UBWC
#define MMM_COLOR_FMT_NV12_BPP10_UBWC  COLOR_FMT_NV12_BPP10_UBWC
#define MMM_COLOR_FMT_P010             COLOR_FMT_P010
#define MMM_COLOR_FMT_Y_META_STRIDE    VENUS_Y_META_STRIDE
#define MMM_COLOR_FMT_Y_META_SCANLINES VENUS_Y_META_SCANLINES
#define MMM_COLOR_FMT_Y_SCANLINES      VENUS_Y_SCANLINES
#define MMM_COLOR_FMT_ALIGN            MSM_MEDIA_ALIGN
#endif // HAVE_MMM_COLOR_FMT_H

// Map between engine parameter enum and the corresponding Codec2 config index.
static const std::unordered_map<uint32_t, C2Param::Index> kParamIndexMap = {
  { GST_C2_PARAM_IN_FORMAT,
      C2StreamPixelFormatInfo::input::PARAM_TYPE },
  { GST_C2_PARAM_OUT_FORMAT,
      C2StreamPixelFormatInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_IN_RESOLUTION,
      C2StreamPictureSizeInfo::input::PARAM_TYPE },
  { GST_C2_PARAM_OUT_RESOLUTION,
      C2StreamPictureSizeInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_IN_FRAMERATE,
      C2StreamFrameRateInfo::input::PARAM_TYPE },
  { GST_C2_PARAM_OUT_FRAMERATE,
      C2StreamFrameRateInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_RATE_CONTROL,
      C2StreamBitrateModeTuning::output::PARAM_TYPE },
  { GST_C2_PARAM_PROFILE_LEVEL,
      C2StreamProfileLevelInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_BITRATE,
      C2StreamBitrateInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_GOP_CONFIG,
      C2StreamGopTuning::output::PARAM_TYPE },
  { GST_C2_PARAM_KEY_FRAME_INTERVAL,
      C2StreamSyncFrameIntervalTuning::output::PARAM_TYPE },
  { GST_C2_PARAM_INTRA_REFRESH,
      C2StreamIntraRefreshTuning::output::PARAM_TYPE },
  { GST_C2_PARAM_ENTROPY_MODE,
      qc2::C2VideoEntropyMode::output::PARAM_TYPE },
  { GST_C2_PARAM_LOOP_FILTER_MODE,
      qc2::C2VideoDeblockFilter::output::PARAM_TYPE },
  { GST_C2_PARAM_SLICE_MB,
      qc2::C2VideoSliceSizeMBCount::output::PARAM_TYPE },
  { GST_C2_PARAM_SLICE_BYTES,
      qc2::C2VideoSliceSizeBytes::output::PARAM_TYPE },
  { GST_C2_PARAM_NUM_LTR_FRAMES,
      qc2::C2VideoLTRCountSetting::input::PARAM_TYPE },
  { GST_C2_PARAM_ROTATION,
      qc2::C2VideoRotation::input::PARAM_TYPE },
  { GST_C2_PARAM_TILE_LAYOUT,
      C2StreamTileLayoutInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_PREPEND_HEADER_MODE,
      C2PrependHeaderModeSetting::PARAM_TYPE },
  { GST_C2_PARAM_ENABLE_PICTURE_ORDER,
      qc2::C2VideoPictureOrder::output::PARAM_TYPE },
  { GST_C2_PARAM_QP_INIT,
      qc2::C2VideoInitQPSetting::output::PARAM_TYPE },
#if defined(CODEC2_CONFIG_VERSION_2_0)
  { GST_C2_PARAM_QP_RANGES,
      C2StreamPictureQuantizationTuning::output::PARAM_TYPE },
#else
  { GST_C2_PARAM_QP_RANGES,
      qc2::C2VideoQPRangeSetting::output::PARAM_TYPE },
#endif // CODEC2_CONFIG_VERSION_2_0
  { GST_C2_PARAM_ROI_ENCODE,
      qc2::QC2VideoROIRegionInfo::output::PARAM_TYPE },
  { GST_C2_PARAM_TRIGGER_SYNC_FRAME,
      C2StreamRequestSyncFrameTuning::output::PARAM_TYPE },
};

// Convenient map for printing the engine parameter name in string form.
static const std::unordered_map<uint32_t, const char*> kParamNameMap = {
  { GST_C2_PARAM_IN_FORMAT, "IN_FORMAT" },
  { GST_C2_PARAM_OUT_FORMAT, "OUT_FORMAT" },
  { GST_C2_PARAM_IN_RESOLUTION, "IN_RESOLUTION" },
  { GST_C2_PARAM_OUT_RESOLUTION, "OUT_RESOLUTION" },
  { GST_C2_PARAM_IN_FRAMERATE, "IN_FRAMERATE" },
  { GST_C2_PARAM_OUT_FRAMERATE, "OUT_FRAMERATE" },
  { GST_C2_PARAM_RATE_CONTROL, "RATE_CONTROL" },
  { GST_C2_PARAM_PROFILE_LEVEL, "PROFILE_LEVEL" },
  { GST_C2_PARAM_BITRATE, "BITRATE" },
  { GST_C2_PARAM_GOP_CONFIG, "GOP_CONFIG" },
  { GST_C2_PARAM_KEY_FRAME_INTERVAL, "KEY_FRAME_INTERVAL" },
  { GST_C2_PARAM_INTRA_REFRESH, "INTRA_REFRESH" },
  { GST_C2_PARAM_ENTROPY_MODE, "ENTROPY_MODE" },
  { GST_C2_PARAM_LOOP_FILTER_MODE, "LOOP_FILTER_MODE" },
  { GST_C2_PARAM_SLICE_MB, "SLICE_MB" },
  { GST_C2_PARAM_SLICE_BYTES, "SLICE_BYTES" },
  { GST_C2_PARAM_NUM_LTR_FRAMES, "NUM_LTR_FRAMES" },
  { GST_C2_PARAM_ROTATION, "ROTATION" },
  { GST_C2_PARAM_TILE_LAYOUT, "TILE_LAYOUT" },
  { GST_C2_PARAM_PREPEND_HEADER_MODE, "PREPEND_HEADER_MODE" },
  { GST_C2_PARAM_ENABLE_PICTURE_ORDER, "ENABLE_PICTURE_ORDER" },
  { GST_C2_PARAM_QP_INIT, "QP_INIT" },
  { GST_C2_PARAM_QP_RANGES, "QP_RANGES" },
  { GST_C2_PARAM_ROI_ENCODE, "ROI_ENCODE" },
  { GST_C2_PARAM_TRIGGER_SYNC_FRAME, "TRIGGER_SYNC_FRAME" },
};

// Map for the GST_C2_PARAM_PROFILE_LEVEL parameter.
static const std::unordered_map<uint32_t, C2Config::profile_t> kProfileMap = {
  { GST_C2_PROFILE_AVC_BASELINE,
      C2Config::profile_t::PROFILE_AVC_BASELINE },
  { GST_C2_PROFILE_AVC_CONSTRAINT_BASELINE,
      C2Config::profile_t::PROFILE_AVC_CONSTRAINED_BASELINE },
  { GST_C2_PROFILE_AVC_CONSTRAINT_HIGH,
      C2Config::profile_t::PROFILE_AVC_CONSTRAINED_HIGH },
  { GST_C2_PROFILE_AVC_HIGH,
      C2Config::profile_t::PROFILE_AVC_HIGH },
  { GST_C2_PROFILE_AVC_MAIN,
      C2Config::profile_t::PROFILE_AVC_MAIN },
  { GST_C2_PROFILE_HEVC_MAIN,
      C2Config::profile_t::PROFILE_HEVC_MAIN },
  { GST_C2_PROFILE_HEVC_MAIN10,
      C2Config::profile_t::PROFILE_HEVC_MAIN_10 },
  { GST_C2_PROFILE_HEVC_MAIN_STILL,
      C2Config::profile_t::PROFILE_HEVC_MAIN_STILL },
};

// Map for the GST_C2_PARAM_PROFILE_LEVEL parameter.
static const std::unordered_map<uint32_t, C2Config::level_t> kLevelMap = {
  { GST_C2_LEVEL_AVC_1,         C2Config::level_t::LEVEL_AVC_1 },
  { GST_C2_LEVEL_AVC_1B,        C2Config::level_t::LEVEL_AVC_1B },
  { GST_C2_LEVEL_AVC_1_1,       C2Config::level_t::LEVEL_AVC_1_1 },
  { GST_C2_LEVEL_AVC_1_2,       C2Config::level_t::LEVEL_AVC_1_2 },
  { GST_C2_LEVEL_AVC_1_3,       C2Config::level_t::LEVEL_AVC_1_3 },
  { GST_C2_LEVEL_AVC_2,         C2Config::level_t::LEVEL_AVC_2 },
  { GST_C2_LEVEL_AVC_2_1,       C2Config::level_t::LEVEL_AVC_2_1 },
  { GST_C2_LEVEL_AVC_2_2,       C2Config::level_t::LEVEL_AVC_2_2 },
  { GST_C2_LEVEL_AVC_3,         C2Config::level_t::LEVEL_AVC_3 },
  { GST_C2_LEVEL_AVC_3_1,       C2Config::level_t::LEVEL_AVC_3_1 },
  { GST_C2_LEVEL_AVC_3_2,       C2Config::level_t::LEVEL_AVC_3_2 },
  { GST_C2_LEVEL_AVC_4,         C2Config::level_t::LEVEL_AVC_4 },
  { GST_C2_LEVEL_AVC_4_1,       C2Config::level_t::LEVEL_AVC_4_1 },
  { GST_C2_LEVEL_AVC_4_1,       C2Config::level_t::LEVEL_AVC_4_2 },
  { GST_C2_LEVEL_AVC_5,         C2Config::level_t::LEVEL_AVC_5 },
  { GST_C2_LEVEL_AVC_5_1,       C2Config::level_t::LEVEL_AVC_5_1 },
  { GST_C2_LEVEL_AVC_5_2,       C2Config::level_t::LEVEL_AVC_5_2 },
  { GST_C2_LEVEL_AVC_6,         C2Config::level_t::LEVEL_AVC_6 },
  { GST_C2_LEVEL_AVC_6_1,       C2Config::level_t::LEVEL_AVC_6_1 },
  { GST_C2_LEVEL_AVC_6_2,       C2Config::level_t::LEVEL_AVC_6_2 },
  { GST_C2_LEVEL_HEVC_MAIN_1,   C2Config::level_t::LEVEL_HEVC_MAIN_1 },
  { GST_C2_LEVEL_HEVC_MAIN_2,   C2Config::level_t::LEVEL_HEVC_MAIN_2 },
  { GST_C2_LEVEL_HEVC_MAIN_2_1, C2Config::level_t::LEVEL_HEVC_MAIN_2_1 },
  { GST_C2_LEVEL_HEVC_MAIN_3,   C2Config::level_t::LEVEL_HEVC_MAIN_3 },
  { GST_C2_LEVEL_HEVC_MAIN_3_1, C2Config::level_t::LEVEL_HEVC_MAIN_3_1 },
  { GST_C2_LEVEL_HEVC_MAIN_4,   C2Config::level_t::LEVEL_HEVC_MAIN_4 },
  { GST_C2_LEVEL_HEVC_MAIN_4_1, C2Config::level_t::LEVEL_HEVC_MAIN_4_1 },
  { GST_C2_LEVEL_HEVC_MAIN_5,   C2Config::level_t::LEVEL_HEVC_MAIN_5 },
  { GST_C2_LEVEL_HEVC_MAIN_5_1, C2Config::level_t::LEVEL_HEVC_MAIN_5_1 },
  { GST_C2_LEVEL_HEVC_MAIN_5_2, C2Config::level_t::LEVEL_HEVC_MAIN_5_2 },
  { GST_C2_LEVEL_HEVC_MAIN_6,   C2Config::level_t::LEVEL_HEVC_MAIN_6 },
  { GST_C2_LEVEL_HEVC_MAIN_6_1, C2Config::level_t::LEVEL_HEVC_MAIN_6_1 },
  { GST_C2_LEVEL_HEVC_MAIN_6_2, C2Config::level_t::LEVEL_HEVC_MAIN_6_2 },
  { GST_C2_LEVEL_HEVC_HIGH_4,   C2Config::level_t::LEVEL_HEVC_HIGH_4 },
  { GST_C2_LEVEL_HEVC_HIGH_4_1, C2Config::level_t::LEVEL_HEVC_HIGH_4_1 },
  { GST_C2_LEVEL_HEVC_HIGH_5,   C2Config::level_t::LEVEL_HEVC_HIGH_5 },
  { GST_C2_LEVEL_HEVC_HIGH_5_1, C2Config::level_t::LEVEL_HEVC_HIGH_5_1 },
  { GST_C2_LEVEL_HEVC_HIGH_5_2, C2Config::level_t::LEVEL_HEVC_HIGH_5_2 },
  { GST_C2_LEVEL_HEVC_HIGH_6,   C2Config::level_t::LEVEL_HEVC_HIGH_6 },
  { GST_C2_LEVEL_HEVC_HIGH_6_1, C2Config::level_t::LEVEL_HEVC_HIGH_6_1 },
  { GST_C2_LEVEL_HEVC_HIGH_6_2, C2Config::level_t::LEVEL_HEVC_HIGH_6_2 },
};

// Map for the GST_C2_PARAM_RATE_CONTROL parameter.
static const std::unordered_map<uint32_t, uint32_t> kRateCtrlMap = {
  { GST_C2_RATE_CTRL_DISABLE,  0x7F000000 },
  { GST_C2_RATE_CTRL_CONSTANT, C2Config::BITRATE_CONST },
  { GST_C2_RATE_CTRL_CBR_VFR,  C2Config::BITRATE_CONST_SKIP_ALLOWED },
  { GST_C2_RATE_CTRL_VBR_CFR,  C2Config::BITRATE_VARIABLE },
  { GST_C2_RATE_CTRL_VBR_VFR,  C2Config::BITRATE_VARIABLE_SKIP_ALLOWED },
  { GST_C2_RATE_CTRL_CQ,       C2Config::BITRATE_IGNORE },
};

// Map for the GST_C2_PARAM_INTRA_REFRESH parameter.
static const std::unordered_map<uint32_t, uint32_t> kIntraRefreshMap = {
  { GST_C2_INTRA_REFRESH_DISABLED,  C2Config::INTRA_REFRESH_DISABLED },
  { GST_C2_INTRA_REFRESH_ARBITRARY, C2Config::INTRA_REFRESH_ARBITRARY },
};

// Map for the GST_C2_ENTROPY_MODE parameter.
static const std::unordered_map<uint32_t, uint32_t> kEntropyMap = {
  { GST_C2_ENTROPY_CAVLC, ENTROPYMODE_CAVLC },
  { GST_C2_ENTROPY_CABAC, ENTROPYMODE_CABAC },
};

// Map for the GST_C2_LOOP_FILTER_MODE parameter.
static const std::unordered_map<uint32_t, uint32_t> kLoopFilterMap = {
  { GST_C2_LOOP_FILTER_ENABLE,                 Qc2AvcLoopFilterEnable },
  { GST_C2_LOOP_FILTER_DISABLE,                Qc2AvcLoopFilterDisable },
  { GST_C2_LOOP_FILTER_DISABLE_SLICE_BOUNDARY, Qc2AvcLoopFilterDisableSliceBoundary },
};

// Map for the GST_C2_PARAM_ROTATION parameter.
static const std::unordered_map<uint32_t, uint32_t> kRotationMap = {
  { GST_C2_ROTATE_NONE,   0 },
  { GST_C2_ROTATE_90_CW,  ROTATION_90 },
  { GST_C2_ROTATE_180,    ROTATION_180 },
  { GST_C2_ROTATE_90_CCW, ROTATION_270 },
};

// Map for the GST_C2_PARAM_PREPEND_HEADER_MODE parameter.
static const std::unordered_map<uint32_t, uint32_t> kPrependHeaderMap = {
  { GST_C2_PREPEND_HEADER_TO_NONE,     C2Config::PREPEND_HEADER_TO_NONE },
  { GST_C2_PREPEND_HEADER_ON_CHANGE,   C2Config::PREPEND_HEADER_ON_CHANGE },
  { GST_C2_PREPEND_HEADER_TO_ALL_SYNC, C2Config::PREPEND_HEADER_TO_ALL_SYNC },
};

C2Param::Index GstC2Utils::ParamIndex(uint32_t type) {

  return kParamIndexMap.at(type);
}

const char* GstC2Utils::ParamName(uint32_t type) {

  return kParamNameMap.at(type);
}

C2PixelFormat GstC2Utils::PixelFormat(GstVideoFormat format, bool isubwc) {

  if (format == GST_VIDEO_FORMAT_RGBA) {
    return isubwc ? C2PixelFormat::kRGBA_UBWC : C2PixelFormat::kRGBA;
  } else if (format == GST_VIDEO_FORMAT_NV12) {
    return isubwc ? C2PixelFormat::kNV12UBWC : C2PixelFormat::kNV12;
  } else if (format == GST_VIDEO_FORMAT_YV12) {
    return C2PixelFormat::kYV12;
  } else if (format == GST_VIDEO_FORMAT_P010_10LE) {
    return C2PixelFormat::kP010;
  } else if (format == GST_VIDEO_FORMAT_NV12_10LE32) {
    return isubwc ? C2PixelFormat::kTP10UBWC : C2PixelFormat::kUnknown;
  } else {
    GST_ERROR ("Unsupported format: %s!", gst_video_format_to_string (format));
  }

  return C2PixelFormat::kUnknown;
}

std::tuple<GstVideoFormat, bool> GstC2Utils::VideoFormat(C2PixelFormat format) {

  if (format == C2PixelFormat::kRGBA_UBWC) {
    return std::make_tuple(GST_VIDEO_FORMAT_RGBA, true);
  } else if (format == C2PixelFormat::kRGBA) {
    return std::make_tuple(GST_VIDEO_FORMAT_RGBA, false);
  } else if (format == C2PixelFormat::kNV12UBWC) {
    return std::make_tuple(GST_VIDEO_FORMAT_NV12, true);
  } else if (format == C2PixelFormat::kNV12) {
    return std::make_tuple(GST_VIDEO_FORMAT_NV12, false);
  } else if (format == C2PixelFormat::kYV12) {
    return std::make_tuple(GST_VIDEO_FORMAT_YV12, false);
  } else if (format == C2PixelFormat::kP010) {
    return std::make_tuple(GST_VIDEO_FORMAT_P010_10LE, false);
  } else if (format == C2PixelFormat::kTP10UBWC) {
    return std::make_tuple(GST_VIDEO_FORMAT_NV12_10LE32, true);
  } else {
    GST_ERROR ("Unsupported format: %u!", format);
  }

  return std::make_tuple(GST_VIDEO_FORMAT_UNKNOWN, false);
}

bool GstC2Utils::UnpackPayload(uint32_t type, void* payload,
                               std::unique_ptr<C2Param>& c2param) {

  switch (type) {
    case GST_C2_PARAM_IN_FORMAT: {
      C2StreamPixelFormatInfo::input pixformat;
      GstC2PixelInfo *pixinfo = reinterpret_cast<GstC2PixelInfo*>(payload);

      pixformat.value = static_cast<uint32_t>(
          GstC2Utils::PixelFormat(pixinfo->format, pixinfo->isubwc));
      c2param = C2Param::Copy(pixformat);
      break;
    }
    case GST_C2_PARAM_OUT_FORMAT: {
      C2StreamPixelFormatInfo::output pixformat;
      GstC2PixelInfo *pixinfo = reinterpret_cast<GstC2PixelInfo*>(payload);

      pixformat.value = static_cast<uint32_t>(
          GstC2Utils::PixelFormat(pixinfo->format, pixinfo->isubwc));
      c2param = C2Param::Copy(pixformat);
      break;
    }
    case GST_C2_PARAM_IN_RESOLUTION: {
      C2StreamPictureSizeInfo::input dimensions;
      dimensions.width = reinterpret_cast<GstC2Resolution*>(payload)->width;
      dimensions.height = reinterpret_cast<GstC2Resolution*>(payload)->height;
      c2param = C2Param::Copy(dimensions);
      break;
    }
    case GST_C2_PARAM_OUT_RESOLUTION: {
      C2StreamPictureSizeInfo::output dimensions;
      dimensions.width = reinterpret_cast<GstC2Resolution*>(payload)->width;
      dimensions.height = reinterpret_cast<GstC2Resolution*>(payload)->height;
      c2param = C2Param::Copy(dimensions);
      break;
    }
    case GST_C2_PARAM_IN_FRAMERATE: {
      C2StreamFrameRateInfo::input framerate;
      framerate.value = *(reinterpret_cast<gdouble*>(payload));
      c2param = C2Param::Copy(framerate);
      break;
    }
    case GST_C2_PARAM_OUT_FRAMERATE: {
      C2StreamFrameRateInfo::output framerate;
      framerate.value = *(reinterpret_cast<gdouble*>(payload));
      c2param = C2Param::Copy(framerate);
      break;
    }
    case GST_C2_PARAM_PROFILE_LEVEL: {
      C2StreamProfileLevelInfo::output plinfo;
      uint32_t profile = (*reinterpret_cast<guint32*>(payload)) & 0xFFFF;
      uint32_t level = ((*reinterpret_cast<guint32*>(payload)) >> 16) & 0xFFFF;

      if (profile != GST_C2_PROFILE_INVALID) {
        plinfo.profile = kProfileMap.at(profile);
      }
      if (level != GST_C2_LEVEL_INVALID) {
        plinfo.level = kLevelMap.at(level);
      }
      c2param = C2Param::Copy(plinfo);
      break;
    }
    case GST_C2_PARAM_RATE_CONTROL: {
      C2StreamBitrateModeTuning::output ratectrl;
      uint32_t mode = *(reinterpret_cast<GstC2RateControl*>(payload));

      ratectrl.value =
          static_cast<C2Config::bitrate_mode_t>(kRateCtrlMap.at(mode));
      c2param = C2Param::Copy(ratectrl);
      break;
    }
    case GST_C2_PARAM_BITRATE: {
      C2StreamBitrateInfo::output bitrate;
      bitrate.value = *(reinterpret_cast<guint32*>(payload));
      c2param = C2Param::Copy(bitrate);
      break;
    }
    case GST_C2_PARAM_GOP_CONFIG: {
      auto c2gop = C2StreamGopTuning::output::AllocUnique(2, 0u);
      GstC2Gop *gop = reinterpret_cast<GstC2Gop*>(payload);

      c2gop->m.values[0] = { P_FRAME, gop->n_pframes };
      c2gop->m.values[1] =
          { C2Config::picture_type_t(P_FRAME | B_FRAME), gop->n_bframes };

      c2param = C2Param::Copy(*c2gop);
      break;
    }
    case GST_C2_PARAM_KEY_FRAME_INTERVAL: {
      C2StreamSyncFrameIntervalTuning::output keyframe;
      keyframe.value = *(reinterpret_cast<int64_t*>(payload));
      c2param = C2Param::Copy(keyframe);
      break;
    }
    case GST_C2_PARAM_INTRA_REFRESH: {
      C2StreamIntraRefreshTuning::output irefresh;
      uint32_t mode = reinterpret_cast<GstC2IntraRefresh*>(payload)->mode;

      irefresh.mode =
          static_cast<C2Config::intra_refresh_mode_t>(kIntraRefreshMap.at(mode));
      irefresh.period = reinterpret_cast<GstC2IntraRefresh*>(payload)->period;
      c2param = C2Param::Copy(irefresh);
      break;
    }
    case GST_C2_PARAM_ENTROPY_MODE: {
      qc2::C2VideoEntropyMode::output entropy;
      uint32_t mode = *(reinterpret_cast<GstC2EntropyMode*>(payload));

      entropy.value = static_cast<qc2::EntropyMode>(kEntropyMap.at(mode));
      c2param = C2Param::Copy(entropy);
      break;
    }
    case GST_C2_PARAM_LOOP_FILTER_MODE: {
      qc2::C2VideoDeblockFilter::output filter;
      uint32_t mode = *(reinterpret_cast<GstC2LoopFilterMode*>(payload));

      filter.value = kLoopFilterMap.at(mode);
      c2param = C2Param::Copy(filter);
      break;
    }
    case GST_C2_PARAM_SLICE_MB: {
      qc2::C2VideoSliceSizeMBCount::output slice;
      slice.value = *(reinterpret_cast<guint32*>(payload));
      c2param = C2Param::Copy(slice);
      break;
    }
    case GST_C2_PARAM_SLICE_BYTES: {
      qc2::C2VideoSliceSizeBytes::output slice;
      slice.value = *(reinterpret_cast<guint32*>(payload));
      c2param = C2Param::Copy(slice);
      break;
    }
    case GST_C2_PARAM_NUM_LTR_FRAMES: {
      qc2::C2VideoLTRCountSetting::input ltr_frames;
      ltr_frames.count = *(reinterpret_cast<guint32*>(payload));
      c2param = C2Param::Copy(ltr_frames);
      break;
    }
    case GST_C2_PARAM_ROTATION: {
      qc2::C2VideoRotation::input rotation;
      uint32_t rotate = *(reinterpret_cast<GstC2VideoRotate*>(payload));

      rotation.angle = kRotationMap.at(rotate);
      c2param = C2Param::Copy(rotation);
      break;
    }
    case GST_C2_PARAM_TILE_LAYOUT: {
      C2StreamTileLayoutInfo::output c2layout;
      auto layout = reinterpret_cast<GstC2TileLayout*>(payload);

      c2layout.tile.width = layout->dims.width;
      c2layout.tile.height = layout->dims.height;
      c2layout.columnCount = layout->n_columns;
      c2layout.rowCount = layout->n_rows;
      c2layout.order = C2Config::SCAN_LEFT_TO_RIGHT_THEN_DOWN;

      c2param = C2Param::Copy(c2layout);
      break;
    }
    case GST_C2_PARAM_PREPEND_HEADER_MODE: {
      C2PrependHeaderModeSetting csdmode;
      uint32_t mode = *(reinterpret_cast<GstC2HeaderMode*>(payload));

      csdmode.value =
          static_cast<C2Config::prepend_header_mode_t>(kPrependHeaderMap.at(mode));
      c2param = C2Param::Copy(csdmode);
      break;
    }
    case GST_C2_PARAM_ENABLE_PICTURE_ORDER: {
      qc2::C2VideoPictureOrder::output porder;
      gboolean enable = *(reinterpret_cast<gboolean*>(payload));

      porder.enable = enable ? 1 : 0;
      c2param = C2Param::Copy(porder);
      break;
    }
    case GST_C2_PARAM_QP_INIT: {
      qc2::C2VideoInitQPSetting::output qpinit;
      qpinit.qpI = reinterpret_cast<GstC2QuantInit*>(payload)->i_frames;
      qpinit.qpIEnable = reinterpret_cast<GstC2QuantInit*>(payload)->i_frames_enable;
      qpinit.qpP = reinterpret_cast<GstC2QuantInit*>(payload)->p_frames;
      qpinit.qpPEnable = reinterpret_cast<GstC2QuantInit*>(payload)->p_frames_enable;
      qpinit.qpB = reinterpret_cast<GstC2QuantInit*>(payload)->b_frames;
      qpinit.qpBEnable = reinterpret_cast<GstC2QuantInit*>(payload)->b_frames_enable;
      c2param = C2Param::Copy(qpinit);
      break;
    }
    case GST_C2_PARAM_QP_RANGES: {
      GstC2QuantRanges* ranges = reinterpret_cast<GstC2QuantRanges*>(payload);

#if defined(CODEC2_CONFIG_VERSION_2_0)
      auto qp_ranges = C2StreamPictureQuantizationTuning::output::AllocUnique(3,0u);

      qp_ranges->m.values[0].type_ = I_FRAME;
      qp_ranges->m.values[0].min  = ranges->min_i_qp;
      qp_ranges->m.values[0].max  = ranges->max_i_qp;
      qp_ranges->m.values[1].type_ = P_FRAME;
      qp_ranges->m.values[1].min  = ranges->min_p_qp;
      qp_ranges->m.values[1].max  = ranges->max_p_qp;
      qp_ranges->m.values[2].type_ = B_FRAME;
      qp_ranges->m.values[2].min  = ranges->min_b_qp;
      qp_ranges->m.values[2].max  = ranges->max_b_qp;

      c2param = C2Param::Copy(*qp_ranges);
#else
      qc2::C2VideoQPRangeSetting::output qp_ranges;

      qp_ranges.miniqp = ranges->min_i_qp;
      qp_ranges.maxiqp = ranges->max_i_qp;
      qp_ranges.minpqp = ranges->min_p_qp;
      qp_ranges.maxpqp = ranges->max_p_qp;
      qp_ranges.minbqp = ranges->min_b_qp;
      qp_ranges.maxbqp = ranges->max_b_qp;

      c2param = C2Param::Copy(qp_ranges);
#endif // CODEC2_CONFIG_VERSION_2_0
      break;
    }
    case GST_C2_PARAM_ROI_ENCODE: {
#if defined(CODEC2_CONFIG_VERSION_2_0)
      qc2::QC2VideoROIRegionInfo::input region;
#else
      qc2::QC2VideoROIRegionInfo::output region;
#endif // CODEC2_CONFIG_VERSION_2_0

      auto rects = reinterpret_cast<GstC2QuantRegions*>(payload)->rects;
      uint32_t n_rects = reinterpret_cast<GstC2QuantRegions*>(payload)->n_rects;
      std::stringstream ss;

      size_t size = sizeof (region.rectPayload);
      size_t extsize = sizeof (region.rectPayloadExt);

      for (uint32_t idx = 0; idx < n_rects; idx++) {
        ss << rects[idx].y << "," // Top
           << rects[idx].x << "-" // Left
           << (rects[idx].y + rects[idx].h - 1) << "," // Bottom
           << (rects[idx].x + rects[idx].w - 1) << "=" // Right
           << rects[idx].qp << ";"; // QP Delta

        size_t len = strlen (region.rectPayload);
        size_t extlen = strlen (region.rectPayloadExt);

        if ((len + ss.tellp()) < size)
          ss.get((region.rectPayload + len), ss.tellp());
        else if ((extlen + ss.tellp()) < extsize)
          ss.get((region.rectPayloadExt + extlen), ss.tellp());
      }

      region.type_[0] = 'r';
      region.type_[1] = 'e';
      region.type_[2] = 'c';
      region.type_[3] = 't';
      region.type_[4] = '\0';

      region.timestampUs = reinterpret_cast<GstC2QuantRegions*>(payload)->timestamp;
      c2param = C2Param::Copy(region);
      break;
    }
    case GST_C2_PARAM_TRIGGER_SYNC_FRAME: {
      C2StreamRequestSyncFrameTuning::output syncframe;
      gboolean enable = *(reinterpret_cast<gboolean*>(payload));

      syncframe.value = enable ? 1 : 0;
      c2param = C2Param::Copy(syncframe);
      break;
    }
    default:
      GST_ERROR ("Unsupported parameter: %u!", type);
      return FALSE;
  }

  return TRUE;
}

bool GstC2Utils::PackPayload(uint32_t type, std::unique_ptr<C2Param>& c2param,
                             void* payload) {

  switch (type) {
    case GST_C2_PARAM_IN_FORMAT: {
      auto pixformat =
          reinterpret_cast<C2StreamPixelFormatInfo::input*>(c2param.get());
      std::tuple<GstVideoFormat, bool> tuple =
          GstC2Utils::VideoFormat(static_cast<C2PixelFormat>(pixformat->value));

      reinterpret_cast<GstC2PixelInfo*>(payload)->format =
          std::get<GstVideoFormat>(tuple);
      reinterpret_cast<GstC2PixelInfo*>(payload)->isubwc =
          std::get<bool>(tuple) ? TRUE : FALSE;
      break;
    }
    case GST_C2_PARAM_OUT_FORMAT: {
      auto pixformat =
          reinterpret_cast<C2StreamPixelFormatInfo::output*>(c2param.get());
      std::tuple<GstVideoFormat, bool> tuple =
          GstC2Utils::VideoFormat(static_cast<C2PixelFormat>(pixformat->value));

      reinterpret_cast<GstC2PixelInfo*>(payload)->format =
          std::get<GstVideoFormat>(tuple);
      reinterpret_cast<GstC2PixelInfo*>(payload)->isubwc =
          std::get<bool>(tuple) ? TRUE : FALSE;
      break;
    }
    case GST_C2_PARAM_IN_RESOLUTION: {
      auto dims =
          reinterpret_cast<C2StreamPictureSizeInfo::input*>(c2param.get());

      reinterpret_cast<GstC2Resolution*>(payload)->width = dims->width;
      reinterpret_cast<GstC2Resolution*>(payload)->height = dims->height;
      break;
    }
    case GST_C2_PARAM_OUT_RESOLUTION: {
      auto dims =
          reinterpret_cast<C2StreamPictureSizeInfo::output*>(c2param.get());

      reinterpret_cast<GstC2Resolution*>(payload)->width = dims->width;
      reinterpret_cast<GstC2Resolution*>(payload)->height = dims->height;
      break;
    }
    case GST_C2_PARAM_IN_FRAMERATE: {
      auto framerate =
          reinterpret_cast<C2StreamFrameRateInfo::input*>(c2param.get());
      *(reinterpret_cast<float*>(payload)) = framerate->value;
      break;
    }
    case GST_C2_PARAM_OUT_FRAMERATE: {
      auto framerate =
          reinterpret_cast<C2StreamFrameRateInfo::output*>(c2param.get());
      *(reinterpret_cast<float*>(payload)) = framerate->value;
      break;
    }
    case GST_C2_PARAM_PROFILE_LEVEL: {
      auto plinfo =
          reinterpret_cast<C2StreamProfileLevelInfo::output*>(c2param.get());

      auto p_result = std::find_if(kProfileMap.begin(), kProfileMap.end(),
          [&](const auto& m) { return m.second == plinfo->profile; });
      uint32_t profile = (p_result != kProfileMap.end()) ?
          p_result->first : GST_C2_PROFILE_INVALID;

      auto l_result = std::find_if(kLevelMap.begin(), kLevelMap.end(),
          [&](const auto& m) { return m.second == plinfo->level; });
      uint32_t level = (l_result != kLevelMap.end()) ?
          l_result->first : GST_C2_LEVEL_INVALID;

      *(reinterpret_cast<guint32*>(payload)) = profile + (level << 16);
      break;
    }
    case GST_C2_PARAM_RATE_CONTROL: {
      auto ratectrl =
          reinterpret_cast<C2StreamBitrateModeTuning::output*>(c2param.get());

      auto result = std::find_if(kRateCtrlMap.begin(), kRateCtrlMap.end(),
          [&](const auto& m) { return m.second == ratectrl->value; });

      *(reinterpret_cast<GstC2RateControl*>(payload)) =
          static_cast<GstC2RateControl>(result->first);
      break;
    }
    case GST_C2_PARAM_BITRATE: {
      auto bitrate =
          reinterpret_cast<C2StreamBitrateInfo::output*>(c2param.get());
      *(reinterpret_cast<guint32*>(payload)) = bitrate->value;
      break;
    }
    case GST_C2_PARAM_GOP_CONFIG: {
      auto gop = reinterpret_cast<C2StreamGopTuning::output*>(c2param.get());

      reinterpret_cast<GstC2Gop*>(payload)->n_pframes = gop->m.values[0].count;
      reinterpret_cast<GstC2Gop*>(payload)->n_bframes = gop->m.values[1].count;
      break;
    }
    case GST_C2_PARAM_KEY_FRAME_INTERVAL: {
      auto keyframe =
          reinterpret_cast<C2StreamSyncFrameIntervalTuning::output*>(c2param.get());
      *(reinterpret_cast<int64_t*>(payload)) = keyframe->value;
      break;
    }
    case GST_C2_PARAM_INTRA_REFRESH: {
      auto irefresh =
          reinterpret_cast<C2StreamIntraRefreshTuning::output*>(c2param.get());

      auto result = std::find_if(kIntraRefreshMap.begin(), kIntraRefreshMap.end(),
          [&](const auto& m) { return m.second == irefresh->mode; });

      reinterpret_cast<GstC2IntraRefresh*>(payload)->mode =
          static_cast<GstC2IRefreshMode>(result->first);
      reinterpret_cast<GstC2IntraRefresh*>(payload)->period = irefresh->period;
      break;
    }
    case GST_C2_PARAM_ENTROPY_MODE: {
      auto entropy =
          reinterpret_cast<qc2::C2VideoEntropyMode::output*>(c2param.get());

      auto result = std::find_if(kEntropyMap.begin(), kEntropyMap.end(),
          [&](const auto& m) { return m.second == entropy->value; });

      *(reinterpret_cast<GstC2EntropyMode*>(payload)) =
          static_cast<GstC2EntropyMode>(result->first);
      break;
    }
    case GST_C2_PARAM_LOOP_FILTER_MODE: {
      auto filter =
          reinterpret_cast<qc2::C2VideoDeblockFilter::output*>(c2param.get());

      auto result = std::find_if(kLoopFilterMap.begin(), kLoopFilterMap.end(),
          [&](const auto& m) { return m.second == filter->value; });

      *(reinterpret_cast<GstC2LoopFilterMode*>(payload)) =
          static_cast<GstC2LoopFilterMode>(result->first);
      break;
    }
    case GST_C2_PARAM_SLICE_MB: {
      auto slice =
          reinterpret_cast<qc2::C2VideoSliceSizeMBCount::output*>(c2param.get());
      *(reinterpret_cast<guint32*>(payload)) = slice->value;
      break;
    }
    case GST_C2_PARAM_SLICE_BYTES: {
      auto slice =
          reinterpret_cast<qc2::C2VideoSliceSizeBytes::output*>(c2param.get());
      *(reinterpret_cast<guint32*>(payload)) = slice->value;
      break;
    }
    case GST_C2_PARAM_NUM_LTR_FRAMES: {
      auto ltr_frames =
          reinterpret_cast<qc2::C2VideoLTRCountSetting::input*>(c2param.get());
      *(reinterpret_cast<guint32*>(payload)) = ltr_frames->count;
      break;
    }
    case GST_C2_PARAM_ROTATION: {
      auto rotation =
          reinterpret_cast<qc2::C2VideoRotation::input*>(c2param.get());

      auto result = std::find_if(kRotationMap.begin(), kRotationMap.end(),
          [&](const auto& m) { return m.second == rotation->angle; });

      *(reinterpret_cast<GstC2VideoRotate*>(payload)) =
          static_cast<GstC2VideoRotate>(result->first);
      break;
    }
    case GST_C2_PARAM_TILE_LAYOUT: {
      auto c2layout =
          reinterpret_cast<C2StreamTileLayoutInfo::output*>(c2param.get());

      reinterpret_cast<GstC2TileLayout*>(payload)->dims.width =c2layout->tile.width;
      reinterpret_cast<GstC2TileLayout*>(payload)->dims.height = c2layout->tile.height;
      reinterpret_cast<GstC2TileLayout*>(payload)->n_columns = c2layout->columnCount;
      reinterpret_cast<GstC2TileLayout*>(payload)->n_rows = c2layout->rowCount;
      break;
    }
    case GST_C2_PARAM_PREPEND_HEADER_MODE: {
      auto csdmode =
          reinterpret_cast<C2PrependHeaderModeSetting*>(c2param.get());

      auto result = std::find_if(kPrependHeaderMap.begin(), kPrependHeaderMap.end(),
          [&](const auto& m) { return m.second == csdmode->value; });

      *(reinterpret_cast<GstC2HeaderMode*>(payload)) =
          static_cast<GstC2HeaderMode>(result->first);
      break;
    }
    case GST_C2_PARAM_ENABLE_PICTURE_ORDER: {
      auto porder =
          reinterpret_cast<qc2::C2VideoPictureOrder::output*>(c2param.get());
      *(reinterpret_cast<gboolean*>(payload)) = porder->enable ? TRUE : FALSE;
      break;
    }
    case GST_C2_PARAM_QP_INIT: {
      auto qpinit =
          reinterpret_cast<qc2::C2VideoInitQPSetting::output*>(c2param.get());

      reinterpret_cast<GstC2QuantInit*>(payload)->i_frames = qpinit->qpI;
      reinterpret_cast<GstC2QuantInit*>(payload)->i_frames_enable = qpinit->qpIEnable;
      reinterpret_cast<GstC2QuantInit*>(payload)->p_frames = qpinit->qpP;
      reinterpret_cast<GstC2QuantInit*>(payload)->p_frames_enable = qpinit->qpPEnable;
      reinterpret_cast<GstC2QuantInit*>(payload)->b_frames = qpinit->qpB;
      reinterpret_cast<GstC2QuantInit*>(payload)->b_frames_enable = qpinit->qpBEnable;
      break;
    }
    case GST_C2_PARAM_QP_RANGES: {
      GstC2QuantRanges* ranges = reinterpret_cast<GstC2QuantRanges*>(payload);

#if defined(CODEC2_CONFIG_VERSION_2_0)
      auto qp_ranges =
          reinterpret_cast<C2StreamPictureQuantizationTuning::output*>(c2param.get());

      ranges->min_i_qp = qp_ranges->m.values[0].min;
      ranges->max_i_qp = qp_ranges->m.values[0].max;
      ranges->min_p_qp = qp_ranges->m.values[1].min;
      ranges->max_p_qp = qp_ranges->m.values[1].max;
      ranges->min_b_qp = qp_ranges->m.values[2].min;
      ranges->max_b_qp = qp_ranges->m.values[2].max;
#else
      auto qp_ranges =
          reinterpret_cast<qc2::C2VideoQPRangeSetting::output*>(c2param.get());

      ranges->min_i_qp = qp_ranges->miniqp;
      ranges->max_i_qp = qp_ranges->maxiqp;
      ranges->min_p_qp = qp_ranges->minpqp;
      ranges->max_p_qp = qp_ranges->maxpqp;
      ranges->min_b_qp = qp_ranges->minbqp;
      ranges->max_b_qp = qp_ranges->maxbqp;
#endif // CODEC2_CONFIG_VERSION_2_0
      break;
    }
    case GST_C2_PARAM_ROI_ENCODE: {
      /// TODO
      break;
    }
    case GST_C2_PARAM_TRIGGER_SYNC_FRAME: {
      auto syncframe =
          reinterpret_cast<C2StreamRequestSyncFrameTuning::output*>(c2param.get());
      *(reinterpret_cast<gboolean*>(payload)) = syncframe->value ? TRUE : FALSE;
      break;
    }
    default:
      GST_ERROR ("Unsupported parameter: %u!", type);
      return FALSE;
  }

  return TRUE;
}

bool GstC2Utils::ImportHandleInfo(GstBuffer* buffer,
                                  ::android::C2HandleGBM* handle) {

  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);
  uint32_t size = gst_buffer_get_size (buffer);
  int32_t fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

  gboolean isubwc = GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_UBWC);
  C2PixelFormat format = GstC2Utils::PixelFormat(vmeta->format, isubwc);

  uint32_t width = vmeta->width;
  uint32_t height = vmeta->height;
  uint32_t stride = vmeta->stride[0];

  switch (format) {
    case C2PixelFormat::kNV12:
      handle->mInts.format = GBM_FORMAT_NV12;
      handle->mInts.slice_height =
          MMM_COLOR_FMT_Y_SCANLINES(MMM_COLOR_FMT_NV12, height);
      break;
    case C2PixelFormat::kNV12UBWC:
      handle->mInts.format = GBM_FORMAT_NV12;
      handle->mInts.usage_lo |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
      handle->mInts.slice_height =
          MMM_COLOR_FMT_Y_SCANLINES(MMM_COLOR_FMT_NV12_UBWC, height);
      break;
    case C2PixelFormat::kP010:
      handle->mInts.format = GBM_FORMAT_YCbCr_420_P010_VENUS;
      handle->mInts.slice_height =
          MMM_COLOR_FMT_Y_SCANLINES(MMM_COLOR_FMT_P010, height);
      break;
    case C2PixelFormat::kTP10UBWC:
      handle->mInts.format = GBM_FORMAT_YCbCr_420_TP10_UBWC;
      handle->mInts.usage_lo |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
      handle->mInts.slice_height =
          MMM_COLOR_FMT_Y_SCANLINES(MMM_COLOR_FMT_NV12_BPP10_UBWC, height);
      break;
    default:
      GST_ERROR ("Unsupported format: %d !", format);
      return false;
  }

  handle->version = ::android::C2HandleGBM::VERSION;
  handle->numFds = ::android::C2HandleGBM::NUM_FDS;
  handle->numInts = ::android::C2HandleGBM::NUM_INTS;

  handle->mFds.buffer_fd = fd;
  handle->mFds.meta_buffer_fd = -1;

  handle->mInts.width = width;
  handle->mInts.height = height;
  handle->mInts.stride = stride;

  handle->mInts.size = size;
  handle->mInts.id = fd;

  return true;
}

bool GstC2Utils::ExtractHandleInfo(GstBuffer* buffer,
                                   const ::android::C2HandleGBM* handle) {

  guint width = 0, height = 0, n_planes = 0;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  gint strides[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gsize offsets[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };

  uint32_t stride = handle->mInts.stride;
  uint32_t scanline = handle->mInts.slice_height;
  uint32_t gbm_format = handle->mInts.format;

  width = handle->mInts.width;
  height = handle->mInts.height;

  switch (gbm_format) {
    case GBM_FORMAT_NV12:
    case GBM_FORMAT_YCbCr_420_SP_VENUS:
    case GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    {
      format = GST_VIDEO_FORMAT_NV12;
      n_planes = 2;

      strides[0] = strides[1] = stride;
      offsets[1] = (stride * scanline);

      if (gbm_format == GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC) {
        auto metastride =
            MMM_COLOR_FMT_Y_META_STRIDE(MMM_COLOR_FMT_NV12_UBWC, width);
        auto metascanline =
            MMM_COLOR_FMT_Y_META_SCANLINES(MMM_COLOR_FMT_NV12_UBWC, height);
        offsets[1] += MMM_COLOR_FMT_ALIGN(metastride * metascanline, 4096);
      }
      break;
    }
    case GBM_FORMAT_YCbCr_420_P010_VENUS:
    {
      format = GST_VIDEO_FORMAT_P010_10LE;
      n_planes = 2;

      strides[0] = strides[1] = stride;
      offsets[1] = (stride * scanline);
      break;
    }
    case GBM_FORMAT_YCbCr_420_TP10_UBWC:
    {
      format = GST_VIDEO_FORMAT_NV12_10LE32;
      n_planes = 2;

      strides[0] = strides[1] = stride;
      offsets[1] = (stride * scanline);

      auto metastride =
          MMM_COLOR_FMT_Y_META_STRIDE(MMM_COLOR_FMT_NV12_BPP10_UBWC, width);
      auto metascanline =
          MMM_COLOR_FMT_Y_META_SCANLINES(MMM_COLOR_FMT_NV12_BPP10_UBWC, height);
      offsets[1] += MMM_COLOR_FMT_ALIGN(metastride * metascanline, 4096);
      break;
    }
    default:
      GST_ERROR ("Unsupported GBM format: '%x'!", gbm_format);
      return false;
  }

  // Fill video metadata needed for graphic buffers.
  gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
      format, width, height, n_planes, offsets, strides);

  return true;
}

std::shared_ptr<C2Buffer> GstC2Utils::CreateBuffer(
    GstBuffer* buffer, std::shared_ptr<C2GraphicBlock>& block) {

  C2GraphicView view = block->map().get();
  if (view.error() != C2_OK) {
    GST_ERROR ("Failed to map C2 graphic block, error %d !", view.error());
    return nullptr;
  }

  GstMapInfo map;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR ("Failed to map GST buffer!");
    return nullptr;
  }

  auto data = view.data();
  memcpy (static_cast<void*>(data[0]), static_cast<void*>(map.data), map.size);

  gst_buffer_unmap (buffer, &map);

  auto c2buffer = C2Buffer::CreateGraphicBuffer(
      block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
  if (!c2buffer) {
    GST_ERROR ("Failed to create graphic C2 buffer!");
    return nullptr;
  }

  return c2buffer;
}

std::shared_ptr<C2Buffer> GstC2Utils::CreateBuffer(
    GstBuffer* buffer, std::shared_ptr<C2LinearBlock>& block) {

  C2WriteView view = block->map().get();
  if (view.error() != C2_OK) {
    GST_ERROR ("Failed to map C2 linear block, error %d !", view.error());
    return nullptr;
  }

  GstMapInfo map;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR ("Failed to map GST buffer!");
    return nullptr;
  }

  memcpy (static_cast<void*>(view.base()), static_cast<void*>(map.data), map.size);
  block->mSize = map.size;

  gst_buffer_unmap (buffer, &map);

  auto c2buffer = C2Buffer::CreateLinearBuffer(block->share(block->offset(),
                                               block->size(), ::C2Fence()));
  if (!c2buffer) {
    GST_ERROR ("Failed to create linear C2 buffer!");
    return nullptr;
  }

  return c2buffer;
}

// TODO Workaround due to issues in codec2 implementation, REMOVE IT.
class C2VencBuffWrapper : public C2GraphicAllocation {
public:
  C2VencBuffWrapper(uint32_t width, uint32_t height,
                    C2Allocator::id_t allocator_id,
                    android::C2HandleGBM * handle)
      : C2GraphicAllocation(width, height),
        base_(nullptr), mapsize_(0),
        allocator_id_(allocator_id),
        handle_(handle) {}
  ~C2VencBuffWrapper() { delete handle_; }

  c2_status_t map(C2Rect rect, C2MemoryUsage usage, C2Fence * fence,
                  C2PlanarLayout * layout, uint8_t ** addr) override {
    return C2_OK;
  }
  c2_status_t unmap(uint8_t ** addr, C2Rect rect, C2Fence * fence) override {
    return C2_OK;
  }
  const C2Handle *handle() const override {
    return reinterpret_cast<const C2Handle*>(handle_);
  }
  id_t getAllocatorId() const override {
    return allocator_id_;
  }
  bool equals(const std::shared_ptr<const C2GraphicAllocation> &other) const override {
    return other && other->handle() == handle();
  }

private:
  android::C2HandleGBM *handle_;
  void                 *base_;
  size_t               mapsize_;
  struct gbm_bo        *bo_;
  C2Allocator::id_t    allocator_id_;
};

std::shared_ptr<C2Buffer> GstC2Utils::ImportBuffer(GstBuffer* buffer) {

  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);
  g_return_val_if_fail (vmeta != NULL, nullptr);

  ::android::C2HandleGBM *handle = new android::C2HandleGBM();

  if (!GstC2Utils::ImportHandleInfo(buffer, handle)) {
    GST_ERROR ("Failed to import handle info !");
    delete handle;

    return nullptr;
  }

  std::shared_ptr<C2GraphicAllocation> allocation =
      std::make_shared<C2VencBuffWrapper>(vmeta->width, vmeta->height,
          android::C2PlatformAllocatorStore::DEFAULT_GRAPHIC, handle);

  std::shared_ptr<C2GraphicBlock> block =
      _C2BlockFactory::CreateGraphicBlock(allocation);
  if (!block) {
    GST_ERROR ("Failed to create graphic block!");
    return nullptr;
  }

  auto c2buffer = C2Buffer::CreateGraphicBuffer(
      block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
  if (!c2buffer) {
    GST_ERROR ("Failed to create graphic C2 buffer!");
    return nullptr;
  }

  return c2buffer;
}
