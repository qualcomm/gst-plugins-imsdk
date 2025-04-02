/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <gst/utils/common-utils.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

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

#define DEFAULT_PROP_ENGINE_BACKEND (gst_video_converter_default_backend())

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_VIDEO_FORMATS \
  "{ NV12, NV21, UYVY, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8, NV12_Q08C }"

static GType gst_vsplit_request_get_type(void);
#define GST_TYPE_VSPLIT_REQUEST  (gst_vsplit_request_get_type())
#define GST_VSPLIT_REQUEST(obj)  ((GstVSplitRequest *) obj)

enum
{
  PROP_0,
  PROP_ENGINE_BACKEND,
};

typedef struct _GstVideoCoords GstVideoCoords;
typedef struct _GstVSplitRequest GstVSplitRequest;

struct _GstVideoCoords {
  gint x;
  gint y;
};

struct _GstVSplitRequest {
  GstMiniObject parent;

  // Composition asynchronous fence object.
  gpointer      fence;

  // Input frame submitted with provided ID.
  GstVideoFrame *inframe;
  // List with video frame arrays for each output.
  GPtrArray     *outframes;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstVSplitRequest, gst_vsplit_request);

static GstCaps *
gst_video_split_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS));

    if (gst_is_gbm_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_video_split_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS));

    if (gst_is_gbm_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_video_split_sink_template (void)
{
  return gst_pad_template_new_with_gtype ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_video_split_sink_caps (), GST_TYPE_VIDEO_SPLIT_SINKPAD);
}

static GstPadTemplate *
gst_video_split_src_template (void)
{
  return gst_pad_template_new_with_gtype ("src_%u", GST_PAD_SRC, GST_PAD_REQUEST,
      gst_video_split_src_caps (), GST_TYPE_VIDEO_SPLIT_SRCPAD);
}

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

  g_slice_free (GstVideoFrame, request->inframe);
  g_ptr_array_free (request->outframes, TRUE);
  g_slice_free (GstVSplitRequest, request);
}

static GstVSplitRequest *
gst_vsplit_request_new (guint n_outputs)
{
  GstVSplitRequest *request = g_slice_new0 (GstVSplitRequest);
  guint idx = 0;

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_VSPLIT_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vsplit_request_free);

  request->inframe = g_slice_new0 (GstVideoFrame);

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

static inline void
gst_video_composition_cleanup (gpointer userdata)
{
  GstVideoComposition *composition = (GstVideoComposition*) userdata;

  // Free only video blits, output frame is owned by the request.
  g_slice_free (GstVideoBlit, composition->blits);
}

static inline void
gst_data_queue_free_item (gpointer userdata)
{
 GstDataQueueItem *item = userdata;

  if (item->object != NULL)
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

static inline GstVideoRegionOfInterestMeta*
gst_buffer_find_region_of_interest_meta (GstBuffer * buffer, const guint index)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;
  guint num = 0;

  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    if (roimeta->parent_id != -1)
      continue;

    if (++num == index)
      return roimeta;
  }

  return NULL;
}

static inline void
gst_buffer_transfer_video_region_of_interest_metas (GstBuffer * outbuffer,
    GstBuffer * inbuffer, const gint parent_meta_id, GstVideoCoords * s_offset,
    GstVideoCoords * d_offset, const gdouble w_scale, const gdouble h_scale)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;
  GList *params = NULL, *param = NULL;
  gint id = -1, parent_id = -1;

  // Find and transfer all necessary ROI metas depending on the configuration.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state)) != NULL) {
    // Skip if ROI is a ImageRegion with actual data (set by another vsplit).
    if (roimeta->roi_type == g_quark_from_static_string ("ImageRegion"))
      continue;

    // If this output is from root ROI meta then transfer only derived ROIs,
    // otherwise transfer all ROIs. Also transfer the list of attached metas.
    if ((parent_meta_id != -1) && (roimeta->parent_id != parent_meta_id))
      continue;

    // Save the pointer to the params list and ID for transfering them later.
    params = roimeta->params;
    id = roimeta->id;
    parent_id = roimeta->parent_id;

    roimeta = gst_buffer_add_video_region_of_interest_meta_id (outbuffer,
        roimeta->roi_type, roimeta->x, roimeta->y, roimeta->w, roimeta->h);

    roimeta->id = id;
    roimeta->parent_id = parent_id;

    roimeta->w = roimeta->w * w_scale;
    roimeta->h = roimeta->h * h_scale;

    roimeta->x = d_offset->x + ((roimeta->x - s_offset->x) * w_scale);
    roimeta->y = d_offset->y + ((roimeta->y - s_offset->y) * h_scale);

    for (param = params; param != NULL; param = g_list_next (param)) {
      GstStructure *structure = GST_STRUCTURE_CAST (param->data);
      GQuark id = gst_structure_get_name_id (structure);
      const GValue *value = NULL;

      if (id == g_quark_from_static_string ("VideoLandmarks")) {
        GArray *keypoints = NULL, *links = NULL;
        gdouble confidence = 0.0;
        guint num = 0;

        gst_structure_get_double (structure, "confidence", &confidence);

        value = gst_structure_get_value (structure, "keypoints");
        keypoints = g_array_copy (g_value_get_boxed (value));

        value = gst_structure_get_value (structure, "links");
        links = g_array_copy (g_value_get_boxed (value));

        // Correct the X and Y of each keypoint based on the regions.
        for (num = 0; num < keypoints->len; num++) {
          GstVideoKeypoint *kp = &(g_array_index (keypoints, GstVideoKeypoint, num));

          kp->x = kp->x * w_scale;
          kp->y = kp->y * h_scale;
        }

        structure = gst_structure_copy (structure);
        gst_structure_set (structure, "keypoints", G_TYPE_ARRAY, keypoints,
            "links", G_TYPE_ARRAY, links, "confidence", G_TYPE_DOUBLE,
            confidence, NULL);

        gst_video_region_of_interest_meta_add_param (roimeta, structure);
      } else if (id == g_quark_from_static_string ("ImageClassification")) {
        structure = gst_structure_copy (structure);
        gst_video_region_of_interest_meta_add_param (roimeta, structure);
      } else if (id == g_quark_from_static_string ("ObjectDetection")) {
        structure = gst_structure_copy (structure);
        gst_video_region_of_interest_meta_add_param (roimeta, structure);
      }
    }

    // If no root parent meta was passed then don't look for sub-derived metas.
    if (parent_meta_id == -1)
      continue;

    // Call recursively in order to add metas sub-derived by current meta.
    gst_buffer_transfer_video_region_of_interest_metas (outbuffer, inbuffer,
        roimeta->id, s_offset, d_offset, w_scale, h_scale);
  }
}

static inline GstVideoLandmarksMeta *
gst_buffer_transfer_video_landmarks_meta (GstBuffer * buffer,
    GstVideoLandmarksMeta * lmkmeta, GstVideoCoords * offset,
    const gdouble w_scale, const gdouble h_scale)
{
  GstVideoLandmarksMeta *newmeta = NULL;
  GArray *keypoints = NULL, *links = NULL;
  guint num = 0;

  keypoints = g_array_copy (lmkmeta->keypoints);
  links = g_array_copy (lmkmeta->links);

  // Correct the X and Y of each keypoint bases on the regions.
  for (num = 0; num < keypoints->len; num++) {
    GstVideoKeypoint *kp = &(g_array_index (keypoints, GstVideoKeypoint, num));

    kp->x = (kp->x * w_scale) + offset->x;
    kp->y = (kp->y * h_scale) + offset->y;
  }

  newmeta = gst_buffer_add_video_landmarks_meta (buffer, lmkmeta->confidence,
      keypoints, links);
  newmeta->id = lmkmeta->id;

  if (lmkmeta->xtraparams != NULL)
    newmeta->xtraparams = gst_structure_copy (lmkmeta->xtraparams);

  return newmeta;
}

static inline GstVideoClassificationMeta *
gst_buffer_transfer_video_classification_meta (GstBuffer * buffer,
    GstVideoClassificationMeta * classmeta)
{
  GstVideoClassificationMeta *newmeta = NULL;
  GArray *labels = g_array_copy (classmeta->labels);
  guint idx = 0;

  // The GArray copy above naturally doesn't copy the data in pointers.
  // Iterate over the labels and deep copy any extra params.
  for (idx = 0; idx < labels->len; idx++) {
    GstClassLabel *label = &(g_array_index (labels, GstClassLabel, idx));

    if (label->xtraparams == NULL)
      continue;

    label->xtraparams = gst_structure_copy (label->xtraparams);
  }

  g_array_set_clear_func (labels,
      (GDestroyNotify) gst_video_classification_label_cleanup);

  newmeta = gst_buffer_add_video_classification_meta (buffer, labels);
  newmeta->id = classmeta->id;

  return newmeta;
}

static inline void
gst_video_split_composition_populate_metas (GstVideoSplitSrcPad * srcpad,
    GstVideoComposition * composition, GstVideoRegionOfInterestMeta * roimeta)
{
  GstBuffer *outbuffer = NULL, *inbuffer = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  GList *params = NULL, *param = NULL;
  GstMeta *meta = NULL;
  gpointer state = NULL;
  GstVideoCoords s_offset = {0, 0}, d_offset = {0, 0};
  gdouble w_scale = 0.0, h_scale = 0.0;
  gint parent_meta_id = -1;

  inbuffer = composition->blits[0].frame->buffer;
  outbuffer = composition->frame->buffer;

  source = &(composition->blits[0].source);
  destination = &(composition->blits[0].destination);

  s_offset.x = source->x;
  s_offset.y = source->y;
  d_offset.x = destination->x;
  d_offset.y = destination->y;

  gst_util_fraction_to_double (destination->w, source->w, &w_scale);
  gst_util_fraction_to_double (destination->h, source->h, &h_scale);

  parent_meta_id = (roimeta != NULL) ? roimeta->id : (-1);

  gst_buffer_transfer_video_region_of_interest_metas (outbuffer, inbuffer,
      parent_meta_id, &s_offset, &d_offset, w_scale, h_scale);

  // Transfer all other metas derived from this ROI and nested in the params list.
  params = (roimeta != NULL) ? roimeta->params : NULL;

  for (param = params; param != NULL; param = g_list_next (param)) {
    GstStructure *structure = GST_STRUCTURE_CAST (param->data);
    GQuark id = gst_structure_get_name_id (structure);
    const GValue *value = NULL;

    if (id == g_quark_from_static_string ("VideoLandmarks")) {
      GstVideoLandmarksMeta *lmkmeta = NULL;
      GArray *keypoints = NULL, *links = NULL;
      gdouble confidence = 0.0;
      guint idx = 0;

      gst_structure_get_double (structure, "confidence", &confidence);

      value = gst_structure_get_value (structure, "keypoints");
      keypoints = g_array_copy (g_value_get_boxed (value));

      value = gst_structure_get_value (structure, "links");
      links = g_array_copy (g_value_get_boxed (value));

      // Correct the X and Y of each keypoint bases on the regions.
      for (idx = 0; idx < keypoints->len; idx++) {
        GstVideoKeypoint *kp = &(g_array_index (keypoints, GstVideoKeypoint, idx));

        kp->x = (kp->x * w_scale) + destination->x;
        kp->y = (kp->y * h_scale) + destination->y;
      }

      lmkmeta = gst_buffer_add_video_landmarks_meta (outbuffer, confidence,
          keypoints, links);
      gst_structure_get_uint (structure, "id", &(lmkmeta->id));

      if ((value = gst_structure_get_value (structure, "xtraparams")) != NULL) {
        GstStructure *xtraparams = GST_STRUCTURE (g_value_get_boxed (value));
        lmkmeta->xtraparams = gst_structure_copy (xtraparams);
      }

      GST_TRACE_OBJECT (srcpad, "Attached derived 'VideoLandmarks' meta "
          "with ID[0x%X] to buffer %p", lmkmeta->id, outbuffer);
    } else if (id == g_quark_from_static_string ("ImageClassification")) {
      GstVideoClassificationMeta *classmeta = NULL;
      GArray *labels = NULL;
      const GValue *value = NULL;
      guint idx = 0;

      value = gst_structure_get_value (structure, "labels");
      labels = g_array_copy (g_value_get_boxed (value));

      // The GArray copy above naturally doesn't copy the data in pointers.
      // Iterate over the labels and deep copy any extra params.
      for (idx = 0; idx < labels->len; idx++) {
        GstClassLabel *label = &(g_array_index (labels, GstClassLabel, idx));

        if (label->xtraparams == NULL)
          continue;

        label->xtraparams = gst_structure_copy (label->xtraparams);
      }

      g_array_set_clear_func (labels,
          (GDestroyNotify) gst_video_classification_label_cleanup);

      classmeta = gst_buffer_add_video_classification_meta (outbuffer, labels);
      gst_structure_get_uint (structure, "id", &(classmeta->id));

      GST_TRACE_OBJECT (srcpad, "Attached derived 'ImageClassification' meta "
          "with ID[0x%X] to buffer %p", classmeta->id, outbuffer);
    }
  }

  // If output buffer is produced from ROI do not transfer any other meta.
  if (roimeta != NULL)
    return;

  // Output buffer is produced directly from input buffer, transfer non-ROI metas.
  while ((meta = gst_buffer_iterate_meta (inbuffer, &state))) {
    if (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE) {
      GstVideoClassificationMeta *classmeta =
          GST_VIDEO_CLASSIFICATION_META_CAST (meta);

      classmeta =
          gst_buffer_transfer_video_classification_meta (outbuffer, classmeta);

      GST_TRACE_OBJECT (srcpad, "Transferred 'ImageClassification' meta "
          "with ID[0x%X] to buffer %p", classmeta->id, outbuffer);
    } else if (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE) {
      GstVideoLandmarksMeta *lmkmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

      lmkmeta = gst_buffer_transfer_video_landmarks_meta (outbuffer, lmkmeta,
          &d_offset, w_scale, h_scale);

      GST_TRACE_OBJECT (srcpad, "Transferred 'VideoLandmarks' meta "
          "with ID[0x%X] to buffer %p", lmkmeta->id, outbuffer);
    }
  }
}

static inline void
gst_video_split_composition_update_regions (GstVideoSplitSrcPad * srcpad,
    GstVideoComposition * composition, GstVideoRegionOfInterestMeta * roimeta)
{
  GstBuffer *outbuffer = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  GstVideoRegionOfInterestMeta *rmeta = NULL;
  gint maxwidth = 0, maxheight = 0;

  outbuffer = composition->frame->buffer;
  source = &(composition->blits[0].source);
  destination = &(composition->blits[0].destination);

  if (roimeta != NULL) {
    source->x = roimeta->x;
    source->y = roimeta->y;
    source->w = roimeta->w;
    source->h = roimeta->h;
  } else {
    source->x = source->y = 0;
    source->w = GST_VIDEO_FRAME_WIDTH (composition->blits[0].frame);
    source->h = GST_VIDEO_FRAME_HEIGHT (composition->blits[0].frame);
  }

  destination->x = destination->y = 0;
  destination->w = maxwidth = GST_VIDEO_FRAME_WIDTH (composition->frame);
  destination->h = maxheight = GST_VIDEO_FRAME_HEIGHT (composition->frame);

  // Recalculate the destination width or height depending on the ratios.
  if ((source->w * destination->h) > (source->h * destination->w))
    destination->h = gst_util_uint64_scale_int (maxwidth, source->h, source->w);
  else if ((source->w * destination->h) < (source->h * destination->w))
    destination->w = gst_util_uint64_scale_int (maxheight, source->w, source->h);

  // Additional correction of X and Y axis for centred image disposition.
  destination->x += (maxwidth - destination->w) / 2;
  destination->y += (maxheight - destination->h) / 2;

  // Add ROI meta with the actual part of the buffer filled with image data.
  rmeta = gst_buffer_add_video_region_of_interest_meta (outbuffer, "ImageRegion",
      destination->x, destination->y, destination->w, destination->h);

  // Propagate the original IDs of the ROI meta via the image region.
  if (roimeta != NULL) {
    GstStructure *structure = NULL;

    rmeta->id = roimeta->id;
    rmeta->parent_id = roimeta->parent_id;

    // Transfer the additional ObjectDetection parameters if present.
    structure = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");

    if (structure != NULL) {
      structure = gst_structure_copy (structure);
      gst_video_region_of_interest_meta_add_param (rmeta, structure);
    }
  }

  GST_TRACE_OBJECT (srcpad, "Attached 'ImageRegion' meta with ID[0x%X] parent "
      "ID[0x%X] to buffer %p", rmeta->id, rmeta->parent_id, outbuffer);
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

  // On EOS wait until all queued buffers have been pushed before propagating it.
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    GST_VIDEO_SPLIT_PAD_WAIT_IDLE (GST_VIDEO_SPLIT_SRCPAD_CAST (pad));

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

  if (gst_data_queue_peek (srcpad->buffers, &item)) {
    GstBuffer *buffer = NULL;

    // Take the buffer from the queue item and null the object pointer.
    buffer = GST_BUFFER (item->object);
    item->object = NULL;

    // Adjust the source pad segment position.
    srcpad->segment.position = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);

    GST_TRACE_OBJECT (srcpad, "Pushing %" GST_PTR_FORMAT, buffer);
    gst_pad_push (GST_PAD (srcpad), buffer);

    // Buffer was sent downstream, remove and free the item from the queue.
    if (gst_data_queue_pop (srcpad->buffers, &item))
      item->destroy (item);
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

  if (gst_data_queue_peek (sinkpad->requests, &item)) {
    GstVSplitRequest *request = NULL;

    // Take the request from the queue item and null the object pointer.
    request = GST_VSPLIT_REQUEST (item->object);
    item->object = NULL;

    if (request->fence != NULL) {
      gpointer fence = request->fence;
      GST_TRACE_OBJECT (vsplit, "Waiting request %p", fence);

      if (!gst_video_converter_engine_wait_fence (vsplit->converter, fence))
        GST_WARNING_OBJECT (vsplit, "Waiting request %p failed!", fence);
    }

    // Get time difference between current time and start.
    request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

    GST_LOG_OBJECT (vsplit, "Conversion took %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (request->time),
        (GST_TIME_AS_USECONDS (request->time) % 1000));

    success = gst_element_foreach_src_pad (GST_ELEMENT_CAST (vsplit),
        gst_video_split_srcpad_push_buffer, request);

    if (!success)
      GST_WARNING_OBJECT (vsplit, "Failed to push output buffers!");

    // Free the memory allocated by the internal request structure.
    gst_vsplit_request_release (request);

    // Buffers have been sent, remove and free the sinkpad item from the queue.
    if (gst_data_queue_pop (sinkpad->requests, &item))
      item->destroy (item);
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
  gst_video_converter_engine_flush (vsplit->converter);
  gst_data_queue_flush (GST_VIDEO_SPLIT_SINKPAD (vsplit->sinkpad)->requests);

  GST_INFO_OBJECT (vsplit, "Removing task %p", vsplit->worktask);

  gst_object_unref (vsplit->worktask);
  vsplit->worktask = NULL;

  return TRUE;
}

static gboolean
gst_video_split_populate_frames_and_compositions (GstVideoSplit * vsplit,
    GstVideoFrame * inframe, GPtrArray * vframes, GArray * compositions)
{
  GList *list = NULL;
  GArray *outframes = NULL;
  GstVideoFrame *outframe = NULL;
  GstVideoComposition *composition = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;
  guint idx = 0, num = 0, id = 0, n_metas = 0, n_entries = 0, i = 0;
  gboolean success = TRUE;

  // Calculate the number of non-derived ROI meta entries from the input buffer.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (inframe->buffer, state)) != NULL)
    n_metas += (roimeta->parent_id == -1) ? 1 : 0;

  GST_VIDEO_SPLIT_LOCK (vsplit);

  // Fetch and prepare compositions for each of the source pads.
  for (list = vsplit->srcpads; list != NULL; list = g_list_next (list)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);

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

    // Resize the number of compositions.
    g_array_set_size (compositions, compositions->len + n_entries);

    // Aquire buffer for each frame and update the converter parameters.
    for (idx = 0; idx < outframes->len; idx++, id++) {
      GstVideoRectangle *source = NULL, *destination = NULL;

      outframe = &(g_array_index (outframes, GstVideoFrame, idx));
      success = gst_video_split_acquire_video_frame (srcpad, inframe, outframe);

      if (!success) {
        GST_ERROR_OBJECT (srcpad, "Failed to acquire video frame!");
        break;
      }

      composition = &(g_array_index (compositions, GstVideoComposition, id));

      composition->frame = outframe;
      composition->flags = 0;

      composition->bgcolor = 0x00000000;
      composition->bgfill = TRUE;

      for (i = 0; i < GST_VCE_MAX_CHANNELS; ++i) {
        composition->scales[i] = 1.0;
        composition->offsets[i] = 0.0;
      }

      composition->blits = g_slice_new0 (GstVideoBlit);
      composition->n_blits = 1;

      composition->blits[0].frame = inframe;

      composition->blits[0].alpha = G_MAXUINT8;
      composition->blits[0].rotate = GST_VCE_ROTATE_0;
      composition->blits[0].flip = GST_VCE_FLIP_NONE;

      // Depending on the mode a different ROI meta is used or none at all.
      if (srcpad->mode == GST_VSPLIT_MODE_ROI_SINGLE)
        roimeta = gst_buffer_find_region_of_interest_meta (inframe->buffer, num);
      else if (srcpad->mode == GST_VSPLIT_MODE_ROI_BATCH)
        roimeta = gst_buffer_find_region_of_interest_meta (inframe->buffer, idx);

      // Update source/destination regions and output buffer meta.
      gst_video_split_composition_update_regions (srcpad, composition, roimeta);
      gst_video_split_composition_populate_metas (srcpad, composition, roimeta);

      source = &(composition->blits[0].source);
      destination = &(composition->blits[0].destination);

      GST_TRACE_OBJECT (srcpad, "Composition [%u] Regions: [%d %d %d %d] ->"
          " [%d %d %d %d]", id, source->x, source->y, source->w, source->h,
          destination->x, destination->y, destination->w, destination->h);

      // Reset ROI metadata pointer.
      roimeta = NULL;
    }
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
  GArray *compositions = NULL;
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, inbuffer);

  GST_VIDEO_SPLIT_LOCK (vsplit);
  request = gst_vsplit_request_new (g_list_length (vsplit->srcpads));
  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  // Allocate request structure with shared input frame across all compositions.
  if (request == NULL) {
    GST_ERROR_OBJECT (pad, "Failed to allocate memory for request!");
    goto cleanup;
  }

  success = gst_video_frame_map (request->inframe, sinkpad->info, inbuffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to map input buffer!");
    goto cleanup;
  }

  compositions = g_array_new (FALSE, FALSE, sizeof (GstVideoComposition));

  if (compositions == NULL) {
    GST_ERROR_OBJECT (pad, "Failed to allocate memory for compositions!");
    goto cleanup;
  }

  g_array_set_clear_func (compositions, gst_video_composition_cleanup);

  // Populate total number of compositions and their output frames.
  success = gst_video_split_populate_frames_and_compositions (vsplit,
      request->inframe, request->outframes, compositions);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to populate compositions!");
    goto cleanup;
  }

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  if (compositions->len != 0) {
    success = gst_video_converter_engine_compose (vsplit->converter,
        (GstVideoComposition*) compositions->data, compositions->len,
        &(request->fence));

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to submit request(s)!");
      goto cleanup;
    }
  }

  g_array_free (compositions, TRUE);
  gst_data_queue_push_object (sinkpad->requests, GST_MINI_OBJECT (request));

  return GST_FLOW_OK;

cleanup:
  if ((request == NULL) || (request->inframe->buffer == NULL))
    gst_buffer_unref (inbuffer);

  g_array_free (compositions, TRUE);
  gst_vsplit_request_release (request);

  return GST_FLOW_ERROR;
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

  GST_VIDEO_SPLIT_LOCK (vsplit);

  for (list = vsplit->srcpads; list != NULL; list = g_list_next (list)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);

    if (!gst_video_split_srcpad_setcaps (srcpad, caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vsplit), CORE, NEGOTIATION, (NULL),
          ("Failed to set caps to %s!", GST_PAD_NAME (srcpad)));

      GST_VIDEO_SPLIT_UNLOCK (vsplit);
      return FALSE;
    }
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

      if (needpool) {
        GstStructure *structure = NULL;
        GstVideoAlignment align = { 0, };

        gst_video_utils_get_gpu_align (&info, &align);
        gst_video_info_align (&info, &align);

        pool = gst_video_split_create_pool (pad, caps, &align);
        structure = gst_buffer_pool_get_config (pool);

        // Set caps and size in query.
        gst_buffer_pool_config_set_params (structure, caps, info.size, 0, 0);

        if (!gst_buffer_pool_set_config (pool, structure)) {
          GST_ERROR_OBJECT (pad, "Failed to set buffer pool configuration!");
          gst_object_unref (pool);
          return FALSE;
        }
      }

      // If upstream does't have a pool requirement, set only size in query.
      gst_query_add_allocation_pool (query, pool, info.size, 0, 0);

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
      // Wait until all queued input requests have been processed.
      GST_VIDEO_SPLIT_PAD_WAIT_IDLE (GST_VIDEO_SPLIT_SINKPAD (pad));

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
  GST_DEBUG_OBJECT (vsplit, "Pad has been removed");
}

static GstStateChangeReturn
gst_video_split_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (vsplit->converter != NULL)
        gst_video_converter_engine_free (vsplit->converter);

      vsplit->converter = gst_video_converter_engine_new (vsplit->backend, NULL);

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
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      vsplit->backend = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      g_value_set_enum (value, vsplit->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_finalize (GObject * object)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

  if (vsplit->converter != NULL)
    gst_video_converter_engine_free (vsplit->converter);

  if (vsplit->srcpads != NULL)
    g_list_free (vsplit->srcpads);

  g_rec_mutex_clear (&(vsplit)->worklock);
  g_mutex_clear (&(vsplit)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vsplit));
}

static void
gst_video_split_class_init (GstVideoSplitClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_video_split_debug, "qtivsplit", 0,
      "QTI Video Split");

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_split_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_split_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_split_finalize);

  g_object_class_install_property (gobject, PROP_ENGINE_BACKEND,
      g_param_spec_enum ("engine", "Engine",
          "Engine backend used for the conversion operations",
          GST_TYPE_VCE_BACKEND, DEFAULT_PROP_ENGINE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Video stream splitter", "Video/Demuxer",
      "Split single video stream into multiple streams", "QTI"
  );

  gst_element_class_add_pad_template (element,
      gst_video_split_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_video_split_src_template ());

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_video_split_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_video_split_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_video_split_change_state);
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

  vsplit->backend = DEFAULT_PROP_ENGINE_BACKEND;

  template = gst_video_split_sink_template ();
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
