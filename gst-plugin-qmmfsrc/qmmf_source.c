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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qmmf_source.h"

#include <stdio.h>

#include <gst/gstplugin.h>
#include <gst/gstpadtemplate.h>
#include <gst/gstelementfactory.h>
#include <gst/allocators/allocators.h>
#include <gst/video/video-utils.h>
#ifdef ENABLE_RUNTIME_PARSER
#include <gst/utils/runtime-flags-parser-c-api.h>
#endif // ENABLE_RUNTIME_PARSER

#include "qmmf_source_utils.h"
#include "qmmf_source_image_pad.h"
#include "qmmf_source_video_pad.h"

// Declare static GstDebugCategory variable for qmmfsrc.
GST_DEBUG_CATEGORY_STATIC (qmmfsrc_debug);
#define GST_CAT_DEFAULT qmmfsrc_debug

#define DEFAULT_PROP_CAMERA_ID                        0
#define DEFAULT_PROP_CAMERA_SLAVE                     FALSE
#define DEFAULT_PROP_CAMERA_LDC_MODE                  FALSE
#define DEFAULT_PROP_CAMERA_LCAC_MODE                 FALSE
#ifndef EIS_MODES_ENABLE
#define DEFAULT_PROP_CAMERA_EIS_MODE                  FALSE
#else
#define DEFAULT_PROP_CAMERA_EIS_MODE                  EIS_OFF
#endif // EIS_MODES_ENABLE
#ifndef VHDR_MODES_ENABLE
#define DEFAULT_PROP_CAMERA_SHDR_MODE                 FALSE
#else
#define DEFAULT_PROP_CAMERA_VHDR_MODE                 VHDR_OFF
#endif // VHDR_MODES_ENABLE
#define DEFAULT_PROP_CAMERA_ADRC                      FALSE
#define DEFAULT_PROP_CAMERA_CONTROL_MODE              CONTROL_MODE_AUTO
#define DEFAULT_PROP_CAMERA_EFFECT_MODE               EFFECT_MODE_OFF
#define DEFAULT_PROP_CAMERA_SCENE_MODE                SCENE_MODE_FACE_PRIORITY
#define DEFAULT_PROP_CAMERA_ANTIBANDING               ANTIBANDING_MODE_AUTO
#define DEFAULT_PROP_CAMERA_SHARPNESS                 2
#define DEFAULT_PROP_CAMERA_CONTRAST                  5
#define DEFAULT_PROP_CAMERA_SATURATION                5
#define DEFAULT_PROP_CAMERA_ISO_MODE                  ISO_MODE_AUTO
#define DEFAULT_PROP_CAMERA_ISO_VALUE                 800
#define DEFAULT_PROP_CAMERA_EXPOSURE_MODE             EXPOSURE_MODE_AUTO
#define DEFAULT_PROP_CAMERA_EXPOSURE_LOCK             FALSE
#define DEFAULT_PROP_CAMERA_EXPOSURE_METERING         EXPOSURE_METERING_AVERAGE
#define DEFAULT_PROP_CAMERA_EXPOSURE_COMPENSATION     0
#define DEFAULT_PROP_CAMERA_EXPOSURE_TABLE            NULL
#define DEFAULT_PROP_CAMERA_EXPOSURE_TIME             33333333
#define DEFAULT_PROP_CAMERA_WHITE_BALANCE_MODE        WHITE_BALANCE_MODE_AUTO
#define DEFAULT_PROP_CAMERA_WHITE_BALANCE_LOCK        FALSE
#define DEFAULT_PROP_CAMERA_MANUAL_WB_SETTINGS        NULL
#define DEFAULT_PROP_CAMERA_FOCUS_MODE                FOCUS_MODE_OFF
#define DEFAULT_PROP_CAMERA_NOISE_REDUCTION           NOISE_REDUCTION_FAST
#define DEFAULT_PROP_CAMERA_DEFOG_TABLE               NULL
#define DEFAULT_PROP_CAMERA_LOCAL_TONE_MAPPING        NULL
#define DEFAULT_PROP_CAMERA_NOISE_REDUCTION_TUNING    NULL
#define DEFAULT_PROP_CAMERA_IR_MODE                   IR_MODE_OFF
#define DEFAULT_PROP_CAMERA_SENSOR_MODE               -1
#define DEFAULT_PROP_CAMERA_FRC_MODE                  FRAME_SKIP
#define DEFAULT_PROP_CAMERA_IFE_DIRECT_STREAM         FALSE
#define DEFAULT_PROP_CAMERA_OPERATION_MODE            CAM_OPMODE_NONE
#define DEFAULT_PROP_CAMERA_MULTI_ROI                 FALSE
#define DEFAULT_PROP_CAMERA_PHYSICAL_CAMERA_SWITCH    -1
#define DEFAULT_PROP_CAMERA_PAD_ACTIVAION_MODE        GST_PAD_ACTIVATION_MODE_NORMAL
#ifdef FEATURE_OFFLINE_IFE_SUPPORT
#define DEFAULT_PROP_CAMERA_MULTICAMERA_HINT          FALSE
#endif // FEATURE_OFFLINE_IFE_SUPPORT
#define DEFAULT_PROP_CAMERA_SW_TNR                    FALSE

static void gst_qmmfsrc_child_proxy_init (gpointer g_iface, gpointer data);

// Declare qmmfsrc_class_init() and qmmfsrc_init() functions, implement
// qmmfsrc_get_type() function and set qmmfsrc_parent_class variable.
G_DEFINE_TYPE_WITH_CODE (GstQmmfSrc, qmmfsrc, GST_TYPE_ELEMENT,
     G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY, gst_qmmfsrc_child_proxy_init));
#define parent_class qmmfsrc_parent_class

enum
{
  SIGNAL_CAPTURE_IMAGE,
  SIGNAL_CANCEL_CAPTURE,
  SIGNAL_RESULT_METADATA,
  SIGNAL_URGENT_METADATA,
  SIGNAL_VIDEO_PADS_ACTIVATION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

enum
{
  PROP_0,
  PROP_CAMERA_ID,
  PROP_CAMERA_SLAVE,
  PROP_CAMERA_LDC,
  PROP_CAMERA_LCAC,
  PROP_CAMERA_EIS,
#ifndef VHDR_MODES_ENABLE
  PROP_CAMERA_SHDR,
#else
  PROP_CAMERA_VHDR,
#endif // VHDR_MODES_ENABLE
  PROP_CAMERA_ADRC,
  PROP_CAMERA_CONTROL_MODE,
  PROP_CAMERA_EFFECT_MODE,
  PROP_CAMERA_SCENE_MODE,
  PROP_CAMERA_ANTIBANDING_MODE,
  PROP_CAMERA_SHARPNESS,
  PROP_CAMERA_CONTRAST,
  PROP_CAMERA_SATURATION,
  PROP_CAMERA_ISO_MODE,
  PROP_CAMERA_ISO_VALUE,
  PROP_CAMERA_EXPOSURE_MODE,
  PROP_CAMERA_EXPOSURE_LOCK,
  PROP_CAMERA_EXPOSURE_METERING,
  PROP_CAMERA_EXPOSURE_COMPENSATION,
  PROP_CAMERA_EXPOSURE_TIME,
  PROP_CAMERA_EXPOSURE_TABLE,
  PROP_CAMERA_WHITE_BALANCE_MODE,
  PROP_CAMERA_WHITE_BALANCE_LOCK,
  PROP_CAMERA_MANUAL_WB_SETTINGS,
  PROP_CAMERA_FOCUS_MODE,
  PROP_CAMERA_NOISE_REDUCTION,
  PROP_CAMERA_NOISE_REDUCTION_TUNING,
  PROP_CAMERA_ZOOM,
  PROP_CAMERA_DEFOG_TABLE,
  PROP_CAMERA_LOCAL_TONE_MAPPING,
  PROP_CAMERA_IR_MODE,
  PROP_CAMERA_ACTIVE_SENSOR_SIZE,
  PROP_CAMERA_SENSOR_MODE,
  PROP_CAMERA_VIDEO_METADATA,
  PROP_CAMERA_IMAGE_METADATA,
  PROP_CAMERA_STATIC_METADATA,
  PROP_CAMERA_SESSION_METADATA,
  PROP_CAMERA_FRC_MODE,
  PROP_CAMERA_IFE_DIRECT_STREAM,
  PROP_CAMERA_MULTI_CAM_EXPOSURE_TIME,
  PROP_CAMERA_OPERATION_MODE,
  PROP_CAMERA_INPUT_ROI,
  PROP_CAMERA_INPUT_ROI_INFO,
  PROP_CAMERA_PHYSICAL_CAMERA_SWITCH,
  PROP_CAMERA_PAD_ACTIVATION_MODE,
#ifdef FEATURE_OFFLINE_IFE_SUPPORT
  PROP_CAMERA_MULTICAMERA_HINT,
#endif // FEATURE_OFFLINE_IFE_SUPPORT
  PROP_CAMERA_SW_TNR,
  PROP_CAMERA_STATIC_METADATAS,
};

#ifdef ENABLE_RUNTIME_PARSER

#define CAPS_SIZE 255

static GstStaticPadTemplate qmmfsrc_video_src_template;
static GstStaticPadTemplate qmmfsrc_image_src_template;

static void
qmmfsrc_deinit_src_templates ()
{
  if (NULL != qmmfsrc_video_src_template.static_caps.string) {
    g_free (qmmfsrc_video_src_template.static_caps.string);
    qmmfsrc_video_src_template.static_caps.string = NULL;
  }

  if (NULL != qmmfsrc_image_src_template.static_caps.string) {
    g_free (qmmfsrc_image_src_template.static_caps.string);
    qmmfsrc_image_src_template.static_caps.string = NULL;
  }
}

static void
qmmfsrc_init_src_templates ()
{
  void* qmmfsrc_parser = get_qmmfsrc_parser ();

  gint video_max_width  = get_flag_as_int (qmmfsrc_parser, "GST_VIDEO_MAX_WIDTH");
  gint video_max_height = get_flag_as_int (qmmfsrc_parser, "GST_VIDEO_MAX_HEIGHT");
  gint video_max_fps    = get_flag_as_int (qmmfsrc_parser, "GST_VIDEO_MAX_FPS");

  gchar* common_video_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (common_video_caps, CAPS_SIZE,
      "width = (int) [ 16, " "%d" " ], "
      "height = (int) [ 16," "%d" " ], "
      "framerate = (fraction) [ 0/1, " "%d" " ] ; ",
      video_max_width,
      video_max_height,
      video_max_fps
  );

  gchar* video_jpeg_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (video_jpeg_caps, CAPS_SIZE,
      "image/jpeg,"
      "%s",
      common_video_caps
  );

  gchar* video_raw_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (video_raw_caps, CAPS_SIZE,
      "video/x-raw, "
      "format = (string) "
      "{ NV12, NV16"
#ifdef GST_VIDEO_YUY2_FORMAT_ENABLE
      ", YUY2"
#endif // GST_VIDEO_YUY2_FORMAT_ENABLE
#ifdef GST_VIDEO_UYVY_FORMAT_ENABLE
      ", UYVY"
#endif // GST_VIDEO_UYVY_FORMAT_ENABLE
#ifdef GST_VIDEO_P010_10LE_FORMAT_ENABLE
      ", P010_10LE"
#endif // GST_VIDEO_P010_10LE_FORMAT_ENABLE
#ifdef GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
      ", NV12_10LE32"
#endif // GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
      " }" ", "
      "%s",
      common_video_caps
  );

  gchar* video_raw_caps_with_features = (gchar *) g_malloc (
      CAPS_SIZE * sizeof (gchar));

  snprintf (video_raw_caps_with_features, CAPS_SIZE,
      "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "
      "format = (string) "
      "{ NV12, NV16"
#ifdef GST_VIDEO_YUY2_FORMAT_ENABLE
      ", YUY2"
#endif // GST_VIDEO_YUY2_FORMAT_ENABLE
#ifdef GST_VIDEO_UYVY_FORMAT_ENABLE
      ", UYVY"
#endif // GST_VIDEO_UYVY_FORMAT_ENABLE
#ifdef GST_VIDEO_P010_10LE_FORMAT_ENABLE
      ", P010_10LE"
#endif // GST_VIDEO_P010_10LE_FORMAT_ENABLE
#ifdef GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
      ", NV12_10LE32"
#endif // GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
      " }" ", "
      "%s",
      common_video_caps
  );

  gchar* video_bayer_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (video_bayer_caps, CAPS_SIZE,
      "video/x-bayer, "
      "format = (string) " "{ bggr, rggb, gbrg, grbg, mono }" ", "
      "bpp = (string) " "{ 8, 10, 12, 16 }" ", "
      "%s",
      common_video_caps
  );

  g_free (common_video_caps);

  const gchar* qmmfsrc_all_video_caps = (const gchar *) g_malloc (
      4 * CAPS_SIZE * sizeof (gchar));

  snprintf ((gchar *) qmmfsrc_all_video_caps, 4 * CAPS_SIZE,
      "%s"
      "%s"
      "%s"
      "%s",
      video_jpeg_caps,
      video_raw_caps,
      video_raw_caps_with_features,
      video_bayer_caps
  );

  g_free (video_jpeg_caps);
  g_free (video_raw_caps);
  g_free (video_raw_caps_with_features);
  g_free (video_bayer_caps);

  GstStaticCaps static_video_caps = {
    .caps = NULL,
    .string = qmmfsrc_all_video_caps,
    ._gst_reserved = { NULL }
  };

  qmmfsrc_video_src_template.name_template = "video_%u";
  qmmfsrc_video_src_template.direction = GST_PAD_SRC;
  qmmfsrc_video_src_template.presence = GST_PAD_REQUEST;
  qmmfsrc_video_src_template.static_caps = static_video_caps;

  gint image_max_width  = get_flag_as_int (qmmfsrc_parser, "GST_IMAGE_MAX_WIDTH");
  gint image_max_height = get_flag_as_int (qmmfsrc_parser, "GST_IMAGE_MAX_HEIGHT");

  gchar* common_image_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (common_image_caps, CAPS_SIZE,
      "width = (int) [ 16, " "%d" " ], "
      "height = (int) [ 16," "%d" " ], "
      "framerate = (fraction) [ 0/1, 30/1 ] ; ",
      image_max_width,
      image_max_height
  );

  gchar* image_jpeg_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (image_jpeg_caps, CAPS_SIZE,
      "image/jpeg,"
      "%s",
      common_image_caps
  );

  gchar* image_raw_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (image_raw_caps, CAPS_SIZE,
      "video/x-raw, "
      "format = (string) "
      "{ NV21"
#ifdef GST_IMAGE_NV12_FORMAT_ENABLE
      ", NV12"
#endif // GST_IMAGE_NV12_FORMAT_ENABLE
      " }" ", "
      "%s",
      common_image_caps
  );

  gchar* image_raw_caps_with_features = (gchar *) g_malloc (
      CAPS_SIZE * sizeof (gchar));

  snprintf (image_raw_caps_with_features, CAPS_SIZE,
      "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "
      "format = (string) "
      "{ NV21"
#ifdef GST_IMAGE_NV12_FORMAT_ENABLE
      ", NV12"
#endif // GST_IMAGE_NV12_FORMAT_ENABLE
      " }" ", "
      "%s",
      common_image_caps
  );

  gchar* image_bayer_caps = (gchar *) g_malloc (CAPS_SIZE * sizeof (gchar));

  snprintf (image_bayer_caps, CAPS_SIZE,
      "video/x-bayer, "
      "format = (string) " "{ bggr, rggb, gbrg, grbg, mono }" ", "
      "bpp = (string) " "{ 8, 10, 12, 16 }" ", "
      "%s",
      common_image_caps
  );

  g_free (common_image_caps);

  const gchar* qmmfsrc_all_image_caps = (const gchar *) g_malloc (
      4 * CAPS_SIZE * sizeof (gchar));

  snprintf ((gchar *) qmmfsrc_all_image_caps, 4 * CAPS_SIZE,
      "%s"
      "%s"
      "%s"
      "%s",
      image_jpeg_caps,
      image_raw_caps,
      image_raw_caps_with_features,
      image_bayer_caps
  );

  g_free (image_jpeg_caps);
  g_free (image_raw_caps);
  g_free (image_raw_caps_with_features);
  g_free (image_bayer_caps);

  GstStaticCaps static_image_caps = {
    .caps = NULL,
    .string = qmmfsrc_all_image_caps,
    ._gst_reserved = { NULL }
  };

  qmmfsrc_image_src_template.name_template = "image_%u";
  qmmfsrc_image_src_template.direction = GST_PAD_SRC;
  qmmfsrc_image_src_template.presence = GST_PAD_REQUEST;
  qmmfsrc_image_src_template.static_caps = static_image_caps;
}
#endif // ENABLE_RUNTIME_PARSER

static gboolean
qmmfsrc_pad_push_event (GstElement * element, GstPad * pad, gpointer data)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstEvent *event = GST_EVENT (data);

  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_copy (event));
}

static gboolean
qmmfsrc_pad_send_event (GstElement * element, GstPad * pad, gpointer data)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstEvent *event = GST_EVENT (data);

  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_send_event (pad, gst_event_copy (event));
}

static gboolean
qmmfsrc_pad_flush_buffers (GstElement * element, GstPad * pad, gpointer data)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  gboolean flush = GPOINTER_TO_UINT (data);

  GST_DEBUG_OBJECT (qmmfsrc, "Flush pad: %s", GST_PAD_NAME (pad));

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    qmmfsrc_video_pad_flush_buffers_queue (pad, flush);
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    qmmfsrc_image_pad_flush_buffers_queue (pad, flush);
  }

  return TRUE;
}

static void
qmmfsrc_pad_reconfigure (GstPad * pad, GstElement * element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstState state = GST_STATE_VOID_PENDING;
  gboolean success = FALSE;

  if (gst_element_get_state (element, &state, NULL, 0) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (element, "Failed to retrieve pipeline state!");
    return;
  }

  if (state != GST_STATE_PLAYING && state != GST_STATE_PAUSED)
    return;

  GST_INFO_OBJECT (qmmfsrc, "Reconfiguration for pad %s in %s state",
      GST_PAD_NAME (pad), gst_element_state_get_name (state));

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    GST_INFO_OBJECT (qmmfsrc, "Reconfigure video pad");
    GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
    GArray *ids = NULL;

    // First delete the previous camera stream associated with this pad.
    if (vpad->id != 0) {
      ids = g_array_new (FALSE, FALSE, sizeof (guint));
      ids = g_array_append_val (ids, vpad->id);

      success = gst_qmmf_context_stop_video_streams (qmmfsrc->context, ids);
      g_array_free (ids, TRUE);

      QMMFSRC_RETURN_IF_FAIL (qmmfsrc, success, "Stream stop failed!");

      success = gst_qmmf_context_delete_video_stream (qmmfsrc->context, pad);
      QMMFSRC_RETURN_IF_FAIL (
          qmmfsrc, success, "Video stream deletion failed!");
    }

    GST_INFO_OBJECT (element, "Create new video stream");
    success = gst_qmmf_context_create_video_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_IF_FAIL (
        qmmfsrc, success, "Video stream creation failed!");

    if (state == GST_STATE_PLAYING) {
      ids = g_array_new (FALSE, FALSE, sizeof (guint));
      ids = g_array_append_val (ids, vpad->id);

      success = gst_qmmf_context_start_video_streams (qmmfsrc->context, ids);
      g_array_free (ids, TRUE);

      QMMFSRC_RETURN_IF_FAIL (qmmfsrc, success, "Stream start failed!");
    }
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    GST_INFO_OBJECT (qmmfsrc, "Reconfigure image pad");

    // First delete the previous camera stream associated with this pad.
    success = gst_qmmf_context_delete_image_stream (qmmfsrc->context, pad, 0);
    QMMFSRC_RETURN_IF_FAIL (qmmfsrc, success, "Image Stream delete failed!");

    GST_INFO_OBJECT (element, "Create new image stream");
    success = gst_qmmf_context_create_image_stream (
        qmmfsrc->context, pad);
    QMMFSRC_RETURN_IF_FAIL (
        qmmfsrc, success, "Image stream creation failed!");
  }
}

static void
qmmfsrc_pad_activation (GstPad * pad, gboolean active, GstElement * element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  GArray *ids = NULL;
  gboolean success = FALSE;
  GstState state = GST_STATE_VOID_PENDING;

  if (gst_element_get_state (element, &state, NULL, 0) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (element, "Failed to retrieve pipeline state!");
    return;
  }

  if (state != GST_STATE_PLAYING ||
      qmmfsrc->pad_activation_mode != GST_PAD_ACTIVATION_MODE_NORMAL)
    return;

  ids = g_array_new (FALSE, FALSE, sizeof (guint));
  ids = g_array_append_val (ids, vpad->id);

  if (active) {
    success = gst_qmmf_context_start_video_streams (qmmfsrc->context, ids);
    QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (qmmfsrc, success,
        { g_array_free (ids, FALSE); }, "Stream start failed!");
  } else {
    success = gst_qmmf_context_stop_video_streams (qmmfsrc->context, ids);
    QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (qmmfsrc, success,
        { g_array_free (ids, FALSE); }, "Stream stop failed!");
  }

  g_array_free (ids, TRUE);
}

static GstPad*
qmmfsrc_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  gchar *padname = NULL;
  guint index = 0, nextindex = 0;
  gboolean isvideo = FALSE, isimage = FALSE;
  GstPad *srcpad = NULL;

  isvideo = (templ == gst_element_class_get_pad_template (klass, "video_%u"));
  isimage = (templ == gst_element_class_get_pad_template (klass, "image_%u"));

  if (!isvideo && !isimage) {
    GST_ERROR_OBJECT (qmmfsrc, "Invalid pad template");
    return NULL;
  }

  GST_QMMFSRC_LOCK (qmmfsrc);

  if ((reqname && sscanf (reqname, "video_%u", &index) == 1) ||
      (reqname && sscanf (reqname, "image_%u", &index) == 1)) {
    if (g_hash_table_contains (qmmfsrc->srcpads, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (qmmfsrc, "Source pad name %s is not unique", reqname);
      GST_QMMFSRC_UNLOCK (qmmfsrc);
      return NULL;
    }

    // Update the next video pad index set his name.
    nextindex = (index >= qmmfsrc->nextidx) ? index + 1 : qmmfsrc->nextidx;
  } else {
    index = qmmfsrc->nextidx;
    // Find an unused source pad index.
    while (g_hash_table_contains (qmmfsrc->srcpads, GUINT_TO_POINTER (index))) {
      index++;
    }
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  if (isvideo) {
    padname = g_strdup_printf ("video_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting video pad %s (%d)", padname, index);
    srcpad = qmmfsrc_request_video_pad (templ, padname, index);

    qmmfsrc->vidindexes =
        g_list_append (qmmfsrc->vidindexes, GUINT_TO_POINTER (index));
    g_free (padname);
  } else if (isimage) {

    padname = g_strdup_printf ("image_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting image pad %d (%s)", index, padname);
    srcpad = qmmfsrc_request_image_pad (templ, padname, index);

    qmmfsrc->imgindexes =
        g_list_append (qmmfsrc->imgindexes, GUINT_TO_POINTER (index));
    g_free (padname);
  }

  if (srcpad == NULL) {
    GST_ERROR_OBJECT (element, "Failed to create pad %d!", index);
    GST_QMMFSRC_UNLOCK (qmmfsrc);
    return NULL;
  }

  GST_DEBUG_OBJECT (qmmfsrc, "Created pad with index %d", index);

  qmmfsrc->nextidx = nextindex;
  g_hash_table_insert (qmmfsrc->srcpads, GUINT_TO_POINTER (index), srcpad);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  gst_element_add_pad (element, srcpad);
  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (srcpad),
      GST_OBJECT_NAME (srcpad));

  // Connect a callback to the 'notify' signal of a pad property to be
  // called when a that property changes during runtime.
  g_signal_connect (srcpad, "notify::framerate",
      G_CALLBACK (gst_qmmf_context_update_video_param), qmmfsrc->context);
  g_signal_connect (srcpad, "notify::crop",
      G_CALLBACK (gst_qmmf_context_update_video_param), qmmfsrc->context);

  // Connect a callback to the pad reconfigure signal.
  g_signal_connect (srcpad, "reconfigure",
      G_CALLBACK (qmmfsrc_pad_reconfigure), GST_ELEMENT (qmmfsrc));

  if (isvideo) {
    // Connect a callback to the pad activation signal.
    g_signal_connect (srcpad, "activation",
        G_CALLBACK (qmmfsrc_pad_activation), GST_ELEMENT (qmmfsrc));
  }

  return srcpad;
}

static void
qmmfsrc_release_pad (GstElement * element, GstPad * pad)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstState state = GST_STATE_VOID_PENDING;
  guint index = 0;
  gboolean success = FALSE;

  GST_QMMFSRC_LOCK (qmmfsrc);

  if (gst_element_get_state (element, &state, NULL, 0) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (element, "Failed to retrieve pipeline state!");
    return;
  }

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    index = GST_QMMFSRC_VIDEO_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing video pad %d", index);

    if (state == GST_STATE_PLAYING || state == GST_STATE_PAUSED) {
      GArray *ids = g_array_new (FALSE, FALSE, sizeof (guint));
      ids = g_array_append_val (ids, GST_QMMFSRC_VIDEO_PAD (pad)->id);

      GST_DEBUG_OBJECT (element, "Delete stream");
      success = gst_qmmf_context_stop_video_streams (qmmfsrc->context, ids);
      g_array_free (ids, TRUE);

      QMMFSRC_RETURN_IF_FAIL (qmmfsrc, success, "Stream stop failed!");

      success = gst_qmmf_context_delete_video_stream (qmmfsrc->context, pad);
      QMMFSRC_RETURN_IF_FAIL (
          qmmfsrc, success, "Video stream deletion failed!");
    }

    qmmfsrc_release_video_pad (element, pad);
    qmmfsrc->vidindexes =
        g_list_remove (qmmfsrc->vidindexes, GUINT_TO_POINTER (index));
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    index = GST_QMMFSRC_IMAGE_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing image pad %d", index);

    if (state == GST_STATE_PLAYING || state == GST_STATE_PAUSED) {
      GST_DEBUG_OBJECT (element, "Delete image stream");
      success = gst_qmmf_context_delete_image_stream (qmmfsrc->context, pad,
          FALSE);
      QMMFSRC_RETURN_IF_FAIL (
          qmmfsrc, success, "Image stream deletion failed!");
    }

    qmmfsrc_release_image_pad (element, pad);
    qmmfsrc->imgindexes =
        g_list_remove (qmmfsrc->imgindexes, GUINT_TO_POINTER (index));
  }

  g_hash_table_remove (qmmfsrc->srcpads, GUINT_TO_POINTER (index));
  GST_DEBUG_OBJECT (qmmfsrc, "Deleted pad %d", index);

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

static GstStaticCaps gst_qmmfsrc_video_static_src_caps =
    GST_STATIC_CAPS (QMMFSRC_VIDEO_JPEG_CAPS "; "
        QMMFSRC_VIDEO_RAW_CAPS (
                "{ NV12, NV16, NV12_Q08C"
#ifdef GST_VIDEO_YUY2_FORMAT_ENABLE
                ", YUY2"
#endif // GST_VIDEO_YUY2_FORMAT_ENABLE
#ifdef GST_VIDEO_UYVY_FORMAT_ENABLE
                ", UYVY"
#endif // GST_VIDEO_UYVY_FORMAT_ENABLE
#ifdef GST_VIDEO_P010_10LE_FORMAT_ENABLE
                ", P010_10LE"
#endif // GST_VIDEO_P010_10LE_FORMAT_ENABLE
#ifdef GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
                ", NV12_10LE32"
#endif // GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
                " }") "; "
            QMMFSRC_VIDEO_BAYER_CAPS (
                "{ bggr, rggb, gbrg, grbg, mono }",
                "{ 8, 10, 12, 16 }"));

static GstStaticCaps gst_qmmfsrc_image_static_src_caps =
    GST_STATIC_CAPS (QMMFSRC_IMAGE_JPEG_CAPS "; "
        QMMFSRC_IMAGE_RAW_CAPS (
                "{ NV21"
#ifdef GST_IMAGE_NV12_FORMAT_ENABLE
                ", NV12"
#endif // GST_IMAGE_NV12_FORMAT_ENABLE
                " }") "; "
            QMMFSRC_IMAGE_BAYER_CAPS (
                "{ bggr, rggb, gbrg, grbg, mono }",
                "{ 8, 10, 12, 16 }"));

static GstCaps *
gst_qmmfsrc_video_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_qmmfsrc_video_static_src_caps);

    if (gst_is_gbm_supported()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              "{ NV12, NV16, NV12_Q08C"
#ifdef GST_VIDEO_YUY2_FORMAT_ENABLE
                ", YUY2"
#endif // GST_VIDEO_YUY2_FORMAT_ENABLE
#ifdef GST_VIDEO_UYVY_FORMAT_ENABLE
                ", UYVY"
#endif // GST_VIDEO_UYVY_FORMAT_ENABLE
#ifdef GST_VIDEO_P010_10LE_FORMAT_ENABLE
                ", P010_10LE"
#endif // GST_VIDEO_P010_10LE_FORMAT_ENABLE
#ifdef GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
                ", NV12_10LE32"
#endif // GST_VIDEO_NV12_10LE32_FORMAT_ENABLE
                " }"));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_qmmfsrc_image_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_qmmfsrc_image_static_src_caps);

    if (gst_is_gbm_supported()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          QMMFSRC_IMAGE_RAW_CAPS_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV21"
#ifdef GST_IMAGE_NV12_FORMAT_ENABLE
                ", NV12"
#endif // GST_IMAGE_NV12_FORMAT_ENABLE
                " }"));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_qmmfsrc_video_src_template (void)
{
  return gst_pad_template_new_with_gtype ("video_%u", GST_PAD_SRC, GST_PAD_REQUEST,
      gst_qmmfsrc_video_src_caps (), GST_TYPE_QMMFSRC_VIDEO_PAD);
}

static GstPadTemplate *
gst_qmmfsrc_image_src_template (void)
{
  return gst_pad_template_new_with_gtype ("image_%u", GST_PAD_SRC, GST_PAD_REQUEST,
      gst_qmmfsrc_image_src_caps (), GST_TYPE_QMMFSRC_IMAGE_PAD);
}

static void
qmmfsrc_event_callback (guint event, gpointer userdata)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (userdata);

  switch (event) {
    case EVENT_SERVICE_DIED:
      GST_ELEMENT_ERROR (qmmfsrc, RESOURCE, NOT_FOUND,
          ("Camera service has died !"), (NULL));
      break;
    case EVENT_CAMERA_ERROR:
      GST_ELEMENT_ERROR (qmmfsrc, RESOURCE, FAILED,
          ("Camera device encountered an un-recovarable error !"), (NULL));
      break;
    case EVENT_CAMERA_OPENED:
      GST_LOG_OBJECT (qmmfsrc, "Camera device has been opened");
      break;
    case EVENT_CAMERA_CLOSING:
      GST_LOG_OBJECT (qmmfsrc, "Closing camera device");

      if (GST_STATE (qmmfsrc) == GST_STATE_PLAYING) {
        gboolean success = gst_element_foreach_src_pad (
            GST_ELEMENT (qmmfsrc), qmmfsrc_pad_push_event, gst_event_new_eos ()
        );

        if (!success)
          GST_ELEMENT_ERROR (qmmfsrc, CORE, EVENT,
              ("Failed to send EOS to source pads !"), (NULL));
      }
      break;
    case EVENT_CAMERA_CLOSED:
      GST_LOG_OBJECT (qmmfsrc, "Camera device has been closed");
      break;
    case EVENT_FRAME_ERROR:
      GST_WARNING_OBJECT (qmmfsrc, "Camera device has encountered non-fatal "
          "frame drop error !");
      break;
    case EVENT_METADATA_ERROR:
      GST_WARNING_OBJECT (qmmfsrc, "Camera device has encountered non-fatal "
          "metadata drop error !");
      break;
    default:
      GST_WARNING_OBJECT (qmmfsrc, "Unknown camera device event");
      break;
  }
}

static void
qmmfsrc_metadata_callback (gint camera_id, gconstpointer metadata,
    gboolean isurgent, gpointer userdata)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (userdata);

  if (isurgent)
    g_signal_emit_by_name (qmmfsrc, "urgent-metadata", metadata);
  else
    g_signal_emit_by_name (qmmfsrc, "result-metadata", metadata);
}

static gboolean
qmmfsrc_create_stream (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  gpointer key = NULL;
  GstPad *pad = NULL;
  GList *list = NULL;
  GValue isslave = G_VALUE_INIT, sframerate = G_VALUE_INIT;

  g_value_init (&isslave, G_TYPE_BOOLEAN);
  g_value_init (&sframerate, G_TYPE_INT);

  GST_TRACE_OBJECT (qmmfsrc, "Create stream");

  // Iterate over the video pads, fixate caps and create streams.
  for (list = qmmfsrc->vidindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    gst_qmmf_context_get_camera_param (qmmfsrc->context,
        PARAM_CAMERA_SLAVE, &isslave);

    if (!g_value_get_boolean (&isslave)) {
      GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SUPER_FRAMERATE, &sframerate);
      vpad->superframerate = g_value_get_int (&sframerate);
    }

    success = qmmfsrc_video_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate video caps!");

    success = gst_qmmf_context_create_video_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Video stream creation failed!");
  }

  g_value_unset (&isslave);
  g_value_unset (&sframerate);

  // Iterate over the image pads, fixate caps and create streams.
  for (list = qmmfsrc->imgindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = qmmfsrc_image_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate image caps!");

    success = gst_qmmf_context_create_image_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Image stream creation failed!");
  }

  success = gst_element_foreach_src_pad (GST_ELEMENT (qmmfsrc),
      qmmfsrc_pad_flush_buffers, GUINT_TO_POINTER (FALSE));
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  GST_TRACE_OBJECT (qmmfsrc, "Stream created");

  return TRUE;
}

static gboolean
qmmfsrc_delete_stream (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  gpointer key;
  GstPad *pad;
  GList *list = NULL;

  GST_TRACE_OBJECT (qmmfsrc, "Delete stream");

  success = gst_element_foreach_src_pad (GST_ELEMENT (qmmfsrc),
      qmmfsrc_pad_flush_buffers, GUINT_TO_POINTER (TRUE));
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  for (list = qmmfsrc->imgindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    success = gst_qmmf_context_delete_image_stream (qmmfsrc->context, pad,
        FALSE);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Image stream deletion failed!");
  }

  for (list = qmmfsrc->vidindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = gst_qmmf_context_delete_video_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Video stream deletion failed!");
  }

  GST_TRACE_OBJECT (qmmfsrc, "Stream deleted");

  return TRUE;
}

static gboolean
qmmfsrc_start_stream (GstQmmfSrc * qmmfsrc)
{
  GstPad *pad = NULL;
  GList *list = NULL;
  GArray *ids = NULL;
  gboolean success = TRUE;
  gpointer key;

  // No source pads, nothing to do but return.
  if (g_hash_table_size (qmmfsrc->srcpads) == 0)
    return TRUE;

  GST_TRACE_OBJECT (qmmfsrc, "Starting stream");

  ids = g_array_new (FALSE, FALSE, sizeof (guint));

  // Iterate over the video pads, fixate caps and create streams.
  for (list = qmmfsrc->vidindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    if (gst_pad_get_task_state (pad) != GST_TASK_STARTED) {
      GST_INFO_OBJECT (qmmfsrc, "Pad %s is not activated", GST_PAD_NAME (pad));
      continue;
    }

    ids = g_array_append_val (ids, GST_QMMFSRC_VIDEO_PAD (pad)->id);
  }

  success = gst_qmmf_context_start_video_streams (qmmfsrc->context, ids);
  g_array_free (ids, TRUE);

  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE, "Stream start failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Stream started");

  return TRUE;
}

static gboolean
qmmfsrc_stop_stream (GstQmmfSrc * qmmfsrc)
{
  GstPad *pad = NULL;
  GList *list = NULL;
  GArray *ids = NULL;
  gboolean success = TRUE;
  gpointer key;

  // No source pads, nothing to do but return.
  if (g_hash_table_size (qmmfsrc->srcpads) == 0)
    return TRUE;

  GST_TRACE_OBJECT (qmmfsrc, "Stopping stream");

  ids = g_array_new (FALSE, FALSE, sizeof (guint));

  // Iterate over the video pads, fixate caps and create streams.
  for (list = qmmfsrc->vidindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    ids = g_array_append_val (ids, GST_QMMFSRC_VIDEO_PAD (pad)->id);
  }

  success = gst_qmmf_context_stop_video_streams (qmmfsrc->context, ids);
  g_array_free (ids, TRUE);

  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE, "Stream stop failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Stream stopped");

  return TRUE;
}

static gboolean
qmmfsrc_capture_image (GstQmmfSrc * qmmfsrc, guint imgtype, guint n_images,
    GPtrArray * metas)
{
  gboolean success = FALSE;

  GST_TRACE_OBJECT (qmmfsrc, "Submit capture image/s");
  success = gst_qmmf_context_capture_image (qmmfsrc->context, qmmfsrc->srcpads,
      qmmfsrc->imgindexes, imgtype, n_images, metas);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Capture image failed!");
  GST_TRACE_OBJECT (qmmfsrc, "Capture image/s submitted");

  return TRUE;
}

static gboolean
qmmfsrc_cancel_capture (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  gpointer key;
  GstPad *pad = NULL;
  GList *list = NULL;

  GST_TRACE_OBJECT (qmmfsrc, "Canceling image capturing");

  for (list = qmmfsrc->imgindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = gst_qmmf_context_delete_image_stream (qmmfsrc->context, pad, TRUE);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to cancel image capture!");
  }

  GST_TRACE_OBJECT (qmmfsrc, "Image capture canceled");

  return TRUE;
}

static gboolean
qmmfsrc_match_srcpad (gpointer key, gpointer value, gpointer user_data)
{
  return g_str_equal (GST_PAD_NAME (GST_PAD (value)), (gchar *)user_data);
}

static gboolean
qmmfsrc_signal_video_pads_activation (GstQmmfSrc * qmmfsrc, gboolean activate,
    GPtrArray * padnames)
{
  GstElement *element = GST_ELEMENT (qmmfsrc);
  GstState state = GST_STATE_VOID_PENDING;
  GList *list = NULL;
  GArray *ids = NULL;
  guint array_index = 0;
  gboolean success = TRUE;

  GST_INFO_OBJECT (qmmfsrc, "video-pads-activation signal received (%s)",
      activate ? "activate" : "deactivate");

  if (qmmfsrc->pad_activation_mode != GST_PAD_ACTIVATION_MODE_SIGNAL) {
    GST_INFO_OBJECT (qmmfsrc, "pad activation mode is normal, "
        "video-pads-activation signal not enabled");
    return FALSE;
  }

  if (gst_element_get_state (element, &state, NULL, 0) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (element, "Failed to retrieve pipeline state!");
    return FALSE;
  }

  if (state != GST_STATE_PLAYING && state != GST_STATE_PAUSED) {
    GST_ERROR_OBJECT (element, "Video streams activation signal can only "
        "be triggered on PLAYING / PAUSED state");
    return FALSE;
  }

  ids = g_array_new (FALSE, FALSE, sizeof (guint));

  for (array_index = 0; array_index < padnames->len; array_index++) {
    gchar *pad_name = g_ptr_array_index (padnames, array_index);
    GstPad *pad = NULL;

    pad = g_hash_table_find (qmmfsrc->srcpads, qmmfsrc_match_srcpad, pad_name);

    if (!(success = (pad != NULL))) {
      GST_INFO_OBJECT (qmmfsrc, "pad %s is invalid", pad_name);
      goto cleanup;
    }

    ids = g_array_append_val (ids, GST_QMMFSRC_VIDEO_PAD (pad)->id);
  }

  if (activate)
    success = gst_qmmf_context_start_video_streams (qmmfsrc->context, ids);
  else
    success = gst_qmmf_context_stop_video_streams (qmmfsrc->context, ids);

  if (!success)
    GST_ERROR_OBJECT (qmmfsrc, "Streams %s failed!", activate ? "start" : "stop");

cleanup:
  g_array_free (ids, FALSE);
  return success;
}

static GstStateChangeReturn
qmmfsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_qmmf_context_open (qmmfsrc->context)) {
        GST_ELEMENT_ERROR (qmmfsrc, RESOURCE, NOT_FOUND,
          ("Failed to Open Camera!"), NULL);
        return GST_STATE_CHANGE_FAILURE;
      }
      qmmfsrc->isplugged = TRUE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!qmmfsrc_create_stream (qmmfsrc)) {
        GST_ELEMENT_ERROR (qmmfsrc, STREAM, FAILED,
          ("Failed to create stream!"), NULL);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!qmmfsrc_start_stream (qmmfsrc)) {
        GST_ELEMENT_ERROR (qmmfsrc, STREAM, FAILED,
          ("Failed to start stream!"), NULL);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (qmmfsrc, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      // Return NO_PREROLL to inform bin/pipeline we won't be able to
      // produce data in the PAUSED state, as this is a live source.
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      // We will call stop_stream only if the camera is plugged, which
      // is always true for BWC while for PoV it may or may not be true.
      // When PoV is plugged stop_stream will be called from here,
      // otherwise it will be called from camera-plug event handling.
      if (qmmfsrc->isplugged && !qmmfsrc_stop_stream (qmmfsrc)) {
        GST_ELEMENT_ERROR (qmmfsrc, STREAM, FAILED, ("Failed to stop stream!"),
          NULL);
        return GST_STATE_CHANGE_FAILURE;
      }
      // Return NO_PREROLL to inform bin/pipeline we won't be able to
      // produce data in the PAUSED state, as this is a live source.
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!qmmfsrc_delete_stream (qmmfsrc)) {
        GST_ELEMENT_ERROR (qmmfsrc, STREAM, FAILED, ("Failed to delete stream!"),
          NULL);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_qmmf_context_close (qmmfsrc->context)) {
        GST_ELEMENT_ERROR (qmmfsrc, STREAM, FAILED, ("Failed to Close Camera!"),
          NULL);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      // Otherwise it's success, we don't want to return spurious
      // NO_PREROLL or ASYNC from internal elements as we care for
      // state changes ourselves here
      // This is to catch PAUSED->PAUSED and PLAYING->PLAYING transitions.
      ret = (GST_STATE_TRANSITION_NEXT (transition) == GST_STATE_PAUSED) ?
          GST_STATE_CHANGE_NO_PREROLL : GST_STATE_CHANGE_SUCCESS;
      break;
  }

  return ret;
}

static gboolean
qmmfsrc_send_event (GstElement * element, GstEvent * event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    // Bidirectional events.
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing FLUSH_START event");
      success =
          gst_element_foreach_src_pad (element, qmmfsrc_pad_send_event, event);
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing FLUSH_STOP event");
      success =
          gst_element_foreach_src_pad (element, qmmfsrc_pad_send_event, event);
      gst_event_unref(event);
      break;

    // Downstream serialized events.
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing EOS event downstream");
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_push_event, event
      );
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_flush_buffers,
          GUINT_TO_POINTER (TRUE)
      );
      gst_event_unref (event);
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      GST_DEBUG_OBJECT (qmmfsrc, "Received custom downstream event");
      if (gst_event_has_name (event, "camera-plug")) {
        // Toggle on camera-plug event
        qmmfsrc->isplugged = qmmfsrc->isplugged ? FALSE: TRUE;
        if(!qmmfsrc->isplugged) {
          // If the camera is unplugged stop stream.
          if (!qmmfsrc_stop_stream(qmmfsrc)) {
            GST_ERROR_OBJECT(qmmfsrc, "Failed to stop stream!");
          }
        }
      } else if (gst_event_has_name (event, "camera-standby")) {
        GValue value = G_VALUE_INIT;
        g_value_init (&value, G_TYPE_UINT);
        g_value_set_uint (&value, 1);
        gst_qmmf_context_set_camera_param(qmmfsrc->context,
            PARAM_CAMERA_STANDBY, &value);
        if (!qmmfsrc_stop_stream(qmmfsrc)) {
          GST_ERROR_OBJECT(qmmfsrc, "Failed to stop stream!");
        }
      }
      gst_event_unref (event);
      break;
    default:
      success = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }

  return success;
}

// GstElement virtual method implementation. Sets the element's properties.
static void
qmmfsrc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (qmmfsrc);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  switch (property_id) {
    case PROP_CAMERA_ID:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ID, value);
      break;
    case PROP_CAMERA_SLAVE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SLAVE, value);
        break;
    case PROP_CAMERA_LDC:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_LDC, value);
      break;
    case PROP_CAMERA_LCAC:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_LCAC, value);
      break;
    case PROP_CAMERA_EIS:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EIS, value);
      break;
#ifndef VHDR_MODES_ENABLE
    case PROP_CAMERA_SHDR:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SHDR, value);
      break;
#else
    case PROP_CAMERA_VHDR:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_VHDR, value);
      break;
#endif // VHDR_MODES_ENABLE
    case PROP_CAMERA_ADRC:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ADRC, value);
      break;
    case PROP_CAMERA_CONTROL_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_CONTROL_MODE, value);
      break;
    case PROP_CAMERA_EFFECT_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EFFECT_MODE, value);
      break;
    case PROP_CAMERA_SCENE_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SCENE_MODE, value);
      break;
    case PROP_CAMERA_ANTIBANDING_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ANTIBANDING_MODE, value);
      break;
    case PROP_CAMERA_SHARPNESS:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SHARPNESS, value);
      break;
    case PROP_CAMERA_CONTRAST:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_CONTRAST, value);
      break;
    case PROP_CAMERA_SATURATION:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SATURATION, value);
      break;
    case PROP_CAMERA_ISO_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ISO_MODE, value);
      break;
    case PROP_CAMERA_ISO_VALUE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ISO_VALUE, value);
      break;
    case PROP_CAMERA_EXPOSURE_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_MODE, value);
      break;
    case PROP_CAMERA_EXPOSURE_LOCK:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_LOCK, value);
      break;
    case PROP_CAMERA_EXPOSURE_METERING:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_METERING, value);
      break;
    case PROP_CAMERA_EXPOSURE_COMPENSATION:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_COMPENSATION, value);
      break;
    case PROP_CAMERA_EXPOSURE_TIME:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_TIME, value);
      break;
    case PROP_CAMERA_EXPOSURE_TABLE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_TABLE, value);
      break;
    case PROP_CAMERA_WHITE_BALANCE_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_WHITE_BALANCE_MODE, value);
      break;
    case PROP_CAMERA_WHITE_BALANCE_LOCK:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_WHITE_BALANCE_LOCK, value);
      break;
    case PROP_CAMERA_MANUAL_WB_SETTINGS:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_MANUAL_WB_SETTINGS, value);
      break;
    case PROP_CAMERA_FOCUS_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_FOCUS_MODE, value);
      break;
    case PROP_CAMERA_NOISE_REDUCTION:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_NOISE_REDUCTION, value);
      break;
    case PROP_CAMERA_NOISE_REDUCTION_TUNING:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_NOISE_REDUCTION_TUNING, value);
      break;
    case PROP_CAMERA_ZOOM:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ZOOM, value);
      break;
    case PROP_CAMERA_DEFOG_TABLE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_DEFOG_TABLE, value);
      break;
    case PROP_CAMERA_LOCAL_TONE_MAPPING:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_LOCAL_TONE_MAPPING, value);
      break;
    case PROP_CAMERA_IR_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_IR_MODE, value);
      break;
    case PROP_CAMERA_SENSOR_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SENSOR_MODE, value);
      break;
    case PROP_CAMERA_VIDEO_METADATA:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_VIDEO_METADATA, value);
      break;
    case PROP_CAMERA_IMAGE_METADATA:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_IMAGE_METADATA, value);
      break;
    case PROP_CAMERA_SESSION_METADATA:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SESSION_METADATA, value);
      break;
    case PROP_CAMERA_FRC_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_FRC_MODE, value);
      break;
    case PROP_CAMERA_IFE_DIRECT_STREAM:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_IFE_DIRECT_STREAM, value);
      break;
    case PROP_CAMERA_MULTI_CAM_EXPOSURE_TIME:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_MULTI_CAM_EXPOSURE_TIME, value);
      break;
    case PROP_CAMERA_OPERATION_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_OPERATION_MODE, value);
      break;
    case PROP_CAMERA_INPUT_ROI:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_INPUT_ROI, value);
      break;
    case PROP_CAMERA_INPUT_ROI_INFO:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_INPUT_ROI_INFO, value);
      break;
    case PROP_CAMERA_PHYSICAL_CAMERA_SWITCH:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_PHYISICAL_CAMERA_SWITCH, value);
      break;
    case PROP_CAMERA_PAD_ACTIVATION_MODE:
      qmmfsrc->pad_activation_mode = g_value_get_enum(value);
      break;
#ifdef FEATURE_OFFLINE_IFE_SUPPORT
    case PROP_CAMERA_MULTICAMERA_HINT:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_MULTICAMERA_HINT, value);
      break;
#endif // FEATURE_OFFLINE_IFE_SUPPORT
    case PROP_CAMERA_SW_TNR:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
           PARAM_CAMERA_SW_TNR, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

// GstElement virtual method implementation. Sets the element's properties.
static void
qmmfsrc_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (object);

  switch (property_id) {
    case PROP_CAMERA_ID:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ID, value);
      break;
    case PROP_CAMERA_SLAVE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SLAVE, value);
        break;
    case PROP_CAMERA_LDC:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_LDC, value);
      break;
    case PROP_CAMERA_LCAC:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_LCAC, value);
      break;
    case PROP_CAMERA_EIS:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EIS, value);
      break;
#ifndef VHDR_MODES_ENABLE
    case PROP_CAMERA_SHDR:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SHDR, value);
      break;
#else
    case PROP_CAMERA_VHDR:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_VHDR, value);
      break;
#endif // VHDR_MODES_ENABLE
    case PROP_CAMERA_ADRC:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ADRC, value);
      break;
    case PROP_CAMERA_CONTROL_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_CONTROL_MODE, value);
      break;
    case PROP_CAMERA_EFFECT_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EFFECT_MODE, value);
      break;
    case PROP_CAMERA_SCENE_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SCENE_MODE, value);
      break;
    case PROP_CAMERA_ANTIBANDING_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ANTIBANDING_MODE, value);
      break;
    case PROP_CAMERA_SHARPNESS:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SHARPNESS, value);
      break;
    case PROP_CAMERA_CONTRAST:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_CONTRAST, value);
      break;
    case PROP_CAMERA_SATURATION:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SATURATION, value);
      break;
    case PROP_CAMERA_ISO_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ISO_MODE, value);
      break;
    case PROP_CAMERA_ISO_VALUE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ISO_VALUE, value);
      break;
    case PROP_CAMERA_EXPOSURE_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_MODE, value);
      break;
    case PROP_CAMERA_EXPOSURE_LOCK:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_LOCK, value);
      break;
    case PROP_CAMERA_EXPOSURE_METERING:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_METERING, value);
      break;
    case PROP_CAMERA_EXPOSURE_COMPENSATION:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_COMPENSATION, value);
      break;
    case PROP_CAMERA_EXPOSURE_TIME:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_TIME, value);
      break;
    case PROP_CAMERA_EXPOSURE_TABLE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_EXPOSURE_TABLE, value);
      break;
    case PROP_CAMERA_WHITE_BALANCE_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_WHITE_BALANCE_MODE, value);
      break;
    case PROP_CAMERA_WHITE_BALANCE_LOCK:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_WHITE_BALANCE_LOCK, value);
      break;
    case PROP_CAMERA_MANUAL_WB_SETTINGS:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_MANUAL_WB_SETTINGS, value);
      break;
    case PROP_CAMERA_FOCUS_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_FOCUS_MODE, value);
      break;
    case PROP_CAMERA_NOISE_REDUCTION:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_NOISE_REDUCTION, value);
      break;
    case PROP_CAMERA_NOISE_REDUCTION_TUNING:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_NOISE_REDUCTION_TUNING, value);
      break;
    case PROP_CAMERA_ZOOM:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ZOOM, value);
      break;
    case PROP_CAMERA_DEFOG_TABLE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_DEFOG_TABLE, value);
      break;
    case PROP_CAMERA_LOCAL_TONE_MAPPING:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_LOCAL_TONE_MAPPING, value);
      break;
    case PROP_CAMERA_IR_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_IR_MODE, value);
      break;
    case PROP_CAMERA_ACTIVE_SENSOR_SIZE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ACTIVE_SENSOR_SIZE, value);
      break;
    case PROP_CAMERA_SENSOR_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SENSOR_MODE, value);
      break;
    case PROP_CAMERA_VIDEO_METADATA:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_VIDEO_METADATA, value);
      break;
    case PROP_CAMERA_IMAGE_METADATA:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_IMAGE_METADATA, value);
      break;
    case PROP_CAMERA_STATIC_METADATA:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_STATIC_METADATA, value);
      break;
    case PROP_CAMERA_FRC_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_FRC_MODE, value);
      break;
    case PROP_CAMERA_IFE_DIRECT_STREAM:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_IFE_DIRECT_STREAM, value);
      break;
    case PROP_CAMERA_MULTI_CAM_EXPOSURE_TIME:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_MULTI_CAM_EXPOSURE_TIME, value);
      break;
    case PROP_CAMERA_OPERATION_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_OPERATION_MODE, value);
      break;
    case PROP_CAMERA_INPUT_ROI:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_INPUT_ROI, value);
      break;
    case PROP_CAMERA_INPUT_ROI_INFO:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_INPUT_ROI_INFO, value);
      break;
    case PROP_CAMERA_PHYSICAL_CAMERA_SWITCH:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_PHYISICAL_CAMERA_SWITCH, value);
      break;
    case PROP_CAMERA_PAD_ACTIVATION_MODE:
      g_value_set_enum(value, qmmfsrc->pad_activation_mode);
      break;
#ifdef FEATURE_OFFLINE_IFE_SUPPORT
    case PROP_CAMERA_MULTICAMERA_HINT:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_MULTICAMERA_HINT, value);
      break;
#endif // FEATURE_OFFLINE_IFE_SUPPORT
    case PROP_CAMERA_SW_TNR:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SW_TNR, value);
      break;
    case PROP_CAMERA_STATIC_METADATAS:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_STATIC_METADATAS, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

// GstElement virtual method implementation. Called when plugin is destroyed.
static void
qmmfsrc_finalize (GObject * object)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (object);

  if (qmmfsrc->srcpads != NULL) {
    g_hash_table_remove_all (qmmfsrc->srcpads);
    g_hash_table_destroy (qmmfsrc->srcpads);
    qmmfsrc->srcpads = NULL;
  }

  if (qmmfsrc->vidindexes != NULL) {
    g_list_free (qmmfsrc->vidindexes);
    qmmfsrc->vidindexes = NULL;
  }

  if (qmmfsrc->imgindexes != NULL) {
    g_list_free (qmmfsrc->imgindexes);
    qmmfsrc->imgindexes = NULL;
  }

  if (qmmfsrc->context != NULL) {
    gst_qmmf_context_free (qmmfsrc->context);
    qmmfsrc->context = NULL;
  }

#ifdef ENABLE_RUNTIME_PARSER
  qmmfsrc_deinit_src_templates ();
#endif // ENABLE_RUNTIME_PARSER

  G_OBJECT_CLASS (qmmfsrc_parent_class)->finalize (object);
}

// GObject element class initialization function.
static void
qmmfsrc_class_init (GstQmmfSrcClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (qmmfsrc_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (qmmfsrc_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (qmmfsrc_finalize);

#ifdef ENABLE_RUNTIME_PARSER
  qmmfsrc_init_src_templates ();
  gst_element_class_add_static_pad_template_with_gtype (gstelement,
      &qmmfsrc_video_src_template, GST_TYPE_QMMFSRC_VIDEO_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement,
      &qmmfsrc_image_src_template, GST_TYPE_QMMFSRC_IMAGE_PAD);
#endif // ENABLE_RUNTIME_PARSER

  gst_element_class_add_pad_template (gstelement,
      gst_qmmfsrc_video_src_template ());
  gst_element_class_add_pad_template (gstelement,
      gst_qmmfsrc_image_src_template ());

  gst_element_class_set_static_metadata (
      gstelement, "QMMF Video Source", "Source/Video",
      "Reads frames from a device via QMMF service", "QTI"
  );

  g_object_class_install_property (gobject, PROP_CAMERA_ID,
      g_param_spec_uint ("camera", "Camera ID",
          "Camera device ID to be used by video/image pads",
          0, 10, DEFAULT_PROP_CAMERA_ID,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_SLAVE,
      g_param_spec_boolean ("slave", "Slave mode",
          "Set camera as slave device", DEFAULT_PROP_CAMERA_SLAVE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_LDC,
      g_param_spec_boolean ("ldc", "LDC",
          "Lens Distortion Correction", DEFAULT_PROP_CAMERA_LDC_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_LCAC,
      g_param_spec_boolean ("lcac", "LCAC",
          "Lateral Chromatic Aberration Correction", DEFAULT_PROP_CAMERA_LCAC_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#ifndef EIS_MODES_ENABLE
  g_object_class_install_property (gobject, PROP_CAMERA_EIS,
      g_param_spec_boolean ("eis", "EIS",
          "Electronic Image Stabilization mode to reduce the effects of camera shake",
          DEFAULT_PROP_CAMERA_EIS_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#else
  g_object_class_install_property (gobject, PROP_CAMERA_EIS,
      g_param_spec_enum ("eis", "EIS",
          "Electronic Image Stabilization mode to reduce the effects of camera shake",
          GST_TYPE_QMMFSRC_EIS_MODE, DEFAULT_PROP_CAMERA_EIS_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif // EIS_MODES_ENABLE
#ifndef VHDR_MODES_ENABLE
  g_object_class_install_property (gobject, PROP_CAMERA_SHDR,
      g_param_spec_boolean ("shdr", "SHDR",
          "Super High Dynamic Range Imaging", DEFAULT_PROP_CAMERA_SHDR_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#else
  g_object_class_install_property (gobject, PROP_CAMERA_VHDR,
      g_param_spec_enum ("vhdr", "VHDR",
          "Video High Dynamic Range Imaging Modes",
          GST_TYPE_QMMFSRC_VHDR_MODE, DEFAULT_PROP_CAMERA_VHDR_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#endif // VHDR_MODES_ENABLE
  g_object_class_install_property (gobject, PROP_CAMERA_ADRC,
      g_param_spec_boolean ("adrc", "ADRC",
          "Automatic Dynamic Range Compression", DEFAULT_PROP_CAMERA_ADRC,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_CONTROL_MODE,
      g_param_spec_enum ("control-mode", "Control Mode",
           "Overall mode of 3A (auto-exposure, auto-white-balance, auto-focus) "
           "control routines. This is a top-level 3A control switch. When set "
           "to OFF, all 3A control by the camera device is disabled.",
           GST_TYPE_QMMFSRC_CONTROL_MODE, DEFAULT_PROP_CAMERA_CONTROL_MODE,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EFFECT_MODE,
      g_param_spec_enum ("effect", "Effect",
           "Effect applied on the camera frames",
           GST_TYPE_QMMFSRC_EFFECT_MODE, DEFAULT_PROP_CAMERA_EFFECT_MODE,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_SCENE_MODE,
      g_param_spec_enum ("scene", "Scene",
           "Camera optimizations depending on the scene",
           GST_TYPE_QMMFSRC_SCENE_MODE, DEFAULT_PROP_CAMERA_SCENE_MODE,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_ANTIBANDING_MODE,
      g_param_spec_enum ("antibanding", "Antibanding",
           "Camera antibanding routine for the current illumination condition",
           GST_TYPE_QMMFSRC_ANTIBANDING, DEFAULT_PROP_CAMERA_ANTIBANDING,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_SHARPNESS,
      g_param_spec_int ("sharpness", "Sharpness",
          "Image Sharpness Strength", 0, 6, DEFAULT_PROP_CAMERA_SHARPNESS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_CONTRAST,
      g_param_spec_int ("contrast", "Contrast",
          "Image Contrast Strength", 1, 10, DEFAULT_PROP_CAMERA_CONTRAST,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_SATURATION,
      g_param_spec_int ("saturation", "Saturation",
          "Image Saturation Strength", 0, 10, DEFAULT_PROP_CAMERA_SATURATION,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_ISO_MODE,
      g_param_spec_enum ("iso-mode", "ISO Mode",
          "ISO exposure mode",
          GST_TYPE_QMMFSRC_ISO_MODE, DEFAULT_PROP_CAMERA_ISO_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_ISO_VALUE,
      g_param_spec_int ("manual-iso-value", "Manual ISO Value",
           "Manual exposure ISO value. Used when the ISO mode is set to 'manual'",
           100, 3200, DEFAULT_PROP_CAMERA_ISO_VALUE,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EXPOSURE_MODE,
      g_param_spec_enum ("exposure-mode", "Exposure Mode",
          "The desired mode for the camera's exposure routine.",
          GST_TYPE_QMMFSRC_EXPOSURE_MODE, DEFAULT_PROP_CAMERA_EXPOSURE_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EXPOSURE_LOCK,
      g_param_spec_boolean ("exposure-lock", "Exposure Lock",
          "Locks current camera exposure routine values from changing.",
          DEFAULT_PROP_CAMERA_EXPOSURE_LOCK,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EXPOSURE_METERING,
      g_param_spec_enum ("exposure-metering", "Exposure Metering",
          "The desired mode for the camera's exposure metering routine.",
          GST_TYPE_QMMFSRC_EXPOSURE_METERING,
          DEFAULT_PROP_CAMERA_EXPOSURE_METERING,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EXPOSURE_COMPENSATION,
      g_param_spec_int ("exposure-compensation", "Exposure Compensation",
          "Adjust (Compensate) camera images target brightness. Adjustment is "
          "measured as a count of steps.",
          -12, 12, DEFAULT_PROP_CAMERA_EXPOSURE_COMPENSATION,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EXPOSURE_TIME,
      g_param_spec_int64 ("manual-exposure-time", "Manual Exposure Time",
           "Manual exposure time in nanoseconds. Used when the Exposure mode"
           " is set to 'off'.",
           0, G_MAXINT64, DEFAULT_PROP_CAMERA_EXPOSURE_TIME,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_EXPOSURE_TABLE,
      g_param_spec_string ("custom-exposure-table", "Custom Exposure Table",
          "A GstStructure describing custom exposure table",
          DEFAULT_PROP_CAMERA_EXPOSURE_TABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_WHITE_BALANCE_MODE,
      g_param_spec_enum ("white-balance-mode", "White Balance Mode",
          "The desired mode for the camera's white balance routine.",
          GST_TYPE_QMMFSRC_WHITE_BALANCE_MODE,
          DEFAULT_PROP_CAMERA_WHITE_BALANCE_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_WHITE_BALANCE_LOCK,
      g_param_spec_boolean ("white-balance-lock", "White Balance Lock",
          "Locks current White Balance values from changing. Affects only "
          "non-manual white balance modes.",
          DEFAULT_PROP_CAMERA_WHITE_BALANCE_LOCK,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_MANUAL_WB_SETTINGS,
      g_param_spec_string ("manual-wb-settings", "Manual WB Settings",
          "Manual White Balance settings such as color correction temperature "
          "and R/G/B gains. Used in manual white balance modes.",
          DEFAULT_PROP_CAMERA_MANUAL_WB_SETTINGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_FOCUS_MODE,
      g_param_spec_enum ("focus-mode", "Focus Mode",
          "Whether auto-focus is currently enabled, and in what mode it is.",
          GST_TYPE_QMMFSRC_FOCUS_MODE, DEFAULT_PROP_CAMERA_FOCUS_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_NOISE_REDUCTION,
      g_param_spec_enum ("noise-reduction", "Noise Reduction",
          "Noise reduction filter mode",
          GST_TYPE_QMMFSRC_NOISE_REDUCTION, DEFAULT_PROP_CAMERA_NOISE_REDUCTION,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_NOISE_REDUCTION_TUNING,
      g_param_spec_string ("noise-reduction-tuning", "Noise Reduction Tuning",
          "A GstStructure describing noise reduction tuning",
          DEFAULT_PROP_CAMERA_NOISE_REDUCTION_TUNING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_ZOOM,
      gst_param_spec_array ("zoom", "Zoom Rectangle",
          "Camera zoom rectangle ('<X, Y, WIDTH, HEIGHT >') in sensor active "
          "pixel array coordinates. Defaults to active-sensor-size values"
          " for 1x or no zoom",
          g_param_spec_int ("value", "Zoom Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_DEFOG_TABLE,
      g_param_spec_string ("defog-table", "Defog Table",
          "A GstStructure describing defog table",
          DEFAULT_PROP_CAMERA_DEFOG_TABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_LOCAL_TONE_MAPPING,
      g_param_spec_string ("ltm-data", "LTM Data",
          "A GstStructure describing local tone mapping data",
          DEFAULT_PROP_CAMERA_LOCAL_TONE_MAPPING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_IR_MODE,
      g_param_spec_enum ("infrared-mode", "IR Mode", "Infrared Mode",
          GST_TYPE_QMMFSRC_IR_MODE, DEFAULT_PROP_CAMERA_IR_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_ACTIVE_SENSOR_SIZE,
      gst_param_spec_array ("active-sensor-size", "Active Sensor Size",
          "The active pixel array of the camera sensor ('<X, Y, WIDTH, HEIGHT >')"
          " and it is filled only when the plugin is in READY or above state",
          g_param_spec_int ("value", "Sensor Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_SENSOR_MODE,
      g_param_spec_int ("sensor-mode", "Sensor Mode",
          "Force set Sensor Mode index (0-15). -1 for Auto selection",
          -1, 15, DEFAULT_PROP_CAMERA_SENSOR_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_VIDEO_METADATA,
      g_param_spec_pointer ("video-metadata", "Video Metadata",
          "Settings and parameters used for submitting capture requests for "
          "video streams in the form of CameraMetadata object. "
          "Caller is responsible for releasing the object.",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_IMAGE_METADATA,
      g_param_spec_pointer ("image-metadata", "Image Metadata",
          "Settings and parameters used for submitting capture requests for high "
          "quality images via the capture-image signal in the form of "
          "CameraMetadata object. Caller is responsible for releasing the object.",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_STATIC_METADATA,
      g_param_spec_pointer ("static-metadata", "Static Metadata",
          "Supported camera capabilities as CameraMetadata object. "
          "Caller is responsible for releasing the object.",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_SESSION_METADATA,
      g_param_spec_pointer ("session-metadata", "Session Metadata",
          "Settings parameters used for configure stream as "
          "CameraMetadata object. Caller is responsible for releasing the object.",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_FRC_MODE,
    g_param_spec_enum ("frc-mode", "Frame rate control",
          "Stream frame rate control mode.",
          GST_TYPE_QMMFSRC_FRC_MODE, DEFAULT_PROP_CAMERA_FRC_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_IFE_DIRECT_STREAM,
      g_param_spec_boolean ("ife-direct-stream", "IFE direct stream",
          "IFE direct stream support, with this param, ISP will generate"
          "output stream from IFE directly and skip others ISP modules"
          "like IPE",
          DEFAULT_PROP_CAMERA_IFE_DIRECT_STREAM,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_STATIC_METADATAS,
      g_param_spec_boxed ("static-metas", "Static Metadata's",
          "It contains the map of each connected camera and its metadata",
          G_TYPE_HASH_TABLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

#ifdef ENABLE_RUNTIME_PARSER
  void* qmmfsrc_parser = get_qmmfsrc_parser ();

  gboolean multi_camera_enable = get_flag_as_bool (qmmfsrc_parser,
      "MULTI_CAMERA_ENABLE");

  if (multi_camera_enable) {
    g_object_class_install_property (gobject, PROP_CAMERA_MULTI_CAM_EXPOSURE_TIME,
        gst_param_spec_array ("multi-camera-exp-time", "Multi Camera Exposure Time",
            "The exposure time (in nano-seconds) for each camera in multi camera"
            " setup ('<exp-time-1, exp-time-2>') and it is used only when"
            " exposure-mode is OFF",
            g_param_spec_int ("exp-time", "Exposure Time",
                "One of exp-time-1, exp-time-2 value.", 0, G_MAXINT, 0,
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_PLAYING));
  }
#else
#ifdef MULTI_CAMERA_ENABLE // MULTI_CAMERA_ENABLE
  g_object_class_install_property (gobject, PROP_CAMERA_MULTI_CAM_EXPOSURE_TIME,
      gst_param_spec_array ("multi-camera-exp-time", "Multi Camera Exposure Time",
          "The exposure time (in nano-seconds) for each camera in multi camera"
          " setup ('<exp-time-1, exp-time-2>') and it is used only when"
          " exposure-mode is OFF",
          g_param_spec_int ("exp-time", "Exposure Time",
              "One of exp-time-1, exp-time-2 value.", 0, G_MAXINT, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#endif  // MULTI_CAMERA_ENABLE
#endif // ENABLE_RUNTIME_PARSER

  g_object_class_install_property (gobject, PROP_CAMERA_OPERATION_MODE,
      g_param_spec_flags ("op-mode", "Camera operation mode",
          "provide camera operation mode to support specified camera function "
          "support mode : none, frameselection and fastswitch"
          "by default camera operation mode is none.",
          GST_TYPE_QMMFSRC_CAM_OPMODE, DEFAULT_PROP_CAMERA_OPERATION_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject, PROP_CAMERA_INPUT_ROI,
      g_param_spec_boolean ("input-roi-enable", "Input ROI reprocess enable",
          "Input ROI if enabled, Input ROI reprocess usecase will be selected",
          DEFAULT_PROP_CAMERA_MULTI_ROI,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_INPUT_ROI_INFO,
      gst_param_spec_array ("input-roi-info", "Input ROI info",
          "Applicable only if input-roi-enable property is set."
          "input-roi-info is array for each roi ('<X1, Y1, WIDTH1, HEIGHT1"
          " X2, Y2, WIDTH2, HEIGHT2, ...>')"
          " it needs to be filled for the no. of Input ROI's"
          " in playing state",
          g_param_spec_int ("value", "Input ROI coordinates",
              "One of X, Y, WIDTH, HEIGHT values.", 0, G_MAXINT, 0,
              G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#if defined (FEATURE_LOGICAL_CAMERA_SUPPORT) && \
  defined (FEATURE_LOGICAL_CAMERA_SENSOR_SWITCH)
  g_object_class_install_property (gobject, PROP_CAMERA_PHYSICAL_CAMERA_SWITCH,
      g_param_spec_int ("camera-switch-index", "set camera index for "
          "logical camera", "logica camera is a camera having a group of two"
          "or more physical sensors. logical camera includes several modes, "
          "SAT mode is where logical camera output the same size as any one of "
          "the physical sensor. the property is used to switch physical sensor's "
          "index within logical camera's all available physical sensors in SAT mode"
          "this property can be used to switch between different physical camera"
          "by their indexes. for example, camera-index=-1 will set "
          "next valid physical camera index, and camera-index=2 will select"
          "physical camera index 2", -1, 10,
          DEFAULT_PROP_CAMERA_PHYSICAL_CAMERA_SWITCH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#endif
  g_object_class_install_property (gobject, PROP_CAMERA_PAD_ACTIVATION_MODE,
      g_param_spec_enum ("video-pads-activation-mode", "Video Pad Activation Mode",
          "set video pad activation mode, by default is normal, use \"signal\" to "
          "control video pad activation by plugin signal \"video-pads-activation\" "
          "together with gst_pad_set_active() ",
          GST_TYPE_QMMFSRC_PAD_ACTIVATION_MODE,
          DEFAULT_PROP_CAMERA_PAD_ACTIVAION_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
#ifdef FEATURE_OFFLINE_IFE_SUPPORT
  g_object_class_install_property (gobject, PROP_CAMERA_MULTICAMERA_HINT,
      g_param_spec_boolean ("multicamera-hint", "multicamera-hint",
          "multicamera-hint if enabled, this flag will make camera hardwares "
          "to work in offline which is useful when camera sensors are more then "
          "camera hardwares, it has impact on memory usage and latency.",
          DEFAULT_PROP_CAMERA_MULTICAMERA_HINT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif // FEATURE_OFFLINE_IFE_SUPPORT
#ifdef FEATURE_SW_TNR
  g_object_class_install_property (gobject, PROP_CAMERA_SW_TNR,
      g_param_spec_boolean ("sw-tnr", "SW TNR",
          "this flag will enable sw based TNR.",
          DEFAULT_PROP_CAMERA_SW_TNR,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif // FEATURE_SW_TNR

  signals[SIGNAL_CAPTURE_IMAGE] =
      g_signal_new_class_handler ("capture-image", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (qmmfsrc_capture_image),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_QMMFSRC_CAPTURE_MODE,
      G_TYPE_UINT, G_TYPE_PTR_ARRAY);
  signals[SIGNAL_CANCEL_CAPTURE] =
      g_signal_new_class_handler ("cancel-capture", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (qmmfsrc_cancel_capture),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 0);

  signals[SIGNAL_VIDEO_PADS_ACTIVATION] =
      g_signal_new_class_handler ("video-pads-activation",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (qmmfsrc_signal_video_pads_activation), NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 2, G_TYPE_BOOLEAN, G_TYPE_PTR_ARRAY);

  signals[SIGNAL_RESULT_METADATA] =
      g_signal_new ("result-metadata", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[SIGNAL_URGENT_METADATA] =
      g_signal_new ("urgent-metadata", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);

  gstelement->request_new_pad = GST_DEBUG_FUNCPTR (qmmfsrc_request_pad);
  gstelement->release_pad = GST_DEBUG_FUNCPTR (qmmfsrc_release_pad);

  gstelement->send_event = GST_DEBUG_FUNCPTR (qmmfsrc_send_event);
  gstelement->change_state = GST_DEBUG_FUNCPTR (qmmfsrc_change_state);

  // Initializes a new qmmfsrc GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (qmmfsrc_debug, "qtiqmmfsrc", 0, "QTI QMMF Source");
}

// GObject element initialization function.
static void
qmmfsrc_init (GstQmmfSrc * qmmfsrc)
{
  GST_DEBUG_OBJECT (qmmfsrc, "Initializing");

  qmmfsrc->srcpads = g_hash_table_new (NULL, NULL);
  qmmfsrc->nextidx = 0;

  qmmfsrc->vidindexes = NULL;
  qmmfsrc->imgindexes = NULL;
  qmmfsrc->isplugged = FALSE;

  qmmfsrc->context = gst_qmmf_context_new (qmmfsrc_event_callback,
      qmmfsrc_metadata_callback, qmmfsrc);
  g_return_if_fail (qmmfsrc->context != NULL);

  GST_OBJECT_FLAG_SET (qmmfsrc, GST_ELEMENT_FLAG_SOURCE);
}

static GObject *
gst_qmmsrc_child_proxy_get_child_by_index (GstChildProxy * proxy, guint index)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (proxy);
  GObject *gobject = NULL;

  GST_QMMFSRC_LOCK (qmmfsrc);

  gobject = G_OBJECT (g_hash_table_lookup (
      qmmfsrc->srcpads, GUINT_TO_POINTER (index)));

  if (gobject != NULL)
    g_object_ref (gobject);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  return gobject;
}

static guint
gst_qmmsrc_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (proxy);
  guint count = 0;

  GST_QMMFSRC_LOCK (qmmfsrc);

  count = g_hash_table_size (qmmfsrc->srcpads);
  GST_INFO_OBJECT (qmmfsrc, "Children Count: %d", count);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  return count;
}

static void
gst_qmmfsrc_child_proxy_init (gpointer g_iface, gpointer data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_qmmsrc_child_proxy_get_child_by_index;
  iface->get_children_count = gst_qmmsrc_child_proxy_get_children_count;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiqmmfsrc", GST_RANK_PRIMARY,
      GST_TYPE_QMMFSRC);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiqmmfsrc,
    "QTI QMMF plugin library",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
