/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "heifmux.h"

#include <stdio.h>
#include <string.h>

#include <gst/video/gstimagepool.h>

#define GST_CAT_DEFAULT gst_heifmux_debug
GST_DEBUG_CATEGORY (gst_heifmux_debug);

#define gst_heifmux_parent_class parent_class
G_DEFINE_TYPE (GstHeifMux, gst_heifmux, GST_TYPE_ELEMENT);

#define DEFAULT_PROP_MIN_BUFFERS     2
#define DEFAULT_PROP_MAX_BUFFERS     10
#define DEFAULT_PROP_QUEUE_SIZE      10

static GstStaticPadTemplate gst_heifmux_main_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("image/heic")
    );

static GstStaticPadTemplate gst_heifmux_thumbnail_sink_template =
    GST_STATIC_PAD_TEMPLATE ("thumbnail_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS ("video/x-h265")
    );

static GstStaticPadTemplate gst_heifmux_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("image/heic")
    );

enum
{
  PROP_0,
  PROP_QUEUE_SIZE,
};

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;

  if (item->object != NULL)
    gst_buffer_unref (GST_BUFFER (item->object));

  g_slice_free (GstDataQueueItem, item);
}

static void
gst_heifmux_flush_thubmnail_queues (GstHeifMux * muxer)
{
  GList *list = NULL;

  GST_HEIFMUX_LOCK (muxer);

  for (list = muxer->thumbpads; list != NULL; list = g_list_next (list)) {
    GstHeifMuxSinkPad *thpad = GST_HEIFMUX_SINK_PAD (list->data);

    gst_data_queue_set_flushing (thpad->buffers, TRUE);
    gst_data_queue_flush (thpad->buffers);
  }

  g_cond_signal (&(muxer)->wakeup);

  GST_HEIFMUX_UNLOCK (muxer);
  return;
}

static GstBufferPool *
gst_heifmux_create_pool (GstHeifMux * muxer, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (muxer, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (muxer, "Failed to create image pool!");
    return NULL;
  }

  // Align size to 64 lines.
  gint alignedw = GST_ROUND_UP_64 (GST_VIDEO_INFO_WIDTH (&info));
  gint alignedh = GST_ROUND_UP_64 (GST_VIDEO_INFO_HEIGHT (&info));
  gsize aligned_size = alignedw * alignedh * 4;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, aligned_size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  if (allocator == NULL) {
    GST_ERROR_OBJECT (muxer, "Failed to create allocator!");
    gst_clear_object (&pool);
    return NULL;
  }

  GST_INFO_OBJECT (muxer, "Buffer pool uses DMA memory.");
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (muxer, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  g_object_unref (allocator);

  return pool;
}

static void
gst_heifmux_worker_task (gpointer userdata)
{
  GstHeifMux *muxer = GST_HEIFMUX (userdata);
  GstBuffer *mainbuf = NULL, *outbuf = NULL;
  GList *thframes = NULL, *list = NULL;
  GstDataQueueItem *item = NULL;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gint  stride[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gboolean success = FALSE;

  if (!gst_data_queue_peek (muxer->sinkpad->buffers, &item))
    return;

  // Take the buffer from the queue item and null the object pointer.
  mainbuf = GST_BUFFER (item->object);
  item->object = NULL;

  // Set buffer video metadata.
  gst_buffer_add_video_meta_full (mainbuf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_ENCODED, muxer->sinkpad->vinfo->width,
      muxer->sinkpad->vinfo->height, 1, offset, stride);

  // Adds thumbnails if they are available.
  for (list = muxer->thumbpads; list != NULL; list = g_list_next (list)) {
    GstHeifMuxSinkPad *thpad = GST_HEIFMUX_SINK_PAD (list->data);

    if (gst_data_queue_peek (thpad->buffers, &item)) {
      GstVideoFrame *frame = g_slice_new0 (GstVideoFrame);;
      GstBuffer *buf = GST_BUFFER (item->object);

      if (!gst_video_frame_map (frame, thpad->vinfo, buf, GST_MAP_READ |
          GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
          GST_ERROR_OBJECT (muxer, "Failed to map thumbnail buffer!");
          g_slice_free (GstVideoFrame, frame);
          goto cleanup;
      }

      thframes = g_list_append (thframes, frame);
      item->object = NULL;
    }
  }

  GST_INFO_OBJECT (muxer, "Processing main frame %" GST_PTR_FORMAT
      " with %d thumbnail%s.", mainbuf, g_list_length (thframes),
      (g_list_length (thframes) != 1) ? "s" : "");

  GST_HEIFMUX_LOCK (muxer);

  if (!muxer->active) {
    GST_INFO_OBJECT (muxer, "Task has been deactivated!");
    GST_HEIFMUX_UNLOCK (muxer);
    goto cleanup;
  }

  // Get output buffer from the pool.
  if (GST_FLOW_OK != gst_buffer_pool_acquire_buffer (muxer->outpool,
      &outbuf, NULL)) {
    GST_ERROR_OBJECT (muxer, "Failed to acquire output buffer!");
    GST_HEIFMUX_UNLOCK (muxer);
    goto cleanup;
  }

  // Copy the flags and timestamps from the main input buffer.
  gst_buffer_copy_into (outbuf, mainbuf, GST_BUFFER_COPY_FLAGS |
      GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  success = gst_heif_engine_execute (muxer->engine, mainbuf, thframes, &outbuf);

  GST_HEIFMUX_UNLOCK (muxer);

  if (!success) {
    GST_ERROR_OBJECT (muxer, "Failed to execute heif muxer!");
    gst_buffer_unref (outbuf);
    outbuf = NULL;
    goto cleanup;
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (outbuf);
  item->size = gst_buffer_get_size (outbuf);
  item->duration = GST_BUFFER_DURATION (outbuf);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  GST_DEBUG_OBJECT (muxer, "Submitting %" GST_PTR_FORMAT, outbuf);

  GST_HEIFMUX_SRC_LOCK (muxer->srcpad);
  muxer->srcpad->segment.position = GST_BUFFER_TIMESTAMP (outbuf);
  GST_HEIFMUX_SRC_UNLOCK (muxer->srcpad);

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (muxer->srcpad->buffers, item))
    item->destroy (item);

cleanup:
  // Remove and free the sinkpad item from the queue.
  if (gst_data_queue_pop (muxer->sinkpad->buffers, &item))
    item->destroy (item);

  for (list = thframes; list != NULL; list = g_list_next (list)) {
    GstVideoFrame *frame = list->data;

    gst_video_frame_unmap (frame);
    g_slice_free (GstVideoFrame, frame);
  }

  if (thframes)
    g_list_free (thframes);

  for (list = muxer->thumbpads; list != NULL; list = g_list_next (list)) {
    GstHeifMuxSinkPad *thpad = GST_HEIFMUX_SINK_PAD (list->data);

    if (gst_data_queue_pop (thpad->buffers, &item))
      item->destroy (item);
  }
}

static gboolean
gst_heifmux_start_task (GstHeifMux * muxer)
{
  GST_HEIFMUX_LOCK (muxer);

  if (muxer->active) {
    GST_HEIFMUX_UNLOCK (muxer);
    return TRUE;
  }

  muxer->worktask = gst_task_new (gst_heifmux_worker_task, muxer, NULL);
  gst_task_set_lock (muxer->worktask, &muxer->worklock);

  GST_INFO_OBJECT (muxer, "Created task %p", muxer->worktask);

  muxer->active = TRUE;
  GST_HEIFMUX_UNLOCK (muxer);

  if (!gst_task_start (muxer->worktask)) {
    GST_ERROR_OBJECT (muxer, "Failed to start worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (muxer, "Started task %p", muxer->worktask);
  return TRUE;
}

static gboolean
gst_heifmux_stop_task (GstHeifMux * muxer)
{
  GST_HEIFMUX_LOCK (muxer);

  if (!muxer->active) {
    GST_HEIFMUX_UNLOCK (muxer);
    return TRUE;
  }

  GST_INFO_OBJECT (muxer, "Stopping task %p", muxer->worktask);

  if (!gst_task_stop (muxer->worktask))
    GST_WARNING_OBJECT (muxer, "Failed to stop worker task!");

  muxer->active = FALSE;

  GST_HEIFMUX_UNLOCK (muxer);

  // Make sure task is not running.
  g_rec_mutex_lock (&muxer->worklock);
  g_rec_mutex_unlock (&muxer->worklock);

  if (!gst_task_join (muxer->worktask)) {
    GST_ERROR_OBJECT (muxer, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (muxer, "Removing task %p", muxer->worktask);

  gst_object_unref (muxer->worktask);
  muxer->worktask = NULL;

  return TRUE;
}

static GstCaps *
gst_heifmux_main_sink_pad_getcaps (GstHeifMux * muxer, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *srccaps = NULL, *templcaps = NULL, *sinkcaps = NULL;

  templcaps = gst_pad_get_pad_template_caps (GST_PAD (muxer->srcpad));

  // Query the source pad peer with the transformed filter.
  srccaps = gst_pad_peer_query_caps (GST_PAD (muxer->srcpad), templcaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (pad, "Src caps %" GST_PTR_FORMAT, srccaps);

  templcaps = gst_pad_get_pad_template_caps (pad);
  sinkcaps = gst_caps_intersect (templcaps, srccaps);

  gst_caps_unref (srccaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (pad, "Filter caps  %" GST_PTR_FORMAT, filter);

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, sinkcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersection);

    gst_caps_unref (sinkcaps);
    sinkcaps = intersection;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}

static gboolean
gst_heifmux_main_sink_pad_setcaps (GstHeifMux * muxer, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;
  GstHeifMuxSinkPad *sinkpad = GST_HEIFMUX_SINK_PAD (pad);
  GstVideoInfo info = { 0 };

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  // Get the negotiated caps between the srcpad and its peer.
  srccaps = gst_pad_get_allowed_caps (GST_PAD (muxer->srcpad));
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

  if (gst_pad_has_current_caps (GST_PAD (muxer->srcpad))) {
    srccaps = gst_pad_get_current_caps (GST_PAD (muxer->srcpad));

    if (!gst_caps_is_equal (srccaps, intersect))
      gst_pad_mark_reconfigure (GST_PAD (muxer->srcpad));

    gst_caps_unref (srccaps);
  }

  gst_caps_unref (intersect);

  GST_DEBUG_OBJECT (pad, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (pad, "Failed to extract input video info from caps!");
    return FALSE;
  }

  if (sinkpad->vinfo != NULL)
    gst_video_info_free (sinkpad->vinfo);

  sinkpad->vinfo = gst_video_info_copy (&info);

  if (muxer->engine == NULL) {
    muxer->engine = gst_heif_engine_new ();
    g_return_val_if_fail (muxer->engine != NULL, FALSE);
  }

  // Unref previouly created pool.
  if (muxer->outpool) {
    gst_buffer_pool_set_active (muxer->outpool, FALSE);
    gst_object_unref (muxer->outpool);
  }

  // Creat a new output memory pool.
  muxer->outpool = gst_heifmux_create_pool (muxer, caps);
  if (!muxer->outpool) {
    GST_ERROR_OBJECT (muxer, "Failed to create output pool!");
    return FALSE;
  }

  // Activate the pool.
  if (!gst_buffer_pool_is_active (muxer->outpool) &&
      !gst_buffer_pool_set_active (muxer->outpool, TRUE)) {
    GST_ERROR_OBJECT (muxer, "Failed to activate output buffer pool!");
    return FALSE;
  }

  // Wait for pending buffers to be processed before sending new caps.
  GST_HEIFMUX_PAD_WAIT_IDLE (GST_HEIFMUX_SINK_PAD (pad));
  GST_HEIFMUX_PAD_WAIT_IDLE (muxer->srcpad);

  GST_DEBUG_OBJECT (pad, "Pushing new caps %" GST_PTR_FORMAT, caps);
  return gst_pad_push_event (GST_PAD (muxer->srcpad), gst_event_new_caps (caps));
}

static gboolean
gst_heifmux_main_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstHeifMux *muxer = GST_HEIFMUX (parent);

  GST_TRACE_OBJECT (muxer, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_heifmux_main_sink_pad_setcaps (muxer, pad, caps);

      gst_event_unref (event);
      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstHeifMuxSrcPad *srcpad = muxer->srcpad;
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      GST_HEIFMUX_LOCK (muxer);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);

        srcpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(srcpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(srcpad)->segment);
        gst_segment_copy_into (&segment, &srcpad->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        GST_HEIFMUX_UNLOCK (muxer);
        return FALSE;
      }

      gst_event_unref (event);
      event = gst_event_new_segment (&(srcpad)->segment);

      GST_HEIFMUX_UNLOCK (muxer);

      return gst_pad_push_event (GST_PAD (srcpad), event);
    }
    case GST_EVENT_FLUSH_START:
      gst_data_queue_set_flushing (GST_HEIFMUX_SINK_PAD (pad)->buffers, TRUE);
      gst_data_queue_flush (GST_HEIFMUX_SINK_PAD (pad)->buffers);

      gst_heifmux_stop_task (muxer);
      gst_heifmux_flush_thubmnail_queues (muxer);

      return gst_pad_push_event (pad, event);
    case GST_EVENT_FLUSH_STOP:
      gst_data_queue_set_flushing (GST_HEIFMUX_SINK_PAD (pad)->buffers, FALSE);
      gst_heifmux_start_task (muxer);

      return gst_pad_push_event (pad, event);
    case GST_EVENT_EOS:
      GST_HEIFMUX_PAD_WAIT_IDLE (GST_HEIFMUX_SINK_PAD (pad));
      GST_HEIFMUX_PAD_WAIT_IDLE (muxer->srcpad);

      gst_heifmux_flush_thubmnail_queues (muxer);
      return gst_pad_push_event (GST_PAD (muxer->srcpad), event);
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_heifmux_main_sink_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstHeifMux *muxer = GST_HEIFMUX (parent);

  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_heifmux_main_sink_pad_getcaps (muxer, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      GST_DEBUG_OBJECT (pad, "Accept caps: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_fixed (caps)) {
        GstCaps *tmplcaps = gst_pad_get_pad_template_caps (pad);
        GST_DEBUG_OBJECT (pad, "Template caps: %" GST_PTR_FORMAT, tmplcaps);

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
gst_heifmux_main_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstHeifMuxSinkPad *sinkpad = GST_HEIFMUX_SINK_PAD (pad);
  GstHeifMux *muxer = GST_HEIFMUX (parent);
  GstDataQueueItem *item = NULL;

  if (!gst_pad_has_current_caps (GST_PAD (muxer->srcpad))) {
    if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_FLUSHING;
    }

    GST_ELEMENT_ERROR (muxer, STREAM, DECODE, ("No caps set!"), (NULL));
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (pad, "Received %" GST_PTR_FORMAT, buffer);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (sinkpad->buffers, item))
    item->destroy (item);
  return GST_FLOW_OK;
}

static gboolean
gst_heifmux_thumbnail_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstHeifMuxSinkPad *thpad = GST_HEIFMUX_SINK_PAD (pad);

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL, *tmplcaps = NULL, *intersect = NULL;
      GstVideoInfo info = { 0 };

      gst_event_parse_caps (event, &caps);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

      // Get the negotiated caps between the srcpad and its peer.
      tmplcaps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (pad, "Template caps %" GST_PTR_FORMAT, tmplcaps);

      intersect = gst_caps_intersect (tmplcaps, caps);
      GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (tmplcaps);

      if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Template and sink caps do not intersect!");

        if (intersect != NULL)
          gst_caps_unref (intersect);

        return FALSE;
      }

      if (!gst_video_info_from_caps (&info, caps)) {
        GST_ERROR_OBJECT (pad, "Failed to extract input video info from caps!");
        return FALSE;
      }

      if (thpad->vinfo != NULL)
        gst_video_info_free (thpad->vinfo);

      thpad->vinfo = gst_video_info_copy (&info);
      return TRUE;
    }
    case GST_EVENT_FLUSH_START:
    {
      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_SEGMENT:
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
gst_heifmux_thumbnail_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstHeifMux *muxer= GST_HEIFMUX (parent);
  GstHeifMuxSinkPad *thpad = GST_HEIFMUX_SINK_PAD (pad);
  GstDataQueueItem *item = NULL;

  if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

  // If the main sink pad has reached EOS return EOS for data(meta) pads.
  if (GST_PAD_IS_EOS (muxer->sinkpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (thpad->buffers, item)) {
    GST_ERROR_OBJECT (muxer, "Thumbnail pad push failed.");
    item->destroy (item);
  }

  return GST_FLOW_OK;
}

static GstPad*
gst_heifmux_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstHeifMux *muxer = GST_HEIFMUX (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_HEIFMUX_LOCK (muxer);

  if (reqname && sscanf (reqname, "thumbnail_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= muxer->nextidx) ? index + 1 : muxer->nextidx;
  } else {
    index = muxer->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  name = g_strdup_printf ("thumbnail_%u", index);

  pad = g_object_new (GST_TYPE_HEIFMUX_SINK_PAD, "name", name,
      "direction", templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (muxer, "Failed to create thumbnail sink pad!");
    GST_HEIFMUX_UNLOCK (muxer);
    return NULL;
  }

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_heifmux_thumbnail_sink_pad_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_heifmux_thumbnail_sink_pad_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (muxer, "Failed to add thumbnail sink pad!");
    gst_object_unref (pad);
    GST_HEIFMUX_UNLOCK (muxer);
    return NULL;
  }

  muxer->thumbpads = g_list_append (muxer->thumbpads, pad);
  muxer->nextidx = nextindex;
  GST_HEIFMUX_SINK_PAD_CAST (pad)->buffers_limit = muxer->queue_size;

  GST_HEIFMUX_UNLOCK (muxer);

  GST_DEBUG_OBJECT (muxer, "Created thumbnail pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_heifmux_release_pad (GstElement * element, GstPad * pad)
{
  GstHeifMux *muxer = GST_HEIFMUX (element);

  GST_DEBUG_OBJECT (muxer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_HEIFMUX_LOCK (muxer);
  muxer->thumbpads = g_list_remove (muxer->thumbpads, pad);
  GST_HEIFMUX_UNLOCK (muxer);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_heifmux_change_state (GstElement * element, GstStateChange transition)
{
  GstHeifMux *muxer = GST_HEIFMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_data_queue_set_flushing (muxer->sinkpad->buffers, FALSE);
      gst_heifmux_start_task (muxer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_data_queue_set_flushing (muxer->sinkpad->buffers, TRUE);
      gst_data_queue_flush (muxer->sinkpad->buffers);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_heifmux_stop_task (muxer);
      gst_heifmux_flush_thubmnail_queues (muxer);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_heifmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHeifMux *muxer = GST_HEIFMUX (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (muxer);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (muxer, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (muxer);

  switch (prop_id) {
    case PROP_QUEUE_SIZE:
      muxer->queue_size = g_value_get_uint (value);
      muxer->sinkpad->buffers_limit = muxer->queue_size;
      muxer->srcpad->buffers_limit = muxer->queue_size;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (muxer);
}

static void
gst_heifmux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHeifMux *muxer = GST_HEIFMUX (object);

  GST_OBJECT_LOCK (muxer);

  switch (prop_id) {
    case PROP_QUEUE_SIZE:
      g_value_set_uint (value, muxer->queue_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (muxer);
}

static void
gst_heifmux_finalize (GObject * object)
{
  GstHeifMux *muxer = GST_HEIFMUX (object);

  if (muxer->outpool != NULL)
    gst_object_unref (muxer->outpool);

  if (muxer->engine != NULL) {
    gst_heif_engine_free (muxer->engine);
    muxer->engine = NULL;
  }

  g_rec_mutex_clear (&muxer->worklock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (muxer));
}

static void
gst_heifmux_class_init (GstHeifMuxClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_heifmux_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_heifmux_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_heifmux_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_heifmux_main_sink_template, GST_TYPE_HEIFMUX_SINK_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_heifmux_thumbnail_sink_template, GST_TYPE_HEIFMUX_SINK_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_heifmux_src_template, GST_TYPE_HEIFMUX_SRC_PAD);

  g_object_class_install_property (gobject, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size", "Input and output queue size",
          "Set the size of the input and output queues.",
          3, G_MAXUINT, DEFAULT_PROP_QUEUE_SIZE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (element,
      "Heif muxer", "HEIF/Thumbnail/Muxer",
      "Muxes compressed images as thumbnails with HEIF stream", "QTI");

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_heifmux_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_heifmux_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_heifmux_change_state);

  // Initializes a new muxer GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_heifmux_debug, "qtiheifmux", 0, "QTI Heif Muxer");
}

static void
gst_heifmux_init (GstHeifMux * muxer)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&muxer->lock);

  muxer->nextidx = 0;
  muxer->thumbpads = NULL;

  muxer->sinkpad = NULL;
  muxer->srcpad = NULL;

  muxer->engine = NULL;
  muxer->outpool = NULL;

  muxer->worktask = NULL;
  g_rec_mutex_init (&muxer->worklock);
  muxer->active = FALSE;

  muxer->queue_size = DEFAULT_PROP_QUEUE_SIZE;

  template = gst_static_pad_template_get (&gst_heifmux_main_sink_template);
  muxer->sinkpad = g_object_new (GST_TYPE_HEIFMUX_SINK_PAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_heifmux_main_sink_pad_event));
  gst_pad_set_query_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_heifmux_main_sink_pad_query));
  gst_pad_set_chain_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_heifmux_main_sink_pad_chain));

  GST_OBJECT_FLAG_SET (muxer->sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_element_add_pad (GST_ELEMENT (muxer), GST_PAD (muxer->sinkpad));
  muxer->sinkpad->buffers_limit = muxer->queue_size;

  template = gst_static_pad_template_get (&gst_heifmux_src_template);
  muxer->srcpad = g_object_new (GST_TYPE_HEIFMUX_SRC_PAD, "name", "src",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_heifmux_src_pad_event));
  gst_pad_set_query_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_heifmux_src_pad_query));
  gst_pad_set_activatemode_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_heifmux_src_pad_activate_mode));
  gst_element_add_pad (GST_ELEMENT (muxer), GST_PAD (muxer->srcpad));
  muxer->srcpad->buffers_limit = muxer->queue_size;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiheifmux", GST_RANK_NONE,
      GST_TYPE_HEIFMUX);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiheifmux,
    "QTI Heif Muxer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
