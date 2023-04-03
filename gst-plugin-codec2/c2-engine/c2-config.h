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

#ifndef __GST_C2_CONFIG_H__
#define __GST_C2_CONFIG_H__

#include <gst/gst.h>

#define CONFIG_FUNCTION_KEY_PIXELFORMAT "pixelformat"
#define CONFIG_FUNCTION_KEY_RESOLUTION "resolution"
#define CONFIG_FUNCTION_KEY_BITRATE "bitrate"
#define CONFIG_FUNCTION_KEY_FRAMERATE "framerate"
#define CONFIG_FUNCTION_KEY_INTERLACE "interlace"
#define CONFIG_FUNCTION_KEY_MIRROR "mirror"
#define CONFIG_FUNCTION_KEY_ROTATION "rotation"
#define CONFIG_FUNCTION_KEY_RATECONTROL "ratecontrol"
#define CONFIG_FUNCTION_KEY_SYNC_FRAME_INT "syncframeint"
#define CONFIG_FUNCTION_KEY_REQUEST_SYNC_FRAME "requestsyncframe"
#define CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY "dec_low_latency"
#define CONFIG_FUNCTION_KEY_INTRAREFRESH "intra_refresh"
#define CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE "output_picture_order_mode"
#define CONFIG_FUNCTION_KEY_ROI_ENCODING "roi_encoding"
#define CONFIG_FUNCTION_KEY_DOWNSCALE "downscale"
#define CONFIG_FUNCTION_KEY_ENC_CSC "enc_colorspace_conversion"
#define CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO "colorspace_color_aspects"
#define CONFIG_FUNCTION_KEY_SLICE_MODE "slice_mode"
#define CONFIG_FUNCTION_KEY_BLUR_MODE "blur_mode"
#define CONFIG_FUNCTION_KEY_BLUR_RESOLUTION "blur_resolution"
#define CONFIG_FUNCTION_KEY_QP_RANGES "qp_ranges"
#define CONFIG_FUNCTION_KEY_ENTROPY_MODE "entropy_mode"
#define CONFIG_FUNCTION_KEY_LOOP_FILTER_MODE "loop_filter_mode"
#define CONFIG_FUNCTION_KEY_QP_INIT "qp_init"
#define CONFIG_FUNCTION_KEY_NUM_LTR_FRAMES "num_ltr_frames"
#define CONFIG_FUNCTION_KEY_PROFILE_LEVEL "profile_level"
#define CONFIG_FUNCTION_KEY_ROTATE "rotate"
#define CONFIG_FUNCTION_KEY_BLOCK_POOL "block_pool"

typedef struct _GstC2ConfigParams GstC2ConfigParams;

typedef enum {
  GST_C2_INTERLACE_MODE_PROGRESSIVE = 0,
  GST_C2_INTERLACE_MODE_INTERLEAVED_TOP_FIRST,
  GST_C2_INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST,
  GST_C2_INTERLACE_MODE_FIELD_TOP_FIRST,
  GST_C2_INTERLACE_MODE_FIELD_BOTTOM_FIRST,
} GstC2InterlaceMode;

typedef enum {
  GST_C2_PIXEL_FORMAT_NV12_LINEAR = 0,
  GST_C2_PIXEL_FORMAT_NV12_UBWC,
  GST_C2_PIXEL_FORMAT_RGBA_8888,
  GST_C2_PIXEL_FORMAT_YV12,
  GST_C2_PIXEL_FORMAT_P010,
  GST_C2_PIXEL_FORMAT_TP10_UBWC
} GstC2PixelFormat;

enum {
  // RGB-Alpha 8 bit per channel
  C2_PIXEL_FORMAT_RGBA8888 = 1,
  // RGBA 8 bit compressed
  C2_PIXEL_FORMAT_RGBA8888_UBWC = 0xC2000000,
  // NV12 EXT with 128 width and height alignment
  C2_PIXEL_FORMAT_VENUS_NV12 = 0x7FA30C04,
  // NV12 EXT with UBWC compression
  C2_PIXEL_FORMAT_VENUS_NV12_UBWC = 0x7FA30C06,
  // 10-bit Tightly-packed and compressed YUV
  C2_PIXEL_FORMAT_VENUS_TP10 = 0x7FA30C09,
  // Venus 10-bit YUV 4:2:0 Planar format
  C2_PIXEL_FORMAT_VENUS_P010 = 0x7FA30C0A,
  ///< canonical YVU 4:2:0 Planar (YV12)
  C2_PIXEL_FORMAT_YV12 = 842094169,
};

enum {
  GST_C2_OUTPUT_PICTURE_ORDER_DEFAULT = 0,
  GST_C2_OUTPUT_PICTURE_ORDER_DISPLAY,
  GST_C2_OUTPUT_PICTURE_ORDER_DECODER,
} GstC2OutputPictureOrder;

typedef enum {
  GST_C2_MIRROR_NONE = 0,
  GST_C2_MIRROR_VERTICAL,
  GST_C2_MIRROR_HORIZONTAL,
  GST_C2_MIRROR_BOTH,
} GstC2Mirror;

typedef enum {
  GST_C2_RATE_CTRLMODE_OFF = 0,
  GST_C2_RATE_CTRLMODE_CONST,
  GST_C2_RATE_CTRLMODE_CBR_VFR,
  GST_C2_RATE_CTRLMODE_VBR_CFR,
  GST_C2_RATE_CTRLMODE_VBR_VFR,
  GST_C2_RATE_CTRLMODE_CQ,
  GST_C2_RATE_CTRLMODE_UNSET = 0xFFFF
} GstC2ControlRate;

typedef enum : unsigned int {
  GST_C2_SLICE_MODE_MB,
  GST_C2_SLICE_MODE_BYTES,
  GST_C2_SLICE_MODE_DEFAULT = 0xFFFFFFFF,
} GstC2SliceMode;

typedef enum : unsigned int {
  GST_C2_BLUR_MODE_AUTO = 0,
  GST_C2_BLUR_MODE_MANUAL,
  GST_C2_BLUR_MODE_DISABLE,
} GstC2BlurMode;

typedef enum : unsigned int {
  GST_C2_ENTROPY_MODE_CAVLC,
  GST_C2_ENTROPY_MODE_CABAC,
  GST_C2_ENTROPY_MODE_DEFAULT = 0xFFFFFFFF,
} GstC2EntropyMode;

typedef enum : unsigned int {
  GST_C2_LOOP_FILTER_ENABLE,
  GST_C2_LOOP_FILTER_DISABLE,
  GST_C2_LOOP_FILTER_DISABLE_SLICE_BOUNDARY,
  GST_C2_LOOP_FILTER_DEFAULT = 0xFFFFFFFF,
} GstC2LoopFilterMode;

typedef enum {
  GST_C2_ROTATE_NONE,
  GST_C2_ROTATE_90_CW,
  GST_C2_ROTATE_90_CCW,
  GST_C2_ROTATE_180,
} GstC2Rotate;

typedef enum {
  GST_C2_COLOR_PRIMARIES_UNSPECIFIED,
  GST_C2_COLOR_PRIMARIES_BT709,
  GST_C2_COLOR_PRIMARIES_BT470_M,
  GST_C2_COLOR_PRIMARIES_BT601_625,
  GST_C2_COLOR_PRIMARIES_BT601_525,
  GST_C2_COLOR_PRIMARIES_GENERIC_FILM,
  GST_C2_COLOR_PRIMARIES_BT2020,
  GST_C2_COLOR_PRIMARIES_RP431,
  GST_C2_COLOR_PRIMARIES_EG432,
  GST_C2_COLOR_PRIMARIES_EBU3213,
} GstC2ColorPrimaries;

typedef enum {
  GST_C2_COLOR_TRANSFER_UNSPECIFIED,
  GST_C2_COLOR_TRANSFER_LINEAR,
  GST_C2_COLOR_TRANSFER_SRGB,
  GST_C2_COLOR_TRANSFER_170M,
  GST_C2_COLOR_TRANSFER_GAMMA22,
  GST_C2_COLOR_TRANSFER_GAMMA28,
  GST_C2_COLOR_TRANSFER_ST2084,
  GST_C2_COLOR_TRANSFER_HLG,
  GST_C2_COLOR_TRANSFER_240M,
  GST_C2_COLOR_TRANSFER_XVYCC,
  GST_C2_COLOR_TRANSFER_BT1361,
  GST_C2_COLOR_TRANSFER_ST428,
} GstC2ColorTransfer;

typedef enum {
  GST_C2_COLOR_MATRIX_UNSPECIFIED,
  GST_C2_COLOR_MATRIX_BT709,
  GST_C2_COLOR_MATRIX_FCC47_73_682,
  GST_C2_COLOR_MATRIX_BT601,
  GST_C2_COLOR_MATRIX_240M,
  GST_C2_COLOR_MATRIX_BT2020,
  GST_C2_COLOR_MATRIX_BT2020_CONSTANT,
} GstC2ColorMatrix;

typedef enum {
  GST_C2_COLOR_RANGE_UNSPECIFIED,
  GST_C2_COLOR_RANGE_FULL,
  GST_C2_COLOR_RANGE_LIMITED,
} GstC2ColorRange;

typedef enum {
  GST_C2_INTRA_REFRESH_MODE_DISABLE,
  GST_C2_INTRA_REFRESH_MODE_ARBITRARY,
  GST_C2_INTRA_REFRESH_MODE_DEFAULT = 0xFFFFFFFF,
} GstC2IRefreshMode;

typedef enum {
  GST_C2_AVC_PROFILE_BASELINE = 0,
  GST_C2_AVC_PROFILE_CONSTRAINT_BASELINE,
  GST_C2_AVC_PROFILE_CONSTRAINT_HIGH,
  GST_C2_AVC_PROFILE_HIGH,
  GST_C2_AVC_PROFILE_MAIN,

  GST_C2_HEVC_PROFILE_MAIN,
  GST_C2_HEVC_PROFILE_MAIN10,
  GST_C2_HEVC_PROFILE_MAIN_STILL_PIC,

  GST_C2_VIDEO_PROFILE_MAX = 0x7FFFFFFF,
} GstC2VideoProfile;

typedef enum {
  GST_C2_AVC_LEVEL_1 = 0,
  GST_C2_AVC_LEVEL_1b,
  GST_C2_AVC_LEVEL_11,
  GST_C2_AVC_LEVEL_12,
  GST_C2_AVC_LEVEL_13,
  GST_C2_AVC_LEVEL_2,
  GST_C2_AVC_LEVEL_21,
  GST_C2_AVC_LEVEL_22,
  GST_C2_AVC_LEVEL_3,
  GST_C2_AVC_LEVEL_31,
  GST_C2_AVC_LEVEL_32,
  GST_C2_AVC_LEVEL_4,
  GST_C2_AVC_LEVEL_41,
  GST_C2_AVC_LEVEL_42,
  GST_C2_AVC_LEVEL_5,
  GST_C2_AVC_LEVEL_51,
  GST_C2_AVC_LEVEL_52,
  GST_C2_AVC_LEVEL_6,
  GST_C2_AVC_LEVEL_61,
  GST_C2_AVC_LEVEL_62,

  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL1,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL2,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL21,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL3,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL31,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL4,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL41,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL5,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL51,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL52,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL6,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL61,
  GST_C2_HEVC_LEVEL_MAIN_TIER_LEVEL62,

  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL1,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL2,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL21,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL3,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL31,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL4,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL41,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL5,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL51,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL52,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL6,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL61,
  GST_C2_HEVC_LEVEL_HIGH_TIER_LEVEL62,

  GST_C2_VIDEO_LEVEL_MAX = 0x7FFFFFFF,
} GstC2VideoLevel;

struct _GstC2ConfigParams {
  const char *config_name;
  gboolean is_input;
  union {
    guint32 u32;
    guint64 u64;
    gint32 i32;
    gint64 i64;
    gfloat fl;
    gboolean bl;
  } val;

  struct {
    guint32 width;
    guint32 height;
  } resolution;

  struct {
    gint64 timestamp;
    gchar *payload;
    gchar *payload_ext;
  } roi;

  struct {
    guint32 miniqp;
    guint32 maxiqp;
    guint32 minpqp;
    guint32 maxpqp;
    guint32 minbqp;
    guint32 maxbqp;
  } qp_ranges;

  struct {
    gboolean quant_i_frames_enable;
    guint32 quant_i_frames;
    gboolean quant_p_frames_enable;
    guint32 quant_p_frames;
    gboolean quant_b_frames_enable;
    guint32 quant_b_frames;
  } qp_init;

  GstC2PixelFormat pixel_fmt;
  GstC2InterlaceMode interlace_mode;
  GstC2Mirror GstC2Mirrorype;
  GstC2ControlRate rc_mode;
  GstC2SliceMode slice_mode;
  GstC2BlurMode blur_mode;
  GstC2EntropyMode entropy_mode;
  GstC2LoopFilterMode loop_filter_mode;
  GstC2VideoProfile profile;
  GstC2VideoLevel level;
  GstC2Rotate rotate;

  struct {
    GstC2IRefreshMode type;
    float intra_refresh_mbs;
  } ir_mode;
  guint output_picture_order_mode;
  gboolean low_latency_mode;
  gboolean color_space_conversion;
  struct {
    GstC2ColorPrimaries primaries;
    GstC2ColorTransfer color_transfer;
    GstC2ColorMatrix matrix;
    GstC2ColorRange full_range;
  } color_aspects;
};

void push_to_settings (gpointer data, gpointer user_data);

GstC2VideoProfile gst_c2_utils_h264_profile_from_string (const gchar * profile);
GstC2VideoProfile gst_c2_utils_h265_profile_from_string (const gchar * profile);

GstC2VideoLevel gst_c2_utils_h264_level_from_string (const gchar * level);
GstC2VideoLevel gst_c2_utils_h265_level_from_string (const gchar * level,
    const gchar * tier);

#endif // __GST_C2_CONFIG_H__
