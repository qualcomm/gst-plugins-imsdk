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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qmmf_source.h"

#include <stdio.h>

#include <gst/gstplugin.h>
#include <gst/gstpadtemplate.h>
#include <gst/gstelementfactory.h>
#include <gst/allocators/allocators.h>

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
#define DEFAULT_PROP_CAMERA_EIS_MODE                  FALSE
#define DEFAULT_PROP_CAMERA_SHDR_MODE                 FALSE
#define DEFAULT_PROP_CAMERA_ADRC                      FALSE
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
  PROP_CAMERA_SHDR,
  PROP_CAMERA_ADRC,
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
};

static GstStaticPadTemplate qmmfsrc_video_src_template =
    GST_STATIC_PAD_TEMPLATE("video_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_VIDEO_JPEG_CAPS "; "
            QMMFSRC_VIDEO_RAW_CAPS(
#if defined(GST_VIDEO_YUY2_FORMAT_ENABLE)
                "{ NV12, NV16, YUY2 }") "; "
#else
                "{ NV12, NV16 }") "; "
#endif
            QMMFSRC_VIDEO_RAW_CAPS_WITH_FEATURES(
                GST_CAPS_FEATURE_MEMORY_GBM,
#if defined(GST_VIDEO_YUY2_FORMAT_ENABLE)
                "{ NV12, YUY2 }") "; "
#else
                "{ NV12 }") "; "
#endif
            QMMFSRC_VIDEO_BAYER_CAPS(
                "{ bggr, rggb, gbrg, grbg, mono }",
                "{ 8, 10, 12, 16 }")
        )
    );

static GstStaticPadTemplate qmmfsrc_image_src_template =
    GST_STATIC_PAD_TEMPLATE("image_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_IMAGE_JPEG_CAPS "; "
            QMMFSRC_IMAGE_RAW_CAPS(
                "{ NV21 }") "; "
            QMMFSRC_IMAGE_RAW_CAPS_WITH_FEATURES(
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV21 }") "; "
            QMMFSRC_IMAGE_BAYER_CAPS(
                "{ bggr, rggb, gbrg, grbg, mono }",
                "{ 8, 10, 12, 16 }")
        )
    );

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
    // Currently there is support for only two image pad.
    // Two image pad is required to accommodate Jpeg and Bayer feature.
    g_return_val_if_fail (g_list_length (qmmfsrc->imgindexes) <= 1, NULL);

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
  return srcpad;
}

static void
qmmfsrc_release_pad (GstElement * element, GstPad * pad)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  guint index = 0;

  GST_QMMFSRC_LOCK (qmmfsrc);

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    index = GST_QMMFSRC_VIDEO_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing video pad %d", index);

    qmmfsrc_release_video_pad (element, pad);
    qmmfsrc->vidindexes =
        g_list_remove (qmmfsrc->vidindexes, GUINT_TO_POINTER (index));
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    index = GST_QMMFSRC_IMAGE_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing image pad %d", index);

    qmmfsrc_release_image_pad (element, pad);
    qmmfsrc->imgindexes =
        g_list_remove (qmmfsrc->imgindexes, GUINT_TO_POINTER (index));
  }

  g_hash_table_remove (qmmfsrc->srcpads, GUINT_TO_POINTER (index));
  GST_DEBUG_OBJECT (qmmfsrc, "Deleted pad %d", index);

  GST_QMMFSRC_UNLOCK (qmmfsrc);
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

static gboolean
qmmfsrc_create_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  gpointer key;
  GstPad *pad = NULL, *jpegpad = NULL, *bayerpad = NULL;
  GList *list = NULL;

  GST_TRACE_OBJECT (qmmfsrc, "Create session");

  success = gst_qmmf_context_create_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session creation failed!");

  // Iterate over the video pads, fixate caps and create streams.
  for (list = qmmfsrc->vidindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = qmmfsrc_video_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate video caps!");

    success = gst_qmmf_context_create_video_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Video stream creation failed!");
  }

  // Iterate over the image pads, fixate caps and create streams.
  for (list = qmmfsrc->imgindexes; list != NULL; list = list->next) {
    key = list->data;
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = qmmfsrc_image_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate image caps!");

    if (GST_QMMFSRC_IMAGE_PAD (pad)->codec == GST_IMAGE_CODEC_JPEG)
      jpegpad = pad;

    if (GST_QMMFSRC_IMAGE_PAD (pad)->format >= GST_BAYER_FORMAT_OFFSET)
      bayerpad = pad;
  }

  // This is to check whether 2 image pad are of Jpeg and Bayer format or not.
  qmmfsrc->jpegbayerenabled = (jpegpad != NULL && bayerpad != NULL) ?
      TRUE : FALSE;

  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc,
      !(g_list_length (qmmfsrc->imgindexes) == 2 &&
      !qmmfsrc->jpegbayerenabled), FALSE,
      "Image pad combination is not correct.");

  if (qmmfsrc->jpegbayerenabled) {
    success = gst_qmmf_context_create_image_stream (qmmfsrc->context,
        jpegpad, bayerpad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Image stream creation failed!");
  } else {
    if (g_list_length (qmmfsrc->imgindexes) > 0) {
      pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads,
          (qmmfsrc->imgindexes)->data));

      success = gst_qmmf_context_create_image_stream (qmmfsrc->context,
          pad, NULL);
      QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
          "Image stream creation failed!");
    }
  }

  GST_TRACE_OBJECT (qmmfsrc, "Session created");

  return TRUE;
}

static gboolean
qmmfsrc_delete_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  gpointer key;
  GstPad *pad;
  GList *list = NULL;

  GST_TRACE_OBJECT (qmmfsrc, "Delete session");

  if (g_list_length (qmmfsrc->imgindexes) > 0) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads,
        (qmmfsrc->imgindexes)->data));

    success = gst_qmmf_context_delete_image_stream (qmmfsrc->context, pad);
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

  success = gst_qmmf_context_delete_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session deletion failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Session deleted");

  return TRUE;
}

static gboolean
qmmfsrc_start_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;

  GST_TRACE_OBJECT (qmmfsrc, "Starting session");

  success = gst_element_foreach_src_pad (GST_ELEMENT (qmmfsrc),
      qmmfsrc_pad_flush_buffers, GUINT_TO_POINTER (FALSE));
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  success = gst_qmmf_context_start_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session start failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Session started");

  return TRUE;
}

static gboolean
qmmfsrc_stop_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;

  GST_TRACE_OBJECT (qmmfsrc, "Stopping session");

  success = gst_element_foreach_src_pad (GST_ELEMENT (qmmfsrc),
      qmmfsrc_pad_flush_buffers, GUINT_TO_POINTER (TRUE));
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  success = gst_qmmf_context_stop_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session stop failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Session stopped");

  return TRUE;
}

static gboolean
qmmfsrc_pause_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE, flush = TRUE;

  GST_TRACE_OBJECT (qmmfsrc, "Pausing session");

  success = gst_qmmf_context_pause_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session pause failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Session paused");

  return TRUE;
}

static gboolean
qmmfsrc_capture_image (GstQmmfSrc * qmmfsrc)
{
  gpointer key;
  GList *list = NULL;
  gboolean success = FALSE;
  GstPad *pad = NULL, *jpegpad = NULL, *bayerpad = NULL;

  GST_TRACE_OBJECT (qmmfsrc, "Submit capture image/s");

  if (qmmfsrc->jpegbayerenabled) {
    for (list = qmmfsrc->imgindexes; list != NULL; list = list->next) {
      key = list->data;
      pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

      if (GST_QMMFSRC_IMAGE_PAD (pad)->codec == GST_IMAGE_CODEC_JPEG)
        jpegpad = pad;

      if (GST_QMMFSRC_IMAGE_PAD (pad)->format >= GST_BAYER_FORMAT_OFFSET)
        bayerpad = pad;
    }

    success = gst_qmmf_context_capture_image (qmmfsrc->context, jpegpad,
        bayerpad);

    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Capture image failed!");
  } else {
    if (g_list_length (qmmfsrc->imgindexes) > 0) {
      pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads,
          (qmmfsrc->imgindexes)->data));

      success = gst_qmmf_context_capture_image (qmmfsrc->context, pad, NULL);
      QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
          "Capture image failed!");
    }
  }
  GST_TRACE_OBJECT (qmmfsrc, "Capture image/s submitted");

  return TRUE;
}

static GstStateChangeReturn
qmmfsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_qmmf_context_open (qmmfsrc->context)) {
        GST_ERROR_OBJECT (qmmfsrc, "Failed to Open!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!qmmfsrc_create_session (qmmfsrc)) {
        GST_ERROR_OBJECT (qmmfsrc, "Failed to create session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!qmmfsrc_start_session (qmmfsrc)) {
        GST_ERROR_OBJECT (qmmfsrc, "Failed to start session!");
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
      if (!qmmfsrc_pause_session (qmmfsrc)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to pause session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      // Return NO_PREROLL to inform bin/pipeline we won't be able to
      // produce data in the PAUSED state, as this is a live source.
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!qmmfsrc_stop_session (qmmfsrc)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to stop session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      if (!qmmfsrc_delete_session (qmmfsrc)) {
        GST_ERROR_OBJECT (qmmfsrc, "Failed to delete session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_qmmf_context_close (qmmfsrc->context)) {
        GST_ERROR_OBJECT (qmmfsrc, "Failed to Close!");
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

  if (!QMMFSRC_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
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
    case PROP_CAMERA_SHDR:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SHDR, value);
      break;
    case PROP_CAMERA_ADRC:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ADRC, value);
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
    case PROP_CAMERA_SHDR:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_SHDR, value);
      break;
    case PROP_CAMERA_ADRC:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_ADRC, value);
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

  gst_element_class_add_static_pad_template_with_gtype (gstelement,
      &qmmfsrc_video_src_template, GST_TYPE_QMMFSRC_VIDEO_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement,
      &qmmfsrc_image_src_template, GST_TYPE_QMMFSRC_IMAGE_PAD);

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
  g_object_class_install_property (gobject, PROP_CAMERA_EIS,
      g_param_spec_boolean ("eis", "EIS",
          "Electronic Image Stabilization to reduce the effects of camera shake",
          DEFAULT_PROP_CAMERA_EIS_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_SHDR,
      g_param_spec_boolean ("shdr", "SHDR",
          "Super High Dynamic Range Imaging", DEFAULT_PROP_CAMERA_SHDR_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CAMERA_ADRC,
      g_param_spec_boolean ("adrc", "ADRC",
          "Automatic Dynamic Range Compression", DEFAULT_PROP_CAMERA_ADRC,
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
          "pixel array coordinates",
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


  signals[SIGNAL_CAPTURE_IMAGE] =
      g_signal_new_class_handler ("capture-image", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (qmmfsrc_capture_image),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

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

  qmmfsrc->context = gst_qmmf_context_new (
      G_CALLBACK (qmmfsrc_event_callback), qmmfsrc);
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
