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

#include "roimux.h"

#include <stdio.h>

#include <ml-meta/ml_meta.h>
#include <cvp/v2.0/cvpOpticalFlow.h>

#define GST_INPUT_VIDEO_FORMATS "{ NV12 }"
#define GST_OUTPUT_VIDEO_FORMATS "{ NV12 }"

typedef struct _GstRoiData GstRoiData;
struct _GstRoiData {
  guint64 timestamp;
  GstVideoRectangle *rects;
  gsize rects_size;
};

static GstStaticPadTemplate roimux_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_INPUT_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_INPUT_VIDEO_FORMATS))
    );

static GstStaticPadTemplate roimux_sink_data_template =
GST_STATIC_PAD_TEMPLATE ("sink_data",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
  );

static GstStaticPadTemplate roimux_sink_video_template =
GST_STATIC_PAD_TEMPLATE ("sink_vid",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_INPUT_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_INPUT_VIDEO_FORMATS))
  );

#define GST_CAT_DEFAULT roimux_debug
GST_DEBUG_CATEGORY_STATIC (roimux_debug);

#define gst_roimux_parent_class parent_class
G_DEFINE_TYPE (GstRoiMux, gst_roimux, GST_TYPE_ELEMENT);

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
  GstVideoInfo info;

  GST_DEBUG_OBJECT (roimux, "received event %p of type %s (%d)",
      event, gst_event_type_get_name (event->type), event->type);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      GST_DEBUG_OBJECT (roimux, "Received stream start");
      break;
    case GST_EVENT_CAPS:
    {
      GST_DEBUG_OBJECT (roimux, "Received caps %" GST_PTR_FORMAT, event);
      gst_event_parse_caps (event, &caps);

      if (pad == roimux->vidsinkpad) {
        if (!gst_video_info_from_caps (&info, caps)) {
            GST_ERROR_OBJECT (roimux, "Failed to parse caps");
            return FALSE;
        }
        roimux->vinfo = gst_video_info_copy (&info);

        // Fixate caps of src pad
        gst_pad_set_caps (roimux->srcpad, caps);
      }

      if (pad == roimux->datasinkpad) {
        GstStructure *structure =
            gst_caps_get_structure (caps, 0);
        if (gst_structure_has_name (structure, "cvp/optiflow")) {
          roimux->datapad_format = GST_DATA_FORMAT_OPTICAL_FLOW;
          GST_INFO_OBJECT (pad, "Set input format cvp/optiflow");
        }
      }
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
    else if (pad == roimux->datasinkpad)
      roimux->datasink_eos = TRUE;

    if (roimux->vidsink_eos && roimux->datasink_eos) {
      if (!gst_pad_push_event (roimux->srcpad, gst_event_ref (event))) {
        GST_ERROR_OBJECT (roimux, "Error gst_pad_push_event");
      }
    }

    if (!roimux->is_config_parsed && pad == roimux->datasinkpad &&
        roimux->datapad_format == GST_DATA_FORMAT_TEXT) {
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

  gst_data_queue_flush (roimux->vidpad_queue);
  gst_data_queue_flush (roimux->datapad_queue);
  g_mutex_clear (&roimux->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (roimux));
}

static void
gst_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_roimux_timestamp_compare (GstRoiMux * roimux)
{
  GstBuffer *videobuffer = NULL;
  GstBuffer *metabuffer = NULL;
  GstDataQueueItem *item = NULL;
  g_mutex_lock (&roimux->lock);

  if (GST_FORMAT_UNDEFINED == roimux->segment.format) {
    gst_segment_init (&roimux->segment, GST_FORMAT_TIME);
    gst_pad_push_event (
        roimux->srcpad, gst_event_new_segment (&roimux->segment));
  }

  // Check if video queue is empty
  if (!gst_data_queue_is_empty (roimux->vidpad_queue)) {
    // Get the item without remove from the queue
    gst_data_queue_peek (roimux->vidpad_queue, &item);
    videobuffer = (GstBuffer *) item->object;
  } else {
    g_mutex_unlock (&roimux->lock);
    return;
  }

  // Check if video queue is empty
  if (!gst_data_queue_is_empty (roimux->datapad_queue)) {
    // Get the item without remove from the queue
    gst_data_queue_peek (roimux->datapad_queue, &item);
    metabuffer = (GstBuffer *) item->object;
  } else {
    g_mutex_unlock (&roimux->lock);
    return;
  }

  GstClockTimeDiff timedelta = GST_CLOCK_DIFF (GST_BUFFER_PTS (videobuffer),
      GST_BUFFER_PTS (metabuffer));

  // Check the timestamp delta
  if (GST_TIME_AS_MSECONDS (timedelta) == 0) {
    // Remove both buffer from the queue
    gst_data_queue_pop (roimux->vidpad_queue, &item);
    videobuffer = (GstBuffer *) item->object;

    gst_data_queue_pop (roimux->datapad_queue, &item);
    metabuffer = (GstBuffer *) item->object;

    GstCvpOpticalFlowMeta *meta = gst_buffer_add_optclflow_meta (videobuffer);

    GstMapInfo map_info0;
    GstMapInfo map_info1;
    if (!gst_buffer_map_range (
        metabuffer, 0, 1, &map_info0, GST_MAP_READWRITE)) {
      GST_ERROR_OBJECT (roimux, "%s Failed to map the mv buffer!!", __func__);
      g_mutex_unlock (&roimux->lock);
      return;
    }
    if (!gst_buffer_map_range (
        metabuffer, 1, 1, &map_info1, GST_MAP_READWRITE)) {
      GST_ERROR_OBJECT (roimux, "%s Failed to map the stats buffer!!", __func__);
      g_mutex_unlock (&roimux->lock);
      return;
    }

    cvpMotionVector *mv_data = (cvpMotionVector *) map_info0.data;
    cvpOFStats *stats_data = (cvpOFStats *) map_info1.data;
    gint n_vectors = GST_VIDEO_INFO_WIDTH (roimux->vinfo) *
        GST_VIDEO_INFO_HEIGHT (roimux->vinfo) / 64;

    meta->mvectors =
          (GstCvpMotionVector*) malloc (sizeof (GstCvpMotionVector) * n_vectors);
    if (!meta) {
      GST_ERROR_OBJECT (roimux, "Failed to allocate meta buffer");
      g_mutex_unlock (&roimux->lock);
      return;
    }
    meta->n_vectors = n_vectors;

    for (int i = 0; i < n_vectors; i++) {
      meta->mvectors[i].x = mv_data[i].nMVX_L0;
      meta->mvectors[i].y = mv_data[i].nMVY_L0;
      meta->mvectors[i].confidence = mv_data[i].nConf;

      meta->mvectors[i].variance = stats_data[i].nVariance;
      meta->mvectors[i].mean     = stats_data[i].nMean;
      meta->mvectors[i].bestsad  = stats_data[i].nBestMVSad;
      meta->mvectors[i].sad      = stats_data[i].nSad;
    }

    gst_buffer_unmap (metabuffer, &map_info1);
    gst_buffer_unmap (metabuffer, &map_info0);
    gst_buffer_unref (metabuffer);

    // Send buffer
    gst_pad_push (roimux->srcpad, videobuffer);
  } else if (GST_TIME_AS_MSECONDS (timedelta) < 0) {
    // Drop meta
    // Remove data buffer from the queue
    gst_data_queue_pop (roimux->datapad_queue, &item);
    metabuffer = (GstBuffer *) item->object;
    gst_buffer_unref (metabuffer);
  } else if (GST_TIME_AS_MSECONDS (timedelta) > 0) {
    // Drop buffer
    // Remove video buffer from the queue
    gst_data_queue_pop (roimux->vidpad_queue, &item);
    videobuffer = (GstBuffer *) item->object;
    gst_buffer_unref (videobuffer);
  }
  g_mutex_unlock (&roimux->lock);
}

static GstFlowReturn
gst_roimux_chain_sink_data (GstPad * pad, GstObject * object,
    GstBuffer * buffer)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  GstMapInfo map_info;

  if (roimux->datapad_format == GST_DATA_FORMAT_OPTICAL_FLOW) {
    // Push buffer to queue
    GstDataQueueItem *item = NULL;
    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (buffer);
    item->visible = TRUE;
    item->destroy = gst_free_queue_item;
    if (!gst_data_queue_push (roimux->datapad_queue, item)) {
      GST_ERROR_OBJECT (roimux, "ERROR: Cannot push data to the queue!");
      item->destroy (item);
      return GST_FLOW_ERROR;
    }
    gst_roimux_timestamp_compare (roimux);
  } else if (roimux->datapad_format == GST_DATA_FORMAT_TEXT) {
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
  } else {
    GST_ERROR_OBJECT (roimux, "ERROR: Data pad format not supported!");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_roimux_chain_sink_video (GstPad * pad, GstObject * object,
    GstBuffer * buffer)
{
  GstRoiMux *roimux = GST_ROIMUX (object);
  GList *list = NULL;

  if (roimux->datapad_format == GST_DATA_FORMAT_OPTICAL_FLOW) {
    // Push buffer to queue
    GstDataQueueItem *item = NULL;
    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (buffer);
    item->visible = TRUE;
    item->destroy = gst_free_queue_item;
    if (!gst_data_queue_push (roimux->vidpad_queue, item)) {
      GST_ERROR_OBJECT (roimux, "ERROR: Cannot push data to the queue!");
      item->destroy (item);
      return GST_FLOW_ERROR;
    }
    gst_roimux_timestamp_compare (roimux);
  } else if (roimux->datapad_format == GST_DATA_FORMAT_TEXT) {
    GstClockTime timestamp = gst_segment_to_running_time (
        &roimux->segment, GST_FORMAT_TIME,
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
    if (GST_FORMAT_UNDEFINED == roimux->segment.format) {
      gst_segment_init (&roimux->segment, GST_FORMAT_TIME);
      gst_pad_push_event (
          roimux->srcpad, gst_event_new_segment (&roimux->segment));
    }
    gst_pad_push (roimux->srcpad, buffer);
  } else {
    GST_ERROR_OBJECT (roimux, "ERROR: Data pad format not supported!");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_roimux_class_init (GstRoiMuxClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);

  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_roimux_finalize);

  gst_element_class_set_static_metadata (element,
      "ROI mux", "Video/Metadata/ROI",
      "ROI, Mux metadata", "QTI");

  gst_element_class_add_static_pad_template (element,
      &roimux_src_template);

  gst_element_class_add_static_pad_template (element,
      &roimux_sink_data_template);

  gst_element_class_add_static_pad_template (element,
        &roimux_sink_video_template);

  element->change_state = GST_DEBUG_FUNCPTR (gst_roimux_change_state);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_roimux_init (GstRoiMux * roimux)
{
  roimux->datasinkpad =
      gst_pad_new_from_static_template (&roimux_sink_data_template, "sink_data");
  gst_pad_set_chain_function (roimux->datasinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_chain_sink_data));
  gst_pad_set_event_function (roimux->datasinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_sink_event));
  gst_element_add_pad (GST_ELEMENT (roimux), roimux->datasinkpad);

  roimux->vidsinkpad =
      gst_pad_new_from_static_template (&roimux_sink_video_template, "sink_vid");
  gst_pad_set_chain_function (roimux->vidsinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_chain_sink_video));
  gst_pad_set_event_function (roimux->vidsinkpad,
      GST_DEBUG_FUNCPTR (gst_roimux_sink_event));
  gst_element_add_pad (GST_ELEMENT (roimux), roimux->vidsinkpad);

  roimux->srcpad =
      gst_pad_new_from_static_template (&roimux_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (roimux), roimux->srcpad);

  gst_segment_init (&roimux->segment, GST_FORMAT_UNDEFINED);
  g_mutex_init (&roimux->lock);
  roimux->roi_data_list = NULL;
  roimux->config_data = NULL;
  roimux->config_size = 0;
  roimux->is_config_parsed = FALSE;
  roimux->vidsink_eos = FALSE;
  roimux->datasink_eos = FALSE;
  roimux->datapad_format = GST_DATA_FORMAT_TEXT;

  roimux->vidpad_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  roimux->datapad_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);

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
