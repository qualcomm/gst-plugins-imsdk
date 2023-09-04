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
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "c2d-video-converter.h"

#include <stdint.h>
#include <dlfcn.h>
#include <unistd.h>

#include <adreno/c2d2.h>
#include <adreno/c2dExt.h>
#include <linux/msm_kgsl.h>
#include <media/msm_media_info.h>


#define GST_CAT_DEFAULT ensure_debug_category()

#define CHECK_C2D_CAPABILITY(info, name) \
    GST_DEBUG ("    %-30s [%c]", #name, \
        info.capabilities_mask & C2D_DRIVER_SUPPORTS_##name ? 'x' : ' ');

#define FABS(value)              (((value) < 0.0F) ? -(value) : (value))

#define C2D_INIT_MAX_OBJECT       12
#define C2D_INIT_MAX_TEMPLATE     20

#define GST_C2D_GET_LOCK(obj)     (&((GstC2dVideoConverter *) obj)->lock)
#define GST_C2D_LOCK(obj)         g_mutex_lock (GST_C2D_GET_LOCK(obj))
#define GST_C2D_UNLOCK(obj)       g_mutex_unlock(GST_C2D_GET_LOCK(obj))

#define GST_C2D_MAX_DRAW_OBJECTS  250

/// Mutex for protecting the static reference counter.
G_LOCK_DEFINE_STATIC (c2d);
// Reference counter as C2D is singleton.
static gint refcount = 0;

struct _GstC2dVideoConverter
{
  // Global mutex lock.
  GMutex     lock;

  // Map of C2D surface ID and its corresponding GPU address.
  GHashTable *gpulist;

  // Map of C2D surface ID and its frame virtual mapped address
  GHashTable *vaddrlist;

  // Map of buffer FDs and their corresponding C2D surface ID.
  GHashTable *insurfaces;
  GHashTable *outsurfaces;

  // C2D library handle.
  gpointer   c2dhandle;

  // C2D library APIs.
  C2D_API C2D_STATUS (*DriverInit) (C2D_DRIVER_SETUP_INFO *setup);
  C2D_API C2D_STATUS (*DriverDeInit) (void);
  C2D_API C2D_STATUS (*CreateSurface) (uint32* id, uint32 bits,
                                       C2D_SURFACE_TYPE type,
                                       void* definition);
  C2D_API C2D_STATUS (*DestroySurface) (uint32 id);
  C2D_API C2D_STATUS (*UpdateSurface) (uint32 id, uint32 bits,
                                       C2D_SURFACE_TYPE type,
                                       void* definition);
  C2D_API C2D_STATUS (*QuerySurface) (uint32 id, uint32* bits,
                                      C2D_SURFACE_TYPE* type,
                                      uint32* width, uint32* height,
                                      uint32* format);
  C2D_API C2D_STATUS (*SurfaceUpdated) (uint32 surface_id, C2D_RECT *rectangle);
  C2D_API C2D_STATUS (*FillSurface) (uint32 surface_id,uint32 color,
                                     C2D_RECT *rectangle);
  C2D_API C2D_STATUS (*Draw) (uint32 id, uint32 config, C2D_RECT* scissor,
                              uint32 mask, uint32 color_key,
                              C2D_OBJECT* objects, uint32 count);
  C2D_API C2D_STATUS (*Flush) (uint32 id, c2d_ts_handle* timestamp);
  C2D_API C2D_STATUS (*WaitTimestamp) (c2d_ts_handle timestamp);
  C2D_API C2D_STATUS (*Finish) (uint32 id);
  C2D_API C2D_STATUS (*MapAddr) (int32_t fd, void* vaddr, uint32 size,
                                 uint32 offset, uint32 flags, void** gpuaddr);
  C2D_API C2D_STATUS (*UnMapAddr) (void* gpuaddr);
  C2D_API C2D_STATUS (*GetDriverCapabilities) (C2D_DRIVER_INFO* caps);
};

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize catonce = 0;
  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("c2d-video-converter",
        0, "C2D video converter");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

static gint
gst_video_format_to_c2d_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return C2D_COLOR_FORMAT_420_Y_UV;
    case GST_VIDEO_FORMAT_NV21:
      return C2D_COLOR_FORMAT_420_Y_VU;
    case GST_VIDEO_FORMAT_I420:
      return C2D_COLOR_FORMAT_420_Y_U_V;
    case GST_VIDEO_FORMAT_YV12:
      return C2D_COLOR_FORMAT_420_Y_V_U;
    case GST_VIDEO_FORMAT_YUV9:
      return C2D_COLOR_FORMAT_410_Y_UV;
    case GST_VIDEO_FORMAT_YVU9:
      return C2D_COLOR_FORMAT_410_Y_VU;
    case GST_VIDEO_FORMAT_NV16:
      return C2D_COLOR_FORMAT_422_Y_UV;
    case GST_VIDEO_FORMAT_NV61:
      return C2D_COLOR_FORMAT_422_Y_VU;
    case GST_VIDEO_FORMAT_YUY2:
      return C2D_COLOR_FORMAT_422_YUYV;
    case GST_VIDEO_FORMAT_UYVY:
      return C2D_COLOR_FORMAT_422_UYVY;
    case GST_VIDEO_FORMAT_YVYU:
      return C2D_COLOR_FORMAT_422_YVYU;
    case GST_VIDEO_FORMAT_VYUY:
      return C2D_COLOR_FORMAT_422_VYUY;
    case GST_VIDEO_FORMAT_Y42B:
      return C2D_COLOR_FORMAT_422_Y_U_V;
    case GST_VIDEO_FORMAT_Y41B:
      return C2D_COLOR_FORMAT_411_Y_U_V;
    case GST_VIDEO_FORMAT_IYU1:
      return C2D_COLOR_FORMAT_411_UYYVYY;
    case GST_VIDEO_FORMAT_IYU2:
      return C2D_COLOR_FORMAT_444_UYV;
    case GST_VIDEO_FORMAT_v308:
      return C2D_COLOR_FORMAT_444_YUV;
    case GST_VIDEO_FORMAT_AYUV:
      return C2D_COLOR_FORMAT_444_AYUV;
    case GST_VIDEO_FORMAT_Y444:
      return C2D_COLOR_FORMAT_444_Y_U_V;
    case GST_VIDEO_FORMAT_P010_10LE:
      return C2D_COLOR_FORMAT_420_P010;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return C2D_COLOR_FORMAT_420_TP10;
    case GST_VIDEO_FORMAT_RGBA:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_BGRA:
      return C2D_COLOR_FORMAT_8888_ARGB;
    case GST_VIDEO_FORMAT_ARGB:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_ABGR:
      return C2D_COLOR_FORMAT_8888_RGBA;
    case GST_VIDEO_FORMAT_RGBx:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA |
          C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_BGRx:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA;
    case GST_VIDEO_FORMAT_xRGB:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA |
          C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_xBGR:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA;
    case GST_VIDEO_FORMAT_RGB:
      return C2D_COLOR_FORMAT_888_RGB | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_BGR:
      return C2D_COLOR_FORMAT_888_RGB;
    case GST_VIDEO_FORMAT_RGB16:
      return C2D_COLOR_FORMAT_565_RGB | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_BGR16:
      return C2D_COLOR_FORMAT_565_RGB;
    case GST_VIDEO_FORMAT_GRAY8:
      return C2D_COLOR_FORMAT_8_L;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }
  return 0;
}

static inline guint
gst_c2d_rectangles_overlapping_area (C2D_RECT * l_rect, C2D_RECT * r_rect)
{
  gint width = 0, height = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN ((l_rect->x >> 16) + (l_rect->width >> 16),
      (r_rect->x >> 16) + (r_rect->width >> 16));
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX ((l_rect->x >> 16), (r_rect->x >> 16));

  // Negative width means that there is no overlapping, zero the value.
  width = (width < 0) ? 0 : width;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN ((l_rect->y >> 16) + (l_rect->height >> 16),
      (r_rect->y >> 16) + (r_rect->height >> 16));
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX ((l_rect->y >> 16), (r_rect->y >> 16));

  // Negative height means that there is no overlapping, zero the value.
  height = (height < 0) ? 0 : height;

  return (width * height);
}

static inline guint
gst_c2d_composition_object_area (C2D_OBJECT * objects, guint index)
{
  C2D_OBJECT *object = NULL;
  C2D_RECT *rect = NULL, C2D_RECT *l_rect = NULL;
  guint num = 0, area = 0;

  // Fetch the object at current index to which we will compare all others.
  object = &(objects[index]);

  // Calculate the target area filled with frame content.
  rect = &(object->target_rect);
  area = (rect->width >> 16) * (rect->height >> 16);

  for (num = 0; num < index; num++) {
    l_rect = &(objects[num].target_rect);

    // Subtract overlapping area from the total rectangle area.
    area -= gst_c2d_rectangles_overlapping_area (rect, l_rect);
  }

  return area;
}

gint gst_c2d_compare_compositions (const void * a, const void * b)
{
  const GstC2dComposition *l_composition = (const GstC2dComposition*) a;
  const GstC2dComposition *r_composition = (const GstC2dComposition*) b;
  gint l_dims = 0, r_dims = 0;

  l_dims = GST_VIDEO_FRAME_WIDTH (l_composition->outframe) *
      GST_VIDEO_FRAME_HEIGHT (l_composition->outframe);
  r_dims = GST_VIDEO_FRAME_WIDTH (r_composition->outframe) *
      GST_VIDEO_FRAME_HEIGHT (r_composition->outframe);

  return (l_dims < r_dims) - (l_dims > r_dims);
}

static inline gboolean
gst_c2d_blits_compatible (const GstC2dComposition * l_composition,
    const GstC2dComposition * r_composition)
{
  GstC2dBlit *l_blit = NULL, *r_blit = NULL;
  GstVideoRectangle *l_rect = NULL, *r_rect = NULL;
  guint idx = 0, num = 0, l_fd = 0, r_fd = 0;

  // TODO For now, support only same object ordering.
  for (idx = 0; idx < l_composition->n_blits; idx++) {
    l_blit = &(l_composition->blits[idx]);
    r_blit = &(r_composition->blits[idx]);

    // Both entries need to have the same flags and global alpha.
    if ((l_blit->flags != r_blit->flags) || (l_blit->alpha != r_blit->alpha))
      return FALSE;

    l_fd = gst_fd_memory_get_fd (
        gst_buffer_peek_memory (l_blit->frame->buffer, 0));
    r_fd = gst_fd_memory_get_fd (
        gst_buffer_peek_memory (r_blit->frame->buffer, 0));

    // The FDs of both entries must match.
    if (l_fd != r_fd)
      return FALSE;

    // Both entries must have same number of Source - Destionation pairs.
    if (l_blit->n_regions != r_blit->n_regions)
      return FALSE;

    for (num = 0; num < l_blit->n_regions; num++) {
      l_rect = &(l_blit->sources[num]);
      r_rect = &(r_blit->sources[num]);

      // Source rectangles must match.
      if ((l_rect->x != r_rect->x) || (l_rect->y != r_rect->y) ||
          (l_rect->w != r_rect->w) || (l_rect->h != r_rect->h))
        return FALSE;

      l_rect = &(l_blit->destinations[num]);
      r_rect = &(r_blit->destinations[num]);

      // Adjust the dimensions of the target rectangles to be in the same scale.
      r_rect->x = gst_util_uint64_scale_int (r_rect->x,
          GST_VIDEO_FRAME_WIDTH (l_composition->frame),
          GST_VIDEO_FRAME_WIDTH (r_composition->frame));

      r_rect->y = gst_util_uint64_scale_int (r_rect->y,
          GST_VIDEO_FRAME_HEIGHT (l_composition->frame),
          GST_VIDEO_FRAME_HEIGHT (r_composition->frame));

      r_rect->w = gst_util_uint64_scale_int (r_rect->w,
          GST_VIDEO_FRAME_WIDTH (l_composition->frame),
          GST_VIDEO_FRAME_WIDTH (r_composition->frame));

      r_rect->h = gst_util_uint64_scale_int (r_rect->h,
          GST_VIDEO_FRAME_HEIGHT (l_composition->frame),
          GST_VIDEO_FRAME_HEIGHT (r_composition->frame));

      // Target ractangles may not match but must have maximum of 1 pixel delta.
      if ((ABS (l_rect->x - r_rect->x) > 1) || (ABS (l_rect->y - r_rect->y) > 1) ||
          (ABS (l_rect->w - r_rect->w) > 1) || (ABS (l_rect->h - r_rect->h) > 1))
        return FALSE;
    }
  }

  return TRUE;
}

static inline gboolean
gst_c2d_optimize_composition (GstC2dBlit * blit,
    const GstC2dComposition * compositions, const guint index)
{
  const GstC2dComposition *composition = &(compositions[index]);
  const GstC2dComposition *l_composition = = NULL;
  gint l_score = -1, score = -1;
  gdouble l_ratio = 0.0, ratio = 0.0;
  guint num = 0, l_resolution = 0, resolution = 0;
  gboolean optimized = FALSE;

  gst_util_fraction_to_double (GST_VIDEO_FRAME_WIDTH (composition->frame),
      GST_VIDEO_FRAME_HEIGHT (composition->frame), &ratio);

  resolution = GST_VIDEO_FRAME_WIDTH (composition->frame) *
      GST_VIDEO_FRAME_HEIGHT (composition->frame);

  // Find the best compatible blit composition to current one.
  for (num = 0; num < index; num++) {
    l_composition = &(compositions[num]);

    // The number of blit entries must be the same.
    if (l_composition->n_blits != composition->n_blits)
      continue;

    // Background color settings have to match.
    if (l_composition->bgcolor != composition->bgcolor)
      continue;

    gst_util_fraction_to_double (GST_VIDEO_FRAME_WIDTH (l_composition->frame),
        GST_VIDEO_FRAME_HEIGHT (l_composition->frame), &l_ratio);

    // Both target surfaces must have the same aspect ratio within tolerance.
    if (FABS (l_ratio - ratio) > 0.005)
      continue;

    l_resolution = GST_VIDEO_FRAME_WIDTH (l_composition->frame) *
        GST_VIDEO_FRAME_HEIGHT (l_composition->frame);

    // The blit surface must have the same or lower resolution.
    if (resolution > l_resolution)
      continue;

    // Compare blit entries.
    if (!gst_c2d_blits_compatible (l_composition, composition))
      continue;

    // Increase the score if both target blit surfaces have the same dimensions.
    l_score = (l_resolution == resolution) ? 1 : 0;
    // Increase the score if both target blit surfaces have the same format flags.
    l_score += (l_composition->frame->info.finfo->flags ==
        composition->frame->info.finfo->flags) ? 1 : 0;
    // Increase the score if both target blit surfaces have the same format.
    l_score += (GST_VIDEO_FRAME_FORMAT (l_composition->frame) ==
        GST_VIDEO_FRAME_FORMAT (composition->frame)) ? 1 : 0;
    // Increase the score if both target blit surfaces have the same UBWC flag.
    l_score += ((l_composition->flags & GST_C2D_FLAG_UBWC_FORMAT) ==
        (composition->flags & GST_C2D_FLAG_UBWC_FORMAT)) ? 1 : 0;

    if (l_score <= score)
      continue;

    // Update the current high score tracker.
    score = l_score;

    blit.frame = compositions[num].frame;
    blit.flags = l_composition->flags & GST_C2D_FLAG_UBWC_FORMAT;

    optimized = TRUE;
  }

  return optimized;
}

static gpointer
gst_c2d_map_gpu_address (GstC2dVideoConverter * convert,
    const GstVideoFrame * frame)
{
  C2D_STATUS status = C2D_STATUS_OK;
  gpointer gpuaddress = NULL;

  gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (frame->buffer, 0));

  status = convert->MapAddr (fd, frame->map->data, frame->map->size, 0,
      KGSL_USER_MEM_TYPE_ION, &gpuaddress);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to map buffer data %p with size %" G_GSIZE_FORMAT
        " and fd %d to GPU!", frame->map->data, frame->map->size, fd);
    return NULL;
  }
  GST_DEBUG ("Mapped data %p with size %" G_GSIZE_FORMAT " and fd %d to "
      "GPU address %p", frame->map->data, frame->map->size, fd, gpuaddress);
  return gpuaddress;
}

static void
gst_c2d_unmap_gpu_address (gpointer key, gpointer data, gpointer userdata)
{
  GstC2dVideoConverter *convert = (GstC2dVideoConverter*) userdata;
  guint surface_id = GPOINTER_TO_UINT (key);
  C2D_STATUS status = C2D_STATUS_OK;

  status = convert->UnMapAddr (data);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to unmap GPU address %p for surface %x, error: %d",
        data, surface_id, status);
    return;
  }
  GST_DEBUG ("Unmapped GPU address %p for surface %x", data, surface_id);
  return;
}

static guint
gst_c2d_create_surface (GstC2dVideoConverter * convert,
    const GstVideoFrame * frame, guint bits, gboolean isubwc)
{
  const gchar *format = NULL, *compression = NULL;
  guint surface_id = 0;
  C2D_STATUS status = C2D_STATUS_NOT_SUPPORTED;

  gpointer gpuaddress = gst_c2d_map_gpu_address (convert, frame);
  g_return_val_if_fail (gpuaddress != NULL, 0);

  format = gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame));

  if (GST_VIDEO_INFO_IS_RGB (&frame->info) ||
      GST_VIDEO_INFO_IS_GRAY (&frame->info)) {
    C2D_RGB_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_val_if_fail (surface.format != 0, 0);

    // In case the format has UBWC enabled append additional format flags.
    if (isubwc) {
      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;
      compression = " UBWC";
    } else {
      compression = "";
    }

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s%s surface - width(%u) height(%u)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.width, surface.height);

    // Plane stride.
    surface.stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

    GST_DEBUG ("%s %s%s surface - stride(%d)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.stride);

    // Set plane virtual and GPU address.
    surface.buffer = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    surface.phys = gpuaddress;

    GST_DEBUG ("%s %s%s surface - plane(%p) phys(%p)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.buffer, surface.phys);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

    // Create RGB surface.
    status = convert->CreateSurface(&surface_id, bits, type, &surface);
  } else if (GST_VIDEO_INFO_IS_YUV (&frame->info)) {
    C2D_YUV_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_val_if_fail (surface.format != 0, 0);

    // In case the format has UBWC enabled append additional format flags.
    if (isubwc) {
      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;
      compression = " UBWC";
    } else {
      compression = "";
    }

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s%s surface - width(%u) height(%u)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.width, surface.height);

    // Y plane stride.
    surface.stride0 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
    // UV plane (U plane in planar format) plane stride.
    surface.stride1 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 2) ?
        GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1) : 0;
    // V plane (planar format, ignored in other formats) plane stride.
    surface.stride2 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 3) ?
        GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2) : 0;

    GST_DEBUG ("%s %s%s surface - stride0(%d) stride1(%d) stride2(%d)",
        !(bits & C2D_TARGET) ? "Input" : "Output", format, compression,
        surface.stride0, surface.stride1, surface.stride2);

    // Y plane virtual address.
    surface.plane0 = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    // UV plane (U plane in planar format) plane virtual address.
    surface.plane1 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 2) ?
        GST_VIDEO_FRAME_PLANE_DATA (frame, 1) : NULL;
    // V plane (planar format, ignored in other formats) plane virtual address.
    surface.plane2 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 3) ?
        GST_VIDEO_FRAME_PLANE_DATA (frame, 2) : NULL;

    GST_DEBUG ("%s %s%s surface - plane0(%p) plane1(%p) plane2(%p)",
        !(bits & C2D_TARGET) ? "Input" : "Output", format, compression,
        surface.plane0, surface.plane1, surface.plane2);

    // Y plane GPU address.
    surface.phys0 = gpuaddress;
    // UV plane (U plane in planar format)  GPU address.
    surface.phys1 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 2) ?
        GSIZE_TO_POINTER (GPOINTER_TO_SIZE (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1)) : NULL;
    // V plane (planar format, ignored in other formats) GPU address.
    surface.phys2 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 3) ?
        GSIZE_TO_POINTER (GPOINTER_TO_SIZE (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 2)) : NULL;

    GST_DEBUG ("%s %s%s surface - phys0(%p) phys1(%p) phys2(%p)",
         !(bits & C2D_TARGET) ? "Input" : "Output", format, compression,
         surface.phys0, surface.phys1, surface.phys2);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);

    // Create YUV surface.
    status = convert->CreateSurface(&surface_id, bits, type, &surface);
  } else {
    GST_ERROR ("Unsupported format %s !", format);
  }

  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to create %s C2D surface, error: %d!",
        !(bits & C2D_TARGET) ? "Input" : "Output", status);
    gst_c2d_unmap_gpu_address (NULL, gpuaddress, convert);
    return 0;
  }

  g_hash_table_insert (convert->gpulist, GUINT_TO_POINTER (surface_id),
       gpuaddress);
  g_hash_table_insert (convert->vaddrlist, GUINT_TO_POINTER (surface_id),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 0));

  GST_DEBUG ("Created %s surface with id %x", !(bits & C2D_TARGET) ?
      "input" : "output", surface_id);
  return surface_id;
}

static gboolean
gst_c2d_update_surface (GstC2dVideoConverter * convert,
    const GstVideoFrame * frame, guint surface_id, guint bits, gboolean isubwc)
{
  const gchar *format = NULL, *compression = NULL;
  C2D_STATUS status = C2D_STATUS_NOT_SUPPORTED;
  gpointer gpuaddress = NULL;

  gpuaddress = g_hash_table_lookup (convert->gpulist,
      GUINT_TO_POINTER (surface_id));
  status = convert->UnMapAddr (gpuaddress);

  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to unmap GPU address %p for surface %x, error: %d",
        gpuaddress, surface_id, status);
    return FALSE;
  }

  gpuaddress = gst_c2d_map_gpu_address (convert, frame);
  g_return_val_if_fail (gpuaddress != NULL, FALSE);

  format = gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame));

  if (GST_VIDEO_INFO_IS_RGB (&frame->info) ||
      GST_VIDEO_INFO_IS_GRAY (&frame->info)) {
    C2D_RGB_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_val_if_fail (surface.format != 0, FALSE);

    // In case the format has UBWC enabled append additional format flags.
    if (isubwc) {
      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;
      compression = " UBWC";
    } else {
      compression = "";
    }

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s%s surface - width(%u) height(%u)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.width, surface.height);

    // Plane stride.
    surface.stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

    GST_DEBUG ("%s %s%s surface - stride(%d)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.stride);

    // Set plane virtual and GPU address.
    surface.buffer = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    surface.phys = gpuaddress;

    GST_DEBUG ("%s %s%s surface - plane(%p) phys(%p)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.buffer, surface.phys);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

    // Update RGB surface.
    status = convert->UpdateSurface(surface_id, bits, type, &surface);
  } else if (GST_VIDEO_INFO_IS_YUV (&frame->info)) {
    C2D_YUV_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_val_if_fail (surface.format != 0, FALSE);

    // In case the format has UBWC enabled append additional format flags.
    if (isubwc) {
      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;
      compression = " UBWC";
    } else {
      compression = "";
    }

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s%s surface - width(%u) height(%u)", !(bits & C2D_TARGET) ?
        "Input" : "Output", format, compression, surface.width, surface.height);

    // Y plane stride.
    surface.stride0 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
    // UV plane (U plane in planar format) plane stride.
    surface.stride1 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 2) ?
        GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1) : 0;
    // V plane (planar format, ignored in other formats) plane stride.
    surface.stride2 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 3) ?
        GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2) : 0;

    GST_DEBUG ("%s %s%s surface - stride0(%d) stride1(%d) stride2(%d)",
        !(bits & C2D_TARGET) ? "Input" : "Output", format, compression,
        surface.stride0, surface.stride1, surface.stride2);

    // Y plane virtual address.
    surface.plane0 = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    // UV plane (U plane in planar format) plane virtual address.
    surface.plane1 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 2) ?
        GST_VIDEO_FRAME_PLANE_DATA (frame, 1) : NULL;
    // V plane (planar format, ignored in other formats) plane virtual address.
    surface.plane2 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 3) ?
        GST_VIDEO_FRAME_PLANE_DATA (frame, 2) : NULL;

    GST_DEBUG ("%s %s%s surface - plane0(%p) plane1(%p) plane2(%p)",
        !(bits & C2D_TARGET) ? "Input" : "Output", format, compression,
        surface.plane0, surface.plane1, surface.plane2);

    // Y plane GPU address.
    surface.phys0 = gpuaddress;
    // UV plane (U plane in planar format)  GPU address.
    surface.phys1 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 2) ?
        GSIZE_TO_POINTER (GPOINTER_TO_SIZE (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1)) : NULL;
    // V plane (planar format, ignored in other formats) GPU address.
    surface.phys2 = (GST_VIDEO_FRAME_N_PLANES (frame) >= 3) ?
        GSIZE_TO_POINTER (GPOINTER_TO_SIZE (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 2)) : NULL;

    GST_DEBUG ("%s %s%s surface - phys0(%p) phys1(%p) phys2(%p)",
         !(bits & C2D_TARGET) ? "Input" : "Output", format, compression,
         surface.phys0, surface.phys1, surface.phys2);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);

    // Update YUV surface.
    status = convert->UpdateSurface(surface_id, bits, type, &surface);
  } else {
    GST_ERROR ("Unsupported format %s !", format);
  }

  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to Update %s C2D surface, error: %d!",
        !(bits & C2D_TARGET) ? "Input" : "Output", status);
    gst_c2d_unmap_gpu_address (NULL, gpuaddress, convert);
    return FALSE;
  }

  g_hash_table_insert (convert->gpulist, GUINT_TO_POINTER (surface_id),
      gpuaddress);
  g_hash_table_insert (convert->vaddrlist, GUINT_TO_POINTER (surface_id),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 0));

  GST_DEBUG ("Updated %s surface with id %x", !(bits & C2D_TARGET) ?
      "input" : "output", surface_id);
  return TRUE;
}

static void
gst_c2d_destroy_surface (gpointer key, gpointer value, gpointer userdata)
{
  GstC2dVideoConverter *convert = (GstC2dVideoConverter*) userdata;
  guint surface_id = GPOINTER_TO_UINT (value);
  C2D_STATUS status = C2D_STATUS_OK;

  status = convert->DestroySurface(surface_id);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to destroy C2D surface %x for key %p, error: %d!",
        surface_id, key, status);
    return;
  }
  GST_DEBUG ("Destroyed surface with id %x", surface_id);
  return;
}

static void
gst_c2d_update_object (C2D_OBJECT * object, const guint surface_id,
    const GstVideoFrame * inframe, guint8 alpha, guint64 flags,
    const GstVideoRectangle * source, const GstVideoRectangle * destination,
    const GstVideoFrame * outframe)
{
  gint x = 0, y = 0, width = 0, height = 0;

  object->surface_id = surface_id;
  object->config_mask = (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT);

  object->global_alpha = alpha;
  GST_TRACE ("Input surface %x - Global alpha: %u", surface_id,
      object->global_alpha);

  if (object->global_alpha != G_MAXUINT8)
    object->config_mask |= C2D_GLOBAL_ALPHA_BIT;

  // Setup the source rectangle.
  if (source != NULL) {
    x = source->x;
    y = source->y;
    width = source->w;
    height = source->h;
  }

  width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) :
      MIN (width, GST_VIDEO_FRAME_WIDTH (inframe) - x);
  height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) :
      MIN (height, GST_VIDEO_FRAME_HEIGHT (inframe) - y);

  object->source.x = x << 16;
  object->source.y = y << 16;
  object->source.width = width << 16;
  object->source.height = height << 16;

  // Apply the flip bits to the object configure mask if set.
  object->config_mask &= ~(C2D_MIRROR_V_BIT | C2D_MIRROR_H_BIT);

  if (flags & GST_C2D_FLAG_FLIP_VERTICAL) {
    object->config_mask |= C2D_MIRROR_V_BIT;
    GST_TRACE ("Input surface %x - Flip Vertically", surface_id);
  }

  if (flags & GST_C2D_FLAG_FLIP_HORIZONTAL) {
    object->config_mask |= C2D_MIRROR_H_BIT;
    GST_TRACE ("Input surface %x - Flip Horizontally", surface_id);
  }

  // Setup the target rectangle.
  if (destination != NULL) {
    x = destination->x;
    y = destination->y;
    width = destination->w;
    height = destination->h;
  }

  // Setup rotation angle and adjustments.
  switch (flags & (0b11 << 2)) {
    case GST_C2D_FLAG_ROTATE_90CW:
    {
      gint dar_n = 0, dar_d = 0;

      gst_util_fraction_multiply (
          GST_VIDEO_FRAME_WIDTH (inframe), GST_VIDEO_FRAME_HEIGHT (inframe),
          GST_VIDEO_INFO_PAR_N (&(inframe)->info),
          GST_VIDEO_INFO_PAR_D (&(inframe)->info),
          &dar_n, &dar_d
      );

      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_270);
      GST_LOG ("Input surface %x - rotate 90° clockwise", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width != 0) ? width :
          GST_VIDEO_FRAME_HEIGHT (outframe) * dar_d / dar_n;
      height = (height != 0) ? height : GST_VIDEO_FRAME_HEIGHT (outframe);

      x = (destination != NULL) ?
          x : (GST_VIDEO_FRAME_WIDTH (outframe) - width) / 2;

      object->target_rect.width = height << 16;
      object->target_rect.height = width << 16;

      // Adjust the target rectangle coordinates.
      object->target_rect.y =
          (GST_VIDEO_FRAME_WIDTH (outframe) - (x + width)) << 16;
      object->target_rect.x = y << 16;
      break;
    }
    case GST_C2D_FLAG_ROTATE_180:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_180);
      GST_LOG ("Input surface %x - rotate 180°", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (outframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (outframe) : height;

      object->target_rect.width = width << 16;
      object->target_rect.height = height << 16;

      // Adjust the target rectangle coordinates.
      object->target_rect.x =
          (GST_VIDEO_FRAME_WIDTH (outframe) - (x + width)) << 16;
      object->target_rect.y =
          (GST_VIDEO_FRAME_HEIGHT (outframe) - (y + height)) << 16;
      break;
    case GST_C2D_FLAG_ROTATE_90CCW:
    {
      gint dar_n = 0, dar_d = 0;

      gst_util_fraction_multiply (
          GST_VIDEO_FRAME_WIDTH (inframe), GST_VIDEO_FRAME_HEIGHT (inframe),
          GST_VIDEO_INFO_PAR_N (&(inframe)->info),
          GST_VIDEO_INFO_PAR_D (&(inframe)->info),
          &dar_n, &dar_d
      );

      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_90);
      GST_LOG ("Input surface %x - rotate 90° counter-clockwise", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width != 0) ? width :
          GST_VIDEO_FRAME_HEIGHT (outframe) * dar_d / dar_n;
      height = (height != 0) ? height : GST_VIDEO_FRAME_HEIGHT (outframe);

      object->target_rect.width = height << 16;
      object->target_rect.height = width << 16;

      x = (destination != NULL) ?
          x : (GST_VIDEO_FRAME_WIDTH (outframe) - width) / 2;

      // Adjust the target rectangle coordinates.
      object->target_rect.x =
          (GST_VIDEO_FRAME_HEIGHT (outframe) - (y + height)) << 16;
      object->target_rect.y = x << 16;
      break;
    }
    default:
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (outframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (outframe) : height;

      object->target_rect.width = width << 16;
      object->target_rect.height = height << 16;

      object->target_rect.x = x << 16;
      object->target_rect.y = y << 16;

      // Remove all rotation flags.
      object->config_mask &=
          ~(C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
            C2D_OVERRIDE_TARGET_ROTATE_90 | C2D_OVERRIDE_TARGET_ROTATE_180 |
            C2D_OVERRIDE_TARGET_ROTATE_270);
      break;
  }

  // Clear the scissor rectangle and the remaining C2D_OBJECT fields.
  object->next = NULL;

  object->scissor_rect.x = object->scissor_rect.y = 0;
  object->scissor_rect.width = object->scissor_rect.height = 0;

  object->mask_surface_id = 0;
  object->color_key = 0;

  object->rot_orig_x = object->rot_orig_y = 0;
  object->rotation = 0;

  object->fg_color = object->bg_color= 0;
  obejct->palette_id = 0;

  GST_TRACE ("Input surface %x - Source rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->source.x >> 16, object->source.y >> 16,
      object->source.width >> 16, object->source.height >> 16);

  GST_TRACE ("Input surface %x - Target rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->target_rect.x >> 16, object->target_rect.y >> 16,
      object->target_rect.width >> 16, object->target_rect.height >> 16);

  GST_TRACE ("Input surface %x - Scissor rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->scissor_rect.x >> 16, object->scissor_rect.y >> 16,
      object->scissor_rect.width >> 16, object->scissor_rect.height >> 16);
}

static guint
gst_c2d_retrieve_surface_id (GstC2dVideoConverter * convert,
    GHashTable * surfaces, guint bits, const GstVideoFrame * vframe,
    const gboolean isubwc)
{
  GstMemory *memory = NULL;
  guint fd = 0, surface_id = 0;

  // Get the 1st (and only) memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (vframe->buffer, 0);

  if ((memory == NULL) || !gst_is_fd_memory (memory)) {
    GST_ERROR ("Buffer %p does not have FD memory!", vframe->buffer);
    return 0;
  }

  // Get the input buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (surfaces, GUINT_TO_POINTER (fd))) {
    // Create an output surface and add its ID to the output hash table.
    surface_id = gst_c2d_create_surface (convert, vframe, bits, isubwc);

    if (surface_id == 0) {
      GST_ERROR ("Failed to create surface!");
      return 0;
    }

    g_hash_table_insert (surfaces, GUINT_TO_POINTER (fd),
        GUINT_TO_POINTER (surface_id));
  } else {
    gpointer vaddress = NULL;

    // Get the input surface ID from the input hash table.
    surface_id = GPOINTER_TO_UINT (
        g_hash_table_lookup (surfaces, GUINT_TO_POINTER (fd)));
    vaddress = g_hash_table_lookup (convert->vaddrlist,
        GUINT_TO_POINTER (surface_id));

    if (vaddress != GST_VIDEO_FRAME_PLANE_DATA (vframe, 0) &&
        !gst_c2d_update_surface (convert, vframe, surface_id, bits, isubwc)) {
      GST_ERROR ("Update failed for surface %x", surface_id);
      return 0;
    }
  }

  return surface_id;
}

GstC2dVideoConverter *
gst_c2d_video_converter_new ()
{
  GstC2dVideoConverter *convert;
  gboolean success = TRUE;
  C2D_DRIVER_SETUP_INFO setup;
  C2D_DRIVER_INFO info;
  C2D_STATUS status = C2D_STATUS_OK;

  convert = g_slice_new0 (GstC2dVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  // Load C2D library.
  if ((convert->c2dhandle = dlopen ("libC2D2.so", RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open C2D library, error: %s!", dlerror());
    goto cleanup;
  }

  // Load C2D library symbols.
  success &= load_symbol ((gpointer*)&convert->DriverInit, convert->c2dhandle,
      "c2dDriverInit");
  success &= load_symbol ((gpointer*)&convert->DriverDeInit, convert->c2dhandle,
      "c2dDriverDeInit");
  success &= load_symbol ((gpointer*)&convert->CreateSurface,
      convert->c2dhandle, "c2dCreateSurface");
  success &= load_symbol ((gpointer*)&convert->DestroySurface,
      convert->c2dhandle, "c2dDestroySurface");
  success &= load_symbol ((gpointer*)&convert->UpdateSurface,
      convert->c2dhandle, "c2dUpdateSurface");
  success &= load_symbol ((gpointer*)&convert->QuerySurface,
      convert->c2dhandle, "c2dQuerySurface");
  success &= load_symbol ((gpointer*)&convert->SurfaceUpdated,
      convert->c2dhandle, "c2dSurfaceUpdated");
  success &= load_symbol ((gpointer*)&convert->FillSurface,
      convert->c2dhandle, "c2dFillSurface");
  success &= load_symbol ((gpointer*)&convert->Draw, convert->c2dhandle,
      "c2dDraw");
  success &= load_symbol ((gpointer*)&convert->Flush, convert->c2dhandle,
      "c2dFlush");
  success &= load_symbol ((gpointer*)&convert->Finish, convert->c2dhandle,
      "c2dFinish");
  success &= load_symbol ((gpointer*)&convert->WaitTimestamp,
      convert->c2dhandle, "c2dWaitTimestamp");
  success &= load_symbol ((gpointer*)&convert->MapAddr, convert->c2dhandle,
      "c2dMapAddr");
  success &= load_symbol ((gpointer*)&convert->UnMapAddr, convert->c2dhandle,
      "c2dUnMapAddr");
  success &= load_symbol ((gpointer*)&convert->GetDriverCapabilities,
      convert->c2dhandle, "c2dGetDriverCapabilities");

  // Check whether symbol loading was successful.
  if (!success)
    goto cleanup;

  if ((convert->insurfaces = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for source surfaces!");
    goto cleanup;
  }

  if ((convert->outsurfaces = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for target surfaces!");
    goto cleanup;
  }

  if ((convert->gpulist = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for GPU mapped addresses!");
    goto cleanup;
  }

  if ((convert->vaddrlist = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for mapped virtual addresses!");
    goto cleanup;
  }

  setup.max_object_list_needed = C2D_INIT_MAX_OBJECT;
  setup.max_surface_template_needed = C2D_INIT_MAX_TEMPLATE;

  G_LOCK (c2d);

  if (refcount++ == 0)
    status = convert->DriverInit (&setup);

  G_UNLOCK (c2d);

  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to initialize driver!");
    goto cleanup;
  }

  status = convert->GetDriverCapabilities (&info);
  if (C2D_STATUS_OK == status) {
    GST_DEBUG ("C2D_DRIVER Capabilities:");
    GST_DEBUG ("    Maximum dimensions: %ux%u", info.max_surface_width,
        info.max_surface_height);
    CHECK_C2D_CAPABILITY (info, GLOBAL_ALPHA_OP);
    CHECK_C2D_CAPABILITY (info, TILE_OP);
    CHECK_C2D_CAPABILITY (info, COLOR_KEY_OP);
    CHECK_C2D_CAPABILITY (info, NO_PIXEL_ALPHA_OP);
    CHECK_C2D_CAPABILITY (info, TARGET_ROTATE_OP);
    CHECK_C2D_CAPABILITY (info, ANTI_ALIASING_OP);
    CHECK_C2D_CAPABILITY (info, BILINEAR_FILTER_OP);
    CHECK_C2D_CAPABILITY (info, LENS_CORRECTION_OP);
    CHECK_C2D_CAPABILITY (info, OVERRIDE_TARGET_ROTATE_OP);
    CHECK_C2D_CAPABILITY (info, SHADER_BLOB_OP);
    CHECK_C2D_CAPABILITY (info, MASK_SURFACE_OP);
    CHECK_C2D_CAPABILITY (info, MIRROR_H_OP);
    CHECK_C2D_CAPABILITY (info, MIRROR_V_OP);
    CHECK_C2D_CAPABILITY (info, SCISSOR_RECT_OP);
    CHECK_C2D_CAPABILITY (info, SOURCE_RECT_OP);
    CHECK_C2D_CAPABILITY (info, TARGET_RECT_OP);
    CHECK_C2D_CAPABILITY (info, ROTATE_OP);
    CHECK_C2D_CAPABILITY (info, FLUSH_WITH_FENCE_FD_OP);
    CHECK_C2D_CAPABILITY (info, UBWC_COMPRESSED_OP);
  }

  GST_INFO ("Created C2D converter: %p", convert);
  return convert;

cleanup:
  gst_c2d_video_converter_free (convert);
  return NULL;
}

void
gst_c2d_video_converter_free (GstC2dVideoConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->insurfaces != NULL) {
    g_hash_table_foreach (convert->insurfaces, gst_c2d_destroy_surface, convert);
    g_hash_table_destroy(convert->insurfaces);
  }

  if (convert->outsurfaces != NULL) {
    g_hash_table_foreach (convert->outsurfaces, gst_c2d_destroy_surface, convert);
    g_hash_table_destroy (convert->outsurfaces);
  }

  if (convert->gpulist != NULL) {
    g_hash_table_foreach (convert->gpulist, gst_c2d_unmap_gpu_address, convert);
    g_hash_table_destroy (convert->gpulist);
  }

  if (convert->vaddrlist != NULL)
    g_hash_table_destroy (convert->vaddrlist);

  G_LOCK (c2d);

  if (convert->DriverDeInit != NULL && ((--refcount) == 0))
    convert->DriverDeInit ();

  G_UNLOCK (c2d);

  if (convert->c2dhandle != NULL)
    dlclose (convert->c2dhandle);

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed C2D converter: %p", convert);
  g_slice_free (GstC2dVideoConverter, convert);
}

gpointer
gst_c2d_video_converter_submit_request (GstC2dVideoConverter * convert,
    GstC2dComposition * compositions, guint n_compositions)
{
  GArray *requests = NULL;
  G2D_OBJECT objects[GST_C2D_MAX_DRAW_OBJECTS] = { 0, };
  guint idx = 0, num = 0, surface_id = 0, area = 0;
  gboolean isubwc = FALSE;
  C2D_STATUS status = C2D_STATUS_OK;

  g_return_val_if_fail (convert != NULL, NULL);
  g_return_val_if_fail ((compositions != NULL) && (n_compositions != 0), NULL);

  requests = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_compositions);
  g_array_set_size (requests, n_compositions);

  // Sort compositions by output frame dimensions.
  qsort (compositions, n_compositions, sizeof (GstC2dComposition),
      gst_c2d_compare_compositions);

  for (idx = 0; idx < n_compositions; idx++) {
    GstVideoFrame *outframe = NULL;
    GstC2dBlit *blits = NULL, l_blit = GST_C2D_BLIT_INIT;
    guint n_blits = 0, n_objects = 0;
    gboolean optimized = FALSE;

    // Sanity checks, output frame and blit entries must not be NULL.
    g_return_val_if_fail (compositions[idx].frame != NULL, NULL);
    g_return_val_if_fail (compositions[idx].blits != NULL, NULL);
    g_return_val_if_fail (compositions[idx].n_blits != 0, NULL);

    outframe = compositions[idx].frame;

    // Optimize current composition to use an existing output as blit entry.
    // If a suitable composition is found then the local blit enry is filled.
    optimized = gst_c2d_optimize_composition (&l_blit, compositions, idx);

    blits = optimized ? (&l_blit) : compositions[idx].blits;
    n_blits = optimized ? 1 : compositions[idx].n_blits;

    // Total area of the output frame that is to be used in later calculations
    // to determine whether there are unoccupied background pixels to be filled.
    area = GST_VIDEO_FRAME_WIDTH (outframe) * GST_VIDEO_FRAME_HEIGHT (outframe);

    // Iterate over the input blit entries and update each C2D_OBJECT for draw.
    for (num = 0; num < n_blits; num++) {
      GstC2dBlit *blit = &(blits[num]);
      guint r_idx = 0;

      GST_C2D_LOCK (convert);

      isubwc = blit->flags & GST_C2D_FLAG_UBWC_FORMAT;
      surface_id = gst_c2d_retrieve_surface_id (convert, convert->insurfaces,
          C2D_SOURCE, blit->frame, isubwc);

      GST_C2D_UNLOCK (convert);

      if (surface_id == 0) {
        GST_ERROR ("Failed to get surface ID for input buffer %p at index %u "
            "in composition %u!", blit->frame->buffer, num, idx);
        goto cleanup;
      }

      // Update a new C2D object (at least 1) for each source/destnation pair.
      do {
        GstVideoRectangle *s_region = NULL, *d_region = NULL;

        if (n_objects >= GST_C2D_MAX_DRAW_OBJECTS) {
          GST_ERROR ("Number of objects exceeds %d!", GST_C2D_MAX_DRAW_OBJECTS);
          goto cleanup;
        }

        s_region = (blit->n_regions != 0) ? &(blit->sources[r_idx]) : NULL;
        d_region = (blit->n_regions != 0) ? &(blit->destinations[r_idx]) : NULL;

        gst_c2d_update_object (&(objects[n_objects]), surface_id, blit->frame,
            blit->alpha, blit->flags, s_region, d_region, outframe);

        // Subtract object area from the total area.
        area -= gst_c2d_composition_object_area (objects, n_objects);

        // Set previous object to point to the current one (linked list).
        if (n_objects != 0)
          objects[n_objects - 1].next = &(objects[n_objects]);

        // Increment the counter for the total number of C2D objects.
        n_objects++;
      } while (++r_idx < blit->n_regions);
    }

    GST_C2D_LOCK (convert);

    isubwc = compositions[idx].flags & GST_C2D_FLAG_UBWC_FORMAT;
    surface_id = gst_c2d_retrieve_surface_id (convert, convert->outsurfaces,
        C2D_SOURCE | C2D_TARGET, outframe, isubwc);

    GST_C2D_UNLOCK (convert);

    if (surface_id == 0) {
      GST_ERROR ("Failed to get surface ID for output buffer %p in "
          "composition %u!", outframe->buffer, idx);
      goto cleanup;
    }

    // Fill the surface if there is visible background area.
    if ((compositions[idx].flags & GST_C2D_FLAG_CLEAR_BACKGROUND) && (area > 0)) {
      GST_LOG ("Fill output surface %x", surface_id);
      status = convert->FillSurface (surface_id, compositions[idx].bgcolor, NULL);

      if (status != C2D_STATUS_OK) {
        GST_ERROR ("Fill failed for surface %x, error: %d!", surface_id, status);
        goto cleanup;
      }
    }

    GST_LOG ("Draw output surface %x", surface_id);
    status = convert->Draw (surface_id, 0, NULL, 0, 0, objects, n_blits);

    if (status != C2D_STATUS_OK) {
      GST_ERROR ("Draw failed for surface %x, error: %d!", surface_id, status);
      goto cleanup;
    }

    g_array_index (requests, guint, idx) = surface_id;
  }

  return requests;

cleanup:
  g_array_free (requests, TRUE),
  return NULL;
}

gboolean
gst_c2d_video_converter_wait_request (GstC2dVideoConverter *convert,
    gpointer request_id)
{
  GArray *requests = (GArray*) request_id;
  C2D_STATUS status = C2D_STATUS_OK;
  guint idx = 0, surface_id = 0;
  gboolean success = TRUE;

  g_return_val_if_fail (convert != NULL, FALSE);

  if (NULL == requests) {
    GST_ERROR ("Invalid request ID!");
    return FALSE;
  }

  for (idx = 0; idx < requests->len; idx++) {
    surface_id = g_array_index (requests, guint, idx);
    GST_LOG ("Waiting surface_id: %x", surface_id);

    if ((status = convert->Finish (surface_id)) != C2D_STATUS_OK) {
      GST_ERROR ("Finish failed for surface %x, error: %d!", surface_id, status);

      success &= FALSE;
      continue;
    }

    GST_LOG ("Finished waiting surface_id: %x", surface_id);
  }

  g_array_free (requests, TRUE);
  return success;
}

void
gst_c2d_video_converter_flush (GstC2dVideoConverter *convert)
{
  C2D_STATUS status = C2D_STATUS_OK;
  GHashTableIter iter;
  gpointer key, value;

  g_return_if_fail (convert != NULL);

  GST_LOG ("Forcing pending requests to complete");

  g_hash_table_iter_init (&iter, convert->outsurfaces);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    guint fd = GPOINTER_TO_UINT (key);
    guint surface_id = GPOINTER_TO_UINT (value);

    if ((status = convert->Finish (surface_id)) != C2D_STATUS_OK)
      GST_ERROR ("c2dFinish failed for surface %x and fd %d, error: %d!",
          surface_id, fd, status);
  }

  GST_LOG ("Finished pending requests");

  GST_C2D_LOCK (convert);

  if (convert->insurfaces != NULL) {
    g_hash_table_foreach (convert->insurfaces, gst_c2d_destroy_surface, convert);
    g_hash_table_remove_all (convert->insurfaces);
  }

  if (convert->outsurfaces != NULL) {
    g_hash_table_foreach (convert->outsurfaces, gst_c2d_destroy_surface, convert);
    g_hash_table_remove_all (convert->outsurfaces);
  }

  if (convert->gpulist != NULL) {
    g_hash_table_foreach (convert->gpulist, gst_c2d_unmap_gpu_address, convert);
    g_hash_table_remove_all (convert->gpulist);
  }

  if (convert->vaddrlist != NULL)
    g_hash_table_remove_all (convert->vaddrlist);

  GST_C2D_UNLOCK (convert);
  return;
}
