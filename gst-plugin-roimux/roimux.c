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
#include "roimux.h"

#define GST_INPUT_VIDEO_FORMATS "{ NV12 }"
#define GST_OUTPUT_VIDEO_FORMATS "{ NV12 }"

typedef struct _GstRoiData GstRoiData;
struct _GstRoiData {
  guint64 timestamp;
  GstVideoRectangle *rects;
  gsize rects_size;
};

enum
{
  PROP_0,
};

static GstStaticPadTemplate roimux_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_INPUT_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_INPUT_VIDEO_FORMATS))
    );

static GstStaticPadTemplate roimux_sink_0_template =
GST_STATIC_PAD_TEMPLATE ("sink_0",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_INPUT_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_INPUT_VIDEO_FORMATS))
  );

static GstStaticPadTemplate roimux_sink_1_template =
GST_STATIC_PAD_TEMPLATE ("sink_1",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
  );

#define GST_CAT_DEFAULT roimux_debug
GST_DEBUG_CATEGORY_STATIC (roimux_debug);

#define gst_roimux_parent_class parent_class
G_DEFINE_TYPE (GstRoiMux, gst_roimux, GST_TYPE_ELEMENT);

static void
gst_roimux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (roimux);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (roimux, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (roimux);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (roimux);
}

static void
gst_roimux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRoiMux *roimux = GST_ROIMUX (object);

  GST_OBJECT_LOCK (roimux);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (roimux);
}

static gboolean
gst_roimux_parse_config (GstRoiMux * roimux)
{
  GstStructure *structure = NULL;
  gint entry_num = 0;
  GValue gvalue = G_VALUE_INIT;

  g_value_init (&gvalue, GST_TYPE_STRUCTURE);
  gchar *contents = (gchar *) roimux->config_data;

  contents = g_strstrip (contents);
  contents = g_strdelimit (contents, "\n", ',');

  if (!gst_value_deserialize (&gvalue, contents)) {
    GST_ERROR_OBJECT (
        roimux, "ERROR: Failed to deserialize config data contents!");
    return FALSE;
  }

  structure = GST_STRUCTURE (g_value_dup_boxed (&gvalue));
  g_value_unset (&gvalue);

  while (TRUE) {
    gchar entry_str[100] = {0};
    snprintf (entry_str, sizeof (entry_str), "%d", entry_num);

    const GValue *value_entry = gst_structure_get_value (structure, entry_str);
    if (value_entry == NULL) {
      GST_DEBUG_OBJECT (roimux, "ERROR: Failed to get value or end of stream!");
      break;
    }

    GstRoiData *roi_data = g_new0 (GstRoiData, 1);
    const GValue *value = gst_value_array_get_value (value_entry, 0);
    if (!G_VALUE_HOLDS_UINT64 (value)) {
      GST_ERROR_OBJECT (roimux, "ERROR: Failed to to get timestamp!");
      gst_structure_free (structure);
      return FALSE;
    }

    roi_data->timestamp = g_value_get_uint64 (value);
    roi_data->rects = g_new0 (GstVideoRectangle, gst_value_array_get_size (
        value_entry) - 1);
    roi_data->rects_size = gst_value_array_get_size (value_entry) - 1;

    for (gsize i = 1; i < gst_value_array_get_size (value_entry); i++) {
      value = (GValue *) gst_value_array_get_value (value_entry, i);
      if (gst_value_array_get_size (value) != 4) {
        GST_ERROR_OBJECT (roimux, "ERROR: Failed to to get roi!");
        gst_structure_free (structure);
        return FALSE;
      }

      roi_data->rects[i - 1].x = g_value_get_int (gst_value_array_get_value (
          value, 0));
      roi_data->rects[i - 1].y = g_value_get_int (gst_value_array_get_value (
          value, 1));
      roi_data->rects[i - 1].w = g_value_get_int (gst_value_array_get_value (
          value, 2));
      roi_data->rects[i - 1].h = g_value_get_int (gst_value_array_get_value (
          value, 3));
    }

    roimux->roi_data_list = g_list_append (roimux->roi_data_list, roi_data);
    entry_num++;
  }

  gst_structure_free (structure);
  roimux->is_config_parsed = TRUE;

  return TRUE;
}

static gboolean
gst_roimux_sink_event (GstPad * pad, GstObject * object, GstEvent * event)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  GstCaps *caps = NULL;
  GstVideoInfo vinfo;

  GST_DEBUG_OBJECT (roimux, "received event %p of type %s (%d)",
      event, gst_event_type_get_name (event->type), event->type);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      GST_DEBUG_OBJECT (roimux, "Received stream start");
      break;
    case GST_EVENT_SEGMENT:
      GST_DEBUG_OBJECT (roimux, "Received segment %" GST_PTR_FORMAT, event);
      roimux->have_segment = TRUE;
      break;
    case GST_EVENT_CAPS:{
      GST_DEBUG_OBJECT (roimux, "Received caps %" GST_PTR_FORMAT, event);
      gst_event_parse_caps (event, &caps);

      //gst_buffer_replace (&outbuffer, NULL);
      if (roimux->caps == NULL || !gst_caps_is_equal (roimux->caps, caps)) {
        GST_INFO_OBJECT (pad, "caps changed to %" GST_PTR_FORMAT, caps);
        gst_caps_replace (&roimux->caps, caps);
      }

      if (!roimux->have_caps) {
        if (!gst_video_info_from_caps (&vinfo, caps)) {
          GST_ERROR_OBJECT (roimux,
              "ERROR: Failed to get input video info from caps");
          return FALSE;
        }

        // Fixate caps of src pad
        gst_pad_set_caps (roimux->srcpad, caps);
      }
      roimux->have_caps = TRUE;
      break;
    }
    default:
      break;
  }

  /* if we have EOS, we should send on EOS ourselves */
  if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    GST_DEBUG_OBJECT (roimux, "Sending on event %" GST_PTR_FORMAT, event);

    if (!gst_pad_push_event (roimux->srcpad, gst_event_ref (event))) {
      GST_ERROR_OBJECT (roimux, "Error gst_pad_push_event");
    }
  }

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GST_DEBUG_OBJECT (roimux, "Sending on event %" GST_PTR_FORMAT, event);

    if (pad == roimux->vidsinkpad)
      roimux->vidsink_eos = TRUE;
    else if (pad == roimux->textsinkpad)
      roimux->textsink_eos = TRUE;

    if (roimux->vidsink_eos && roimux->textsink_eos) {
      if (!gst_pad_push_event (roimux->srcpad, gst_event_ref (event))) {
        GST_ERROR_OBJECT (roimux, "Error gst_pad_push_event");
      }
    }

    if (!roimux->is_config_parsed && pad == roimux->textsinkpad) {
      if (!gst_roimux_parse_config (roimux)) {
        GST_ERROR_OBJECT (roimux, "ERROR: Failed to parse the config data!");
        gst_event_unref (event);
        return FALSE;
      }
      else
        GST_DEBUG_OBJECT (roimux, "Success parse the config data!");
    }
  }

  gst_event_unref (event);
  return TRUE;
}

static GstStateChangeReturn
gst_roimux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_roimux_finalize (GObject * object)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  GList *list = NULL;

  // Release roi data
  for (list = roimux->roi_data_list; list; list = list->next) {
    GstRoiData *roi_data = (GstRoiData *) list->data;

    if (roi_data && roi_data->rects)
      g_free (roi_data->rects);

    if (roi_data)
      g_free (roi_data);
  }
  g_list_free (roimux->roi_data_list);

  // Release config data
  if (roimux->config_data)
    g_free (roimux->config_data);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (roimux));
}

static GstFlowReturn
gst_roimux_chain_sink_0 (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  GList *list = NULL;

  if (GST_FORMAT_UNDEFINED == roimux->src_segment.format) {
    gst_segment_init (&roimux->src_segment, GST_FORMAT_TIME);
    gst_pad_push_event (
        roimux->srcpad, gst_event_new_segment (&roimux->src_segment));
  }

  GstClockTime timestamp = gst_segment_to_running_time (
      &roimux->src_segment, GST_FORMAT_TIME,
      GST_BUFFER_PTS (buffer)) + GST_BUFFER_DURATION (buffer);

  GstRoiData *best_roi_data = NULL;
  if (roimux->is_config_parsed) {
    for (list = roimux->roi_data_list; list; list = list->next) {
      GstRoiData *roi_data = (GstRoiData *) list->data;
      if (roi_data && (roi_data->timestamp <= timestamp)) {
        best_roi_data = roi_data;
      }
    }
  }

  if (best_roi_data) {
    for (gsize i = 0; i < best_roi_data->rects_size; i++) {
      GstVideoRegionOfInterestMeta *roi_meta =
          gst_buffer_add_video_region_of_interest_meta (buffer, "roimuxmeta",
              best_roi_data->rects[i].x, best_roi_data->rects[i].y,
              best_roi_data->rects[i].w, best_roi_data->rects[i].h);

      roi_meta->id = i;
    }
  }
  gst_pad_push (roimux->srcpad, buffer);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_roimux_chain_sink_1 (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  GstMapInfo map_info;

  gst_buffer_map (buffer, &map_info, GST_MAP_READ);
  gchar *data = NULL;
  if (!roimux->config_data) {
    roimux->config_data = g_new0 (gchar, map_info.size);
    roimux->config_size = 0;
  } else {
    roimux->config_data = g_renew (
        gchar, roimux->config_data, roimux->config_size + map_info.size);
  }

  data = roimux->config_data + roimux->config_size;
  memcpy (data, map_info.data, map_info.size);
  roimux->config_size += map_info.size;

  gst_buffer_unmap (buffer, &map_info);
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static void
gst_roimux_class_init (GstRoiMuxClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_roimux_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_roimux_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_roimux_finalize);

  gst_element_class_set_static_metadata (element,
      "ROI mux", "Video/Metadata/ROI",
      "ROI, Mux metadata", "QTI");

  gst_element_class_add_static_pad_template (element,
      &roimux_src_template);

  gst_element_class_add_static_pad_template (element,
      &roimux_sink_0_template);

  gst_element_class_add_static_pad_template (element,
        &roimux_sink_1_template);

  element->change_state = GST_DEBUG_FUNCPTR (gst_roimux_change_state);
}

static void
gst_roimux_init (GstRoiMux * roimux)
{
  roimux->vidsinkpad =
      gst_pad_new_from_static_template (&roimux_sink_0_template, "sink_0");
  gst_pad_set_chain_function (roimux->vidsinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_chain_sink_0));
  gst_pad_set_event_function (roimux->vidsinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_sink_event));
  gst_element_add_pad (GST_ELEMENT (roimux), roimux->vidsinkpad);

  roimux->textsinkpad =
      gst_pad_new_from_static_template (&roimux_sink_1_template, "sink_1");
  gst_pad_set_chain_function (roimux->textsinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_chain_sink_1));
  gst_pad_set_event_function (roimux->textsinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_sink_event));
  gst_element_add_pad (GST_ELEMENT (roimux), roimux->textsinkpad);

  roimux->srcpad =
      gst_pad_new_from_static_template (&roimux_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (roimux), roimux->srcpad);


  gst_segment_init (&roimux->src_segment, GST_FORMAT_UNDEFINED);
  roimux->roi_data_list = NULL;
  roimux->config_data = NULL;
  roimux->config_size = 0;
  roimux->is_config_parsed = FALSE;
  roimux->vidsink_eos = FALSE;
  roimux->textsink_eos = FALSE;

  GST_DEBUG_CATEGORY_INIT (roimux_debug, "qtiroimux", 0, "QTI roi mux");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiroimux", GST_RANK_PRIMARY,
      GST_TYPE_ROIMUX);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiroimux,
    "Muxing video stream and roi data",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
