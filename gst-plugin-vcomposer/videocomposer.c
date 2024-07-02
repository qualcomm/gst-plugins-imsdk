/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "videocomposer.h"

#include <gst/utils/common-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

#include "videocomposersinkpad.h"

#define GST_CAT_DEFAULT gst_video_composer_debug
GST_DEBUG_CATEGORY_STATIC (gst_video_composer_debug);

static void gst_video_composer_child_proxy_init (gpointer g_iface, gpointer data);

#define gst_video_composer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVideoComposer, gst_video_composer,
     GST_TYPE_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
         gst_video_composer_child_proxy_init));

#define DEFAULT_VIDEO_WIDTH         640
#define DEFAULT_VIDEO_HEIGHT        480
#define DEFAULT_VIDEO_FPS_NUM       30
#define DEFAULT_VIDEO_FPS_DEN       1

#define DEFAULT_PROP_MIN_BUFFERS    2
#define DEFAULT_PROP_MAX_BUFFERS    40

#define DEFAULT_PROP_ENGINE_BACKEND (gst_video_converter_default_backend())
#define DEFAULT_PROP_BACKGROUND     0xFF808080

#define GST_VCOMPOSER_MAX_QUEUE_LEN 16

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_VIDEO_FORMATS \
  "{ NV12, NV21, UYVY, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8 }"

static GType gst_converter_request_get_type(void);
#define GST_TYPE_CONVERTER_REQUEST  (gst_converter_request_get_type())
#define GST_CONVERTER_REQUEST(obj) ((GstConverterRequest *) obj)

enum
{
  PROP_0,
  PROP_ENGINE_BACKEND,
  PROP_BACKGROUND,
};

static GstStaticPadTemplate gst_video_composer_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
    );

static GstStaticPadTemplate gst_video_composer_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
    );

typedef struct _GstConverterRequest GstConverterRequest;

struct _GstConverterRequest {
  GstMiniObject parent;

  // Composition asynchronous fence object.
  gpointer      fence;

  // List with video frames for each valid input.
  GArray        *inframes;
  // Output frame submitted with provided ID.
  GstVideoFrame *outframe;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstConverterRequest, gst_converter_request);

static void
gst_converter_request_free (GstConverterRequest * request)
{
  GstVideoFrame *frame = NULL;
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < request->inframes->len; idx++) {
    frame = &(g_array_index (request->inframes, GstVideoFrame, idx));

    if ((buffer = frame->buffer) != NULL) {
      gst_video_frame_unmap (frame);
      gst_buffer_unref (buffer);
    }
  }

  if ((buffer = request->outframe->buffer) != NULL) {
    if (gst_buffer_get_size (buffer) != 0)
      gst_video_frame_unmap (request->outframe);

    gst_buffer_unref (buffer);
  }

  g_slice_free (GstVideoFrame, request->outframe);
  g_array_free (request->inframes, TRUE);
  g_slice_free (GstConverterRequest, request);
}

static GstConverterRequest *
gst_converter_request_new (guint n_inputs)
{
  GstConverterRequest *request = g_slice_new0 (GstConverterRequest);

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_CONVERTER_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_converter_request_free);

  request->inframes =
      g_array_sized_new (FALSE, TRUE, sizeof (GstVideoFrame), n_inputs);
  g_array_set_size (request->inframes, n_inputs);

  request->outframe = g_slice_new0 (GstVideoFrame);
  request->time = GST_CLOCK_TIME_NONE;

  return request;
}

static inline void
gst_converter_request_unref (GstConverterRequest * request)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (request));
}

static inline void
gst_video_composition_cleanup (GstVideoComposition * composition)
{
  guint idx = 0;

  // Free only source/destination rectangles, frames are owned by the request.
  for (idx = 0; idx < composition->n_blits; idx++) {
    g_slice_free (GstVideoRectangle, composition->blits[idx].sources);
    g_slice_free (GstVideoRectangle, composition->blits[idx].destinations);
  }

  // Free only video blits, output frame is owned by the request.
  g_free (composition->blits);
}

static inline void
gst_data_queue_item_free (gpointer data)
{
  GstDataQueueItem *item = data;
  gst_converter_request_unref (GST_CONVERTER_REQUEST (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static inline void
gst_buffer_transfer_roi_meta (GstBuffer * buffer, GstVideoRectangle * source,
    GstVideoRectangle * destination, GstVideoRegionOfInterestMeta * roimeta)
{
  GstVideoRegionOfInterestMeta *newmeta = NULL;
  GList *param = NULL;
  gdouble w_scale = 0.0, h_scale = 0.0;
  guint num = 0, x = 0, y = 0, width = 0, height = 0;

  gst_util_fraction_to_double (destination->w, source->w, &w_scale);
  gst_util_fraction_to_double (destination->h, source->h, &h_scale);

  width = roimeta->w * w_scale;
  height = roimeta->h * h_scale;
  x = (roimeta->x * w_scale) + destination->x;
  y = (roimeta->y * h_scale) + destination->y;

  // Add ROI meta with the actual part of the buffer filled with image data.
  newmeta = gst_buffer_add_video_region_of_interest_meta_id (buffer,
      roimeta->roi_type, x, y, width, height);

  // Transfer all meta derived from the ROI meta.
  for (param = roimeta->params; param != NULL; param = g_list_next (param)) {
    GstStructure *structure = GST_STRUCTURE_CAST (param->data);
    GQuark id = gst_structure_get_name_id (structure);

    if (id == g_quark_from_static_string ("VideoLandmarks")) {
      GArray *keypoints = NULL, *links = NULL;
      GArray *newkeypoints = NULL, *newlinks = NULL;
      gdouble confidence = 0.0;
      guint n_bytes = 0;

      gst_structure_get_double (structure, "confidence", &confidence);

      keypoints = (GArray *) g_value_get_boxed (
          gst_structure_get_value (structure, "keypoints"));
      links = (GArray *) g_value_get_boxed (
          gst_structure_get_value (structure, "links"));

      // TODO: replace with g_array_copy() in glib version > 2.62
      newkeypoints = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoKeypoint),
          keypoints->len);
      newkeypoints = g_array_set_size (newkeypoints, keypoints->len);

      n_bytes = keypoints->len * sizeof (GstVideoKeypoint);
      memcpy (newkeypoints->data, keypoints->data, n_bytes);

      newlinks = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoKeypointLink),
          links->len);
      newlinks = g_array_set_size (newlinks, links->len);

      n_bytes = links->len * sizeof (GstVideoKeypointLink);
      memcpy (newlinks->data, links->data, n_bytes);

      // Correct the X and Y of each keypoint based on the regions.
      for (num = 0; num < newkeypoints->len; num++) {
        GstVideoKeypoint *kp =
            &(g_array_index (newkeypoints, GstVideoKeypoint, num));

        kp->x = kp->x * w_scale;
        kp->y = kp->y * h_scale;
      }

      structure = gst_structure_new ("VideoLandmarks", "keypoints",
          G_TYPE_ARRAY, newkeypoints, "links", G_TYPE_ARRAY, newlinks,
          "confidence", G_TYPE_DOUBLE, confidence, NULL);
      gst_video_region_of_interest_meta_add_param (newmeta, structure);
    } else if (id == g_quark_from_static_string ("ImageClassification")) {
      structure = gst_structure_copy (structure);
      gst_video_region_of_interest_meta_add_param (newmeta, structure);
    } else if (id == g_quark_from_static_string ("ObjectDetection")) {
      structure = gst_structure_copy (structure);
      gst_video_region_of_interest_meta_add_param (newmeta, structure);
    }
  }
}

static inline void
gst_buffer_transfer_landmarks_meta (GstBuffer * buffer, GstVideoRectangle * source,
    GstVideoRectangle * destination, GstVideoLandmarksMeta * lmkmeta)
{
  GArray *keypoints = NULL, *links = NULL;
  gdouble w_scale = 0.0, h_scale = 0.0;
  guint num = 0, n_bytes = 0;

  gst_util_fraction_to_double (destination->w, source->w, &w_scale);
  gst_util_fraction_to_double (destination->h, source->h, &h_scale);

  // TODO: replace with g_array_copy() in glib version > 2.62
  keypoints = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoKeypoint),
      lmkmeta->keypoints->len);
  keypoints = g_array_set_size (keypoints, lmkmeta->keypoints->len);

  links = g_array_sized_new (FALSE, FALSE, sizeof (GstVideoKeypointLink),
      lmkmeta->links->len);
  links = g_array_set_size (links, lmkmeta->links->len);

  n_bytes = keypoints->len * sizeof (GstVideoKeypoint);
  memcpy (keypoints->data, lmkmeta->keypoints->data, n_bytes);

  n_bytes = links->len * sizeof (GstVideoKeypointLink);
  memcpy (links->data, lmkmeta->links->data, n_bytes);

  // Correct the X and Y of each keypoint bases on the regions.
  for (num = 0; num < keypoints->len; num++) {
    GstVideoKeypoint *kp = &(g_array_index (keypoints, GstVideoKeypoint, num));

    kp->x = (kp->x * w_scale) + destination->x;
    kp->y = (kp->y * h_scale) + destination->y;
  }

  gst_buffer_add_video_landmarks_meta (buffer, lmkmeta->confidence, keypoints,
      links);
}

static inline void
gst_buffer_transfer_classification_meta (GstBuffer * buffer,
    GstVideoClassificationMeta * classmeta)
{
  GArray *labels = NULL;

  // TODO: replace with g_array_copy() in glib version > 2.62
  labels = g_array_sized_new (FALSE, FALSE, sizeof (GstClassLabel),
      classmeta->labels->len);
  labels = g_array_set_size (labels, classmeta->labels->len);

  memcpy (labels->data, classmeta->labels->data,
      labels->len * sizeof (GstClassLabel));

  gst_buffer_add_video_classification_meta (buffer, labels);
}

static void
gst_video_composition_populate_output_metas (GstVideoComposition * composition)
{
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  GstMeta *meta = NULL;
  gpointer state = NULL;
  guint idx = 0;

  outbuffer = composition->frame->buffer;

  for (idx = 0; idx < composition->n_blits; idx++) {
    inbuffer = composition->blits[idx].frame->buffer;

    source = &(composition->blits[idx].sources[0]);
    destination = &(composition->blits[idx].destinations[0]);

    while ((meta = gst_buffer_iterate_meta (inbuffer, &state))) {
      if (meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
        GstVideoRegionOfInterestMeta *roimeta =
            (GstVideoRegionOfInterestMeta *) meta;

        // Skip if ROI is a ImageRegion with actual data (populated by vsplit).
        // This is primery used for blitting only pixels with actual data.
        if (roimeta->roi_type == g_quark_from_static_string ("ImageRegion"))
          continue;

        gst_buffer_transfer_roi_meta (outbuffer, source, destination, roimeta);
      } else if (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE) {
        GstVideoClassificationMeta *classmeta =
            GST_VIDEO_CLASSIFICATION_META_CAST (meta);

        gst_buffer_transfer_classification_meta (outbuffer, classmeta);
      } else if (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE) {
        GstVideoLandmarksMeta *lmkmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

        gst_buffer_transfer_landmarks_meta (outbuffer, source, destination, lmkmeta);
      }
    }
  }
}

static inline GstVideoConvFlip
gst_video_composer_translate_flip (gboolean flip_h, gboolean flip_v)
{
  if (flip_h && flip_v)
   return GST_VCE_FLIP_BOTH;
  else if (flip_h)
    return GST_VCE_FLIP_HORIZONTAL;
  else if (flip_v)
    return GST_VCE_FLIP_VERTICAL;

  return GST_VCE_FLIP_NONE;
}

static inline GstVideoConvRotate
gst_video_composer_translate_rotation (GstVideoComposerRotate rotation)
{
  switch (rotation) {
    case GST_VIDEO_COMPOSER_ROTATE_90_CW:
      return GST_VCE_ROTATE_90;
    case GST_VIDEO_COMPOSER_ROTATE_90_CCW:
      return GST_VCE_ROTATE_270;
    case GST_VIDEO_COMPOSER_ROTATE_180:
      return GST_VCE_ROTATE_180;
    case GST_VIDEO_COMPOSER_ROTATE_NONE:
      return GST_VCE_ROTATE_0;
    default:
      GST_WARNING ("Invalid rotation flag %d!", rotation);
  }
  return GST_VCE_ROTATE_0;
}

static gint
gst_video_composer_zorder_compare (const GstVideoComposerSinkPad * lpad,
    const GstVideoComposerSinkPad * rpad)
{
  return lpad->zorder - rpad->zorder;
}

static gint
gst_video_composer_index_compare (const GstVideoComposerSinkPad * pad,
    const guint * index)
{
  return pad->index - (*index);
}

static GstBufferPool *
gst_video_composer_create_pool (GstVideoComposer * vcomposer, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  // If downstream allocation query supports GBM, allocate gbm memory.
  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    GST_INFO_OBJECT (vcomposer, "Uses GBM memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
  } else {
    GST_INFO_OBJECT (vcomposer, "Uses ION memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (gst_caps_has_compression (caps, "ubwc")) {
    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_UBWC_MODE);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (vcomposer, "Failed to set pool configuration!");
    g_clear_object (&pool);
  }

  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_video_composer_prepare_input_frame (GstVideoComposer * vcomposer,
    GstVideoComposerSinkPad * sinkpad, GstVideoFrame * frame)
{
  GstBuffer *buffer = NULL;
  GstSegment *segment = NULL;
  GstClockTime timestamp, position;

  buffer = gst_aggregator_pad_peek_buffer (GST_AGGREGATOR_PAD (sinkpad));

  if (buffer == NULL) {
    GST_TRACE_OBJECT (sinkpad, "No buffer available!");
    return FALSE;
  }

  GST_TRACE_OBJECT (sinkpad, "Taking %" GST_PTR_FORMAT, buffer);

  segment = &GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment;

  // Check whether the buffer should be kept in the queue for future reuse.
  timestamp = gst_segment_to_running_time (
      &GST_AGGREGATOR_PAD (sinkpad)->segment, GST_FORMAT_TIME,
      GST_BUFFER_PTS (buffer)) + GST_BUFFER_DURATION (buffer);
  position = gst_segment_to_running_time (segment, GST_FORMAT_TIME,
      segment->position) + vcomposer->duration;

  if (timestamp > position)
    GST_TRACE_OBJECT (sinkpad, "Keeping buffer at least until %"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  else
    gst_aggregator_pad_drop_buffer (GST_AGGREGATOR_PAD (sinkpad));

  // GAP buffer, nothing further to do.
  if (gst_buffer_get_size (buffer) == 0 ||
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    gst_buffer_unref (buffer);
    return TRUE;
  }

  if (!gst_video_frame_map (frame, sinkpad->info, buffer,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (sinkpad, "Failed to map input buffer!");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_video_composer_prepare_output_frame (GstVideoComposer * vcomposer,
    GstVideoFrame * frame, gboolean is_gap)
{
  GstBufferPool *pool = vcomposer->outpool;
  GstBuffer *buffer = NULL;

  if (!is_gap) {
    if (!gst_buffer_pool_is_active (pool) &&
        !gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (vcomposer, "Failed to activate output video buffer pool!");
      return FALSE;
    }

    if (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (vcomposer, "Failed to create output video buffer!");
      return FALSE;
    }

    if (!gst_video_frame_map (frame, vcomposer->outinfo, buffer,
            GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
      GST_ERROR_OBJECT (vcomposer, "Failed to map output buffer!");
      gst_buffer_unref (buffer);
      return FALSE;
    }
  } else {
    // Create an empty GAP buffer, which will be submitted downstream.
    buffer = gst_buffer_new ();
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_GAP);
    frame->buffer = buffer;
  }

  GST_BUFFER_DURATION (buffer) = vcomposer->duration;

  {
    GstSegment *s = NULL;

    GST_OBJECT_LOCK (vcomposer);
    s = &GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment;

    GST_BUFFER_TIMESTAMP (buffer) = (s->position == GST_CLOCK_TIME_NONE ||
        s->position <= s->start) ? s->start : s->position;

    s->position = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    GST_OBJECT_UNLOCK (vcomposer);
  }

  GST_TRACE_OBJECT (vcomposer, "Output %" GST_PTR_FORMAT, buffer);
  return TRUE;
}

static gboolean
gst_video_composer_populate_frames_and_composition (
    GstVideoComposer * vcomposer, GArray * inframes, GstVideoFrame * outframe,
    GstVideoComposition * composition)
{
  GList *list = NULL;
  gboolean is_gap = FALSE;
  guint idx = 0, num = 0;

  GST_OBJECT_LOCK (vcomposer);

  // Extrapolate the highest width, height and frame rate from the sink pads.
  for (list = GST_ELEMENT (vcomposer)->sinkpads; list; list = list->next, idx++) {
    GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (list->data);
    GstVideoFrame *inframe = &(g_array_index (inframes, GstVideoFrame, idx));
    GstVideoBlit *blit = NULL;

    if (gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (sinkpad)))
      continue;

    if (!gst_video_composer_prepare_input_frame (vcomposer, sinkpad, inframe)) {
      GST_TRACE_OBJECT (vcomposer, "Failed to prepare input frame!");
      GST_OBJECT_UNLOCK (vcomposer);
      return FALSE;
    }

    // GAP input buffer, nothing to do.
    if (inframe->buffer == NULL)
      continue;

    num = composition->n_blits++;

    composition->blits =
        g_renew (GstVideoBlit, composition->blits, composition->n_blits);

    blit = &(composition->blits[num]);
    blit->frame = inframe;

    GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

    blit->alpha = sinkpad->alpha * G_MAXUINT8;
    blit->isubwc = sinkpad->isubwc;

    blit->flip = gst_video_composer_translate_flip (sinkpad->flip_h, sinkpad->flip_v);
    blit->rotate = gst_video_composer_translate_rotation (sinkpad->rotation);

    blit->sources = g_slice_dup (GstVideoRectangle, &(sinkpad->crop));
    blit->destinations = g_slice_dup (GstVideoRectangle, &(sinkpad->destination));
    blit->n_regions = 1;

    if ((blit->sources[0].w == 0) && (blit->sources[0].h == 0)) {
      blit->sources[0].w = GST_VIDEO_FRAME_WIDTH (blit->frame);
      blit->sources[0].h = GST_VIDEO_FRAME_HEIGHT (blit->frame);
    }

    if ((blit->destinations[0].w == 0) && (blit->destinations[0].h == 0)) {
      blit->destinations[0].w = GST_VIDEO_INFO_WIDTH (vcomposer->outinfo);
      blit->destinations[0].h = GST_VIDEO_INFO_HEIGHT (vcomposer->outinfo);
    }

    GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);
  }

  GST_OBJECT_UNLOCK (vcomposer);

  // Whether to allocate a GAP output buffer.
  is_gap = (composition->n_blits == 0) ? TRUE : FALSE;

  if (!gst_video_composer_prepare_output_frame (vcomposer, outframe, is_gap)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to prepae output frame!");
    return FALSE;
  }

  composition->frame = outframe;
  composition->bgfill = TRUE;
  composition->flags = 0;

  GST_VIDEO_COMPOSER_LOCK (vcomposer);

  composition->bgcolor = vcomposer->background;
  composition->isubwc = vcomposer->isubwc;

  GST_VIDEO_COMPOSER_UNLOCK (vcomposer);

  // Transfer metadata from the input buffers to the output buffer.
  gst_video_composition_populate_output_metas (composition);

  return TRUE;
}

static gboolean
gst_video_composer_propose_allocation (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * inquery, GstQuery * outquery)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (aggregator);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  guint size = 0;
  gboolean needpool = FALSE;

  GST_DEBUG_OBJECT (vcomposer, "Pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (vcomposer, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to get video info!");
    return FALSE;
  }

  // Get the size from video info.
  size = GST_VIDEO_INFO_SIZE (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    pool = gst_video_composer_create_pool (vcomposer, caps);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (vcomposer, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, needpool ? pool : NULL, size, 0, 0);

  if (pool != NULL)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (outquery, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_video_composer_decide_allocation (GstAggregator * aggregator,
    GstQuery * query)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (aggregator);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  guint size, minbuffers, maxbuffers;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (vcomposer, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (vcomposer->outpool) {
    gst_buffer_pool_set_active (vcomposer->outpool, FALSE);
    gst_object_unref (vcomposer->outpool);
  }

  // Create a new buffer pool.
  pool = gst_video_composer_create_pool (vcomposer, caps);
  vcomposer->outpool = pool;

  {
    GstStructure *config = NULL;
    GstAllocator *allocator = NULL;
    GstAllocationParams params;

    // Get the configured pool properties in order to set in query.
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
        &maxbuffers);

    if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
      gst_query_add_allocation_param (query, allocator, &params);

    gst_structure_free (config);
  }

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers,
        maxbuffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_video_composer_sink_query (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * query)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_TRACE_OBJECT (vcomposer, "Received %s query on pad %s:%s",
      GST_QUERY_TYPE_NAME (query), GST_DEBUG_PAD_NAME (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter = NULL, *caps = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_video_composer_sinkpad_getcaps (pad, aggregator, filter);
      gst_query_set_caps_result (query, caps);

      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_video_composer_sinkpad_acceptcaps (pad, aggregator, caps);
      gst_query_set_accept_caps_result (query, success);

      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (
      aggregator, pad, query);
}

static gboolean
gst_video_composer_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstEvent * event)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_TRACE_OBJECT (vcomposer, "Received %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_video_composer_sinkpad_setcaps (pad, aggregator, caps);

      gst_event_unref (event);
      return success;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (
      aggregator, pad, event);
}

static gboolean
gst_video_composer_src_query (GstAggregator * aggregator, GstQuery * query)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_TRACE_OBJECT (vcomposer, "Received %s query on src pad",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (vcomposer, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_DURATION:
      // TODO
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (aggregator, query);
}

static gboolean
gst_video_composer_src_event (GstAggregator * aggregator, GstEvent * event)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_TRACE_OBJECT (vcomposer, "Received %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      // TODO
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_event (aggregator, event);
}

static GstFlowReturn
gst_video_composer_update_src_caps (GstAggregator * aggregator,
    GstCaps * caps, GstCaps ** othercaps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  gint outwidth = 0, outheight = 0, out_fps_n = 0, out_fps_d = 0;
  guint idx = 0, length = 0;
  gboolean configured = TRUE;


  GST_DEBUG_OBJECT (vcomposer, "Update output caps based on caps %"
      GST_PTR_FORMAT, caps);

  {
    GstVideoComposerSinkPad *sinkpad = NULL;
    GList *list = NULL;

    GST_OBJECT_LOCK (vcomposer);

    // Extrapolate the highest width, height and frame rate from the sink pads.
    for (list = GST_ELEMENT (vcomposer)->sinkpads; list; list = list->next) {
      gint width, height, fps_n, fps_d;
      gdouble fps = 0.0, outfps = 0;

      sinkpad = GST_VIDEO_COMPOSER_SINKPAD_CAST (list->data);

      if (NULL == sinkpad->info) {
        GST_DEBUG_OBJECT (vcomposer, "%s caps not set!", GST_PAD_NAME (sinkpad));
        configured = FALSE;
        continue;
      }

      GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

      width = (sinkpad->destination.w != 0) ?
          sinkpad->destination.w : GST_VIDEO_INFO_WIDTH (sinkpad->info);
      height = (sinkpad->destination.h != 0) ?
          sinkpad->destination.h : GST_VIDEO_INFO_HEIGHT (sinkpad->info);

      fps_n = GST_VIDEO_INFO_FPS_N (sinkpad->info);
      fps_d = GST_VIDEO_INFO_FPS_D (sinkpad->info);

      // Adjust the width & height to take into account the X & Y coordinates.
      width += (width > 0) ? sinkpad->destination.x : 0;
      height += (height > 0) ? sinkpad->destination.y : 0;

      GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

      if (width == 0 || height == 0)
        continue;

      // Take the greater dimensions.
      outwidth = (width > outwidth) ? width : outwidth;
      outheight = (height > outheight) ? height : outheight;

      gst_util_fraction_to_double (fps_n, fps_d, &fps);

      if (out_fps_d != 0)
        gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

      if (outfps < fps) {
        out_fps_n = fps_n;
        out_fps_d = fps_d;
      }
    }

    GST_OBJECT_UNLOCK (vcomposer);
  }

  *othercaps = gst_caps_new_empty ();
  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (caps, idx);
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);
    const GValue *framerate = NULL;
    gint width = 0, height = 0;

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (*othercaps, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
    framerate = gst_structure_get_value (structure, "framerate");

    if (!width && !outwidth) {
      gst_structure_set (structure, "width", G_TYPE_INT,
          DEFAULT_VIDEO_WIDTH, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Width not set, using default value: %d",
          DEFAULT_VIDEO_WIDTH);
    } else if (!width) {
      gst_structure_set (structure, "width", G_TYPE_INT, outwidth, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Width not set, using extrapolated width "
          "based on the sinkpads: %d", outwidth);
    } else if (width < outwidth) {
      GST_ERROR_OBJECT (vcomposer, "Set width (%u) is not compatible with the "
          "extrapolated width (%d) from the sinkpads!", width, outwidth);
      gst_structure_free (structure);
      gst_caps_unref (*othercaps);
      return GST_FLOW_NOT_SUPPORTED;
    }

    if (!height && !outheight) {
      gst_structure_set (structure, "height", G_TYPE_INT,
          DEFAULT_VIDEO_HEIGHT, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Height not set, using default value: %d",
          DEFAULT_VIDEO_HEIGHT);
    } else if (!height) {
      gst_structure_set (structure, "height", G_TYPE_INT, outheight, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Height not set, using extrapolated height "
          "based on the sinkpads: %d", outheight);
    } else if (height < outheight) {
      GST_ERROR_OBJECT (vcomposer, "Set height (%u) is not compatible with the "
          "extrapolated height (%d) from the sinkpads!", height, outheight);
      gst_structure_free (structure);
      gst_caps_unref (*othercaps);
      return GST_FLOW_NOT_SUPPORTED;
    }

    if (!gst_value_is_fixed (framerate) && (out_fps_n <= 0 || out_fps_d <= 0)) {
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
      GST_DEBUG_OBJECT (vcomposer, "Frame rate not set, using default value: "
          "%d/%d", DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
    } else if (!gst_value_is_fixed (framerate)) {
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          out_fps_n, out_fps_d);
      GST_DEBUG_OBJECT (vcomposer, "Frame rate not set, using extrapolated "
          "rate (%d/%d) from the sinkpads", out_fps_n, out_fps_d);
    } else {
      gint fps_n = gst_value_get_fraction_numerator (framerate);
      gint fps_d = gst_value_get_fraction_denominator (framerate);
      gdouble fps = 0.0, outfps = 0.0;

      gst_util_fraction_to_double (fps_n, fps_d, &fps);
      gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

      if (fps != outfps) {
        GST_ERROR_OBJECT (vcomposer, "Set framerate (%d/%d) is not compatible"
            " with the extrapolated rate (%d/%d) from the sinkpads!", fps_n,
            fps_d, out_fps_n, out_fps_d);
        gst_structure_free (structure);
        gst_caps_unref (*othercaps);
        return GST_FLOW_NOT_SUPPORTED;
      }
    }

    // TODO optimize that to take into account sink pads format.
    // Fixate the format field in case it wasn't already fixated.
    gst_structure_fixate_field (structure, "format");

    framerate = gst_structure_get_value (structure, "framerate");
    vcomposer->duration = gst_util_uint64_scale_int (GST_SECOND,
        gst_value_get_fraction_denominator (framerate),
        gst_value_get_fraction_numerator (framerate));

    gst_caps_append_structure_full (*othercaps, structure,
        gst_caps_features_copy (features));
  }

  GST_DEBUG_OBJECT (vcomposer, "Updated caps %" GST_PTR_FORMAT, *othercaps);

  // Applicable for 1.20 version, since the GstAggregator base class is not
  // marking src pad for reconfigure in case NEED_DATA is returned.
  // On older versions, mark reconfigure is already happening but it won't
  // cause any problem since it is just about setting the flag.
  if (!configured)
    gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vcomposer));

  return configured ? GST_FLOW_OK : GST_AGGREGATOR_FLOW_NEED_DATA;
}

static GstCaps *
gst_video_composer_fixate_src_caps (GstAggregator * aggregator, GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  guint idx = 0;

  // Check caps structures for memory:GBM feature.
  for (idx = 0; idx < gst_caps_get_size (caps); idx++) {
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);

    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GBM)) {
      // Found caps structure with memory:GBM feature, remove all others.
      GstStructure *structure = gst_caps_steal_structure (caps, idx);

      gst_caps_unref (caps);
      caps = gst_caps_new_empty ();

      gst_caps_append_structure_full (caps, structure,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
      break;
    }
  }

  caps = gst_caps_fixate (caps);
  GST_DEBUG_OBJECT (vcomposer, "Fixated output caps to %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_video_composer_negotiated_src_caps (GstAggregator * aggregator,
    GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GstVideoInfo info;
  gint dar_n = 0, dar_d = 0;

  GST_DEBUG_OBJECT (vcomposer, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to get video info from caps!");
    return FALSE;
  }

  if (!gst_util_fraction_multiply (info.width, info.height,
          info.par_n, info.par_d, &dar_n, &dar_d)) {
    GST_WARNING_OBJECT (vcomposer, "Failed to calculate DAR!");
    dar_n = dar_d = -1;
  }

  GST_DEBUG_OBJECT (vcomposer, "Output %dx%d (PAR: %d/%d, DAR: %d/%d), size"
      " %" G_GSIZE_FORMAT, info.width, info.height, info.par_n, info.par_d,
      dar_n, dar_d, info.size);

  if (vcomposer->outinfo != NULL)
    gst_video_info_free (vcomposer->outinfo);

  vcomposer->outinfo = gst_video_info_copy (&info);
  vcomposer->isubwc = gst_caps_has_compression (caps, "ubwc");

  if (vcomposer->converter != NULL)
    gst_video_converter_engine_free (vcomposer->converter);

  vcomposer->converter = gst_video_converter_engine_new (vcomposer->backend, NULL);

  gst_aggregator_set_latency (aggregator, vcomposer->duration,
      vcomposer->duration);

  return TRUE;
}

static void
gst_video_composer_task_loop (gpointer userdata)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (vcomposer->requests, &item)) {
    GstConverterRequest *request = NULL;
    GstBuffer *buffer = NULL;
    gboolean success = TRUE;

    // Increase the request reference count to indicate that it is in use.
    request = GST_CONVERTER_REQUEST (gst_mini_object_ref (item->object));
    item->destroy (item);

    if (request->fence != NULL) {
      GST_TRACE_OBJECT (vcomposer, "Waiting request %p", request->fence);
      success = gst_video_converter_engine_wait_fence (vcomposer->converter,
          request->fence);
    }

    if (!success) {
      GST_DEBUG_OBJECT (vcomposer, " Waiting request %p failed!", request->fence);
      gst_converter_request_unref (request);
      return;
    }

    // Get time difference between current time and start.
    request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

    GST_LOG_OBJECT (vcomposer, "Request %p took %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", request->fence, GST_TIME_AS_MSECONDS (request->time),
        (GST_TIME_AS_USECONDS (request->time) % 1000));

    // Increase the buffer reference count to indicate that it is in use.
    buffer = gst_buffer_ref (request->outframe->buffer);
    gst_converter_request_unref (request);

    gst_aggregator_finish_buffer (GST_AGGREGATOR (vcomposer), buffer);
  } else {
    GST_DEBUG_OBJECT (vcomposer, "Paused worker thread");
    gst_task_pause (vcomposer->worktask);
  }
}

static gboolean
gst_video_composer_start (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  if (vcomposer->worktask != NULL)
    return TRUE;

  vcomposer->worktask =
      gst_task_new (gst_video_composer_task_loop, aggregator, NULL);
  GST_INFO_OBJECT (vcomposer, "Created task %p", vcomposer->worktask);

  gst_task_set_lock (vcomposer->worktask, &vcomposer->worklock);

  if (!gst_task_start (vcomposer->worktask)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to start worker task!");
    return FALSE;
  }

  // Disable requests queue in flushing state to enable normal work.
  gst_data_queue_set_flushing (vcomposer->requests, FALSE);
  return TRUE;
}

static gboolean
gst_video_composer_stop (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  if (NULL == vcomposer->worktask)
    return TRUE;

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (vcomposer->requests, TRUE);

  if (!gst_task_stop (vcomposer->worktask))
    GST_WARNING_OBJECT (vcomposer, "Failed to stop worker task!");

  // Make sure task is not running.
  g_rec_mutex_lock (&vcomposer->worklock);
  g_rec_mutex_unlock (&vcomposer->worklock);

  if (!gst_task_join (vcomposer->worktask)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to join worker task!");
    return FALSE;
  }

  // Flush converter and requests queue.
  gst_video_converter_engine_flush (vcomposer->converter);
  gst_data_queue_flush (vcomposer->requests);

  GST_INFO_OBJECT (vcomposer, "Removing task %p", vcomposer->worktask);

  gst_object_unref (vcomposer->worktask);
  vcomposer->worktask = NULL;

  return TRUE;
}

static GstClockTime
gst_video_composer_get_next_time (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;
  GstClockTime nexttime;

  GST_OBJECT_LOCK (vcomposer);
  nexttime = (segment->position == GST_CLOCK_TIME_NONE ||
      segment->position < segment->start) ? segment->start : segment->position;

  if (segment->stop != GST_CLOCK_TIME_NONE && nexttime > segment->stop)
    nexttime = segment->stop;

  nexttime = gst_segment_to_running_time (segment, GST_FORMAT_TIME, nexttime);
  GST_OBJECT_UNLOCK (vcomposer);

  return nexttime;
}

static gboolean
gst_video_composer_is_eos (GstAggregator * aggregator)
{
  GList *list = NULL;
  gboolean eos = TRUE;

  GST_OBJECT_LOCK (aggregator);

  // Iterate over every sink pad and check whether they reached EOS.
  for (list = GST_ELEMENT (aggregator)->sinkpads; list; list = list->next)
    eos &= gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (list->data));

  GST_OBJECT_UNLOCK (aggregator);

  return eos;
}

static GstFlowReturn
gst_video_composer_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GstConverterRequest *request = NULL;
  GstDataQueueItem *item = NULL;
  GstVideoComposition composition = GST_VCE_COMPOSITION_INIT;
  gboolean success = FALSE;

  if (timeout && (NULL == vcomposer->outinfo))
    return GST_AGGREGATOR_FLOW_NEED_DATA;

  // Check whether all pads have reached EOS.
  if (gst_video_composer_is_eos (aggregator))
    return GST_FLOW_EOS;

  request = gst_converter_request_new (vcomposer->n_inputs);

  success = gst_video_composer_populate_frames_and_composition (vcomposer,
      request->inframes, request->outframe, &composition);

  if (!success) {
    gst_video_composition_cleanup (&composition);
    gst_converter_request_unref (request);
    return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  if ((composition.blits != NULL) && (composition.n_blits != 0)) {
    success = gst_video_converter_engine_compose (vcomposer->converter,
        &composition, 1, &(request->fence));
    gst_video_composition_cleanup (&composition);
  }

  if (!success) {
    GST_WARNING_OBJECT (vcomposer, "Failed to submit request to converter!");
    gst_converter_request_unref (request);
    return GST_FLOW_ERROR;
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (request);
  item->visible = TRUE;
  item->destroy = gst_data_queue_item_free;

  // Push the request into the queue or free it on failure.
  if (!gst_data_queue_push (vcomposer->requests, item)) {
    item->destroy (item);
    return GST_FLOW_OK;
  }

  GST_TRACE_OBJECT (vcomposer, "Submitted request with ID: %p", request->fence);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_video_composer_flush (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_INFO_OBJECT (vcomposer, "Flushing request queue");

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (vcomposer->requests, TRUE);

  // Flush converter and requests queue.
  gst_video_converter_engine_flush (vcomposer->converter);
  gst_data_queue_flush (vcomposer->requests);

  return GST_AGGREGATOR_CLASS (parent_class)->flush (aggregator);;
}

static GstPad*
gst_video_composer_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (element);
  GstPad *pad = NULL;

  pad = GST_ELEMENT_CLASS (parent_class)->request_new_pad
      (element, templ, reqname, caps);

  if (pad == NULL) {
    GST_ERROR_OBJECT (element, "Failed to create sink pad!");
    return NULL;
  }

  GST_OBJECT_LOCK (vcomposer);

  // Extract the pad index field from its name.
  GST_VIDEO_COMPOSER_SINKPAD (pad)->index =
      g_ascii_strtoull (&GST_PAD_NAME (pad)[5], NULL, 10);

  // In case Z axis order is not filled use the order of creation.
  if (GST_VIDEO_COMPOSER_SINKPAD (pad)->zorder < 0)
    GST_VIDEO_COMPOSER_SINKPAD (pad)->zorder = element->numsinkpads;

  // Sort sink pads by their Z axis order.
  element->sinkpads = g_list_sort (element->sinkpads,
      (GCompareFunc) gst_video_composer_zorder_compare);

  vcomposer->n_inputs = element->numsinkpads;

  GST_OBJECT_UNLOCK (vcomposer);

  GST_DEBUG_OBJECT (vcomposer, "Created pad: %s", GST_PAD_NAME (pad));

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  return pad;
}

static void
gst_video_composer_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (element);

  GST_DEBUG_OBJECT (vcomposer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_OBJECT_LOCK (vcomposer);
  vcomposer->n_inputs = element->numsinkpads - 1;
  GST_OBJECT_UNLOCK (vcomposer);

  if (0 == vcomposer->n_inputs) {
    GstSegment *segment =
        &GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment;
    segment->position = GST_CLOCK_TIME_NONE;
  }

  gst_child_proxy_child_removed (GST_CHILD_PROXY (vcomposer), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (GST_ELEMENT (vcomposer), pad);

  gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vcomposer));
}

static void
gst_video_composer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (object);

  GST_VIDEO_COMPOSER_LOCK (vcomposer);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      vcomposer->backend = g_value_get_enum (value);
      break;
    case PROP_BACKGROUND:
      vcomposer->background = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VIDEO_COMPOSER_UNLOCK (vcomposer);
}

static void
gst_video_composer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (object);

  GST_VIDEO_COMPOSER_LOCK (vcomposer);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      g_value_set_enum (value, vcomposer->backend);
      break;
    case PROP_BACKGROUND:
      g_value_set_uint (value, vcomposer->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VIDEO_COMPOSER_UNLOCK (vcomposer);
}

static void
gst_video_composer_finalize (GObject * object)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (object);

  if (vcomposer->converter != NULL)
    gst_video_converter_engine_free (vcomposer->converter);

  if (vcomposer->requests != NULL) {
    gst_data_queue_set_flushing (vcomposer->requests, TRUE);
    gst_data_queue_flush (vcomposer->requests);
    gst_object_unref (GST_OBJECT_CAST(vcomposer->requests));
  }

  if (vcomposer->outpool != NULL) {
    gst_buffer_pool_set_active (vcomposer->outpool, FALSE);
    gst_object_unref (vcomposer->outpool);
  }

  if (vcomposer->outinfo != NULL)
    gst_video_info_free (vcomposer->outinfo);

  g_rec_mutex_clear (&vcomposer->worklock);
  g_mutex_clear (&vcomposer->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vcomposer));
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  return (visible >= GST_VCOMPOSER_MAX_QUEUE_LEN) ? TRUE : FALSE;
}

static void
gst_video_composer_class_init (GstVideoComposerClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *aggregator = GST_AGGREGATOR_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_video_composer_debug, "qtivcomposer", 0,
      "QTI video composer");

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_composer_finalize);
  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_composer_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_composer_get_property);

  g_object_class_install_property (gobject, PROP_ENGINE_BACKEND,
      g_param_spec_enum ("engine", "Engine",
          "Engine backend used for the conversion operations",
          GST_TYPE_VCE_BACKEND, DEFAULT_PROP_ENGINE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_BACKGROUND,
      g_param_spec_uint ("background", "Background",
          "Background color", 0, 0xFFFFFFFF, DEFAULT_PROP_BACKGROUND,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  gst_element_class_set_static_metadata (element,
      "Video composer", "Filter/Editor/Video/Compositor/Scaler",
      "Mix together multiple video streams", "QTI");

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_composer_sink_template, GST_TYPE_VIDEO_COMPOSER_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_composer_src_template, GST_TYPE_AGGREGATOR_PAD);

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_video_composer_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_video_composer_release_pad);

  aggregator->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_composer_propose_allocation);
  aggregator->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_composer_decide_allocation);
  aggregator->sink_query = GST_DEBUG_FUNCPTR (gst_video_composer_sink_query);
  aggregator->sink_event = GST_DEBUG_FUNCPTR (gst_video_composer_sink_event);
  aggregator->src_event = GST_DEBUG_FUNCPTR (gst_video_composer_src_event);
  aggregator->src_query = GST_DEBUG_FUNCPTR (gst_video_composer_src_query);
  aggregator->update_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_update_src_caps);
  aggregator->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_fixate_src_caps);
  aggregator->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_negotiated_src_caps);
  aggregator->start = GST_DEBUG_FUNCPTR (gst_video_composer_start);
  aggregator->stop = GST_DEBUG_FUNCPTR (gst_video_composer_stop);
  aggregator->get_next_time =
      GST_DEBUG_FUNCPTR (gst_video_composer_get_next_time);
  aggregator->aggregate = GST_DEBUG_FUNCPTR (gst_video_composer_aggregate);
  aggregator->flush = GST_DEBUG_FUNCPTR (gst_video_composer_flush);
}

static void
gst_video_composer_init (GstVideoComposer * vcomposer)
{
  g_mutex_init (&vcomposer->lock);
  g_rec_mutex_init (&vcomposer->worklock);

  vcomposer->n_inputs = 0;

  vcomposer->outinfo = NULL;
  vcomposer->outpool = NULL;
  vcomposer->isubwc = FALSE;

  vcomposer->duration = GST_CLOCK_TIME_NONE;

  vcomposer->worktask = NULL;
  vcomposer->requests =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, vcomposer);

  vcomposer->backend = DEFAULT_PROP_ENGINE_BACKEND;
  vcomposer->background = DEFAULT_PROP_BACKGROUND;

  GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment.position =
      GST_CLOCK_TIME_NONE;
}


static GObject *
gst_video_composer_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (proxy);
  GList *list = NULL;
  GObject *gobject = NULL;

  GST_OBJECT_LOCK (vcomposer);

  list = g_list_find_custom (GST_ELEMENT_CAST (vcomposer)->sinkpads, &index,
      (GCompareFunc) gst_video_composer_index_compare);

  if (list != NULL)
    gobject = gst_object_ref (list->data);

  GST_OBJECT_UNLOCK (vcomposer);

  return gobject;
}

static guint
gst_video_composer_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (vcomposer);
  count = GST_ELEMENT_CAST (vcomposer)->numsinkpads;
  GST_OBJECT_UNLOCK (vcomposer);

  return count;
}

static void
gst_video_composer_child_proxy_init (gpointer g_iface, gpointer data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_video_composer_child_proxy_get_child_by_index;
  iface->get_children_count = gst_video_composer_child_proxy_get_children_count;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivcomposer", GST_RANK_PRIMARY,
          GST_TYPE_VIDEO_COMPOSER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivcomposer,
    "QTI Video composer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
