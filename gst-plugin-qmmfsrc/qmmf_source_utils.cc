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
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "qmmf_source_utils.h"
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf_source_context.h>

// Declare Qmmf buffer pool
G_DEFINE_TYPE(GstQmmfBufferPool, gst_qmmf_buffer_pool, GST_TYPE_BUFFER_POOL);
#define qmmf_pool_parent_class gst_qmmf_buffer_pool_parent_class

// Declare qmmf_buffer_qdata_quark() to return Quark for Qmmf buffer data
G_DEFINE_QUARK(QmmfBufferQDataQuark, qmmf_buffer_qdata);

#define QMMFSRC_PROPERTY_MAP_SIZE(MAP) (sizeof(MAP)/sizeof(MAP[0]))

// Default values when static metadata is not available
#define DEFAULT_MAX_FPS 120
#define DEFAULT_MAX_WIDTH 8192
#define DEFAULT_MAX_HEIGHT 5440
#define DEFAULT_MIN_WIDTH 16
#define DEFAULT_MIN_HEIGHT 16

typedef struct _PropAndroidEnum PropAndroidEnum;

struct _PropAndroidEnum
{
  gint value;
  guchar venum;
};

static const PropAndroidEnum control_mode_map[] = {
  {CONTROL_MODE_OFF, ANDROID_CONTROL_MODE_OFF},
  {CONTROL_MODE_AUTO, ANDROID_CONTROL_MODE_AUTO},
  {CONTROL_MODE_USE_SCENE_MODE, ANDROID_CONTROL_MODE_USE_SCENE_MODE},
  {CONTROL_MODE_OFF_KEEP_STATE, ANDROID_CONTROL_MODE_OFF_KEEP_STATE},
};

static const PropAndroidEnum effect_mode_map[] = {
  {EFFECT_MODE_OFF, ANDROID_CONTROL_EFFECT_MODE_OFF},
  {EFFECT_MODE_MONO, ANDROID_CONTROL_EFFECT_MODE_MONO},
  {EFFECT_MODE_NEGATIVE, ANDROID_CONTROL_EFFECT_MODE_NEGATIVE},
  {EFFECT_MODE_SOLARIZE, ANDROID_CONTROL_EFFECT_MODE_SOLARIZE},
  {EFFECT_MODE_SEPIA, ANDROID_CONTROL_EFFECT_MODE_SEPIA},
  {EFFECT_MODE_POSTERIZE, ANDROID_CONTROL_EFFECT_MODE_POSTERIZE},
  {EFFECT_MODE_WHITEBOARD, ANDROID_CONTROL_EFFECT_MODE_WHITEBOARD},
  {EFFECT_MODE_BLACKBOARD, ANDROID_CONTROL_EFFECT_MODE_BLACKBOARD},
  {EFFECT_MODE_AQUA, ANDROID_CONTROL_EFFECT_MODE_AQUA},
};

static const PropAndroidEnum scene_mode_map[] = {
  {SCENE_MODE_DISABLED, ANDROID_CONTROL_SCENE_MODE_DISABLED},
  {SCENE_MODE_FACE_PRIORITY, ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY},
  {SCENE_MODE_ACTION, ANDROID_CONTROL_SCENE_MODE_ACTION},
  {SCENE_MODE_PORTRAIT, ANDROID_CONTROL_SCENE_MODE_PORTRAIT},
  {SCENE_MODE_LANDSCAPE, ANDROID_CONTROL_SCENE_MODE_LANDSCAPE},
  {SCENE_MODE_NIGHT, ANDROID_CONTROL_SCENE_MODE_NIGHT},
  {SCENE_MODE_NIGHT_PORTRAIT, ANDROID_CONTROL_SCENE_MODE_NIGHT_PORTRAIT},
  {SCENE_MODE_THEATRE, ANDROID_CONTROL_SCENE_MODE_THEATRE},
  {SCENE_MODE_BEACH, ANDROID_CONTROL_SCENE_MODE_BEACH},
  {SCENE_MODE_SNOW, ANDROID_CONTROL_SCENE_MODE_SNOW},
  {SCENE_MODE_SUNSET, ANDROID_CONTROL_SCENE_MODE_SUNSET},
  {SCENE_MODE_STEADYPHOTO, ANDROID_CONTROL_SCENE_MODE_STEADYPHOTO},
  {SCENE_MODE_FIREWORKS, ANDROID_CONTROL_SCENE_MODE_FIREWORKS},
  {SCENE_MODE_SPORTS, ANDROID_CONTROL_SCENE_MODE_SPORTS},
  {SCENE_MODE_PARTY, ANDROID_CONTROL_SCENE_MODE_PARTY},
  {SCENE_MODE_CANDLELIGHT, ANDROID_CONTROL_SCENE_MODE_CANDLELIGHT},
  {SCENE_MODE_HDR, ANDROID_CONTROL_SCENE_MODE_HDR},
};

static const PropAndroidEnum antibanding_map[] = {
  {ANTIBANDING_MODE_OFF, ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF},
  {ANTIBANDING_MODE_50HZ, ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ},
  {ANTIBANDING_MODE_60HZ, ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ},
  {ANTIBANDING_MODE_AUTO, ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO},
};

static const PropAndroidEnum exposure_mode_map[] = {
  {EXPOSURE_MODE_OFF, ANDROID_CONTROL_AE_MODE_OFF},
  {EXPOSURE_MODE_AUTO, ANDROID_CONTROL_AE_MODE_ON},
};

static const PropAndroidEnum white_balance_mode_map[] = {
  { WHITE_BALANCE_MODE_OFF,
      ANDROID_CONTROL_AWB_MODE_OFF
  },
  { WHITE_BALANCE_MODE_AUTO,
      ANDROID_CONTROL_AWB_MODE_AUTO
  },
  { WHITE_BALANCE_MODE_SHADE,
      ANDROID_CONTROL_AWB_MODE_SHADE
  },
  { WHITE_BALANCE_MODE_INCANDESCENT,
      ANDROID_CONTROL_AWB_MODE_INCANDESCENT
  },
  { WHITE_BALANCE_MODE_FLUORESCENT,
      ANDROID_CONTROL_AWB_MODE_FLUORESCENT
  },
  { WHITE_BALANCE_MODE_WARM_FLUORESCENT,
      ANDROID_CONTROL_AWB_MODE_WARM_FLUORESCENT
  },
  { WHITE_BALANCE_MODE_DAYLIGHT,
      ANDROID_CONTROL_AWB_MODE_DAYLIGHT
  },
  { WHITE_BALANCE_MODE_CLOUDY_DAYLIGHT,
      ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT
  },
  { WHITE_BALANCE_MODE_TWILIGHT,
      ANDROID_CONTROL_AWB_MODE_TWILIGHT
  },
};

static const PropAndroidEnum focus_mode_map[] = {
  {FOCUS_MODE_OFF, ANDROID_CONTROL_AF_MODE_OFF},
  {FOCUS_MODE_AUTO, ANDROID_CONTROL_AF_MODE_AUTO},
  {FOCUS_MODE_MACRO, ANDROID_CONTROL_AF_MODE_MACRO},
  {FOCUS_MODE_CONTINUOUS, ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO},
  {FOCUS_MODE_EDOF, ANDROID_CONTROL_AF_MODE_EDOF},
};

static const PropAndroidEnum noise_reduction_map[] = {
  {NOISE_REDUCTION_OFF, ANDROID_NOISE_REDUCTION_MODE_OFF},
  {NOISE_REDUCTION_FAST, ANDROID_NOISE_REDUCTION_MODE_FAST},
  {NOISE_REDUCTION_HIGH_QUALITY, ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY},
};

GType
gst_qmmfsrc_control_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { CONTROL_MODE_OFF,
        "Full application control of pipeline.", "off"
    },
    { CONTROL_MODE_AUTO,
        "Manual control of capture parameters is disabled.", "auto"
    },
    { CONTROL_MODE_USE_SCENE_MODE,
        "Use a specific scene mode.", "use-scene-mode"
    },
    { CONTROL_MODE_OFF_KEEP_STATE,
        "Same as OFF mode, except that this capture will not be used by camera "
        "device background auto-exposure, auto-white balance and auto-focus "
        "algorithms (3A) to update their statistics.", "off-keep-state"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraControlMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_effect_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { EFFECT_MODE_OFF,
        "No color effect will be applied.", "off"
    },
    { EFFECT_MODE_MONO,
        "A 'monocolor' effect where the image is mapped into a single color.",
        "mono"
    },
    { EFFECT_MODE_NEGATIVE,
        "A 'photo-negative' effect where the image's colors are inverted.",
        "negative"
    },
    { EFFECT_MODE_SOLARIZE,
        "A 'solarisation' effect (Sabattier effect) where the image is wholly "
        "or partially reversed in tone.", "solarize"
    },
    { EFFECT_MODE_SEPIA,
        "A 'sepia' effect where the image is mapped into warm gray, red, and "
        "brown tones.", "sepia"},
    { EFFECT_MODE_POSTERIZE,
        "A 'posterization' effect where the image uses discrete regions of "
        "tone rather than a continuous gradient of tones.", "posterize"
    },
    { EFFECT_MODE_WHITEBOARD,
        "A 'whiteboard' effect where the image is typically displayed as "
        "regions of white, with black or grey details.", "whiteboard"
    },
    { EFFECT_MODE_BLACKBOARD,
        "A 'blackboard' effect where the image is typically displayed as "
        "regions of black, with white or grey details.", "blackboard"
    },
    { EFFECT_MODE_AQUA,
        "An 'aqua' effect where a blue hue is added to the image.", "aqua"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraEffectMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_scene_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { SCENE_MODE_DISABLED,
        "Indicates that no scene modes are set.", "disabled"
    },
    { SCENE_MODE_FACE_PRIORITY,
        "Optimized for photos of with priority of people faces.",
        "face-priority"
    },
    { SCENE_MODE_ACTION,
        "Optimized for photos of quickly moving objects.", "action"
    },
    { SCENE_MODE_PORTRAIT,
        "Optimized for still photos of people.", "portrait"
    },
    { SCENE_MODE_LANDSCAPE,
        "Optimized for photos of distant macroscopic objects.", "landscape"
    },
    { SCENE_MODE_NIGHT,
        "Optimized for low-light settings.", "night"
    },
    { SCENE_MODE_NIGHT_PORTRAIT,
        "Optimized for still photos of people in low-light settings.",
        "night-portrait"},
    { SCENE_MODE_THEATRE,
        "Optimized for dim, indoor settings where flash must remain off.",
        "theatre"},
    { SCENE_MODE_BEACH,
        "Optimized for bright, outdoor beach settings.", "beach"
    },
    { SCENE_MODE_SNOW,
        "Optimized for bright, outdoor settings containing snow.", "snow"
    },
    { SCENE_MODE_SUNSET,
        "Optimized for scenes of the setting sun.", "sunset"
    },
    { SCENE_MODE_STEADYPHOTO,
        "Optimized to avoid blurry photos due to small amounts of device "
        "motion (for example: due to hand shake).", "steady-photo"
    },
    { SCENE_MODE_FIREWORKS,
        "Optimized for nighttime photos of fireworks.", "fireworks"
    },
    { SCENE_MODE_SPORTS,
        "Optimized for photos of quickly moving people.", "sports"
    },
    { SCENE_MODE_PARTY,
        "Optimized for dim, indoor settings with multiple moving people.",
        "party"
    },
    { SCENE_MODE_CANDLELIGHT,
        "Optimized for dim settings where the main light source is a candle.",
        "candlelight"
    },
    { SCENE_MODE_HDR,
        "Turn on a device-specific high dynamic range (HDR) mode.", "hdr"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraSceneMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_antibanding_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { ANTIBANDING_MODE_OFF,
        "The camera device will not adjust exposure duration to avoid banding "
        "problems.", "off"
    },
    { ANTIBANDING_MODE_50HZ,
        "The camera device will adjust exposure duration to avoid banding "
        "problems with 50Hz illumination sources.", "50hz"
    },
    { ANTIBANDING_MODE_60HZ,
        "The camera device will adjust exposure duration to avoid banding "
        "problems with 60Hz illumination sources.", "60hz"
    },
    { ANTIBANDING_MODE_AUTO,
        "The camera device will automatically adapt its antibanding routine "
        "to the current illumination condition.", "auto"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstAntibandingMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_exposure_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { EXPOSURE_MODE_OFF,
        "The auto exposure routine is disabled. Manual exposure time will be "
        "used set via the 'exposure-time' property", "off"
    },
    { EXPOSURE_MODE_AUTO,
        "The auto exposure routine is active.", "auto"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraExposureMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_white_balance_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { WHITE_BALANCE_MODE_OFF,
        "Both auto and manual white balance routines are disabled.", "off"
    },
    { WHITE_BALANCE_MODE_MANUAL_CCTEMP,
        "The auto-white balance routine is inactive and manual color correction"
        " temperature is used which is set via the 'manual-wb-settings' "
        "property.", "manual-cc-temp"
    },
    { WHITE_BALANCE_MODE_MANUAL_GAINS,
        "The auto-white balance routine is inactive and manual R/G/B gains are"
        " used which are set via the 'manual-wb-settings' property.",
        "manual-rgb-gains"
    },
    { WHITE_BALANCE_MODE_AUTO,
        "The auto-white balance routine is active.", "auto"
    },
    { WHITE_BALANCE_MODE_SHADE,
        "The camera device uses shade light as the assumed scene illumination "
        "for white balance correction.", "shade"
    },
    { WHITE_BALANCE_MODE_INCANDESCENT,
        "The camera device uses incandescent light as the assumed scene "
        "illumination for white balance correction.", "incandescent"
    },
    { WHITE_BALANCE_MODE_FLUORESCENT,
        "The camera device uses fluorescent light as the assumed scene "
        "illumination for white balance correction.", "fluorescent"
    },
    { WHITE_BALANCE_MODE_WARM_FLUORESCENT,
        "The camera device uses warm fluorescent light as the assumed scene "
        "illumination for white balance correction.", "warm-fluorescent"
    },
    { WHITE_BALANCE_MODE_DAYLIGHT,
        "The camera device uses daylight light as the assumed scene "
        "illumination for white balance correction.", "daylight"},
    { WHITE_BALANCE_MODE_CLOUDY_DAYLIGHT,
        "The camera device uses cloudy daylight light as the assumed scene "
        "illumination for white balance correction.", "cloudy-daylight"
    },
    { WHITE_BALANCE_MODE_TWILIGHT,
        "The camera device uses twilight light as the assumed scene "
        "illumination for white balance correction.", "twilight"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraWiteBalanceMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_focus_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { FOCUS_MODE_OFF,
        "The auto focus routine is disabled.", "off"
    },
    { FOCUS_MODE_AUTO,
        "The auto focus routine is active.", "auto"
    },
    { FOCUS_MODE_MACRO,
        "In this mode, the auto focus algorithm is optimized for focusing on "
        "objects very close to the camera.", "macro"
    },
    { FOCUS_MODE_CONTINUOUS,
        "In this mode, the AF algorithm modifies the lens position continually"
        " to attempt to provide a constantly-in-focus image stream.",
        "continuous"
    },
    { FOCUS_MODE_EDOF,
        "The camera device will produce images with an extended depth of field"
        " automatically; no special focusing operations need to be done before"
        " taking a picture.", "edof"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraFocusMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_ir_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { IR_MODE_OFF,
        "The infrared LED is OFF and cut filter is applied i.e. infrared light"
        " is blocked.", "off"
    },
    { IR_MODE_ON,
        "The infrared LED is ON and cut filter is removed i.e. infrared light "
        "is allowed.", "on"
    },
    { IR_MODE_AUTO,
        "The infrared LED and cut filter are turned ON or OFF depending"
        "on the conditions.", "auto"
    },
    { IR_MODE_FILTER_ONLY,
        "The infrared LED is turned OFF and cut filter is applied i.e. "
        "IR light is blocked.", "cut-filter-only"
    },
    { IR_MODE_FILTER_DISABLE,
        "Infrared cut filter is removed allowing IR light to pass. This mode is"
        " used for transitioning from 'cut-filter-only' mode i.e. disabling only"
        " the cut filter.", "cut-filter-disable"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraIRMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_iso_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { ISO_MODE_AUTO,
        "The ISO exposure mode will be chosen depending on the scene.", "auto"
    },
    { ISO_MODE_DEBLUR,
        "The ISO exposure sensitivity set to prioritize motion deblur.",
        "deblur"
    },
    { ISO_MODE_100,
        "The ISO exposure sensitivity set to prioritize level 100.", "100"
    },
    { ISO_MODE_200,
        "The ISO exposure sensitivity set to prioritize level 200.", "200"
    },
    { ISO_MODE_400,
        "The ISO exposure sensitivity set to prioritize level 400.", "400"
    },
    { ISO_MODE_800,
        "The ISO exposure sensitivity set to prioritize level 800.", "800"
    },
    { ISO_MODE_1600,
        "The ISO exposure sensitivity set to prioritize level 1600.", "1600"
    },
    { ISO_MODE_3200,
        "The ISO exposure sensitivity set to prioritize level 3200.", "3200"
    },
    { ISO_MODE_MANUAL,
        "The ISO exposure value provided by manual-iso-value will be used.",
        "manual"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraISOMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_exposure_metering_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { EXPOSURE_METERING_AVERAGE,
        "The camera device's exposure metering is calculated as average from "
        "the whole frame.", "average"
    },
    { EXPOSURE_METERING_CENTER_WEIGHTED,
        "The camera device's exposure metering is calculated from the center "
        "region of the frame.", "center-weighted"
    },
    { EXPOSURE_METERING_SPOT,
        "The camera device's exposure metering is calculated from a chosen "
        "spot.", "spot"
    },
    { EXPOSURE_METERING_CUSTOM,
        "The camera device's exposure metering is calculated from a custom "
        "metering table.", "custom"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraExposureMetering", variants);

  return gtype;
}

GType
gst_qmmfsrc_noise_reduction_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { NOISE_REDUCTION_OFF,
        "No noise reduction filter is applied.", "off"
    },
    { NOISE_REDUCTION_FAST,
        "TNR (Temoral Noise Reduction) Fast Mode.", "fast"
    },
    { NOISE_REDUCTION_HIGH_QUALITY,
        "TNR (Temoral Noise Reduction) High Quality Mode.", "hq"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraNoiseReduction", variants);

  return gtype;
}

GType
gst_qmmfsrc_capture_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { VIDEO_CAPTURE_MODE,
        "Snapshot requests will be submitted together with any existing video "
        "stream. Any request metadata passed as arguments will be ignored and "
        "instead the video stream metadata will be used.", "video"
    },
    { STILL_CAPTURE_MODE,
        "Snapshot requests will be interleaved with the requests for any "
        "existing video stream. In this mode any metadata passed as aguments "
        "will be used for the requests.", "still"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstImageCaptureMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_frc_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { FRAME_SKIP,
        "Control stream frame rate by frame skip", "frame-skip"
    },
    { CAPTURE_REQUEST,
        "Control stream frame rate by camera capture request", "capture-request"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstFrcMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_eis_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { EIS_OFF,
        "EIS is not applied.", "eis-off"
    },
    { EIS_ON_SINGLE_STREAM,
        "EIS is applied on first (non-snapshot) stream. Maximum number of "
        "each of preview, video and snapshot streams can be one.",
        "eis-on-single-stream"
    },
    { EIS_ON_DUAL_STREAM,
        "EIS is applied on both preview and video streams. Maximum number of "
        "each of preview, video and snapshot streams can be one.",
        "eis-on-dual-stream"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstEisMode", variants);

  return gtype;
}

#ifdef VHDR_MODES_ENABLE
GType
gst_qmmfsrc_vhdr_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { VHDR_OFF,
        "VHDR is disabled.", "off"
    },
    { SHDR_MODE_RAW,
        "Raw SHDR line interleaved mode with 2 frame. "
        "Use this mode for better performance on supporting sensor.",
        "shdr-raw"
    },
    { SHDR_MODE_YUV,
        "YUV SHDR virtual channel mode with 2 frames. "
        "Use this mode for better quality on supporting sensor. "
        "This mode may result in reduced framerate.", "shdr-yuv"
    },
    { SHDR_RAW_SWITCH_ENABLE,
        "Enable Raw SHDR switch. "
        "Use this mode for enabling shdr switch in camera backend based on lux value. "
        "The switch is between linear and Raw SHDR based on support in camera.",
        "raw-shdr-switch"
    },
    { SHDR_YUV_SWITCH_ENABLE,
        "Enable YUV SHDR switch. "
        "Use this mode for enabling shdr switch in camera backend based on lux value. "
        "The switch is between linear and YUV SHDR based on support in camera.",
        "yuv-shdr-switch"
    },
    { QBC_HDR_MODE_VIDEO,
        "Enable in-sensor HDR for video stream. "
        "This mode is applicable for sensor that support this feature only. ",
        "qbc-hdr-video"
    },
    { QBC_HDR_MODE_SNAPSHOT,
        "Enable in-sensor HDR for snapshot. "
        "When enabled camera backend decides to enable in-sensor hdr for snapshot"
        " based on the scene. This mode is applicable for sensor that support this"
        " feature only.", "qbc-hdr-snapshot"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstSHDRMode", variants);

  return gtype;
}
#endif // VHDR_MODES_ENABLE

GType
gst_qmmfsrc_rotate_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { ROTATE_NONE,
        "No Rotation ", "none"
    },
    { ROTATE_90CCW,
        "Rotate 90 degrees counter-clockwise", "90CCW"
    },
    { ROTATE_180CCW,
        "Rotate 180 degrees counter-clockwise", "180CCW"
    },
    { ROTATE_270CCW,
        "Rotate 270 degrees counter-clockwise", "270CCW"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstRotate", variants);

  return gtype;
}

GType
gst_qmmfsrc_cam_opmode_get_type (void)
{
  static GType gtype = 0;
  static const GFlagsValue variants[] = {
    { CAM_OPMODE_NONE,
        "Normal Camera Operation Mode", "none"
    },
    { CAM_OPMODE_FRAMESELECTION,
        "Camera Operation Mode Frame Selection", "frameselection"
    },
    { CAM_OPMODE_FASTSWITCH,
        "Camera Operation Mode Fast Switch", "fastswitch"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_flags_register_static ("GstFrameSelection", variants);

  return gtype;
}

GType
gst_qmmfsrc_pad_logical_stream_type_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue variants[GST_PAD_LOGICAL_STREAM_TYPE_MAX];
  gint i;
  gint index_num = (GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MAX -
      GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN + 1);

  if (gtype != 0)
    return gtype;

  // Physical camera index enum
  for (i = 0; i < index_num; ++i) {
    variants[i].value = i;
    variants[i].value_name = g_strdup_printf (
        "The stream uses specific physical camera with the index %d.", i);
    variants[i].value_nick = g_strdup_printf (
        "camera-index-%d", i);
  }

  // Stitch layout enum
  variants[i].value = GST_PAD_LOGICAL_STREAM_TYPE_SIDEBYSIDE;
  variants[i].value_name =
      "The stream uses all physical cameras and stitch images side by side.";
  variants[i++].value_nick = "sidebyside";

  variants[i].value = GST_PAD_LOGICAL_STREAM_TYPE_PANORAMA;
  variants[i].value_name =
      "The stream uses all physical cameras and stitch images to panorama.";
  variants[i++].value_nick = "panorama";

  variants[i].value = GST_PAD_LOGICAL_STREAM_TYPE_NONE;
  variants[i].value_name = "None";
  variants[i].value_nick = "none";


  gtype = g_enum_register_static ("GstQmmfSrcPadLogicalStreamType", variants);

  return gtype;
}

GType
gst_qmmfsrc_pad_activation_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_PAD_ACTIVATION_MODE_NORMAL,
        "Pad activation normal mode", "normal"
    },
    { GST_PAD_ACTIVATION_MODE_SIGNAL,
        "Pad activation by signal", "signal"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstPadActivationMode", variants);

  return gtype;
}

guchar
gst_qmmfsrc_control_mode_android_value (const gint value)
{
  static guint idx = 0;

  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(control_mode_map); ++idx) {
    if (control_mode_map[idx].value == value)
      return control_mode_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_control_mode (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(control_mode_map); ++idx) {
    if (control_mode_map[idx].venum == value)
      return control_mode_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_effect_mode_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(effect_mode_map); ++idx) {
    if (effect_mode_map[idx].value == value)
      return effect_mode_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_effect_mode (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(effect_mode_map); ++idx) {
    if (effect_mode_map[idx].venum == value)
      return effect_mode_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_scene_mode_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(scene_mode_map); ++idx) {
    if (scene_mode_map[idx].value == value)
      return scene_mode_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_scene_mode (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(scene_mode_map); ++idx) {
    if (scene_mode_map[idx].venum == value)
      return scene_mode_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_antibanding_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(antibanding_map); ++idx) {
    if (antibanding_map[idx].value == value)
      return antibanding_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_antibanding (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(antibanding_map); ++idx) {
    if (antibanding_map[idx].venum == value)
      return antibanding_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_exposure_mode_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(exposure_mode_map); ++idx) {
    if (exposure_mode_map[idx].value == value)
      return exposure_mode_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_exposure_mode (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(exposure_mode_map); ++idx) {
    if (exposure_mode_map[idx].venum == value)
      return exposure_mode_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_white_balance_mode_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(white_balance_mode_map); ++idx) {
    if (white_balance_mode_map[idx].value == value)
      return white_balance_mode_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_white_balance_mode (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(white_balance_mode_map); ++idx) {
    if (white_balance_mode_map[idx].venum == value)
      return white_balance_mode_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_focus_mode_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(focus_mode_map); ++idx) {
    if (focus_mode_map[idx].value == value)
      return focus_mode_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_focus_mode (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(focus_mode_map); ++idx) {
    if (focus_mode_map[idx].venum == value)
      return focus_mode_map[idx].value;
  }
  return UINT_MAX;
}

guchar
gst_qmmfsrc_noise_reduction_android_value (const gint value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(noise_reduction_map); ++idx) {
    if (noise_reduction_map[idx].value == value)
      return noise_reduction_map[idx].venum;
  }
  return UCHAR_MAX;
}

guint
gst_qmmfsrc_android_value_noise_reduction (const guchar value)
{
  static guint idx = 0;
  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(noise_reduction_map); ++idx) {
    if (noise_reduction_map[idx].venum == value)
      return noise_reduction_map[idx].value;
  }
  return UINT_MAX;
}

const char *
gst_qmmf_video_format_to_string (gint format)
{
  if (gst_video_format_to_string ((GstVideoFormat)format)) {
    return gst_video_format_to_string ((GstVideoFormat)format);
  }

  switch (format) {
    case GST_BAYER_FORMAT_BGGR:
      return "RGGB";
    case GST_BAYER_FORMAT_RGGB:
      return "RGGB";
    case GST_BAYER_FORMAT_GBRG:
      return "GBRG";
    case GST_BAYER_FORMAT_GRBG:
      return "GRBG";
    case GST_BAYER_FORMAT_MONO:
      return "MONO";
    default:
      return "unknown";
  }
}

GHashTable*
gst_qmmf_get_static_metas (void) {
  static GHashTable *static_metas = NULL;

  if (!static_metas)
    static_metas = g_hash_table_new (NULL, NULL);
  return static_metas;
}

void
gst_qmmf_cleanup_static_metas (void) {
  GHashTable *metas = gst_qmmf_get_static_metas ();

  if (metas) {
    GList *keys = g_hash_table_get_keys (metas);

    for (GList *key = keys; key; key = key->next) {
      ::qmmf::CameraMetadata *meta =
        static_cast<::qmmf::CameraMetadata *>(g_hash_table_lookup (metas, key->data));
      if (meta) {
        delete meta;
      }
    }
    g_list_free (keys);
    g_hash_table_destroy (metas);

    GST_INFO ("Cleaned up global static_metas hash table");
  }
}

guint
gst_qmmfsrc_check_sw_tnr_support () {
  guint tag_id = get_vendor_tag_by_name ("org.quic.camera.swcapabilities", "SWTNREnable");

  if (tag_id == 0) {
    GST_INFO ("SW_TNR not supported: tag_id is 0");
    return 0;
  }
  GST_INFO ("SW_TNR supported: tag_id = %u", tag_id);
  return 1;
}

guint
gst_qmmfsrc_check_eis_support () {
  guint tag_id = get_vendor_tag_by_name ("com.qti.node.supportedEISmodes","EISModes");

  if (tag_id == 0) {
    GST_INFO ("tag_id is 0");
    return 0;
  }
  GST_INFO ("tag_id is present");
  return 1;
}

guint
gst_qmmfsrc_get_max_fps ()
{
  guint max_fps = 0;
  gboolean has_high_speed = FALSE;

  GList *keys = g_hash_table_get_keys(gst_qmmf_get_static_metas());

  // If keys are NULL, return default max fps
  if (!keys) {
    GST_WARNING ("No static metadata available, using default max fps: %d", DEFAULT_MAX_FPS);
    return DEFAULT_MAX_FPS;
  }

  for (GList *key = keys; key; key = key->next) {
    ::qmmf::CameraMetadata *meta =
      static_cast<::qmmf::CameraMetadata *>(g_hash_table_lookup(gst_qmmf_get_static_metas(),
                                              key->data));

    // check if HIGH_SPEED_VIDEO_CONFIGURATIONS is available
    if (meta->exists(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS)) {
      has_high_speed = TRUE;
      auto entry = meta->find(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS);

      for (uint32_t i = 0; i < entry.count; i += 5) {
        guint fps = entry.data.i32[i + 3];

        if (fps > max_fps)
          max_fps = fps;
      }
    }
  }

  // If HIGH_SPEED not available, use AVAILABLE_TARGET_FPS_RANGES
  if (!has_high_speed) {
    for (GList *key = keys; key; key = key->next) {
      ::qmmf::CameraMetadata *meta =
        static_cast<::qmmf::CameraMetadata *>(g_hash_table_lookup(gst_qmmf_get_static_metas(),
                                                key->data));

      if (meta->exists(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)) {
        auto entry = meta->find(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);

        for (uint32_t i = 0; i < entry.count; i += 2) {
          guint fps = entry.data.i32[i + 1];

          if (fps > max_fps)
            max_fps = fps;
        }
      }
    }
  }

  g_list_free(keys);
  return max_fps;
}

void
gst_qmmfsrc_get_jpeg_resolution_range (GstQmmfSrcResolutionRange *range)
{
  if (!range)
    return;

  range->max_width = 0;
  range->max_height = 0;
  range->min_width = G_MAXUINT;
  range->min_height = G_MAXUINT;

  GList *keys = g_hash_table_get_keys(gst_qmmf_get_static_metas());

  // If keys are NULL, set default resolution values
  if (!keys) {
    GST_WARNING ("No static metadata available, using default resolution: %dx%d to %dx%d",
                 DEFAULT_MIN_WIDTH, DEFAULT_MIN_HEIGHT, DEFAULT_MAX_WIDTH, DEFAULT_MAX_HEIGHT);
    range->max_width = DEFAULT_MAX_WIDTH;
    range->max_height = DEFAULT_MAX_HEIGHT;
    range->min_width = DEFAULT_MIN_WIDTH;
    range->min_height = DEFAULT_MIN_HEIGHT;
    return;
  }

  for (GList *key = keys; key; key = key->next) {
    ::qmmf::CameraMetadata *meta =
      static_cast<::qmmf::CameraMetadata *>(g_hash_table_lookup(gst_qmmf_get_static_metas(),
                                              key->data));

    // Get maximum resolution from appropriate configuration
    camera_metadata_tag max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
#if defined(HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) && \
    defined(HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION)
    // Check if sensor supports ULTRA_HIGH_RESOLUTION
    if (meta->exists(ANDROID_REQUEST_AVAILABLE_CAPABILITIES)) {
      auto cap_entry = meta->find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
      for (uint32_t i = 0; i < cap_entry.count; i++) {
        if (cap_entry.data.u8[i] ==
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) {
          max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
          GST_INFO ("Sensor has ULTRA_HIGH_RESOLUTION_SENSOR capability");
          GST_INFO ("Using MAXIMUM_RESOLUTION configurations for max resolution");
          break;
        }
      }
    }
    if (max_config_tag == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS) {
      GST_INFO ("Using standard STREAM_CONFIGURATIONS for max resolution");
    }
#else
    GST_INFO ("Using standard STREAM_CONFIGURATIONS for max resolution");
#endif

    if (meta->exists(max_config_tag)) {
      auto entry = meta->find(max_config_tag);
      for (uint32_t i = 0; i < entry.count; i += 4) {
        if (HAL_PIXEL_FORMAT_BLOB == entry.data.i32[i]) {
          if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
              entry.data.i32[i+3]) {
            guint width = entry.data.i32[i+1];
            guint height = entry.data.i32[i+2];

            if (width > range->max_width)
              range->max_width = width;
            if (height > range->max_height)
              range->max_height = height;
          }
        }
      }
    }

    // Always get minimum resolution from standard STREAM_CONFIGURATIONS
    if (meta->exists(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS)) {
      auto entry = meta->find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
      for (uint32_t i = 0; i < entry.count; i += 4) {
        if (HAL_PIXEL_FORMAT_BLOB == entry.data.i32[i]) {
          if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
              entry.data.i32[i+3]) {
            guint width = entry.data.i32[i+1];
            guint height = entry.data.i32[i+2];

            if (width < range->min_width)
              range->min_width = width;
            if (height < range->min_height)
              range->min_height = height;
          }
        }
      }
    }
  }

  g_list_free(keys);

  GST_INFO ("JPEG resolution range: %ux%u to %ux%u\n",
            range->min_width, range->min_height,
            range->max_width, range->max_height);
}

void
gst_qmmfsrc_get_bayer_resolution_range (GstQmmfSrcResolutionRange *range)
{
  if (!range)
    return;

  range->max_width = 0;
  range->max_height = 0;
  range->min_width = G_MAXUINT;
  range->min_height = G_MAXUINT;

  GList *keys = g_hash_table_get_keys(gst_qmmf_get_static_metas());

  // If keys are NULL, set default resolution values
  if (!keys) {
    GST_WARNING ("No static metadata available, using default resolution: %dx%d to %dx%d",
                 DEFAULT_MIN_WIDTH, DEFAULT_MIN_HEIGHT, DEFAULT_MAX_WIDTH, DEFAULT_MAX_HEIGHT);
    range->max_width = DEFAULT_MAX_WIDTH;
    range->max_height = DEFAULT_MAX_HEIGHT;
    range->min_width = DEFAULT_MIN_WIDTH;
    range->min_height = DEFAULT_MIN_HEIGHT;
    return;
  }

  for (GList *key = keys; key; key = key->next) {
    ::qmmf::CameraMetadata *meta =
      static_cast<::qmmf::CameraMetadata *>(g_hash_table_lookup(gst_qmmf_get_static_metas(),
                                              key->data));

    // Get maximum resolution from appropriate configuration
    camera_metadata_tag max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
#if defined(HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) && \
    defined(HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION)
    // Check if sensor supports ULTRA_HIGH_RESOLUTION
    if (meta->exists(ANDROID_REQUEST_AVAILABLE_CAPABILITIES)) {
      auto cap_entry = meta->find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
      for (uint32_t i = 0; i < cap_entry.count; i++) {
        if (cap_entry.data.u8[i] ==
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) {
          max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
          GST_INFO ("Sensor has ULTRA_HIGH_RESOLUTION_SENSOR capability");
          GST_INFO ("Using MAXIMUM_RESOLUTION configurations for max resolution");
          break;
        }
      }
    }
    if (max_config_tag == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS) {
      GST_INFO ("Using standard STREAM_CONFIGURATIONS for max resolution");
    }
#else
    GST_INFO ("Using standard STREAM_CONFIGURATIONS for max resolution");
#endif

    if (meta->exists(max_config_tag)) {
      auto entry = meta->find(max_config_tag);

      for (uint32_t i = 0; i < entry.count; i += 4) {
        gint32 format = entry.data.i32[i];

        if (HAL_PIXEL_FORMAT_RAW8 == format ||
            HAL_PIXEL_FORMAT_RAW10 == format ||
            HAL_PIXEL_FORMAT_RAW12 == format ||
            HAL_PIXEL_FORMAT_RAW16 == format) {
          if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
              entry.data.i32[i+3]) {
            guint width = entry.data.i32[i+1];
            guint height = entry.data.i32[i+2];

            if (width > range->max_width)
              range->max_width = width;
            if (height > range->max_height)
              range->max_height = height;
          }
        }
      }
    }

    // Always get minimum resolution from standard STREAM_CONFIGURATIONS
    if (meta->exists(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS)) {
      auto entry = meta->find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);

      for (uint32_t i = 0; i < entry.count; i += 4) {
        gint32 format = entry.data.i32[i];

        if (HAL_PIXEL_FORMAT_RAW8 == format ||
            HAL_PIXEL_FORMAT_RAW10 == format ||
            HAL_PIXEL_FORMAT_RAW12 == format ||
            HAL_PIXEL_FORMAT_RAW16 == format) {
          if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
              entry.data.i32[i+3]) {
            guint width = entry.data.i32[i+1];
            guint height = entry.data.i32[i+2];

            if (width < range->min_width)
              range->min_width = width;
            if (height < range->min_height)
              range->min_height = height;
          }
        }
      }
    }
  }

  g_list_free(keys);

  GST_INFO ("Bayer resolution range: %ux%u to %ux%u",
            range->min_width, range->min_height,
            range->max_width, range->max_height);
}

void
gst_qmmfsrc_get_raw_resolution_range (GstQmmfSrcResolutionRange *range)
{
  if (!range)
    return;

  range->max_width = 0;
  range->max_height = 0;
  range->min_width = G_MAXUINT;
  range->min_height = G_MAXUINT;

  GList *keys = g_hash_table_get_keys(gst_qmmf_get_static_metas());

  // If keys are NULL, set default resolution values
  if (!keys) {
    GST_WARNING ("No static metadata available, using default resolution: %dx%d to %dx%d",
                 DEFAULT_MIN_WIDTH, DEFAULT_MIN_HEIGHT, DEFAULT_MAX_WIDTH, DEFAULT_MAX_HEIGHT);
    range->max_width = DEFAULT_MAX_WIDTH;
    range->max_height = DEFAULT_MAX_HEIGHT;
    range->min_width = DEFAULT_MIN_WIDTH;
    range->min_height = DEFAULT_MIN_HEIGHT;
    return;
  }

  for (GList *key = keys; key; key = key->next) {
    ::qmmf::CameraMetadata *meta =
      static_cast<::qmmf::CameraMetadata *>(g_hash_table_lookup(gst_qmmf_get_static_metas(),
                                              key->data));

    // Get maximum resolution from appropriate configuration
    camera_metadata_tag max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
#if defined(HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) && \
    defined(HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION)
    // Check if sensor supports ULTRA_HIGH_RESOLUTION
    if (meta->exists(ANDROID_REQUEST_AVAILABLE_CAPABILITIES)) {
      auto cap_entry = meta->find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
      for (uint32_t i = 0; i < cap_entry.count; i++) {
        if (cap_entry.data.u8[i] ==
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) {
          max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
          GST_INFO ("Sensor has ULTRA_HIGH_RESOLUTION_SENSOR capability");
          GST_INFO ("Using MAXIMUM_RESOLUTION configurations for max resolution");
          break;
        }
      }
    }
    if (max_config_tag == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS) {
      GST_INFO ("Using standard STREAM_CONFIGURATIONS for max resolution");
    }
#else
    GST_INFO ("Using standard STREAM_CONFIGURATIONS for max resolution");
#endif

    if (meta->exists(max_config_tag)) {
      auto entry = meta->find(max_config_tag);

      for (uint32_t i = 0; i < entry.count; i += 4) {
        if (HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED == entry.data.i32[i]) {
          if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
              entry.data.i32[i+3]) {
            guint width = entry.data.i32[i+1];
            guint height = entry.data.i32[i+2];

            if (width > range->max_width)
              range->max_width = width;
            if (height > range->max_height)
              range->max_height = height;
          }
        }
      }
    }

    // Always get minimum resolution from standard STREAM_CONFIGURATIONS
    if (meta->exists(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS)) {
      auto entry = meta->find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);

      for (uint32_t i = 0; i < entry.count; i += 4) {
        if (HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED == entry.data.i32[i]) {
          if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
              entry.data.i32[i+3]) {
            guint width = entry.data.i32[i+1];
            guint height = entry.data.i32[i+2];

            if (width < range->min_width)
              range->min_width = width;
            if (height < range->min_height)
              range->min_height = height;
          }
        }
      }
    }
  }

  g_list_free(keys);

  GST_INFO ("RAW resolution range: %ux%u to %ux%u",
            range->min_width, range->min_height,
            range->max_width, range->max_height);
}

gboolean
gst_qmmfsrc_check_format (PixFormat format) {
  GList *keys = g_hash_table_get_keys (gst_qmmf_get_static_metas ());

  // If keys are NULL, return TRUE to support all formats
  if (!keys) {
    GST_WARNING ("No static metadata available, supporting all formats");
    return TRUE;
  }

  for (GList *key = keys; key; key = key->next) {
    ::qmmf::CameraMetadata *meta =
      static_cast<::qmmf::CameraMetadata *> (g_hash_table_lookup (gst_qmmf_get_static_metas (),
                                             key->data));

    if (meta->exists (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS)) {
      auto entry =
          meta->find (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);

      for (guint j = 0; j < entry.count; j += 4) {
        if (entry.data.i32[j] == format) {
          g_list_free (keys);
          return TRUE;
        }
      }
    }
  }
  g_list_free (keys);
  return FALSE;
}

static void
parse_logical_cam_support (::qmmf::CameraMetadata *meta,
  gboolean &is_logical_cam) {
  camera_metadata_entry entry;

  entry = meta->find (ANDROID_REQUEST_AVAILABLE_CAPABILITIES);

  if (entry.count != 0) {
    guint8 *cap_req_keys = entry.data.u8;
    size_t i = 0;

    for (i = 0; i < entry.count; i++) {
      if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA ==
          cap_req_keys[i]) {
        is_logical_cam = TRUE;
        break;
      }
    }
  }
}

gboolean
gst_qmmfsrc_check_logical_cam_support () {
  gboolean is_logical_cam = FALSE;
  gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GList *keys = g_hash_table_get_keys (gst_qmmf_get_static_metas ());

    // If keys are NULL, return FALSE
    if (!keys) {
      GST_WARNING ("No static metadata available, logical camera not supported");
      g_once_init_leave (&catonce, 1);
      return FALSE;
    }

    for (GList *key = keys; key; key = key->next) {
      ::qmmf::CameraMetadata *meta =
        static_cast<::qmmf::CameraMetadata *> (g_hash_table_lookup (gst_qmmf_get_static_metas (),
                                               key->data));

      parse_logical_cam_support (meta, is_logical_cam);
    }
    g_list_free (keys);
    g_once_init_leave (&catonce, 1);
  }
  return is_logical_cam;
}

static void
gst_qmmf_buffer_pool_reset (GstBufferPool * pool, GstBuffer * buffer)
{
  GST_LOG_OBJECT (pool, "QMMF buffer reset %p", buffer);

  // Invoke the previously registered destroy notify function
  gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer),
      qmmf_buffer_qdata_quark (), NULL, NULL);

  gst_buffer_remove_all_memory (buffer);
  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);

  GST_BUFFER_POOL_CLASS (qmmf_pool_parent_class)->reset_buffer (pool, buffer);
}

GstBufferPool *
gst_qmmf_buffer_pool_new ()
{
  gboolean success = TRUE;
  GstStructure *config = NULL;
  GstQmmfBufferPool *pool;

  pool = (GstQmmfBufferPool *) g_object_new (GST_TYPE_QMMF_BUFFER_POOL, NULL);
  g_return_val_if_fail (pool != NULL, NULL);
  gst_object_ref_sink (pool);

  GST_LOG_OBJECT (pool, "New QMMF buffer pool %p", pool);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

  gst_buffer_pool_config_set_params (config, NULL, 0, 3, 0);

  success = gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);
  QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN (NULL, success == TRUE,
      gst_object_unref (pool), NULL, "Failed to set pool configuration!");

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_qmmf_buffer_pool_class_init (GstQmmfBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *pool = (GstBufferPoolClass *) klass;

  pool->reset_buffer = gst_qmmf_buffer_pool_reset;
}

static void
gst_qmmf_buffer_pool_init (GstQmmfBufferPool * pool)
{
  GST_DEBUG ("Initializing pool!");
}
