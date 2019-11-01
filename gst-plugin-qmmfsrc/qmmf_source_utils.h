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
#define GST_TYPE_QMMFSRC_AWB_MODE (gst_qmmfsrc_awb_mode_get_type())

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
  AWB_MODE_OFF,
  AWB_MODE_AUTO,
  AWB_MODE_SHADE,
  AWB_MODE_INCANDESCENT,
  AWB_MODE_FLUORESCENT,
  AWB_MODE_WARM_FLUORESCENT,
  AWB_MODE_DAYLIGHT,
  AWB_MODE_CLOUDY_DAYLIGHT,
  AWB_MODE_TWILIGHT,
};

GType gst_qmmfsrc_effect_mode_get_type (void);

GType gst_qmmfsrc_scene_mode_get_type (void);

GType gst_qmmfsrc_antibanding_get_type (void);

GType gst_qmmfsrc_awb_mode_get_type (void);

guchar gst_qmmfsrc_effect_mode_android_value (const guint value);

guchar gst_qmmfsrc_scene_mode_android_value (const guint value);

guchar gst_qmmfsrc_antibanding_android_value (const guint value);

guchar gst_qmmfsrc_awb_mode_android_value (const guint value);

G_END_DECLS

#endif // __GST_QMMFSRC_UTILS_H__
