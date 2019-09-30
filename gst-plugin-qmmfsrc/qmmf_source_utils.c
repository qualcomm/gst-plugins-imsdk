/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#include "qmmf_source_utils.h"

#include <system/camera_metadata_tags.h>

#define QMMFSRC_PROPERTY_MAP_SIZE(MAP) (sizeof(MAP)/sizeof(MAP[0]))

typedef struct _PropAndroidEnum PropAndroidEnum;

struct _PropAndroidEnum
{
  gint value;
  guchar venum;
};

GType
gst_qmmfsrc_effect_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    {EFFECT_MODE_OFF,
        "No color effect will be applied.", "off"},
    {EFFECT_MODE_MONO,
        "A 'monocolor' effect where the image is mapped into a single color.",
        "mono"},
    {EFFECT_MODE_NEGATIVE,
        "A 'photo-negative' effect where the image's colors are inverted.",
        "negative"},
    { EFFECT_MODE_SOLARIZE,
        "A 'solarisation' effect (Sabattier effect) where the image is wholly "
        "or partially reversed in tone.", "solarize"},
    { EFFECT_MODE_SEPIA,
        "A 'sepia' effect where the image is mapped into warm gray, red, and "
        "brown tones.", "sepia"},
    { EFFECT_MODE_POSTERIZE,
        "A 'posterization' effect where the image uses discrete regions of "
        "tone rather than a continuous gradient of tones.", "posterize"},
    { EFFECT_MODE_WHITEBOARD,
        "A 'whiteboard' effect where the image is typically displayed as "
        "regions of white, with black or grey details.", "whiteboard"},
    { EFFECT_MODE_BLACKBOARD,
        "A 'blackboard' effect where the image is typically displayed as "
        "regions of black, with white or grey details.", "blackboard"},
    {EFFECT_MODE_AQUA,
        "An 'aqua' effect where a blue hue is added to the image.", "aqua"},
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
    {SCENE_MODE_DISABLED,
        "Indicates that no scene modes are set.", "disabled"},
    {SCENE_MODE_ACTION,
        "Optimized for photos of quickly moving objects.", "action"},
    {SCENE_MODE_PORTRAIT,
        "Optimized for still photos of people.", "portrait"},
    { SCENE_MODE_LANDSCAPE,
        "Optimized for photos of distant macroscopic objects.", "landscape"},
    { SCENE_MODE_NIGHT,
        "Optimized for low-light settings.", "night"},
    { SCENE_MODE_NIGHT_PORTRAIT,
        "Optimized for still photos of people in low-light settings.",
        "night-portrait"},
    { SCENE_MODE_THEATRE,
        "Optimized for dim, indoor settings where flash must remain off.",
        "theatre"},
    { SCENE_MODE_BEACH,
        "Optimized for bright, outdoor beach settings.", "beach"},
    {SCENE_MODE_SNOW,
        "Optimized for bright, outdoor settings containing snow.", "snow"},
    {SCENE_MODE_SUNSET,
        "Optimized for scenes of the setting sun.", "sunset"},
    {SCENE_MODE_STEADYPHOTO,
        "Optimized to avoid blurry photos due to small amounts of device "
        "motion (for example: due to hand shake).", "steady-photo"},
    {SCENE_MODE_FIREWORKS,
        "Optimized for nighttime photos of fireworks.", "fireworks"},
    {SCENE_MODE_SPORTS,
        "Optimized for photos of quickly moving people.", "sports"},
    {SCENE_MODE_PARTY,
        "Optimized for dim, indoor settings with multiple moving people.",
        "party"},
    {SCENE_MODE_CANDLELIGHT,
        "Optimized for dim settings where the main light source is a candle.",
        "candlelight"},
    {SCENE_MODE_HDR,
        "Turn on a device-specific high dynamic range (HDR) mode.", "hdr"},
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
    {ANTIBANDING_MODE_OFF,
        "The camera device will not adjust exposure duration to avoid banding "
        "problems.", "off"},
    {ANTIBANDING_MODE_50HZ,
        "The camera device will adjust exposure duration to avoid banding "
        "problems with 50Hz illumination sources.", "50hz"},
    {ANTIBANDING_MODE_60HZ,
        "The camera device will adjust exposure duration to avoid banding "
        "problems with 60Hz illumination sources.", "60hz"},
    { ANTIBANDING_MODE_AUTO,
        "The camera device will automatically adapt its antibanding routine "
        "to the current illumination condition.", "auto"},
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstAntibandingMode", variants);

  return gtype;
}

GType
gst_qmmfsrc_awb_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    {AWB_MODE_OFF,
        "The camera device's auto-white balance routine is disabled.", "off"},
    {AWB_MODE_AUTO,
        "The camera device's auto-white balance routine is active.", "auto"},
    {AWB_MODE_SHADE,
        "The camera device uses shade light as the assumed scene illumination "
        "for white balance.", "shade"},
    { AWB_MODE_INCANDESCENT,
        "The camera device uses incandescent light as the assumed scene "
        "illumination for white balance.", "incandescent"},
    {AWB_MODE_FLUORESCENT,
        "The camera device uses fluorescent light as the assumed scene "
        "illumination for white balance.", "fluorescent"},
    {AWB_MODE_WARM_FLUORESCENT,
        "The camera device uses warm fluorescent light as the assumed scene "
        "illumination for white balance.", "warm-fluorescent"},
    { AWB_MODE_DAYLIGHT,
        "The camera device uses daylight light as the assumed scene "
        "illumination for white balance.", "daylight"},
    {AWB_MODE_CLOUDY_DAYLIGHT,
        "The camera device uses cloudy daylight light as the assumed scene "
        "illumination for white balance.", "cloudy-daylight"},
    {AWB_MODE_TWILIGHT,
        "The camera device uses twilight light as the assumed scene "
        "illumination for white balance.", "twilight"},
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraAWBMode", variants);

  return gtype;
}

guchar
gst_qmmfsrc_effect_mode_android_value (const guint value)
{
  static guint idx = 0;
  static const PropAndroidEnum map[] = {
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

  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(map); ++idx) {
    if (map[idx].value == value)
      return map[idx].venum;
  }
  return (-1);
}

guchar
gst_qmmfsrc_scene_mode_android_value (const guint value)
{
  static guint idx = 0;
  static const PropAndroidEnum map[] = {
      {SCENE_MODE_DISABLED, ANDROID_CONTROL_SCENE_MODE_DISABLED},
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

  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(map); ++idx) {
    if (map[idx].value == value)
      return map[idx].venum;
  }
  return UCHAR_MAX;
}

guchar
gst_qmmfsrc_antibanding_android_value (const guint value)
{
  static guint idx = 0;
  static const PropAndroidEnum map[] = {
      {ANTIBANDING_MODE_OFF, ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF},
      {ANTIBANDING_MODE_50HZ, ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ},
      {ANTIBANDING_MODE_60HZ, ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ},
      {ANTIBANDING_MODE_AUTO, ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO},
  };

  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(map); ++idx) {
    if (map[idx].value == value)
      return map[idx].venum;
  }
  return UCHAR_MAX;
}

guchar
gst_qmmfsrc_awb_mode_android_value (const guint value)
{
  static guint idx = 0;
  static const PropAndroidEnum map[] = {
      {AWB_MODE_OFF, ANDROID_CONTROL_AWB_MODE_OFF},
      {AWB_MODE_AUTO, ANDROID_CONTROL_AWB_MODE_AUTO},
      {AWB_MODE_SHADE, ANDROID_CONTROL_AWB_MODE_SHADE},
      {AWB_MODE_INCANDESCENT, ANDROID_CONTROL_AWB_MODE_INCANDESCENT},
      {AWB_MODE_FLUORESCENT, ANDROID_CONTROL_AWB_MODE_FLUORESCENT},
      {AWB_MODE_WARM_FLUORESCENT, ANDROID_CONTROL_AWB_MODE_WARM_FLUORESCENT},
      {AWB_MODE_DAYLIGHT, ANDROID_CONTROL_AWB_MODE_DAYLIGHT},
      {AWB_MODE_CLOUDY_DAYLIGHT, ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT},
      {AWB_MODE_TWILIGHT, ANDROID_CONTROL_AWB_MODE_TWILIGHT},
  };

  for (idx = 0; idx < QMMFSRC_PROPERTY_MAP_SIZE(map); ++idx) {
    if (map[idx].value == value)
      return map[idx].venum;
  }
  return UCHAR_MAX;
}
