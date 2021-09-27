/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#include <gbm.h>
#include <gbm_priv.h>
#include <gst/memory/gstionpool.h>

#include "imagepyramid.h"
#include "imagepyramidpads.h"

#define GST_CAT_DEFAULT cvp_imgpyramid_debug
GST_DEBUG_CATEGORY_STATIC (cvp_imgpyramid_debug);

#define gst_cvp_imgpyramid_parent_class parent_class
G_DEFINE_TYPE (GstCvpImgPyramid, gst_cvp_imgpyramid, GST_TYPE_ELEMENT);

#define DEFAULT_PROP_N_SCALES           4
#define DEFAULT_PROP_N_OCTAVES          5
#define DEFAULT_PROP_OP_FPS             30
#define DEFAULT_OCTAVE_SHARPNESS_COEF   3

#define DEFAULT_MIN_BUFFERS             2
#define DEFAULT_MAX_BUFFERS             10

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#define GST_VIDEO_FORMATS "{ GRAY8, NV12 }"

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
  ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
      : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
          : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
              : (state <= GST_STATE_NULL))))

static GType gst_cvp_request_get_type (void);
#define GST_TYPE_CVP_REQUEST  (gst_cvp_request_get_type())
#define GST_CVP_REQUEST(obj) ((GstCvpRequest *) obj)

/* Properties */
enum
{
  PROP_0,
  PROP_N_OCTAVES,
  PROP_N_SCALES,
  PROP_OCTAVE_SHARPNESS_COEF,
};

static GstStaticPadTemplate gst_cvp_imgpyramid_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
            GST_VIDEO_FORMATS))
    );

static GstStaticPadTemplate gst_cvp_imgpyramid_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ GRAY8 }"))
    );

typedef struct _GstCvpRequest GstCvpRequest;

struct _GstCvpRequest
{
  GstMiniObject parent;

  // Input frame submitted with provided ID.
  GstVideoFrame *inframe;

  // Output frames submitted with provided ID.
  GstBufferList *outbuffers;
  // Number of output frames.
  guint         n_outputs;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstCvpRequest, gst_cvp_request);

static void
gst_cvp_request_free (GstCvpRequest * request)
{
  GstBuffer *buffer = NULL;

  GST_TRACE ("Freeing request: %p", request);
  buffer = request->inframe->buffer;

  if (buffer != NULL) {
    gst_video_frame_unmap (request->inframe);
    gst_buffer_unref (buffer);
  }

  if (request->outbuffers) {
    gst_buffer_list_unref (request->outbuffers);
    request->outbuffers = NULL;
  }

  g_free (request->inframe);
  g_free (request);
}

static GstCvpRequest *
gst_cvp_request_new ()
{
  GstCvpRequest *request = g_new0 (GstCvpRequest, 1);

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_CVP_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_cvp_request_free);

  request->inframe = NULL;
  request->outbuffers = NULL;
  request->n_outputs = 0;
  request->time = GST_CLOCK_TIME_NONE;

  return request;
}

static inline void
gst_cvp_request_unref (GstCvpRequest * request)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (request));
}

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;
  gst_mini_object_unref (item->object);
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    if (feature == NULL && ((gst_caps_features_get_size (features) == 0) ||
            gst_caps_features_is_any (features)))
      return TRUE;

    // Skip ANY caps and return immediately if feature is present.
    if ((feature != NULL) && !gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
}

static gboolean
gst_cvp_imgpyramid_prepare_output_buffer (GstCvpImgPyramid * imgpyramid,
    GstCvpRequest * request)
{
  GstBufferPool *pool = NULL;
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;
  guint idx = 0;

  inbuffer = request->inframe->buffer;

  for (idx = 1; idx < request->n_outputs; idx++) {
    pool =
        GST_BUFFER_POOL_CAST (g_hash_table_lookup (imgpyramid->bufferpools,
            GUINT_TO_POINTER (idx)));

    if (!gst_buffer_pool_is_active (pool) &&
        !gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (imgpyramid, "Failed to activate buffer pool!");
      return FALSE;
    }

    // Retrieve new output buffer from the pool.
    if (gst_buffer_pool_acquire_buffer (pool, &outbuffer, NULL)
        != GST_FLOW_OK) {
      GST_ERROR_OBJECT (imgpyramid, "Failed to acquire buffer!");
      return FALSE;
    }

    // Copy the flags and timestamps from the input buffer.
    gst_buffer_copy_into (outbuffer, inbuffer,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

#ifdef HAVE_LINUX_DMA_BUF_H
    if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
      struct dma_buf_sync bufsync;
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

      bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

      if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
        GST_WARNING_OBJECT (imgpyramid, "DMA IOCTL SYNC START failed!");
    }
#endif // HAVE_LINUX_DMA_BUF_H

    gst_buffer_list_insert (request->outbuffers, -1, outbuffer);
  }

  return TRUE;
}

static void
gst_cvp_imgpyramid_push_output_buffer (gpointer key,
    gpointer value, gpointer userdata)
{
  GstDataQueueItem *item = NULL;
  GstCvpRequest *request = GST_CVP_REQUEST (userdata);
  guint idx = GPOINTER_TO_UINT (key);
  GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (value);
  GstBuffer *outbuffer = gst_buffer_list_get (request->outbuffers, idx - 1);

  gst_buffer_ref (outbuffer);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (srcpad, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (outbuffer);
  item->size = gst_buffer_get_size (outbuffer);
  item->duration = GST_BUFFER_DURATION (outbuffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (srcpad->buffers, item)) {
    item->destroy (item);
    GST_WARNING_OBJECT (srcpad,
        "Failed to push buffer %" GST_PTR_FORMAT, outbuffer);
  }
}

static void
gst_cvp_imgpyramid_worker_task (gpointer userdata)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (userdata);
  GstCvpImgPyramidSinkPad *sinkpad = GST_CVP_IMGPYRAMID_SINKPAD (imgpyramid->sinkpad);
  GstDataQueueItem *item = NULL;
  gboolean success;

  if (gst_data_queue_pop (sinkpad->requests, &item)) {
    GstCvpRequest *request = NULL;

    request = GST_CVP_REQUEST (gst_mini_object_ref (item->object));

    success = gst_cvp_imgpyramid_engine_execute (imgpyramid->engine, request->inframe,
        request->outbuffers);

    if (!success) {
      GST_ERROR_OBJECT (sinkpad, "Failed to execute request!");
      item->destroy (item);
      gst_cvp_request_unref (request);
      return;
    }

    g_hash_table_foreach (imgpyramid->srcpads,
        (GHFunc) gst_cvp_imgpyramid_push_output_buffer, request);

    item->destroy (item);
    // Free the memory allocated by the internal request structure.
    gst_cvp_request_unref (request);
  } else {
    GST_INFO_OBJECT (imgpyramid, "Pause worker task!");
    gst_task_pause (imgpyramid->worktask);
  }
}

static gboolean
gst_cvp_imgpyramid_start_worker_task (GstCvpImgPyramid * imgpyramid)
{
  if (imgpyramid->worktask != NULL)
    return TRUE;

  imgpyramid->worktask =
      gst_task_new (gst_cvp_imgpyramid_worker_task, imgpyramid, NULL);
  GST_INFO_OBJECT (imgpyramid, "Created task %p", imgpyramid->worktask);

  gst_task_set_lock (imgpyramid->worktask, &imgpyramid->worklock);

  if (!gst_task_start (imgpyramid->worktask)) {
    GST_ERROR_OBJECT (imgpyramid, "Failed to start worker task!");
    return FALSE;
  }

  // Disable requests queue in flushing state to enable normal work.
  gst_data_queue_set_flushing (GST_CVP_IMGPYRAMID_SINKPAD (imgpyramid->
          sinkpad)->requests, FALSE);
  return TRUE;
}

static gboolean
gst_cvp_imgpyramid_stop_worker_task (GstCvpImgPyramid * imgpyramid)
{
  if (NULL == imgpyramid->worktask)
    return TRUE;

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (GST_CVP_IMGPYRAMID_SINKPAD (imgpyramid->
          sinkpad)->requests, TRUE);

  if (!gst_task_stop (imgpyramid->worktask))
    GST_WARNING_OBJECT (imgpyramid, "Failed to stop worker task!");

  // Make sure task is not running.
  g_rec_mutex_lock (&imgpyramid->worklock);
  g_rec_mutex_unlock (&imgpyramid->worklock);

  if (!gst_task_join (imgpyramid->worktask)) {
    GST_ERROR_OBJECT (imgpyramid, "Failed to join worker task!");
    return FALSE;
  }

  gst_data_queue_flush (GST_CVP_IMGPYRAMID_SINKPAD (imgpyramid->sinkpad)->requests);

  GST_INFO_OBJECT (imgpyramid, "Removing task %p", imgpyramid->worktask);

  gst_object_unref (imgpyramid->worktask);
  imgpyramid->worktask = NULL;

  return TRUE;
}

static GstFlowReturn
gst_cvp_imgpyramid_sinkpad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * inbuffer)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (parent);
  GstCvpRequest *request = NULL;
  GstDataQueueItem *item = NULL;
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, inbuffer);

  // Convenient structure containing all the necessary data.
  request = gst_cvp_request_new ();
  request->inframe = g_new0 (GstVideoFrame, 1);
  request->outbuffers = gst_buffer_list_new ();
  request->n_outputs = imgpyramid->n_levels;

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  success = gst_video_frame_map (request->inframe,
      GST_CVP_IMGPYRAMID_SINKPAD (pad)->info, inbuffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  // Fetch and prepare output buffers for each level
  success = gst_cvp_imgpyramid_prepare_output_buffer (imgpyramid, request);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Failed to prepare output video frames!");
    gst_cvp_request_unref (request);
    return GST_FLOW_ERROR;
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (request);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (GST_CVP_IMGPYRAMID_SINKPAD (pad)->requests, item))
    item->destroy (item);

  return GST_FLOW_OK;
}

static gboolean
gst_cvp_imgpyramid_create_pool (GstCvpImgPyramid * imgpyramid, GArray * sizes)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint idx, size;

  for (idx = 1; idx < imgpyramid->n_levels; idx++) {
    size = g_array_index (sizes, guint, idx);
    pool = gst_ion_buffer_pool_new ();

    if (pool == NULL) {
      GST_ERROR_OBJECT (imgpyramid, "Failed to create pool of size (%u)!", size);
      return FALSE;
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, NULL, size,
        DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

    allocator = gst_fd_allocator_new ();
    gst_buffer_pool_config_set_allocator (config, allocator, NULL);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_WARNING_OBJECT (imgpyramid, "Failed to set pool configuration!");
      g_object_unref (pool);
      return FALSE;
    }

    g_object_unref (allocator);

    g_hash_table_insert (imgpyramid->bufferpools, GUINT_TO_POINTER (idx), pool);
  }

  return TRUE;
}

// Sink pad implementation
static GstCaps *
gst_cvp_imgpyramid_sinkpad_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = NULL, *intersect = NULL;

  if (!(caps = gst_pad_get_current_caps (pad)))
    caps = gst_pad_get_pad_template_caps (pad);

  GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);
    intersect =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersect;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_cvp_imgpyramid_sinkpad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *tmplcaps = NULL;
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success &= gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with template!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cvp_imgpyramid_sinkpad_setcaps (GstCvpImgPyramid * imgpyramid, GstPad * pad,
    GstCaps * caps)
{
  GstVideoInfo info = { 0, };
  guint size = 0, stride = 0, scanline = 0;
  GstCvpImgPyramidSettings settings;
  GArray *level_sizes = NULL;
  GstVideoFormat format;
  GHashTableIter iter;
  gpointer key, value;


  GST_DEBUG_OBJECT (imgpyramid, "Setting caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (imgpyramid,
        "Failed to extract input video info from caps!");
    return FALSE;
  }

  format = GST_VIDEO_INFO_FORMAT (&info);
  if (format != GST_VIDEO_FORMAT_NV12) {
    GST_ERROR_OBJECT (imgpyramid, "Invalid video format");
    return FALSE;
  }

  GST_CVP_IMGPYRAMID_LOCK (imgpyramid);

  g_hash_table_iter_init (&iter, imgpyramid->srcpads);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (value);
    if (srcpad && !gst_cvp_imgpyramid_srcpad_setcaps (srcpad)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (imgpyramid), CORE, NEGOTIATION, (NULL),
          ("Failed to set caps to %s!", GST_PAD_NAME (srcpad)));

      GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);
      return FALSE;
    }
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    GST_LOG_OBJECT (imgpyramid, "Using stride and scanline from GBM");

    struct gbm_buf_info bufinfo;
    bufinfo.width = GST_VIDEO_INFO_WIDTH (&info);
    bufinfo.height = GST_VIDEO_INFO_HEIGHT (&info);
    bufinfo.format = GBM_FORMAT_NV12;

    gbm_perform (GBM_PERFORM_GET_BUFFER_SIZE_DIMENSIONS,
        &bufinfo, 0, &stride, &scanline, &size);
  } else {
    GST_LOG_OBJECT (imgpyramid, "Using stride and scanline from GstVideoInfo");

    stride = GST_VIDEO_INFO_PLANE_STRIDE (&info, 0);
    scanline = (GST_VIDEO_INFO_N_PLANES (&info) == 2) ?
        (GST_VIDEO_INFO_PLANE_OFFSET (&info, 1) / stride) :
        GST_VIDEO_INFO_SIZE (&info);
  }

  GST_LOG_OBJECT (imgpyramid, "stride %d, scanline %d", stride, scanline);

  if (imgpyramid->engine != NULL)
    gst_cvp_imgpyramid_engine_free (imgpyramid->engine);

  // Fill the cvp_imgpyramid input options structure.
  settings.width = GST_VIDEO_INFO_WIDTH (&info);
  settings.height = GST_VIDEO_INFO_HEIGHT (&info);
  settings.stride = stride;
  settings.scanline = scanline;
  settings.format = GST_VIDEO_INFO_FORMAT (&info);
  settings.framerate =
      GST_VIDEO_INFO_FPS_N (&info) / GST_VIDEO_INFO_FPS_D (&info);
  settings.n_octaves = imgpyramid->n_octaves;
  settings.n_scales = imgpyramid->n_scales;
  settings.div2coef = imgpyramid->octave_sharpness;
  level_sizes = g_array_new (FALSE, FALSE, sizeof (guint));

  imgpyramid->engine = gst_cvp_imgpyramid_engine_new (&settings, level_sizes);

  if (GST_CVP_IMGPYRAMID_SINKPAD (pad)->info != NULL)
    gst_video_info_free (GST_CVP_IMGPYRAMID_SINKPAD (pad)->info);

  GST_CVP_IMGPYRAMID_SINKPAD (pad)->info = gst_video_info_copy (&info);

  // Create buffer pool
  if (!gst_cvp_imgpyramid_create_pool (imgpyramid, level_sizes)) {
    GST_ERROR_OBJECT (imgpyramid, "Failed to create pool!");
    return FALSE;
  }

  g_array_free (level_sizes, TRUE);

  GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);

  return TRUE;
}

static gboolean
gst_cvp_imgpyramid_sinkpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_cvp_imgpyramid_sinkpad_getcaps (pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_cvp_imgpyramid_sinkpad_acceptcaps (pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_cvp_imgpyramid_sinkpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (parent);
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_cvp_imgpyramid_sinkpad_setcaps (imgpyramid, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstCvpImgPyramidSinkPad *sinkpad = GST_CVP_IMGPYRAMID_SINKPAD (pad);
      GHashTableIter iter;
      gpointer key, value;
      GstSegment segment;

      gst_event_copy_segment (event, &segment);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(sinkpad)->segment, GST_FORMAT_TIME);
        sinkpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
        gst_segment_copy_into (&segment, &(sinkpad)->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }

      GST_OBJECT_LOCK (imgpyramid);

      g_hash_table_iter_init (&iter, imgpyramid->srcpads);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (value);
        gst_segment_copy_into (&(sinkpad)->segment, &(srcpad)->segment);
      }

      GST_OBJECT_UNLOCK (imgpyramid);

      event = gst_event_new_segment (&(sinkpad)->segment);

      success = gst_element_foreach_src_pad (GST_ELEMENT (imgpyramid),
          gst_cvp_imgpyramid_srcpad_push_event, event);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_STREAM_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (imgpyramid),
          gst_cvp_imgpyramid_srcpad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (imgpyramid),
          gst_cvp_imgpyramid_srcpad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_STOP:
    {
      GstCvpImgPyramidSinkPad *sinkpad = GST_CVP_IMGPYRAMID_SINKPAD (pad);
      GHashTableIter iter;
      gpointer key, value;

      GST_OBJECT_LOCK (imgpyramid);

      g_hash_table_iter_init (&iter, imgpyramid->srcpads);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        GstCvpImgPyramidSrcPad *srcpad = GST_CVP_IMGPYRAMID_SRCPAD (value);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);
      }

      GST_OBJECT_UNLOCK (imgpyramid);

      gst_segment_init (&(sinkpad)->segment, GST_FORMAT_UNDEFINED);

      success = gst_element_foreach_src_pad (GST_ELEMENT (imgpyramid),
          gst_cvp_imgpyramid_srcpad_push_event, event);
      return success;
    }
    case GST_EVENT_EOS:
      success = gst_element_foreach_src_pad (GST_ELEMENT (imgpyramid),
          gst_cvp_imgpyramid_srcpad_push_event, event);
      return success;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstPad *
gst_cvp_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0;
  guint nlevels = imgpyramid->n_levels;

  GST_CVP_IMGPYRAMID_LOCK (imgpyramid);

  if (reqname && sscanf (reqname, "src_%u", &index) == 1) {
    if (index == 0 || index > nlevels) {
      GST_ERROR_OBJECT (imgpyramid,
          "Source pad index (%u) is invalid, expected (0 < index <=%u)",
          index, nlevels);
      GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);
      return NULL;
    }
    if (g_hash_table_contains (imgpyramid->srcpads, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (imgpyramid, "Source pad name %s is not unique", reqname);
      GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);
      return NULL;

    }
  } else {
    GST_ERROR_OBJECT (imgpyramid, "Source pad name must include the index: %s",
        reqname);
    GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);
    return NULL;
  }

  GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);

  name = g_strdup_printf ("src_%u", index);

  pad = g_object_new (GST_TYPE_CVP_IMGPYRAMID_SRCPAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (imgpyramid, "Failed to create source pad!");
    return NULL;
  }

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_srcpad_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_srcpad_event));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_srcpad_activate_mode));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (imgpyramid, "Failed to add source pad!");
    gst_object_unref (pad);
    return NULL;
  }

  GST_CVP_IMGPYRAMID_LOCK (imgpyramid);
  g_hash_table_insert (imgpyramid->srcpads, GUINT_TO_POINTER (index), pad);
  GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);

  GST_DEBUG_OBJECT (imgpyramid, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_cvp_imgpyramid_release_pad (GstElement * element, GstPad * pad)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (element);

  GST_DEBUG_OBJECT (imgpyramid, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_CVP_IMGPYRAMID_LOCK (imgpyramid);
  g_hash_table_remove (imgpyramid->srcpads, GUINT_TO_POINTER (index));
  GST_CVP_IMGPYRAMID_UNLOCK (imgpyramid);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_cvp_imgpyramid_change_state (GstElement * element, GstStateChange transition)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_cvp_imgpyramid_start_worker_task (imgpyramid)) {
        GST_ERROR_OBJECT (imgpyramid, "Failed to start worker task!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_cvp_imgpyramid_stop_worker_task (imgpyramid)) {
        GST_ERROR_OBJECT (imgpyramid, "Failed to stop worker task!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_cvp_imgpyramid_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (imgpyramid);
  guint val;

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (imgpyramid, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (imgpyramid);

  switch (prop_id) {
    case PROP_N_OCTAVES:
      val = g_value_get_uint (value);
      imgpyramid->n_octaves = val;
      imgpyramid->n_levels = imgpyramid->n_octaves * imgpyramid->n_scales;
      break;
    case PROP_N_SCALES:
      val = g_value_get_uint (value);
      imgpyramid->n_scales = val;
      imgpyramid->n_levels = imgpyramid->n_octaves * imgpyramid->n_scales;
      break;
    case PROP_OCTAVE_SHARPNESS_COEF:
    {
      guint len, idx, *data;

      len = gst_value_array_get_size (value);
      g_return_if_fail (len <= imgpyramid->n_octaves);

      for (idx = 0; idx < len; idx++) {
        val = g_value_get_uint (gst_value_array_get_value (value, idx));
        data = &g_array_index (imgpyramid->octave_sharpness, guint, idx);
        *data = val;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (imgpyramid);
}

static void
gst_cvp_imgpyramid_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (object);

  GST_OBJECT_LOCK (imgpyramid);

  switch (prop_id) {
    case PROP_N_OCTAVES:
      g_value_set_uint (value, imgpyramid->n_octaves);
      break;
    case PROP_N_SCALES:
      g_value_set_uint (value, imgpyramid->n_scales);
      break;
    case PROP_OCTAVE_SHARPNESS_COEF:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_UINT);
      guint idx;

      for (idx = 0; idx < imgpyramid->n_octaves; idx++) {
        g_value_set_uint (&val,
            g_array_index (imgpyramid->octave_sharpness, guint, idx));
        gst_value_array_append_value (value, &val);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (imgpyramid);
}

static void
gst_cvp_imgpyramid_finalize (GObject * object)
{
  GstCvpImgPyramid *imgpyramid = GST_CVP_IMGPYRAMID (object);

  if (imgpyramid->engine != NULL)
    gst_cvp_imgpyramid_engine_free (imgpyramid->engine);

  if (imgpyramid->octave_sharpness != NULL)
    g_array_free (imgpyramid->octave_sharpness, TRUE);

  if (imgpyramid->srcpads != NULL) {
    g_hash_table_destroy (imgpyramid->srcpads);
    imgpyramid->srcpads = NULL;
  }

  if (imgpyramid->bufferpools != NULL) {
    g_hash_table_destroy (imgpyramid->bufferpools);
    imgpyramid->bufferpools = NULL;
  }

  g_rec_mutex_clear (&(imgpyramid)->worklock);
  g_mutex_clear (&(imgpyramid)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (imgpyramid));
}

static void
gst_cvp_imgpyramid_init (GstCvpImgPyramid * imgpyramid)
{
  GstPadTemplate *template = NULL;
  guint idx;

  imgpyramid->n_octaves = 5;
  imgpyramid->n_scales = 4;
  imgpyramid->n_levels = imgpyramid->n_octaves * imgpyramid->n_scales;

  imgpyramid->octave_sharpness = g_array_new (FALSE, FALSE, sizeof (guint));
  guint default_sharpness = DEFAULT_OCTAVE_SHARPNESS_COEF;
  for (idx = 0; idx < imgpyramid->n_octaves; idx++) {
    g_array_append_val (imgpyramid->octave_sharpness, default_sharpness);
  }

  g_mutex_init (&(imgpyramid)->lock);
  g_rec_mutex_init (&imgpyramid->worklock);

  imgpyramid->srcpads = g_hash_table_new (NULL, NULL);
  imgpyramid->bufferpools = g_hash_table_new (NULL, NULL);;

  imgpyramid->worktask = NULL;

  template = gst_static_pad_template_get (&gst_cvp_imgpyramid_sink_template);
  imgpyramid->sinkpad =
      g_object_new (GST_TYPE_CVP_IMGPYRAMID_SINKPAD, "name", "sink", "direction",
      template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_chain_function (imgpyramid->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_sinkpad_chain));
  gst_pad_set_query_function (imgpyramid->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_sinkpad_query));
  gst_pad_set_event_function (imgpyramid->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_sinkpad_event));

  gst_element_add_pad (GST_ELEMENT (imgpyramid), imgpyramid->sinkpad);
}

static void
gst_cvp_imgpyramid_class_init (GstCvpImgPyramidClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_cvp_imgpyramid_sink_template, GST_TYPE_CVP_IMGPYRAMID_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_cvp_imgpyramid_src_template, GST_TYPE_CVP_IMGPYRAMID_SRCPAD);


  g_object_class_install_property (gobject, PROP_N_OCTAVES,
      g_param_spec_uint ("num-octaves", "Number of octaves",
          "Number of layers in the pyramid where the resolution is halved",
          1, 5, DEFAULT_PROP_N_OCTAVES,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_N_SCALES,
      g_param_spec_uint ("num-scales", "Number of scales",
          "Number of intermediate layers in the pyramid between two octaves",
          1, 4, DEFAULT_PROP_N_SCALES,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_OCTAVE_SHARPNESS_COEF,
      gst_param_spec_array ("octave-sharpness", "Adjust sharpness of octaves",
          "Array of coefficients. The size of this array is equal to the number of"
          "octaves (n_octaves). Format is <c1, c2, c3, cn>."
          "The value range per octave [0-4], with default 3",
          g_param_spec_uint ("value", "Coefficient Value",
              "One of the filter coefficient value",
              0, 4, DEFAULT_OCTAVE_SHARPNESS_COEF,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "CVP Image Pyramid Scaler", "Runs image pyramid downscaler from CVP",
      "Generates image pyramid with downsampled images per input video frame",
      "QTI");

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_cvp_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_cvp_imgpyramid_change_state);

  // Initializes a new qticvpimgpyramid GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (cvp_imgpyramid_debug, "qticvpimgpyramid", 0,
      "QTI Computer Vision Processor Image Pyramid Scaler");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qticvpimgpyramid", GST_RANK_PRIMARY,
      GST_TYPE_CVP_IMGPYRAMID);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qticvpimgpyramid,
    "Computer Vision Processor Image Pyramid Scaler",
    plugin_init,
    PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
