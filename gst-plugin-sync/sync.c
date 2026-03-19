/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sync.h"

#define GST_CAT_DEFAULT gst_sync_debug
GST_DEBUG_CATEGORY_STATIC (gst_sync_debug);

#define gst_sync_parent_class parent_class
G_DEFINE_TYPE (GstSync, gst_sync, GST_TYPE_ELEMENT);

#define GST_SYNC_SINK_CAPS \
    "video/x-raw(ANY); "

#define GST_SYNC_SRC_CAPS \
    "video/x-raw(ANY); "

static GstStaticPadTemplate gst_sync_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_SYNC_SINK_CAPS)
    );

static GstStaticPadTemplate gst_sync_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_SYNC_SRC_CAPS)
    );

static GstFlowReturn
gst_sync_sinkpad_chain (GstPad * pad, GstObject * parent, GstBuffer * in_buffer)
{
  GstSync *sync = GST_SYNC (parent);

  if (sync->clock == NULL) {
    GST_TRACE_OBJECT (sync, "SKIP buffer, no clock");
    gst_buffer_unref (in_buffer);
    return GST_FLOW_OK;
  }

  GstClockTime real_ts = gst_clock_get_time (sync->clock);

  if (sync->have_start_time) {
    const char *discont =
        GST_BUFFER_IS_DISCONT (in_buffer) ? " with discontinuity" : "";
    GstClockTime expected_real_ts =
        sync->stream_start_real_time + GST_BUFFER_TIMESTAMP (in_buffer);
    gboolean early = real_ts < expected_real_ts;

    if (early) {
      GstClockID cid = gst_clock_new_single_shot_id (sync->clock,
          expected_real_ts);

      GST_TRACE_OBJECT (sync, "ts: %" GST_TIME_FORMAT " %s, waiting for %ld ms",
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (in_buffer)),
          discont, (expected_real_ts - real_ts) / 1000000);
      gst_clock_id_wait (cid, NULL);
      gst_clock_id_unref (cid);
    } else {
      GST_TRACE_OBJECT (sync, "ts: %" GST_TIME_FORMAT " %s, pad on time",
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (in_buffer)), discont);
    }
  } else {
    sync->stream_start_real_time = real_ts - GST_BUFFER_TIMESTAMP (in_buffer);
    sync->have_start_time = TRUE;
  }

  GST_TRACE_OBJECT (sync, "Push buffer");
  return gst_pad_push (sync->srcpad, in_buffer);
}

static gboolean
gst_sync_sinkpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSync *sync = GST_SYNC (parent);
  gboolean success = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      GstCaps *srccaps = NULL, *intersect = NULL;

      gst_event_parse_caps (event, &caps);
      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

      // Get the negotiated caps between the srcpad and its peer.
      srccaps = gst_pad_get_allowed_caps (GST_PAD (sync->srcpad));
      GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

      intersect = gst_caps_intersect (srccaps, caps);
      GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (srccaps);

      if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Source and sink caps do not intersect!");

        if (intersect != NULL)
          gst_caps_unref (intersect);

        return FALSE;
      }

      if (gst_pad_has_current_caps (GST_PAD (sync->srcpad))) {
        srccaps = gst_pad_get_current_caps (GST_PAD (sync->srcpad));

        if (!gst_caps_is_equal (srccaps, caps))
          gst_pad_mark_reconfigure (GST_PAD (sync->srcpad));

        gst_caps_unref (srccaps);
      }

      gst_caps_unref (intersect);

      GST_DEBUG_OBJECT (pad, "Negotiated caps %" GST_PTR_FORMAT, caps);

      GST_DEBUG_OBJECT (pad, "Pushing new caps %" GST_PTR_FORMAT, caps);
      gst_pad_push_event (GST_PAD (sync->srcpad), gst_event_new_caps (caps));

      gst_event_unref (event);

      break;
    }
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }

  return success;
}

static gboolean
gst_sync_set_clock (GstElement * element, GstClock * clock)
{
  GstSync *sync = GST_SYNC (element);

  if (sync->clock) {
    gst_object_unref (sync->clock);
    sync->clock = NULL;
  }

  if (clock != NULL)
    sync->clock = gst_object_ref (clock);

  return TRUE;
}

static void
gst_sync_finalize (GObject * object)
{
  GstSync *sync = GST_SYNC (object);

  if (sync->clock) {
    gst_object_unref (sync->clock);
    sync->clock = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (sync));
}

static void
gst_sync_init (GstSync * sync)
{
  sync->clock = NULL;
  sync->have_start_time = FALSE;

  sync->sinkpad =
      gst_pad_new_from_static_template (&gst_sync_sink_pad_template, "sink");

  sync->srcpad =
      gst_pad_new_from_static_template (&gst_sync_src_pad_template, "src");

  gst_pad_set_chain_function (sync->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sync_sinkpad_chain));
  gst_pad_set_event_function (sync->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sync_sinkpad_event));

  GST_OBJECT_FLAG_SET (sync->sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_element_add_pad (GST_ELEMENT (sync), sync->sinkpad);
  gst_element_add_pad (GST_ELEMENT (sync), sync->srcpad);
}

static void
gst_sync_class_init (GstSyncClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_sync_finalize);

  element->set_clock = GST_DEBUG_FUNCPTR (gst_sync_set_clock);

  gst_element_class_add_static_pad_template (element,
      &gst_sync_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_sync_src_pad_template);

  gst_element_class_set_static_metadata (element,
      "Sync", "Video/Audio/Text/Muxer",
      "Filter that throttles pipeline throughput to real time", "QTI");

  GST_DEBUG_CATEGORY_INIT (gst_sync_debug, "qtisync", 0,
      "QTI Sync Plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisync", GST_RANK_NONE,
      GST_TYPE_SYNC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisync,
    "QTI Plugin for buffer flow synchronization",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
