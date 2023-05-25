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

#ifndef __GST_C2_ENGINE_PARAMS_H__
#define __GST_C2_ENGINE_PARAMS_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

// GST Buffer flag for key/sync frame.
#define GST_VIDEO_BUFFER_FLAG_SYNC (GST_VIDEO_BUFFER_FLAG_LAST << 0)
// GST Buffer flag for frame with UBWC.
#define GST_VIDEO_BUFFER_FLAG_UBWC (GST_VIDEO_BUFFER_FLAG_LAST << 1)

// Maximum number of regions for encoding.
#define GST_C2_MAX_RECT_ROI_NUM    20

typedef struct _GstC2PixelInfo GstC2PixelInfo;
typedef struct _GstC2Resolution GstC2Resolution;
typedef struct _GstC2Gop GstC2Gop;
typedef struct _GstC2IntraRefresh GstC2IntraRefresh;
typedef struct _GstC2Slice GstC2Slice;
typedef struct _GstC2TileLayout GstC2TileLayout;
typedef struct _GstC2QuantInit GstC2QuantInit;
typedef struct _GstC2QuantRanges GstC2QuantRanges;
typedef struct _GstC2QuantRectangle GstC2QuantRectangle;
typedef struct _GstC2QuantRegions GstC2QuantRegions;
typedef struct _GstC2TemporalLayer GstC2TemporalLayer;

// GStreamer Codec2 Engine parameter types.
enum {
  GST_C2_PARAM_IN_FORMAT,            // GstC2PixelInfo
  GST_C2_PARAM_OUT_FORMAT,           // GstC2PixelInfo
  GST_C2_PARAM_IN_RESOLUTION,        // GstC2Resolution
  GST_C2_PARAM_OUT_RESOLUTION,       // GstC2Resolution
  GST_C2_PARAM_IN_FRAMERATE,         // gdouble
  GST_C2_PARAM_OUT_FRAMERATE,        // gdouble
  GST_C2_PARAM_PROFILE_LEVEL,        // guint32 (profile & 0xFFFF) + (level << 16)
  GST_C2_PARAM_RATE_CONTROL,         // GstC2RateControl
  GST_C2_PARAM_BITRATE,              // guint32
  GST_C2_PARAM_GOP_CONFIG,           // GstC2Gop
  GST_C2_PARAM_KEY_FRAME_INTERVAL,   // gint64
  GST_C2_PARAM_INTRA_REFRESH,        // GstC2IntraRefresh
  GST_C2_PARAM_ADAPTIVE_B_FRAMES,    // gboolean
  GST_C2_PARAM_ENTROPY_MODE,         // GstC2EntropyMode
  GST_C2_PARAM_LOOP_FILTER_MODE,     // GstC2LoopFilterMode
  GST_C2_PARAM_SLICE_MB,             // GstC2Slice
  GST_C2_PARAM_SLICE_BYTES,          // guint32
  GST_C2_PARAM_NUM_LTR_FRAMES,       // guint32
  GST_C2_PARAM_ROTATION,             // GstC2VideoRotate
  GST_C2_PARAM_TILE_LAYOUT,          // GstC2TileLayout
  GST_C2_PARAM_PREPEND_HEADER_MODE,  // GstC2HeaderMode
  GST_C2_PARAM_ENABLE_PICTURE_ORDER, // gboolean
  GST_C2_PARAM_QP_INIT,              // GstC2QuantInit
  GST_C2_PARAM_QP_RANGES,            // GstC2QuantRanges
  GST_C2_PARAM_ROI_ENCODE,           // GstC2QuantRegions
  GST_C2_PARAM_TRIGGER_SYNC_FRAME,   // gboolean
  GST_C2_PARAM_NATIVE_RECORDING,     // gboolean
  GST_C2_PARAM_TEMPORAL_LAYERING,    // GstC2TemporalLayer
};

typedef enum {
  GST_C2_PROFILE_AVC_BASELINE,
  GST_C2_PROFILE_AVC_CONSTRAINT_BASELINE,
  GST_C2_PROFILE_AVC_HIGH,
  GST_C2_PROFILE_AVC_CONSTRAINT_HIGH,
  GST_C2_PROFILE_AVC_MAIN,

  GST_C2_PROFILE_HEVC_MAIN,
  GST_C2_PROFILE_HEVC_MAIN10,
  GST_C2_PROFILE_HEVC_MAIN_STILL,

  GST_C2_PROFILE_INVALID,
} GstC2Profile;

typedef enum {
  GST_C2_LEVEL_AVC_1,
  GST_C2_LEVEL_AVC_1B,
  GST_C2_LEVEL_AVC_1_1,
  GST_C2_LEVEL_AVC_1_2,
  GST_C2_LEVEL_AVC_1_3,
  GST_C2_LEVEL_AVC_2,
  GST_C2_LEVEL_AVC_2_1,
  GST_C2_LEVEL_AVC_2_2,
  GST_C2_LEVEL_AVC_3,
  GST_C2_LEVEL_AVC_3_1,
  GST_C2_LEVEL_AVC_3_2,
  GST_C2_LEVEL_AVC_4,
  GST_C2_LEVEL_AVC_4_1,
  GST_C2_LEVEL_AVC_4_2,
  GST_C2_LEVEL_AVC_5,
  GST_C2_LEVEL_AVC_5_1,
  GST_C2_LEVEL_AVC_5_2,
  GST_C2_LEVEL_AVC_6,
  GST_C2_LEVEL_AVC_6_1,
  GST_C2_LEVEL_AVC_6_2,

  GST_C2_LEVEL_HEVC_MAIN_1,
  GST_C2_LEVEL_HEVC_MAIN_2,
  GST_C2_LEVEL_HEVC_MAIN_2_1,
  GST_C2_LEVEL_HEVC_MAIN_3,
  GST_C2_LEVEL_HEVC_MAIN_3_1,
  GST_C2_LEVEL_HEVC_MAIN_4,
  GST_C2_LEVEL_HEVC_MAIN_4_1,
  GST_C2_LEVEL_HEVC_MAIN_5,
  GST_C2_LEVEL_HEVC_MAIN_5_1,
  GST_C2_LEVEL_HEVC_MAIN_5_2,
  GST_C2_LEVEL_HEVC_MAIN_6,
  GST_C2_LEVEL_HEVC_MAIN_6_1,
  GST_C2_LEVEL_HEVC_MAIN_6_2,

  GST_C2_LEVEL_HEVC_HIGH_4,
  GST_C2_LEVEL_HEVC_HIGH_4_1,
  GST_C2_LEVEL_HEVC_HIGH_5,
  GST_C2_LEVEL_HEVC_HIGH_5_1,
  GST_C2_LEVEL_HEVC_HIGH_5_2,
  GST_C2_LEVEL_HEVC_HIGH_6,
  GST_C2_LEVEL_HEVC_HIGH_6_1,
  GST_C2_LEVEL_HEVC_HIGH_6_2,

  GST_C2_LEVEL_INVALID,
} GstC2Level;

typedef enum {
  GST_C2_RATE_CTRL_DISABLE,
  GST_C2_RATE_CTRL_CONSTANT,
  GST_C2_RATE_CTRL_CBR_VFR,
  GST_C2_RATE_CTRL_VBR_CFR,
  GST_C2_RATE_CTRL_VBR_VFR,
  GST_C2_RATE_CTRL_CQ,
} GstC2RateControl;

typedef enum {
  GST_C2_INTRA_REFRESH_DISABLED,
  GST_C2_INTRA_REFRESH_ARBITRARY,
} GstC2IRefreshMode;

typedef enum {
  GST_C2_ENTROPY_CAVLC,
  GST_C2_ENTROPY_CABAC,
} GstC2EntropyMode;

typedef enum {
  GST_C2_LOOP_FILTER_ENABLE,
  GST_C2_LOOP_FILTER_DISABLE,
  GST_C2_LOOP_FILTER_DISABLE_SLICE_BOUNDARY,
} GstC2LoopFilterMode;

typedef enum {
  GST_C2_SLICE_MB,
  GST_C2_SLICE_BYTES,
} GstC2SliceMode;

typedef enum {
  GST_C2_ROTATE_NONE,
  GST_C2_ROTATE_90_CW,
  GST_C2_ROTATE_180,
  GST_C2_ROTATE_90_CCW,
} GstC2VideoRotate;

typedef enum {
  GST_C2_PREPEND_HEADER_TO_NONE,
  GST_C2_PREPEND_HEADER_ON_CHANGE,
  GST_C2_PREPEND_HEADER_TO_ALL_SYNC,
} GstC2HeaderMode;

struct _GstC2PixelInfo {
  GstVideoFormat format;
  gboolean       isubwc;
};

struct _GstC2Resolution {
  guint32 width;
  guint32 height;
};

struct _GstC2Gop {
  guint32 n_pframes;
  guint32 n_bframes;
};

struct _GstC2IntraRefresh {
  GstC2IRefreshMode mode;
  guint32           period;
};

struct _GstC2Slice {
  GstC2SliceMode mode;
  guint32        size;
};

struct _GstC2TileLayout {
  GstC2Resolution dims;
  guint32         n_columns;
  guint32         n_rows;
};

struct _GstC2QuantInit {
  gboolean i_frames_enable;
  guint32  i_frames;
  gboolean p_frames_enable;
  guint32  p_frames;
  gboolean b_frames_enable;
  guint32  b_frames;
};

struct _GstC2QuantRanges {
  guint32 min_i_qp;
  guint32 max_i_qp;
  guint32 min_p_qp;
  guint32 max_p_qp;
  guint32 min_b_qp;
  guint32 max_b_qp;
};

struct _GstC2QuantRectangle {
  gint32 x;
  gint32 y;
  gint32 w;
  gint32 h;
  gint32 qp;
};

struct _GstC2QuantRegions {
  GstC2QuantRectangle rects[GST_C2_MAX_RECT_ROI_NUM];
  guint32             n_rects;
  guint64             timestamp;
};

struct _GstC2TemporalLayer {
  guint32 n_layers;
  guint32 n_blayers;
  GArray  *bitrate_ratios;
};

guint gst_c2_utils_h264_profile_from_string (const gchar * profile);
guint gst_c2_utils_h265_profile_from_string (const gchar * profile);

const gchar * gst_c2_utils_h264_profile_to_string (guint profile);
const gchar * gst_c2_utils_h265_profile_to_string (guint profile);

guint gst_c2_utils_h264_level_from_string (const gchar * level);
guint gst_c2_utils_h265_level_from_string (const gchar * level, const gchar * tier);

const gchar * gst_c2_utils_h264_level_to_string (guint level);
const gchar * gst_c2_utils_h265_level_to_string (guint level);

G_END_DECLS

#endif // __GST_C2_ENGINE_PARAMS_H__
