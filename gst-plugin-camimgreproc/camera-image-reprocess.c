/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "camera-image-reprocess.h"

#include <stdio.h>
#include <string.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>
#include <gst/video/gstimagepool.h>
#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>

#define GST_CAT_DEFAULT gst_camera_image_reproc_debug
GST_DEBUG_CATEGORY (gst_camera_image_reproc_debug);

static void gst_camera_image_reproc_child_proxy_init (gpointer g_iface,
    gpointer data);

#define gst_camera_image_reproc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCameraImageReproc, gst_camera_image_reproc,
    GST_TYPE_ELEMENT, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
    gst_camera_image_reproc_child_proxy_init));

#define EXTRACT_FIELD_PARAMS(structure, name, offset, size, isunsigned)  \
{                                                                        \
  const GValue *value = gst_structure_get_value (structure, name);       \
  g_return_val_if_fail (value != NULL, FALSE);                           \
                                                                         \
  offset = g_value_get_uchar (gst_value_array_get_value (value, 0));     \
  size = g_value_get_uchar (gst_value_array_get_value (value, 1));       \
  isunsigned = g_value_get_uchar (gst_value_array_get_value (value, 2)); \
}

#define DEFAULT_PROP_MIN_BUFFERS    2
#define DEFAULT_PROP_MAX_BUFFERS    10
#define DEFAULT_PROP_QUEUE_SIZE     10

// Pad Template
#define GST_CAPS_FORMATS "{ NV12, NV12_Q08C, P010_10LE }"

enum
{
  PROP_0,
  PROP_QUEUE_SIZE,
};

static GstStaticCaps gst_camera_image_reproc_static_caps =
    GST_STATIC_CAPS (CAMERA_IMAGE_REPROC_VIDEO_JPEG_CAPS "; "
        CAMERA_IMAGE_REPROC_VIDEO_RAW_CAPS (
            GST_CAPS_FORMATS) "; "
        CAMERA_IMAGE_REPROC_VIDEO_BAYER_CAPS (
            "{ bggr, rggb, gbrg, grbg, mono }",
            "{ 8, 10, 12, 16 }"));

static GstCaps *
gst_camera_reproc_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_camera_image_reproc_static_caps);

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_CAPS_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_camera_reproc_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_camera_image_reproc_static_caps);

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_CAPS_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_camera_reproc_sink_template (void)
{
  return gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK,
      GST_PAD_REQUEST, gst_camera_reproc_sink_caps (),
      GST_TYPE_CAMERA_REPROC_SINK_PAD);
}

static GstPadTemplate *
gst_camera_reproc_src_template (void)
{
  return gst_pad_template_new_with_gtype ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, gst_camera_reproc_src_caps (),
      GST_TYPE_CAMERA_REPROC_SRC_PAD);
}

static GstBufferPool*
gst_camera_image_reproc_create_buffer_pool (GstCameraImageReproc *reprocess,
    GstCaps *caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;
  gint alignedw, alignedh;
  gsize aligned_size;

  if (!gst_video_info_from_caps (&info, gst_caps_fixate(caps))) {
    GST_ERROR_OBJECT (reprocess, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (reprocess, "Failed to create image pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (reprocess, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (reprocess, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (reprocess, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  if (GST_VIDEO_INFO_FORMAT(&info) == GST_VIDEO_FORMAT_ENCODED) {
    // Align size to 64 lines
    alignedw = (GST_VIDEO_INFO_WIDTH (&info) + 64-1) & ~(64-1);
    alignedh = (GST_VIDEO_INFO_HEIGHT (&info) + 64-1) & ~(64-1);
    aligned_size = alignedw * alignedh * 4;
  } else {
    aligned_size = info.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, aligned_size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (reprocess, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  gst_object_unref (allocator);

  return pool;
}

static GstBuffer *
gst_camera_image_reproc_create_output_buffer (GstCameraImageReproc * reprocess,
    GstBuffer * inbuffer)
{
  GstBuffer *outbuffer = NULL;
  GstBufferPool *pool = reprocess->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (reprocess,
        "Failed to activate output video buffer pool!");
    return NULL;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, &outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (reprocess, "Failed to create output video buffer!");
    return NULL;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  GST_TRACE_OBJECT (reprocess, "Providing %" GST_PTR_FORMAT, outbuffer);

  return outbuffer;
}

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;

  if (item->object != NULL)
    gst_buffer_unref (GST_BUFFER (item->object));

  g_slice_free (GstDataQueueItem, item);
}

static void
gst_camera_image_reproc_data_callback (gpointer *array, gpointer userdata)
{
  GstCameraImageReproc *reprocess =
      GST_CAMERA_IMAGE_REPROC (userdata);
  GPtrArray *ptr_array = (GPtrArray *)array;
  GstBuffer *inbuf[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM] = {NULL};
  GstBuffer *outbuf = NULL;
  GstDataQueueItem *item = NULL;
  guint idx = 0;
  GstClockTime duration, position;

  duration = position = GST_CLOCK_TIME_NONE;

  for (idx = 0; idx < OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM; idx++) {
    inbuf[idx] = (GstBuffer *) g_ptr_array_index (ptr_array, idx);

    if (inbuf[idx] != NULL)
      gst_buffer_unref (inbuf[idx]);
  }

  outbuf = (GstBuffer *) g_ptr_array_index (ptr_array, 2);

  duration = GST_BUFFER_DURATION (outbuf);
  position = GST_BUFFER_TIMESTAMP (outbuf);
  position += (duration != GST_CLOCK_TIME_NONE) ? duration : 0;

  reprocess->srcpad->segment.position = position;

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (outbuf);
  item->size = gst_buffer_get_size (outbuf);
  item->duration = GST_BUFFER_DURATION (outbuf);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (reprocess->srcpad->buffers, item))
    item->destroy (item);

  GST_LOG_OBJECT (reprocess, "Callback called. GstBuffer(%p) pushed.",
      outbuf);
}

static gboolean
gst_camera_image_reproc_set_format (GstCameraImageReproc * reprocess)
{
  GList * list = NULL;
  guint idx = 0;
  GstCaps * sinkcaps;
  GstCaps * srccaps;
  GstStructure *input, *output;
  GstCameraImageParams params[2] = {0};
  gboolean ret;

  //update reprocess context
  for (list = GST_ELEMENT (reprocess)->sinkpads; list != NULL; list = list->next) {
    GstCameraReprocSinkPad *sinkpad =
        GST_CAMERA_REPROC_SINK_PAD (list->data);

    sinkcaps = gst_pad_get_current_caps (GST_PAD (sinkpad));
    input = gst_caps_get_structure (sinkcaps, 0);
    gst_structure_get_int (input, "width", &params[0].width);
    gst_structure_get_int (input, "height", &params[0].height);

    if (g_str_equal (gst_structure_get_string (input, "format"), "bggr") ||
        g_str_equal (gst_structure_get_string (input, "format"), "rggb") ||
        g_str_equal (gst_structure_get_string (input, "format"), "gbrg") ||
        g_str_equal (gst_structure_get_string (input, "format"), "grbg") ||
        g_str_equal (gst_structure_get_string (input, "format"), "mono"))
      params[0].format = GST_VIDEO_FORMAT_UNKNOWN;
    else
      params[0].format = gst_video_format_from_string (
          gst_structure_get_string (input, "format"));

    gst_caps_unref (sinkcaps);

    gst_camera_image_reproc_context_update(reprocess->context, idx,
        sinkpad->camera_id, sinkpad->req_meta_path, sinkpad->req_meta_step,
        sinkpad->eis);
    idx++;
  }

  srccaps = gst_pad_get_current_caps (GST_PAD (reprocess->srcpad));
  output = gst_caps_get_structure (srccaps, 0);
  gst_structure_get_int (output, "width", &params[1].width);
  gst_structure_get_int (output, "height", &params[1].height);

  if (g_str_equal (gst_structure_get_name(output), "image/jpeg"))
    params[1].format = GST_VIDEO_FORMAT_ENCODED;
  else
    params[1].format = gst_video_format_from_string (
        gst_structure_get_string (output, "format"));

  ret = gst_camera_image_reproc_context_create (reprocess->context, params,
      gst_camera_image_reproc_data_callback);

  if (!ret) {
    GST_ERROR_OBJECT (reprocess,
        "Failed to configure camera reprocess module.");
    return FALSE;
  }

  return TRUE;
}

static void
gst_camera_image_reproc_worker_task (gpointer userdata)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (userdata);
  GstDataQueueItem *item = NULL;
  guint i;
  guint idx = 0;
  GList *list = NULL;
  GstBuffer *outbuffer = NULL;
  GstBuffer *inbuffer[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM] = {NULL, NULL};
  gboolean ret;

  for (list = reprocess->dynsinkpads; list != NULL; list = list->next) {
    GstCameraReprocSinkPad *dpad =
        GST_CAMERA_REPROC_SINK_PAD (list->data);

    if (!gst_data_queue_pop (dpad->buffers, &item))
      continue;

    inbuffer[idx++] = GST_BUFFER_CAST (g_steal_pointer(&(item->object)));
    item->destroy (item);
  }

  if (idx > 0) {
    outbuffer = gst_camera_image_reproc_create_output_buffer (reprocess,
        inbuffer[0]);
    if (outbuffer == NULL) {
      GST_ERROR ("Failed to create output buffer.");
      for (i=0; i<idx; i++)
        gst_buffer_unref (inbuffer[i]);
      return;
    }

    ret = gst_camera_image_reproc_context_process (reprocess->context, idx,
        inbuffer, outbuffer);

    if (!ret) {
      GST_ERROR_OBJECT (reprocess, "Failed to send request to process.");
      for (i=0; i<idx; i++)
        gst_buffer_unref (inbuffer[i]);
      gst_buffer_unref (outbuffer);
    }
  } else {
    GST_DEBUG_OBJECT (reprocess, "Failed, only get %d input buffer.", idx);
  }
}

static gboolean
gst_camera_image_reproc_start_worker_task (GstCameraImageReproc * reprocess)
{
  GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);

  if (reprocess->active) {
    GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);
    return TRUE;
  }

  reprocess->worktask = gst_task_new (
      gst_camera_image_reproc_worker_task, reprocess, NULL);
  gst_task_set_lock (reprocess->worktask, &reprocess->worklock);

  GST_INFO_OBJECT (reprocess, "Created task %p", reprocess->worktask);

  reprocess->active = TRUE;
  GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

  if (!gst_task_start (reprocess->worktask)) {
    reprocess->active = FALSE;
    GST_ERROR_OBJECT (reprocess, "Failed to start worker task!");

    return FALSE;
  }

  GST_INFO_OBJECT (reprocess, "Started task %p", reprocess->worktask);

  return TRUE;
}

static void
gst_camera_image_reproc_event_callback (guint event, gpointer userdata)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (userdata);

  switch (event) {
    case EVENT_SERVICE_DIED:
      GST_ERROR_OBJECT (reprocess, "Service has died!");
      break;
    case EVENT_CAMERA_ERROR:
      GST_ERROR_OBJECT (reprocess, "Encountered an un-recoverable error!");
      break;
    case EVENT_FRAME_ERROR:
      GST_WARNING_OBJECT (reprocess, "Encountered frame drop!");
      break;
    case EVENT_METADATA_ERROR:
      GST_WARNING_OBJECT (reprocess, "Encountered metadata drop error!");
      break;
    default:
      GST_WARNING_OBJECT (reprocess, "Unknown module event.");
      break;
  }
}

static gboolean
gst_camera_image_reproc_stop_worker_task (GstCameraImageReproc * reprocess)
{
  GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);

  if (!reprocess->active) {
    GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);
    return TRUE;
  }

  GST_INFO_OBJECT (reprocess, "Stopping task %p", reprocess->worktask);

  if (!gst_task_stop (reprocess->worktask))
    GST_WARNING_OBJECT (reprocess, "Failed to stop worker task!");

  reprocess->active = FALSE;
  GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

  if (!gst_task_join (reprocess->worktask)) {
    GST_ERROR_OBJECT (reprocess, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (reprocess, "Removing task %p", reprocess->worktask);

  gst_object_unref (reprocess->worktask);
  reprocess->worktask = NULL;

  return TRUE;
}

static GstCaps *
gst_camera_reproc_sink_pad_getcaps (GstCameraImageReproc * reprocess,
    GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = NULL, *intersect = NULL;

  if (!(caps = gst_pad_get_current_caps (pad)))
    caps = gst_pad_get_pad_template_caps (pad);

  GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);
    intersect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersect;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_camera_reproc_sink_pad_setcaps (GstCameraImageReproc * reprocess,
    GstPad * pad, GstCaps * caps)
{
  GstCaps *srccaps = NULL, *tmplcaps = NULL, *intersect = NULL;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

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

  // Unref previouly created pool
  if (reprocess->outpool) {
    gst_buffer_pool_set_active (reprocess->outpool, FALSE);
    gst_object_unref (reprocess->outpool);
  }

  srccaps = gst_pad_get_allowed_caps (GST_PAD (reprocess->srcpad));
  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  // Creat a new output buffer pool
  reprocess->outpool = gst_camera_image_reproc_create_buffer_pool (
      reprocess, srccaps);
  if (!reprocess->outpool) {
    GST_ERROR_OBJECT (reprocess, "Failed to create output pool!");
    return FALSE;
  }

  // Activate the pool
  if (!gst_buffer_pool_is_active (reprocess->outpool) &&
      !gst_buffer_pool_set_active (reprocess->outpool, TRUE)) {
    GST_ERROR_OBJECT (reprocess, "Failed to activate output buffer pool!");
    gst_object_unref (reprocess->outpool);
    return FALSE;
  }

  // Wait for pending buffers to be processed before sending new caps.
  GST_CAMERA_IMAGE_REPROC_PAD_WAIT_IDLE (
      GST_CAMERA_REPROC_SINK_PAD (pad));
  GST_CAMERA_IMAGE_REPROC_PAD_WAIT_IDLE (reprocess->srcpad);

  GST_DEBUG_OBJECT (pad, "Pushing new caps %" GST_PTR_FORMAT, srccaps);
  return gst_pad_push_event (GST_PAD (reprocess->srcpad),
      gst_event_new_caps (srccaps));
}

static gboolean
gst_camera_reproc_sink_main_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (parent);

  GST_TRACE_OBJECT (reprocess, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_camera_reproc_sink_pad_setcaps (reprocess,
          pad, caps);

      gst_event_unref (event);
      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstCameraReprocSrcPad *srcpad = reprocess->srcpad;
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      GST_CAMERA_REPROC_SRC_LOCK (srcpad);

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
        GST_CAMERA_REPROC_SRC_UNLOCK (srcpad);
        return FALSE;
      }

      gst_event_unref (event);
      event = gst_event_new_segment (&(srcpad)->segment);

      GST_CAMERA_REPROC_SRC_UNLOCK (srcpad);

      return gst_pad_push_event (GST_PAD (srcpad), event);
    }
    case GST_EVENT_FLUSH_START:
      gst_data_queue_set_flushing (
          GST_CAMERA_REPROC_SINK_PAD (pad)->buffers, TRUE);
      gst_data_queue_flush (
          GST_CAMERA_REPROC_SINK_PAD (pad)->buffers);

      gst_camera_image_reproc_stop_worker_task (reprocess);

      return gst_pad_push_event (pad, event);
    case GST_EVENT_FLUSH_STOP:
    {
      GList *list = NULL;

      GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);
      for (list = GST_ELEMENT (reprocess)->sinkpads; list != NULL;
          list = list->next) {
        GstCameraReprocSinkPad *sinkpad =
            GST_CAMERA_REPROC_SINK_PAD (list->data);

        gst_segment_init (&(sinkpad)->segment, GST_FORMAT_UNDEFINED);
      }
      GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

      gst_segment_init (&(reprocess->srcpad)->segment,
          GST_FORMAT_UNDEFINED);

      gst_data_queue_set_flushing (
          GST_CAMERA_REPROC_SINK_PAD (pad)->buffers, FALSE);
      gst_camera_image_reproc_start_worker_task (reprocess);

      return gst_pad_push_event (pad, event);
    }
    case GST_EVENT_EOS:
      GST_CAMERA_IMAGE_REPROC_PAD_WAIT_IDLE (
          GST_CAMERA_REPROC_SINK_PAD (pad));
      GST_CAMERA_IMAGE_REPROC_PAD_WAIT_IDLE (reprocess->srcpad);

      return gst_pad_push_event (GST_PAD (reprocess->srcpad), event);
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_camera_reproc_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (parent);

  GST_TRACE_OBJECT (reprocess, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL, *tmplcaps = NULL, *intersect = NULL;

      gst_event_parse_caps (event, &caps);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

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

      return TRUE;
    }
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_GAP:
    case GST_EVENT_STREAM_START:
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_camera_reproc_sink_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (parent);

  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_camera_reproc_sink_pad_getcaps (reprocess, pad,
          filter);

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
gst_camera_reproc_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstCameraReprocSinkPad *sinkpad =
      GST_CAMERA_REPROC_SINK_PAD_CAST (pad);
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (parent);
  GstDataQueueItem *item = NULL;

  if (!gst_pad_has_current_caps (GST_PAD (reprocess->srcpad))) {
    if (GST_PAD_IS_FLUSHING (reprocess->srcpad)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_FLUSHING;
    }

    GST_ELEMENT_ERROR (reprocess, STREAM, DECODE, ("No caps set!"), (NULL));
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (sinkpad, "Received %" GST_PTR_FORMAT, buffer);

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

static GstPad*
gst_camera_image_reproc_request_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * reqname, const GstCaps * caps)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);

  if (reqname && sscanf (reqname, "sink_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= reprocess->nextidx) ?
        index + 1 : reprocess->nextidx;
  } else {
    index = reprocess->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  name = g_strdup_printf ("sink_%u", index);

  pad = g_object_new (GST_TYPE_CAMERA_REPROC_SINK_PAD, "name", name,
      "direction", templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (reprocess, "Failed to create sink pad!");
    GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

    return NULL;
  }

  if (g_list_length (reprocess->dynsinkpads) == 0)
    gst_pad_set_event_function (pad,
        GST_DEBUG_FUNCPTR (gst_camera_reproc_sink_main_pad_event));
  else
    gst_pad_set_event_function (pad,
        GST_DEBUG_FUNCPTR (gst_camera_reproc_sink_pad_event));

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_camera_reproc_sink_pad_query));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_camera_reproc_sink_pad_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (reprocess, "Failed to add sink pad!");
    gst_object_unref (pad);
    GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

    return NULL;
  }

  GST_CAMERA_REPROC_SINK_PAD_CAST (pad)->buffers_limit =
      reprocess->queue_size;
  reprocess->dynsinkpads = g_list_append (reprocess->dynsinkpads, pad);
  reprocess->nextidx = nextindex;

  GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

  GST_DEBUG_OBJECT (reprocess, "Created pad: %s", GST_PAD_NAME (pad));

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  return pad;
}

static void
gst_camera_image_reproc_release_pad (GstElement * element, GstPad * pad)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (element);

  GST_DEBUG_OBJECT (reprocess, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);
  reprocess->dynsinkpads = g_list_remove (reprocess->dynsinkpads, pad);
  GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_camera_image_reproc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (element);
  GstStateChangeReturn state_ret = GST_STATE_CHANGE_SUCCESS;
  GList *list = NULL;
  gboolean ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      reprocess->context = gst_camera_image_reproc_context_new ();
      if (reprocess->context == NULL) {
        GST_ERROR_OBJECT (reprocess, "Failed to new context.");
        return GST_STATE_CHANGE_FAILURE;
      }

      ret = gst_camera_image_reproc_context_connect (reprocess->context,
          gst_camera_image_reproc_event_callback, reprocess);

      if (!ret) {
        GST_ERROR_OBJECT (reprocess, "Failed to connect.");
        gst_camera_image_reproc_context_free (reprocess->context);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      for (list = reprocess->dynsinkpads; list != NULL; list = list->next) {
        GstCameraReprocSinkPad *dpad =
            GST_CAMERA_REPROC_SINK_PAD (list->data);

        gst_data_queue_set_flushing (dpad->buffers, FALSE);
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      ret = gst_camera_image_reproc_set_format (reprocess);

      if (!ret){
        GST_ERROR_OBJECT (reprocess, "Failed to set format.");
        return GST_STATE_CHANGE_FAILURE;
      }

      gst_camera_image_reproc_start_worker_task (reprocess);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      for (list = reprocess->dynsinkpads; list != NULL; list = list->next) {
        GstCameraReprocSinkPad *dpad =
            GST_CAMERA_REPROC_SINK_PAD (list->data);

        gst_data_queue_set_flushing (dpad->buffers, TRUE);
        gst_data_queue_flush (dpad->buffers);
      }
      break;
    default:
      break;
  }

  state_ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_camera_image_reproc_stop_worker_task (reprocess);

      ret = gst_camera_image_reproc_context_destroy (reprocess->context);
      if (!ret) {
          GST_DEBUG_OBJECT (reprocess, "Failed to destroy camera reprocess"
              " module session.");
      }
      break;
    default:
      break;
  }

  return state_ret;
}

static void
gst_camera_image_reproc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (object);
  GList *list = NULL;

  switch (prop_id) {
    case PROP_QUEUE_SIZE:
      reprocess->queue_size = g_value_get_uint (value);
      reprocess->srcpad->buffers_limit = reprocess->queue_size;
      for (list = GST_ELEMENT (reprocess)->sinkpads; list != NULL;
          list = list->next) {
        GstCameraReprocSinkPad *sinkpad =
            GST_CAMERA_REPROC_SINK_PAD (list->data);

        sinkpad->buffers_limit = reprocess->queue_size;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camera_image_reproc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (object);

  switch (prop_id) {
    case PROP_QUEUE_SIZE:
      g_value_set_uint (value, reprocess->queue_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camera_image_reproc_finalize (GObject * object)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (object);
  gboolean ret;

  ret = gst_camera_image_reproc_context_disconnect (reprocess->context);

  if (!ret) {
    GST_ERROR_OBJECT (reprocess, "Failed to disconnect.");
  }

  if (reprocess->outpool != NULL) {
    gst_buffer_pool_set_active (reprocess->outpool, FALSE);
    gst_object_unref (reprocess->outpool);
  }

  if (reprocess->context) {
    gst_camera_image_reproc_context_free (reprocess->context);
    reprocess->context = NULL;
  }

  g_rec_mutex_clear (&reprocess->worklock);

  g_mutex_clear (&reprocess->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (reprocess));
}

static void
gst_camera_image_reproc_class_init (GstCameraImageReprocClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_camera_image_reproc_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_camera_image_reproc_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_camera_image_reproc_finalize);

  gst_element_class_add_pad_template (element, gst_camera_reproc_sink_template ());
  gst_element_class_add_pad_template (element, gst_camera_reproc_src_template ());

  g_object_class_install_property (object, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size", "Input and output queue size",
          "Set the size of the input and output queues.",
          3, G_MAXUINT, DEFAULT_PROP_QUEUE_SIZE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (element, "Camera Image Reprocess",
      "Filter/Converter", "Reprocess images via camera module", "QTI");

  element->request_new_pad =
    GST_DEBUG_FUNCPTR (gst_camera_image_reproc_request_pad);
  element->release_pad =
    GST_DEBUG_FUNCPTR (gst_camera_image_reproc_release_pad);
  element->change_state =
    GST_DEBUG_FUNCPTR (gst_camera_image_reproc_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_camera_image_reproc_debug,
      "qticamimgreproc", 0, "QTI Camera Image Reprocess");
}

static void
gst_camera_image_reproc_init (GstCameraImageReproc * reprocess)
{
  GstElementClass *eclass = GST_ELEMENT_GET_CLASS (GST_ELEMENT(reprocess));
  GstPadTemplate *template = NULL;

  g_mutex_init (&reprocess->lock);

  reprocess->nextidx = 0;
  reprocess->dynsinkpads = NULL;

  reprocess->active = FALSE;
  reprocess->worktask = NULL;

  g_rec_mutex_init (&reprocess->worklock);

  reprocess->outpool = NULL;

  reprocess->queue_size = DEFAULT_PROP_QUEUE_SIZE;

  template = gst_element_class_get_pad_template (eclass, "src");
  reprocess->srcpad = g_object_new (GST_TYPE_CAMERA_REPROC_SRC_PAD,
      "name", "src","direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_query_function (GST_PAD (reprocess->srcpad),
      GST_DEBUG_FUNCPTR (gst_camera_reproc_src_pad_query));
  gst_pad_set_activatemode_function (GST_PAD (reprocess->srcpad),
      GST_DEBUG_FUNCPTR (gst_camera_reproc_src_pad_activate_mode));
  gst_element_add_pad (GST_ELEMENT (reprocess),
      GST_PAD (reprocess->srcpad));
  reprocess->srcpad->buffers_limit = reprocess->queue_size;

  GST_INFO_OBJECT (reprocess, "Camera reprocess plugin instance inited.");
}

static GObject *
gst_camera_image_reproc_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (proxy);
  GObject *g_object = NULL;

  GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);

  g_object = G_OBJECT (g_list_nth_data (
      GST_ELEMENT_CAST (reprocess)->sinkpads, index));

  if (g_object != NULL)
    g_object_ref (g_object);

  GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

  return g_object;
}

static guint
gst_camera_image_reproc_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstCameraImageReproc *reprocess = GST_CAMERA_IMAGE_REPROC (proxy);
  guint count = 0;

  GST_CAMERA_IMAGE_REPROC_LOCK (reprocess);
  count = GST_ELEMENT_CAST (reprocess)->numsinkpads;
  GST_CAMERA_IMAGE_REPROC_UNLOCK (reprocess);

  return count;
}

static void
gst_camera_image_reproc_child_proxy_init (gpointer g_iface, gpointer data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index =
      gst_camera_image_reproc_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_camera_image_reproc_child_proxy_get_children_count;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qticamimgreproc", GST_RANK_PRIMARY,
      GST_TYPE_CAMERA_IMAGE_REPROC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qticamimgreproc,
    "Reprocess images via camera module",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
