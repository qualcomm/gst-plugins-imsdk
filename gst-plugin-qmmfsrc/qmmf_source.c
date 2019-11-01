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
#include "qmmf_source_audio_pad.h"

// Declare static GstDebugCategory variable for qmmfsrc.
GST_DEBUG_CATEGORY_STATIC (qmmfsrc_debug);
#define GST_CAT_DEFAULT qmmfsrc_debug

#define DEFAULT_PROP_CAMERA_ID              0
#define DEFAULT_PROP_CAMERA_EFFECT_MODE     EFFECT_MODE_OFF
#define DEFAULT_PROP_CAMERA_SCENE_MODE      SCENE_MODE_DISABLED
#define DEFAULT_PROP_CAMERA_ANTIBANDING     ANTIBANDING_MODE_AUTO
#define DEFAULT_PROP_CAMERA_AE_COMPENSATION 0
#define DEFAULT_PROP_CAMERA_AE_LOCK         FALSE
#define DEFAULT_PROP_CAMERA_AWB_MODE        AWB_MODE_AUTO
#define DEFAULT_PROP_CAMERA_AWB_LOCK        FALSE

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
  PROP_CAMERA_EFFECT_MODE,
  PROP_CAMERA_SCENE_MODE,
  PROP_CAMERA_ANTIBANDING_MODE,
  PROP_CAMERA_AE_COMPENSATION,
  PROP_CAMERA_AE_LOCK,
  PROP_CAMERA_AWB_MODE,
  PROP_CAMERA_AWB_LOCK,
};

static GstStaticPadTemplate qmmfsrc_video_src_template =
    GST_STATIC_PAD_TEMPLATE("video_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_VIDEO_H264_CAPS "; "
            QMMFSRC_VIDEO_H264_CAPS_WITH_FEATURES (
                GST_CAPS_FEATURE_MEMORY_GBM) "; "
            QMMFSRC_VIDEO_RAW_CAPS(
                "{ NV12 }") "; "
            QMMFSRC_VIDEO_RAW_CAPS_WITH_FEATURES(
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }")
        )
    );

static GstStaticPadTemplate qmmfsrc_audio_src_template =
    GST_STATIC_PAD_TEMPLATE("audio_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_AUDIO_AAC_CAPS "; "
            QMMFSRC_AUDIO_AMR_CAPS "; "
            QMMFSRC_AUDIO_AMRWB_CAPS "; "
            QMMFSRC_AUDIO_PCM_CAPS
        )
    );

static GstStaticPadTemplate qmmfsrc_image_src_template =
    GST_STATIC_PAD_TEMPLATE("image_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_IMAGE_JPEG_CAPS "; "
            QMMFSRC_IMAGE_JPEG_CAPS_WITH_FEATURES (
                GST_CAPS_FEATURE_MEMORY_GBM)
        )
    );

static gboolean
qmmfsrc_pad_push_event (GstElement * element, GstPad * pad, GstEvent * event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_copy (event));
}

static gboolean
qmmfsrc_pad_send_event (GstElement * element, GstPad * pad, GstEvent * event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_send_event (pad, gst_event_copy (event));
}

static gboolean
qmmfsrc_pad_flush_buffers (GstElement * element, GstPad * pad, gpointer data)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  gboolean flush = GPOINTER_TO_UINT (data);
  GstSegment segment;

  GST_DEBUG_OBJECT (qmmfsrc, "Flush pad: %s", GST_PAD_NAME (pad));

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    qmmfsrc_video_pad_flush_buffers_queue (pad, flush);
  } else if (GST_IS_QMMFSRC_AUDIO_PAD (pad)) {
    qmmfsrc_audio_pad_flush_buffers_queue (pad, flush);
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    qmmfsrc_image_pad_flush_buffers_queue (pad, flush);
  }

  // Ensure segment (format) is properly setup.
  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (pad, gst_event_new_segment (&segment));

  return TRUE;
}

static GstPad*
qmmfsrc_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  gchar *padname = NULL;
  GHashTable *indexes = NULL;
  guint index = 0, nextindex = 0;
  gboolean isvideo = FALSE, isaudio = FALSE, isimage = FALSE;
  GstPad *srcpad = NULL;

  isvideo = (templ == gst_element_class_get_pad_template (klass, "video_%u"));
  isaudio = (templ == gst_element_class_get_pad_template (klass, "audio_%u"));
  isimage = (templ == gst_element_class_get_pad_template (klass, "image_%u"));

  if (!isvideo && !isaudio && !isimage) {
    GST_ERROR_OBJECT (qmmfsrc, "Invalid pad template");
    return NULL;
  }

  GST_QMMFSRC_LOCK (qmmfsrc);

  if ((reqname && sscanf (reqname, "video_%u", &index) == 1) ||
      (reqname && sscanf (reqname, "audio_%u", &index) == 1) ||
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

    indexes = qmmfsrc->vidindexes;
    g_free (padname);
  } else if (isaudio) {
    padname = g_strdup_printf ("audio_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting audio pad %s (%d)", padname, index);
    srcpad = qmmfsrc_request_audio_pad (templ, padname, index);

    indexes = qmmfsrc->audindexes;
    g_free (padname);
  } else if (isimage) {
    // Currently there is support for only one image pad.
    g_return_val_if_fail (g_hash_table_size (qmmfsrc->imgindexes) == 0, NULL);

    padname = g_strdup_printf ("image_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting image pad %d (%s)", index, padname);
    srcpad = qmmfsrc_request_image_pad (templ, padname, index);

    indexes = qmmfsrc->imgindexes;
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
  g_hash_table_insert (indexes, GUINT_TO_POINTER (index), NULL);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  gst_element_add_pad (element, srcpad);
  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (srcpad),
      GST_OBJECT_NAME (srcpad));

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
    g_hash_table_remove (qmmfsrc->vidindexes, GUINT_TO_POINTER (index));
  } else if (GST_IS_QMMFSRC_AUDIO_PAD (pad)) {
    index = GST_QMMFSRC_AUDIO_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing audio pad %d", index);

    qmmfsrc_release_audio_pad (element, pad);
    g_hash_table_remove (qmmfsrc->audindexes, GUINT_TO_POINTER (index));
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    index = GST_QMMFSRC_IMAGE_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing image pad %d", index);

    qmmfsrc_release_image_pad (element, pad);
    g_hash_table_remove (qmmfsrc->imgindexes, GUINT_TO_POINTER (index));
  }

  g_hash_table_remove (qmmfsrc->srcpads, GUINT_TO_POINTER (index));
  GST_DEBUG_OBJECT (qmmfsrc, "Deleted pad %d", index);

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

static gboolean
qmmfsrc_create_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  GHashTableIter iter;
  gpointer key, value;
  GstPad *pad;

  GST_TRACE_OBJECT (qmmfsrc, "Create session");

  success = gst_qmmf_context_create_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session creation failed!");

  g_hash_table_iter_init (&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    success = qmmfsrc_video_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate video caps!");

    success = gst_qmmf_context_create_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Video stream creation failed!");

    // Connect a callback to the 'notify' signal of a pad property to be
    // called when a that property changes during runtime.
    g_signal_connect (pad, "notify::bitrate",
        G_CALLBACK (gst_qmmf_context_update_video_param), qmmfsrc->context);
    g_signal_connect (pad, "notify::framerate",
        G_CALLBACK (gst_qmmf_context_update_video_param), qmmfsrc->context);
  }

  g_hash_table_iter_init (&iter, qmmfsrc->audindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    success = qmmfsrc_audio_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate audio caps!");

    success = gst_qmmf_context_create_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Audio stream creation failed!");
  }

  g_hash_table_iter_init (&iter, qmmfsrc->imgindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    success = qmmfsrc_image_pad_fixate_caps (pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Failed to fixate image caps!");

    success = gst_qmmf_context_create_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Image stream creation failed!");
  }

  GST_TRACE_OBJECT (qmmfsrc, "Session created");

  return TRUE;
}

static gboolean
qmmfsrc_delete_session (GstQmmfSrc * qmmfsrc)
{
  gboolean success = FALSE;
  GHashTableIter iter;
  gpointer key, value;
  GstPad *pad;

  GST_TRACE_OBJECT (qmmfsrc, "Delete session");

  g_hash_table_iter_init (&iter, qmmfsrc->audindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = gst_qmmf_context_delete_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Video stream deletion failed!");
  }

  g_hash_table_iter_init (&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = gst_qmmf_context_delete_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Audio stream deletion failed!");
  }

  g_hash_table_iter_init (&iter, qmmfsrc->imgindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    success = gst_qmmf_context_delete_stream (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Image stream deletion failed!");
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
  gboolean success = FALSE, flush = FALSE;

  GST_TRACE_OBJECT (qmmfsrc, "Starting session");

  success = gst_element_foreach_src_pad (
      GST_ELEMENT (qmmfsrc), qmmfsrc_pad_flush_buffers,
      GUINT_TO_POINTER (flush)
  );
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
  gboolean success = FALSE, flush = TRUE;

  GST_TRACE_OBJECT (qmmfsrc, "Stopping session");

  success = gst_element_foreach_src_pad (
      GST_ELEMENT (qmmfsrc), qmmfsrc_pad_flush_buffers,
      GUINT_TO_POINTER (flush)
  );
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  success = gst_qmmf_context_stop_session (qmmfsrc->context);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Session stop failed!");

  GST_TRACE_OBJECT (qmmfsrc, "Session stopped");

  return TRUE;
}

static gboolean
qmmfsrc_capture_image (GstQmmfSrc * qmmfsrc)
{
  GHashTableIter iter;
  gpointer key, value;

  GST_TRACE_OBJECT (qmmfsrc, "Submit capture image/s");

  g_hash_table_iter_init (&iter, qmmfsrc->imgindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstPad *pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));

    gboolean success = gst_qmmf_context_capture_image (qmmfsrc->context, pad);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Capture image failed!");
  }

  GST_TRACE_OBJECT (qmmfsrc, "Capture image/s submitted");

  return TRUE;
}

static gboolean
qmmfsrc_cancel_capture (GstQmmfSrc * qmmfsrc)
{
  GHashTableIter iter;
  gpointer key, value;

  GST_TRACE_OBJECT (qmmfsrc, "Canceling image capture");

  g_hash_table_iter_init (&iter, qmmfsrc->imgindexes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    gboolean success = gst_qmmf_context_cancel_capture (qmmfsrc->context);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
        "Cancel capture image failed!");
  }

  GST_TRACE_OBJECT (qmmfsrc, "Image capture canceled");

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
      if (!qmmfsrc_stop_session (qmmfsrc)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to stop session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
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
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_send_event, event
      );
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing FLUSH_STOP event");
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_send_event, event
      );
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
    case PROP_CAMERA_AE_COMPENSATION:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AE_COMPENSATION, value);
      break;
    case PROP_CAMERA_AE_LOCK:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AE_LOCK, value);
      break;
    case PROP_CAMERA_AWB_MODE:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AWB_MODE, value);
      break;
    case PROP_CAMERA_AWB_LOCK:
      gst_qmmf_context_set_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AWB_LOCK, value);
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
    case PROP_CAMERA_AE_COMPENSATION:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AE_COMPENSATION, value);
      break;
    case PROP_CAMERA_AE_LOCK:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AE_LOCK, value);
      break;
    case PROP_CAMERA_AWB_MODE:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AWB_MODE, value);
      break;
    case PROP_CAMERA_AWB_LOCK:
      gst_qmmf_context_get_camera_param (qmmfsrc->context,
          PARAM_CAMERA_AWB_LOCK, value);
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
    g_hash_table_remove_all (qmmfsrc->vidindexes);
    g_hash_table_destroy (qmmfsrc->vidindexes);
    qmmfsrc->vidindexes = NULL;
  }

  if (qmmfsrc->audindexes != NULL) {
    g_hash_table_remove_all (qmmfsrc->audindexes);
    g_hash_table_destroy (qmmfsrc->audindexes);
    qmmfsrc->audindexes = NULL;
  }

  if (qmmfsrc->imgindexes != NULL) {
    g_hash_table_remove_all (qmmfsrc->imgindexes);
    g_hash_table_destroy (qmmfsrc->imgindexes);
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
      &qmmfsrc_audio_src_template, GST_TYPE_QMMFSRC_AUDIO_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement,
      &qmmfsrc_image_src_template, GST_TYPE_QMMFSRC_IMAGE_PAD);

  gst_element_class_set_static_metadata (
      gstelement, "QMMF Video/Audio Source", "Source/Video",
      "Reads frames from a device via QMMF service", "QTI"
  );

  g_object_class_install_property (gobject, PROP_CAMERA_ID,
      g_param_spec_uint ("camera", "Camera ID",
          "Camera device ID to be used by video/image pads",
          0, 10, DEFAULT_PROP_CAMERA_ID,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
  g_object_class_install_property (gobject, PROP_CAMERA_AE_COMPENSATION,
      g_param_spec_int ("ae-compensation", "AE Compensation",
          "Auto Exposure Compensation",
          -12, 12, DEFAULT_PROP_CAMERA_AE_COMPENSATION,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_AE_LOCK,
      g_param_spec_boolean ("ae-lock", "AE Lock",
          "Auto Exposure lock", DEFAULT_PROP_CAMERA_AE_LOCK,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_AWB_MODE,
      g_param_spec_enum ("awb-mode", "AWB Mode",
           "Auto White Balance mode",
           GST_TYPE_QMMFSRC_AWB_MODE, DEFAULT_PROP_CAMERA_AWB_MODE,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CAMERA_AWB_LOCK,
      g_param_spec_boolean ("awb-lock", "AWB Lock",
          "Auto White Balance lock", DEFAULT_PROP_CAMERA_AWB_LOCK,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  signals[SIGNAL_CAPTURE_IMAGE] =
      g_signal_new_class_handler ("capture-image",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (qmmfsrc_capture_image),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  signals[SIGNAL_CANCEL_CAPTURE] =
      g_signal_new_class_handler ("cancel-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (qmmfsrc_cancel_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gstelement->request_new_pad = GST_DEBUG_FUNCPTR (qmmfsrc_request_pad);
  gstelement->release_pad = GST_DEBUG_FUNCPTR (qmmfsrc_release_pad);

  gstelement->send_event = GST_DEBUG_FUNCPTR (qmmfsrc_send_event);
  gstelement->change_state = GST_DEBUG_FUNCPTR (qmmfsrc_change_state);

  // Initializes a new qmmfsrc GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (qmmfsrc_debug, "qmmfsrc", 0, "QTI QMMF Source");
}

// GObject element initialization function.
static void
qmmfsrc_init (GstQmmfSrc * qmmfsrc)
{
  GST_DEBUG_OBJECT (qmmfsrc, "Initializing");

  qmmfsrc->srcpads = g_hash_table_new (NULL, NULL);
  qmmfsrc->nextidx = 0;

  qmmfsrc->vidindexes = g_hash_table_new (NULL, NULL);
  qmmfsrc->audindexes = g_hash_table_new (NULL, NULL);
  qmmfsrc->imgindexes = g_hash_table_new (NULL, NULL);

  qmmfsrc->context = gst_qmmf_context_new ();
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
  return gst_element_register (plugin, "qmmfsrc", GST_RANK_PRIMARY,
      GST_TYPE_QMMFSRC);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qmmfsrc,
    "QTI QMMF plugin library",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
