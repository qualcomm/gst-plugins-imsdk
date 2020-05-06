/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QMMFSRC_CONTEXT_H__
#define __GST_QMMFSRC_CONTEXT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_QMMF_CONTEXT_CAST(obj)   ((GstQmmfContext*)(obj))

typedef struct _GstQmmfContext GstQmmfContext;

enum
{
  PARAM_CAMERA_ID,
  PARAM_CAMERA_LDC,
  PARAM_CAMERA_SHDR,
  PARAM_CAMERA_EIS,
  PARAM_CAMERA_EFFECT_MODE,
  PARAM_CAMERA_SCENE_MODE,
  PARAM_CAMERA_ANTIBANDING_MODE,
  PARAM_CAMERA_AE_COMPENSATION,
  PARAM_CAMERA_AE_METERING_MODE,
  PARAM_CAMERA_AE_MODE,
  PARAM_CAMERA_AE_LOCK,
  PARAM_CAMERA_EXPOSURE_TIME,
  PARAM_CAMERA_EXPOSURE_TABLE,
  PARAM_CAMERA_AWB_MODE,
  PARAM_CAMERA_AWB_LOCK,
  PARAM_CAMERA_SLAVE,
  PARAM_CAMERA_AF_MODE,
  PARAM_CAMERA_IR_MODE,
  PARAM_CAMERA_ADRC,
  PARAM_CAMERA_ISO_MODE,
  PARAM_CAMERA_NOISE_REDUCTION,
  PARAM_CAMERA_ZOOM,
  PARAM_CAMERA_DEFOG_TABLE,
  PARAM_CAMERA_LOCAL_TONE_MAPPING,
  PARAM_CAMERA_NOISE_REDUCTION_TUNING,
};

GST_API GstQmmfContext *
gst_qmmf_context_new (void);

GST_API void
gst_qmmf_context_free (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_open (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_close (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_create_session (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_delete_session (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_create_stream (GstQmmfContext * context, GstPad * pad);

GST_API gboolean
gst_qmmf_context_delete_stream (GstQmmfContext * context, GstPad * pad);

GST_API gboolean
gst_qmmf_context_start_session (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_stop_session (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_pause_session (GstQmmfContext * context);

GST_API gboolean
gst_qmmf_context_capture_image (GstQmmfContext * context, GstPad * pad);

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
