/*
 * Copyright (c) 2017-2018, 2021 The Linux Foundation. All rights reserved.
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

 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "video-utils.h"

#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <gbm.h>
#include <gst/gfx/gfx-utils.h>
#include <gst/utils/common-utils.h>

#define INT8_CONVERSION_OFFSET   (G_MAXUINT8 / 2 + 1)
#define INT16_CONVERSION_OFFSET  (G_MAXUINT16 / 2 + 1)
#define UINT16_CONVERSION_SCALE  (G_MAXUINT16 / G_MAXUINT8)
#define INT32_CONVERSION_OFFSET  (G_MAXUINT32 / 2 + 1)
#define UINT32_CONVERSION_SCALE  (G_MAXUINT32 / G_MAXUINT8)
#define INT64_CONVERSION_OFFSET  (G_MAXUINT64 / 2 + 1)
#define UINT64_CONVERSION_SCALE  (G_MAXUINT64 / G_MAXUINT8)
#define FLOAT_CONVERSION_SCALE   (1.0 / G_MAXUINT8)

// Function pointers for the GBM library.
typedef struct gbm_device* (*gbm_create_device_func)(int fd);
typedef void (*gbm_device_destroy_func)(struct gbm_device *device);
typedef const char* (*gbm_device_get_backend_name_func)(struct gbm_device *device);

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR ("Failed to find symbol %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

static inline gboolean
gst_video_data_normalization (gpointer data, guint idx, gdouble value,
    gdouble mean, gdouble sigma, GstVideoDataType datatype) {

  switch (datatype) {
    case GST_VIDEO_DATA_TYPE_U8:
    {
      GST_UINT8_PTR_CAST (data)[idx] = (guint8) ((value - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_I8:
    {
      gint8 newvalue = value - INT8_CONVERSION_OFFSET;
      GST_INT8_PTR_CAST (data)[idx] = (gint8) ((newvalue - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_U16:
    {
      guint16 newvalue = value * UINT16_CONVERSION_SCALE;
      GST_UINT16_PTR_CAST (data)[idx] = (guint16) ((newvalue - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_I16:
    {
      gint16 newvalue = value * UINT16_CONVERSION_SCALE - INT16_CONVERSION_OFFSET;
      GST_INT16_PTR_CAST (data)[idx] = (gint16) ((newvalue - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_U32:
    {
      guint32 newvalue = value * UINT32_CONVERSION_SCALE;
      GST_UINT32_PTR_CAST (data)[idx] = (guint32) ((newvalue - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_I32:
    {
      gint32 newvalue = value * UINT32_CONVERSION_SCALE - INT32_CONVERSION_OFFSET;
      GST_INT32_PTR_CAST (data)[idx] = (gint32) ((newvalue - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_U64:
    {
      guint64 newvalue = ((guint64) value) * UINT64_CONVERSION_SCALE;
      GST_UINT64_PTR_CAST (data)[idx] = (guint64) ((newvalue - mean) * sigma);
      break;
    }
    case GST_VIDEO_DATA_TYPE_I64:
    {
      gint64 newvalue = ((gint64) value) * UINT64_CONVERSION_SCALE -
          INT64_CONVERSION_OFFSET;
      GST_INT64_PTR_CAST (data)[idx] = (gint64) ((newvalue - mean) * sigma);
      break;
    }
#if defined(__ARM_FP16_FORMAT_IEEE)
    case GST_VIDEO_DATA_TYPE_F16:
    {
      __fp16 newvalue = value * FLOAT_CONVERSION_SCALE;
      GST_FLOAT16_PTR_CAST (data)[idx] = (__fp16) ((newvalue - mean) * sigma);
      break;
    }
#endif //__ARM_FP16_FORMAT_IEEE
    case GST_VIDEO_DATA_TYPE_F32:
    {
      gfloat newvalue = value * FLOAT_CONVERSION_SCALE;
      GST_FLOAT_PTR_CAST (data)[idx] = (gfloat) ((newvalue - mean) * sigma);
      break;
    }
    default:
      GST_ERROR ("Unsupported type: %s", gst_video_data_type_to_string (datatype));
      return FALSE;
  }

  return TRUE;
}

guint
gst_video_data_type_get_size (GstVideoDataType datatype)
{
  switch (datatype) {
    case GST_VIDEO_DATA_TYPE_U8:
    case GST_VIDEO_DATA_TYPE_I8:
      return 1;
    case GST_VIDEO_DATA_TYPE_U16:
    case GST_VIDEO_DATA_TYPE_I16:
    case GST_VIDEO_DATA_TYPE_F16:
      return 2;
    case GST_VIDEO_DATA_TYPE_U32:
    case GST_VIDEO_DATA_TYPE_I32:
    case GST_VIDEO_DATA_TYPE_F32:
      return 4;
    case GST_VIDEO_DATA_TYPE_U64:
    case GST_VIDEO_DATA_TYPE_I64:
      return 8;
    default:
      break;
  }

  return 0;
}

const gchar *
gst_video_data_type_to_string (GstVideoDataType datatype)
{
  switch (datatype) {
    case GST_VIDEO_DATA_TYPE_U8:
      return "UINT8";
    case GST_VIDEO_DATA_TYPE_I8:
      return "INT8";
    case GST_VIDEO_DATA_TYPE_U16:
      return "UINT16";
    case GST_VIDEO_DATA_TYPE_I16:
      return "INT16";
    case GST_VIDEO_DATA_TYPE_U32:
      return "UINT32";
    case GST_VIDEO_DATA_TYPE_I32:
      return "INT32";
    case GST_VIDEO_DATA_TYPE_U64:
      return "UINT64";
    case GST_VIDEO_DATA_TYPE_I64:
      return "INT64";
    case GST_VIDEO_DATA_TYPE_F16:
      return "FLOAT16";
    case GST_VIDEO_DATA_TYPE_F32:
      return "FLOAT32";
    default:
      break;
  }

  return "UNKNOWN";
}

void
gst_video_point_affine_transform (GstVideoPoint * point, gdouble matrix[3][3])
{
  gdouble x = 0.0, y = 0.0, z = 0.0;

  // Calcualte the new X and Y coordinates with the following formulas:
  // +------------+ +---+   +----+
  // | A0  A1  A2 | | x |   | x' | x' = A0 * x + A1 * y + A2
  // | B0  B1  B2 | | y | = | y' | y' = B0 * x + B1 * y + B2
  // | C0  C1  C2 | | 1 |   | z' | z' = C0 * x + C1 * y + C2
  // +------------+ +---+   +----+
  x = matrix[0][0] * point->x + matrix[0][1] * point->y + matrix[0][2];
  y = matrix[1][0] * point->x + matrix[1][1] * point->y + matrix[1][2];
  z = matrix[2][0] * point->x + matrix[2][1] * point->y + matrix[2][2];

  // Transform from world space (3D) to screen space (2D).
  point->x = x / z;
  point->y = y / z;
}

gboolean
gst_video_quadrilateral_is_rectangle (const GstVideoQuadrilateral * quadrilateral)
{
  return (quadrilateral->a.x == quadrilateral->b.x) &&
      (quadrilateral->c.x == quadrilateral->d.x) &&
      (quadrilateral->a.y == quadrilateral->c.y) &&
      (quadrilateral->b.y == quadrilateral->d.y);
}

void
gst_video_quadrilateral_from_rectangle (GstVideoQuadrilateral * quadrilateral,
    const GstVideoRectangle * rectangle)
{
  quadrilateral->a.x = rectangle->x;
  quadrilateral->a.y = rectangle->y;
  quadrilateral->b.x = rectangle->x;
  quadrilateral->b.y = rectangle->y + rectangle->h;
  quadrilateral->c.x = rectangle->x + rectangle->w;
  quadrilateral->c.y = rectangle->y;
  quadrilateral->d.x = rectangle->x + rectangle->w;
  quadrilateral->d.y = rectangle->y + rectangle->h;
}

void
gst_video_quadrilateral_to_rectangle (
    const GstVideoQuadrilateral * quadrilateral, GstVideoRectangle * rectangle)
{
  rectangle->x = quadrilateral->a.x;
  rectangle->y = quadrilateral->a.y;
  rectangle->w = quadrilateral->d.x - quadrilateral->a.x;
  rectangle->h = quadrilateral->d.y - quadrilateral->a.y;
}

gboolean
gst_gbm_qcom_backend_is_supported (void)
{
  static gboolean supported = FALSE;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    struct gbm_device* device = NULL;
    gpointer libhandle = NULL;
    gbm_create_device_func gbm_create_device = NULL;
    gbm_device_destroy_func gbm_device_destroy = NULL;
    gbm_device_get_backend_name_func gbm_device_get_backend_name = NULL;
    gboolean success = FALSE;
    gint fd = -1;

    // Load GBM library and symbols.
    libhandle = dlopen ("libgbm.so.1", RTLD_NOW);
    if (libhandle != NULL) {
      success = load_symbol ((gpointer*)&gbm_create_device, libhandle,
          "gbm_create_device");
      success &= load_symbol ((gpointer*)&gbm_device_destroy, libhandle,
          "gbm_device_destroy");
      success &= load_symbol ((gpointer*)&gbm_device_get_backend_name, libhandle,
          "gbm_device_get_backend_name");
    }

    if (success) {
      fd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

      // Fallback to /dev/dma_heap/system
      if (fd < 0)
        fd = open ("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);

      // Fallback to ION
      if (fd < 0)
        fd = open ("/dev/ion", O_RDONLY | O_CLOEXEC);

      success = (fd >= 0) ? TRUE : FALSE;
    }

    if (success) {
      device = gbm_create_device (fd);
      success = (device != NULL) ? TRUE : FALSE;
    }

    if (success) {
      const char* backend_name = gbm_device_get_backend_name (device);
      supported = (strncmp(backend_name, "msm_drm", 7) == 0) ? TRUE : FALSE;
    }

    if (device != NULL)
      gbm_device_destroy (device);

    if (fd > 0)
      close (fd);

    if (libhandle)
      dlclose (libhandle);

    g_once_init_leave (&inited, 1);
  }

  return supported;
}

gboolean
gst_video_retrieve_gpu_alignment (GstVideoInfo * info, GstVideoAlignment * align)
{
  const GstVideoFormatInfo *vfinfo = info->finfo;
  const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(info);
  guint num = 0;

  for (num = 0; num < GST_VIDEO_INFO_N_PLANES (info); num++) {
    gint alignment = gst_gfx_get_alignment ();
    gint comp[GST_VIDEO_MAX_COMPONENTS] = { 0, };

    gst_video_format_info_component (vfinfo, num, comp);
    alignment = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (vfinfo, comp[0], alignment);

    align->stride_align[num] = alignment - 1;
  }

  // Adreno GPUs require 24bpp formats (e.g., RGB, BGR) to be aligned as 32bpp.
  // Each pixel is 3 bytes, but the GPU expects 4-byte alignment for efficiency.
  // To simulate 32bpp layout, we add (width / 3) pixels of padding to each row.
  // This adds 'width' bytes, improving memory access and bandwidth usage.
  if (format == GST_VIDEO_FORMAT_RGB || format == GST_VIDEO_FORMAT_BGR) {
    align->padding_right = info->width / 3;
  }

  // Freedreno alignment limitation. Height need to be multiple of 4
  if (GST_VIDEO_FORMAT_INFO_IS_RGB (vfinfo))
    align->padding_bottom = GST_ROUND_UP_4 (info->height) - info->height;

  return gst_video_info_align (info, align);
}

void
gst_video_alignment_update (GstVideoAlignment * align,
    const GstVideoAlignment * otheralign)
{
  GstVideoAlignment alignment = { 0, };
  guint num = 0, max = 0;

  // Take the highest number of padding lines and pixels.
  alignment.padding_bottom = MAX (align->padding_bottom, otheralign->padding_bottom);
  alignment.padding_top = MAX (align->padding_top, otheralign->padding_top);
  alignment.padding_left = MAX (align->padding_left, otheralign->padding_left);
  alignment.padding_right = MAX (align->padding_right, otheralign->padding_right);

  // Calculate the lowest common multiple for the stride alignments.
  for (num = 0; num < GST_VIDEO_MAX_PLANES; num++) {
    max = MAX (align->stride_align[num], otheralign->stride_align[num]);
    alignment.stride_align[num] = max;

    if ((align->stride_align[num] == 0) || (otheralign->stride_align[num] == 0))
      continue;

    while (((alignment.stride_align[num] + 1) % (align->stride_align[num] + 1) != 0) ||
           ((alignment.stride_align[num] + 1) % (otheralign->stride_align[num] + 1) != 0))
      alignment.stride_align[num] += max + 1;
  }

  // Update the alignment structure with the calculated common values.
  *align = alignment;
}

gboolean
gst_query_parse_video_alignment (GstQuery * query, GstVideoAlignment * align)
{
  const GstStructure *params = NULL;
  guint idx = 0;

  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (align != NULL, FALSE);

  if (!gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, &idx))
    return FALSE;

  gst_query_parse_nth_allocation_meta (query, idx, &params);

  if (params == NULL)
    return FALSE;

  return gst_structure_get (params,
      "padding-top", G_TYPE_UINT, &align->padding_top,
      "padding-bottom", G_TYPE_UINT, &align->padding_bottom,
      "padding-left", G_TYPE_UINT, &align->padding_left,
      "padding-right", G_TYPE_UINT, &align->padding_right,
      "stride-align0", G_TYPE_UINT, &align->stride_align[0],
      "stride-align1", G_TYPE_UINT, &align->stride_align[1],
      "stride-align2", G_TYPE_UINT, &align->stride_align[2],
      "stride-align3", G_TYPE_UINT, &align->stride_align[3], NULL);
}

GstVideoRegionOfInterestMeta *
gst_buffer_copy_video_region_of_interest_meta (GstBuffer * buffer,
    GstVideoRegionOfInterestMeta * roimeta)
{
  GstVideoRegionOfInterestMeta *newmeta = NULL;
  GList *list = NULL;

  // Add ROI meta with the actual part of the buffer filled with image data.
  newmeta = gst_buffer_add_video_region_of_interest_meta_id (buffer,
      roimeta->roi_type, roimeta->x, roimeta->y, roimeta->w, roimeta->h);

  newmeta->id = roimeta->id;
  newmeta->parent_id = roimeta->parent_id;

  for (list = roimeta->params; list != NULL; list = list->next) {
    GstStructure *structure = gst_structure_copy (GST_STRUCTURE_CAST (list->data));
    gst_video_region_of_interest_meta_add_param (newmeta, structure);
  }

  return newmeta;
}

GList *
gst_buffer_get_video_region_of_interest_metas_parent_id (GstBuffer * buffer,
    const gint parent_id)
{
  GList *metalist = NULL;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api != GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)
      continue;

    if (GST_VIDEO_ROI_META_CAST (meta)->roi_type ==
            g_quark_from_static_string ("ImageRegion"))
      continue;

    if (GST_VIDEO_ROI_META_CAST (meta)->parent_id == parent_id)
      metalist = g_list_prepend (metalist, meta);
  }
  return metalist;
}

void
gst_video_region_of_interest_meta_transform_coordinates (
    GstVideoRegionOfInterestMeta * roimeta, const GstVideoRectangle * source,
    const GstVideoRectangle * destination)
{
  gdouble w_scale = 0.0, h_scale = 0.0;

  gst_util_fraction_to_double (destination->w, source->w, &w_scale);
  gst_util_fraction_to_double (destination->h, source->h, &h_scale);

  roimeta->w = roimeta->w * w_scale;
  roimeta->h = roimeta->h * h_scale;
  roimeta->x = ((roimeta->x - source->x) * w_scale) + destination->x;
  roimeta->y = ((roimeta->y - source->y) * h_scale) + destination->y;
}

gboolean
gst_buffer_has_valid_parent_meta (GstBuffer * buffer, gint parent_id)
{
  GstVideoRegionOfInterestMeta *parent_meta = NULL;

  if (parent_id == -1)
    return FALSE;

  parent_meta =
      gst_buffer_get_video_region_of_interest_meta_id (buffer, parent_id);

  if (parent_meta == NULL)
    return FALSE;

  if (parent_meta->roi_type == g_quark_from_static_string ("ImageRegion"))
    return FALSE;

  return TRUE;
}

gboolean
gst_video_info_modify_with_meta (GstVideoInfo * info, const GstVideoMeta * meta)
{
  if (meta == NULL)
    return TRUE;

  g_return_val_if_fail (info->finfo->format == meta->format, FALSE);
  g_return_val_if_fail (info->width == (gint) meta->width, FALSE);
  g_return_val_if_fail (info->height == (gint) meta->height, FALSE);
  g_return_val_if_fail (info->finfo->n_planes == meta->n_planes, FALSE);

  for (size_t idx = 0; idx < GST_VIDEO_INFO_N_PLANES (info); idx++) {
    info->offset[idx] = meta->offset[idx];
    info->stride[idx] = meta->stride[idx];
  }

  return TRUE;
}

gboolean
gst_video_frame_normalize_ip (GstVideoFrame * vframe,
    GstVideoDataType datatype, gdouble offsets[GST_VIDEO_MAX_COMPONENTS],
    gdouble scales[GST_VIDEO_MAX_COMPONENTS])
{
  guint8 *indata = NULL;
  gpointer outdata = NULL;
  gint row = 0, column = 0, width = 0, height = 0, idx = 0, bpp = 0;
  gboolean success = TRUE, normalize = FALSE;

  // Raise the normalization flag if output is not UINT8.
  normalize = (datatype != GST_VIDEO_DATA_TYPE_U8);

  // The flag will be raised also if there are custom nomalization params.
  for (idx = 0; idx < GST_VIDEO_MAX_COMPONENTS; idx++)
    normalize |= (offsets[idx] != 0) || (scales[idx] != 1);

  if (!normalize)
    return TRUE;

  GST_TRACE ("Normalization for %" GST_PTR_FORMAT, vframe->buffer);

  // Retrive the video frame Bytes Per Pixel for later calculations.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (vframe->info.finfo) *
      GST_VIDEO_FORMAT_INFO_N_COMPONENTS (vframe->info.finfo);
  bpp /= 8;

  indata = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
  outdata = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (vframe);
  height = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Normalize in reverse as front bytes are occupied.
  for (row = (height - 1); row >= 0; row--) {
    for (column = ((width * bpp) - 1); (column >= 0) && success; column--) {
      idx = (row * width * bpp) + column;

      // Convert value to actual type and apply normalization.
      success = gst_video_data_normalization (outdata, idx, indata[idx],
          offsets[idx % bpp], scales[idx % bpp], datatype);
    }
  }

  if (!success)
    GST_ERROR ("Normalization failed for %" GST_PTR_FORMAT, vframe->buffer);

  return success;
}
