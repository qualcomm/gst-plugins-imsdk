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

#include "videocomposer.h"

#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
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
    GST_TYPE_VIDEO_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
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

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_VIDEO_FORMATS \
  "{ NV12, NV21, UYVY, YUY2, P010_10LE, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8, NV12_Q08C }"

enum
{
  PROP_0,
  PROP_ENGINE_BACKEND,
  PROP_BACKGROUND,
};

static GstCaps *
gst_video_composer_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS));

    if (gst_gbm_qcom_backend_is_supported ()) {
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
gst_video_composer_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS));

    if (gst_gbm_qcom_backend_is_supported ()) {
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
gst_video_composer_sink_template (void)
{
  return gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      gst_video_composer_sink_caps (), GST_TYPE_VIDEO_COMPOSER_SINKPAD);
}

static GstPadTemplate *
gst_video_composer_src_template (void)
{
  return gst_pad_template_new_with_gtype ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_video_composer_src_caps (), GST_TYPE_AGGREGATOR_PAD);
}

static void
gst_video_composition_populate_output_metas (GstVideoComposer * vcomposer,
    GstVideoComposition * composition)
{
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;
  GstVideoRectangle *source = NULL, *destination = NULL;
  GstMeta *meta = NULL;
  gpointer state = NULL;
  guint idx = 0;

  outbuffer = composition->frame->buffer;

  for (idx = 0; idx < composition->n_blits; idx++) {
    inbuffer = composition->blits[idx].frame->buffer;

    source = &(composition->blits[idx].source);
    destination = &(composition->blits[idx].destination);

    while ((meta = gst_buffer_iterate_meta (inbuffer, &state))) {
      if (meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
        GstVideoRegionOfInterestMeta *roimeta = GST_VIDEO_ROI_META_CAST (meta);

        // Skip if ROI is a ImageRegion with actual data (populated by vsplit).
        // This is primarily used for blitting only pixels with actual data.
        if (roimeta->roi_type == g_quark_from_static_string ("ImageRegion"))
          continue;

        roimeta = gst_buffer_copy_video_region_of_interest_meta (outbuffer, roimeta);
        gst_video_region_of_interest_coordinates_correction (roimeta, source,
            destination);

        GST_TRACE_OBJECT (vcomposer, "Transferred 'VideoRegionOfInterest' meta "
            "with ID[0x%X] and parent ID[0x%X] to buffer %p", roimeta->id,
            roimeta->parent_id, outbuffer);
      } else if (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE) {
        GstVideoClassificationMeta *classmeta =
            GST_VIDEO_CLASSIFICATION_META_CAST (meta);

        classmeta = gst_buffer_copy_video_classification_meta (outbuffer,
            classmeta);

        GST_TRACE_OBJECT (vcomposer, "Transferred 'ImageClassification' meta "
            "with ID[0x%X] and parent ID[0x%X] to buffer %p", classmeta->id,
            classmeta->parent_id, outbuffer);
      } else if (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE) {
        GstVideoLandmarksMeta *lmkmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

        lmkmeta = gst_buffer_copy_video_landmarks_meta (outbuffer, lmkmeta);
        gst_video_landmarks_coordinates_correction (lmkmeta, source, destination);

        GST_TRACE_OBJECT (vcomposer, "Transferred 'VideoLandmarks' meta "
            "with ID[0x%X] and parent ID[0x%X] to buffer %p", lmkmeta->id,
            lmkmeta->parent_id, outbuffer);
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
gst_video_composer_create_pool (GstVideoComposer * vcomposer, GstCaps * caps,
    GstVideoAlignment * align, GstAllocationParams * params)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info = {0,};

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (vcomposer, "Failed to create image pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (vcomposer, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (vcomposer, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (vcomposer, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_allocator (config, allocator, params);
  g_object_unref (allocator);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, align);
  gst_video_info_align (&info, align);

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (vcomposer, "Failed to set pool configuration!");
    g_clear_object (&pool);
  }

  return pool;
}

static gboolean
gst_video_composer_propose_allocation (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * inquery, GstQuery * outquery)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (aggregator);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
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

  if (needpool) {
    GstStructure *structure = NULL;
    GstAllocator *allocator = NULL;
    GstVideoAlignment align = { 0, };

    if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
      GST_ERROR_OBJECT (vcomposer, "Failed to get alignment!");
      return FALSE;
    }

    pool = gst_video_composer_create_pool (vcomposer, caps, &align, NULL);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, info.size, 0, 0);

    gst_buffer_pool_config_get_allocator (structure, &allocator, NULL);
    gst_query_add_allocation_param (outquery, allocator, NULL);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (vcomposer, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, pool, info.size, 0, 0);

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
  GstVideoInfo info;
  GstVideoAlignment align = { 0, }, ds_align = { 0, };
  GstBufferPool *pool = NULL;
  guint size = 0, minbuffers = 0, maxbuffers = 0;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (vcomposer, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (vcomposer->outpool) {
    gst_buffer_pool_set_active (vcomposer->outpool, FALSE);
    gst_clear_object (&vcomposer->outpool);
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to get alignment!");
    return FALSE;
  }

  if (gst_query_get_video_alignment (query, &ds_align)) {
    GST_DEBUG_OBJECT (vcomposer, "Downstream alignment: padding (top: %u bottom:"
        " %u left: %u right: %u) stride (%u, %u, %u, %u)", ds_align.padding_top,
        ds_align.padding_bottom, ds_align.padding_left, ds_align.padding_right,
        ds_align.stride_align[0], ds_align.stride_align[1],
        ds_align.stride_align[2], ds_align.stride_align[3]);

    // Find the most the appropriate alignment between us and downstream.
    align = gst_video_calculate_common_alignment (&align, &ds_align);

    GST_DEBUG_OBJECT (vcomposer, "Common alignment: padding (top: %u bottom: "
        "%u left: %u right: %u) stride (%u, %u, %u, %u)", align.padding_top,
        align.padding_bottom, align.padding_left, align.padding_right,
        align.stride_align[0], align.stride_align[1], align.stride_align[2],
        align.stride_align[3]);
  }

  {
    GstStructure *config = NULL;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = {0,};

    if (gst_query_get_n_allocation_params (query))
      gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

    pool = gst_video_composer_create_pool (vcomposer, caps, &align, &params);

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

  vcomposer->outpool = pool;

  GST_DEBUG_OBJECT (vcomposer, "Output pool: %" GST_PTR_FORMAT,
      vcomposer->outpool);

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

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (aggregator, pad, query);
}

static GstCaps *
gst_video_composer_fixate_src_caps (GstAggregator * aggregator, GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GList *list = NULL;
  GstStructure *structure = NULL;
  const GValue *value = NULL;
  gint outwidth = 0, outheight = 0, out_fps_n = 0, out_fps_d = 0;
  guint idx = 0, length = 0;

  GST_DEBUG_OBJECT (vcomposer, "Update output caps based on caps %"
      GST_PTR_FORMAT, caps);

  GST_OBJECT_LOCK (vcomposer);

  // Extrapolate the highest width, height and frame rate from the sink pads.
  for (list = GST_ELEMENT (vcomposer)->sinkpads; list; list = list->next) {
    GstVideoComposerSinkPad *sinkpad = NULL;
    GstVideoInfo *info = NULL;
    gint width = 0, height = 0, fps_n = 0, fps_d = 0;
    gdouble fps = 0.0, outfps = 0;

    sinkpad = GST_VIDEO_COMPOSER_SINKPAD_CAST (list->data);

    if (NULL == GST_VIDEO_AGGREGATOR_PAD (sinkpad)->info.finfo) {
      GST_DEBUG_OBJECT (vcomposer, "%s caps not set!", GST_PAD_NAME (sinkpad));
      continue;
    }

    info = &(GST_VIDEO_AGGREGATOR_PAD (sinkpad)->info);

    GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

    width = (sinkpad->destination.w != 0) ?
        sinkpad->destination.w : GST_VIDEO_INFO_WIDTH (info);
    height = (sinkpad->destination.h != 0) ?
        sinkpad->destination.h : GST_VIDEO_INFO_HEIGHT (info);

    fps_n = GST_VIDEO_INFO_FPS_N (info);
    fps_d = GST_VIDEO_INFO_FPS_D (info);

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

  caps = gst_caps_make_writable (caps);
  length = gst_caps_get_size (caps);

  // Check caps structures for memory:GBM feature.
  for (idx = 0; idx < length; idx++) {
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);

    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GBM)) {
      // Found caps structure with memory:GBM feature, remove all others.
      structure = gst_caps_steal_structure (caps, idx);

      gst_caps_unref (caps);
      caps = gst_caps_new_empty ();

      gst_caps_append_structure_full (caps, structure,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
      break;
    }
  }

  // Truncate to only one set of caps.
  if (gst_caps_get_size (caps) != 1)
    caps = gst_caps_truncate (caps);

  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "width");

  if (!gst_value_is_fixed (value) && !outwidth) {
    gst_structure_fixate_field_nearest_int (structure, "width",
        DEFAULT_VIDEO_WIDTH);
    GST_DEBUG_OBJECT (vcomposer, "Width not set, using default value: %d",
        DEFAULT_VIDEO_WIDTH);
  } else if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field_nearest_int (structure, "width", outwidth);
    GST_DEBUG_OBJECT (vcomposer, "Width not set, using extrapolated width "
        "based on the sinkpads: %d", outwidth);
  } else if (g_value_get_int (value) < outwidth) {
    GST_ERROR_OBJECT (vcomposer, "Set width (%u) is not compatible with the "
        "extrapolated width (%d) from the sinkpads!", g_value_get_int (value),
        outwidth);
    return NULL;
  }

  value = gst_structure_get_value (structure, "height");

  if (!gst_value_is_fixed (value) && !outheight) {
    gst_structure_fixate_field_nearest_int (structure, "height",
        DEFAULT_VIDEO_HEIGHT);
    GST_DEBUG_OBJECT (vcomposer, "Height not set, using default value: %d",
        DEFAULT_VIDEO_HEIGHT);
  } else if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field_nearest_int (structure, "height", outheight);
    GST_DEBUG_OBJECT (vcomposer, "Height not set, using extrapolated height "
        "based on the sinkpads: %d", outheight);
  } else if (g_value_get_int (value) < outheight) {
    GST_ERROR_OBJECT (vcomposer, "Set height (%u) is not compatible with the "
        "extrapolated height (%d) from the sinkpads!", g_value_get_int (value),
        outheight);
    return NULL;
  }

  value = gst_structure_get_value (structure, "framerate");

  if (!gst_value_is_fixed (value) && (out_fps_n <= 0 || out_fps_d <= 0)) {
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
    GST_DEBUG_OBJECT (vcomposer, "Frame rate not set, using default value: "
        "%d/%d", DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
  } else if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        out_fps_n, out_fps_d);
    GST_DEBUG_OBJECT (vcomposer, "Frame rate not set, using extrapolated "
        "rate (%d/%d) from the sinkpads", out_fps_n, out_fps_d);
  } else {
    gint fps_n = gst_value_get_fraction_numerator (value);
    gint fps_d = gst_value_get_fraction_denominator (value);
    gdouble fps = 0.0, outfps = 0.0;

    gst_util_fraction_to_double (fps_n, fps_d, &fps);
    gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

    if (fps != outfps) {
      GST_ERROR_OBJECT (vcomposer, "Set framerate (%d/%d) is not compatible"
          " with the extrapolated rate (%d/%d) from the sinkpads!", fps_n,
          fps_d, out_fps_n, out_fps_d);
      return NULL;
    }
  }

  if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", 1, 1);
  } else {
    gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        1, 1, NULL);
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

  GST_DEBUG_OBJECT (vcomposer, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (vcomposer->converter != NULL)
    gst_video_converter_engine_free (vcomposer->converter);

  vcomposer->converter = gst_video_converter_engine_new (vcomposer->backend, NULL);

  return GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (aggregator, caps);
}

static gboolean
gst_video_composer_stop (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_INFO_OBJECT (vcomposer, "Flushing video converter engine");
  gst_video_converter_engine_flush (vcomposer->converter);

  return GST_AGGREGATOR_CLASS (parent_class)->stop (aggregator);
}

static GstFlowReturn
gst_video_composer_flush (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_INFO_OBJECT (vcomposer, "Flushing video converter engine");
  gst_video_converter_engine_flush (vcomposer->converter);

  return GST_AGGREGATOR_CLASS (parent_class)->flush (aggregator);
}

static GstFlowReturn
gst_video_composer_create_output_buffer (GstVideoAggregator * vaggregator,
    GstBuffer ** outbuffer)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (vaggregator);
  GstBufferPool *pool = vcomposer->outpool;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to activate output video buffer pool!");
    return GST_FLOW_ERROR;
  }

  if (gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (vcomposer, "Failed to create output video buffer!");
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (vcomposer, "Providing %" GST_PTR_FORMAT, *outbuffer);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_video_composer_aggregate_frames (GstVideoAggregator * vaggregator,
    GstBuffer * outbuffer)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (vaggregator);
  GList *list = NULL;
  GstVideoFrame outframe = {0,};
  GstVideoComposition composition = GST_VCE_COMPOSITION_INIT;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = TRUE;
  guint idx = 0, n_inputs = 0;

  // Get start time for performance measurements.
  time = gst_util_get_timestamp ();

  GST_OBJECT_LOCK (vaggregator);

  composition.n_blits = GST_ELEMENT (vcomposer)->numsinkpads;
  composition.blits = g_new0 (GstVideoBlit, composition.n_blits);

  for (list = GST_ELEMENT (vcomposer)->sinkpads; list != NULL; list = list->next) {
    GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (list->data);
    GstVideoFrame *inframe = NULL;
    GstVideoBlit *vblit = NULL;

#if GST_VERSION_MAJOR > 1 || (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR >= 16)
    inframe = gst_video_aggregator_pad_get_prepared_frame (
        GST_VIDEO_AGGREGATOR_PAD (sinkpad));
#else
    inframe = GST_VIDEO_AGGREGATOR_PAD (sinkpad)->aggregated_frame;
#endif // GST_VERSION_MAJOR > 1 || (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR >= 16)

    // GAP input buffer, nothing to do.
    if (inframe == NULL || inframe->buffer == NULL)
      continue;

    // Index to the current blit object to be populated.
    idx = n_inputs;

    vblit = &(composition.blits[idx]);
    vblit->frame = inframe;

    GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

    vblit->alpha = sinkpad->alpha * G_MAXUINT8;
    vblit->flip = gst_video_composer_translate_flip (sinkpad->flip_h, sinkpad->flip_v);
    vblit->rotate = gst_video_composer_translate_rotation (sinkpad->rotation);
    vblit->source = sinkpad->crop;
    vblit->destination = sinkpad->destination;

    GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

    if ((vblit->source.w == 0) && (vblit->source.h == 0)) {
      vblit->source.w = GST_VIDEO_FRAME_WIDTH (vblit->frame);
      vblit->source.h = GST_VIDEO_FRAME_HEIGHT (vblit->frame);
    }

    if ((vblit->destination.w == 0) && (vblit->destination.h == 0)) {
      vblit->destination.w = GST_VIDEO_INFO_WIDTH (&(vaggregator->info));
      vblit->destination.h = GST_VIDEO_INFO_HEIGHT (&(vaggregator->info));
    }

    // Increase the number of populated blit objects.
    n_inputs++;

    GST_TRACE_OBJECT (sinkpad, "Prepared %" GST_PTR_FORMAT, inframe->buffer);
  }

  GST_OBJECT_UNLOCK (vaggregator);

  // Return a GAP buffer if there are no lit objects available.
  if (n_inputs == 0) {
    gst_buffer_set_size (outbuffer, 0);
    GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_GAP);
    goto cleanup;
  }

  // Resize the blits array as actual number is less then the maximum.
  if (n_inputs < composition.n_blits)
    composition.blits = g_renew (GstVideoBlit, composition.blits, n_inputs);

  composition.n_blits = n_inputs;

  success = gst_video_frame_map (&outframe, &(vaggregator->info), outbuffer,
      GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (vcomposer, "Failed to map output buffer!");
    goto cleanup;
  }

  composition.frame = &outframe;
  composition.bgfill = TRUE;
  composition.flags = 0;

  GST_VIDEO_COMPOSER_LOCK (vcomposer);
  composition.bgcolor = vcomposer->background;
  GST_VIDEO_COMPOSER_UNLOCK (vcomposer);

  // Transfer metadata from the input buffers to the output buffer.
  gst_video_composition_populate_output_metas (vcomposer, &composition);

  success = gst_video_converter_engine_compose (vcomposer->converter,
      &composition, 1, NULL);

  if (!success) {
    GST_WARNING_OBJECT (vcomposer, "Failed to submit request to converter!");
    goto cleanup;
  }

  // Get time difference between current time and start.
  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (vcomposer, "Composition took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

cleanup:
  if (outframe.buffer != NULL)
    gst_video_frame_unmap (&outframe);

  if (composition.blits != NULL)
    g_free (composition.blits);

  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
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
  guint n_inputs = 0;

  GST_DEBUG_OBJECT (vcomposer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_OBJECT_LOCK (vcomposer);
  n_inputs = element->numsinkpads - 1;
  GST_OBJECT_UNLOCK (vcomposer);

  if (0 == n_inputs) {
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

  if (vcomposer->outpool != NULL) {
    gst_buffer_pool_set_active (vcomposer->outpool, FALSE);
    gst_object_unref (vcomposer->outpool);
  }

  g_mutex_clear (&vcomposer->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vcomposer));
}

static void
gst_video_composer_class_init (GstVideoComposerClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *aggregator = GST_AGGREGATOR_CLASS (klass);
  GstVideoAggregatorClass *vaggregator = GST_VIDEO_AGGREGATOR_CLASS (klass);

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

  gst_element_class_add_pad_template (element,
      gst_video_composer_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_video_composer_src_template ());

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_video_composer_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_video_composer_release_pad);

  aggregator->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_composer_propose_allocation);
  aggregator->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_composer_decide_allocation);
  aggregator->sink_query = GST_DEBUG_FUNCPTR (gst_video_composer_sink_query);
  aggregator->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_fixate_src_caps);
  aggregator->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_negotiated_src_caps);
  aggregator->stop = GST_DEBUG_FUNCPTR (gst_video_composer_stop);
  aggregator->flush = GST_DEBUG_FUNCPTR (gst_video_composer_flush);

#if GST_VERSION_MAJOR > 1 || (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR >= 16)
  vaggregator->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_video_composer_create_output_buffer);
#else
  vaggregator->get_output_buffer =
      GST_DEBUG_FUNCPTR (gst_video_composer_create_output_buffer);
#endif // GST_VERSION_MAJOR > 1 || (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR >= 16)

  vaggregator->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_video_composer_aggregate_frames);
}

static void
gst_video_composer_init (GstVideoComposer * vcomposer)
{
  g_mutex_init (&vcomposer->lock);

  vcomposer->outpool = NULL;

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
