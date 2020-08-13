/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_QMMFSRC_UTILS_H__
#define __GST_QMMFSRC_UTILS_H__

#include <gst/gst.h>
#include <glib/gtypes.h>

G_BEGIN_DECLS

#define QMMFSRC_RETURN_VAL_IF_FAIL(element, expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR_OBJECT (element, __VA_ARGS__); \
    return (value); \
  } \
}

#define QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN(element, expression, cleanup, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR_OBJECT (element, __VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define QMMFSRC_RETURN_IF_FAIL(element, expression, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR_OBJECT (element, __VA_ARGS__); \
    return; \
  } \
}

#define QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN(element, expression, cleanup,...) \
{ \
  if (!(expression)) { \
    GST_ERROR_OBJECT (element, __VA_ARGS__); \
    cleanup; \
    return; \
  } \
}

#define QMMFSRC_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

#define GST_TYPE_QMMFSRC_EFFECT_MODE (gst_qmmfsrc_effect_mode_get_type())
#define GST_TYPE_QMMFSRC_SCENE_MODE (gst_qmmfsrc_scene_mode_get_type())
#define GST_TYPE_QMMFSRC_ANTIBANDING (gst_qmmfsrc_antibanding_get_type())
#define GST_TYPE_QMMFSRC_AE_MODE (gst_qmmfsrc_ae_mode_get_type())
#define GST_TYPE_QMMFSRC_WHITE_BALANCE_MODE (gst_qmmfsrc_white_balance_mode_get_type())
#define GST_TYPE_QMMFSRC_AF_MODE (gst_qmmfsrc_af_mode_get_type())
#define GST_TYPE_QMMFSRC_IR_MODE (gst_qmmfsrc_ir_mode_get_type())
#define GST_TYPE_QMMFSRC_ISO_MODE (gst_qmmfsrc_iso_mode_get_type())
#define GST_TYPE_QMMFSRC_AE_METERING_MODE (gst_qmmfsrc_ae_metering_mode_get_type())
#define GST_TYPE_QMMFSRC_NOISE_REDUCTION (gst_qmmfsrc_noise_reduction_get_type())

#define GST_BAYER_FORMAT_OFFSET 0x1000

// Extension to the GstVideoFormat for supporting bayer formats.
typedef enum {
  GST_BAYER_FORMAT_BGGR = GST_BAYER_FORMAT_OFFSET,
  GST_BAYER_FORMAT_RGGB,
  GST_BAYER_FORMAT_GBRG,
  GST_BAYER_FORMAT_GRBG,
  GST_BAYER_FORMAT_MONO,
} GstBayerFormat;

enum
{
  EFFECT_MODE_OFF,
  EFFECT_MODE_MONO,
  EFFECT_MODE_NEGATIVE,
  EFFECT_MODE_SOLARIZE,
  EFFECT_MODE_SEPIA,
  EFFECT_MODE_POSTERIZE,
  EFFECT_MODE_WHITEBOARD,
  EFFECT_MODE_BLACKBOARD,
  EFFECT_MODE_AQUA,
};

enum
{
  SCENE_MODE_DISABLED,
  SCENE_MODE_FACE_PRIORITY,
  SCENE_MODE_ACTION,
  SCENE_MODE_PORTRAIT,
  SCENE_MODE_LANDSCAPE,
  SCENE_MODE_NIGHT,
  SCENE_MODE_NIGHT_PORTRAIT,
  SCENE_MODE_THEATRE,
  SCENE_MODE_BEACH,
  SCENE_MODE_SNOW,
  SCENE_MODE_SUNSET,
  SCENE_MODE_STEADYPHOTO,
  SCENE_MODE_FIREWORKS,
  SCENE_MODE_SPORTS,
  SCENE_MODE_PARTY,
  SCENE_MODE_CANDLELIGHT,
  SCENE_MODE_HDR,
};

enum
{
  ANTIBANDING_MODE_OFF,
  ANTIBANDING_MODE_50HZ,
  ANTIBANDING_MODE_60HZ,
  ANTIBANDING_MODE_AUTO,
};

enum
{
  AE_MODE_OFF,
  AE_MODE_ON,
};

enum
{
  WHITE_BALANCE_MODE_OFF,
  WHITE_BALANCE_MODE_MANUAL_CCTEMP,
  WHITE_BALANCE_MODE_MANUAL_GAINS,
  WHITE_BALANCE_MODE_AUTO,
  WHITE_BALANCE_MODE_SHADE,
  WHITE_BALANCE_MODE_INCANDESCENT,
  WHITE_BALANCE_MODE_FLUORESCENT,
  WHITE_BALANCE_MODE_WARM_FLUORESCENT,
  WHITE_BALANCE_MODE_DAYLIGHT,
  WHITE_BALANCE_MODE_CLOUDY_DAYLIGHT,
  WHITE_BALANCE_MODE_TWILIGHT,
};

enum
{
  AF_MODE_OFF,
  AF_MODE_AUTO,
  AF_MODE_MACRO,
  AF_MODE_CONTINUOUS,
  AF_MODE_EDOF,
};

enum
{
  IR_MODE_OFF,
  IR_MODE_ON,
  IR_MODE_AUTO,
};

enum
{
  ISO_MODE_AUTO,
  ISO_MODE_DEBLUR,
  ISO_MODE_100,
  ISO_MODE_200,
  ISO_MODE_400,
  ISO_MODE_800,
  ISO_MODE_1600,
  ISO_MODE_3200,
};

enum
{
  AE_METERING_MODE_AVERAGE,
  AE_METERING_MODE_CENTER_WEIGHTED,
  AE_METERING_MODE_SPOT,
  AE_METERING_MODE_SMART,
  AE_METERING_MODE_SPOT_ADVANCED,
  AE_METERING_MODE_CENTER_WEIGHTED_ADVANCED,
  AE_METERING_MODE_CUSTOM,
};

enum
{
  NOISE_REDUCTION_OFF,
  NOISE_REDUCTION_FAST,
  NOISE_REDUCTION_HIGH_QUALITY,
};

GType gst_qmmfsrc_effect_mode_get_type (void);

GType gst_qmmfsrc_scene_mode_get_type (void);

GType gst_qmmfsrc_antibanding_get_type (void);

GType gst_qmmfsrc_ae_mode_get_type (void);

GType gst_qmmfsrc_white_balance_mode_get_type (void);

GType gst_qmmfsrc_af_mode_get_type (void);

GType gst_qmmfsrc_ir_mode_get_type (void);

GType gst_qmmfsrc_iso_mode_get_type (void);

GType gst_qmmfsrc_ae_metering_mode_get_type (void);

GType gst_qmmfsrc_noise_reduction_get_type (void);

guchar gst_qmmfsrc_effect_mode_android_value (const guint value);

guchar gst_qmmfsrc_scene_mode_android_value (const guint value);

guchar gst_qmmfsrc_antibanding_android_value (const guint value);

guchar gst_qmmfsrc_ae_mode_android_value (const guint value);

guchar gst_qmmfsrc_wb_mode_android_value (const guint value);

guchar gst_qmmfsrc_af_mode_android_value (const guint value);

guchar gst_qmmfsrc_noise_reduction_android_value (const guint value);

/// org.quic.camera.defog
static const gchar * gst_camera_defog_table[] =
{
    "enable",
    "algo_type",
    "algo_decision_mode",
    "strength",
    "convergence_speed",
    "lp_color_comp_gain",
    "abc_en",
    "acc_en",
    "afsd_en",
    "afsd_2a_en",
    "defog_dark_thres",
    "defog_bright_thres",
    "abc_gain",
    "acc_max_dark_str",
    "acc_max_bright_str",
    "dark_limit",
    "bright_limit",
    "dark_preserve",
    "bright_preserve",
    "trig_params",
    "ce_en",
    "convergence_mode",
    "guc_en",
    "dcc_en",
    "guc_str",
    "dcc_dark_str",
    "dcc_bright_str",
    "ce_trig_params",
};

/// org.codeaurora.qcamera3.exposuretable
static const gchar * gst_camera_exposure_table[] =
{
    "isValid",
    "sensitivityCorrectionFactor",
    "kneeCount",
    "gainKneeEntries",
    "expTimeKneeEntries",
    "incrementPriorityKneeEntries",
    "expIndexKneeEntries",
    "thresAntiBandingMinExpTimePct",
};

/// org.quic.camera.ltmDynamicContrast
static const gchar * gst_camera_ltm_data[] =
{
    "ltmDynamicContrastStrength",
    "ltmDarkBoostStrength",
    "ltmBrightSupressStrength",
};

/// org.quic.camera.anr_tuning
static const gchar * gst_camera_nr_tuning_data[] =
{
    "anr_intensity",
    "anr_motion_sensitivity",
};

/// org.codeaurora.qcamera3.manualWB
static const gchar * gst_camera_manual_wb_settings[] =
{
    "gains",
    "color_temperature",
};

G_END_DECLS

#endif // __GST_QMMFSRC_UTILS_H__
