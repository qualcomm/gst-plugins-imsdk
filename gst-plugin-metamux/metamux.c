/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "metamux.h"

#include <stdio.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "metamuxpads.h"

#define GST_CAT_DEFAULT gst_meta_mux_debug
GST_DEBUG_CATEGORY_STATIC (gst_meta_mux_debug);

#define gst_meta_mux_parent_class parent_class

#define GST_METAMUX_MEDIA_CAPS \
    "video/x-raw(ANY); "      \
    "audio/x-raw(ANY)"

#define GST_METAMUX_DATA_CAPS \
    "text/x-raw"

G_DEFINE_TYPE (GstMetaMux, gst_meta_mux, GST_TYPE_ELEMENT);

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_meta_mux_media_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_METAMUX_MEDIA_CAPS)
    );

static GstStaticPadTemplate gst_meta_mux_data_sink_template =
    GST_STATIC_PAD_TEMPLATE("data_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_METAMUX_DATA_CAPS)
    );

static GstStaticPadTemplate gst_meta_mux_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_METAMUX_MEDIA_CAPS)
    );


static gboolean
gst_caps_is_media_type (const GstCaps * caps, const gchar * mediatype)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);

  return (g_ascii_strcasecmp (gst_structure_get_name (s), mediatype) == 0) ?
      TRUE : FALSE;
}

static gboolean
gst_meta_mux_data_available (GstMetaMux * muxer)
{
  GList *list = NULL;
  gboolean available = TRUE;

  for (list = muxer->metapads; list != NULL; list = g_list_next (list))
    available &= !g_queue_is_empty (GST_META_MUX_DATA_PAD (list->data)->queue);

  return available;
}

static void
gst_meta_mux_flush_queues (GstMetaMux * muxer)
{
  GList *list = NULL;

  GST_METAMUX_LOCK (muxer);

  for (list = muxer->metapads; list != NULL; list = g_list_next (list)) {
    GstMetaMuxDataPad *dpad = GST_META_MUX_DATA_PAD (list->data);

    while (!g_queue_is_empty (dpad->queue)) {
      GValue *value = g_queue_pop_head (dpad->queue);

      g_value_unset (value);
      g_free (value);
    }

    g_clear_pointer (&(dpad)->stash, g_free);
  }

  GST_METAMUX_UNLOCK (muxer);

  g_cond_signal (&(muxer)->wakeup);
}

static void
gst_meta_mux_process_metadata_entry (GstMetaMux * muxer, GstBuffer * buffer,
    const GValue * value, const guint index)
{
  GstStructure *structure = NULL;
  GstVideoRegionOfInterestMeta *meta = NULL;
  const GValue *entry = NULL;
  gint x = 0, y = 0, width = 0, height = 0;

  entry = gst_value_list_get_value (value, index);
  structure = GST_STRUCTURE (g_value_dup_boxed (entry));

  // Fetch bounding box rectangle and fill ROI coordinates it it exists.
  entry = gst_structure_get_value (structure, "rectangle");

  if ((entry != NULL) && (gst_value_array_get_size (entry) != 4)) {
    GST_WARNING_OBJECT (muxer, "Badly formed ROI rectangle, expected 4 "
        "entries but received %u!", gst_value_array_get_size (entry));
  } else if (entry != NULL) {
    gfloat left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;

    top    = g_value_get_float (gst_value_array_get_value (entry, 0));
    left   = g_value_get_float (gst_value_array_get_value (entry, 1));
    bottom = g_value_get_float (gst_value_array_get_value (entry, 2));
    right  = g_value_get_float (gst_value_array_get_value (entry, 3));

    x      = ABS (left) * GST_VIDEO_INFO_WIDTH (muxer->vinfo);
    y      = ABS (top) * GST_VIDEO_INFO_HEIGHT (muxer->vinfo);
    width  = ABS (right - left) * GST_VIDEO_INFO_WIDTH (muxer->vinfo);
    height = ABS (bottom - top) * GST_VIDEO_INFO_HEIGHT (muxer->vinfo);

    // Adjust bounding box dimensions with extracted source aspect ratio.
    if (gst_structure_has_field (structure, "aspect-ratio")) {
      gint sar_n = 1, sar_d = 1;
      gdouble coeficient = 0.0;

      sar_n = gst_value_get_fraction_numerator (
          gst_structure_get_value (structure, "aspect-ratio"));
      sar_d = gst_value_get_fraction_denominator (
          gst_structure_get_value (structure, "aspect-ratio"));

      if (sar_n > sar_d) {
        gst_util_fraction_to_double (sar_n, sar_d, &coeficient);

        y *= coeficient;
        height *= coeficient;
      } else if (sar_n < sar_d) {
        gst_util_fraction_to_double (sar_d, sar_n, &coeficient);

        x *= coeficient;
        width *= coeficient;
      }
    }

    // Clip width and height if it outside the frame limits.
    width = ((x + width) > GST_VIDEO_INFO_WIDTH (muxer->vinfo)) ?
        (GST_VIDEO_INFO_WIDTH (muxer->vinfo) - x) : width;
    height = ((y + height) > GST_VIDEO_INFO_HEIGHT (muxer->vinfo)) ?
        (GST_VIDEO_INFO_HEIGHT (muxer->vinfo) - y) : height;
  }

  // Remove the rectangle & aspect-ratio fields as that data is no longer needed.
  gst_structure_remove_field (structure, "rectangle");
  gst_structure_remove_field (structure, "aspect-ratio");

  meta = gst_buffer_add_video_region_of_interest_meta (buffer,
      gst_structure_get_name (structure), x, y, width, height);
  meta->id = index;

  gst_video_region_of_interest_meta_add_param (meta, structure);
}

static GstCaps *
gst_meta_mux_main_sink_getcaps (GstMetaMux * muxer, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *srccaps = NULL, *templcaps = NULL, *sinkcaps = NULL;

  templcaps = gst_pad_get_pad_template_caps (muxer->srcpad);

  // Query the source pad peer with the transformed filter.
  srccaps = gst_pad_peer_query_caps (muxer->srcpad, templcaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (muxer, "Src caps %" GST_PTR_FORMAT, srccaps);

  templcaps = gst_pad_get_pad_template_caps (pad);
  sinkcaps = gst_caps_intersect (templcaps, srccaps);

  gst_caps_unref (srccaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (muxer, "Filter caps  %" GST_PTR_FORMAT, filter);

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, sinkcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (muxer, "Intersected caps %" GST_PTR_FORMAT, intersection);

    gst_caps_unref (sinkcaps);
    sinkcaps = intersection;
  }

  GST_DEBUG_OBJECT (muxer, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}

static gboolean
gst_meta_mux_main_sink_setcaps (GstMetaMux * muxer, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;

  GST_DEBUG_OBJECT (muxer, "Setting caps %" GST_PTR_FORMAT, caps);

  // Get the negotiated caps between the srcpad and its peer.
  srccaps = gst_pad_get_allowed_caps (muxer->srcpad);
  GST_DEBUG_OBJECT (muxer, "Source caps %" GST_PTR_FORMAT, srccaps);

  intersect = gst_caps_intersect (srccaps, caps);
  GST_DEBUG_OBJECT (muxer, "Intersected caps %" GST_PTR_FORMAT, intersect);

  gst_caps_unref (srccaps);

  if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
    GST_ERROR_OBJECT (muxer, "Source and sink caps do not intersect!");

    if (intersect != NULL)
      gst_caps_unref (intersect);

    return FALSE;
  }

  if (gst_pad_has_current_caps (muxer->srcpad)) {
    srccaps = gst_pad_get_current_caps (muxer->srcpad);

    if (!gst_caps_is_equal (srccaps, intersect))
      gst_pad_push_event (muxer->srcpad, gst_event_new_reconfigure ());

    gst_caps_unref (srccaps);
  }

  gst_caps_unref (intersect);

  if (gst_caps_is_media_type (caps, "video/x-raw")) {
    if (muxer->vinfo != NULL)
      gst_video_info_free (muxer->vinfo);

    muxer->vinfo = gst_video_info_new ();

    if (!gst_video_info_from_caps (muxer->vinfo, caps)) {
      GST_ERROR_OBJECT (muxer, "Invalid caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }
  } else {
    if (muxer->ainfo != NULL)
      gst_audio_info_free (muxer->ainfo);

    muxer->ainfo = gst_audio_info_new ();

    if (!gst_audio_info_from_caps (muxer->ainfo, caps)) {
      GST_ERROR_OBJECT (muxer, "Invalid caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_meta_mux_main_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_LOG_OBJECT (muxer, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);

      if ((success = gst_meta_mux_main_sink_setcaps (muxer, pad, caps)))
        success = gst_pad_push_event (muxer->srcpad, event);
      else
        gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (muxer, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&muxer->segment, GST_FORMAT_TIME);

        muxer->segment.start = segment.start;

        GST_DEBUG_OBJECT (muxer, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &muxer->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (muxer, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &muxer->segment);
        gst_segment_copy_into (&segment, &muxer->segment);
      } else {
        GST_ERROR_OBJECT (muxer, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }

      gst_event_unref (event);
      event = gst_event_new_segment (&muxer->segment);

      return gst_pad_push_event (muxer->srcpad, event);
    }
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_meta_mux_flush_queues (muxer);
      break;
    case GST_EVENT_EOS:
      gst_meta_mux_flush_queues (muxer);
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_meta_mux_main_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_LOG_OBJECT (muxer, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_meta_mux_main_sink_getcaps (muxer, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      GST_DEBUG_OBJECT (muxer, "Accept caps: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_fixed (caps)) {
        GstCaps *tmplcaps = gst_pad_get_pad_template_caps (pad);
        GST_DEBUG_OBJECT (muxer, "Template caps: %" GST_PTR_FORMAT, tmplcaps);

        success = gst_caps_can_intersect (tmplcaps, caps);
        gst_caps_unref (tmplcaps);
      }

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static GstFlowReturn
gst_meta_mux_main_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMetaMux *muxer = GST_METAMUX (parent);
  GList *list = NULL;

  if (!gst_pad_has_current_caps (muxer->srcpad)) {
    if (GST_PAD_IS_FLUSHING (muxer->srcpad))
      return GST_FLOW_FLUSHING;

    GST_ELEMENT_ERROR (muxer, STREAM, DECODE, ("No caps set!"), (NULL));
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (muxer, "Received buffer %p with pts %" GST_TIME_FORMAT
      ", dts %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buffer,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  GST_METAMUX_LOCK (muxer);

  while (muxer->metapads && !gst_meta_mux_data_available (muxer))
    g_cond_wait (&muxer->wakeup, GST_METAMUX_GET_LOCK (muxer));

  // Iterate over all of the data pad queues and extract available data.
  for (list = muxer->metapads; list != NULL; list = g_list_next (list)) {
    GstMetaMuxDataPad *dpad = GST_META_MUX_DATA_PAD (list->data);
    GValue *value = NULL;
    guint idx = 0, size = 0;

    if (g_queue_is_empty (dpad->queue))
      continue;

    value = g_queue_pop_head (dpad->queue);
    size = gst_value_list_get_size (value);

    for (idx = 0; idx < size; idx++)
      gst_meta_mux_process_metadata_entry (muxer, buffer, value, idx);

    g_value_unset (value);
    g_free (value);
  }

  GST_METAMUX_UNLOCK (muxer);

  GST_TRACE_OBJECT (muxer, "Submitting buffer %p of size %" G_GSIZE_FORMAT
      " with pts %" GST_TIME_FORMAT ", dts %" GST_TIME_FORMAT ", duration %"
      GST_TIME_FORMAT, buffer, gst_buffer_get_size (buffer),
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  if (gst_pad_push (muxer->srcpad, buffer) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (muxer, "Failed to push buffer to pad!");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}


static gboolean
gst_meta_mux_data_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_LOG_OBJECT (muxer, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
    case GST_EVENT_GAP:
    case GST_EVENT_STREAM_START:
      // Drop the event, those events are forwarded by the main sink pad.
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_meta_mux_data_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMetaMux *muxer = GST_METAMUX (parent);
  GstMetaMuxDataPad *dpad = GST_META_MUX_DATA_PAD (pad);
  GstMapInfo memmap = {};
  gchar **strings = NULL, *string = NULL;
  guint idx = 0;

  if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

  // If the main sink pad has reached EOS return EOS for data(meta) pads.
  if (GST_PAD_IS_EOS (muxer->sinkpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }

  GST_TRACE_OBJECT (muxer, "Received buffer at %s pad of size %"
      G_GSIZE_FORMAT " with pts %" GST_TIME_FORMAT ", dts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, GST_PAD_NAME (pad),
      gst_buffer_get_size (buffer), GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
    GST_ERROR_OBJECT (muxer, "Failed to map buffer!");

    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  // Split the data into separate serialized GValue-s for parsing.
  strings = g_strsplit_set ((const gchar *) memmap.data, "\n", 0);

  // Iterate over the serialized strings and turn them into GstValueList.
  for (idx = 0; strings[idx] != NULL; idx++) {
    GValue *value = NULL;

    // Check for empty string and skip it.
    if (g_strcmp0 (strings[idx], "") == 0)
      continue;

    if (!g_utf8_validate (strings[idx], -1, NULL)) {
      GST_WARNING_OBJECT (muxer, "Extracted buffer data at %s pad and index %u"
          " is not UTF-8: '%s'!", GST_PAD_NAME (pad), idx, strings[idx]);
      continue;
    }

    value = g_value_init (g_new0 (GValue, 1), GST_TYPE_LIST);

    // If deserialize fails it mangles the string so work with local copy.
    string = (dpad->stash != NULL) ?
        g_strconcat (dpad->stash, strings[idx], NULL) : g_strdup (strings[idx]);

    if (!gst_value_deserialize (value, string)) {
      GST_DEBUG_OBJECT (muxer, "Failed to deserialize data at %s pad!",
          GST_PAD_NAME (pad));

      g_free (string);

      // Could be a partial string (e.g. when reading from a file). Stash the
      // string, combine it with the 1st string from next buffer and try again.
      if (dpad->stash != NULL) {
        string = g_strconcat (dpad->stash, strings[idx], NULL);

        g_free (dpad->stash);
        dpad->stash = string;
      } else {
        dpad->stash = g_strdup (strings[idx]);
      }

      g_value_unset (value);
      g_free (value);

      continue;
    }

    g_clear_pointer (&(dpad)->stash, g_free);
    g_free (string);

    GST_METAMUX_LOCK (muxer);

    g_queue_push_tail (dpad->queue, value);
    g_cond_signal (&(muxer)->wakeup);

    GST_METAMUX_UNLOCK (muxer);
  }

  g_strfreev (strings);
  gst_buffer_unmap (buffer, &memmap);
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static gboolean
gst_meta_mux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_LOG_OBJECT (muxer, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_meta_mux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_LOG_OBJECT (muxer, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);

      GST_DEBUG_OBJECT (muxer, "Current caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (muxer, "Filter caps: %" GST_PTR_FORMAT, caps);

      if (filter != NULL) {
        GstCaps *intersection  =
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (caps);
        caps = intersection;
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &muxer->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (muxer, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static GstPad*
gst_meta_mux_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstMetaMux *muxer = GST_METAMUX (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_METAMUX_LOCK (muxer);

  if (reqname && sscanf (reqname, "data_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= muxer->nextidx) ? index + 1 : muxer->nextidx;
  } else {
    index = muxer->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  name = g_strdup_printf ("data_%u", index);

  pad = g_object_new (GST_TYPE_META_MUX_DATA_PAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (muxer, "Failed to create sink pad!");
    return NULL;
  }

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_data_sink_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_data_sink_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (muxer, "Failed to add sink pad!");
    gst_object_unref (pad);
    return NULL;
  }

  muxer->metapads = g_list_append (muxer->metapads, pad);
  muxer->nextidx = nextindex;

  GST_METAMUX_UNLOCK (muxer);

  GST_DEBUG_OBJECT (muxer, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_meta_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstMetaMux *muxer = GST_METAMUX (element);

  GST_DEBUG_OBJECT (muxer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_METAMUX_LOCK (muxer);

  muxer->metapads = g_list_remove (muxer->metapads, pad);

  GST_METAMUX_UNLOCK (muxer);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_meta_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstMetaMux *muxer = GST_METAMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_meta_mux_flush_queues (muxer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_meta_mux_flush_queues (muxer);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_meta_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_meta_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_meta_mux_finalize (GObject * object)
{
  GstMetaMux *muxer = GST_METAMUX (object);

  if (muxer->ainfo != NULL)
    gst_audio_info_free (muxer->ainfo);

  if (muxer->vinfo != NULL)
    gst_video_info_free (muxer->vinfo);

  g_mutex_clear (&muxer->lock);
  g_cond_clear (&muxer->wakeup);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (muxer));
}

static void
gst_meta_mux_class_init (GstMetaMuxClass *klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_meta_mux_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_meta_mux_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_meta_mux_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_meta_mux_media_sink_template, GST_TYPE_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_meta_mux_data_sink_template, GST_TYPE_META_MUX_DATA_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_meta_mux_src_template, GST_TYPE_PAD);

  gst_element_class_set_static_metadata (element,
      "Meta muxer", "Video/Audio/Text/Muxer",
      "Muxes data stream as GstMeta with raw audio or video stream", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_meta_mux_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_meta_mux_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_meta_mux_change_state);

  // Initializes a new muxer GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_meta_mux_debug, "qtimetamux", 0, "QTI Meta Muxer");
}

static void
gst_meta_mux_init (GstMetaMux * muxer)
{
  g_mutex_init (&muxer->lock);
  g_cond_init (&muxer->wakeup);

  muxer->nextidx = 0;
  muxer->metapads = NULL;

  muxer->vinfo = NULL;
  muxer->ainfo = NULL;

  gst_segment_init (&muxer->segment, GST_FORMAT_UNDEFINED);

  muxer->sinkpad = gst_pad_new_from_static_template (
      &gst_meta_mux_media_sink_template, "sink");
  g_return_if_fail (muxer->sinkpad != NULL);

  gst_pad_set_event_function (muxer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_main_sink_event));
  gst_pad_set_query_function (muxer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_main_sink_query));
  gst_pad_set_chain_function (muxer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_main_sink_chain));
  gst_element_add_pad (GST_ELEMENT (muxer), muxer->sinkpad);

  muxer->srcpad = gst_pad_new_from_static_template (
      &gst_meta_mux_src_template, "src");
  g_return_if_fail (muxer->srcpad != NULL);

  gst_pad_set_event_function (muxer->srcpad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_src_event));
  gst_pad_set_query_function (muxer->srcpad,
      GST_DEBUG_FUNCPTR (gst_meta_mux_src_query));
  gst_element_add_pad (GST_ELEMENT (muxer), muxer->srcpad);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimetamux", GST_RANK_NONE,
      GST_TYPE_METAMUX);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimetamux,
    "QTI Meta Muxer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
