/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtivideotemplate.h"

#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video-utils.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#include <sys/mman.h>
#include <stdint.h>
#include <dlfcn.h>

#define GST_CAT_DEFAULT gst_video_template_debug
GST_DEBUG_CATEGORY_STATIC (gst_video_template_debug);

#define gst_video_template_parent_class parent_class
G_DEFINE_TYPE (GstVideoTemplate, gst_video_template, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MIN_BUFFERS      2
#define DEFAULT_PROP_MAX_BUFFERS      24

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_SINK_VIDEO_FORMATS \
  "{ NV12, NV21, YUY2, P010_10LE, NV12_10LE32, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8 }"

#define GST_SRC_VIDEO_FORMATS \
  "{ NV12, NV21, YUY2, P010_10LE, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8 }"

enum
{
  PROP_0,
  PROP_CUSTOM_LIB_NAME,
  PROP_CUSTOM_PARAMS,
};

static GstStaticCaps gst_video_template_static_sink_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_SINK_VIDEO_FORMATS));

static GstStaticCaps gst_video_template_static_src_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_SRC_VIDEO_FORMATS));


CustomCmdStatus
gst_video_template_buffer_done (GstBuffer * buf, void *priv_data)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE_CAST (priv_data);
  GST_DEBUG ("gstbuf: %p videotemplate=%p", buf, videotemplate);

  if (buf) {
    if (BUFFER_ALLOC_MODE_CUSTOM == videotemplate->buffer_alloc_mode) {
      GstFlowReturn ret =
          gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (videotemplate), buf);

      if (ret == GST_FLOW_OK) {
        return CUSTOM_STATUS_OK;
      } else {
        GST_ERROR_OBJECT (videotemplate,
            "failed to push output buffer to src pad asynchronously. ret=%d",
            ret);
      }
    }
  }

  return CUSTOM_STATUS_FAIL;
}

void
gst_video_template_lock_dma_buf_for_writing (GstBuffer * buffer)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING ("DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H
}

void
gst_video_template_unlock_dma_buf_for_writing (GstBuffer * buffer)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING ("DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H
}

static GstCaps *
gst_video_template_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_video_template_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_video_template_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_video_template_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_video_template_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_video_template_sink_caps ());
}

static GstPadTemplate *
gst_video_template_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_video_template_src_caps ());
}

static GstBufferPool *
gst_video_template_create_pool (GstVideoTemplate * videotemplate,
    GstVideoAlignment * align, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (videotemplate, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_DEBUG_OBJECT (videotemplate, "caps %" GST_PTR_FORMAT, caps);

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (videotemplate, "Failed to create image pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (videotemplate, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (videotemplate, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (videotemplate, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  g_object_unref (allocator);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (align) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, align);
    gst_video_info_align (&info, align);
  }

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  GST_DEBUG_OBJECT (videotemplate, "allocator configured size %lu", info.size);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (videotemplate, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  return pool;
}

gboolean
gst_video_template_get_alignment (GstVideoTemplate * videotemplate,
    GstVideoAlignment * align_rslt, GstCaps * caps)
{
  GstVideoInfo info;
  GstVideoAlignment align = { 0, }, ds_align = { 0, };
  GstQuery *query = NULL;
  gboolean ret = FALSE;

  if (NULL == videotemplate || NULL == align_rslt || NULL == caps) {
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (videotemplate, "Invalid src caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
    GST_ERROR_OBJECT (videotemplate, "Failed to get alignment!");
    return FALSE;
  }

  query = gst_query_new_allocation (caps, FALSE);
  gst_caps_unref (caps);

  if (!gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (videotemplate), query)) {
    GST_ERROR_OBJECT (videotemplate, "failed to query source pad");
    return FALSE;
  }

  ret = gst_query_get_video_alignment (query, &ds_align);
  gst_query_unref (query);

  if (!ret) {
    GST_ERROR_OBJECT (videotemplate, "failed to get video alignment");
    return FALSE;
  }

  GST_DEBUG_OBJECT (videotemplate,
      "Downstream alignment: padding (top: %u bottom: "
      "%u left: %u right: %u) stride (%u, %u, %u, %u)", ds_align.padding_top,
      ds_align.padding_bottom, ds_align.padding_left, ds_align.padding_right,
      ds_align.stride_align[0], ds_align.stride_align[1],
      ds_align.stride_align[2], ds_align.stride_align[3]);

  // Find the most the appropriate alignment between us and downstream.
  *align_rslt = gst_video_calculate_common_alignment (&align, &ds_align);

  GST_DEBUG_OBJECT (videotemplate,
      "Common alignment: padding (top: %u bottom: %u "
      "left: %u right: %u) stride (%u, %u, %u, %u)", align_rslt->padding_top,
      align_rslt->padding_bottom, align_rslt->padding_left, align_rslt->padding_right,
      align_rslt->stride_align[0], align_rslt->stride_align[1],
      align_rslt->stride_align[2], align_rslt->stride_align[3]);

  return TRUE;
}

void
gst_video_template_allocate_outbuffer (GstBuffer ** outbuffer, void *priv_data)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE_CAST (priv_data);
  GstBufferPool *pool = NULL;

  static gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    gboolean ret_align = FALSE;
    GstVideoAlignment align;
    GstCaps *out_caps =
        gst_pad_get_current_caps (GST_BASE_TRANSFORM_SRC_PAD (videotemplate));
    ret_align =
        gst_video_template_get_alignment (videotemplate, &align, out_caps);
    videotemplate->outpool =
        gst_video_template_create_pool (videotemplate,
        ret_align ? &align : NULL, out_caps);

    if (videotemplate->outpool == NULL) {
      GST_ERROR_OBJECT (videotemplate, "Failed to create output buffer pool");
    }

    g_once_init_leave (&inited, 1);
  }

  pool = videotemplate->outpool;
  *outbuffer = NULL;

  if (NULL == pool) {
    GST_ERROR_OBJECT (videotemplate, "Ouptut video buffer pool is NULL");
    return;
  }

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (videotemplate,
        "Failed to activate output video buffer pool!");
    return;
  }

  if (gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (videotemplate,
        "Failed to create output video buffer for async!");
    return;
  }
}

static void
get_dimensions_from_caps (GstStructure * structure, VideoCfgRanges * query)
{
  if (!structure || !query) {
    GST_ERROR ("invalid arguments");
    return;
  }

  query->min_width = query->max_width = -1;
  query->min_height = query->max_height = -1;
  query->formats[0] = 0;

  const GValue *width_value = gst_structure_get_value (structure, "width");
  const GValue *height_value = gst_structure_get_value (structure, "height");

  if (width_value) {
    if (G_VALUE_HOLDS_INT (width_value)) {
      query->min_width = g_value_get_int (width_value);
      query->max_width = query->min_width;
      GST_DEBUG ("Width: %d", query->min_width);
    } else if (GST_VALUE_HOLDS_INT_RANGE (width_value)) {
      query->min_width = gst_value_get_int_range_min (width_value);
      query->max_width = gst_value_get_int_range_max (width_value);
      GST_DEBUG ("Range width: %d %d", query->min_width, query->max_width);
    }
  }

  if (height_value) {
    if (G_VALUE_HOLDS_INT (height_value)) {
      query->min_height = g_value_get_int (height_value);
      query->max_height = query->min_height;
      GST_DEBUG ("Height: %d", query->min_height);
    } else if (GST_VALUE_HOLDS_INT_RANGE (height_value)) {
      query->min_height = gst_value_get_int_range_min (height_value);
      query->max_height = gst_value_get_int_range_max (height_value);
      GST_DEBUG ("Range height: %d %d", query->min_height, query->max_height);
    }
  }

  const GValue *format_value = gst_structure_get_value (structure, "format");

  if (format_value) {
    if (GST_VALUE_HOLDS_LIST (format_value)) {
      gint length = gst_value_list_get_size (format_value);
      gboolean is_first_fmt = TRUE;

      for (int idx = 0; idx < length; idx++) {
        const GValue *out_fmt_value =
            gst_value_list_get_value (format_value, idx);

        if (G_VALUE_HOLDS_STRING (out_fmt_value)) {
          const char *out_fmt_str = g_value_get_string (out_fmt_value);

          if (FALSE == is_first_fmt) {
            g_strlcat (query->formats, ",", sizeof (query->formats));
          }
          g_strlcat (query->formats, out_fmt_str, sizeof (query->formats));
          is_first_fmt = FALSE;
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format_value)) {
      const char *out_fmt_str = g_value_get_string (format_value);
      g_strlcat (query->formats, out_fmt_str, sizeof (query->formats));
    }
  }
}

static void
gst_video_template_update_gst_struct (GstStructure * structure,
    VideoCfgRanges * result_transform)
{

  if (NULL == result_transform) {
    return;
  }

  // Set width and height to a range instead of fixed value
  if (result_transform->min_width != result_transform->max_width &&
      result_transform->min_height != result_transform->max_height) {
    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE,
        result_transform->min_width, result_transform->max_width,
        "height", GST_TYPE_INT_RANGE,
        result_transform->min_height, result_transform->max_height, NULL);
  } else {
    gst_structure_set (structure,
        "width", G_TYPE_INT, result_transform->min_width,
        "height", G_TYPE_INT, result_transform->min_height, NULL);
  }

  if (0 == result_transform->formats[0]) {
    gst_structure_remove_fields (structure, "format", NULL);
  } else {
    char *token = NULL, *remaining = result_transform->formats;
    GValue formats = G_VALUE_INIT;
    g_value_init (&formats, GST_TYPE_LIST);

    while ((token = strtok_r (remaining, ",", &remaining))) {
      GValue format = G_VALUE_INIT;
      g_value_init (&format, G_TYPE_STRING);
      g_value_set_string (&format, token);
      gst_value_list_append_value (&formats, &format);
      g_value_unset (&format);
    }

    // Add the formats to the GstStructure
    gst_structure_set_value (structure, "format", &formats);
    g_value_unset (&formats);
  }
}

/**
 * @param base
 * @param direction
 * @param caps: These are the specific capabilities of the pad
 *            in the given direction (src/sink)
 *
 * @param filter: These are the constraints or possible E
 *              capabilities that the OTHER PAD can handle
 * @return GstCaps*
 */
static GstCaps *
gst_video_template_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE (base);
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GstCapsFeatures *features = NULL;
  gint idx = 0, length = 0;

  GST_DEBUG_OBJECT (videotemplate, "Transforming caps %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  if (NULL == videotemplate->customlib_query_possible_srcpad_cfgs) {
    GST_ERROR_OBJECT (videotemplate,
        "transform_caps failed: query_possible_srcpad_cfgs undefined");
    return NULL;
  }

  if (NULL == videotemplate->customlib_query_possible_sinkpad_cfgs) {
    GST_ERROR_OBJECT (videotemplate,
        "transform_caps failed: query_possible_sinkpad_cfgs undefined");
    return NULL;
  }

  result = gst_caps_new_empty ();

  // In case there is no memory:GBM caps structure prepend one.
  if (!gst_caps_is_empty (caps) &&
      !gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    VideoCfgRanges query_transform, result_transform;
    structure = gst_caps_get_structure (caps, 0);
    features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL);

    if (!features) {
      GST_ERROR_OBJECT (videotemplate, "Failed to create caps features");
      gst_caps_unref (result);
      return NULL;
    }

    get_dimensions_from_caps (structure, &query_transform);

    if (GST_PAD_SINK == direction) {
      (*videotemplate->customlib_query_possible_srcpad_cfgs) (&query_transform,
          &result_transform);
    } else {
      (*videotemplate->customlib_query_possible_sinkpad_cfgs) (&query_transform,
          &result_transform);
    }

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    if (!structure) {
      GST_ERROR_OBJECT (videotemplate, "Failed to copy structure");
      gst_caps_features_free (features);
      gst_caps_unref (result);
      return NULL;
    }

    gst_video_template_update_gst_struct (structure, &result_transform);

    gst_caps_append_structure_full (result, structure, features);
  }

  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    VideoCfgRanges query_transform, result_transform;

    structure = gst_caps_get_structure (caps, idx);
    features = gst_caps_get_features (caps, idx);

    get_dimensions_from_caps (structure, &query_transform);

    if (GST_PAD_SINK == direction) {
      (*videotemplate->customlib_query_possible_srcpad_cfgs) (&query_transform,
          &result_transform);
    } else {
      (*videotemplate->customlib_query_possible_sinkpad_cfgs) (&query_transform,
          &result_transform);
    }

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    gst_video_template_update_gst_struct (structure, &result_transform);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));

  }

  // In case there is no featureless caps structure append one.
  if (!gst_caps_is_empty (caps) && !gst_caps_has_feature (caps, NULL)) {
    VideoCfgRanges query_transform, result_transform;

    structure = gst_caps_get_structure (caps, 0);

    get_dimensions_from_caps (structure, &query_transform);

    if (GST_PAD_SINK == direction) {
      (*videotemplate->customlib_query_possible_srcpad_cfgs) (&query_transform,
          &result_transform);
    } else {
      (*videotemplate->customlib_query_possible_sinkpad_cfgs) (&query_transform,
          &result_transform);
    }

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    gst_video_template_update_gst_struct (structure, &result_transform);

    gst_caps_append_structure (result, structure);
  }

  if (filter) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (videotemplate, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_video_template_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE (base);
  GstStructure *input, *output;
  const GstVideoFormatInfo *outinfo = NULL;
  VideoCfgRanges input_struct, output_struct;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  // Take a copy of the input caps structure so we can freely modify it.
  input = gst_caps_get_structure (incaps, 0);
  input = gst_structure_copy (input);

  GST_DEBUG_OBJECT (videotemplate,
      "Trying to fixate output caps\n    %" GST_PTR_FORMAT
      " based on caps\n   %" GST_PTR_FORMAT " \nin PadDirection %s",
      outcaps, incaps, direction == GST_PAD_SRC ? "SRC" : "SINK");

  get_dimensions_from_caps (input, &input_struct);
  get_dimensions_from_caps (output, &output_struct);

  if (gst_structure_has_field (input, "colorimetry")) {
    const gchar *string = gst_structure_get_string (input, "colorimetry");

    if (gst_structure_has_field (output, "colorimetry"))
      gst_structure_fixate_field_string (output, "colorimetry", string);
    else
      gst_structure_set (output, "colorimetry", G_TYPE_STRING, string, NULL);
  }

  if (gst_structure_has_field (input, "chroma-site")) {
    const gchar *string = gst_structure_get_string (input, "chroma-site");

    if (gst_structure_has_field (output, "chroma-site"))
      gst_structure_fixate_field_string (output, "chroma-site", string);
    else
      gst_structure_set (output, "chroma-site", G_TYPE_STRING, string, NULL);
  }

  if (gst_structure_has_field (input, "compression")) {
    const gchar *string = gst_structure_get_string (input, "compression");

    if (gst_structure_has_field (output, "compression"))
      gst_structure_fixate_field_string (output, "compression", string);
    else
      gst_structure_set (output, "compression", G_TYPE_STRING, string, NULL);
  }

  VideoCfg result;

  if (NULL == videotemplate->customlib_select_src_pad_cfg) {
    GST_ERROR ("customlib_select_src_pad_cfg not defined");
  } else {
    (*videotemplate->customlib_select_src_pad_cfg) (videotemplate->custom_lib,
        &input_struct, &output_struct, &result);

    gst_structure_set (output, "width", G_TYPE_INT, result.selected_width,
        "height", G_TYPE_INT, result.selected_height, NULL);

    outinfo =
        gst_video_format_get_info (gst_video_format_from_string
        (result.selected_format));
    GST_DEBUG ("selected_format='%s'", result.selected_format);

    if (outinfo != NULL) {
      gst_structure_fixate_field_string (output, "format",
          GST_VIDEO_FORMAT_INFO_NAME (outinfo));
    } else {
      GST_ERROR_OBJECT (videotemplate, "Failed to fixate format");
    }
  }

  // Remove compression field if caps do not contain memory:GBM feature.
  if (!gst_caps_has_feature (outcaps, GST_CAPS_FEATURE_MEMORY_GBM))
    gst_structure_remove_field (output, "compression");

  // Free the local copy of the input caps structure.
  gst_structure_free (input);

  GST_DEBUG_OBJECT (videotemplate, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_video_template_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE (base);
  GstVideoInfo ininfo, outinfo;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (videotemplate,
        "Failed to get input video info from caps!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&outinfo, outcaps)) {
    GST_ERROR_OBJECT (videotemplate,
        "Failed to get output video info from caps!");
    return FALSE;
  }

  if (NULL == videotemplate->custom_lib) {
    GST_ERROR_OBJECT (videotemplate, "Failed to create custom_lib");
    return FALSE;
  }

  (*videotemplate->customlib_set_cfg) (videotemplate->custom_lib, &ininfo,
      &outinfo);

  (*videotemplate->
      customlib_query_buffer_alloc_mode) (videotemplate->custom_lib,
      &videotemplate->buffer_alloc_mode);

  GST_INFO_OBJECT (videotemplate,
      "buffer_alloc_mode=%d", videotemplate->buffer_alloc_mode);

  return TRUE;
}

static void
gst_video_template_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (videotemplate);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (videotemplate,
        "Property '%s' change not supported in %s " "state!", propname,
        gst_element_state_get_name (state));
    return;
  }

  switch (prop_id) {
    case PROP_CUSTOM_LIB_NAME:
    {
      const gchar *customlib_name = g_value_get_string (value);

      if (customlib_name) {
        g_strlcpy (videotemplate->customlib_name,
            customlib_name, sizeof (videotemplate->customlib_name));

        void *handle = dlopen (customlib_name, RTLD_LAZY);

        if (NULL == handle) {
          GST_ERROR_OBJECT (videotemplate,
              "failed to load '%s' error:%s", customlib_name, dlerror ());
          return;
        }

        videotemplate->custom_lib_handle = handle;

        GST_INFO_OBJECT (videotemplate,
            "Successfully loaded '%s'", customlib_name);

        *(void **) (&videotemplate->customlib_create_handle) =
            dlsym (handle, "custom_create_handle");

        if (NULL == videotemplate->customlib_create_handle)
          GST_ERROR_OBJECT (videotemplate,
              "custom_create_handle error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_set_custom_params) =
            dlsym (handle, "custom_set_custom_params");

        if (NULL == videotemplate->customlib_set_custom_params)
          GST_ERROR_OBJECT (videotemplate,
              "custom_init_custom_param error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_query_possible_srcpad_cfgs) =
            dlsym (handle, "custom_query_possible_srcpad_cfgs");

        if (NULL == videotemplate->customlib_query_possible_srcpad_cfgs)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_query_possible_srcpad_cfgs error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_query_possible_sinkpad_cfgs) =
            dlsym (handle, "custom_query_possible_sinkpad_cfgs");

        if (NULL == videotemplate->customlib_query_possible_sinkpad_cfgs)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_query_possible_sinkpad_cfgs error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_select_src_pad_cfg) =
            dlsym (handle, "custom_query_preferred_src_pad_cfg");

        if (NULL == videotemplate->customlib_select_src_pad_cfg)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_select_src_pad_cfg error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_set_cfg) =
            dlsym (handle, "custom_set_cfg");

        if (NULL == videotemplate->customlib_set_cfg)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_set_cfg error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_query_buffer_alloc_mode) =
            dlsym (handle, "custom_query_buffer_alloc_mode");

        if (NULL == videotemplate->customlib_query_buffer_alloc_mode)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_query_buffer_alloc_mode error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_process_buffer_inplace) =
            dlsym (handle, "custom_process_buffer_inplace");

        if (NULL == videotemplate->customlib_process_buffer_inplace)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_process_buffer_inplace error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_process_buffer) =
            dlsym (handle, "custom_process_buffer");

        if (NULL == videotemplate->customlib_process_buffer)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_process_buffer error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_process_buffer_custom) =
            dlsym (handle, "custom_process_buffer_custom");

        if (NULL == videotemplate->customlib_process_buffer_custom)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_process_buffer_custom error:%s", dlerror ());

        *(void **) (&videotemplate->customlib_delete_handle) =
            dlsym (handle, "custom_delete_handle");

        if (NULL == videotemplate->customlib_delete_handle)
          GST_ERROR_OBJECT (videotemplate,
              "customlib_delete_handle error:%s", dlerror ());

        videotemplate->custom_lib = NULL;
        videotemplate->buffer_alloc_mode = BUFFER_ALLOC_MODE_INPLACE;

        if (NULL == videotemplate->custom_lib) {
          VideoTemplateCb cb;
          cb.allocate_outbuffer = gst_video_template_allocate_outbuffer;
          cb.buffer_done = gst_video_template_buffer_done;
          cb.lock_buf_for_writing = gst_video_template_lock_dma_buf_for_writing;
          cb.unlock_buf_for_writing =
              gst_video_template_unlock_dma_buf_for_writing;
          videotemplate->custom_lib =
              (*videotemplate->customlib_create_handle) (&cb, videotemplate);
        }

        if (NULL == videotemplate->custom_lib) {
          GST_ERROR_OBJECT (videotemplate, "Failed to create custom_lib");
          return;
        }

        if (videotemplate->custom_lib &&
            videotemplate->customlib_set_custom_params &&
            videotemplate->custom_params[0]) {
          GST_DEBUG_OBJECT (videotemplate, "Setting custom_params when loading"
              " custom library: %s", videotemplate->custom_params);
          (*videotemplate->
              customlib_set_custom_params) (videotemplate->custom_lib,
              videotemplate->custom_params);
        }
      }
    }
      break;

    case PROP_CUSTOM_PARAMS:
    {
      const gchar *custom_params = g_value_get_string (value);
      if (NULL == custom_params) {
        return;
      }

      g_strlcpy (videotemplate->custom_params,
          custom_params, sizeof (videotemplate->custom_params));

      GST_DEBUG_OBJECT (videotemplate,
          "Custom params: '%s'", videotemplate->custom_params);

      if (videotemplate->custom_lib &&
          videotemplate->customlib_set_custom_params) {
        (*videotemplate->
            customlib_set_custom_params) (videotemplate->custom_lib,
            videotemplate->custom_params);
      }
    }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_template_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE (object);

  switch (prop_id) {
    case PROP_CUSTOM_LIB_NAME:
      g_value_set_string (value, videotemplate->customlib_name);
      break;
    case PROP_CUSTOM_PARAMS:
      g_value_set_string (value, videotemplate->custom_params);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_template_finalize (GObject * object)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE (object);

  if (videotemplate->custom_lib) {
    (*videotemplate->customlib_delete_handle) (videotemplate->custom_lib);
    videotemplate->custom_lib = NULL;
  }

  if (videotemplate->outpool != NULL) {
    gst_object_unref (videotemplate->outpool);
    videotemplate->outpool = NULL;
  }

  if (videotemplate->custom_lib_handle) {
    GST_INFO_OBJECT (videotemplate, "Closing custom library");
    dlclose (videotemplate->custom_lib_handle);
    videotemplate->custom_lib_handle = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (videotemplate));
}

static GstFlowReturn
gst_video_template_handle_custom_mode (GstVideoTemplate * videotemplate,
    GstBuffer * buffer)
{
  if (NULL == videotemplate->customlib_process_buffer_custom) {
    GST_ERROR_OBJECT (videotemplate,
        "customlib_process_buffer_custom undefined for BUFFER_ALLOC_MODE_CUSTOM");
    return GST_FLOW_ERROR;
  }

  CustomCmdStatus status =
      (*videotemplate->
      customlib_process_buffer_custom) (videotemplate->custom_lib, buffer);

  // buffer ownership transferred to custom lib
  gst_buffer_unref (buffer);
  return (CUSTOM_STATUS_OK == status ? GST_FLOW_OK : GST_FLOW_ERROR);

}

static GstFlowReturn
gst_video_template_handle_inplace_mode (GstVideoTemplate * videotemplate,
    GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  if (NULL == videotemplate->customlib_process_buffer_inplace) {
    GST_ERROR_OBJECT (videotemplate,
        "customlib_process_buffer_inplace undefined");
    return GST_FLOW_ERROR;
  }

  CustomCmdStatus status =
      (*videotemplate->
      customlib_process_buffer_inplace) (videotemplate->custom_lib, buffer);

  if (CUSTOM_STATUS_OK != status) {
    GST_ERROR_OBJECT (videotemplate, "customlib_process_buffer_inplace failed");
    return GST_FLOW_ERROR;
  }

  ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (videotemplate), buffer);

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (videotemplate,
        "failed to synchromously push output buffer to src pad. ret=%d", ret);
    return ret;
  }

  return ret;
}

static GstFlowReturn
gst_video_template_handle_alloc_mode (GstVideoTemplate * videotemplate,
    GstBuffer * buffer)
{
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;

  gst_video_template_allocate_outbuffer (&outbuf, videotemplate);

  if (NULL == outbuf) {
    GST_ERROR_OBJECT (videotemplate, "failed to allocate output buffer");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (outbuf, buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  if (NULL == videotemplate->customlib_process_buffer) {
    GST_ERROR_OBJECT (videotemplate,
        "customlib_process_buffer undefined for BUFFER_ALLOC_MODE_ALLOC");
    return GST_FLOW_ERROR;
  }

  CustomCmdStatus status =
      (*videotemplate->customlib_process_buffer) (videotemplate->custom_lib,
      buffer, outbuf);

  if (CUSTOM_STATUS_OK != status) {
    GST_ERROR_OBJECT (videotemplate, "customlib_process_buffer failed");
    return GST_FLOW_ERROR;
  }

  gst_buffer_unref (buffer);

  ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (videotemplate), outbuf);

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (videotemplate,
        "failed to synchromously push output buffer to src pad. ret=%d", ret);
    return ret;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_video_template_generate_output (GstBaseTransform * base,
    GstBuffer ** outbuffer)
{
  GstVideoTemplate *videotemplate = GST_VIDEO_TEMPLATE_CAST (base);
  GstBuffer *buffer = base->queued_buf;
  base->queued_buf = NULL;

  (void) outbuffer;

  if (NULL == buffer) {
    GST_ERROR ("Null input buffer");
    return GST_FLOW_OK;
  }

  if (BUFFER_ALLOC_MODE_INPLACE == videotemplate->buffer_alloc_mode) {
    return gst_video_template_handle_inplace_mode (videotemplate, buffer);
  }

  if (BUFFER_ALLOC_MODE_ALLOC == videotemplate->buffer_alloc_mode) {
    return gst_video_template_handle_alloc_mode (videotemplate, buffer);
  }

  if (BUFFER_ALLOC_MODE_CUSTOM == videotemplate->buffer_alloc_mode) {
    return gst_video_template_handle_custom_mode (videotemplate, buffer);
  }

  GST_ERROR_OBJECT (videotemplate,
      "Unexpected error: Buffer alloc mode %d != ALLOC",
      videotemplate->buffer_alloc_mode);
  return GST_FLOW_ERROR;
}

static void
gst_video_template_class_init (GstVideoTemplateClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_video_template_debug, "qtivideotemplate", 0,
      "QTI video template");

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_template_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_template_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_template_finalize);

  g_object_class_install_property (gobject, PROP_CUSTOM_LIB_NAME,
      g_param_spec_string ("custom-lib-name", "Custom library name",
          "Custom library name eg \"custom-lib.so\"",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject, PROP_CUSTOM_PARAMS,
      g_param_spec_string ("custom-params", "Custom params",
          "Custom params to configure functionality",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Video template", "Hook for custom video frame processing",
      "Facilates custom library for custom video frame processing", "QTI");

  gst_element_class_add_pad_template (element,
      gst_video_template_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_video_template_src_template ());

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_video_template_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_template_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_video_template_set_caps);

  base->generate_output =
      GST_DEBUG_FUNCPTR (gst_video_template_generate_output);
}

static void
gst_video_template_init (GstVideoTemplate * videotemplate)
{
  videotemplate->custom_lib = NULL;
  videotemplate->buffer_alloc_mode = BUFFER_ALLOC_MODE_INPLACE;
  videotemplate->customlib_name[0] = 0;
  videotemplate->custom_params[0] = 0;
  videotemplate->custom_lib_handle = NULL;

  *(void **) (&videotemplate->customlib_create_handle) = NULL;
  *(void **) (&videotemplate->customlib_set_custom_params) = NULL;
  *(void **) (&videotemplate->customlib_query_possible_srcpad_cfgs) = NULL;
  *(void **) (&videotemplate->customlib_query_possible_sinkpad_cfgs) = NULL;
  *(void **) (&videotemplate->customlib_select_src_pad_cfg) = NULL;
  *(void **) (&videotemplate->customlib_set_cfg) = NULL;
  *(void **) (&videotemplate->customlib_query_buffer_alloc_mode) = NULL;
  *(void **) (&videotemplate->customlib_process_buffer_inplace) = NULL;
  *(void **) (&videotemplate->customlib_process_buffer) = NULL;
  *(void **) (&videotemplate->customlib_process_buffer_custom) = NULL;
  *(void **) (&videotemplate->customlib_delete_handle) = NULL;

  videotemplate->outpool = NULL;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivideotemplate", GST_RANK_PRIMARY,
      GST_TYPE_VIDEO_TEMPLATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivideotemplate,
    "Video template for custom processing",
    plugin_init,
    PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
