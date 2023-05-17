/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "videosplit.h"

#include <stdio.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#include "videosplitpads.h"


#define GST_CAT_DEFAULT gst_video_split_debug
GST_DEBUG_CATEGORY (gst_video_split_debug);

// Forward declaration of the child proxy function.
static void gst_vsplit_child_proxy_init (gpointer g_iface, gpointer data);

#define gst_video_split_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVideoSplit, gst_video_split, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY, gst_vsplit_child_proxy_init));

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_VIDEO_FORMATS \
  "{ NV12, NV21, UYVY, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8 }"

static GType gst_vsplit_request_get_type(void);
#define GST_TYPE_VSPLIT_REQUEST  (gst_vsplit_request_get_type())
#define GST_VSPLIT_REQUEST(obj)  ((GstVSplitRequest *) obj)

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_video_split_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
    );

static GstStaticPadTemplate gst_video_split_src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
    );

typedef struct _GstVSplitRequest GstVSplitRequest;

struct _GstVSplitRequest {
  GstMiniObject parent;

  // Request ID.
  gpointer      id;

  // Input frame submitted with provided ID.
  GstVideoFrame *inframe;
  // List with video frame arrays for each output.
  GPtrArray     *outframes;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstVSplitRequest, gst_vsplit_request);

static void
gst_vsplit_request_free (GstVSplitRequest * request)
{
  GstBuffer *buffer = NULL;
  guint idx = 0, num = 0;

  for (idx = 0; idx < request->outframes->len; idx++) {
    GArray *vframes = g_ptr_array_index (request->outframes, idx);

    if (vframes == NULL)
      continue;

    for (num = 0; num < vframes->len; num++) {
      GstVideoFrame *vframe = &(g_array_index (vframes, GstVideoFrame, num));

      if ((buffer = vframe->buffer) != NULL) {
        gst_video_frame_unmap (vframe);
        gst_buffer_unref (buffer);
      }
    }

    g_array_free (vframes, TRUE);
  }

  if ((buffer = request->inframe->buffer) != NULL) {
    gst_video_frame_unmap (request->inframe);
    gst_buffer_unref (buffer);
  }

  g_ptr_array_free (request->outframes, TRUE);
  g_free (request->inframe);
  g_free (request);
}

static GstVSplitRequest *
gst_vsplit_request_new (guint n_outputs)
{
  GstVSplitRequest *request = g_new (GstVSplitRequest, 1);
  guint idx = 0;

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_VSPLIT_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vsplit_request_free);

  request->id = NULL;
  request->inframe = g_new0 (GstVideoFrame, 1);

  request->outframes = g_ptr_array_sized_new (n_outputs);
  g_ptr_array_set_size (request->outframes, n_outputs);

  for (idx = 0; idx < n_outputs; idx++)
    g_ptr_array_index (request->outframes, idx) = NULL;

  request->time = GST_CLOCK_TIME_NONE;

  return request;
}

static inline void
gst_vsplit_request_release (GstVSplitRequest * request)
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

static inline void
gst_data_queue_push_object (GstDataQueue * queue, GstMiniObject * object)
{
  GstDataQueueItem *item = g_slice_new0 (GstDataQueueItem);

  item->object = object;
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the mini object into the queue or free it on failure.
  if (!gst_data_queue_push (queue, item))
    item->destroy (item);
}

static gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

static gboolean
gst_video_split_acquire_video_frame (GstVideoSplitSrcPad * srcpad,
    const GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  GstBufferPool *pool = NULL;
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;

  inbuffer = inframe->buffer;
  pool = srcpad->pool;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (srcpad, "Failed to activate buffer pool!");
    return FALSE;
  }

  // Retrieve new output buffer from the pool.
  if (gst_buffer_pool_acquire_buffer (pool, &outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (srcpad, "Failed to acquire buffer!");
    return FALSE;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  if (!gst_video_frame_map (outframe, srcpad->info, outbuffer,
          GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (srcpad, "Failed to map buffer!");
    return FALSE;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (srcpad, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  return TRUE;
}

static gboolean
gst_video_split_srcpad_push_event (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstEvent *event = GST_EVENT (userdata);

  GST_TRACE_OBJECT (vsplit, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_ref (event));
}

static gboolean
gst_video_split_srcpad_push_buffer (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);
  GstVSplitRequest *request = GST_VSPLIT_REQUEST (userdata);
  GArray *vframes = NULL;
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;
  guint idx = 0;

  GST_OBJECT_LOCK (vsplit);
  idx = g_list_index (element->srcpads, pad);
  GST_OBJECT_UNLOCK (vsplit);

  inbuffer = request->inframe->buffer;
  vframes = g_ptr_array_index (request->outframes, idx);

  if (srcpad->passthrough && (vframes == NULL)) {
    // When in passthrough and there are no output frames submit same buffer.
    outbuffer = gst_buffer_ref (inbuffer);

    gst_data_queue_push_object (srcpad->buffers, GST_MINI_OBJECT (outbuffer));
    return TRUE;
  } else if (!srcpad->passthrough && (vframes == NULL)) {
    // When not in passthrough and there are no output frames submit GAP buffer.
    outbuffer = gst_buffer_new ();

    // Copy the flags and timestamps from the input buffer.
    gst_buffer_copy_into (outbuffer, inbuffer,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    // Mark this buffer as GAP.
    GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_GAP);

    gst_data_queue_push_object (srcpad->buffers, GST_MINI_OBJECT (outbuffer));
    return TRUE;
  }

  // Unmap and submit the processed output buffers.
  for (idx = 0; idx < vframes->len; idx++) {
    GstVideoFrame *vframe = &(g_array_index (vframes, GstVideoFrame, idx));

    outbuffer = vframe->buffer;

  #ifdef HAVE_LINUX_DMA_BUF_H
    if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
      struct dma_buf_sync bufsync;
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

      bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

      if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
        GST_WARNING_OBJECT (pad, "DMA IOCTL SYNC END failed!");
    }
  #endif // HAVE_LINUX_DMA_BUF_H

    gst_video_frame_unmap (vframe);
    vframe->buffer = NULL;

    // Mark the first buffer in the bundle of frames that belong together.
    if ((srcpad->mode == GST_VSPLIT_MODE_ROI_BATCH) && (idx == 0))
      GST_BUFFER_FLAG_SET (outbuffer, GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE);

    gst_data_queue_push_object (srcpad->buffers, GST_MINI_OBJECT (outbuffer));
  }

  return TRUE;
}

static void
gst_video_split_srcpad_worker_task (gpointer userdata)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting %" GST_PTR_FORMAT, buffer);

    // Adjust the source pad segment position.
    srcpad->segment.position = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);

    gst_pad_push (GST_PAD (srcpad), buffer);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

static void
gst_video_split_worker_task (gpointer userdata)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (userdata);
  GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad);
  GstDataQueueItem *item = NULL;
  gboolean success = FALSE;

  if (gst_data_queue_pop (sinkpad->requests, &item)) {
    GstVSplitRequest *request = NULL;

    request = GST_VSPLIT_REQUEST (gst_mini_object_ref (item->object));
    item->destroy (item);

    if (request->id != NULL) {
      gpointer id = request->id;
      GST_TRACE_OBJECT (vsplit, "Waiting request %p", id);

#ifdef USE_C2D_CONVERTER
      if (!gst_c2d_video_converter_wait_request (vsplit->c2dconvert, id))
        GST_WARNING_OBJECT (vsplit, "Waiting request %p failed!", id);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
      if (!gst_gles_video_converter_wait_request (vsplit->glesconvert, id))
        GST_WARNING_OBJECT (vsplit, "Waiting request %p failed!", id);
#endif // USE_GLES_CONVERTER
    }

    // Get time difference between current time and start.
    request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

    GST_LOG_OBJECT (vsplit, "Conversion took %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (request->time),
        (GST_TIME_AS_USECONDS (request->time) % 1000));

    success = gst_element_foreach_src_pad (GST_ELEMENT_CAST (vsplit),
        gst_video_split_srcpad_push_buffer, request);

    // Free the memory allocated by the internal request structure.
    gst_vsplit_request_release (request);

    if (!success)
      GST_WARNING_OBJECT (vsplit, "Failed to push output buffers!");

  } else {
    GST_INFO_OBJECT (vsplit, "Pause worker task!");
    gst_task_pause (vsplit->worktask);
  }
}

static gboolean
gst_video_split_start_worker_task (GstVideoSplit * vsplit)
{
  if (vsplit->worktask != NULL)
    return TRUE;

  vsplit->worktask =
      gst_task_new (gst_video_split_worker_task, vsplit, NULL);
  GST_INFO_OBJECT (vsplit, "Created task %p", vsplit->worktask);

  gst_task_set_lock (vsplit->worktask, &vsplit->worklock);

  if (!gst_task_start (vsplit->worktask)) {
    GST_ERROR_OBJECT (vsplit, "Failed to start worker task!");
    return FALSE;
  }

  // Disable requests queue in flushing state to enable normal work.
  gst_data_queue_set_flushing (
      GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad)->requests, FALSE);
  return TRUE;
}

static gboolean
gst_video_split_stop_worker_task (GstVideoSplit * vsplit)
{
  if (NULL == vsplit->worktask)
    return TRUE;

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (
      GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad)->requests, TRUE);

  if (!gst_task_stop (vsplit->worktask))
    GST_WARNING_OBJECT (vsplit, "Failed to stop worker task!");

  // Make sure task is not running.
  g_rec_mutex_lock (&vsplit->worklock);
  g_rec_mutex_unlock (&vsplit->worklock);

  if (!gst_task_join (vsplit->worktask)) {
    GST_ERROR_OBJECT (vsplit, "Failed to join worker task!");
    return FALSE;
  }

  // Flush converter and requests queue.
#ifdef USE_C2D_CONVERTER
  gst_c2d_video_converter_flush (vsplit->c2dconvert);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  gst_gles_video_converter_flush (vsplit->glesconvert);
#endif // USE_GLES_CONVERTER

  gst_data_queue_flush (GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad)->requests);

  GST_INFO_OBJECT (vsplit, "Removing task %p", vsplit->worktask);

  gst_object_unref (vsplit->worktask);
  vsplit->worktask = NULL;

  return TRUE;
}

static void
gst_video_split_update_params (GstVideoSplit * vsplit, guint index,
    GstVideoFrame * inframe, GstVideoFrame * outframe,
    GstVideoRegionOfInterestMeta * roimeta)
{
  GstStructure *opts = NULL;
  GValue srcrects = G_VALUE_INIT, dstrects = G_VALUE_INIT;
  GValue entry = G_VALUE_INIT, value = G_VALUE_INIT;
  GstVideoRectangle inrect = {0,0,0,0}, outrect = {0,0,0,0};
  gint par_n = 0, par_d = 0, sar_n = 0, sar_d = 0, num = 0, den = 0;

  g_value_init (&srcrects, GST_TYPE_ARRAY);
  g_value_init (&dstrects, GST_TYPE_ARRAY);

  g_value_init (&entry, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_INT);

  // If available extract coordinates and dimensions from ROI meta.
  inrect.x = roimeta ? (gint) roimeta->x : 0;
  inrect.y = roimeta ? (gint) roimeta->y : 0;
  inrect.w = roimeta ? (gint) roimeta->w : GST_VIDEO_FRAME_WIDTH (inframe);
  inrect.h = roimeta ? (gint) roimeta->h : GST_VIDEO_FRAME_HEIGHT (inframe);

  g_value_set_int (&value, inrect.x);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, inrect.y);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, inrect.w);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, inrect.h);
  gst_value_array_append_value (&entry, &value);

  gst_value_array_append_value (&srcrects, &entry);
  g_value_reset (&entry);

  // Fill output PAR (Pixel Aspect Ratio), will be used to calculations.
  par_n = GST_VIDEO_INFO_PAR_N (&(outframe)->info);
  par_d = GST_VIDEO_INFO_PAR_D (&(outframe)->info);

  // Calculate input SAR (Source Aspect Ratio) value.
  if (!gst_util_fraction_multiply (inrect.w, inrect.h, par_n, par_d,
          &sar_n, &sar_d))
    sar_n = sar_d = 1;

  outrect.x = 0;
  outrect.y = 0;
  outrect.w = GST_VIDEO_FRAME_WIDTH (outframe);
  outrect.h = GST_VIDEO_FRAME_HEIGHT (outframe);

  // Adjust destination dimensions to preserve SAR.
  gst_util_fraction_multiply (sar_n, sar_d, par_d, par_n, &num, &den);

  if (num > den) {
    outrect.h = gst_util_uint64_scale_int (outrect.w, den, num);

    // Clip height if outside the limit and recalculate width.
    if (outrect.h > GST_VIDEO_FRAME_HEIGHT (outframe)) {
      outrect.h = GST_VIDEO_FRAME_HEIGHT (outframe);
      outrect.w = gst_util_uint64_scale_int (outrect.h, num, den);
      outrect.x = (GST_VIDEO_FRAME_WIDTH (outframe) - outrect.w) / 2;
    }

    outrect.y = (GST_VIDEO_FRAME_HEIGHT (outframe) - outrect.h) / 2;
  } else if (num < den) {
    outrect.w = gst_util_uint64_scale_int (outrect.h, num, den);

    // Clip width if outside the limit and recalculate height.
    if (outrect.w > GST_VIDEO_FRAME_WIDTH (outframe)) {
      outrect.w = GST_VIDEO_FRAME_WIDTH (outframe);
      outrect.h = gst_util_uint64_scale_int (outrect.w, den, num);
      outrect.y = (GST_VIDEO_FRAME_HEIGHT (outframe) - outrect.h) / 2;
    }

    outrect.x = (GST_VIDEO_FRAME_WIDTH (outframe) - outrect.w) / 2;
  }

  g_value_set_int (&value, outrect.x);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, outrect.y);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, outrect.w);
  gst_value_array_append_value (&entry, &value);
  g_value_set_int (&value, outrect.h);
  gst_value_array_append_value (&entry, &value);

  gst_value_array_append_value (&dstrects, &entry);
  g_value_reset (&entry);

  // Add ROI meta with the actual part of the buffer filled with image data.
  gst_buffer_add_video_region_of_interest_meta (outframe->buffer,
      "ImageRegion", outrect.x, outrect.y, outrect.w, outrect.h);

  GST_TRACE_OBJECT (vsplit, "Rectangles [%u] SAR[%d/%d]: [%d %d %d %d]"
      " -> [%d %d %d %d]", index, sar_n, sar_d, inrect.x, inrect.y, inrect.w,
      inrect.h, outrect.x, outrect.y, outrect.w, outrect.h);

#ifdef USE_C2D_CONVERTER
  opts = gst_structure_new ("options",
      GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad)->isubwc,
      NULL);

  gst_structure_set_value (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &srcrects);
  gst_structure_set_value (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &dstrects);

  gst_c2d_video_converter_set_input_opts (vsplit->c2dconvert, index, opts);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  opts = gst_structure_new ("options",
      GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad)->isubwc,
      NULL);

  gst_structure_set_value (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &srcrects);
  gst_structure_set_value (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &dstrects);

  gst_gles_video_converter_set_input_opts (vsplit->glesconvert, index, opts);
#endif // USE_GLES_CONVERTER

  g_value_unset (&value);
  g_value_unset (&entry);

  g_value_unset (&dstrects);
  g_value_unset (&srcrects);
}

static gboolean
gst_video_split_populate_compositions (GstVideoSplit * vsplit,
    GstVideoFrame * inframe, GPtrArray * vframes, guint * n_compositions)
{
  GList *list = NULL;
  GstVideoFrame *outframe = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  guint idx = 0, num = 0, id = 0, n_metas = 0,n_entries = 0;
  gboolean success = TRUE;

  // Fetch the number of ROI meta entries from the input buffer.
  n_metas = gst_buffer_get_n_meta (inframe->buffer,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  GST_VIDEO_SPLIT_LOCK (vsplit);

  // Fetch and prepare compositions for each of the source pads.
  for (list = vsplit->srcpads; list != NULL; list = g_list_next (list)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);
    GArray *outframes = NULL;

    // Skip this pad as there there is no actual work to be done.
    if (srcpad->passthrough)
      continue;

    // Skip this pad as there is no corresponding ROI meta in single ROI mode.
    if ((srcpad->mode == GST_VSPLIT_MODE_ROI_SINGLE) && (num++ >= n_metas))
      continue;

    // Skip this pad as there is no ROI meta in batched ROI mode.
    if ((srcpad->mode == GST_VSPLIT_MODE_ROI_BATCH) && (n_metas == 0))
      continue;

    n_entries = (srcpad->mode == GST_VSPLIT_MODE_ROI_BATCH) ? n_metas : 1;
    outframes = g_array_sized_new (FALSE, TRUE, sizeof (GstVideoFrame), n_entries);
    g_array_set_size (outframes, n_entries);

    idx = g_list_index (vsplit->srcpads, srcpad);
    g_ptr_array_index (vframes, idx) = outframes;

    // Aquire buffer for each frame and update the converter parameters.
    for (idx = 0; idx < outframes->len; idx++, id++) {
      outframe = &(g_array_index (outframes, GstVideoFrame, idx));

      success = gst_video_split_acquire_video_frame (srcpad, inframe, outframe);
      if (!success) {
        GST_ERROR_OBJECT (srcpad, "Failed to acquire video frame!");
        break;
      }

      // Depending on the mode a different ROI meta is used or none at all.
      if (srcpad->mode == GST_VSPLIT_MODE_ROI_SINGLE)
        roimeta = gst_buffer_get_video_region_of_interest_meta_id (
            inframe->buffer, (num -1));
      else if (srcpad->mode == GST_VSPLIT_MODE_ROI_BATCH)
        roimeta = gst_buffer_get_video_region_of_interest_meta_id (
            inframe->buffer, idx);

      // Update source/destination rectangles and output buffers flags/meta.
      gst_video_split_update_params (vsplit, id, inframe, outframe, roimeta);

      // Reset ROI metadata pointer.
      roimeta = NULL;
    }

    // Increase the total number of compositions.
    *n_compositions += n_entries;
  }

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  return success;
}

static GstFlowReturn
gst_video_split_sinkpad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * inbuffer)
{
  GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD (pad);
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (parent);
  GstVSplitRequest *request = NULL;
  guint idx = 0, n_entries = 0, n_compositions = 0;
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, inbuffer);

  GST_VIDEO_SPLIT_LOCK (vsplit);
  n_entries = g_list_length (vsplit->srcpads);
  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  // Allocate request structure with shared input frame across all compositions.
  request = gst_vsplit_request_new (n_entries);

  success = gst_video_frame_map (request->inframe, sinkpad->info, inbuffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to map input buffer!");

    gst_vsplit_request_release (request);
    gst_buffer_unref (inbuffer);

    return GST_FLOW_ERROR;
  }

  // Populate total number of compositions and their output frames.
  success = gst_video_split_populate_compositions (vsplit, request->inframe,
      request->outframes, &n_compositions);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to populate compositions!");
    gst_vsplit_request_release (request);
    return GST_FLOW_ERROR;
  }

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

#ifdef USE_C2D_CONVERTER
  if (n_compositions != 0) {
    GstC2dComposition *compositions = g_new (GstC2dComposition, n_compositions);
    guint num = 0, id = 0;

    for (idx = 0; idx < request->outframes->len; idx++) {
      GArray *vframes = g_ptr_array_index (request->outframes, idx);

      for (num = 0; (vframes != NULL) && (num < vframes->len); num++, id++) {
        compositions[id].inframes = request->inframe;
        compositions[id].n_inputs = 1;
        compositions[id].outframe = &(g_array_index (vframes, GstVideoFrame, num));
      }
    }

    request->id = gst_c2d_video_converter_submit_request (vsplit->c2dconvert,
        compositions, n_compositions);

    g_free (compositions);
  }
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  if (n_compositions != 0) {
    GstGlesComposition *compositions = g_new (GstGlesComposition, n_compositions);
    guint num = 0, id = 0;

    for (idx = 0; idx < request->outframes->len; idx++) {
      GArray *vframes = g_ptr_array_index (request->outframes, idx);

      for (num = 0; (vframes != NULL) && (num < vframes->len); num++, id++) {
        compositions[id].inframes = request->inframe;
        compositions[id].n_inputs = 1;
        compositions[id].outframe = &(g_array_index (vframes, GstVideoFrame, num));
      }
    }

    request->id = gst_gles_video_converter_submit_request (vsplit->glesconvert,
        compositions, n_compositions);

    g_free (compositions);
  }
#endif // USE_GLES_CONVERTER

  if ((n_compositions != 0) && (request->id == NULL)) {
    GST_ERROR_OBJECT (pad, "Failed to submit request(s)!");
    gst_vsplit_request_release (request);
    return GST_FLOW_ERROR;
  }

  gst_data_queue_push_object (sinkpad->requests, GST_MINI_OBJECT (request));
  return GST_FLOW_OK;
}

static GstCaps *
gst_video_split_sinkpad_getcaps (GstPad * pad, GstCaps * filter)
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
gst_video_split_sinkpad_acceptcaps (GstPad * pad, GstCaps * caps)
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
gst_video_split_sinkpad_setcaps (GstVideoSplit * vsplit, GstPad * pad,
    GstCaps * caps)
{
  GstStructure *opts = NULL;
  GList *list = NULL;
  GstVideoInfo info = { 0, };

  GST_DEBUG_OBJECT (vsplit, "Setting caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vsplit, "Failed to extract input video info from caps!");
    return FALSE;
  }

  // Fill video info structure from the negotiated caps.
  if (GST_VIDEO_SPLIT_SINKPAD (pad)->info != NULL)
    gst_video_info_free (GST_VIDEO_SPLIT_SINKPAD (pad)->info);

  GST_VIDEO_SPLIT_SINKPAD (pad)->info = gst_video_info_copy (&info);
  GST_VIDEO_SPLIT_SINKPAD (pad)->isubwc = gst_caps_has_compression (caps, "ubwc");

  GST_VIDEO_SPLIT_LOCK (vsplit);

  for (list = vsplit->srcpads; list != NULL; list = g_list_next (list)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);

    if (!gst_video_split_srcpad_setcaps (srcpad, caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vsplit), CORE, NEGOTIATION, (NULL),
          ("Failed to set caps to %s!", GST_PAD_NAME (srcpad)));

      GST_VIDEO_SPLIT_UNLOCK (vsplit);
      return FALSE;
    }

#ifdef USE_C2D_CONVERTER
  opts = gst_structure_new ("options",
      GST_C2D_VIDEO_CONVERTER_OPT_BACKGROUND, G_TYPE_UINT, 0x00000000,
      GST_C2D_VIDEO_CONVERTER_OPT_CLEAR, G_TYPE_BOOLEAN,
          (srcpad->mode == GST_VSPLIT_MODE_NONE) ? FALSE : TRUE,
      GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          GST_VIDEO_SPLIT_SRCPAD (srcpad)->isubwc,
      NULL);

  gst_c2d_video_converter_set_output_opts (vsplit->c2dconvert,
      g_list_index (vsplit->srcpads, srcpad), opts);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  opts = gst_structure_new ("options",
      GST_GLES_VIDEO_CONVERTER_OPT_BACKGROUND, G_TYPE_UINT, 0x00000000,
      GST_GLES_VIDEO_CONVERTER_OPT_CLEAR, G_TYPE_BOOLEAN,
          (srcpad->mode == GST_VSPLIT_MODE_NONE) ? FALSE : TRUE,
      GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          GST_VIDEO_SPLIT_SRCPAD (srcpad)->isubwc,
      NULL);

  gst_gles_video_converter_set_output_opts (vsplit->glesconvert,
      g_list_index (vsplit->srcpads, srcpad), opts);
#endif // USE_GLES_CONVERTER
  }

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  return TRUE;
}

static gboolean
gst_video_split_sinkpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_video_split_sinkpad_getcaps (pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_video_split_sinkpad_acceptcaps (pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    case GST_QUERY_ALLOCATION:
    {
      GstCaps *caps = NULL;
      GstBufferPool *pool = NULL;
      GstVideoInfo info;
      guint size = 0;
      gboolean needpool = FALSE;

      // Extract caps from the query.
      gst_query_parse_allocation (query, &caps, &needpool);

      if (NULL == caps) {
        GST_ERROR_OBJECT (pad, "Failed to extract caps from query!");
        return FALSE;
      }

      if (!gst_video_info_from_caps (&info, caps)) {
        GST_ERROR_OBJECT (pad, "Failed to get video info!");
        return FALSE;
      }

      // Get the size from video info.
      size = GST_VIDEO_INFO_SIZE (&info);

      if (needpool) {
        GstStructure *structure = NULL;

        pool = gst_video_split_create_pool (pad, caps);
        structure = gst_buffer_pool_get_config (pool);

        // Set caps and size in query.
        gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

        if (!gst_buffer_pool_set_config (pool, structure)) {
          GST_ERROR_OBJECT (pad, "Failed to set buffer pool configuration!");
          gst_object_unref (pool);
          return FALSE;
        }
      }

      // If upstream does't have a pool requirement, set only size in query.
      gst_query_add_allocation_pool (query, needpool ? pool : NULL, size, 0, 0);

      if (pool != NULL)
        gst_object_unref (pool);

      gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_video_split_sinkpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (parent);
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_video_split_sinkpad_setcaps (vsplit, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD (pad);
      GList *list = NULL;
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

      GST_OBJECT_LOCK (vsplit);

      for (list = GST_ELEMENT (vsplit)->srcpads; list; list = list->next) {
        GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);
        gst_segment_copy_into (&(sinkpad)->segment, &(srcpad)->segment);
      }

      GST_OBJECT_UNLOCK (vsplit);

      event = gst_event_new_segment (&(sinkpad)->segment);

      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_STREAM_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_STOP:
    {
      GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD (pad);
      GList *list = NULL;

      GST_OBJECT_LOCK (vsplit);

      for (list = GST_ELEMENT (vsplit)->srcpads; list; list = list->next) {
        GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);
      }

      GST_OBJECT_UNLOCK (vsplit);

      gst_segment_init (&(sinkpad)->segment, GST_FORMAT_UNDEFINED);

      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    }
    case GST_EVENT_EOS:
      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_video_split_srcpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &(srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (srcpad, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_SEGMENT:
    {
      GstSegment *segment = &(srcpad)->segment;
      gint64 start = 0, stop = 0;

      start = gst_segment_to_stream_time (segment, segment->format,
          segment->start);

      stop = (segment->stop == GST_CLOCK_TIME_NONE) ? segment->duration :
          gst_segment_to_stream_time (segment, segment->format, segment->stop);

      gst_query_set_segment (query, segment->rate, segment->format, start, stop);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_video_split_srcpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_video_split_srcpad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);
  gboolean success = TRUE;

  GST_INFO_OBJECT (pad, "%s worker task", active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (srcpad->buffers, FALSE);

        success = gst_pad_start_task (pad, gst_video_split_srcpad_worker_task,
            pad, NULL);
      } else {
        gst_data_queue_set_flushing (srcpad->buffers, TRUE);
        gst_data_queue_flush (srcpad->buffers);

        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to %s worker task!",
        active ? "activate" : "deactivate");
    return FALSE;
  }

  GST_INFO_OBJECT (pad, "Worker task %s", active ? "activated" : "deactivated");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static GstPad*
gst_video_split_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_VIDEO_SPLIT_LOCK (vsplit);

  if (reqname && sscanf (reqname, "src_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= vsplit->nextidx) ? index + 1 : vsplit->nextidx;
  } else {
    index = vsplit->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  name = g_strdup_printf ("src_%u", index);

  pad = g_object_new (GST_TYPE_VIDEO_SPLIT_SRCPAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (vsplit, "Failed to create source pad!");
    return NULL;
  }

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_split_srcpad_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_split_srcpad_event));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_split_srcpad_activate_mode));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (vsplit, "Failed to add source pad!");
    gst_object_unref (pad);
    return NULL;
  }

  GST_VIDEO_SPLIT_LOCK (vsplit);

  vsplit->srcpads = g_list_append (vsplit->srcpads, pad);
  vsplit->nextidx = nextindex;

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_DEBUG_OBJECT (vsplit, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_video_split_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);

  GST_DEBUG_OBJECT (vsplit, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_VIDEO_SPLIT_LOCK (vsplit);
  vsplit->srcpads = g_list_remove (vsplit->srcpads, pad);
  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  gst_element_remove_pad (element, pad);
  GST_DEBUG_OBJECT (vsplit, "Rad has been removed");
}

static GstStateChangeReturn
gst_video_split_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_video_split_start_worker_task (vsplit)) {
        GST_ERROR_OBJECT (vsplit, "Failed to start worker task!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_video_split_stop_worker_task (vsplit)) {
        GST_ERROR_OBJECT (vsplit, "Failed to stop worker task!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_video_split_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_finalize (GObject * object)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

#ifdef USE_C2D_CONVERTER
  if (vsplit->c2dconvert != NULL)
    gst_c2d_video_converter_free (vsplit->c2dconvert);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  if (vsplit->glesconvert != NULL)
    gst_gles_video_converter_free (vsplit->glesconvert);
#endif // USE_GLES_CONVERTER

  if (vsplit->srcpads != NULL)
    g_list_free (vsplit->srcpads);

  g_rec_mutex_clear (&(vsplit)->worklock);
  g_mutex_clear (&(vsplit)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vsplit));
}

static void
gst_video_split_class_init (GstVideoSplitClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_video_split_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_video_split_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_video_split_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_split_sink_template, GST_TYPE_VIDEO_SPLIT_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_split_src_template, GST_TYPE_VIDEO_SPLIT_SRCPAD);

  gst_element_class_set_static_metadata (element,
      "Video stream splitter", "Video/Demuxer",
      "Split single video stream into multiple streams", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_video_split_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_video_split_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_video_split_change_state);

  // Initializes a new vsplit GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_video_split_debug, "qtivsplit", 0,
      "QTI Video Split");
}

static void
gst_video_split_init (GstVideoSplit * vsplit)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&(vsplit)->lock);
  g_rec_mutex_init (&vsplit->worklock);

  vsplit->nextidx = 0;
  vsplit->srcpads = NULL;

  vsplit->worktask = NULL;

#ifdef USE_C2D_CONVERTER
  vsplit->c2dconvert = gst_c2d_video_converter_new ();
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  vsplit->glesconvert = gst_gles_video_converter_new ();
#endif // USE_GLES_CONVERTER

  template = gst_static_pad_template_get (&gst_video_split_sink_template);
  vsplit->sinkpad = g_object_new (GST_TYPE_VIDEO_SPLIT_SINKPAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_chain_function (vsplit->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_chain));
  gst_pad_set_query_function (vsplit->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_query));
  gst_pad_set_event_function (vsplit->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_event));

  gst_element_add_pad (GST_ELEMENT (vsplit), vsplit->sinkpad);
}

static GObject *
gst_vsplit_child_proxy_get_child_by_index (GstChildProxy * proxy, guint index)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (proxy);
  GObject *g_object = NULL;

  GST_VIDEO_SPLIT_LOCK (vsplit);

  g_object = G_OBJECT (g_list_nth_data (vsplit->srcpads, index));

  if (g_object != NULL)
    g_object_ref (g_object);

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  return g_object;
}

static guint
gst_vsplit_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (proxy);
  guint count = 0;

  GST_VIDEO_SPLIT_LOCK (vsplit);
  count = g_list_length (vsplit->srcpads);
  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  return count;
}

static void
gst_vsplit_child_proxy_init (gpointer g_iface, gpointer data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_vsplit_child_proxy_get_child_by_index;
  iface->get_children_count = gst_vsplit_child_proxy_get_children_count;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivsplit", GST_RANK_NONE,
      GST_TYPE_VIDEO_SPLIT);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivsplit,
    "QTI video stream splitter",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
