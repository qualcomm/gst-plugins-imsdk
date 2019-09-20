/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#include "c2d_video_converter.h"

#include <stdint.h>
#include <dlfcn.h>
#include <unistd.h>

#include <adreno/c2d2.h>
#include <adreno/c2dExt.h>
#include <linux/msm_kgsl.h>
#include <media/msm_media_info.h>

#define C2D_RETURN_NULL_IF_FAIL(expr, cleanup) \
  if (!(expr)) { \
    cleanup; \
    return NULL; \
  }

#define C2D_RETURN_NULL_IF_FAIL_WITH_MSG(expr, cleanup, ...) \
  if (!(expr)) { \
    GST_ERROR(__VA_ARGS__); \
    cleanup; \
    return NULL; \
  }

#define C2D_RETURN_IF_FAIL(expr, cleanup) \
  if (!(expr)) { \
    cleanup; \
    return; \
  }

#define DEFAULT_OPT_FLIP_HORIZONTAL FALSE
#define DEFAULT_OPT_FLIP_VERTICAL   FALSE
#define DEFAULT_OPT_ROTATE_MODE     GST_C2D_VIDEO_ROTATE_NONE

#define GET_OPT_FLIP_HORIZONTAL(c) get_opt_bool (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL, DEFAULT_OPT_FLIP_HORIZONTAL)
#define GET_OPT_FLIP_VERTICAL(c) get_opt_bool (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL, DEFAULT_OPT_FLIP_VERTICAL)
#define GET_OPT_ROTATE_MODE(c) get_opt_enum(c, \
    GST_C2D_VIDEO_CONVERTER_OPT_ROTATE_MODE, GST_TYPE_C2D_VIDEO_ROTATE_MODE, \
    DEFAULT_OPT_ROTATE_MODE)
#define GET_OPT_SRC_X(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_X, v)
#define GET_OPT_SRC_Y(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_Y, v)
#define GET_OPT_SRC_WIDTH(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_WIDTH, v)
#define GET_OPT_SRC_HEIGHT(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_HEIGHT, v)
#define GET_OPT_DEST_X(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_X, v)
#define GET_OPT_DEST_Y(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_Y, v)
#define GET_OPT_DEST_WIDTH(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_WIDTH, v)
#define GET_OPT_DEST_HEIGHT(c, v) get_opt_int (c, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_HEIGHT, v)

#define DEFAULT_C2D_INIT_MAX_OBJECT    4
#define DEFAULT_C2D_INIT_MAX_TEMPLATE  4

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done = (gsize) _gst_debug_category_new ("c2d-video-converter", 0,
        "c2d-video-converter object");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}

struct _GstC2dVideoConverter
{
  GstVideoInfo          input;
  GstVideoInfo          output;

  GstStructure          *configuration;

  gint                  inwidth;
  gint                  inheight;
  gint                  outwidth;
  gint                  outheight;

  gboolean              flip_h;
  gboolean              flip_v;
  GstC2dVideoRotateMode rotate;

  // Source and destination rectangle.
  GstVideoRectangle     srcrect;
  GstVideoRectangle     destrect;

  // Map of virtual address and its corresponding GPU address.
  GHashTable            *gpulist;

  // Map of buffers and their corresponding C2D surface ID.
  GHashTable            *insurfaces;
  GHashTable            *outsurfaces;

  // C2D library handle.
  gpointer              c2dhandle;

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

GType
gst_c2d_video_rotate_mode_get_type (void)
{
  static GType video_rotation_type = 0;
  static const GEnumValue methods[] = {
    {GST_C2D_VIDEO_ROTATE_NONE, "No rotation", "none"},
    {GST_C2D_VIDEO_ROTATE_90_CW, "Rotate 90 degrees clockwise", "90CW"},
    {GST_C2D_VIDEO_ROTATE_90_CCW, "Rotate 90 degrees counter-clockwise", "90CCW"},
    {GST_C2D_VIDEO_ROTATE_180, "Rotate 180 degrees", "180"},
    {0, NULL, NULL},
  };
  if (!video_rotation_type) {
    video_rotation_type =
        g_enum_register_static ("GstC2dVideoRotateMode", methods);
  }
  return video_rotation_type;
}

static guint
get_opt_int (GstC2dVideoConverter * convert, const gchar * opt, gint defval)
{
  gint result;
  return gst_structure_get_int (convert->configuration, opt, &result) ?
    result : defval;
}

static gboolean
get_opt_bool (GstC2dVideoConverter * convert, const gchar * opt,
    gboolean defval)
{
  gboolean result;
  return gst_structure_get_boolean (convert->configuration, opt, &result) ?
      result : defval;
}

static gint
get_opt_enum (GstC2dVideoConverter * convert, const gchar * opt, GType type,
    gint defval)
{
  gint result;
  return gst_structure_get_enum (convert->configuration, opt, type, &result) ?
    result : defval;
}

static gpointer
map_gpu_address (GstC2dVideoConverter * convert, const GstVideoFrame * frame)
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
unmap_gpu_address (gpointer key, gpointer data, gpointer userdata)
{
  C2D_STATUS status = C2D_STATUS_OK;
  GstC2dVideoConverter *convert = userdata;
  guint surface_id = GPOINTER_TO_UINT (key);

  status = convert->UnMapAddr (data);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to unmap GPU address %p for surface %x, error: %d",
        data, surface_id, status);
    return;
  }
  GST_DEBUG ("Unmapped GPU address %p for surface %x", data, surface_id);
  return;
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
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case GST_VIDEO_FORMAT_BGRx:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA |
          C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_RGBx:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA;
    case GST_VIDEO_FORMAT_xBGR:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA |
          C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_xRGB:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA;
    case GST_VIDEO_FORMAT_RGBA:
      return C2D_COLOR_FORMAT_8888_RGBA;
    case GST_VIDEO_FORMAT_ARGB:
      return C2D_COLOR_FORMAT_8888_ARGB;
    case GST_VIDEO_FORMAT_BGR:
      return C2D_COLOR_FORMAT_888_RGB | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_RGB:
      return C2D_COLOR_FORMAT_888_RGB;
    case GST_VIDEO_FORMAT_BGR16:
      return C2D_COLOR_FORMAT_565_RGB | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_RGB16:
      return C2D_COLOR_FORMAT_565_RGB;
#else
    case GST_VIDEO_FORMAT_BGRx:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA;
    case GST_VIDEO_FORMAT_RGBx:
      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA |
          C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_xBGR:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA;
    case GST_VIDEO_FORMAT_xRGB:
      return C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA |
          C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_BGRA:
      return C2D_COLOR_FORMAT_8888_ARGB;
    case GST_VIDEO_FORMAT_ABGR:
      return C2D_COLOR_FORMAT_8888_RGBA;
    case GST_VIDEO_FORMAT_BGR:
      return C2D_COLOR_FORMAT_888_RGB;
    case GST_VIDEO_FORMAT_RGB:
      return C2D_COLOR_FORMAT_888_RGB | C2D_FORMAT_SWAP_RB;
    case GST_VIDEO_FORMAT_BGR16:
      return C2D_COLOR_FORMAT_565_RGB;
    case GST_VIDEO_FORMAT_RGB16:
      return C2D_COLOR_FORMAT_565_RGB | C2D_FORMAT_SWAP_RB;
#endif
    case GST_VIDEO_FORMAT_GRAY8:
      return C2D_COLOR_FORMAT_8_L;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }
  return 0;
}

static guint
create_surface (GstC2dVideoConverter * convert, const GstVideoFrame * frame,
    guint bits)
{
  C2D_STATUS status = C2D_STATUS_OK;
  const gchar *format;
  guint surface_id;

  gpointer gpuaddress = map_gpu_address (convert, frame);
  g_return_val_if_fail (gpuaddress != NULL, 0);

  format = gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame));

  if (GST_VIDEO_INFO_IS_RGB (&frame->info)) {
    C2D_RGB_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_val_if_fail (surface.format != 0, 0);

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s surface - width(%u) height(%u)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.width, surface.height);

    // Plane stride.
    surface.stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

    GST_DEBUG ("%s %s surface - stride(%d)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.stride);

    // Set plane virtual and GPU address.
    surface.buffer = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    surface.phys = gpuaddress;

    GST_DEBUG ("%s %s surface - plane(%p) phys(%p)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.buffer, surface.phys);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

    // Create RGB surface.
    status = convert->CreateSurface(&surface_id, bits, type, &surface);
  } else if (GST_VIDEO_INFO_IS_YUV (&frame->info)) {
    C2D_YUV_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_val_if_fail (surface.format != 0, 0);

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s surface - width(%u) height(%u)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.width, surface.height);

    // Y plane stride.
    surface.stride0 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
    // UV plane (U plane in planar format) plane stride.
    surface.stride1 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
    // V plane (planar format, ignored in other formats) plane stride.
    surface.stride2 = (GST_VIDEO_FRAME_N_PLANES (frame) != 3) ?
        0 : GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2);

    GST_DEBUG ("%s %s surface - stride0(%d) stride1(%d) stride2(%d)",
        (bits & C2D_SOURCE) ? "Input" : "Output", format, surface.stride0,
        surface.stride1, surface.stride2);

    // Y plane virtual address.
    surface.plane0 = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    // UV plane (U plane in planar format) plane virtual address.
    surface.plane1 = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
    // V plane (planar format, ignored in other formats) plane virtual address.
    surface.plane2 = (GST_VIDEO_FRAME_N_PLANES (frame) != 3) ?
        NULL : GST_VIDEO_FRAME_PLANE_DATA (frame, 2);

    GST_DEBUG ("%s %s surface - plane0(%p) plane1(%p) plane2(%p)",
        (bits & C2D_SOURCE) ? "Input" : "Output", format, surface.plane0,
        surface.plane1, surface.plane2);

    // Y plane GPU address.
    surface.phys0 = gpuaddress;
    // UV plane (U plane in planar format)  GPU address.
    surface.phys1 = GUINT_TO_POINTER (GPOINTER_TO_UINT (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1));
    // V plane (planar format, ignored in other formats) GPU address.
    surface.phys2 = (GST_VIDEO_FRAME_N_PLANES (frame) != 3) ?
        NULL : GUINT_TO_POINTER (GPOINTER_TO_UINT (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 2));

    GST_DEBUG ("%s %s surface - phys0(%p) phys1(%p) phys2(%p)",
         (bits & C2D_SOURCE) ? "Input" : "Output", format, surface.phys0,
         surface.phys1, surface.phys2);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);

    // Create YUV surface.
    status = convert->CreateSurface(&surface_id, bits, type, &surface);
  }

  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to create C2D surface, error: %d!", status);
    unmap_gpu_address (NULL, gpuaddress, convert);
    return 0;
  }

  g_hash_table_insert (convert->gpulist, GUINT_TO_POINTER (surface_id),
       gpuaddress);

  GST_DEBUG ("Created %s surface with id %x", (bits & C2D_SOURCE) ?
      "input" : "output", surface_id);
  return surface_id;
}

static void
update_surface (GstC2dVideoConverter * convert, const GstVideoFrame * frame,
    guint surface_id, guint bits)
{
  C2D_STATUS status = C2D_STATUS_OK;
  const gchar *format;

  gpointer gpuaddress =
      g_hash_table_lookup (convert->gpulist, GUINT_TO_POINTER (surface_id));

  format = gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame));

  if (GST_VIDEO_INFO_IS_RGB (&frame->info)) {
    C2D_RGB_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_if_fail (surface.format != 0);

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s surface - width(%u) height(%u)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.width, surface.height);

    // Plane stride.
    surface.stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

    GST_DEBUG ("%s %s surface - stride(%d)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.stride);

    // Set plane virtual and GPU address.
    surface.buffer = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    surface.phys = gpuaddress;

    GST_DEBUG ("%s %s surface - plane(%p) phys(%p)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.buffer, surface.phys);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

    // Create RGB surface.
    status = convert->UpdateSurface(surface_id, bits, type, &surface);
  } else if (GST_VIDEO_INFO_IS_YUV (&frame->info)) {
    C2D_YUV_SURFACE_DEF surface = { 0, };
    C2D_SURFACE_TYPE type;

    surface.format =
        gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));
    g_return_if_fail (surface.format != 0);

    // Set surface dimensions.
    surface.width = GST_VIDEO_FRAME_WIDTH (frame);
    surface.height = GST_VIDEO_FRAME_HEIGHT (frame);

    GST_DEBUG ("%s %s surface - width(%u) height(%u)", (bits & C2D_SOURCE) ?
        "Input" : "Output", format, surface.width, surface.height);

    // Y plane stride.
    surface.stride0 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
    // UV plane (U plane in planar format) plane stride.
    surface.stride1 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
    // V plane (planar format, ignored in other formats) plane stride.
    surface.stride2 = (GST_VIDEO_FRAME_N_PLANES (frame) != 3) ?
        0 : GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2);

    GST_DEBUG ("%s %s surface - stride0(%d) stride1(%d) stride2(%d)",
        (bits & C2D_SOURCE) ? "Input" : "Output", format, surface.stride0,
        surface.stride1, surface.stride2);

    // Y plane virtual address.
    surface.plane0 = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    // UV plane (U plane in planar format) plane virtual address.
    surface.plane1 = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
    // V plane (planar format, ignored in other formats) plane virtual address.
    surface.plane2 = (GST_VIDEO_FRAME_N_PLANES (frame) != 3) ?
        NULL : GST_VIDEO_FRAME_PLANE_DATA (frame, 2);

    GST_DEBUG ("%s %s surface - plane0(%p) plane1(%p) plane2(%p)",
        (bits & C2D_SOURCE) ? "Input" : "Output", format, surface.plane0,
        surface.plane1, surface.plane2);

    // Y plane GPU address.
    surface.phys0 = gpuaddress;
    // UV plane (U plane in planar format)  GPU address.
    surface.phys1 = GUINT_TO_POINTER (GPOINTER_TO_UINT (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1));
    // V plane (planar format, ignored in other formats) GPU address.
    surface.phys2 = (GST_VIDEO_FRAME_N_PLANES (frame) != 3) ?
        NULL : GUINT_TO_POINTER (GPOINTER_TO_UINT (gpuaddress) +
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 2));

    GST_DEBUG ("%s %s surface - phys0(%p) phys1(%p) phys2(%p)",
         (bits & C2D_SOURCE) ? "Input" : "Output", format, surface.phys0,
         surface.phys1, surface.phys2);

    type = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);

    // Create YUV surface.
    status = convert->UpdateSurface(surface_id, bits, type, &surface);
  }

  if (status != C2D_STATUS_OK) {
    GST_ERROR ("Failed to update C2D surface, error: %d!", status);
    return;
  }

  GST_DEBUG ("Updated %s surface with id %x", (bits & C2D_SOURCE) ?
      "input" : "output", surface_id);
}

static void
destroy_surface (gpointer key, gpointer value, gpointer userdata)
{
  C2D_STATUS status = C2D_STATUS_OK;
  GstC2dVideoConverter *convert = userdata;
  guint surface_id = GPOINTER_TO_UINT (value);

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
construct_object (GstC2dVideoConverter * convert, guint surface_id,
    C2D_OBJECT * object)
{
  memset (object, 0x00, sizeof(*object));

  object->surface_id = surface_id;
  object->config_mask = (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT |
      C2D_ALPHA_BLEND_NONE);

  // Setup the source rectangle.
  object->source_rect.x = convert->srcrect.x << 16;
  object->source_rect.y = convert->srcrect.y << 16;
  object->source_rect.width = convert->srcrect.w << 16;
  object->source_rect.height = convert->srcrect.h << 16;

  // Setup the target rectangle.
  object->target_rect.x = convert->destrect.x << 16;
  object->target_rect.y = convert->destrect.y << 16;

  // Apply the flip bits to the object configure mask if set.
  object->config_mask &= ~(C2D_MIRROR_V_BIT | C2D_MIRROR_H_BIT);
  if (convert->flip_v) {
    object->config_mask |= C2D_MIRROR_V_BIT;
    GST_LOG ("Input surface %x - flip vertically", surface_id);
  }
  if (convert->flip_h) {
    object->config_mask |= C2D_MIRROR_H_BIT;
    GST_LOG ("Input surface %x - flip horizontally", surface_id);
  }

  switch (convert->rotate) {
    case GST_C2D_VIDEO_ROTATE_90_CW:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_270);
      object->target_rect.width = convert->destrect.h << 16;
      object->target_rect.height = convert->destrect.w << 16;
      GST_LOG ("Input surface %x - rotate 90° clockwise", surface_id);
      break;
    case GST_C2D_VIDEO_ROTATE_180:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_180);
      object->target_rect.width = convert->destrect.w << 16;
      object->target_rect.height = convert->destrect.h << 16;
      GST_LOG ("Input surface %x - rotate 180°", surface_id);
      break;
    case GST_C2D_VIDEO_ROTATE_90_CCW:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_90);
      object->target_rect.width = convert->destrect.h << 16;
      object->target_rect.height = convert->destrect.w << 16;
      GST_LOG ("Input surface %x - rotate 90° counter-clockwise", surface_id);
      break;
    default:
      // Remove all rotation flags.
      object->config_mask &=
          ~(C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
            C2D_OVERRIDE_TARGET_ROTATE_90 | C2D_OVERRIDE_TARGET_ROTATE_180 |
            C2D_OVERRIDE_TARGET_ROTATE_270);
      object->target_rect.width = convert->destrect.w << 16;
      object->target_rect.height = convert->destrect.h << 16;
      break;
  }

  GST_TRACE ("Input surface %x - source rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->source_rect.x >> 16, object->source_rect.y >> 16,
      object->source_rect.width >> 16, object->source_rect.height >> 16);

  GST_TRACE ("Input surface %x - target rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->target_rect.x >> 16, object->target_rect.y >> 16,
      object->target_rect.width >> 16, object->target_rect.height >> 16);

  GST_TRACE ("Input surface %x - scissor rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->scissor_rect.x >> 16, object->scissor_rect.y >> 16,
      object->scissor_rect.width >> 16, object->scissor_rect.height >> 16);
}

static gboolean
overwrite_configuration (GQuark field, const GValue * value, gpointer userdata)
{
  GstC2dVideoConverter *convert = userdata;

  gst_structure_id_set_value (convert->configuration, field, value);

  return TRUE;
}

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

GstC2dVideoConverter *
gst_c2d_video_converter_new (GstVideoInfo * input, GstVideoInfo * output,
    GstStructure * configuration)
{
  GstC2dVideoConverter *convert;
  //const GstVideoFormatInfo *fin, *fout, *finfo;
  gboolean success;
  C2D_DRIVER_SETUP_INFO setup;
  C2D_DRIVER_INFO caps;
  C2D_STATUS status;

  g_return_val_if_fail (input != NULL, NULL);
  g_return_val_if_fail (output != NULL, NULL);

  // Frame conversion is not supported.
  g_return_val_if_fail (input->fps_n == output->fps_n, NULL);
  g_return_val_if_fail (input->fps_d == output->fps_d, NULL);

  // Deinterlace is not supported.
  g_return_val_if_fail (input->interlace_mode == output->interlace_mode, NULL);

  convert = g_slice_new0 (GstC2dVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  // Load C2D library.
  convert->c2dhandle = dlopen ("libC2D2.so", RTLD_NOW);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (convert->c2dhandle != NULL,
      gst_c2d_video_converter_free (convert), "Failed to open C2D library, "
      "error: %s!", dlerror());

  // Load C2D library symbols.
  success = load_symbol ((gpointer*)&convert->DriverInit, convert->c2dhandle,
      "c2dDriverInit");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->DriverDeInit, convert->c2dhandle,
      "c2dDriverDeInit");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->CreateSurface,
      convert->c2dhandle, "c2dCreateSurface");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->DestroySurface,
      convert->c2dhandle, "c2dDestroySurface");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->UpdateSurface,
      convert->c2dhandle, "c2dUpdateSurface");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->Draw, convert->c2dhandle,
      "c2dDraw");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->Flush, convert->c2dhandle,
      "c2dFlush");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->Finish, convert->c2dhandle,
      "c2dFinish");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->WaitTimestamp,
      convert->c2dhandle, "c2dWaitTimestamp");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->MapAddr, convert->c2dhandle,
      "c2dMapAddr");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->UnMapAddr, convert->c2dhandle,
      "c2dUnMapAddr");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  success = load_symbol ((gpointer*)&convert->GetDriverCapabilities,
      convert->c2dhandle, "c2dGetDriverCapabilities");
  C2D_RETURN_NULL_IF_FAIL (success, gst_c2d_video_converter_free (convert));

  convert->insurfaces = g_hash_table_new (NULL, NULL);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (convert->insurfaces != NULL,
      gst_c2d_video_converter_free (convert), "Failed to create hash table "
      "for source surfaces!");

  convert->outsurfaces = g_hash_table_new (NULL, NULL);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (convert->outsurfaces != NULL,
      gst_c2d_video_converter_free (convert), "Failed to create hash table "
      "for target surfaces!");

  convert->gpulist = g_hash_table_new (NULL, NULL);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (convert->gpulist != NULL,
      gst_c2d_video_converter_free (convert), "Failed to create hash table "
      "for GPU mapped addresses!");

  setup.max_object_list_needed = DEFAULT_C2D_INIT_MAX_OBJECT;
  setup.max_surface_template_needed = DEFAULT_C2D_INIT_MAX_TEMPLATE;

  status = convert->DriverInit (&setup);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (C2D_STATUS_OK == status,
      gst_c2d_video_converter_free (convert), "Failed to initialize driver!");

  status = convert->GetDriverCapabilities (&caps);
  if (C2D_STATUS_OK == status) {
    GST_DEBUG ("C2D_DRIVER Capabilities:");
    GST_DEBUG ("    Maximum dimensions: %ux%u", caps.max_surface_width,
        caps.max_surface_height);
    GST_DEBUG ("    SUPPORTS_GLOBAL_ALPHA_OP:           [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_GLOBAL_ALPHA_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_TILE_OP:                   [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_TILE_OP ? 'x' : ' ');
    GST_DEBUG ("    SUPPORTS_COLOR_KEY_OP:              [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_COLOR_KEY_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_NO_PIXEL_ALPHA_OP:         [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_NO_PIXEL_ALPHA_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_TARGET_ROTATE_OP:          [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_TARGET_ROTATE_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_ANTI_ALIASING_OP:          [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_ANTI_ALIASING_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_BILINEAR_FILTER_OP:        [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_BILINEAR_FILTER_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_LENS_CORRECTION_OP:        [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_LENS_CORRECTION_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_OVERRIDE_TARGET_ROTATE_OP: [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_OVERRIDE_TARGET_ROTATE_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_SHADER_BLOB_OP:            [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_SHADER_BLOB_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_MASK_SURFACE_OP:           [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_MASK_SURFACE_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_MIRROR_H_OP:               [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_MIRROR_H_OP ? 'x' : ' ');
    GST_DEBUG ("    SUPPORTS_MIRROR_V_OP:               [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_MIRROR_V_OP ? 'x' : ' ');
    GST_DEBUG ("    SUPPORTS_SCISSOR_RECT_OP:           [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_SCISSOR_RECT_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_TARGET_RECT_OP:            [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_TARGET_RECT_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_FLUSH_WITH_FENCE_FD_OP:    [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_FLUSH_WITH_FENCE_FD_OP ?
            'x' : ' ');
    GST_DEBUG ("    SUPPORTS_UBWC_COMPRESSED_OP:        [%c]",
        caps.capabilities_mask & C2D_DRIVER_SUPPORTS_UBWC_COMPRESSED_OP ?
            'x' : ' ');
  }

  //fin = input->finfo;
  //fout = output->finfo;

  convert->input = *input;
  convert->output = *output;

  convert->inwidth = GST_VIDEO_INFO_WIDTH (input);
  convert->inheight = GST_VIDEO_INFO_HEIGHT (input);
  convert->outwidth = GST_VIDEO_INFO_WIDTH (output);
  convert->outheight = GST_VIDEO_INFO_HEIGHT (output);

  GST_INFO ("Input dimensions %dx%d", convert->inwidth, convert->inheight);
  GST_INFO ("Output dimensions %dx%d", convert->outwidth, convert->outheight);

  // Default configuration.
  convert->configuration = gst_structure_new_empty ("GstC2dVideoConverter");
  gst_c2d_video_converter_set_config (convert,
      (configuration != NULL) ? configuration : convert->configuration);

  GST_INFO ("Created C2D converter: %p", convert);
  return convert;
}

void
gst_c2d_video_converter_free (GstC2dVideoConverter * convert)
{
  if (convert->insurfaces != NULL) {
    g_hash_table_foreach (convert->insurfaces, destroy_surface, convert);
    g_hash_table_destroy(convert->insurfaces);
    convert->insurfaces = NULL;
  }

  if (convert->outsurfaces != NULL) {
    g_hash_table_foreach (convert->outsurfaces, destroy_surface, convert);
    g_hash_table_destroy (convert->outsurfaces);
    convert->outsurfaces = NULL;
  }

  if (convert->gpulist != NULL) {
    g_hash_table_foreach (convert->gpulist, unmap_gpu_address, convert);
    g_hash_table_destroy (convert->gpulist);
    convert->gpulist = NULL;
  }

  if (convert->DriverDeInit != NULL) {
    convert->DriverDeInit ();
  }

  if (convert->c2dhandle != NULL) {
    dlclose (convert->c2dhandle);
    convert->c2dhandle = NULL;
  }

  GST_INFO ("Destroyed C2D converter: %p", convert);
  g_slice_free (GstC2dVideoConverter, convert);
}

gboolean
gst_c2d_video_converter_set_config (GstC2dVideoConverter * convert,
    GstStructure * config)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  gst_structure_foreach (config, overwrite_configuration, convert);
  gst_structure_free (config);

  convert->flip_h = GET_OPT_FLIP_HORIZONTAL (convert);
  convert->flip_v = GET_OPT_FLIP_VERTICAL (convert);
  convert->rotate = GET_OPT_ROTATE_MODE (convert);

  convert->srcrect.x  = GET_OPT_SRC_X (convert, 0);
  convert->srcrect.y  = GET_OPT_SRC_Y (convert, 0);
  convert->srcrect.w  = GET_OPT_SRC_WIDTH (convert, convert->inwidth);
  convert->srcrect.h  = GET_OPT_SRC_HEIGHT (convert, convert->inheight);

  convert->srcrect.w = (convert->srcrect.w == 0) ? convert->inwidth :
      MIN (convert->srcrect.w, convert->inwidth - convert->srcrect.x);
  convert->srcrect.h = (convert->srcrect.h == 0) ? convert->inheight :
      MIN (convert->srcrect.h, convert->inheight - convert->srcrect.y);

  convert->destrect.x  = GET_OPT_DEST_X (convert, 0);
  convert->destrect.y  = GET_OPT_DEST_Y (convert, 0);
  convert->destrect.w  = GET_OPT_DEST_WIDTH (convert, convert->outwidth);
  convert->destrect.h  = GET_OPT_DEST_HEIGHT (convert, convert->outheight);

  convert->destrect.w = (convert->destrect.w == 0) ? convert->outwidth :
      MIN (convert->destrect.w, convert->outwidth - convert->destrect.x);
  convert->destrect.h = (convert->destrect.h == 0) ? convert->outheight :
      MIN (convert->destrect.h, convert->outheight - convert->destrect.y);

  return TRUE;
}

const GstStructure *
gst_c2d_video_converter_get_config (GstC2dVideoConverter * convert)
{
  g_return_val_if_fail (convert != NULL, NULL);

  return convert->configuration;
}

void
gst_c2d_video_converter_frame (GstC2dVideoConverter * convert,
    const GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  C2D_STATUS status = C2D_STATUS_OK;
  GstMemory *memory = NULL;
  guint fd, source_surface_id, target_surface_id;
  C2D_OBJECT object;

  g_return_if_fail (convert != NULL);
  g_return_if_fail (inframe != NULL);
  g_return_if_fail (outframe != NULL);

  memory = gst_buffer_peek_memory (inframe->buffer, 0);
  g_return_if_fail (gst_is_fd_memory (memory));

  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (convert->insurfaces, GUINT_TO_POINTER (fd))) {
    source_surface_id = create_surface (convert, inframe, C2D_SOURCE);
    g_return_if_fail (source_surface_id != 0);

    g_hash_table_insert (convert->insurfaces, GUINT_TO_POINTER (fd),
        GUINT_TO_POINTER (source_surface_id));
  } else {
    source_surface_id = GPOINTER_TO_UINT (
        g_hash_table_lookup (convert->insurfaces, GUINT_TO_POINTER (fd)));
    update_surface (convert, inframe, source_surface_id, C2D_SOURCE);
  }

  memory = gst_buffer_peek_memory (outframe->buffer, 0);
  g_return_if_fail (gst_is_fd_memory (memory));

  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (convert->outsurfaces, GUINT_TO_POINTER (fd))) {
    target_surface_id = create_surface (convert, outframe, C2D_TARGET);
    g_return_if_fail (target_surface_id != 0);

    g_hash_table_insert (convert->outsurfaces, GUINT_TO_POINTER (fd),
        GUINT_TO_POINTER (target_surface_id));
  } else {
    target_surface_id = GPOINTER_TO_UINT (
        g_hash_table_lookup (convert->outsurfaces, GUINT_TO_POINTER (fd)));
    update_surface (convert, outframe, target_surface_id, C2D_TARGET);
  }

  construct_object (convert, source_surface_id, &object);

  GST_LOG ("Draw output surface %x", target_surface_id);

  status = convert->Draw (target_surface_id, 0, NULL, 0, 0, &object, 0);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("c2dDraw failed for target surface %x, , error: %d!",
        target_surface_id, status);
    return;
  }

  status = convert->Finish (target_surface_id);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("c2dDraw finish failed for output surface %x, , error: %d!",
        target_surface_id, status);
    return;
  }

  GST_LOG ("Finished drawing output surface %x", target_surface_id);
}
