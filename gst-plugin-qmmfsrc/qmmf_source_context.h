/*
* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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


#ifndef __GST_QMMFSRC_CONTEXT_H__
#define __GST_QMMFSRC_CONTEXT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_QMMF_CONTEXT_CAST(obj)   ((GstQmmfContext*)(obj))

typedef struct _GstQmmfContext            GstQmmfContext;
typedef struct _GstQmmfLogicalCamInfo     GstQmmfLogicalCamInfo;
typedef struct _GstQmmfCameraSwitchInfo   GstQmmfCameraSwitchInfo;

typedef void (*GstCameraEventCb) (guint event, gpointer userdata);
typedef void (*GstCameraMetaCb) (gint camera_id, gconstpointer metadata,
    gboolean isurgent, gpointer userdata);

enum {
  EVENT_UNKNOWN,
  EVENT_SERVICE_DIED,
  EVENT_CAMERA_ERROR,
  EVENT_CAMERA_OPENED,
  EVENT_CAMERA_CLOSING,
  EVENT_CAMERA_CLOSED,
  EVENT_FRAME_ERROR,
  EVENT_METADATA_ERROR,
};

enum
{
  PARAM_CAMERA_ID,
  PARAM_CAMERA_SLAVE,
  PARAM_CAMERA_LDC,
  PARAM_CAMERA_LCAC,
  PARAM_CAMERA_EIS,
#ifndef VHDR_MODES_ENABLE
  PARAM_CAMERA_SHDR,
#else
  PARAM_CAMERA_VHDR,
#endif // VHDR_MODES_ENABLE
  PARAM_CAMERA_ADRC,
  PARAM_CAMERA_CONTROL_MODE,
  PARAM_CAMERA_EFFECT_MODE,
  PARAM_CAMERA_SCENE_MODE,
  PARAM_CAMERA_ANTIBANDING_MODE,
  PARAM_CAMERA_SHARPNESS,
  PARAM_CAMERA_CONTRAST,
  PARAM_CAMERA_SATURATION,
  PARAM_CAMERA_ISO_MODE,
  PARAM_CAMERA_ISO_VALUE,
  PARAM_CAMERA_EXPOSURE_MODE,
  PARAM_CAMERA_EXPOSURE_LOCK,
  PARAM_CAMERA_EXPOSURE_METERING,
  PARAM_CAMERA_EXPOSURE_COMPENSATION,
  PARAM_CAMERA_EXPOSURE_TIME,
  PARAM_CAMERA_EXPOSURE_TABLE,
  PARAM_CAMERA_WHITE_BALANCE_MODE,
  PARAM_CAMERA_WHITE_BALANCE_LOCK,
  PARAM_CAMERA_MANUAL_WB_SETTINGS,
  PARAM_CAMERA_FOCUS_MODE,
  PARAM_CAMERA_NOISE_REDUCTION,
  PARAM_CAMERA_NOISE_REDUCTION_TUNING,
  PARAM_CAMERA_ZOOM,
  PARAM_CAMERA_DEFOG_TABLE,
  PARAM_CAMERA_LOCAL_TONE_MAPPING,
  PARAM_CAMERA_IR_MODE,
  PARAM_CAMERA_ACTIVE_SENSOR_SIZE,
  PARAM_CAMERA_SENSOR_MODE,
  PARAM_CAMERA_VIDEO_METADATA,
  PARAM_CAMERA_IMAGE_METADATA,
  PARAM_CAMERA_STATIC_METADATA,
  PARAM_CAMERA_SESSION_METADATA,
  PARAM_CAMERA_FRC_MODE,
  PARAM_CAMERA_IFE_DIRECT_STREAM,
  PARAM_CAMERA_MULTI_CAM_EXPOSURE_TIME,
  PARAM_CAMERA_STANDBY,
  PARAM_CAMERA_OPERATION_MODE,
  PARAM_CAMERA_INPUT_ROI,
  PARAM_CAMERA_INPUT_ROI_INFO,
  PARAM_CAMERA_PHYISICAL_CAMERA_SWITCH,
  PARAM_CAMERA_SUPER_FRAMERATE,
#ifdef FEATURE_OFFLINE_IFE_SUPPORT
  PARAM_CAMERA_MULTICAMERA_HINT,
#endif // FEATURE_OFFLINE_IFE_SUPPORT
  PARAM_CAMERA_SW_TNR,
  PARAM_CAMERA_STATIC_METADATAS,
};

GST_API GstQmmfContext *
gst_qmmf_context_new (GstCameraEventCb eventcb, GstCameraMetaCb metacb,
                      gpointer userdata);

GST_API void
gst_qmmf_context_free (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_open (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_close (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_create_video_stream (GstQmmfContext * context, GstPad * pad);

GST_API gboolean
gst_qmmf_context_delete_video_stream (GstQmmfContext * context, GstPad * pad);

GST_API gboolean
gst_qmmf_context_create_image_stream (GstQmmfContext * context, GstPad * pad);

GST_API gboolean
gst_qmmf_context_delete_image_stream (GstQmmfContext * context, GstPad * pad,
                                      gboolean cache);

GST_API gboolean
gst_qmmf_context_start_video_streams (GstQmmfContext * context, GArray * ids);

GST_API gboolean
gst_qmmf_context_stop_video_streams (GstQmmfContext * context, GArray * ids);

GST_API gboolean
gst_qmmf_context_capture_image (GstQmmfContext * context,
                                GHashTable * srcpads,
                                GList * imgindexes,
                                guint imgtype,
                                guint n_images,
                                GPtrArray * metas);

GST_API void
gst_qmmf_context_set_camera_param (GstQmmfContext * context, guint param_id,
                                   const GValue * value);

GST_API void
gst_qmmf_context_get_camera_param (GstQmmfContext * context, guint param_id,
                                   GValue * value);

GST_API void
gst_qmmf_context_update_video_param (GstPad * pad, GParamSpec * pspec,
                                     GstQmmfContext * context);

G_END_DECLS

#endif // __GST_QMMFSRC_CONTEXT_H__
