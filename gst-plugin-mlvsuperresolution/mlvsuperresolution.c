/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "mlvsuperresolution.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/video/gstimagepool.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_ml_video_super_resolution_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_super_resolution_debug);

#define gst_ml_video_super_resolution_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoSuperResolution, gst_ml_video_super_resolution,
    GST_TYPE_BASE_TRANSFORM);

#define GST_TYPE_ML_MODULES (gst_ml_modules_get_type())

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_ML_VIDEO_SUPER_RESOLUTION_VIDEO_FORMATS \
    "{ RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR }"

#define GST_ML_VIDEO_SUPER_RESOLUTION_SRC_CAPS                            \
    "video/x-raw, "                                                   \
    "format = (string) " GST_ML_VIDEO_SUPER_RESOLUTION_VIDEO_FORMATS "; " \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "                  \
    "format = (string) " GST_ML_VIDEO_SUPER_RESOLUTION_VIDEO_FORMATS

#define GST_ML_VIDEO_SUPER_RESOLUTION_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE         0
#define DEFAULT_PROP_CONSTANTS      NULL

#define DEFAULT_MIN_BUFFERS         2
#define DEFAULT_MAX_BUFFERS         10

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_CONSTANTS,
};

static GstStaticCaps gst_ml_video_super_resolution_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_SUPER_RESOLUTION_SINK_CAPS);

static GstStaticCaps gst_ml_video_super_resolution_static_src_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_SUPER_RESOLUTION_SRC_CAPS);

static GstCaps *
gst_ml_video_super_resolution_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_super_resolution_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_super_resolution_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_super_resolution_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_super_resolution_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_super_resolution_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_super_resolution_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_super_resolution_src_caps ());
}

static GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_enumarate_modules ("ml-vsuperresolution-");
  gtype = g_enum_register_static ("GstMLVideoSuperResolutionModules", variants);

  return gtype;
}

static gboolean
caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    // Skip ANY caps and return immediately if feature is present.
    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
}

static GstBufferPool *
gst_ml_video_super_resolution_create_pool (
    GstMLVideoSuperResolution * super_resolution, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;
  guint size = 0;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (super_resolution, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  // If downstream allocation query supports GBM, allocate gbm memory.
  if (caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    GST_INFO_OBJECT (super_resolution, "Uses GBM memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
  } else {
    GST_INFO_OBJECT (super_resolution, "Uses ION memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
  }

  if (NULL == pool) {
    GST_ERROR_OBJECT (super_resolution, "Failed to create buffer pool!");
    return NULL;
  }

  size = GST_VIDEO_INFO_SIZE (&info);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (super_resolution, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_video_super_resolution_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (super_resolution, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (super_resolution->outpool)
    gst_object_unref (super_resolution->outpool);

  // Create a new buffer pool.
  pool = gst_ml_video_super_resolution_create_pool (super_resolution, caps);
  if (pool == NULL) {
    GST_ERROR_OBJECT (super_resolution, "Failed to create buffer pool!");
    return FALSE;
  }

  super_resolution->outpool = pool;

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
gst_ml_video_super_resolution_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (base);
  GstBufferPool *pool = super_resolution->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_TRACE_OBJECT (super_resolution, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  } else if (gst_base_transform_is_in_place (base)) {
    GST_TRACE_OBJECT (super_resolution, "Inplace, use input buffer as output");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (super_resolution, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (super_resolution, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_video_super_resolution_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (base);
  GstCaps *tmplcaps = NULL, *result = NULL;
  guint idx = 0, num = 0, length = 0;

  GST_DEBUG_OBJECT (super_resolution, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (super_resolution, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    tmplcaps = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    tmplcaps = gst_pad_get_pad_template_caps (pad);
  }

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = NULL;
    GstCapsFeatures *features = NULL;

    for (num = 0; num < gst_caps_get_size (caps); num++) {
      const GValue *value = NULL;

      structure = gst_caps_get_structure (tmplcaps, idx);
      features = gst_caps_get_features (tmplcaps, idx);

      // Make a copy that will be modified.
      structure = gst_structure_copy (structure);

      // Extract the rate from incoming caps and propagate it to result caps.
      value = gst_structure_get_value (gst_caps_get_structure (caps, num),
          (direction == GST_PAD_SRC) ? "framerate" : "rate");

      // Skip if there is no value.
      if (value != NULL) {
        gst_structure_set_value (structure,
            (direction == GST_PAD_SRC) ? "rate" : "framerate", value);
      }

      // If this is already expressed by the existing caps skip this structure.
      if (gst_caps_is_subset_structure_full (result, structure, features)) {
        gst_structure_free (structure);
        continue;
      }

      gst_caps_append_structure_full (result, structure,
          gst_caps_features_copy (features));
    }
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (super_resolution, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_ml_video_super_resolution_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (base);
  GstStructure *output = NULL;
  GstMLInfo mlinfo;
  gint width = 0, height = 0, par_n = 1, par_d = 1;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (super_resolution, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Fixate the output format.
  value = gst_structure_get_value (output, "format");

  if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field (output, "format");
    value = gst_structure_get_value (output, "format");
  }

  GST_DEBUG_OBJECT (super_resolution, "Output format fixed to: %s",
      g_value_get_string (value));

  // Fixate output PAR if not already fixated..
  value = gst_structure_get_value (output, "pixel-aspect-ratio");

  if ((NULL == value) || !gst_value_is_fixed (value)) {
    gst_structure_set (output, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, 1, 1, NULL);
    value = gst_structure_get_value (output, "pixel-aspect-ratio");
  }

  par_d = gst_value_get_fraction_denominator (value);
  par_n = gst_value_get_fraction_numerator (value);

  if (par_n != par_d) {
    GST_ERROR_OBJECT (super_resolution,
        "Output PAR other than 1/1 not supported!");
    return NULL;
  }

  GST_DEBUG_OBJECT (super_resolution, "Output PAR fixed to: %d/%d", par_n, par_d);

  gst_ml_info_from_caps (&mlinfo, incaps);

  value = gst_structure_get_value (output, "width");

  if ((NULL == value) || !gst_value_is_fixed (value)) {
    // 2nd dimension correspond to height, 3rd dimension correspond to width.
    width = GST_ROUND_DOWN_16 (mlinfo.tensors[0][2]);

    gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
    gst_structure_get_int (output, "width", &width);
  } else {
    gst_structure_get_int (output, "width", &width);
  }

  value = gst_structure_get_value (output, "height");

  if ((NULL == value) || !gst_value_is_fixed (value)) {
    // 2nd dimension correspond to height, 3rd dimension correspond to width.
    height = mlinfo.tensors[0][1];

    gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
    gst_structure_get_int (output, "height", &height);
  } else {
    gst_structure_get_int (output, "height", &height);
  }

  GST_DEBUG_OBJECT (super_resolution, "Output width and height fixated to: %dx%d",
      width, height);

  // Fixate any remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (super_resolution, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_video_super_resolution_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (base);
  GstCaps *modulecaps = NULL;
  GstStructure *structure = NULL;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstMLInfo ininfo;
  GstVideoInfo outinfo;

  if (DEFAULT_PROP_MODULE == super_resolution->mdlenum) {
    GST_ELEMENT_ERROR (super_resolution, RESOURCE, NOT_FOUND, (NULL),
        ("Module name not set, automatic module pick up not supported!"));
    return FALSE;
  }

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_ML_MODULES));
  evalue = g_enum_get_value (eclass, super_resolution->mdlenum);

  gst_ml_module_free (super_resolution->module);
  super_resolution->module = gst_ml_module_new (evalue->value_name);

  if (NULL == super_resolution->module) {
    GST_ELEMENT_ERROR (super_resolution, RESOURCE, FAILED, (NULL),
        ("Module creation failed!"));
    return FALSE;
  }

  modulecaps = gst_ml_module_get_caps (super_resolution->module);

  if (!gst_caps_can_intersect (incaps, modulecaps)) {
    GST_ELEMENT_ERROR (super_resolution, RESOURCE, FAILED, (NULL),
        ("Module caps %" GST_PTR_FORMAT " do not intersect with the "
         "negotiated caps %" GST_PTR_FORMAT "!", modulecaps, incaps));
    return FALSE;
  }

  if (!gst_ml_module_init (super_resolution->module)) {
    GST_ELEMENT_ERROR (super_resolution, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));
    return FALSE;
  }

  structure = gst_structure_new ("options",
      GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, incaps,
      NULL);

  if (super_resolution->mlconstants != NULL) {
    gst_structure_set (structure, GST_ML_MODULE_OPT_CONSTANTS,
        GST_TYPE_STRUCTURE, super_resolution->mlconstants, NULL);
  }

  if (!gst_ml_module_set_opts (super_resolution->module, structure)) {
    GST_ELEMENT_ERROR (super_resolution, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ELEMENT_ERROR (super_resolution, CORE, CAPS, (NULL),
        ("Failed to get input ML info from caps %" GST_PTR_FORMAT "!", incaps));
    return FALSE;
  }

  if (!gst_video_info_from_caps (&outinfo, outcaps)) {
    GST_ERROR_OBJECT (super_resolution, "Failed to get output video info from caps"
        " %" GST_PTR_FORMAT "!", outcaps);
    return FALSE;
  }

  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_in_place (base, FALSE);

  if (super_resolution->mlinfo != NULL)
    gst_ml_info_free (super_resolution->mlinfo);

  super_resolution->mlinfo = gst_ml_info_copy (&ininfo);

  if (GST_ML_INFO_TENSOR_DIM (super_resolution->mlinfo, 0, 0) > 1) {
    GST_ELEMENT_ERROR (super_resolution, CORE, FAILED, (NULL),
        ("Batched input tensors with video output is not supported!"));
    return FALSE;
  }

  if (super_resolution->vinfo != NULL)
    gst_video_info_free (super_resolution->vinfo);

  super_resolution->vinfo = gst_video_info_copy (&outinfo);

  GST_DEBUG_OBJECT (super_resolution, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (super_resolution, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_super_resolution_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (base);
  GstMLFrame mlframe = { 0, };
  GstVideoFrame vframe = { 0, };
  gboolean success = FALSE;

  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  g_return_val_if_fail (super_resolution->module != NULL, GST_FLOW_ERROR);

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  ts_begin = gst_util_get_timestamp ();

  if (!gst_ml_frame_map (&mlframe, super_resolution->mlinfo, inbuffer,
      GST_MAP_READ)) {
    GST_ERROR_OBJECT (super_resolution, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&vframe, super_resolution->vinfo, outbuffer,
      GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (super_resolution, "Failed to map output buffer!");
    gst_ml_frame_unmap (&mlframe);
    return GST_FLOW_ERROR;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (super_resolution, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  // Call the submodule process funtion.
  success = gst_ml_module_video_super_resolution_execute (
      super_resolution->module, &mlframe, &vframe);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (super_resolution, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  gst_video_frame_unmap (&vframe);
  gst_ml_frame_unmap (&mlframe);

  if (!success) {
    GST_ERROR_OBJECT (super_resolution, "Failed to process tensors!");
    return GST_FLOW_ERROR;
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (super_resolution, "super_resolution took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_super_resolution_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (object);

  switch (prop_id) {
    case PROP_MODULE:
      super_resolution->mdlenum = g_value_get_enum (value);
      break;
    case PROP_CONSTANTS:
    {
      const gchar *input = g_value_get_string (value);
      GValue value = G_VALUE_INIT;

      g_value_init (&value, GST_TYPE_STRUCTURE);

      if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
        GString *string = NULL;
        GError *error = NULL;
        gchar *contents = NULL;
        gboolean success = FALSE;

        if (!g_file_get_contents (input, &contents, NULL, &error)) {
          GST_ERROR ("Failed to get file contents, error: %s!",
              GST_STR_NULL (error->message));
          g_clear_error (&error);
          break;
        }

        // Remove trailing space and replace new lines with a comma delimiter.
        contents = g_strstrip (contents);
        contents = g_strdelimit (contents, "\n", ',');

        string = g_string_new (contents);
        g_free (contents);

        // Add opening and closing brackets.
        string = g_string_prepend (string, "{ ");
        string = g_string_append (string, " }");

        // Get the raw character data.
        contents = g_string_free (string, FALSE);

        success = gst_value_deserialize (&value, contents);
        g_free (contents);

        if (!success) {
          GST_ERROR ("Failed to deserialize file contents!");
          break;
        }
      } else if (!gst_value_deserialize (&value, input)) {
        GST_ERROR ("Failed to deserialize string!");
        break;
      }

      if (super_resolution->mlconstants != NULL)
        gst_structure_free (super_resolution->mlconstants);

      super_resolution->mlconstants = GST_STRUCTURE (g_value_dup_boxed (&value));
      g_value_unset (&value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_super_resolution_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_value_set_enum (value, super_resolution->mdlenum);
      break;
    case PROP_CONSTANTS:
    {
      gchar *string = NULL;

      if (super_resolution->mlconstants != NULL)
        string = gst_structure_to_string (super_resolution->mlconstants);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_super_resolution_finalize (GObject * object)
{
  GstMLVideoSuperResolution *super_resolution = GST_ML_VIDEO_SUPER_RESOLUTION (object);

  gst_ml_module_free (super_resolution->module);

  if (super_resolution->mlinfo != NULL)
    gst_ml_info_free (super_resolution->mlinfo);

  if (super_resolution->vinfo != NULL)
    gst_video_info_free (super_resolution->vinfo);

  if (super_resolution->outpool != NULL)
    gst_object_unref (super_resolution->outpool);

  if (super_resolution->mlconstants != NULL)
    gst_structure_free (super_resolution->mlconstants);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (super_resolution));
}

static void
gst_ml_video_super_resolution_class_init (GstMLVideoSuperResolutionClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          GST_TYPE_ML_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CONSTANTS,
      g_param_spec_string ("constants", "Constants",
          "Constants, offsets and coefficients used by the chosen module for "
          "post-processing of incoming tensors in GstStructure string format. "
          "Applicable only for some modules.",
          DEFAULT_PROP_CONSTANTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning image super resolution", "Filter/Effect/Converter",
      "Machine Learning plugin for image super_resolution", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_super_resolution_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_super_resolution_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_transform_caps);
  base->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_super_resolution_transform);
}

static void
gst_ml_video_super_resolution_init (GstMLVideoSuperResolution * super_resolution)
{
  super_resolution->outpool = NULL;
  super_resolution->module = NULL;
  super_resolution->mdlenum = DEFAULT_PROP_MODULE;
  super_resolution->mlconstants = DEFAULT_PROP_CONSTANTS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (super_resolution), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_super_resolution_debug,
      "qtimlvsuperresolution", 0, "QTI ML image super resolution plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvsuperresolution", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_SUPER_RESOLUTION);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvsuperresolution,
    "QTI Machine Learning plugin for image super resolution post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
