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
 * Copyright (c) 2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "mlvsegmentation.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_ml_video_segmentation_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_segmentation_debug);

#define gst_ml_video_segmentation_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoSegmentation, gst_ml_video_segmentation,
    GST_TYPE_BASE_TRANSFORM);

#define GST_TYPE_ML_MODULES (gst_ml_modules_get_type())

#define GST_ML_VIDEO_SEGMENTATION_VIDEO_FORMATS \
    "{ RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR }"

#define GST_ML_VIDEO_SEGMENTATION_SRC_CAPS                            \
    "video/x-raw, "                                                   \
    "format = (string) " GST_ML_VIDEO_SEGMENTATION_VIDEO_FORMATS

#define GST_ML_VIDEO_SEGMENTATION_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE         0
#define DEFAULT_PROP_LABELS         NULL
#define DEFAULT_PROP_CONSTANTS      NULL

#define DEFAULT_MIN_BUFFERS         2
#define DEFAULT_MAX_BUFFERS         10

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_LABELS,
  PROP_CONSTANTS,
};

static GstStaticCaps gst_ml_video_segmentation_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_SEGMENTATION_SINK_CAPS);

static GstCaps *
gst_ml_video_segmentation_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_segmentation_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_segmentation_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_ML_VIDEO_SEGMENTATION_SRC_CAPS);

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_ML_VIDEO_SEGMENTATION_VIDEO_FORMATS));

      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_segmentation_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_segmentation_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_segmentation_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_segmentation_src_caps ());
}

static GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_enumarate_modules ("ml-vsegmentation-");
  gtype = g_enum_register_static ("GstMLVideoSegmentationModules", variants);

  return gtype;
}

static GstBufferPool *
gst_ml_video_segmentation_create_pool (GstMLVideoSegmentation * segmentation,
    GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info = {0,};
  GstVideoAlignment align = {0,};

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (segmentation, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (segmentation, "Failed to create image pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (segmentation, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (segmentation, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (segmentation, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  g_object_unref (allocator);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
    GST_ERROR_OBJECT (segmentation, "Failed to get alignment!");
    gst_clear_object (&pool);
    return NULL;
  }

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (segmentation, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  return pool;
}

static gboolean
gst_ml_video_segmentation_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (segmentation, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (segmentation->outpool)
    gst_object_unref (segmentation->outpool);

  // Create a new buffer pool.
  pool = gst_ml_video_segmentation_create_pool (segmentation, caps);
  if (pool == NULL) {
    GST_ERROR_OBJECT (segmentation, "Failed to create buffer pool!");
    return FALSE;
  }

  segmentation->outpool = pool;

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
gst_ml_video_segmentation_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (base);
  GstBufferPool *pool = segmentation->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_TRACE_OBJECT (segmentation, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  } else if (gst_base_transform_is_in_place (base)) {
    GST_TRACE_OBJECT (segmentation, "Inplace, use input buffer as output");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (segmentation, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (segmentation, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_video_segmentation_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (base);
  GstCaps *tmplcaps = NULL, *result = NULL;
  guint idx = 0, num = 0, length = 0;

  GST_DEBUG_OBJECT (segmentation, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (segmentation, "Filter caps: %" GST_PTR_FORMAT, filter);

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

  GST_DEBUG_OBJECT (segmentation, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_ml_video_segmentation_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (base);
  GstStructure *output = NULL;
  GstMLInfo mlinfo;
  gint width = 0, height = 0, par_n = 1, par_d = 1;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (segmentation, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Fixate the output format.
  value = gst_structure_get_value (output, "format");

  if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field (output, "format");
    value = gst_structure_get_value (output, "format");
  }

  GST_DEBUG_OBJECT (segmentation, "Output format fixed to: %s",
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
    GST_ERROR_OBJECT (segmentation, "Output PAR other than 1/1 not supported!");
    return NULL;
  }

  GST_DEBUG_OBJECT (segmentation, "Output PAR fixed to: %d/%d", par_n, par_d);

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

  GST_DEBUG_OBJECT (segmentation, "Output width and height fixated to: %dx%d",
      width, height);

  // Fixate any remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (segmentation, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_video_segmentation_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (base);
  GstCaps *modulecaps = NULL;
  GstStructure *structure = NULL;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstMLInfo ininfo;
  GstVideoInfo outinfo;

  if (NULL == segmentation->labels) {
    GST_ELEMENT_ERROR (segmentation, RESOURCE, NOT_FOUND, (NULL),
        ("Labels not set!"));
    return FALSE;
  } else if (DEFAULT_PROP_MODULE == segmentation->mdlenum) {
    GST_ELEMENT_ERROR (segmentation, RESOURCE, NOT_FOUND, (NULL),
        ("Module name not set, automatic module pick up not supported!"));
    return FALSE;
  }

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_ML_MODULES));
  evalue = g_enum_get_value (eclass, segmentation->mdlenum);

  gst_ml_module_free (segmentation->module);
  segmentation->module = gst_ml_module_new (evalue->value_name);

  if (NULL == segmentation->module) {
    GST_ELEMENT_ERROR (segmentation, RESOURCE, FAILED, (NULL),
        ("Module creation failed!"));
    return FALSE;
  }

  modulecaps = gst_ml_module_get_caps (segmentation->module);

  if (!gst_caps_can_intersect (incaps, modulecaps)) {
    GST_ELEMENT_ERROR (segmentation, RESOURCE, FAILED, (NULL),
        ("Module caps %" GST_PTR_FORMAT " do not intersect with the "
         "negotiated caps %" GST_PTR_FORMAT "!", modulecaps, incaps));
    return FALSE;
  }

  if (!gst_ml_module_init (segmentation->module)) {
    GST_ELEMENT_ERROR (segmentation, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));
    return FALSE;
  }

  structure = gst_structure_new ("options",
      GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, incaps,
      GST_ML_MODULE_OPT_LABELS, G_TYPE_STRING, segmentation->labels,
      NULL);

  if (segmentation->mlconstants != NULL) {
    gst_structure_set (structure, GST_ML_MODULE_OPT_CONSTANTS,
        GST_TYPE_STRUCTURE, segmentation->mlconstants, NULL);
  }

  if (!gst_ml_module_set_opts (segmentation->module, structure)) {
    GST_ELEMENT_ERROR (segmentation, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ELEMENT_ERROR (segmentation, CORE, CAPS, (NULL),
        ("Failed to get input ML info from caps %" GST_PTR_FORMAT "!", incaps));
    return FALSE;
  }

  if (!gst_video_info_from_caps (&outinfo, outcaps)) {
    GST_ERROR_OBJECT (segmentation, "Failed to get output video info from caps"
        " %" GST_PTR_FORMAT "!", outcaps);
    return FALSE;
  }

  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_in_place (base, FALSE);

  if (segmentation->mlinfo != NULL)
    gst_ml_info_free (segmentation->mlinfo);

  segmentation->mlinfo = gst_ml_info_copy (&ininfo);

  if (GST_ML_INFO_TENSOR_DIM (segmentation->mlinfo, 0, 0) > 1) {
    GST_ELEMENT_ERROR (segmentation, CORE, FAILED, (NULL),
        ("Batched input tensors with video output is not supported!"));
    return FALSE;
  }

  if (segmentation->vinfo != NULL)
    gst_video_info_free (segmentation->vinfo);

  segmentation->vinfo = gst_video_info_copy (&outinfo);

  GST_DEBUG_OBJECT (segmentation, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (segmentation, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_segmentation_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (base);
  GstMLFrame mlframe = { 0, };
  GstVideoFrame vframe = { 0, };
  gboolean success = FALSE;

  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  g_return_val_if_fail (segmentation->module != NULL, GST_FLOW_ERROR);

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  ts_begin = gst_util_get_timestamp ();

  if (!gst_ml_frame_map (&mlframe, segmentation->mlinfo, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (segmentation, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&vframe, segmentation->vinfo, outbuffer,
          GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (segmentation, "Failed to map output buffer!");
    gst_ml_frame_unmap (&mlframe);
    return GST_FLOW_ERROR;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (segmentation, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  // Call the submodule process funtion.
  success = gst_ml_module_video_segmentation_execute (segmentation->module,
      &mlframe, &vframe);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (segmentation, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  gst_video_frame_unmap (&vframe);
  gst_ml_frame_unmap (&mlframe);

  if (!success) {
    GST_ERROR_OBJECT (segmentation, "Failed to process tensors!");
    return GST_FLOW_ERROR;
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (segmentation, "Segmentation took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_segmentation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (object);

  switch (prop_id) {
    case PROP_MODULE:
      segmentation->mdlenum = g_value_get_enum (value);
      break;
    case PROP_LABELS:
      g_free (segmentation->labels);
      segmentation->labels = g_strdup (g_value_get_string (value));
      break;
    case PROP_CONSTANTS:
    {
      GValue structure = G_VALUE_INIT;

      g_value_init (&structure, GST_TYPE_STRUCTURE);

      if (!gst_parse_string_property_value (value, &structure)) {
        GST_ERROR_OBJECT (segmentation, "Failed to parse constants!");
        break;
      }

      if (segmentation->mlconstants != NULL)
        gst_structure_free (segmentation->mlconstants);

      segmentation->mlconstants = GST_STRUCTURE (g_value_dup_boxed (&structure));
      g_value_unset (&structure);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_segmentation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_value_set_enum (value, segmentation->mdlenum);
      break;
    case PROP_LABELS:
      g_value_set_string (value, segmentation->labels);
      break;
    case PROP_CONSTANTS:
    {
      gchar *string = NULL;

      if (segmentation->mlconstants != NULL)
        string = gst_structure_to_string (segmentation->mlconstants);

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
gst_ml_video_segmentation_finalize (GObject * object)
{
  GstMLVideoSegmentation *segmentation = GST_ML_VIDEO_SEGMENTATION (object);

  gst_ml_module_free (segmentation->module);

  if (segmentation->mlinfo != NULL)
    gst_ml_info_free (segmentation->mlinfo);

  if (segmentation->vinfo != NULL)
    gst_video_info_free (segmentation->vinfo);

  if (segmentation->outpool != NULL)
    gst_object_unref (segmentation->outpool);

  g_free (segmentation->labels);

  if (segmentation->mlconstants != NULL)
    gst_structure_free (segmentation->mlconstants);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (segmentation));
}

static void
gst_ml_video_segmentation_class_init (GstMLVideoSegmentationClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          GST_TYPE_ML_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_LABELS,
      g_param_spec_string ("labels", "Labels",
          "Labels filename", DEFAULT_PROP_LABELS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning image segmentation", "Filter/Effect/Converter",
      "Machine Learning plugin for image segmentation", "QTI");
  g_object_class_install_property (gobject, PROP_CONSTANTS,
      g_param_spec_string ("constants", "Constants",
          "Constants, offsets and coefficients used by the chosen module for "
          "post-processing of incoming tensors in GstStructure string format. "
          "Applicable only for some modules.",
          DEFAULT_PROP_CONSTANTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element,
      gst_ml_video_segmentation_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_segmentation_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_segmentation_transform);
}

static void
gst_ml_video_segmentation_init (GstMLVideoSegmentation * segmentation)
{
  segmentation->outpool = NULL;
  segmentation->module = NULL;

  segmentation->mdlenum = DEFAULT_PROP_MODULE;
  segmentation->labels = DEFAULT_PROP_LABELS;
  segmentation->mlconstants = DEFAULT_PROP_CONSTANTS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (segmentation), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_segmentation_debug, "qtimlvsegmentation",
      0, "QTI ML image segmentation plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvsegmentation", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_SEGMENTATION);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvsegmentation,
    "QTI Machine Learning plugin for image segmentation post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
