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

 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2022-2023, 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "video-utils.h"

#include <stdbool.h>
#include <string.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>

//TODO: Workaround due to Adreno not exporting the formats.
typedef enum {
  ADRENO_PIXELFORMAT_UNKNOWN = 0,
  ADRENO_PIXELFORMAT_R32G32B32A32_FLOAT = 2,
  ADRENO_PIXELFORMAT_R32G32B32_FLOAT = 6,
  ADRENO_PIXELFORMAT_R16G16B16A16_FLOAT = 10,
  ADRENO_PIXELFORMAT_R10G10B10A2_UNORM = 24,  // Vertex, Normalized GL_UNSIGNED_INT_10_10_10_2_OES
  ADRENO_PIXELFORMAT_R8G8B8A8 = 28,
  ADRENO_PIXELFORMAT_R8G8B8A8_SRGB = 29,
  ADRENO_PIXELFORMAT_R16G16_UNORM = 35,
  ADRENO_PIXELFORMAT_R8G8_UNORM = 49,
  ADRENO_PIXELFORMAT_R16_UNORM = 56,
  ADRENO_PIXELFORMAT_R8_UNORM = 61,
  ADRENO_PIXELFORMAT_B5G6R5 = 85,
  ADRENO_PIXELFORMAT_B5G5R5A1 = 86,
  ADRENO_PIXELFORMAT_B8G8R8A8_UNORM = 87,
  ADRENO_PIXELFORMAT_B8G8R8X8_UNORM = 88,
  ADRENO_PIXELFORMAT_B8G8R8A8 = 90,
  ADRENO_PIXELFORMAT_B8G8R8A8_SRGB = 91,
  ADRENO_PIXELFORMAT_B8G8R8X8_SRGB = 93,
  ADRENO_PIXELFORMAT_NV12 = 103,
  ADRENO_PIXELFORMAT_P010 = 104,
  ADRENO_PIXELFORMAT_YUY2 = 107,
  ADRENO_PIXELFORMAT_B4G4R4A4 = 115,
  ADRENO_PIXELFORMAT_NV12_EXT = 506,       // NV12 with non-std alignment and offsets
  ADRENO_PIXELFORMAT_R8G8B8X8 = 507,       //  GL_RGB8 (Internal)
  ADRENO_PIXELFORMAT_R8G8B8 = 508,         //  GL_RGB8
  ADRENO_PIXELFORMAT_A1B5G5R5 = 519,       //  GL_RGB5_A1
  ADRENO_PIXELFORMAT_R8G8B8X8_SRGB = 520,  //  GL_SRGB8
  ADRENO_PIXELFORMAT_R8G8B8_SRGB = 521,    //  GL_SRGB8
  ADRENO_PIXELFORMAT_R16G16B16_FLOAT = 523,
  ADRENO_PIXELFORMAT_R5G6B5 = 610,         //  RGBA version of B5G6R5
  ADRENO_PIXELFORMAT_R5G5B5A1 = 611,       //  RGBA version of B5G5R5A1
  ADRENO_PIXELFORMAT_R4G4B4A4 = 612,       //  RGBA version of B4G4R4A4
  ADRENO_PIXELFORMAT_UYVY = 614,           //  YUV 4:2:2 packed progressive (1 plane)
  ADRENO_PIXELFORMAT_NV21 = 619,
  ADRENO_PIXELFORMAT_Y8U8V8A8 = 620,  // YUV 4:4:4 packed (1 plane)
  ADRENO_PIXELFORMAT_Y8 = 625,        //  Single 8-bit luma only channel YUV format
  ADRENO_PIXELFORMAT_NV21_EXT = 647,  // NV12 with swapped ordering of UV samples on UV plane (2 planes)
  ADRENO_PIXELFORMAT_TP10 = 654,      // YUV 4:2:0 planar 10 bits/comp (2 planes)
} ADRENOPIXELFORMAT;

typedef struct gbm_device* (*gbm_create_device_func)(int fd);
typedef void (*gbm_device_destroy_func)(struct gbm_device *device);
typedef const char* (*gbm_device_get_backend_name_func)(struct gbm_device *device);
typedef void (*adreno_utils_compute_alignment)(int width, int height, int plane_id,
    ADRENOPIXELFORMAT format, int num_samples, int tile_mode, int raster_mode,
    int padding_threshold, int *stride, int *scanline);

static ADRENOPIXELFORMAT
gst_video_format_to_pixel_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV12_Q08C:
      return ADRENO_PIXELFORMAT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return ADRENO_PIXELFORMAT_NV21_EXT;
    case GST_VIDEO_FORMAT_YUY2:
      return ADRENO_PIXELFORMAT_YUY2;
    case GST_VIDEO_FORMAT_UYVY:
      return ADRENO_PIXELFORMAT_UYVY;
    case GST_VIDEO_FORMAT_P010_10LE:
      return ADRENO_PIXELFORMAT_P010;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return ADRENO_PIXELFORMAT_TP10;
    case GST_VIDEO_FORMAT_BGRA:
      return ADRENO_PIXELFORMAT_B8G8R8A8;
    case GST_VIDEO_FORMAT_RGBx:
      return ADRENO_PIXELFORMAT_R8G8B8X8;
    case GST_VIDEO_FORMAT_xBGR:
      return ADRENO_PIXELFORMAT_R8G8B8X8;
    case GST_VIDEO_FORMAT_RGBA:
      return ADRENO_PIXELFORMAT_R8G8B8A8;
    case GST_VIDEO_FORMAT_ABGR:
      return ADRENO_PIXELFORMAT_R8G8B8A8;
    case GST_VIDEO_FORMAT_RGB:
      return ADRENO_PIXELFORMAT_R8G8B8;
    case GST_VIDEO_FORMAT_BGR:
      return ADRENO_PIXELFORMAT_R8G8B8;
    case GST_VIDEO_FORMAT_BGR16:
      return ADRENO_PIXELFORMAT_R5G6B5;
    case GST_VIDEO_FORMAT_RGB16:
      return ADRENO_PIXELFORMAT_B5G6R5;
    case GST_VIDEO_FORMAT_GRAY8:
      return ADRENO_PIXELFORMAT_R8_UNORM;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }
  return ADRENO_PIXELFORMAT_UNKNOWN;
}

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

gboolean
gst_adreno_utils_compute_alignment (guint width, guint height,
    GstVideoFormat format, gint *stride, gint *scanline)
{
  void *handle = NULL;
  adreno_utils_compute_alignment compute_alignment = NULL;
  ADRENOPIXELFORMAT gpu_pixel_format = ADRENO_PIXELFORMAT_UNKNOWN;
  gboolean success = FALSE;

  gpu_pixel_format = gst_video_format_to_pixel_format (format);
  if (gpu_pixel_format == ADRENO_PIXELFORMAT_UNKNOWN) {
    GST_ERROR("Gpu pixel format is unknown");
    return FALSE;
  }

  handle = dlopen ("libadreno_utils.so", RTLD_NOW);
  if (handle == NULL) {
    GST_ERROR("Failed to load Adreno utils lib, error: %s", dlerror());
    return FALSE;
  }

  success = load_symbol ((gpointer*)&compute_alignment, handle,
      "compute_fmt_aligned_width_and_height");
  if (success == FALSE) {
    GST_ERROR("Failed to load Adreno utils symbol, error: %s", dlerror());
    dlclose (handle);
    return FALSE;
  }

  compute_alignment (width, height, 0 , gpu_pixel_format, 1,
      0, 0, 512, stride, scanline);
  dlclose (handle);
  return TRUE;
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
    libhandle = dlopen ("libgbm.so", RTLD_NOW);
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

GstVideoAlignment
gst_video_calculate_common_alignment (GstVideoAlignment * l_align,
    GstVideoAlignment * r_align)
{
  GstVideoAlignment align = { 0, };

  // Take the highest number of additional lines in height.
  align.padding_bottom = MAX (l_align->padding_bottom, r_align->padding_bottom);

  // TODO: Workaround: Also assume that other fields are equal.
  align.padding_top = l_align->padding_top;
  align.padding_left = l_align->padding_left;

  // TODO: Workaround: Considering the alignments are power of 2.
  align.padding_right = MAX (l_align->padding_right, r_align->padding_right);

  return align;
}

void
gst_video_utils_get_gpu_align (GstVideoInfo * info, GstVideoAlignment * align)
{
  GstVideoFormat format;
  gboolean success;
  guint width, height;
  gint stride, scanline;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  format = GST_VIDEO_INFO_FORMAT (info);

  success = gst_adreno_utils_compute_alignment (width, height, format,
     &stride, &scanline);
  if (success) {
    align->padding_bottom = scanline - height;
    align->padding_right = stride - width;
  }
}

gboolean
gst_query_get_video_alignment (GstQuery * query, GstVideoAlignment * align)
{
  const GstStructure *params = NULL;
  guint idx = 0;

  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (align != NULL, FALSE);

  if (!gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, &idx))
    return FALSE;

  gst_query_parse_nth_allocation_meta (query, idx, &params);

  if (params != NULL)
    return gst_structure_get (params,
        "padding-top", G_TYPE_UINT, &align->padding_top,
        "padding-bottom", G_TYPE_UINT, &align->padding_bottom,
        "padding-left", G_TYPE_UINT, &align->padding_left,
        "padding-right", G_TYPE_UINT, &align->padding_right,
        "stride-align0", G_TYPE_UINT, &align->stride_align[0],
        "stride-align1", G_TYPE_UINT, &align->stride_align[1],
        "stride-align2", G_TYPE_UINT, &align->stride_align[2],
        "stride-align3", G_TYPE_UINT, &align->stride_align[3], NULL);

  return TRUE;
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
gst_video_region_of_interest_coordinates_correction (
    GstVideoRegionOfInterestMeta * roimeta, GstVideoRectangle * source,
    GstVideoRectangle * destination)
{
  gdouble w_scale = 0.0, h_scale = 0.0;

  gst_util_fraction_to_double (destination->w, source->w, &w_scale);
  gst_util_fraction_to_double (destination->h, source->h, &h_scale);

  roimeta->w = roimeta->w * w_scale;
  roimeta->h = roimeta->h * h_scale;
  roimeta->x = ((roimeta->x - source->x) * w_scale) + destination->x;
  roimeta->y = ((roimeta->y - source->y) * h_scale) + destination->y;
}
