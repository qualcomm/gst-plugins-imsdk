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
#define CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY "dec_low_latency"
#define CONFIG_FUNCTION_KEY_INTRAREFRESH "intra_refresh"
#define CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE "output_picture_order_mode"
#define CONFIG_FUNCTION_KEY_DOWNSCALE "downscale"
#define CONFIG_FUNCTION_KEY_ENC_CSC "enc_colorspace_conversion"
#define CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO "colorspace_color_aspects"
#define CONFIG_FUNCTION_KEY_SLICE_MODE "slice_mode"
#define CONFIG_FUNCTION_KEY_BLUR_MODE "blur_mode"
#define CONFIG_FUNCTION_KEY_BLUR_RESOLUTION "blur_resolution"
#define CONFIG_FUNCTION_KEY_QP_RANGES "qp_ranges"

typedef enum {
  INTERLACE_MODE_PROGRESSIVE = 0,
  INTERLACE_MODE_INTERLEAVED_TOP_FIRST,
  INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST,
  INTERLACE_MODE_FIELD_TOP_FIRST,
  INTERLACE_MODE_FIELD_BOTTOM_FIRST,
} INTERLACE_MODE_TYPE;

typedef enum {
  C2_INTERLACE_MODE_PROGRESSIVE = 0,
  C2_INTERLACE_MODE_INTERLEAVED_TOP_FIRST,
  C2_INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST,
  C2_INTERLACE_MODE_FIELD_TOP_FIRST,
  C2_INTERLACE_MODE_FIELD_BOTTOM_FIRST,
} C2_INTERLACE_MODE_TYPE;

typedef enum {
  PIXEL_FORMAT_NV12_LINEAR = 0,
  PIXEL_FORMAT_NV12_UBWC,
  PIXEL_FORMAT_RGBA_8888,
  PIXEL_FORMAT_YV12,
  PIXEL_FORMAT_P010,
  PIXEL_FORMAT_TP10_UBWC
} PIXEL_FORMAT_TYPE;

typedef enum {
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
} C2_PIXEL_FORMAT;

typedef enum {
  DEFAULT_ORDER = 0,
  DISPLAY_ORDER,
  DECODER_ORDER,
} OUTPUT_PIC_ORDER;

typedef enum {
  MIRROR_NONE = 0,
  MIRROR_VERTICAL,
  MIRROR_HORIZONTAL,
  MIRROR_BOTH,
} MIRROR_TYPE;

typedef enum {
  RC_OFF = 0,
  RC_CONST,
  RC_CBR_VFR,
  RC_VBR_CFR,
  RC_VBR_VFR,
  RC_CQ,
  RC_UNSET = 0xFFFF
} RC_MODE_TYPE;

typedef enum {
  SLICE_MODE_DISABLE,
  SLICE_MODE_MB,
  SLICE_MODE_BYTES,
} SLICE_MODE;

typedef enum {
  BLUR_AUTO = 0,
  BLUR_MANUAL,
  BLUR_DISABLE,
} BLUR_MODE;

typedef enum {
  COLOR_PRIMARIES_UNSPECIFIED,
  COLOR_PRIMARIES_BT709,
  COLOR_PRIMARIES_BT470_M,
  COLOR_PRIMARIES_BT601_625,
  COLOR_PRIMARIES_BT601_525,
  COLOR_PRIMARIES_GENERIC_FILM,
  COLOR_PRIMARIES_BT2020,
  COLOR_PRIMARIES_RP431,
  COLOR_PRIMARIES_EG432,
  COLOR_PRIMARIES_EBU3213,
} COLOR_PRIMARIES;

typedef enum {
  COLOR_TRANSFER_UNSPECIFIED,
  COLOR_TRANSFER_LINEAR,
  COLOR_TRANSFER_SRGB,
  COLOR_TRANSFER_170M,
  COLOR_TRANSFER_GAMMA22,
  COLOR_TRANSFER_GAMMA28,
  COLOR_TRANSFER_ST2084,
  COLOR_TRANSFER_HLG,
  COLOR_TRANSFER_240M,
  COLOR_TRANSFER_XVYCC,
  COLOR_TRANSFER_BT1361,
  COLOR_TRANSFER_ST428,
} TRANSFER_CHAR;

typedef enum {
  COLOR_MATRIX_UNSPECIFIED,
  COLOR_MATRIX_BT709,
  COLOR_MATRIX_FCC47_73_682,
  COLOR_MATRIX_BT601,
  COLOR_MATRIX_240M,
  COLOR_MATRIX_BT2020,
  COLOR_MATRIX_BT2020_CONSTANT,
} MATRIX;

typedef enum {
  COLOR_RANGE_UNSPECIFIED,
  COLOR_RANGE_FULL,
  COLOR_RANGE_LIMITED,
} FULL_RANGE;

typedef enum {
  IR_NONE = 0,
  IR_RANDOM,
} IR_MODE_TYPE;

typedef struct {
  const char* config_name;
  gboolean isInput;
  union {
    guint32 u32;
    guint64 u64;
    gint32 i32;
    gint64 i64;
  } val;

  struct {
    guint32 width;
    guint32 height;
  } resolution;

  struct {
    guint32 miniqp;
    guint32 maxiqp;
    guint32 minpqp;
    guint32 maxpqp;
    guint32 minbqp;
    guint32 maxbqp;
  } qp_ranges;

  union {
    PIXEL_FORMAT_TYPE fmt;
  } pixelFormat;

  union {
    INTERLACE_MODE_TYPE type;
  } interlaceMode;

  union {
    MIRROR_TYPE type;
  } mirror;

  union {
    RC_MODE_TYPE type;
  } rcMode;

  union {
    SLICE_MODE type;
  } SliceMode;

  union {
    BLUR_MODE mode;
  } blur;

  struct {
    IR_MODE_TYPE type;
    float intra_refresh_mbs;
  } irMode;
  guint output_picture_order_mode;
  gboolean low_latency_mode;
  gboolean color_space_conversion;
  struct {
    COLOR_PRIMARIES primaries;
    TRANSFER_CHAR transfer_char;
    MATRIX matrix;
    FULL_RANGE full_range;
  } colorAspects;
} ConfigParams;

void push_to_settings(gpointer data, gpointer user_data);

#endif // __GST_C2_CONFIG_H__