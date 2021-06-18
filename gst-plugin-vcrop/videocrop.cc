/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

#include <stdio.h>
#include "videocrop.h"
#include "videocrop-video-pad.h"

#define GST_INPUT_VIDEO_FORMATS "{ NV12 }"
#define GST_OUTPUT_VIDEO_FORMATS "{ NV12, RGB }"

#define GST_TYPE_VIDEO_CROP_TYPE (gst_video_crop_type_get_type())

#define DEFAULT_PROP_CROP_X             0
#define DEFAULT_PROP_CROP_Y             0
#define DEFAULT_PROP_CROP_WIDTH         0
#define DEFAULT_PROP_CROP_HEIGHT        0
#define DEFAULT_PROP_CROP_TYPE          GST_VIDEO_CROP_TYPE_C2D
#define DEFAULT_PROP_MAX_BUFFERS        10

enum
{
  PROP_0,
  PROP_CROP,
  PROP_CROP_TYPE,
  PROP_MAX_BUFFERS,
};

static GType
gst_video_crop_type_get_type (void)
{
  static GType video_crop_type = 0;
  static const GEnumValue methods[] = {
    { GST_VIDEO_CROP_TYPE_C2D,
        "Crop type: C2D", "C2D"
    },
    { GST_VIDEO_CROP_TYPE_FASTCV,
        "Crop type: FastCV", "FastCV"
    },
    {0, NULL, NULL},
  };
  if (!video_crop_type) {
    video_crop_type =
        g_enum_register_static ("GstVideoCropType", methods);
  }
  return video_crop_type;
}

static GstStaticPadTemplate video_crop_src_template =
    GST_STATIC_PAD_TEMPLATE("video_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_OUTPUT_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_OUTPUT_VIDEO_FORMATS))
    );

static GstStaticPadTemplate video_crop_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_INPUT_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_INPUT_VIDEO_FORMATS))
  );

#define GST_CAT_DEFAULT video_crop_debug
GST_DEBUG_CATEGORY_STATIC (video_crop_debug);

#define gst_video_crop_parent_class parent_class
G_DEFINE_TYPE (GstVideoCrop, gst_video_crop, GST_TYPE_ELEMENT);

static void
gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (vcrop);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (vcrop, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (vcrop);

  switch (prop_id) {
    case PROP_CROP:
      g_return_if_fail (gst_value_array_get_size (value) == 4);
      vcrop->crop.x = g_value_get_int (gst_value_array_get_value (value, 0));
      vcrop->crop.y = g_value_get_int (gst_value_array_get_value (value, 1));
      vcrop->crop.w = g_value_get_int (gst_value_array_get_value (value, 2));
      vcrop->crop.h = g_value_get_int (gst_value_array_get_value (value, 3));
      break;
    case PROP_CROP_TYPE:
      vcrop->crop_type = (GstVideoCropType) g_value_get_enum (value);
      break;
    case PROP_MAX_BUFFERS:
      vcrop->maxbuffers = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (vcrop);
}

static void
gst_video_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (object);

  GST_OBJECT_LOCK (vcrop);

  switch (prop_id) {
    case PROP_CROP:
      {
        GValue val = G_VALUE_INIT;
        g_value_init (&val, G_TYPE_INT);

        g_value_set_int (&val, vcrop->crop.x);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, vcrop->crop.y);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, vcrop->crop.w);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, vcrop->crop.h);
        gst_value_array_append_value (value, &val);
        break;
      }
    case PROP_CROP_TYPE:
      g_value_set_enum (value, vcrop->crop_type);
      break;
    case PROP_MAX_BUFFERS:
      g_value_set_uint (value, vcrop->maxbuffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (vcrop);
}

static gboolean
gst_video_crop_process (GstVideoCrop * vcrop, GstBuffer * buffer,
    GstVideoRectangle * crop, gboolean input_is_free)
{

  if (!vcrop->have_segment || !vcrop->inputcaps) {
    GST_ERROR_OBJECT (vcrop, "1.0, missing new_segment or caps, returning");
    return FALSE;
  }

  if (vcrop->pads_process) {
    VideoCropPadProcess *process =
        (VideoCropPadProcess *) vcrop->pads_process->data;
    process->SetCrop (crop);
    if (!process->Process (input_is_free, buffer)) {
      GST_ERROR_OBJECT (vcrop, "ERROR: Pad process failed!");
      return FALSE;
    }
  }

  return TRUE;
}

static GSList *
gst_buffer_iterate_video_region_of_interest_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;
  const GstMetaInfo *info = GST_VIDEO_REGION_OF_INTEREST_META_INFO;

  g_return_val_if_fail (buffer != NULL, NULL);

  GSList *meta_list = NULL;
  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      meta_list = g_slist_prepend(meta_list, meta);
    }
  }
  return meta_list;
}

static GstFlowReturn
gst_video_crop_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (object);
  gboolean res = TRUE;

  GSList *meta_list = gst_buffer_iterate_video_region_of_interest_meta (buffer);
  if (meta_list) {
    guint meta_num = g_slist_length (meta_list);
    for (gint i = 0; i < meta_num; i++) {
      GstVideoRegionOfInterestMeta *roi_meta =
          (GstVideoRegionOfInterestMeta *) g_slist_nth_data (meta_list, i);

      GstVideoRectangle crop;
      crop.x = roi_meta->x;
      crop.y = roi_meta->y;
      crop.w = roi_meta->w;
      crop.h = roi_meta->h;
      res = gst_video_crop_process (vcrop, buffer, &crop, FALSE);
      if (!res) {
        GST_ERROR_OBJECT (vcrop, "ERROR: Failed to process crop!");
        return GST_FLOW_ERROR;
      }
    }
    gst_buffer_unref (buffer);
  } else {
    res = gst_video_crop_process (vcrop, buffer, &vcrop->crop, TRUE);
    if (!res) {
      GST_ERROR_OBJECT (vcrop, "ERROR: Failed to process crop!");
      return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}

static gint
gst_video_crop_sort_pads (const gpointer lproc,
    const gpointer rproc, GstVideoCrop * vcrop)
{
  GstCaps *caps;
  GstVideoInfo video_info;
  GstPad *lpad;
  GstPad *rpad;
  gint lwidth = 0;
  gint lheight = 0;
  gint rwidth = 0;
  gint rheight = 0;

  lpad = ((VideoCropPadProcess *) lproc)->GetPad();
  rpad = ((VideoCropPadProcess *) rproc)->GetPad();

  caps = gst_pad_get_current_caps (lpad);
  if (!caps) {
    GST_ERROR_OBJECT (vcrop, "Error: Failed to get caps");
    return 0;
  }

  if (!gst_video_info_from_caps (&video_info, caps)) {
    GST_ERROR_OBJECT (vcrop, "Error: Failed to get video info from caps");
    return 0;
  }

  lwidth = GST_VIDEO_INFO_WIDTH(&video_info);
  lheight = GST_VIDEO_INFO_HEIGHT(&video_info);

  caps = gst_pad_get_current_caps (rpad);
  if (!caps) {
    GST_ERROR_OBJECT (vcrop, "Error: Failed to get caps");
    return 0;
  }

  if (!gst_video_info_from_caps (&video_info, caps)) {
    GST_ERROR_OBJECT (vcrop, "Error: Failed to get video info from caps");
    return 0;
  }

  rwidth = GST_VIDEO_INFO_WIDTH(&video_info);
  rheight = GST_VIDEO_INFO_HEIGHT(&video_info);

  if (lwidth > rwidth && lheight > rheight) {
    return -1;
  } else if (lwidth < rwidth && lheight < rheight) {
    return 1;
  }

  return 0;
}

static gboolean
gst_video_crop_sink_event (GstPad * pad, GstObject * object, GstEvent * event)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (object);
  GstCaps *caps;
  gpointer key;
  GstPad *src_pad;
  GList *list = NULL;
  GstVideoInfo in_video_info;

  GST_DEBUG_OBJECT (vcrop, "received event %p of type %s (%d)",
      event, gst_event_type_get_name (event->type), event->type);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      GST_DEBUG_OBJECT (vcrop, "Received stream start");
      break;
    case GST_EVENT_SEGMENT:
      GST_DEBUG_OBJECT (vcrop, "Received segment %" GST_PTR_FORMAT, event);
      gst_event_copy_segment (event, &vcrop->segment);
      vcrop->have_segment = TRUE;

      // Set segment of all src pads
      for (list = vcrop->pads_process; list != NULL; list = list->next) {
        VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
        if (process) {
          process->SetSinkSegment (vcrop->segment);
        }
      }
      break;
    case GST_EVENT_CAPS:{
      GST_DEBUG_OBJECT (vcrop, "Received caps %" GST_PTR_FORMAT, event);
      gst_event_parse_caps (event, &caps);
      if (vcrop->inputcaps == NULL || !gst_caps_is_equal (vcrop->inputcaps, caps)) {
        GST_INFO_OBJECT (pad, "caps changed to %" GST_PTR_FORMAT, caps);
        gst_caps_replace (&vcrop->inputcaps, caps);
        if (!gst_video_info_from_caps (&in_video_info, vcrop->inputcaps)) {
          GST_ERROR_OBJECT (vcrop,
              "Error: Failed to get input video info from caps");
          return FALSE;
        }

        // Fixate caps of all src pads
        for (list = vcrop->pads_process; list != NULL; list = list->next) {
          VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
          if (process) {
            GST_DEBUG_OBJECT (vcrop, "Fixate src caps");
            videocrop_video_pad_fixate_caps (process->GetPad());
          }
        }

        // Sort srcpads to width and height
        vcrop->pads_process = g_list_sort_with_data (vcrop->pads_process,
            (GCompareDataFunc) gst_video_crop_sort_pads, vcrop);

        // Allocate buffers of all src pads
        VideoCropPadProcess *process_prev = NULL;
        for (list = vcrop->pads_process; list != NULL; list = list->next) {
          VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
          if (process) {
            if (process_prev) {
              // Set process instance to the previous process
              process_prev->SetNextProcess (process);

              caps = gst_pad_get_current_caps (process_prev->GetPad());
              if (!caps) {
                GST_ERROR_OBJECT (vcrop, "Error: Failed to get src caps");
                return FALSE;
              }

              if (!gst_video_info_from_caps (&in_video_info, caps)) {
                GST_ERROR_OBJECT (vcrop,
                    "Error: Failed to get input video info from caps");
                return FALSE;
              }
            }
            process_prev = process;

            GST_DEBUG_OBJECT (vcrop, "Pad process init");
            process->Init (in_video_info);
          }
        }
      }
      break;
    }
    default:
      break;
  }

  /* if we have EOS, we should send on EOS ourselves */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS
      || GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    GST_DEBUG_OBJECT (vcrop, "Sending on event %" GST_PTR_FORMAT, event);
    for (list = vcrop->pads_process; list != NULL; list = list->next) {
      VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
      if (!gst_pad_push_event (process->GetPad(), gst_event_ref (event))) {
        GST_ERROR_OBJECT (vcrop, "Error gst_pad_push_event");
      }
    }
  }

  gst_event_unref (event);
  return TRUE;
}

static GstStateChangeReturn
gst_video_crop_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstVideoCrop *vcrop = GST_VIDEO_CROP (element);
  gpointer key;
  GstPad *src_pad;
  GList *list = NULL;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      // Dealocate buffers of all src pads
      for (list = vcrop->pads_process; list != NULL; list = list->next) {
        VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
        process->Deinit ();
        vcrop->inputcaps = NULL;
        vcrop->have_segment = FALSE;
      }
      vcrop->inputcaps = NULL;
      vcrop->have_segment = FALSE;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
is_src_index_free (GstVideoCrop *vcrop, gint index)
{
  GList *list = NULL;
  for (list = vcrop->pads_process; list != NULL; list = list->next) {
    VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
    if (process->GetIndex() == index) {
      return FALSE;
    }
  }
  return TRUE;
}

static GstPad*
video_crop_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  gchar *padname = NULL;
  guint index = 0, nextindex = 0;
  gboolean isvideo = FALSE;
  GstPad *srcpad = NULL;

  isvideo = (templ == gst_element_class_get_pad_template (klass, "video_%u"));

  if (!isvideo) {
    GST_ERROR_OBJECT (vcrop, "Invalid pad template");
    return NULL;
  }

  GST_VIDEO_CROP_LOCK (vcrop);

  if ((reqname && sscanf (reqname, "video_%u", &index) == 1)) {
    if (!is_src_index_free (vcrop, index)) {
      GST_ERROR_OBJECT (vcrop, "Source pad name %s is not unique", reqname);
      GST_VIDEO_CROP_UNLOCK (vcrop);
      return NULL;
    }
    // Update the next video pad index set his name.
    nextindex = (index >= vcrop->nextidx) ? index + 1 : vcrop->nextidx;
  } else {
    index = vcrop->nextidx;
    // Find an unused source pad index.
    while (!is_src_index_free (vcrop, index)) {
      index++;
    }
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  if (isvideo) {
    padname = g_strdup_printf ("video_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting video pad %s (%d)", padname, index);
    srcpad = videocrop_request_video_pad (templ, padname, index);
    g_free (padname);
  }

  if (srcpad == NULL) {
    GST_ERROR_OBJECT (element, "Failed to create pad %d!", index);
    GST_VIDEO_CROP_UNLOCK (vcrop);
    return NULL;
  }

  GST_DEBUG_OBJECT (vcrop, "Created pad with index %d", index);

  vcrop->nextidx = nextindex;

  GST_VIDEO_CROP_UNLOCK (vcrop);

  gst_element_add_pad (element, srcpad);

  GST_DEBUG_OBJECT (vcrop, "Create process instance");
  vcrop->pads_process =
      g_list_append (vcrop->pads_process,
          new VideoCropPadProcess (srcpad, index, vcrop->crop_type,
              vcrop->maxbuffers));

  return srcpad;
}

static void
video_crop_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (element);
  guint index = 0;

  GST_VIDEO_CROP_LOCK (vcrop);

  index = GST_VIDEOCROP_VIDEO_PAD (pad)->index;
  GST_DEBUG_OBJECT (element, "Releasing video pad %d", index);

  videocrop_release_video_pad (element, pad);

  GList *list = NULL;
  for (list = vcrop->pads_process; list != NULL; list = list->next) {
    VideoCropPadProcess *process = (VideoCropPadProcess *) list->data;
    if (process->GetIndex() == index) {
      GST_DEBUG_OBJECT (vcrop, "Remove process instance");
      vcrop->pads_process = g_list_remove (vcrop->pads_process, process);
      delete (process);
      break;
    }
  }

  GST_DEBUG_OBJECT (vcrop, "Deleted pad %d", index);

  GST_VIDEO_CROP_UNLOCK (vcrop);
}

static void
gst_video_crop_finalize (GObject * object)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (object);

  if (vcrop->pads_process != NULL) {
    g_list_free (vcrop->pads_process);
    vcrop->pads_process = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vcrop));
}

static void
gst_video_crop_class_init (GstVideoCropClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_crop_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_crop_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_video_crop_finalize);

  g_object_class_install_property (gobject, PROP_CROP,
      gst_param_spec_array ("crop", "Crop rectangle",
          "The crop rectangle ('<X, Y, WIDTH, HEIGHT >')",
          g_param_spec_int ("value", "Crop Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)),
              (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CROP_TYPE,
      g_param_spec_enum ("crop-type", "Crop type", "Crop Type",
          GST_TYPE_VIDEO_CROP_TYPE, DEFAULT_PROP_CROP_TYPE,
          (GParamFlags) (G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject, PROP_MAX_BUFFERS,
        g_param_spec_uint ("max-buffers", "Maximum output buffers count",
            "The maximum count of the output buffers",
            3, 50, DEFAULT_PROP_MAX_BUFFERS,
            (GParamFlags) (G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
                G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element,
      "Video crop", "Video/crop",
      "Crop, resize converts video", "QTI");

  gst_element_class_add_static_pad_template (element,
      &video_crop_src_template);

  gst_element_class_add_static_pad_template (element,
      &video_crop_sink_template);


  element->request_new_pad = GST_DEBUG_FUNCPTR (video_crop_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (video_crop_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_video_crop_change_state);
}

static void
gst_video_crop_init (GstVideoCrop * vcrop)
{
  vcrop->sinkpad =
      gst_pad_new_from_static_template (&video_crop_sink_template, "sink");
  gst_pad_set_chain_function (vcrop->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_crop_chain));
  gst_pad_set_event_function (vcrop->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_crop_sink_event));
  gst_element_add_pad (GST_ELEMENT (vcrop), vcrop->sinkpad);

  vcrop->pads_process = NULL;
  vcrop->nextidx = 0;
  vcrop->inputcaps = NULL;
  vcrop->crop.x = DEFAULT_PROP_CROP_X;
  vcrop->crop.y = DEFAULT_PROP_CROP_Y;
  vcrop->crop.w = DEFAULT_PROP_CROP_WIDTH;
  vcrop->crop.h = DEFAULT_PROP_CROP_HEIGHT;

  GST_DEBUG_CATEGORY_INIT (video_crop_debug, "qtivcrop", 0, "QTI video crop");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivcrop", GST_RANK_PRIMARY,
      GST_TYPE_VIDEO_CROP);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivcrop,
    "Crop and resize main video and output on multiple pads",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
