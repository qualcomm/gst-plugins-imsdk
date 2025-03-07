/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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

#include "videotransform.h"

#include <gst/video/gstqtibufferpool.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_video_transform_debug
GST_DEBUG_CATEGORY_STATIC (gst_video_transform_debug);

#define gst_video_transform_parent_class parent_class
G_DEFINE_TYPE (GstVideoTransform, gst_video_transform, GST_TYPE_BASE_TRANSFORM);

#define GST_TYPE_VIDEO_TRANSFORM_ROTATE (gst_video_trasform_rotate_get_type())

#define DEFAULT_PROP_ENGINE_BACKEND     (gst_video_converter_default_backend())
#define DEFAULT_PROP_FLIP_HORIZONTAL    FALSE
#define DEFAULT_PROP_FLIP_VERTICAL      FALSE
#define DEFAULT_PROP_ROTATE             GST_VIDEO_TRANSFORM_ROTATE_NONE
#define DEFAULT_PROP_CROP_X             0
#define DEFAULT_PROP_CROP_Y             0
#define DEFAULT_PROP_CROP_WIDTH         0
#define DEFAULT_PROP_CROP_HEIGHT        0
#define DEFAULT_PROP_DESTINATION_X      0
#define DEFAULT_PROP_DESTINATION_Y      0
#define DEFAULT_PROP_DESTINATION_WIDTH  0
#define DEFAULT_PROP_DESTINATION_HEIGHT 0
#define DEFAULT_PROP_BACKGROUND         0xFF808080

#define DEFAULT_PROP_MIN_BUFFERS      2
#define DEFAULT_PROP_MAX_BUFFERS      24

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_SINK_VIDEO_FORMATS \
  "{ NV12, NV21, YUY2, P010_10LE, NV12_10LE32, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8, NV12_Q08C }"

#define GST_SRC_VIDEO_FORMATS \
  "{ NV12, NV21, YUY2, P010_10LE, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8, NV12_Q08C }"

enum
{
  PROP_0,
  PROP_ENGINE_BACKEND,
  PROP_FLIP_HORIZONTAL,
  PROP_FLIP_VERTICAL,
  PROP_ROTATE,
  PROP_CROP,
  PROP_DESTINATION,
  PROP_BACKGROUND,
};

static GType
gst_video_trasform_rotate_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue methods[] = {
    { GST_VIDEO_TRANSFORM_ROTATE_NONE,
        "No rotation", "none"
    },
    { GST_VIDEO_TRANSFORM_ROTATE_90_CW,
        "Rotate 90 degrees clockwise", "90CW"
    },
    { GST_VIDEO_TRANSFORM_ROTATE_90_CCW,
        "Rotate 90 degrees counter-clockwise", "90CCW"
    },
    { GST_VIDEO_TRANSFORM_ROTATE_180,
        "Rotate 180 degrees", "180"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstVideoTransformRotate", methods);

  return gtype;
}

static GstCaps *
gst_video_transform_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_SINK_VIDEO_FORMATS));

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_SINK_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_video_transform_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_SRC_VIDEO_FORMATS));

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_SRC_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_video_transform_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_video_transform_sink_caps ());
}

static GstPadTemplate *
gst_video_transform_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_video_transform_src_caps ());
}

static inline GstVideoConvFlip
gst_video_transform_translate_flip (gboolean flip_h, gboolean flip_v)
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
gst_video_transform_translate_rotation (GstVideoTransformRotate rotation)
{
  switch (rotation) {
    case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      return GST_VCE_ROTATE_90;
    case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
      return GST_VCE_ROTATE_270;
    case GST_VIDEO_TRANSFORM_ROTATE_180:
      return GST_VCE_ROTATE_180;
    case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      return GST_VCE_ROTATE_0;
    default:
      GST_WARNING ("Invalid rotation flag %d!", rotation);
  }
  return GST_VCE_ROTATE_0;
}

static void
gst_video_transform_determine_passthrough (GstVideoTransform * vtrans)
{
  gboolean passthrough = TRUE;

  // Determine whether we are going to operate in passthrough mode.
  if (vtrans->ininfo != NULL && vtrans->outinfo != NULL) {
    passthrough &= vtrans->ininfo->width == vtrans->outinfo->width &&
        vtrans->ininfo->height == vtrans->outinfo->height;
    passthrough &=
        vtrans->ininfo->finfo->format == vtrans->outinfo->finfo->format;
    passthrough &= (vtrans->crop.w == 0 || vtrans->crop.h == 0) ||
        (vtrans->crop.x == 0 && vtrans->crop.y == 0 &&
        vtrans->crop.w == vtrans->ininfo->width &&
        vtrans->crop.h == vtrans->ininfo->height);
    passthrough &= (vtrans->destination.w == 0 || vtrans->destination.h == 0) ||
        (vtrans->destination.x == 0 && vtrans->destination.y == 0 &&
        vtrans->destination.w == vtrans->outinfo->width &&
        vtrans->destination.h == vtrans->outinfo->height);
  } else {
    passthrough &= vtrans->crop.w == 0 || vtrans->crop.h == 0;
    passthrough &= vtrans->destination.w == 0 || vtrans->destination.h == 0;
    passthrough &= vtrans->crop.w == 0 || vtrans->crop.h == 0;
    passthrough &= vtrans->destination.w == 0 || vtrans->destination.h == 0;
  }

  passthrough &= !vtrans->flip_h && !vtrans->flip_v;
  passthrough &= vtrans->rotation == GST_VIDEO_TRANSFORM_ROTATE_NONE;

  passthrough &= vtrans->outfeature == vtrans->infeature;

  GST_DEBUG_OBJECT (vtrans, "Passthrough has been %s",
      passthrough ? "enabled" : "disabled");

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (vtrans), passthrough);
}

static GstBufferPool *
gst_video_transform_create_pool (GstVideoTransform * vtrans, GstCaps * caps,
    GstVideoAlignment * align, GstAllocationParams * params)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vtrans, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if (gst_gbm_qcom_backend_is_supported ()) {
    // If downstream allocation query supports GBM, allocate gbm memory.
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (vtrans, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (vtrans, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    config = gst_buffer_pool_get_config (pool);

    allocator = gst_fd_allocator_new ();

    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);
  } else {
    pool = gst_qti_buffer_pool_new ();
    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, align);

    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    if (allocator == NULL) {
      GST_ERROR_OBJECT (vtrans, "Failed to create QTI allocator");
      gst_clear_object (&pool);
      return NULL;
    }
  }

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);
  gst_buffer_pool_config_set_allocator (config, allocator, params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (vtrans, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_video_transform_propose_allocation (GstBaseTransform * base,
    GstQuery * inquery, GstQuery * outquery)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM_CAST (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  gboolean needpool = FALSE;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (
        base, inquery, outquery))
    return FALSE;

  // No input query, nothing to do.
  if (NULL == inquery)
    return TRUE;

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (vtrans, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vtrans, "Failed to get video info!");
    return FALSE;
  }

  if (needpool) {
    GstStructure *structure = NULL;
    GstAllocator *allocator = NULL;
    GstVideoAlignment align = { 0, };

    if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
      GST_ERROR_OBJECT (vtrans, "Failed to get alignment!");
      return FALSE;
    }

    pool = gst_video_transform_create_pool (vtrans, caps, &align, NULL);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, info.size, 0, 0);

    gst_buffer_pool_config_get_allocator (structure, &allocator, NULL);
    gst_query_add_allocation_param (outquery, allocator, NULL);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (vtrans, "Failed to set buffer pool configuration!");
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
gst_video_transform_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM_CAST (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size = 0, minbuffers = 0, maxbuffers = 0;
  GstAllocationParams params = { 0, };
  GstVideoInfo info;
  GstVideoAlignment align = { 0, }, ds_align = { 0, };

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (vtrans, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (vtrans->outpool) {
    gst_buffer_pool_set_active (vtrans->outpool, FALSE);
    gst_clear_object (&vtrans->outpool);
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vtrans, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
    GST_ERROR_OBJECT (vtrans, "Failed to get alignment!");
    return FALSE;
  }

  if (gst_query_get_video_alignment (query, &ds_align)) {
    GST_DEBUG_OBJECT (vtrans, "Downstream alignment: padding (top: %u bottom: "
        "%u left: %u right: %u) stride (%u, %u, %u, %u)", ds_align.padding_top,
        ds_align.padding_bottom, ds_align.padding_left, ds_align.padding_right,
        ds_align.stride_align[0], ds_align.stride_align[1],
        ds_align.stride_align[2], ds_align.stride_align[3]);

    // Find the most the appropriate alignment between us and downstream.
    align = gst_video_calculate_common_alignment (&align, &ds_align);

    GST_DEBUG_OBJECT (vtrans, "Common alignment: padding (top: %u bottom: %u "
        "left: %u right: %u) stride (%u, %u, %u, %u)", align.padding_top,
        align.padding_bottom, align.padding_left, align.padding_right,
        align.stride_align[0], align.stride_align[1], align.stride_align[2],
        align.stride_align[3]);
  }

  if (gst_query_get_n_allocation_params (query))
    gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

  // Create a new buffer pool.
  pool = gst_video_transform_create_pool (vtrans, caps, &align, &params);
  vtrans->outpool = pool;

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

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

static GstFlowReturn
gst_video_transform_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM_CAST (base);
  GstBufferPool *pool = vtrans->outpool;
  gboolean passthrough = FALSE, writable = TRUE;

  // Check whether passthrough should be true/false based on parameters.
  gst_video_transform_determine_passthrough (vtrans);

  passthrough = gst_base_transform_is_passthrough (base);
  writable = gst_buffer_is_writable (inbuffer);

  // Force a copy when the buffer is not writable.
  if (passthrough && !writable) {
    GST_TRACE_OBJECT (vtrans, "Input buffer not writable, disable passthrough");
    gst_base_transform_set_passthrough (base, FALSE);
  } else if (passthrough) {
    GST_LOG_OBJECT (vtrans, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (vtrans, "Failed to activate output video buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (vtrans, "Failed to create output video buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_video_transform_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM (base);
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GstCapsFeatures *features = NULL;
  gint idx = 0, length = 0;

  GST_DEBUG_OBJECT (vtrans, "Transforming caps %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (vtrans, "Filter caps %" GST_PTR_FORMAT, filter);


  result = gst_caps_new_empty ();

  // In case there is no memory:GBM caps structure prepend one.
  if (gst_gbm_qcom_backend_is_supported () && !gst_caps_is_empty (caps) &&
      !gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    // Make a copy that will be modified.
    structure = gst_caps_get_structure (caps, 0);
    features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL);

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure_full (result, structure, features);
  }

  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (caps, idx);
    features = gst_caps_get_features (caps, idx);

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (result, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));
  }

  // In case there is no featureless caps structure append one.
  if (!gst_caps_is_empty (caps) && !gst_caps_has_feature (caps, NULL)) {
    structure = gst_caps_get_structure (caps, 0);

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure (result, structure);
  }

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (vtrans, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static gboolean
gst_video_transform_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM (base);
  const gchar *feature = NULL;
  GstVideoInfo ininfo, outinfo;
  gint in_dar_n, in_dar_d, out_dar_n, out_dar_d;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (vtrans, "Failed to get input video info from caps!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&outinfo, outcaps)) {
    GST_ERROR_OBJECT (vtrans, "Failed to get output video info from caps!");
    return FALSE;
  }

  if (!gst_util_fraction_multiply (ininfo.width, ininfo.height,
          ininfo.par_n, ininfo.par_d, &in_dar_n, &in_dar_d)) {
    GST_WARNING_OBJECT (vtrans, "Failed to calculate input DAR!");
    in_dar_n = in_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (outinfo.width, outinfo.height,
          outinfo.par_n, outinfo.par_d, &out_dar_n, &out_dar_d)) {
    GST_WARNING_OBJECT (vtrans, "Failed to calculate output DAR!");
    out_dar_n = out_dar_d = -1;
  }

  GST_DEBUG_OBJECT (vtrans, "From %dx%d (PAR: %d/%d, DAR: %d/%d), size %"
      G_GSIZE_FORMAT " -> To %dx%d (PAR: %d/%d, DAR: %d/%d), size %"
      G_GSIZE_FORMAT, ininfo.width, ininfo.height, ininfo.par_n, ininfo.par_d,
      in_dar_n, in_dar_d, ininfo.size, outinfo.width, outinfo.height,
      outinfo.par_n, outinfo.par_d, out_dar_n, out_dar_d, outinfo.size);

  if (vtrans->ininfo != NULL)
    gst_video_info_free (vtrans->ininfo);

  vtrans->ininfo = gst_video_info_copy (&ininfo);

  if (vtrans->outinfo != NULL)
    gst_video_info_free (vtrans->outinfo);

  vtrans->outinfo = gst_video_info_copy (&outinfo);

  feature = gst_caps_has_feature (incaps, GST_CAPS_FEATURE_MEMORY_GBM) ?
      GST_CAPS_FEATURE_MEMORY_GBM : NULL;
  vtrans->infeature = g_quark_from_static_string (feature);

  feature = gst_caps_has_feature (outcaps, GST_CAPS_FEATURE_MEMORY_GBM) ?
      GST_CAPS_FEATURE_MEMORY_GBM : NULL;
  vtrans->outfeature = g_quark_from_static_string (feature);

  if ((vtrans->crop.w == 0) && (vtrans->crop.h == 0)) {
    vtrans->crop.w = GST_VIDEO_INFO_WIDTH (vtrans->ininfo);
    vtrans->crop.h = GST_VIDEO_INFO_HEIGHT (vtrans->ininfo);
  }

  if ((vtrans->destination.w == 0) && (vtrans->destination.h == 0)) {
    vtrans->destination.w = GST_VIDEO_INFO_WIDTH (vtrans->outinfo);
    vtrans->destination.h = GST_VIDEO_INFO_HEIGHT (vtrans->outinfo);
  }

  if (vtrans->converter != NULL)
    gst_video_converter_engine_free (vtrans->converter);

  vtrans->converter = gst_video_converter_engine_new (vtrans->backend, NULL);

  // Disable passthrough in order to decide output allocation.
  gst_base_transform_set_passthrough (base, FALSE);
  return TRUE;
}

static void
gst_video_transform_score_format (GstVideoTransform * vtrans,
    const GstVideoFormatInfo * ininfo, const GValue * value, gint * score,
    const GstVideoFormatInfo ** outinfo)
{
  const GstVideoFormatInfo *info;
  GstVideoFormat format;
  gint l_score = 0;

  format = gst_video_format_from_string (g_value_get_string (value));
  info = gst_video_format_get_info (format);

  // Same formats, increase the score.
  l_score += (GST_VIDEO_FORMAT_INFO_FORMAT (ininfo) ==
      GST_VIDEO_FORMAT_INFO_FORMAT (info)) ? 1 : 0;

  // Same base format conversion, increase the score.
  l_score += GST_VIDEO_FORMAT_INFO_IS_YUV (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_YUV (info) ? 1 : 0;
  l_score += GST_VIDEO_FORMAT_INFO_IS_RGB (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_RGB (info) ? 1 : 0;
  l_score += GST_VIDEO_FORMAT_INFO_IS_GRAY (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_GRAY (info) ? 1 : 0;

  // Both formats have aplha channels, increase the score.
  l_score += GST_VIDEO_FORMAT_INFO_HAS_ALPHA (ininfo) &&
      GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info) ? 1 : 0;

  // Loss of color, decrease the score.
  l_score -= !(GST_VIDEO_FORMAT_INFO_IS_GRAY (ininfo)) &&
      GST_VIDEO_FORMAT_INFO_IS_GRAY (info) ? 1 : 0;

  // Loss of alpha channel, decrease the score.
  l_score -= GST_VIDEO_FORMAT_INFO_HAS_ALPHA (ininfo) &&
      !(GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info)) ? 1 : 0;

  GST_DEBUG_OBJECT (vtrans, "Score %s -> %s = %d",
      GST_VIDEO_FORMAT_INFO_NAME (ininfo),
      GST_VIDEO_FORMAT_INFO_NAME (info), l_score);

  if (l_score > *score) {
    GST_DEBUG_OBJECT (vtrans, "Found new best score %d (%s)", l_score,
        GST_VIDEO_FORMAT_INFO_NAME (info));
    *outinfo = info;
    *score = l_score;
  }
}

static void
gst_video_transform_fixate_format (GstVideoTransform *vtrans,
    GstStructure * input, GstStructure * output)
{
  const GstVideoFormatInfo *ininfo, *outinfo = NULL;
  const GValue *format = NULL, *value = NULL;
  gint idx, length, score = G_MININT;
  const gchar *infmt = NULL;
  gboolean sametype = FALSE;

  infmt = gst_structure_get_string (input, "format");
  g_return_if_fail (infmt != NULL);

  GST_DEBUG_OBJECT (vtrans, "Source format %s", infmt);

  ininfo = gst_video_format_get_info (gst_video_format_from_string (infmt));
  g_return_if_fail (ininfo != NULL);

  format = gst_structure_get_value (output, "format");
  g_return_if_fail (format != NULL);

  if (GST_VALUE_HOLDS_LIST (format)) {
    length = gst_value_list_get_size (format);

    GST_DEBUG_OBJECT (vtrans, "Have %u formats", length);

    for (idx = 0; idx < length; idx++) {
      value = gst_value_list_get_value (format, idx);

      if (G_VALUE_HOLDS_STRING (value)) {
        gst_video_transform_score_format (vtrans, ininfo, value, &score,
            &outinfo);
      } else {
        GST_WARNING_OBJECT (vtrans, "Format value has invalid type!");
      }
    }
  } else if (G_VALUE_HOLDS_STRING (format)) {
    gst_video_transform_score_format (vtrans, ininfo, format, &score,
        &outinfo);
  } else {
    GST_WARNING_OBJECT (vtrans, "Format field has invalid type!");
  }

  if (outinfo != NULL)
    gst_structure_fixate_field_string (output, "format",
        GST_VIDEO_FORMAT_INFO_NAME (outinfo));

  sametype |= GST_VIDEO_FORMAT_INFO_IS_YUV (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_YUV (outinfo);
  sametype |= GST_VIDEO_FORMAT_INFO_IS_RGB (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_RGB (outinfo);
  sametype |= GST_VIDEO_FORMAT_INFO_IS_GRAY (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_GRAY (outinfo);

  if (gst_structure_has_field (input, "colorimetry") && sametype) {
    const gchar *string = gst_structure_get_string (input, "colorimetry");

    if (gst_structure_has_field (output, "colorimetry"))
      gst_structure_fixate_field_string (output, "colorimetry", string);
    else
      gst_structure_set (output, "colorimetry", G_TYPE_STRING, string, NULL);
  }

  if (gst_structure_has_field (input, "chroma-site") && sametype) {
    const gchar *string = gst_structure_get_string (input, "chroma-site");

    if (gst_structure_has_field (output, "chroma-site"))
      gst_structure_fixate_field_string (output, "chroma-site", string);
    else
      gst_structure_set (output, "chroma-site", G_TYPE_STRING, string, NULL);
  }

  if (gst_structure_has_field (input, "compression") && sametype) {
    const gchar *string = gst_structure_get_string (input, "compression");

    if (gst_structure_has_field (output, "compression"))
      gst_structure_fixate_field_string (output, "compression", string);
    else
      gst_structure_set (output, "compression", G_TYPE_STRING, string, NULL);
  }
}

static gboolean
gst_video_transform_fill_pixel_aspect_ratio (GstVideoTransform * vtrans,
    GstPadDirection direction, GstStructure * input, GstStructure * output)
{
  const GValue *in_par, *out_par;

  in_par = gst_structure_get_value (input, "pixel-aspect-ratio");
  out_par = gst_structure_get_value (output, "pixel-aspect-ratio");

  switch (direction) {
    case GST_PAD_SRC:
      if ((NULL == in_par) || !gst_value_is_fixed (in_par))
        gst_structure_set (input, "pixel-aspect-ratio",
            GST_TYPE_FRACTION, 1, 1, NULL);

      if ((NULL == out_par) || !gst_value_is_fixed (out_par))
        gst_structure_set (output, "pixel-aspect-ratio",
            GST_TYPE_FRACTION, 1, 1, NULL);
      break;
    case GST_PAD_SINK:
      if ((NULL == in_par) || !gst_value_is_fixed (in_par))
        gst_structure_set (input, "pixel-aspect-ratio",
            GST_TYPE_FRACTION, 1, 1, NULL);

      if (NULL == out_par)
        gst_structure_set (output, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      break;
    case GST_PAD_UNKNOWN:
    default:
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Invalid or unknown pad direction!"));
      return FALSE;
  }
  return TRUE;
}

static void
gst_video_transform_fixate_width (GstVideoTransform * vtrans,
    GstStructure * input, GstStructure * output, gint out_height)
{
  const GValue *in_par, *out_par;
  gint in_par_n, in_par_d, in_dar_n, in_dar_d, in_width, in_height;
  gboolean success;

  GST_DEBUG_OBJECT (vtrans, "Output height is fixed to: %d", out_height);

  // Retrieve the PAR (pixel aspect ratio) values for the input and output.
  in_par = gst_structure_get_value (input, "pixel-aspect-ratio");
  out_par = gst_structure_get_value (output, "pixel-aspect-ratio");

  in_par_n = gst_value_get_fraction_numerator (in_par);
  in_par_d = gst_value_get_fraction_denominator (in_par);

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
        ("Error calculating the input DAR!"));
    return;
  }

  GST_DEBUG_OBJECT (vtrans, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  // PAR is fixed, choose width that is nearest to the width with the same DAR.
  if (gst_value_is_fixed (out_par)) {
    gint out_par_n, out_par_d, num, den, out_width;

    out_par_d = gst_value_get_fraction_denominator (out_par);
    out_par_n = gst_value_get_fraction_numerator (out_par);

    GST_DEBUG_OBJECT (vtrans, "Output PAR fixed to: %d/%d",
        out_par_n, out_par_d);

    // Calculate width scale factor from input DAR and output PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        out_par_d, out_par_n, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output width scale factor!"));
      return;
    }

    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        out_width = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_height, den, num));
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        out_width = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_height, num, den));
        break;
    }

    gst_structure_fixate_field_nearest_int (output, "width", out_width);
    gst_structure_get_int (output, "width", &out_width);

    GST_DEBUG_OBJECT (vtrans, "Output width fixated to: %d", out_width);
  } else {
    // PAR is not fixed, try to keep the input DAR and PAR.
    GstStructure *structure = gst_structure_copy (output);
    gint out_par_n, out_par_d, set_par_n, set_par_d, num, den, out_width;

    // Calculate output width scale factor from input DAR and PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        in_par_n, in_par_d, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output width scale factor!"));
      gst_structure_free (structure);
      return;
    }

    // Scale the output width to a value nearest to the input with same DAR
    // and adjust the output PAR if needed.
    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        out_width = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_height, den, num));

        gst_structure_fixate_field_nearest_int (structure, "width", out_width);
        gst_structure_get_int (structure, "width", &out_width);

        success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
            out_width, out_height, &out_par_n, &out_par_d);
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        out_width = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_height, num, den));

        gst_structure_fixate_field_nearest_int (structure, "width", out_width);
        gst_structure_get_int (structure, "width", &out_width);

        success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
            out_height, out_width, &out_par_n, &out_par_d);
        break;
    }

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output PAR!"));
      gst_structure_free (structure);
      return;
    }

    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", out_par_n, out_par_d);
    gst_structure_get_fraction (structure, "pixel-aspect-ratio",
        &set_par_n, &set_par_d);

    gst_structure_free (structure);

    // Validate the adjusted output PAR and update the output fields.
    if (set_par_n == out_par_n && set_par_d == out_par_d) {
      gst_structure_set (output, "width", G_TYPE_INT, out_width,
          "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (vtrans, "Output width fixated to: %d, and PAR fixated"
          " to: %d/%d", out_width, set_par_n, set_par_d);
      return;
    }

    // The above approach failed, scale the width to the new PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        set_par_d, set_par_n, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output width!"));
      return;
    }

    out_width = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_height, num, den));
    gst_structure_fixate_field_nearest_int (output, "width", out_width);
    gst_structure_get_int (structure, "width", &out_width);

    gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        set_par_n, set_par_d, NULL);

    GST_DEBUG_OBJECT (vtrans, "Output width fixated to: %d, and PAR fixated"
        " to: %d/%d", out_width, set_par_n, set_par_d);
  }

  return;
}

static void
gst_video_transform_fixate_height (GstVideoTransform * vtrans,
    GstStructure * input, GstStructure * output, gint out_width)
{
  const GValue *in_par, *out_par;
  gint in_par_n, in_par_d, in_dar_n, in_dar_d, in_width, in_height;
  gboolean success;

  GST_DEBUG_OBJECT (vtrans, "Output width is fixed to: %d", out_width);

  // Retrieve the PAR (pixel aspect ratio) values for the input and output.
  in_par = gst_structure_get_value (input, "pixel-aspect-ratio");
  out_par = gst_structure_get_value (output, "pixel-aspect-ratio");

  in_par_n = gst_value_get_fraction_numerator (in_par);
  in_par_d = gst_value_get_fraction_denominator (in_par);

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
        ("Error calculating the input DAR!"));
    return;
  }

  GST_DEBUG_OBJECT (vtrans, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  // PAR is fixed, choose height that is nearest to the height with the same DAR.
  if (gst_value_is_fixed (out_par)) {
    gint out_par_n, out_par_d, num, den, out_height;

    out_par_n = gst_value_get_fraction_numerator (out_par);
    out_par_d = gst_value_get_fraction_denominator (out_par);

    GST_DEBUG_OBJECT (vtrans, "Output PAR fixed to: %d/%d",
        out_par_n, out_par_d);

    // Calculate height from input DAR and output PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        out_par_d, out_par_n, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output width!"));
      return;
    }

    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        out_height = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_width, num, den));
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        out_height = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_width, den, num));
        break;
    }

    gst_structure_fixate_field_nearest_int (output, "height", out_height);
    gst_structure_get_int (output, "height", &out_height);

    GST_DEBUG_OBJECT (vtrans, "Output height fixated to: %d", out_height);
  } else {
    // PAR is not fixed, try to keep the input DAR and PAR.
    GstStructure *structure = gst_structure_copy (output);
    gint out_par_n, out_par_d, set_par_n, set_par_d, num, den, out_height;

    // Calculate output width scale factor from input DAR and PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        in_par_n, in_par_d, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output height scale factor!"));
      gst_structure_free (structure);
      return;
    }

    // Scale the output height to a value nearest to the input with same DAR
    // and adjust the output PAR if needed.
    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        out_height = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_width, num, den));

        gst_structure_fixate_field_nearest_int (structure, "height", out_height);
        gst_structure_get_int (structure, "height", &out_height);

        success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
            out_width, out_height, &out_par_n, &out_par_d);
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        out_height = GST_ROUND_UP_4 (
            gst_util_uint64_scale_int (out_width, den, num));

        gst_structure_fixate_field_nearest_int (structure, "height", out_height);
        gst_structure_get_int (structure, "height", &out_height);

        success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
            out_height, out_width, &out_par_n, &out_par_d);
        break;
    }

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output PAR!"));
      gst_structure_free (structure);
      return;
    }

    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", out_par_n, out_par_d);
    gst_structure_get_fraction (structure, "pixel-aspect-ratio",
        &set_par_n, &set_par_d);

    gst_structure_free (structure);

    // Validate the adjusted output PAR and update the output fields.
    if (set_par_n == out_par_n && set_par_d == out_par_d) {
      gst_structure_set (output, "height", G_TYPE_INT, out_height,
          "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (vtrans, "Output height fixated to: %d, and PAR fixated"
          " to: %d/%d", out_height, set_par_n, set_par_d);
      return;
    }

    // The above approach failed, scale the width to the new PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        set_par_d, set_par_n, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output width!"));
      return;
    }

    out_height = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_width, den, num));
    gst_structure_fixate_field_nearest_int (output, "height", out_height);
    gst_structure_get_int (output, "height", &out_height);

    gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        set_par_n, set_par_d, NULL);

    GST_DEBUG_OBJECT (vtrans, "Output height fixated to: %d, and PAR fixated"
        " to: %d/%d", out_height, set_par_n, set_par_d);
  }

  return;
}

static void
gst_video_transform_fixate_width_and_height (GstVideoTransform * vtrans,
    GstStructure * input, GstStructure * output, const GValue *out_par)
{
  gint in_par_n, in_par_d, in_dar_n, in_dar_d, in_width, in_height;
  gint out_par_n, out_par_d;
  gboolean success;

  out_par_n = gst_value_get_fraction_numerator (out_par);
  out_par_d = gst_value_get_fraction_denominator (out_par);

  GST_DEBUG_OBJECT (vtrans, "Output PAR is fixed to: %d/%d",
      out_par_n, out_par_d);

  {
    // Retrieve the PAR (pixel aspect ratio) values for the input.
    const GValue *in_par = gst_structure_get_value (input,
        "pixel-aspect-ratio");

    in_par_n = gst_value_get_fraction_numerator (in_par);
    in_par_d = gst_value_get_fraction_denominator (in_par);
  }

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
        ("Error calculating the input DAR!"));
    return;
  }

  GST_DEBUG_OBJECT (vtrans, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  {
    GstStructure *structure = gst_structure_copy (output);
    gint out_width, out_height, set_w, set_h, num, den, value;

    // Calculate output dimensions scale factor from input DAR and output PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d, out_par_n,
        out_par_d, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scale factor!"));
      gst_structure_free (structure);
      return;
    }

    // Keep the input height (because of interlacing).
    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        gst_structure_fixate_field_nearest_int (structure, "height", in_width);
        gst_structure_get_int (structure, "height", &set_h);

        // Scale width in order to keep DAR.
        set_w = GST_ROUND_UP_4 (gst_util_uint64_scale_int (set_h, den, num));
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        gst_structure_fixate_field_nearest_int (structure, "height", in_height);
        gst_structure_get_int (structure, "height", &set_h);

        // Scale width in order to keep DAR.
        set_w = GST_ROUND_UP_4 (gst_util_uint64_scale_int (set_h, num, den));
        break;
    }

    gst_structure_fixate_field_nearest_int (structure, "width", set_w);
    gst_structure_get_int (structure, "width", &value);

    // We kept the DAR and the height nearest to the original.
    if (set_w == value) {
      gst_structure_set (output, "width", G_TYPE_INT, set_w,
          "height", G_TYPE_INT, set_h, NULL);
      gst_structure_free (structure);

      GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d",
          set_w, set_h);
      return;
    }

    // Store the values from initial run, they will be used if all else fails.
    out_width = set_w;
    out_height = set_h;

    // Failed to set output width while keeping the input height, try width.
    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        gst_structure_fixate_field_nearest_int (structure, "width", in_height);
        gst_structure_get_int (structure, "width", &set_w);

        // Scale height in order to keep DAR.
        set_h = GST_ROUND_UP_4 (gst_util_uint64_scale_int (set_w, num, den));
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        gst_structure_fixate_field_nearest_int (structure, "width", in_width);
        gst_structure_get_int (structure, "width", &set_w);

        // Scale height in order to keep DAR.
        set_h = GST_ROUND_UP_4 (gst_util_uint64_scale_int (set_w, den, num));
        break;
    }

    gst_structure_fixate_field_nearest_int (structure, "height", set_h);
    gst_structure_get_int (structure, "height", &value);

    gst_structure_free (structure);

    // We kept the DAR and the width nearest to the original.
    if (set_h == value) {
      gst_structure_set (output, "width", G_TYPE_INT, set_w,
          "height", G_TYPE_INT, set_h, NULL);

      GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d",
          set_w, set_h);
      return;
    }

    // All of the above approaches failed, keep the height that was
    // nearest to the original height and the nearest possible width.
    gst_structure_set (output, "width", G_TYPE_INT, out_width,
        "height", G_TYPE_INT, out_height, NULL);

    GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d",
        out_width, out_height);
  }

  return;
}

static void
gst_video_transform_fixate_dimensions (GstVideoTransform * vtrans,
    GstStructure * input, GstStructure * output)
{
  gint in_par_n, in_par_d, in_dar_n, in_dar_d, in_width, in_height;
  gboolean success;

  {
    // Retrieve the PAR (pixel aspect ratio) values for the input.
    const GValue *in_par = gst_structure_get_value (input,
        "pixel-aspect-ratio");

    in_par_n = gst_value_get_fraction_numerator (in_par);
    in_par_d = gst_value_get_fraction_denominator (in_par);
  }

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
        ("Error calculating the input DAR!"));
    return;
  }

  GST_DEBUG_OBJECT (vtrans, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  {
    // Keep the dimensions as near as possible to the input and scale PAR.
    GstStructure *structure = gst_structure_copy (output);
    gint set_h, set_w, set_par_n, set_par_d, num, den, value;
    gint out_par_n, out_par_d, out_width, out_height;

    switch (vtrans->rotation) {
      case GST_VIDEO_TRANSFORM_ROTATE_90_CW:
      case GST_VIDEO_TRANSFORM_ROTATE_90_CCW:
        gst_structure_fixate_field_nearest_int (structure, "width", in_height);
        gst_structure_get_int (structure, "width", &out_width);

        gst_structure_fixate_field_nearest_int (structure, "height", in_width);
        gst_structure_get_int (structure, "height", &out_height);

        success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
            out_width, out_height, &out_par_n, &out_par_d);
        break;
      case GST_VIDEO_TRANSFORM_ROTATE_NONE:
      case GST_VIDEO_TRANSFORM_ROTATE_180:
        gst_structure_fixate_field_nearest_int (structure, "width", in_width);
        gst_structure_get_int (structure, "width", &out_width);

        gst_structure_fixate_field_nearest_int (structure, "height", in_height);
        gst_structure_get_int (structure, "height", &out_height);

        success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
            out_height, out_width, &out_par_n, &out_par_d);
        break;
    }

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output PAR!"));
      gst_structure_free (structure);
      return;
    }

    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", out_par_n, out_par_d);
    gst_structure_get_fraction (structure, "pixel-aspect-ratio",
        &set_par_n, &set_par_d);

    // Validate the output PAR and update the output fields.
    if (set_par_n == out_par_n && set_par_d == out_par_d) {
      gst_structure_set (output, "width", G_TYPE_INT, out_width,
          "height", G_TYPE_INT, out_height, NULL);

      gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d, and PAR"
          " fixated to: %d/%d", out_width, out_height, set_par_n, set_par_d);

      gst_structure_free (structure);
      return;
    }

    // Above failed, scale width to keep the DAR with the set PAR and height.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d, set_par_d,
        set_par_n, &num, &den);

    if (!success) {
      GST_ELEMENT_ERROR (vtrans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output width!"));
      gst_structure_free (structure);
      return;
    }

    set_w = gst_util_uint64_scale_int (out_height, num, den);
    gst_structure_fixate_field_nearest_int (structure, "width", set_w);
    gst_structure_get_int (structure, "width", &value);

    if (set_w == value) {
      gst_structure_set (output, "width", G_TYPE_INT, set_w,
          "height", G_TYPE_INT, out_height, NULL);

      gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d, and PAR"
          " fixated to: %d/%d", out_width, out_height, set_par_n, set_par_d);

      gst_structure_free (structure);
      return;
    }

    // Above failed, scale height to keep the DAR with the set PAR and width.
    set_h = gst_util_uint64_scale_int (out_width, den, num);
    gst_structure_fixate_field_nearest_int (structure, "height", set_h);
    gst_structure_get_int (structure, "height", &value);

    gst_structure_free (structure);

    if (set_h == value) {
      gst_structure_set (output, "width", G_TYPE_INT, out_width,
          "height", G_TYPE_INT, set_h, NULL);

      gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d, and PAR"
          " fixated to: %d/%d", out_width, out_height, set_par_n, set_par_d);
      return;
    }

    // All approaches failed, take the values from the 1st iteration.
    gst_structure_set (output, "width", G_TYPE_INT, out_width,
        "height", G_TYPE_INT, out_height, NULL);
    gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        out_par_n, out_par_d, NULL);

    GST_DEBUG_OBJECT (vtrans, "Output dimensions fixated to: %dx%d, and PAR"
        " fixated to: %d/%d", out_width, out_height, out_par_n, out_par_d);
  }

  return;
}

static GstCaps *
gst_video_transform_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM (base);
  GstStructure *input, *output;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  // Take a copy of the input caps structure so we can freely modify it.
  input = gst_caps_get_structure (incaps, 0);
  input = gst_structure_copy (input);

  GST_DEBUG_OBJECT (vtrans, "Trying to fixate output caps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // First fixate the output format.
  gst_video_transform_fixate_format (vtrans, input, output);

  {
    // Fill the pixel-aspect-ratio fields if they weren't set in the caps.
    gboolean success = gst_video_transform_fill_pixel_aspect_ratio (
        vtrans, direction, input, output);
    g_return_val_if_fail (success, outcaps);
  }

  {
    // Fixate output width, height and PAR.
    gint width = 0, height = 0;
    const GValue *par = NULL;

    // Retrieve the output width and height.
    gst_structure_get_int (output, "width", &width);
    gst_structure_get_int (output, "height", &height);

    // Retrieve the output PAR (pixel aspect ratio) value.
    par = gst_structure_get_value (output, "pixel-aspect-ratio");

    // Check which values are fixed and take the necessary actions.
    if ((width != 0) && (height != 0) && !gst_value_is_fixed (par)) {
      // The output dimensions are set but the PAR is not fixated.
      gst_structure_fixate_field_nearest_fraction (output,
          "pixel-aspect-ratio", 1, 1);
    } else if ((width != 0) && (height == 0)) {
      // The output width is set, try to calculate output height.
      gst_video_transform_fixate_height (vtrans, input, output, width);
    } else if ((height != 0) && (width == 0)) {
      // The output height is set, try to calculate output width.
      gst_video_transform_fixate_width (vtrans, input, output, height);
    } else if (gst_value_is_fixed (par)) {
      // The output PAR is set, try to calculate the output width and height.
      gst_video_transform_fixate_width_and_height (vtrans, input, output, par);
    } else {
      // Neither the dimensions nor the PAR are fixated at the output.
      gst_video_transform_fixate_dimensions (vtrans, input, output);
    }
  }

  // Remove compression field if caps do not contain memory:GBM feature.
  if (!gst_caps_has_feature (outcaps, GST_CAPS_FEATURE_MEMORY_GBM))
    gst_structure_remove_field (output, "compression");

  // Free the local copy of the input caps structure.
  gst_structure_free (input);

  GST_DEBUG_OBJECT (vtrans, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_video_transform_flush_converter (GstVideoTransform * vtrans)
{
  GST_DEBUG_OBJECT (vtrans, "Flush video converter");

  // Flush converter and requests queue.
  gst_video_converter_engine_flush (vtrans->converter);

  return TRUE;
}

static GstFlowReturn
gst_video_transform_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM_CAST (base);
  GstVideoFrame inframe = { 0, }, outframe = { 0, };
  GstVideoBlit blit = GST_VCE_BLIT_INIT;
  GstVideoComposition composition = GST_VCE_COMPOSITION_INIT;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  if (!gst_video_frame_map (&inframe, vtrans->ininfo, inbuffer,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (vtrans, "Failed to map input buffer!");
    return GST_FLOW_OK;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (vtrans, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  if (!gst_video_frame_map (&outframe, vtrans->outinfo, outbuffer,
          GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (vtrans, "Failed to map output buffer!");
    gst_video_frame_unmap (&inframe);
    return GST_FLOW_OK;
  }

  time = gst_util_get_timestamp ();

  GST_VIDEO_TRANSFORM_LOCK (vtrans);

  blit.frame = &inframe;

  blit.source = vtrans->crop;
  blit.destination = vtrans->destination;

  blit.flip = gst_video_transform_translate_flip (vtrans->flip_h, vtrans->flip_v);
  blit.rotate = gst_video_transform_translate_rotation (vtrans->rotation);

  composition.blits = &blit;
  composition.n_blits = 1;

  composition.frame = &outframe;
  composition.flags = 0;

  composition.bgcolor = vtrans->background;
  composition.bgfill = TRUE;


  success = gst_video_converter_engine_compose (vtrans->converter,
      &composition, 1, NULL);

  GST_VIDEO_TRANSFORM_UNLOCK (vtrans);

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (vtrans, "Conversion took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  gst_video_frame_unmap (&outframe);
  gst_video_frame_unmap (&inframe);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (vtrans, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  if (!success) {
    GST_ERROR_OBJECT (vtrans, "Failed to process composition!");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_video_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (vtrans);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (vtrans, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_VIDEO_TRANSFORM_LOCK (vtrans);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      vtrans->backend = g_value_get_enum (value);
      break;
    case PROP_FLIP_HORIZONTAL:
      vtrans->flip_h = g_value_get_boolean (value);
      break;
    case PROP_FLIP_VERTICAL:
      vtrans->flip_v = g_value_get_boolean (value);
      break;
    case PROP_ROTATE:
      vtrans->rotation = g_value_get_enum (value);
      break;
    case PROP_CROP:
    {
      guint x = 0, y = 0, width = 0, height = 0;

      g_return_if_fail (gst_value_array_get_size (value) == 4);

      x = g_value_get_int (gst_value_array_get_value (value, 0));
      y = g_value_get_int (gst_value_array_get_value (value, 1));
      width = g_value_get_int (gst_value_array_get_value (value, 2));
      height = g_value_get_int (gst_value_array_get_value (value, 3));

      if ((width == 0) || (height == 0)) {
        GST_WARNING_OBJECT (vtrans, "Invalid crop dimensions!");
        break;
      }

      vtrans->crop.x = x;
      vtrans->crop.y = y;
      vtrans->crop.w = width;
      vtrans->crop.h = height;
      break;
    }
    case PROP_DESTINATION:
    {
      guint x = 0, y = 0, width = 0, height = 0;

      g_return_if_fail (gst_value_array_get_size (value) == 4);

      x = g_value_get_int (gst_value_array_get_value (value, 0));
      y = g_value_get_int (gst_value_array_get_value (value, 1));
      width = g_value_get_int (gst_value_array_get_value (value, 2));
      height = g_value_get_int (gst_value_array_get_value (value, 3));

      if ((width == 0) || (height == 0)) {
        GST_WARNING_OBJECT (vtrans, "Invalid destination dimensions!");
        break;
      }

      vtrans->destination.x = x;
      vtrans->destination.y = y;
      vtrans->destination.w = width;
      vtrans->destination.h = height;
      break;
    }
    case PROP_BACKGROUND:
      vtrans->background = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VIDEO_TRANSFORM_UNLOCK (vtrans);
}

static void
gst_video_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM (object);

  GST_VIDEO_TRANSFORM_LOCK (vtrans);

  switch (prop_id) {
    case PROP_ENGINE_BACKEND:
      g_value_set_enum (value, vtrans->backend);
      break;
    case PROP_FLIP_HORIZONTAL:
      g_value_set_boolean (value, vtrans->flip_h);
      break;
    case PROP_FLIP_VERTICAL:
      g_value_set_boolean (value, vtrans->flip_v);
      break;
    case PROP_ROTATE:
      g_value_set_enum (value, vtrans->rotation);
      break;
    case PROP_CROP:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, vtrans->crop.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, vtrans->crop.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, vtrans->crop.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, vtrans->crop.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_DESTINATION:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, vtrans->destination.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, vtrans->destination.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, vtrans->destination.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, vtrans->destination.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_BACKGROUND:
      g_value_set_uint (value, vtrans->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VIDEO_TRANSFORM_UNLOCK (vtrans);
}

static void
gst_video_transform_finalize (GObject * object)
{
  GstVideoTransform *vtrans = GST_VIDEO_TRANSFORM (object);

  if (vtrans->converter != NULL)
    gst_video_converter_engine_free (vtrans->converter);

  if (vtrans->ininfo != NULL)
    gst_video_info_free (vtrans->ininfo);

  if (vtrans->outinfo != NULL)
    gst_video_info_free (vtrans->outinfo);

  if (vtrans->outpool != NULL)
    gst_object_unref (vtrans->outpool);

  g_mutex_clear (&(vtrans)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vtrans));
}

static void
gst_video_transform_class_init (GstVideoTransformClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_video_transform_debug, "qtivtransform", 0,
      "QTI video transform");

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_transform_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_transform_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_video_transform_finalize);

  g_object_class_install_property (gobject, PROP_ENGINE_BACKEND,
      g_param_spec_enum ("engine", "Engine",
          "Engine backend used for the conversion operations",
          GST_TYPE_VCE_BACKEND, DEFAULT_PROP_ENGINE_BACKEND,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_FLIP_HORIZONTAL,
      g_param_spec_boolean ("flip-horizontal", "Flip horizontally",
          "Flip video image horizontally", DEFAULT_PROP_FLIP_HORIZONTAL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_FLIP_VERTICAL,
      g_param_spec_boolean ("flip-vertical", "Flip vertically",
          "Flip video image vertically", DEFAULT_PROP_FLIP_VERTICAL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_ROTATE,
      g_param_spec_enum ("rotate", "Rotate", "Rotate video image",
          GST_TYPE_VIDEO_TRANSFORM_ROTATE, DEFAULT_PROP_ROTATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CROP,
      gst_param_spec_array ("crop", "Crop rectangle",
          "The crop rectangle inside the input ('<X, Y, WIDTH, HEIGHT >')",
          g_param_spec_int ("value", "Crop Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_DESTINATION,
      gst_param_spec_array ("destination", "Destination rectangle",
          "Destination rectangle inside the output ('<X, Y, WIDTH, HEIGHT >')",
          g_param_spec_int ("value", "Crop Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_BACKGROUND,
      g_param_spec_uint ("background", "Background",
          "Background color", 0, 0xFFFFFFFF, DEFAULT_PROP_BACKGROUND,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  // TODO: Temporary solution to flush cached buffers in the video converter
  // until proper solution is implemented using flush start/stop
  g_signal_new_class_handler ("flush-converter", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_video_transform_flush_converter),
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  gst_element_class_set_static_metadata (element,
      "Video transformer", "Filter/Effect/Converter/Video/Scaler",
      "Resizes, colorspace converts, flips and rotates video", "QTI");

  gst_element_class_add_pad_template (element,
      gst_video_transform_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_video_transform_src_template ());

  base->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_transform_propose_allocation);
  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_transform_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_video_transform_prepare_output_buffer);
  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_transform_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_transform_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_video_transform_set_caps);
  base->transform = GST_DEBUG_FUNCPTR (gst_video_transform_transform);
}

static void
gst_video_transform_init (GstVideoTransform * vtrans)
{
  g_mutex_init (&(vtrans)->lock);

  vtrans->backend = DEFAULT_PROP_ENGINE_BACKEND;
  vtrans->flip_h = DEFAULT_PROP_FLIP_HORIZONTAL;
  vtrans->flip_v = DEFAULT_PROP_FLIP_VERTICAL;
  vtrans->rotation = DEFAULT_PROP_ROTATE;
  vtrans->crop.x = DEFAULT_PROP_CROP_X;
  vtrans->crop.y = DEFAULT_PROP_CROP_Y;
  vtrans->crop.w = DEFAULT_PROP_CROP_WIDTH;
  vtrans->crop.h = DEFAULT_PROP_CROP_HEIGHT;
  vtrans->destination.x = DEFAULT_PROP_DESTINATION_X;
  vtrans->destination.y = DEFAULT_PROP_DESTINATION_Y;
  vtrans->destination.w = DEFAULT_PROP_DESTINATION_WIDTH;
  vtrans->destination.h = DEFAULT_PROP_DESTINATION_HEIGHT;

  vtrans->ininfo = NULL;
  vtrans->outinfo = NULL;

  vtrans->infeature = g_quark_from_static_string (NULL);
  vtrans->outfeature = g_quark_from_static_string (NULL);

  vtrans->outpool = NULL;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivtransform", GST_RANK_PRIMARY,
      GST_TYPE_VIDEO_TRANSFORM);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivtransform,
    "Resizes, colorspace converts, flips and rotates video",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
