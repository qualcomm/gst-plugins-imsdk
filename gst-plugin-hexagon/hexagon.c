/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hexagon.h"
#include "hexagon-module.h"

#include <stdio.h>
#include <stdlib.h>

#include <gst/video/gstimagepool.h>
#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_hexagon_debug
GST_DEBUG_CATEGORY (gst_hexagon_debug);

#define gst_hexagon_parent_class parent_class
G_DEFINE_TYPE (GstHexagon, gst_hexagon, GST_TYPE_BASE_TRANSFORM);

#define GST_HEXAGON_MODULES_PREFIX "hexagon-"

#define DEFAULT_PROP_MODULE        0

#define DEFAULT_MIN_BUFFERS        2
#define DEFAULT_MAX_BUFFERS        10

enum
{
  PROP_0,
  PROP_MODULE,
};

static GstCaps *
gst_hexagon_src_video_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL));

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_VIDEO_FORMATS_ALL));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstCaps *
gst_hexagon_src_audio_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS_ALL));
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstCaps *
gst_hexagon_sink_video_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL));

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_VIDEO_FORMATS_ALL));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstCaps *
gst_hexagon_sink_audio_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS_ALL));
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstPadTemplate *
gst_hexagon_src_template (void)
{
  GstCaps *caps = gst_caps_new_empty ();

  gst_caps_append (caps, gst_hexagon_src_video_caps ());
  gst_caps_append (caps, gst_hexagon_src_audio_caps ());

  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
}

static GstPadTemplate *
gst_hexagon_sink_template (void)
{
  GstCaps *caps = gst_caps_new_empty ();

  gst_caps_append (caps, gst_hexagon_sink_video_caps ());
  gst_caps_append (caps, gst_hexagon_sink_audio_caps ());

  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
}

static GstBufferPool *
gst_hexagon_create_image_pool (GstHexagon * hexagon, GstCaps * caps,
    GstVideoAlignment * align, GstAllocationParams * params)
{
  GstStructure *config = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info = {0,};

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (hexagon, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (hexagon, "Failed to create pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (hexagon, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (hexagon, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (hexagon, "Failed to create allocator");
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
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (hexagon, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  return pool;
}

static gboolean
gst_hexagon_propose_allocation (GstBaseTransform * base,  GstQuery * inquery,
    GstQuery * outquery)
{
  GstHexagon *hexagon = GST_HEXAGON (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *params = NULL;
  GstVideoInfo info;
  GstVideoAlignment align = { 0, };
  guint num = 0;
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
    GST_ERROR_OBJECT (hexagon, "Failed to extract caps from query!");
    return FALSE;
  }

  // check for video or audio
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (hexagon, "Failed to get video info!");
    return FALSE;
  }

  if (needpool) {
    GstStructure *structure = NULL;
    GstAllocator *allocator = NULL;

    // check video or audio
    for (num = 0; num < GST_VIDEO_INFO_N_PLANES (&info); num++)
      align.stride_align[num] = 128 - 1;

    align.padding_bottom = GST_ROUND_UP_32 (GST_VIDEO_INFO_HEIGHT (&info)) -
        GST_VIDEO_INFO_HEIGHT (&info);

    pool = gst_hexagon_create_image_pool (hexagon, caps, &align, NULL);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, info.size, 0, 0);

    gst_buffer_pool_config_get_allocator (structure, &allocator, NULL);
    gst_query_add_allocation_param (outquery, allocator, NULL);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (hexagon, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, pool, info.size, 0, 0);

  if (pool != NULL)
    gst_object_unref (pool);

  params = gst_structure_new ("GstVideoAlignment",
      "padding-top", G_TYPE_UINT, align.padding_top,
      "padding-bottom", G_TYPE_UINT, align.padding_bottom,
      "padding-left", G_TYPE_UINT, align.padding_left,
      "padding-right", G_TYPE_UINT, align.padding_right,
      "stride-align0", G_TYPE_UINT, align.stride_align[0],
      "stride-align1", G_TYPE_UINT, align.stride_align[1],
      "stride-align2", G_TYPE_UINT, align.stride_align[2],
      "stride-align3", G_TYPE_UINT, align.stride_align[3], NULL);

  gst_query_add_allocation_meta (outquery, GST_VIDEO_META_API_TYPE, params);

  return TRUE;
}

static gboolean
gst_hexagon_decide_allocation (GstBaseTransform * base, GstQuery * query)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstStructure *structure = NULL;
  guint num = 0, size = 0, minbuffers = 0, maxbuffers = 0;
  GstAllocationParams params = {};

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (hexagon, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (hexagon->outpool) {
    gst_buffer_pool_set_active (hexagon->outpool, FALSE);
    gst_clear_object (&hexagon->outpool);
  }

  structure = gst_caps_get_structure (caps, 0);

  // Create a new buffer pool.
  if (gst_structure_has_name (structure, "video/x-raw")) {
    GstVideoInfo info = { 0, };
    GstVideoAlignment align = { 0, }, ds_align = { 0, };

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (hexagon, "Invalid caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }

    for (num = 0; num < GST_VIDEO_INFO_N_PLANES (&info); num++)
      align.stride_align[num] = 128 - 1;

    align.padding_bottom = GST_ROUND_UP_32 (GST_VIDEO_INFO_HEIGHT (&info)) -
        GST_VIDEO_INFO_HEIGHT (&info);

    gst_video_info_align (&info, &align);

    if (gst_query_get_video_alignment (query, &ds_align)) {
      GST_DEBUG_OBJECT (hexagon, "Downstream alignment: padding (top: %u bottom: "
          "%u left: %u right: %u) stride (%u, %u, %u, %u)", ds_align.padding_top,
          ds_align.padding_bottom, ds_align.padding_left, ds_align.padding_right,
          ds_align.stride_align[0], ds_align.stride_align[1],
          ds_align.stride_align[2], ds_align.stride_align[3]);

      // Find the most the appropriate alignment between us and downstream.
      align = gst_video_calculate_common_alignment (&align, &ds_align);

      GST_DEBUG_OBJECT (hexagon, "Common alignment: padding (top: %u bottom: %u "
          "left: %u right: %u) stride (%u, %u, %u, %u)", align.padding_top,
          align.padding_bottom, align.padding_left, align.padding_right,
          align.stride_align[0], align.stride_align[1], align.stride_align[2],
          align.stride_align[3]);
    }

    if (gst_query_get_n_allocation_params (query))
      gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

    pool = gst_hexagon_create_image_pool (hexagon, caps, &align, NULL);
  }

  if (pool == NULL) {
    GST_ERROR_OBJECT (hexagon, "Failed to create buffer pool!");
    return FALSE;
  }

  hexagon->outpool = pool;

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

  if (GST_IS_IMAGE_BUFFER_POOL (pool))
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstCaps *
gst_hexagon_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (base);
  GstCaps *intersection = NULL, *modulecaps = NULL, *result = NULL;
  GstStructure *structure = NULL;
  GstCapsFeatures *features = NULL;
  gint idx = 0, length = 0;

  GST_DEBUG_OBJECT (hexagon, "Transforming caps %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (hexagon, "Filter caps %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC)
    result = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));
  else if (direction == GST_PAD_SINK)
    result = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (base));

  result = gst_caps_make_writable (result);
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
    if (gst_structure_has_field (structure, "pixel-aspect-ratio"))
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);

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

  GST_DEBUG_OBJECT (hexagon, "Intersection caps: %" GST_PTR_FORMAT, result);

  if (hexagon->module != NULL) {
    modulecaps = gst_hexagon_module_get_caps (hexagon->module);
    GST_DEBUG_OBJECT (hexagon, "Module caps: %" GST_PTR_FORMAT, modulecaps);

    intersection = gst_caps_intersect_full (modulecaps, result,
        GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (hexagon, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_hexagon_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (base);
  GstCaps *result = NULL, *intersect = NULL;
  GstStructure *structure = NULL;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;

  if (gst_caps_is_fixed (outcaps)) {
    GST_DEBUG_OBJECT (hexagon, "Already fixed to %" GST_PTR_FORMAT, outcaps);
    return outcaps;
  }

  result = gst_caps_copy (incaps);

  GST_DEBUG_OBJECT (hexagon, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_HEXAGON_MODULES));
  evalue = g_enum_get_value (eclass, hexagon->mdlenum);

  switch (evalue->value) {
    case GST_HEXAGON_MODULE_UBWC_DMA:
    {
      structure = gst_caps_get_structure (result, 0);
      format = gst_video_format_from_string (
          gst_structure_get_string (structure, "format"));

      if (format == GST_VIDEO_FORMAT_NV12_Q08C) {
        gst_structure_set (structure, "format", G_TYPE_STRING, "NV12", NULL);
        GST_DEBUG_OBJECT (hexagon, "Format has been set to NV12.");
      } else if (format == GST_VIDEO_FORMAT_NV12) {
        gst_structure_set (structure, "format", G_TYPE_STRING, "NV12_Q08C", NULL);
        GST_DEBUG_OBJECT (hexagon, "Format has been set to NV12.");
      } else {
        GST_ERROR_OBJECT (hexagon, "Unsupported format: %s!",
            gst_video_format_to_string (format));
        gst_caps_unref (result);
        return NULL;
      }
      break;
    }
    default:
    {
      GST_ERROR ("No Hexagon Module has been set!");
      gst_caps_unref (result);
      return NULL;
    }
  }

  intersect = gst_caps_intersect (result, outcaps);

  // Fixate any remaining fields.
  intersect = gst_caps_fixate (intersect);

  gst_caps_unref (result);
  result = intersect;

  GST_DEBUG_OBJECT (hexagon, "Fixated caps to %" GST_PTR_FORMAT, result);

  return result;
}

static GstFlowReturn
gst_hexagon_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (base);
  GstBufferPool *pool = hexagon->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (hexagon, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (hexagon, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if ((gst_buffer_get_size (inbuffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    *outbuffer = gst_buffer_new ();
    GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
  }

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (hexagon, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_FLAGS |
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_METADATA, 0, -1);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hexagon_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  time = gst_util_get_timestamp ();

  success = gst_hexagon_module_process (hexagon->module, inbuffer, outbuffer);

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  if (!success) {
    GST_ERROR_OBJECT (hexagon, "Failed to convert data stream!");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (hexagon, "Processing took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_hexagon_change_state (GstElement * element,
    GstStateChange transition)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (element);
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      if (DEFAULT_PROP_MODULE == hexagon->mdlenum) {
        GST_ELEMENT_ERROR (hexagon, RESOURCE, NOT_FOUND, (NULL),
            ("Module name not set, automatic module pick up not supported!"));
        return GST_STATE_CHANGE_FAILURE;
      }

      eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_HEXAGON_MODULES));
      evalue = g_enum_get_value (eclass, hexagon->mdlenum);

      hexagon->module =
          gst_hexagon_module_new (GST_HEXAGON_MODULES_PREFIX, evalue->value_nick);

      if (NULL == hexagon->module) {
        GST_ELEMENT_ERROR (hexagon, RESOURCE, FAILED, (NULL),
            ("Module creation failed!"));
        return GST_STATE_CHANGE_FAILURE;
      }

      if (!gst_hexagon_module_init (hexagon->module)) {
        GST_ELEMENT_ERROR (hexagon, RESOURCE, FAILED, (NULL),
            ("Module initialization failed!"));

        gst_hexagon_module_close (hexagon->module);
        hexagon->module = NULL;

        return GST_STATE_CHANGE_FAILURE;
      }

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_ERROR_OBJECT (hexagon, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_hexagon_module_close (hexagon->module);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_hexagon_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHexagon *hexagon = GST_HEXAGON (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (hexagon);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (hexagon, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  switch (prop_id) {
    case PROP_MODULE:
      hexagon->mdlenum = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hexagon_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHexagon *hexagon = GST_HEXAGON (object);

  switch (prop_id) {
     case PROP_MODULE:
      g_value_set_enum (value, hexagon->mdlenum);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hexagon_finalize (GObject * object)
{
  GstHexagon *hexagon = GST_HEXAGON_CAST (object);

  if (hexagon->outpool != NULL)
    gst_object_unref (hexagon->outpool);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (hexagon));
}

static void
gst_hexagon_class_init (GstHexagonClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_hexagon_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_hexagon_get_property);
  gobject->finalize     =
      GST_DEBUG_FUNCPTR (gst_hexagon_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module Task name that is going to be used in Hexagon",
          GST_TYPE_HEXAGON_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Hexagon SDK data processing", "Filter/Effect/Converter",
      "Hexagon processing plugin for Hexagon Tasks", "QTI");

  gst_element_class_add_pad_template (element,
      gst_hexagon_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_hexagon_src_template ());

  element->change_state =
      GST_DEBUG_FUNCPTR (gst_hexagon_change_state);

  base->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_hexagon_propose_allocation);
  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_hexagon_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_hexagon_prepare_output_buffer);
  base->transform = GST_DEBUG_FUNCPTR (gst_hexagon_transform);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hexagon_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_hexagon_fixate_caps);

  GST_DEBUG_CATEGORY_INIT (gst_hexagon_debug, "qtihexagon", 0,
      "QTI Hexagon processing plugin");
}

static void
gst_hexagon_init (GstHexagon * hexagon)
{
  hexagon->module = NULL;
  hexagon->outpool = NULL;

  hexagon->mdlenum = DEFAULT_PROP_MODULE;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (hexagon), TRUE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtihexagon", GST_RANK_NONE,
      GST_TYPE_HEXAGON);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtihexagon,
    "QTI Hexagon plugin for data processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
