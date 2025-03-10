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
#include <unistd.h>

#include <gbm.h>

// Function pointers for the Adreno Utils library.
typedef unsigned int (*get_gpu_pixel_alignment)(void);

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

static gint
gst_adreno_get_pixel_alignment ()
{
  static GMutex mutex;
  static gint alignment = 0;
  void *handle = NULL;
  get_gpu_pixel_alignment GetGpuPixelAlignment = NULL;
  gboolean success = FALSE;

  g_mutex_lock (&mutex);

  // Alignment has already been set, just return its value.
  if (alignment != 0) {
    g_mutex_unlock (&mutex);
    return alignment;
  }

  if ((handle = dlopen ("libadreno_utils.so", RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to load Adreno utils lib, error: %s", dlerror());
    return -1;
  }

  success = load_symbol ((gpointer*)&GetGpuPixelAlignment, handle,
      "get_gpu_pixel_alignment");
  if (success == FALSE) {
    dlclose (handle);
    return -1;
  }

  // Fetch the GPU Pixel Alignment.
  alignment = GetGpuPixelAlignment();

  // Close the library as it is no longer needed.
  dlclose (handle);

  g_mutex_unlock (&mutex);
  return alignment;
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

gboolean
gst_video_retrieve_gpu_alignment (GstVideoInfo * info, GstVideoAlignment * align)
{
  const GstVideoFormatInfo *vfinfo = info->finfo;
  guint num = 0;

  for (num = 0; num < GST_VIDEO_INFO_N_PLANES (info); num++) {
    gint alignment = gst_adreno_get_pixel_alignment ();
    gint comp[GST_VIDEO_MAX_COMPONENTS] = { 0, };

    if (alignment == -1)
      return FALSE;

    gst_video_format_info_component (vfinfo, num, comp);
    alignment = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (vfinfo, comp[0], alignment);

    align->stride_align[num] = alignment - 1;
  }

  return gst_video_info_align (info, align);
}

GstVideoAlignment
gst_video_calculate_common_alignment (GstVideoAlignment * l_align,
    GstVideoAlignment * r_align)
{
  GstVideoAlignment align = { 0, };
  guint num = 0, max = 0;

  // Take the highest number of padding lines and pixels.
  align.padding_bottom = MAX (l_align->padding_bottom, r_align->padding_bottom);
  align.padding_top = MAX (l_align->padding_top, r_align->padding_top);
  align.padding_left = MAX (l_align->padding_left, r_align->padding_left);
  align.padding_right = MAX (l_align->padding_right, r_align->padding_right);

  // Calculate the lowest common multiple for the stride alignments.
  for (num = 0; num < GST_VIDEO_MAX_PLANES; num++) {
    max = MAX (l_align->stride_align[num], r_align->stride_align[num]);
    align.stride_align[num] = max;

    if ((l_align->stride_align[num] == 0) || (r_align->stride_align[num] == 0))
      continue;

    while (((align.stride_align[num] + 1) % (l_align->stride_align[num] + 1) != 0) ||
           ((align.stride_align[num] + 1) % (r_align->stride_align[num] + 1) != 0))
      align.stride_align[num] += max + 1;
  }

  return align;
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
