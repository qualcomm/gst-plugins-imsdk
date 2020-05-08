/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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

#include "c2d-video-converter.h"

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

#define CHECK_C2D_CAPABILITY(info, name) \
    GST_DEBUG ("    %-30s [%c]", #name, \
        info.capabilities_mask & C2D_DRIVER_SUPPORTS_##name ? 'x' : ' ');

#define DEFAULT_C2D_INIT_MAX_OBJECT    4
#define DEFAULT_C2D_INIT_MAX_TEMPLATE  4

#define DEFAULT_OPT_FLIP_HORIZONTAL FALSE
#define DEFAULT_OPT_FLIP_VERTICAL   FALSE
#define DEFAULT_OPT_ROTATE_MODE     GST_C2D_VIDEO_ROTATE_NONE
#define DEFAULT_OPT_BACKGROUND      0xFF808080

#define GET_OPT_FLIP_HORIZONTAL(s) get_opt_bool (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL, DEFAULT_OPT_FLIP_HORIZONTAL)
#define GET_OPT_FLIP_VERTICAL(s) get_opt_bool (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL, DEFAULT_OPT_FLIP_VERTICAL)
#define GET_OPT_ROTATE_MODE(s) get_opt_enum(s, \
    GST_C2D_VIDEO_CONVERTER_OPT_ROTATE_MODE, GST_TYPE_C2D_VIDEO_ROTATE_MODE, \
    DEFAULT_OPT_ROTATE_MODE)
#define GET_OPT_BACKGROUND(s) get_opt_uint (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_BACKGROUND, DEFAULT_OPT_BACKGROUND)
#define GET_OPT_ALPHA(s, v) get_opt_double (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_ALPHA, v)
#define GET_OPT_SRC_X(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_X, v)
#define GET_OPT_SRC_Y(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_Y, v)
#define GET_OPT_SRC_WIDTH(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_WIDTH, v)
#define GET_OPT_SRC_HEIGHT(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_SRC_HEIGHT, v)
#define GET_OPT_DEST_X(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_X, v)
#define GET_OPT_DEST_Y(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_Y, v)
#define GET_OPT_DEST_WIDTH(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_WIDTH, v)
#define GET_OPT_DEST_HEIGHT(s, v) get_opt_int (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_DEST_HEIGHT, v)

#define GST_C2D_GET_LOCK(obj) (&((GstC2dVideoConverter *) obj)->lock)
#define GST_C2D_LOCK(obj)     g_mutex_lock (GST_C2D_GET_LOCK(obj))
#define GST_C2D_UNLOCK(obj)   g_mutex_unlock(GST_C2D_GET_LOCK(obj))

#define GST_CAT_DEFAULT ensure_debug_category()

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

struct _GstC2dRequest {
  gpointer tshandle;
};

struct _GstC2dVideoConverter
{
  // Global mutex lock.
  GMutex            lock;

  // List of surface options for each input frame.
  GList             *inopts;
  // Surface options for the output frame.
  GstStructure      *outopts;

  // Map of C2D surface ID and its corresponding GPU address.
  GHashTable        *gpulist;

  // Map of buffer FDs and their corresponding C2D surface ID.
  GHashTable        *insurfaces;
  GHashTable        *outsurfaces;

  // C2D library handle.
  gpointer          c2dhandle;

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

GType
gst_c2d_video_rotate_mode_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue methods[] = {
    { GST_C2D_VIDEO_ROTATE_NONE,
      "No rotation", "none"
    },
    { GST_C2D_VIDEO_ROTATE_90_CW,
      "Rotate 90 degrees clockwise", "90CW"
    },
    { GST_C2D_VIDEO_ROTATE_90_CCW,
      "Rotate 90 degrees counter-clockwise", "90CCW"
    },
    { GST_C2D_VIDEO_ROTATE_180,
      "Rotate 180 degrees", "180"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstC2dVideoRotate", methods);

  return gtype;
}

static gdouble
get_opt_double (const GstStructure * options, const gchar * opt, gdouble value)
{
  gdouble result;
  return gst_structure_get_double (options, opt, &result) ? result : value;
}

static gint
get_opt_int (const GstStructure * options, const gchar * opt, gint value)
{
  gint result;
  return gst_structure_get_int (options, opt, &result) ? result : value;
}

static guint
get_opt_uint (const GstStructure * options, const gchar * opt, guint value)
{
  guint result;
  return gst_structure_get_uint (options, opt, &result) ? result : value;
}

static gboolean
get_opt_bool (const GstStructure * options, const gchar * opt, gboolean value)
{
  gboolean result;
  return gst_structure_get_boolean (options, opt, &result) ? result : value;
}

static gint
get_opt_enum (const GstStructure * options, const gchar * opt, GType type,
    gint value)
{
  gint result;
  return gst_structure_get_enum (options, opt, type, &result) ? result : value;
}

static gboolean
update_options (GQuark field, const GValue * value, gpointer userdata)
{
  gst_structure_id_set_value (GST_STRUCTURE_CAST (userdata), field, value);
  return TRUE;
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
  C2D_STATUS status = C2D_STATUS_NOT_SUPPORTED;
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
update_object (C2D_OBJECT * object, guint surface_id, const GstStructure * opts,
    const GstVideoFrame * inframe, const GstVideoFrame * outframe)
{
  gint x = 0, y = 0, width = 0, height = 0;

  object->surface_id = surface_id;
  object->config_mask = (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT |
      C2D_GLOBAL_ALPHA_BIT);

  // Transform alpha from double (0.0 - 1.0) to integer (0 - 255).
  object->global_alpha = G_MAXUINT8 * GET_OPT_ALPHA (opts, 1.0);
  GST_TRACE ("Input surface %x - Global alpha: %u", surface_id,
      object->global_alpha);

  // Setup the source rectangle.
  x = GET_OPT_SRC_X (opts, 0);
  y = GET_OPT_SRC_Y (opts, 0);
  width = GET_OPT_SRC_WIDTH (opts, GST_VIDEO_FRAME_WIDTH (inframe));
  height = GET_OPT_SRC_HEIGHT (opts, GST_VIDEO_FRAME_HEIGHT (inframe));

  width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) :
      MIN (width, GST_VIDEO_FRAME_WIDTH (inframe) - x);
  height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) :
      MIN (height, GST_VIDEO_FRAME_HEIGHT (inframe) - y);

  object->source_rect.x = x << 16;
  object->source_rect.y = y << 16;
  object->source_rect.width = width << 16;
  object->source_rect.height = height << 16;

  // Apply the flip bits to the object configure mask if set.
  object->config_mask &= ~(C2D_MIRROR_V_BIT | C2D_MIRROR_H_BIT);

  if (GET_OPT_FLIP_VERTICAL (opts)) {
    object->config_mask |= C2D_MIRROR_V_BIT;
    GST_TRACE ("Input surface %x - Flip Vertically", surface_id);
  }

  if (GET_OPT_FLIP_HORIZONTAL (opts)) {
    object->config_mask |= C2D_MIRROR_H_BIT;
    GST_TRACE ("Input surface %x - Flip Horizontally", surface_id);
  }

  // Setup the target rectangle.
  x = GET_OPT_DEST_X (opts, 0);
  y = GET_OPT_DEST_Y (opts, 0);
  width = GET_OPT_DEST_WIDTH (opts, 0);
  height = GET_OPT_DEST_HEIGHT (opts, 0);

  // Setup rotation angle and adjustments.
  switch (GET_OPT_ROTATE_MODE (opts)) {
    case GST_C2D_VIDEO_ROTATE_90_CW:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_270);
      GST_LOG ("Input surface %x - rotate 90° clockwise", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) : height;

      object->target_rect.width = height << 16;
      object->target_rect.height = width << 16;

      // Adjust the target rectangle coordinates.
      object->target_rect.y =
          (GST_VIDEO_FRAME_WIDTH (outframe) - (x + width)) << 16;
      object->target_rect.x = y << 16;
      break;
    case GST_C2D_VIDEO_ROTATE_180:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_180);
      GST_LOG ("Input surface %x - rotate 180°", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) : height;

      object->target_rect.width = width << 16;
      object->target_rect.height = height << 16;

      // Adjust the target rectangle coordinates.
      object->target_rect.x =
          (GST_VIDEO_FRAME_WIDTH (outframe) - (x + width)) << 16;
      object->target_rect.y =
          (GST_VIDEO_FRAME_HEIGHT (outframe) - (y + height)) << 16;
      break;
    case GST_C2D_VIDEO_ROTATE_90_CCW:
      object->config_mask |= (C2D_OVERRIDE_GLOBAL_TARGET_ROTATE_CONFIG |
          C2D_OVERRIDE_TARGET_ROTATE_90);
      GST_LOG ("Input surface %x - rotate 90° counter-clockwise", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) : height;

      object->target_rect.width = height << 16;
      object->target_rect.height = width << 16;

      // Adjust the target rectangle coordinates.
      object->target_rect.x =
          (GST_VIDEO_FRAME_HEIGHT (outframe) - (y + height)) << 16;
      object->target_rect.y = x << 16;
      break;
    default:
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) : height;

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

  GST_TRACE ("Input surface %x - Source rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->source_rect.x >> 16, object->source_rect.y >> 16,
      object->source_rect.width >> 16, object->source_rect.height >> 16);

  GST_TRACE ("Input surface %x - Target rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->target_rect.x >> 16, object->target_rect.y >> 16,
      object->target_rect.width >> 16, object->target_rect.height >> 16);

  GST_TRACE ("Input surface %x - Scissor rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->scissor_rect.x >> 16, object->scissor_rect.y >> 16,
      object->scissor_rect.width >> 16, object->scissor_rect.height >> 16);
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

  // Load C2D library.
  convert->c2dhandle = dlopen ("libC2D2.so", RTLD_NOW);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (convert->c2dhandle != NULL,
      gst_c2d_video_converter_free (convert), "Failed to open C2D library, "
      "error: %s!", dlerror());

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

  convert->outopts = gst_structure_new_empty ("Output");
  g_mutex_init (&convert->lock);

  GST_INFO ("Created C2D converter: %p", convert);
  return convert;
}

void
gst_c2d_video_converter_free (GstC2dVideoConverter * convert)
{
  g_mutex_clear (&convert->lock);
  gst_structure_free (convert->outopts);

  if (convert->inopts != NULL)
    g_list_free_full (convert->inopts, (GDestroyNotify) gst_structure_free);

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
gst_c2d_video_converter_set_input_opts (GstC2dVideoConverter * convert,
    guint index, GstStructure * opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);

  GST_C2D_LOCK (convert);

  if (index > g_list_length (convert->inopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_C2D_UNLOCK (convert);
    return FALSE;
  } else if ((index == g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_C2D_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->inopts = g_list_remove (convert->inopts,
        g_list_nth_data (convert->inopts, index));
    GST_C2D_UNLOCK (convert);
    return TRUE;
  }

  if (index == g_list_length (convert->inopts)) {
    GST_LOG ("Add a new opts structure in the options list");

    convert->inopts = g_list_append (convert->inopts,
        gst_structure_new_empty ("Input"));
  }

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options,
      g_list_nth_data (convert->inopts, index));
  gst_structure_free (opts);

  GST_C2D_UNLOCK (convert);

  return TRUE;
}

gboolean
gst_c2d_video_converter_set_output_opts (GstC2dVideoConverter * convert,
    GstStructure * opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  GST_C2D_LOCK (convert);

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options, convert->outopts);
  gst_structure_free (opts);

  GST_C2D_UNLOCK (convert);

  return TRUE;
}

gpointer
gst_c2d_video_converter_submit_request (GstC2dVideoConverter * convert,
    const GstVideoFrame * inframes, guint n_inputs, GstVideoFrame * outframe)
{
  GstMemory *memory = NULL;
  guint id = 0, idx = 0, fd = 0, surface_id = 0;
  C2D_STATUS status = C2D_STATUS_OK;
  C2D_OBJECT *objects = NULL;
  c2d_ts_handle timestamp;

  g_return_val_if_fail (convert != NULL, NULL);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs != 0), NULL);
  g_return_val_if_fail (outframe != NULL, NULL);

  GST_C2D_LOCK (convert);

  // C2D draw objects.
  objects = g_new0 (C2D_OBJECT, n_inputs);

  // Iterate over the input frames.
  for (idx = 0; idx < n_inputs; idx++) {
    const GstVideoFrame *inframe = &inframes[idx];
    const GstStructure *opts = NULL;

    if (NULL == inframe->buffer)
      continue;

    // Get the 1st (and only) memory block from the input GstBuffer.
    memory = gst_buffer_peek_memory (inframe->buffer, 0);
    C2D_RETURN_NULL_IF_FAIL_WITH_MSG (gst_is_fd_memory (memory),
        GST_C2D_UNLOCK (convert); g_free (objects),
        "Input buffer %p does not have FD memory!", inframe->buffer);

    // Get the input buffer FD from the GstBuffer memory block.
    fd = gst_fd_memory_get_fd (memory);

    if (!g_hash_table_contains (convert->insurfaces, GUINT_TO_POINTER (fd))) {
      // Create an input surface and add its ID to the input hash table.
      surface_id = create_surface (convert, inframe, C2D_SOURCE);
      C2D_RETURN_NULL_IF_FAIL_WITH_MSG (surface_id != 0,
          GST_C2D_UNLOCK (convert); g_free (objects),
          "Failed to create surface!");

      g_hash_table_insert (convert->insurfaces, GUINT_TO_POINTER (fd),
          GUINT_TO_POINTER (surface_id));
    } else {
      // Get the input surface ID from the input hash table.
      surface_id = GPOINTER_TO_UINT (
          g_hash_table_lookup (convert->insurfaces, GUINT_TO_POINTER (fd)));
    }

    if (idx >= g_list_length (convert->inopts)) {
      convert->inopts = g_list_append (convert->inopts,
          gst_structure_new_empty ("GstC2dVideoConverter"));
    }

    // Get the options for current input buffer.
    opts = g_list_nth_data (convert->inopts, idx);

    // Update the input C2D object.
    update_object (&objects[id], surface_id, opts, inframe, outframe);

    // Set the previous object to point to current one (linked list).
    if (id >= 1)
      objects[(id - 1)].next = &objects[id];

    id++;
  }

  // Get the 1st (and only) memory block from the output GstBuffer.
  memory = gst_buffer_peek_memory (outframe->buffer, 0);
  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (gst_is_fd_memory (memory),
      GST_C2D_UNLOCK (convert); g_free (objects),
      "Output buffer %p does not have FD memory!", outframe->buffer);

  // Get the output buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (convert->outsurfaces, GUINT_TO_POINTER (fd))) {
    // Create an output surface and add its ID to the output hash table.
    surface_id = create_surface (convert, outframe, C2D_TARGET);
    C2D_RETURN_NULL_IF_FAIL_WITH_MSG (surface_id != 0,
        GST_C2D_UNLOCK (convert); g_free (objects),
        "Failed to create surface!");

    g_hash_table_insert (convert->outsurfaces, GUINT_TO_POINTER (fd),
        GUINT_TO_POINTER (surface_id));
  } else {
    // Get the input surface ID from the input hash table.
    surface_id = GPOINTER_TO_UINT (
        g_hash_table_lookup (convert->outsurfaces, GUINT_TO_POINTER (fd)));
  }

  GST_LOG ("Draw output surface %x", surface_id);

  status = convert->FillSurface (surface_id,
      GET_OPT_BACKGROUND (convert->outopts), NULL);

  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (C2D_STATUS_OK == status,
      GST_C2D_UNLOCK (convert); g_free (objects), "c2dFillSurface failed for"
      " target surface %x, error: %d!", surface_id, status);

  status = convert->Draw (surface_id, 0, NULL, 0, 0, objects, 0);

  C2D_RETURN_NULL_IF_FAIL_WITH_MSG (C2D_STATUS_OK == status,
      GST_C2D_UNLOCK (convert); g_free (objects), "c2dDraw failed for"
      " target surface %x, error: %d!", surface_id, status);

  // Free C2D draw objects.
  g_free (objects);

  GST_C2D_UNLOCK (convert);

  status = convert->Flush (surface_id, &timestamp);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("c2dDraw finish failed for output surface %x, error: %d!",
        surface_id, status);
    return NULL;
  }

  GST_LOG ("Output surface %x, timestamp: %p", surface_id, timestamp);
  return timestamp;
}

gboolean
gst_c2d_video_converter_wait_request (GstC2dVideoConverter *convert,
    gpointer request_id)
{
  C2D_STATUS status = C2D_STATUS_OK;
  c2d_ts_handle timestamp = request_id;

  if (NULL == timestamp) {
    GST_ERROR ("Invalid timestamp handle!");
    return FALSE;
  }

  GST_LOG ("Waiting timestamp: %p", timestamp);

  status = convert->WaitTimestamp (timestamp);
  if (status != C2D_STATUS_OK) {
    GST_ERROR ("c2dWaitTimestamp %p, error: %d!", timestamp, status);
    return FALSE;
  }

  GST_LOG ("Finished waiting timestamp: %p", timestamp);
  return TRUE;
}
