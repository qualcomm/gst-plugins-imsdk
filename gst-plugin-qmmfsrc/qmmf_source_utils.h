/*
* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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

/*
 *  Changes from Qualcomm Innovation Center are provided under the following license:
 *
 *  Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *
 *      * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
 *
 *  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 *  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 *  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#define GST_TYPE_QMMFSRC_CONTROL_MODE (gst_qmmfsrc_control_mode_get_type())
#define GST_TYPE_QMMFSRC_EFFECT_MODE (gst_qmmfsrc_effect_mode_get_type())
#define GST_TYPE_QMMFSRC_SCENE_MODE (gst_qmmfsrc_scene_mode_get_type())
#define GST_TYPE_QMMFSRC_ANTIBANDING (gst_qmmfsrc_antibanding_get_type())
#define GST_TYPE_QMMFSRC_EXPOSURE_MODE (gst_qmmfsrc_exposure_mode_get_type())
#define GST_TYPE_QMMFSRC_EXPOSURE_METERING \
    (gst_qmmfsrc_exposure_metering_get_type())
#define GST_TYPE_QMMFSRC_WHITE_BALANCE_MODE \
    (gst_qmmfsrc_white_balance_mode_get_type())
#define GST_TYPE_QMMFSRC_FOCUS_MODE (gst_qmmfsrc_focus_mode_get_type())
#define GST_TYPE_QMMFSRC_IR_MODE (gst_qmmfsrc_ir_mode_get_type())
#define GST_TYPE_QMMFSRC_ISO_MODE (gst_qmmfsrc_iso_mode_get_type())
#define GST_TYPE_QMMFSRC_NOISE_REDUCTION \
    (gst_qmmfsrc_noise_reduction_get_type())

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
  CONTROL_MODE_OFF,
  CONTROL_MODE_AUTO,
  CONTROL_MODE_USE_SCENE_MODE,
  CONTROL_MODE_OFF_KEEP_STATE,
};

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
  EXPOSURE_MODE_OFF,
  EXPOSURE_MODE_AUTO,
};

enum
{
  EXPOSURE_METERING_AVERAGE,
  EXPOSURE_METERING_CENTER_WEIGHTED,
  EXPOSURE_METERING_SPOT,
  EXPOSURE_METERING_SMART,
  EXPOSURE_METERING_SPOT_ADVANCED,
  EXPOSURE_METERING_CENTER_WEIGHTED_ADVANCED,
  EXPOSURE_METERING_CUSTOM,
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
  FOCUS_MODE_OFF,
  FOCUS_MODE_AUTO,
  FOCUS_MODE_MACRO,
  FOCUS_MODE_CONTINUOUS,
  FOCUS_MODE_EDOF,
};

enum
{
  IR_MODE_OFF,
  IR_MODE_ON,
  IR_MODE_AUTO,
  IR_MODE_FILTER_ONLY,
  IR_MODE_FILTER_DISABLE,
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
  ISO_MODE_MANUAL,
};

enum
{
  NOISE_REDUCTION_OFF,
  NOISE_REDUCTION_FAST,
  NOISE_REDUCTION_HIGH_QUALITY,
};

GType gst_qmmfsrc_control_mode_get_type (void);

GType gst_qmmfsrc_effect_mode_get_type (void);

GType gst_qmmfsrc_scene_mode_get_type (void);

GType gst_qmmfsrc_antibanding_get_type (void);

GType gst_qmmfsrc_exposure_mode_get_type (void);

GType gst_qmmfsrc_exposure_metering_get_type (void);

GType gst_qmmfsrc_white_balance_mode_get_type (void);

GType gst_qmmfsrc_focus_mode_get_type (void);

GType gst_qmmfsrc_ir_mode_get_type (void);

GType gst_qmmfsrc_iso_mode_get_type (void);

GType gst_qmmfsrc_noise_reduction_get_type (void);

guchar gst_qmmfsrc_control_mode_android_value (const guint value);

guint gst_qmmfsrc_android_value_control_mode (const guchar value);

guchar gst_qmmfsrc_effect_mode_android_value (const guint value);

guint gst_qmmfsrc_android_value_effect_mode (const guchar value);

guchar gst_qmmfsrc_scene_mode_android_value (const guint value);

guint gst_qmmfsrc_android_value_scene_mode (const guchar value);

guchar gst_qmmfsrc_antibanding_android_value (const guint value);

guint gst_qmmfsrc_android_value_antibanding (const guchar value);

guchar gst_qmmfsrc_exposure_mode_android_value (const guint value);

guint gst_qmmfsrc_android_value_exposure_mode (const guchar value);

guchar gst_qmmfsrc_white_balance_mode_android_value (const guint value);

guint gst_qmmfsrc_android_value_white_balance_mode (const guchar value);

guchar gst_qmmfsrc_focus_mode_android_value (const guint value);

guint gst_qmmfsrc_android_value_focus_mode (const guchar value);

guchar gst_qmmfsrc_noise_reduction_android_value (const guint value);

guint gst_qmmfsrc_android_value_noise_reduction (const guchar value);

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
    "ates_enable",
    "ates_strength"
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
