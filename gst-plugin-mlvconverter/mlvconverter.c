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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlvconverter.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/video/gstimagepool.h>

#define GST_CAT_DEFAULT gst_ml_video_converter_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_converter_debug);

#define gst_ml_video_converter_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoConverter, gst_ml_video_converter,
               GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MIN_BUFFERS     2
#define DEFAULT_PROP_MAX_BUFFERS     15

#define DEFAULT_PROP_SUBPIXEL_LAYOUT GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR
#define DEFAULT_PROP_MEAN            128.0
#define DEFAULT_PROP_SIGMA           128.0

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_ML_VIDEO_FORMATS \
    "{ RGBA, BGRA, ABGR, ARGB, RGBx, BGRx, xRGB, xBGR, BGR, RGB, GRAY8, NV12, NV21, YUY2 }"

#define GST_ML_VIDEO_CONVERTER_SINK_CAPS              \
    "video/x-raw, "                                  \
    "format = (string) " GST_ML_VIDEO_FORMATS "; "   \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), " \
    "format = (string) " GST_ML_VIDEO_FORMATS

#define GST_ML_TENSOR_TYPES "{ UINT8, INT32, FLOAT32 }"

#define GST_ML_VIDEO_CONVERTER_SRC_CAPS    \
    "neural-network/tensors, "             \
    "type = (string) " GST_ML_TENSOR_TYPES

enum
{
  PROP_0,
  PROP_SUBPIXEL_LAYOUT,
  PROP_MEAN,
  PROP_SIGMA,
};

static GstStaticCaps gst_ml_video_converter_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CONVERTER_SINK_CAPS);

static GstStaticCaps gst_ml_video_converter_static_src_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CONVERTER_SRC_CAPS);


static GstCaps *
gst_ml_video_converter_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_converter_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_converter_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_converter_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_converter_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_converter_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_converter_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_converter_src_caps ());
}

GType
gst_ml_video_pixel_layout_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR,
        "Regular subpixel layout e.g. RGB, RGBA, RGBx, etc.", "regular"
    },
    { GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE,
        "Reverse subpixel layout e.g. BGR, BGRA, BGRx, etc.", "reverse"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLVideoPixelLayout", variants);

  return gtype;
}

static void
init_formats (GValue * formats, ...)
{
  GValue format = G_VALUE_INIT;
  gchar *string = NULL;
  va_list args;

  g_value_init (formats, GST_TYPE_LIST);
  va_start (args, formats);

  while ((string = va_arg (args, gchar *))) {
    g_value_init (&format, G_TYPE_STRING);
    g_value_set_string (&format, string);

    gst_value_list_append_value (formats, &format);
    g_value_unset (&format);
  }

  va_end (args);
}

static gboolean
is_conversion_required (GstVideoFrame * inframes, guint n_inputs,
    GstVideoFrame * outframe)
{
  gboolean conversion = FALSE;

  // When there is more than 1 input frame we must always do conversion.
  if (n_inputs > 1)
    return TRUE;

  // Conversion is required if input and output formats are different.
  conversion |=  GST_VIDEO_FRAME_FORMAT (&inframes[0]) !=
      GST_VIDEO_FRAME_FORMAT (outframe);
  // Conversion is required if input and output strides are different.
  conversion |= GST_VIDEO_FRAME_PLANE_STRIDE (&inframes[0], 0) !=
      GST_VIDEO_FRAME_PLANE_STRIDE (outframe, 0);
  // Conversion is required if input and output heights are different.
  conversion |= GST_VIDEO_FRAME_HEIGHT (&inframes[0]) !=
      GST_VIDEO_FRAME_HEIGHT (outframe);

  return conversion;
}

static void
gst_unmap_input_video_frames (GstVideoFrame * inframes, guint n_inputs)
{
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < n_inputs; idx++) {
    if ((buffer = inframes[idx].buffer) == NULL)
      continue;

    gst_video_frame_unmap (&inframes[idx]);
    gst_buffer_unref (buffer);
  }

  g_free (inframes);
}

static gboolean
gst_map_input_video_frames (GstBuffer * inbuffer, GstVideoInfo * info,
    GstVideoFrame ** inframes, guint * n_inputs)
{
  GstVideoMeta *vmeta = NULL;
  guint idx = 0;

  *n_inputs = gst_buffer_n_memory (inbuffer);
  *inframes = g_new0 (GstVideoFrame, *n_inputs);

  for (idx = 0; idx < *n_inputs; idx++) {
    // Create a new buffer to placehold a reference to a single GstMemory block.
    GstBuffer *buffer = gst_buffer_new ();

    // Append the memory block from input buffer into the new buffer.
    gst_buffer_append_memory (buffer, gst_buffer_get_memory (inbuffer, idx));

    // Add parent meta, input buffer won't be released until new buffer is freed.
    gst_buffer_add_parent_buffer_meta (buffer, inbuffer);

    // Copy video metadata assigned to current memory block into the new buffer.
    if ((vmeta = gst_buffer_get_video_meta_id (inbuffer, idx)) != NULL)
      gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
          vmeta->format, vmeta->width, vmeta->height, vmeta->n_planes,
          vmeta->offset, vmeta->stride);

    // Create a video frame from the new input buffer.
    if (!gst_video_frame_map (inframes[idx], info, buffer,
            GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
      GST_ERROR ("Failed to map memory at idx %u!", idx);

      gst_buffer_unref (buffer);
      gst_unmap_input_video_frames (*inframes, *n_inputs);

      *inframes = NULL;
      *n_inputs = 0;

      return FALSE;
    }
  }

  return TRUE;
}

static GstCaps *
gst_ml_video_converter_translate_ml_caps (GstMLVideoConverter * mlconverter,
    const GstCaps * caps)
{
  GstCaps *result = NULL, *tmplcaps = NULL;
  GstMLInfo mlinfo;
  gint idx = 0, length = 0;

  tmplcaps = gst_caps_new_empty ();

  gst_caps_append_structure_full (tmplcaps,
      gst_structure_new_empty ("video/x-raw"),
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
  gst_caps_append_structure (tmplcaps,
      gst_structure_new_empty ("video/x-raw"));

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return tmplcaps;

  if (!gst_caps_is_fixed (caps) || !gst_ml_info_from_caps (&mlinfo, caps))
    return tmplcaps;

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (tmplcaps, idx);
    GstCapsFeatures *features = gst_caps_get_features (tmplcaps, idx);

    GValue formats = G_VALUE_INIT;
    const GValue *value = NULL;

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (result, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // The 2nd and 3rd dimensions correspond to width and height respectively.
    gst_structure_set (structure,
        "width", G_TYPE_INT, mlinfo.tensors[0][1],
        "height", G_TYPE_INT, mlinfo.tensors[0][2],
        NULL);

    // The 4th dimension corresponds to the bit depth.
    if (mlinfo.tensors[0][3] == 1) {
      init_formats (&formats, "GRAY8", NULL);
    } else if (mlinfo.tensors[0][3] == 3) {
      if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR)
        init_formats (&formats, "RGB", NULL);
      else if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE)
        init_formats (&formats, "BGR", NULL);
    } else if (mlinfo.tensors[0][3] == 4) {
      if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR)
        init_formats (&formats, "RGBA", "RGBx", "ARGB", "xRGB", NULL);
      else if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE)
        init_formats (&formats, "BGRA", "BGRx", "ABGR", "xBGR", NULL);
    }


    gst_structure_set_value (structure, "format", &formats);
    g_value_unset (&formats);

    // Extract the frame rate from ML and propagate it to the video caps.
    value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

    if (value != NULL)
      gst_structure_set_value (structure, "framerate", value);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));
  }

  gst_caps_unref (tmplcaps);

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_video_converter_translate_video_caps (GstMLVideoConverter * mlconverter,
    const GstCaps * caps)
{
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GValue dimensions = G_VALUE_INIT, dimension = G_VALUE_INIT;
  const GValue *value;

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return gst_caps_new_empty_simple ("neural-network/tensors");

  result = gst_caps_new_simple ("neural-network/tensors",
      "type", G_TYPE_STRING, gst_ml_type_to_string (GST_ML_TYPE_UINT8),
      NULL);

  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "width");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  value = gst_structure_get_value (structure, "height");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  value = gst_structure_get_value (structure, "format");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  g_value_init (&dimensions, GST_TYPE_ARRAY);
  g_value_init (&dimension, G_TYPE_INT);

  // 1st dimension is batch size.
  g_value_set_int (&dimension, 1);
  gst_value_array_append_value (&dimensions, &dimension);

  // 2nd dimension is video width.
  gst_value_array_append_value (&dimensions,
      gst_structure_get_value (structure, "width"));

  // 3rd dimension is video height.
  gst_value_array_append_value (&dimensions,
      gst_structure_get_value (structure, "height"));

  value = gst_structure_get_value (structure, "format");

  // 4th dimension is video chanels number.
  switch (gst_video_format_from_string (g_value_get_string (value))) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      g_value_set_int (&dimension, 4);
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      g_value_set_int (&dimension, 3);
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      g_value_set_int (&dimension, 1);
      break;
    default:
      GST_WARNING_OBJECT (mlconverter, "Unsupported format: %s, "
          "falling back to RGB!", g_value_get_string (value));
      g_value_set_int (&dimension, 3);
      break;
  }

  gst_value_array_append_value (&dimensions, &dimension);
  g_value_unset (&dimension);

  gst_caps_set_value (result, "dimensions", &dimensions);

  g_value_unset (&dimensions);

  // Extract the frame rate from video and propagate it to the ML caps.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      "framerate");

  if (value != NULL)
    gst_caps_set_value (result, "rate", value);

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static void
gst_ml_video_converter_update_destination (GstMLVideoConverter * mlconverter,
    GstVideoInfo * ininfo, GstVideoInfo * outinfo)
{
  GstStructure *opts = NULL;
  gint num = 0, den = 0, from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  gint width = 0, height = 0;

  if (!gst_util_fraction_multiply (ininfo->width, ininfo->height,
          ininfo->par_n, ininfo->par_d, &from_dar_n, &from_dar_d)) {
    GST_WARNING_OBJECT (mlconverter, "Failed to calculate input DAR!");
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (outinfo->width, outinfo->height,
          outinfo->par_n, outinfo->par_d, &to_dar_n, &to_dar_d)) {
    GST_WARNING_OBJECT (mlconverter, "Failed to calculate output DAR!");
    to_dar_n = to_dar_d = -1;
  }

  if (from_dar_n == to_dar_n && from_dar_d == to_dar_d)
    return;

  gst_util_fraction_multiply (from_dar_n, from_dar_d,
      outinfo->par_d, outinfo->par_n, &num, &den);

  if (num > den) {
    width = outinfo->width;
    height = gst_util_uint64_scale_int (outinfo->width, den, num);

    GST_DEBUG_OBJECT (mlconverter, "Adding border on the bottom with "
        "height %d px", outinfo->height - height);
  } else if (num < den) {
    width = gst_util_uint64_scale_int (outinfo->height, num, den);
    height = outinfo->height;

    GST_DEBUG_OBJECT (mlconverter, "Adding border at the right side with "
        "width %d px", outinfo->width - width);
  }

  opts = gst_structure_new ("qtivtransform",
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_WIDTH, G_TYPE_INT, width,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_HEIGHT, G_TYPE_INT, height,
      NULL);
  gst_c2d_video_converter_set_input_opts (mlconverter->c2dconvert, 0, opts);
}

static gboolean
gst_ml_video_converter_normalize_ip (GstMLVideoConverter * mlconverter,
    GstVideoFrame * vframe)
{
  guint8 *source = NULL;
  gfloat *destination = NULL;
  gfloat mean[4] = {0}, sigma[4] = {0};
  gint idx = 0, row = 0, column = 0, width = 0, height = 0, bpp = 0;

  // Retrive the video frame Bytes Per Pixel for later calculations.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (vframe->info.finfo) *
      GST_VIDEO_FORMAT_INFO_N_COMPONENTS (vframe->info.finfo);
  bpp /= 8;

  // Convinient local variables for per channel mean and sigma values.
  for (idx = 0; idx < bpp; idx++) {
    mean[idx] = (mlconverter->mean->len > (guint) idx) ?
        g_array_index (mlconverter->mean, gfloat, 0) : DEFAULT_PROP_MEAN;

    sigma[idx] = (mlconverter->sigma->len > (guint) idx) ?
        g_array_index (mlconverter->sigma, gfloat, 0) : DEFAULT_PROP_SIGMA;
  }

  source = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
  destination = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (vframe);
  height = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Adjust dimensions so that only the image pixels will be normalized.
  if (mlconverter->sar_n > mlconverter->sar_d)
    height = gst_util_uint64_scale_int (width, mlconverter->sar_d,
        mlconverter->sar_n);
  else if (mlconverter->sar_n < mlconverter->sar_d)
    width = gst_util_uint64_scale_int (height, mlconverter->sar_n,
        mlconverter->sar_d);

  // Normalize in reverse as front bytes are occupied.
  for (row = (height - 1); row >= 0; row--) {
    for (column = ((width * bpp) - 1); column >= 0; column--) {
      idx = (row * width * bpp) + column;

      destination[idx] = (source[idx] - mean[idx % bpp]) / sigma[idx % bpp];
    }
  }

  return TRUE;
}

static gboolean
gst_ml_video_converter_normalize (GstMLVideoConverter * mlconverter,
    GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  guint8 *source = NULL;
  gfloat *destination = NULL;
  gfloat mean[4] = {0}, sigma[4] = {0};
  guint idx = 0, size = 0, bpp = 0;

  // Sanity checks, input and output frame must differ only in type.
  g_return_val_if_fail (GST_VIDEO_FRAME_FORMAT (inframe) ==
      GST_VIDEO_FRAME_FORMAT (outframe), FALSE);
  g_return_val_if_fail (GST_VIDEO_FRAME_WIDTH (inframe) ==
      GST_VIDEO_FRAME_WIDTH (outframe), FALSE);
  g_return_val_if_fail (GST_VIDEO_FRAME_HEIGHT (inframe) ==
      GST_VIDEO_FRAME_HEIGHT (outframe), FALSE);

  // Retrive the input frame Bytes Per Pixel for later calculations.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (inframe->info.finfo) *
      GST_VIDEO_FORMAT_INFO_N_COMPONENTS (inframe->info.finfo);
  bpp /= 8;

  // Number of individual channels we need to normalize.
  size = GST_VIDEO_FRAME_SIZE (outframe) /
      gst_ml_type_get_size (mlconverter->mlinfo->type);

  // Sanity check, input frame size must be equal to adjusted output size.
  g_return_val_if_fail (GST_VIDEO_FRAME_SIZE (inframe) == size, FALSE);

  // Convinient local variables for per channel mean and sigma values.
  for (idx = 0; idx < bpp; idx++) {
    mean[idx] = (mlconverter->mean->len > idx) ?
        g_array_index (mlconverter->mean, gfloat, 0) : DEFAULT_PROP_MEAN;

    sigma[idx] = (mlconverter->sigma->len > idx) ?
        g_array_index (mlconverter->sigma, gfloat, 0) : DEFAULT_PROP_SIGMA;
  }

  source = GST_VIDEO_FRAME_PLANE_DATA (inframe, 0);
  destination = GST_VIDEO_FRAME_PLANE_DATA (outframe, 0);

  for (idx = 0; idx < size; idx++)
    destination[idx] = (source[idx] - mean[idx % bpp]) / sigma[idx % bpp];

  return TRUE;
}

static GstBufferPool *
gst_ml_video_converter_create_pool (GstMLVideoConverter * mlconverter,
    GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlconverter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (mlconverter, "Uses ION memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_ION);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (mlconverter, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_video_converter_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (mlconverter, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (mlconverter->outpool)
    gst_object_unref (mlconverter->outpool);

  // Create a new buffer pool.
  if ((pool = gst_ml_video_converter_create_pool (mlconverter, caps)) == NULL) {
    GST_ERROR_OBJECT (mlconverter, "Failed to create buffer pool!");
    return FALSE;
  }

  mlconverter->outpool = pool;

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

  gst_query_add_allocation_meta (query, GST_ML_TENSOR_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_converter_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstBufferPool *pool = mlconverter->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (base)) {
    GST_TRACE_OBJECT (mlconverter, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  } else if (gst_base_transform_is_in_place (base)) {
    GST_TRACE_OBJECT (mlconverter, "Inplace, use input buffer as output");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (mlconverter, "Failed to acquire output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_video_converter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *result = NULL, *intersection = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (mlconverter, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (mlconverter, "Filter caps: %" GST_PTR_FORMAT, filter);


  if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);

    if (!(result = gst_pad_get_current_caps (pad)))
      result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);

    if (!(result = gst_pad_get_current_caps (pad)))
      result = gst_pad_get_pad_template_caps (pad);
  }

  // Extract the framerate and propagate it to result caps.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      (direction == GST_PAD_SRC) ? "rate" : "framerate");

  if (value != NULL) {
    gint idx = 0, length = 0;

    result = gst_caps_make_writable (result);
    length = gst_caps_get_size (result);

    for (idx = 0; idx < length; idx++) {
      GstStructure *structure = gst_caps_get_structure (result, idx);
      gst_structure_set_value (structure,
          (direction == GST_PAD_SRC) ? "framerate" : "rate", value);
    }
  }

  if (filter != NULL) {
    intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_video_converter_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *mlcaps = NULL;
  const GValue *value = NULL;
  gint width = 0, height = 0, par_n = 0, par_d = 0;

  GST_DEBUG_OBJECT (mlconverter, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT " in direction %s",
      outcaps, incaps, (direction == GST_PAD_SINK) ? "sink" : "src");

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  mlcaps = gst_ml_video_converter_translate_video_caps (mlconverter, incaps);

  value = gst_structure_get_value (
    gst_caps_get_structure (outcaps, 0), "dimensions");

  if (NULL == value || !gst_value_is_fixed (value)) {
    value = gst_structure_get_value (
        gst_caps_get_structure (mlcaps, 0), "dimensions");
    gst_caps_set_value (outcaps, "dimensions", value);
  }

  value = gst_structure_get_value (
      gst_caps_get_structure (outcaps, 0), "type");

  if (NULL == value || !gst_value_is_fixed (value)) {
    value = gst_structure_get_value (
        gst_caps_get_structure (mlcaps, 0), "type");
    gst_caps_set_value (outcaps, "type", value);
  }

  // Retrieve the input width and height.
  gst_structure_get_int (gst_caps_get_structure (incaps, 0),
      "width", &width);
  gst_structure_get_int (gst_caps_get_structure (incaps, 0),
      "height", &height);

  // Retrieve the input PAR (Pixel Aspect Ratio) value.
  value = gst_structure_get_value (gst_caps_get_structure (incaps, 0),
      "pixel-aspect-ratio");

  if (value != NULL && gst_value_is_fixed (value)) {
    par_n = gst_value_get_fraction_numerator (value);
    par_d = gst_value_get_fraction_denominator (value);
  } else {
    par_n = par_d = 1;
  }

  // Calculate input DAR (Display Aspect Ratio) value.
  if (!gst_util_fraction_multiply (width, height, par_n, par_d,
          &(mlconverter)->sar_n, &(mlconverter)->sar_d))
    mlconverter->sar_n = mlconverter->sar_d = 1;

  // Set aspect ratio in output to be used for tensor processing downstream.
  gst_caps_set_simple (outcaps,
      "aspect-ratio", GST_TYPE_FRACTION, mlconverter->sar_n, mlconverter->sar_d,
      NULL);

  gst_caps_unref (mlcaps);
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (mlconverter, "Fixated caps: %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_video_converter_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *othercaps = NULL;
  GstVideoInfo ininfo, outinfo;
  GstMLInfo mlinfo;
  guint bpp = 0, padding = 0;
  gboolean passthrough = FALSE;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get input video info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&mlinfo, outcaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get output ML info from caps"
        " %" GST_PTR_FORMAT "!", outcaps);
    return FALSE;
  }

  if (mlconverter->ininfo != NULL)
    gst_video_info_free (mlconverter->ininfo);

  mlconverter->ininfo = gst_video_info_copy (&ininfo);

  if (mlconverter->mlinfo != NULL)
    gst_ml_info_free (mlconverter->mlinfo);

  mlconverter->mlinfo = gst_ml_info_copy (&mlinfo);

  othercaps = gst_ml_video_converter_translate_ml_caps (mlconverter, outcaps);
  othercaps = gst_caps_fixate (othercaps);

  if (!gst_video_info_from_caps (&outinfo, othercaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get output video info from caps %"
        GST_PTR_FORMAT "!", othercaps);
    gst_caps_unref (othercaps);
    return FALSE;
  }

  gst_caps_unref (othercaps);

  // Retrieve the Bits Per Pixel in order to calculate the line padding.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (outinfo.finfo) *
      GST_VIDEO_FORMAT_INFO_N_COMPONENTS (outinfo.finfo);

  // For padding calculations use the video meta if present.
  padding = GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, 0) -
      (GST_VIDEO_INFO_WIDTH (&outinfo) * bpp / 8);

  // Remove any padding from output video info as tensors require none.
  GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, 0) -= padding;

  // Adjust the  video info size to account the removed padding.
  GST_VIDEO_INFO_SIZE (&outinfo) -= padding * GST_VIDEO_INFO_HEIGHT (&outinfo);

  // Additionally adjust the total size depending on the ML type.
  GST_VIDEO_INFO_SIZE (&outinfo) *=
      gst_ml_type_get_size (mlconverter->mlinfo->type);

  if (mlconverter->vinfo != NULL)
    gst_video_info_free (mlconverter->vinfo);

  mlconverter->vinfo = gst_video_info_copy (&outinfo);

  passthrough =
      GST_VIDEO_INFO_SIZE (&ininfo) == GST_VIDEO_INFO_SIZE (&outinfo) &&
      GST_VIDEO_INFO_WIDTH (&ininfo) == GST_VIDEO_INFO_WIDTH (&outinfo) &&
      GST_VIDEO_INFO_HEIGHT (&ininfo) == GST_VIDEO_INFO_HEIGHT (&outinfo) &&
      GST_VIDEO_INFO_FORMAT (&ininfo) == GST_VIDEO_INFO_FORMAT (&outinfo);

  gst_base_transform_set_passthrough (base, passthrough);
  gst_base_transform_set_in_place (base, FALSE);

  // Add borders to the output tensor in order to keep input aspect ratio.
  gst_ml_video_converter_update_destination (mlconverter, &ininfo, &outinfo);

  GST_DEBUG_OBJECT (mlconverter, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (mlconverter, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_converter_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstVideoFrame *inframes = NULL, outframe;
  guint n_inputs = 0;
  gboolean success = TRUE;

  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_TIME_NONE;

  if (!gst_map_input_video_frames (inbuffer, mlconverter->ininfo,
          &inframes, &n_inputs)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to create input frames!");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&outframe, mlconverter->vinfo, outbuffer,
          GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to map output buffer!");

    gst_unmap_input_video_frames (inframes, n_inputs);
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  if (is_conversion_required (inframes, n_inputs, &outframe)) {
    // Submit conversion request to the C2D converter.
    gpointer request_id = gst_c2d_video_converter_submit_request (
        mlconverter->c2dconvert, inframes, n_inputs, &outframe);

    // Wait for the C2D conversion request to finish.
    success = gst_c2d_video_converter_wait_request (
        mlconverter->c2dconvert, request_id);

    // If the conversion request was successful apply normalization.
    if (success && mlconverter->mlinfo->type == GST_ML_TYPE_FLOAT32)
      success = gst_ml_video_converter_normalize_ip (mlconverter, &outframe);
  } else if (mlconverter->mlinfo->type == GST_ML_TYPE_FLOAT32) {
    // There is not need for frame conversion, apply only normalization.
    success = gst_ml_video_converter_normalize (mlconverter,
        &inframes[0], &outframe);
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (mlconverter, "Conversion took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  gst_video_frame_unmap (&outframe);
  gst_unmap_input_video_frames (inframes, n_inputs);

  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static void
gst_ml_video_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  switch (prop_id) {
    case PROP_SUBPIXEL_LAYOUT:
      mlconverter->pixlayout = g_value_get_enum (value);
      break;
    case PROP_MEAN:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        gfloat val = g_value_get_double (gst_value_array_get_value (value, idx));
        g_array_append_val (mlconverter->mean, val);
      }
      break;
    }
    case PROP_SIGMA:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        gfloat val = g_value_get_double (gst_value_array_get_value (value, idx));
        g_array_append_val (mlconverter->sigma, val);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  switch (prop_id) {
    case PROP_SUBPIXEL_LAYOUT:
      g_value_set_enum (value, mlconverter->pixlayout);
      break;
    case PROP_MEAN:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_DOUBLE);

      for (idx = 0; idx < mlconverter->mean->len; idx++) {
        g_value_set_double (&val,
            g_array_index (mlconverter->mean, gfloat, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    case PROP_SIGMA:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_DOUBLE);

      for (idx = 0; idx < mlconverter->sigma->len; idx++) {
        g_value_set_double (&val,
            g_array_index (mlconverter->sigma, gfloat, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_converter_finalize (GObject * object)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  if (mlconverter->sigma != NULL)
    g_array_free (mlconverter->sigma, TRUE);

  if (mlconverter->mean != NULL)
    g_array_free (mlconverter->mean, TRUE);

  if (mlconverter->c2dconvert != NULL)
    gst_c2d_video_converter_free (mlconverter->c2dconvert);

  if (mlconverter->mlinfo != NULL)
    gst_ml_info_free (mlconverter->mlinfo);

  if (mlconverter->vinfo != NULL)
    gst_video_info_free (mlconverter->vinfo);

  if (mlconverter->ininfo != NULL)
    gst_video_info_free (mlconverter->ininfo);

  if (mlconverter->outpool != NULL)
    gst_object_unref (mlconverter->outpool);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (mlconverter));
}

static void
gst_ml_video_converter_class_init (GstMLVideoConverterClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_converter_finalize);

  g_object_class_install_property (gobject, PROP_SUBPIXEL_LAYOUT,
      g_param_spec_enum ("subpixel-layout", "Subpixel Layout",
          "Arrangement of the image pixels in the output tensor",
          GST_TYPE_ML_VIDEO_PIXEL_LAYOUT, DEFAULT_PROP_SUBPIXEL_LAYOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_MEAN,
     gst_param_spec_array ("mean", "Mean Subtraction",
          "Channels mean subtraction values for FLOAT tensors "
          "('<R, G, B>', '<R, G, B, A>', '<G>')",
          g_param_spec_double ("value", "Mean Value",
              "One of B, G or R value.", 0.0, 255.0, DEFAULT_PROP_MEAN,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_SIGMA,
     gst_param_spec_array ("sigma", "Sigma Values",
          "Channel divisor values for FLOAT tensors "
          "('<R, G, B>', '<R, G, B, A>', '<G>')",
          g_param_spec_double ("value", "Sigma Value",
              "One of B, G or R value.", 0.0, 255.0, DEFAULT_PROP_SIGMA,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning Video Converter", "Filter/Video/Scaler",
      "Parse an video streams into a ML stream", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_converter_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_converter_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_video_converter_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_converter_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_converter_transform);
}

static void
gst_ml_video_converter_init (GstMLVideoConverter * mlconverter)
{
  mlconverter->ininfo = NULL;

  mlconverter->vinfo = NULL;
  mlconverter->mlinfo = NULL;
  mlconverter->outpool = NULL;

  mlconverter->c2dconvert = gst_c2d_video_converter_new ();

  mlconverter->pixlayout = DEFAULT_PROP_SUBPIXEL_LAYOUT;
  mlconverter->mean = g_array_new (FALSE, FALSE, sizeof (gfloat));
  mlconverter->sigma = g_array_new (FALSE, FALSE, sizeof (gfloat));

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_converter_debug, "qtimlvconverter",
      0, "QTI ML video converter plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvconverter", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_CONVERTER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvconverter,
    "QTI Machine Learning plugin for converting video stream into ML stream",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
