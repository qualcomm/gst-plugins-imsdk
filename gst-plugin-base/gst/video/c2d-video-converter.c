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


#define GST_C2D_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define CHECK_C2D_CAPABILITY(info, name) \
    GST_DEBUG ("    %-30s [%c]", #name, \
        info.capabilities_mask & C2D_DRIVER_SUPPORTS_##name ? 'x' : ' ');

#define FABS(value)                 (((value) < 0.0F) ? -(value) : (value))
#define DISBALE_BACKGROUND_MASK    0x100000000

#define C2D_INIT_MAX_OBJECT         12
#define C2D_INIT_MAX_TEMPLATE       20

#define DEFAULT_OPT_FLIP_HORIZONTAL FALSE
#define DEFAULT_OPT_FLIP_VERTICAL   FALSE
#define DEFAULT_OPT_ROTATION        GST_C2D_VIDEO_ROTATE_NONE
#define DEFAULT_OPT_BACKGROUND      0x00000000
#define DEFAULT_OPT_CLEAR           TRUE
#define DEFAULT_OPT_UBWC_FORMAT     FALSE

#define GET_OPT_FLIP_HORIZONTAL(s) get_opt_bool (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL, DEFAULT_OPT_FLIP_HORIZONTAL)
#define GET_OPT_FLIP_VERTICAL(s) get_opt_bool (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL, DEFAULT_OPT_FLIP_VERTICAL)
#define GET_OPT_ROTATION(s) get_opt_enum(s, \
    GST_C2D_VIDEO_CONVERTER_OPT_ROTATION, GST_TYPE_C2D_VIDEO_ROTATION, \
    DEFAULT_OPT_ROTATION)
#define GET_OPT_ALPHA(s, v) get_opt_double (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_ALPHA, v)
#define GET_OPT_BACKGROUND(s) get_opt_uint (s, \
    GST_C2D_VIDEO_CONVERTER_OPT_BACKGROUND, DEFAULT_OPT_BACKGROUND)
#define GET_OPT_CLEAR(s) get_opt_bool(s, \
    GST_C2D_VIDEO_CONVERTER_OPT_CLEAR, DEFAULT_OPT_CLEAR)
#define GET_OPT_UBWC_FORMAT(s) get_opt_bool(s, \
    GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, DEFAULT_OPT_UBWC_FORMAT)

#define GST_C2D_GET_LOCK(obj) (&((GstC2dVideoConverter *) obj)->lock)
#define GST_C2D_LOCK(obj)     g_mutex_lock (GST_C2D_GET_LOCK(obj))
#define GST_C2D_UNLOCK(obj)   g_mutex_unlock(GST_C2D_GET_LOCK(obj))

#define GST_CAT_DEFAULT ensure_debug_category()

/// Mutex for protecting the static reference counter.
G_LOCK_DEFINE_STATIC (c2d);
// Reference counter as C2D is singleton.
static gint refcount = 0;

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

typedef struct _GstC2dSurface GstC2dSurface;
typedef struct _GstC2dBlit GstC2dBlit;

struct _GstC2dSurface {
  guint32          id;
  guint32          width;
  guint32          height;
  guint32          format;
  C2D_SURFACE_TYPE type;
  guint32          bits;
};

struct _GstC2dBlit {
  GstC2dSurface surface;
  GArray        *objects;
  guint64       bgcolor;
};

struct _GstC2dVideoConverter
{
  // Global mutex lock.
  GMutex            lock;

  // List of surface options for each input frame.
  GList             *inopts;
  // List of options performed for each output frame.
  GList             *outopts;

  // Map of C2D surface ID and its corresponding GPU address.
  GHashTable        *gpulist;

  // Map of C2D surface ID and its frame virtual mapped address
  GHashTable        *vaddrlist;

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

static gdouble
get_opt_double (const GstStructure * options, const gchar * opt, gdouble value)
{
  gdouble result;
  return gst_structure_get_double (options, opt, &result) ? result : value;
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

GType
gst_c2d_video_rotation_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
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

  G_LOCK (c2d);

  if (!gtype)
    gtype = g_enum_register_static ("GstC2dVideoRotation", variants);

  G_UNLOCK (c2d);

  return gtype;
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

static void
gst_c2d_blit_free (gpointer data)
{
  GstC2dBlit *blit = (GstC2dBlit*) data;

  if (blit->objects != NULL)
    g_array_free (blit->objects, TRUE);
}

static inline gboolean
gst_c2d_blit_objects_compatible (const GstC2dBlit * l_blit,
    const GstC2dBlit * r_blit)
{
  C2D_OBJECT *l_object = NULL, *r_object = NULL;
  GstVideoRectangle l_rect = {0}, r_rect = {0};
  guint idx = 0;

  // TODO For now, support only same object ordering.
  for (idx = 0; idx < l_blit->objects->len; idx++) {
    l_object = &(g_array_index (l_blit->objects, C2D_OBJECT, idx));
    r_object = &(g_array_index (r_blit->objects, C2D_OBJECT, idx));

    // Both objects need to have the same surface ID, mask and global alpha.
    if ((l_object->surface_id != r_object->surface_id) ||
        (l_object->config_mask != r_object->config_mask) ||
        (l_object->global_alpha != r_object->global_alpha))
      return FALSE;

    // Source rectangles must match.
    if ((l_object->source_rect.x != r_object->source_rect.x) ||
        (l_object->source_rect.y != r_object->source_rect.y) ||
        (l_object->source_rect.width != r_object->source_rect.width) ||
        (l_object->source_rect.height != r_object->source_rect.height))
      return FALSE;

    l_rect.x = (l_object->target_rect.x >> 16);
    l_rect.y = (l_object->target_rect.y >> 16);
    l_rect.w = (l_object->target_rect.width >> 16);
    l_rect.h = (l_object->target_rect.height >> 16);

    r_rect.x = (r_object->target_rect.x >> 16);
    r_rect.y = (r_object->target_rect.y >> 16);
    r_rect.w = (r_object->target_rect.width >> 16);
    r_rect.h = (r_object->target_rect.height >> 16);

    // Adjust the dimensions of the target rectangles to be in the same scale.
    r_rect.x = gst_util_uint64_scale_int (r_rect.x,
        l_blit->surface.width, r_blit->surface.width);
    r_rect.y = gst_util_uint64_scale_int (r_rect.y,
        l_blit->surface.height, r_blit->surface.height);
    r_rect.w = gst_util_uint64_scale_int (r_rect.w,
        l_blit->surface.width, r_blit->surface.width);
    r_rect.h = gst_util_uint64_scale_int (r_rect.h,
        l_blit->surface.height, r_blit->surface.height);

    // Target ractangles may not match but must have maximum of 1 pixel delta.
    if ((ABS(l_rect.x - r_rect.x) > 1) || (ABS(l_rect.y - r_rect.y) > 1) ||
        (ABS(l_rect.w - r_rect.w) > 1) || (ABS(l_rect.h - r_rect.h) > 1))
      return FALSE;
  }

  return TRUE;
}

static inline void
gst_c2d_blit_optimize (const GArray * blits, GstC2dBlit * blit)
{
  GstC2dBlit *l_blit = NULL;
  C2D_OBJECT *object = NULL;
  gint l_score = -1, score = -1;
  gdouble l_ratio = 0.0, ratio = 0.0;
  guint idx = 0;

  gst_util_fraction_to_double (blit->surface.width, blit->surface.height, &ratio);

  // Find the best compatible blit composition to current one.
  for (idx = 0; idx < blits->len; idx++) {
    l_blit = &(g_array_index (blits, GstC2dBlit, idx));

    // Return immediately when a blank blit surface is encountered.
    if ((l_blit->surface.id == 0) && (l_blit->objects == NULL))
      return;

    // Exclude current blit object from the comparison.
    if (blit->surface.id == l_blit->surface.id)
      continue;

    // The number of blit objects must match.
    if (l_blit->objects->len != blit->objects->len)
      continue;

    // Background color settings have to match.
    if (l_blit->bgcolor != blit->bgcolor)
      continue;

    gst_util_fraction_to_double (l_blit->surface.width, l_blit->surface.height,
        &l_ratio);

    // Both target surfaces must have the same aspect ratio.
    if (FABS(l_ratio - ratio) > 0.005)
      continue;

    // The blit surface must have the same or lower resolution.
    if ((blit->surface.width * blit->surface.height) >
            (l_blit->surface.width * l_blit->surface.height))
      continue;

    // Compare blit objects.
    if (!gst_c2d_blit_objects_compatible (l_blit, blit))
      continue;

    // Increase the score if both target blit surfaces have the same dimensions.
    l_score = ((l_blit->surface.width == blit->surface.width) &&
        (l_blit->surface.height == blit->surface.height)) ? 1 : 0;
    // Increase the score if both target blit surfaces have the same type.
    l_score += (l_blit->surface.type == blit->surface.type) ? 1 : 0;
    // Increase the score if both target blit surfaces have the same format.
    l_score += (l_blit->surface.format == blit->surface.format) ? 1 : 0;

    if (l_score <= score)
      continue;

    // Update the current high score tracker.
    score = l_score;

    // Update the objects for this blit composition.
    if (blit->objects->len > 1)
      g_array_remove_range (blit->objects, 1, blit->objects->len);

    object = &(g_array_index (blit->objects, C2D_OBJECT, 0));

    object->surface_id = l_blit->surface.id;
    object->config_mask = (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT);
    object->global_alpha = G_MAXUINT8;

    object->source_rect.x = 0;
    object->source_rect.y = 0;
    object->source_rect.width = l_blit->surface.width << 16;
    object->source_rect.height = l_blit->surface.height << 16;

    object->target_rect.x = 0;
    object->target_rect.y = 0;
    object->target_rect.width = blit->surface.width << 16;
    object->target_rect.height = blit->surface.height << 16;

    blit->bgcolor = DISBALE_BACKGROUND_MASK;
  }

  return;
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

static guint
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

static void
gst_c2d_populate_object (C2D_OBJECT * object, const GstStructure * opts,
    const GstVideoFrame * inframe, const C2D_RECT * srcrect,
    const GstVideoFrame * outframe, const C2D_RECT * dstrect)
{
  gint x = 0, y = 0, width = 0, height = 0;
  guint surface_id = 0;

  surface_id = object->surface_id;
  object->config_mask = (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT);

  // Transform alpha from double (0.0 - 1.0) to integer (0 - 255).
  object->global_alpha = G_MAXUINT8 * GET_OPT_ALPHA (opts, 1.0);
  GST_TRACE ("Input surface %x - Global alpha: %u", surface_id,
      object->global_alpha);

  if (object->global_alpha != G_MAXUINT8)
    object->config_mask |= C2D_GLOBAL_ALPHA_BIT;

  // Setup the source rectangle.
  x = srcrect->x;
  y = srcrect->y;
  width = srcrect->width;
  height = srcrect->height;

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
  x = dstrect->x;
  y = dstrect->y;
  width = dstrect->width;
  height = dstrect->height;

  // Setup rotation angle and adjustments.
  switch (GET_OPT_ROTATION (opts)) {
    case GST_C2D_VIDEO_ROTATE_90_CW:
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

      x = (dstrect->width && dstrect->height) ?
          x : (GST_VIDEO_FRAME_WIDTH (outframe) - width) / 2;

      object->target_rect.width = height << 16;
      object->target_rect.height = width << 16;

      // Adjust the target rectangle coordinates.
      object->target_rect.y =
          (GST_VIDEO_FRAME_WIDTH (outframe) - (x + width)) << 16;
      object->target_rect.x = y << 16;
      break;
    }
    case GST_C2D_VIDEO_ROTATE_180:
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
    case GST_C2D_VIDEO_ROTATE_90_CCW:
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

      x = (dstrect->width && dstrect->height) ?
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

static gint
gst_c2d_update_objects (GArray * objects, const guint surface_id,
    const GstStructure * opts, const GstVideoFrame * inframe,
    const GstVideoFrame * outframe)
{
  const GValue *srclist = NULL, *dstlist = NULL, *entry = NULL;
  guint idx = 0, num = 0, i = 0, n_srcrects = 0, n_dstrects = 0, n_rects = 0;
  gint area = 0;

  // Extract the source and destination rectangles.
  srclist = gst_structure_get_value (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_RECTANGLES);
  dstlist = gst_structure_get_value (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_RECTANGLES);

  // Make sure that there is at least one new rectangle in the lists.
  n_srcrects = (srclist == NULL) ? 0 : gst_value_array_get_size (srclist);
  n_dstrects = (dstlist == NULL) ? 0 : gst_value_array_get_size (dstlist);

  n_srcrects = (n_srcrects == 0) ? 1 : n_srcrects;
  n_dstrects = (n_dstrects == 0) ? 1 : n_dstrects;

  if (n_srcrects > n_dstrects) {
    GST_WARNING ("Number of source rectangles exceeds the number of "
        "destination rectangles, clipping!");
    n_rects = n_srcrects = n_dstrects;
  } else if (n_srcrects < n_dstrects) {
    GST_WARNING ("Number of destination rectangles exceeds the number of "
        "source rectangles, clipping!");
    n_rects = n_dstrects = n_srcrects;
  } else {
    // Same number of source and destination rectangles.
    n_rects = n_srcrects;
  }

  // Increase the size of the C2D blit objects array.
  num = objects->len;
  g_array_set_size (objects, (num + n_rects));

  // Fill a separate C2D object for each rectangle pair in this input frame.
  for (idx = 0; idx < n_rects; idx++) {
    C2D_OBJECT *object = &(g_array_index (objects, C2D_OBJECT, num));
    C2D_RECT *l_rect = NULL, *r_rect = NULL;
    C2D_RECT srcbox = {0}, dstbox = {0};

    entry = (n_srcrects != 0) ? gst_value_array_get_value (srclist, idx) : NULL;

    if ((entry != NULL) && gst_value_array_get_size (entry) == 4) {
      srcbox.x = g_value_get_int (gst_value_array_get_value (entry, 0));
      srcbox.y = g_value_get_int (gst_value_array_get_value (entry, 1));
      srcbox.width = g_value_get_int (gst_value_array_get_value (entry, 2));
      srcbox.height = g_value_get_int (gst_value_array_get_value (entry, 3));
    } else if (entry != NULL) {
      GST_WARNING ("Source rectangle at index %u does not contain "
          "exactly 4 values, using default values!", idx);
    }

    entry = (n_dstrects != 0) ? gst_value_array_get_value (dstlist, idx) : NULL;

    if ((entry != NULL) && gst_value_array_get_size (entry) == 4) {
      dstbox.x = g_value_get_int (gst_value_array_get_value (entry, 0));
      dstbox.y = g_value_get_int (gst_value_array_get_value (entry, 1));
      dstbox.width = g_value_get_int (gst_value_array_get_value (entry, 2));
      dstbox.height = g_value_get_int (gst_value_array_get_value (entry, 3));
    } else if (entry != NULL) {
      GST_WARNING ("Destination rectangle at index %u does not contain "
          "exactly 4 values, using default values!", idx);
    }

    object->surface_id = surface_id;

    // Populate C2D object.
    gst_c2d_populate_object (object, opts, inframe, &srcbox, outframe, &dstbox);

    // Calculate the target area filled with frame content.
    l_rect = &(object->target_rect);
    // Add the rectangle area of current C2D object to the total area.
    area += (l_rect->width >> 16) * (l_rect->height >> 16);

    for (i = 0; i < num; i++) {
      object = &(g_array_index (objects, C2D_OBJECT, i));
      r_rect = &(object->target_rect);

      // Subtract overlapping area from the total rectangle area.
      area -= gst_c2d_rectangles_overlapping_area (l_rect, r_rect);

      // Set current object to point to the next one (linked list).
      if (i <= (num - 1))
        object->next = &(g_array_index (objects, C2D_OBJECT, (i + 1)));
    }

    // Increment the counter for the C2D objets.
    num++;
  }

  return area;
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
  GST_C2D_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), 0,
      "Buffer %p does not have FD memory!", vframe->buffer);

  // Get the input buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (surfaces, GUINT_TO_POINTER (fd))) {
    // Create an output surface and add its ID to the output hash table.
    surface_id = gst_c2d_create_surface (convert, vframe, bits, isubwc);
    GST_C2D_RETURN_VAL_IF_FAIL (surface_id != 0, 0, "Failed to create surface!");

    g_hash_table_insert (surfaces, GUINT_TO_POINTER (fd),
        GUINT_TO_POINTER (surface_id));
  } else {
    gpointer vaddress = NULL;

    // Get the input surface ID from the input hash table.
    surface_id = GPOINTER_TO_UINT (
        g_hash_table_lookup (surfaces, GUINT_TO_POINTER (fd)));
    vaddress = g_hash_table_lookup (convert->vaddrlist,
        GUINT_TO_POINTER (surface_id));

    if (vaddress != GST_VIDEO_FRAME_PLANE_DATA (vframe, 0)) {
      gboolean success = FALSE;

      success = gst_c2d_update_surface (convert, vframe, surface_id, bits, isubwc);

      GST_C2D_RETURN_VAL_IF_FAIL (success == TRUE, 0,
          "Update failed for target surface %x", surface_id);
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

  // Load C2D library.
  convert->c2dhandle = dlopen ("libC2D2.so", RTLD_NOW);
  GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->c2dhandle != NULL, NULL,
      gst_c2d_video_converter_free (convert),
      "Failed to open C2D library, error: %s!", dlerror());

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
  if (!success) {
    gst_c2d_video_converter_free (convert);
    return NULL;
  }

  convert->insurfaces = g_hash_table_new (NULL, NULL);
  GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->insurfaces != NULL, NULL,
      gst_c2d_video_converter_free (convert),
      "Failed to create hash table for source surfaces!");

  convert->outsurfaces = g_hash_table_new (NULL, NULL);
  GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->outsurfaces != NULL, NULL,
      gst_c2d_video_converter_free (convert),
      "Failed to create hash table for target surfaces!");

  convert->gpulist = g_hash_table_new (NULL, NULL);
  GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->gpulist != NULL, NULL,
      gst_c2d_video_converter_free (convert),
      "Failed to create hash table for GPU mapped addresses!");

  convert->vaddrlist = g_hash_table_new (NULL, NULL);
  GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->vaddrlist != NULL, NULL,
      gst_c2d_video_converter_free (convert),
      "Failed to create hash table for mapped virtual addresses!");

  setup.max_object_list_needed = C2D_INIT_MAX_OBJECT;
  setup.max_surface_template_needed = C2D_INIT_MAX_TEMPLATE;

  G_LOCK (c2d);

  if (refcount++ == 0)
    status = convert->DriverInit (&setup);

  G_UNLOCK (c2d);

  GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (C2D_STATUS_OK == status, NULL,
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

  g_mutex_init (&convert->lock);

  GST_INFO ("Created C2D converter: %p", convert);
  return convert;
}

void
gst_c2d_video_converter_free (GstC2dVideoConverter * convert)
{
  g_mutex_clear (&convert->lock);

  if (convert->inopts != NULL)
    g_list_free_full (convert->inopts, (GDestroyNotify) gst_structure_free);

  if (convert->outopts != NULL)
    g_list_free_full (convert->outopts, (GDestroyNotify) gst_structure_free);

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

  GST_INFO ("Destroyed C2D converter: %p", convert);
  g_slice_free (GstC2dVideoConverter, convert);
}

gboolean
gst_c2d_video_converter_set_input_opts (GstC2dVideoConverter * convert,
    guint index, GstStructure * opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);

  GST_C2D_LOCK (convert);

  if ((index >= g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_C2D_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->inopts = g_list_remove (convert->inopts,
        g_list_nth_data (convert->inopts, index));
    GST_C2D_UNLOCK (convert);
    return TRUE;
  } else if (index > g_list_length (convert->inopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_C2D_UNLOCK (convert);
    return FALSE;
  }

  if (index == g_list_length (convert->inopts)) {
    GST_LOG ("Add a new opts structure in the list at index %u", index);

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
    guint index, GstStructure * opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);

  GST_C2D_LOCK (convert);

  if ((index >= g_list_length (convert->outopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_C2D_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->outopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->outopts = g_list_remove (convert->outopts,
        g_list_nth_data (convert->outopts, index));
    GST_C2D_UNLOCK (convert);
    return TRUE;
  } else if (index > g_list_length (convert->outopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_C2D_UNLOCK (convert);
    return FALSE;
  }

  if (index == g_list_length (convert->outopts)) {
    GST_LOG ("Add a new opts structure in the list at index %u", index);

    convert->outopts = g_list_append (convert->outopts,
        gst_structure_new_empty ("Input"));
  }

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options,
      g_list_nth_data (convert->outopts, index));
  gst_structure_free (opts);

  GST_C2D_UNLOCK (convert);

  return TRUE;
}

gpointer
gst_c2d_video_converter_submit_request (GstC2dVideoConverter * convert,
    GstC2dComposition * compositions, guint n_compositions)
{
  GArray *blits = NULL, *requests = NULL;
  GstStructure *opts = NULL;
  guint idx = 0, num = 0, offset = 0, surface_id = 0, area = 0;
  C2D_STATUS status = C2D_STATUS_OK;

  g_return_val_if_fail (convert != NULL, NULL);
  g_return_val_if_fail ((compositions != NULL) && (n_compositions != 0), NULL);

  blits = g_array_sized_new (FALSE, TRUE, sizeof (GstC2dBlit), n_compositions);

  g_array_set_size (blits, n_compositions);
  g_array_set_clear_func (blits, gst_c2d_blit_free);

  requests = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_compositions);
  g_array_set_size (requests, n_compositions);

  // Sort compositions by output frame dimensions.
  qsort (compositions, n_compositions, sizeof (GstC2dComposition),
      gst_c2d_compare_compositions);

  for (idx = 0; idx < n_compositions; idx++) {
    const GstVideoFrame *outframe = compositions[idx].outframe;
    const GstVideoFrame *inframes = compositions[idx].inframes;
    GstC2dBlit *blit = &(g_array_index (blits, GstC2dBlit, idx));
    guint n_inputs = 0;

    n_inputs = compositions[idx].n_inputs;

    // Sanity checks, output frame and input frames must not be NULL.
    g_return_val_if_fail (outframe != NULL, NULL);
    g_return_val_if_fail ((inframes != NULL) && (n_inputs != 0), NULL);

    // Skip this configuration if there is no output buffer.
    if (NULL == outframe->buffer)
      continue;

    // Initialize empty options structure in case none have been set.
    if (idx >= g_list_length (convert->outopts)) {
      convert->outopts =
          g_list_append (convert->outopts, gst_structure_new_empty ("options"));
    }

    // Total area of the output frame that is to be used in later calculations
    // to determine whether there are unoccupied background pixels to be filled.
    area = GST_VIDEO_FRAME_WIDTH (outframe) * GST_VIDEO_FRAME_HEIGHT (outframe);

    // Initial allocation of C2D blit objects.
    blit->objects = g_array_new (FALSE, TRUE, sizeof (C2D_OBJECT));

    GST_C2D_LOCK (convert);

    // Iterate over the input frames.
    for (num = 0; num < n_inputs; num++) {
      const GstVideoFrame *inframe = &inframes[num];

      if (NULL == inframe->buffer)
        continue;

      // Initialize empty options structure in case none have been set.
      if ((num + offset) >= g_list_length (convert->inopts)) {
        convert->inopts =
            g_list_append (convert->inopts, gst_structure_new_empty ("options"));
      }

      // Get the options for current input buffer.
      opts = g_list_nth_data (convert->inopts, (num + offset));

      surface_id = gst_c2d_retrieve_surface_id (convert, convert->insurfaces,
          C2D_SOURCE, inframe, GET_OPT_UBWC_FORMAT (opts));
      GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (surface_id != 0, NULL,
          GST_C2D_UNLOCK (convert); g_array_free (blits, TRUE);
          g_array_free (requests, TRUE),
          "Failed to get surface ID for input buffer!");

      // Extract and populate blit objects and return the area occupied by them.
      area -= gst_c2d_update_objects (
          blit->objects, surface_id, opts, inframe, outframe);
    }

    // Increate the offset to the input frame options.
    offset += n_inputs;

    // Get the options for current output frame.
    opts = GST_STRUCTURE (g_list_nth_data (convert->outopts, idx));

    surface_id = gst_c2d_retrieve_surface_id (convert, convert->outsurfaces,
        C2D_SOURCE | C2D_TARGET, outframe, GET_OPT_UBWC_FORMAT (opts));
    GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (surface_id != 0, NULL,
        GST_C2D_UNLOCK (convert); g_array_free (blits, TRUE);
        g_array_free (requests, TRUE),
        "Failed to get surface ID for output buffer!");

    blit->surface.id = surface_id;

    // Extract background color and whether to clear it.
    blit->bgcolor = GET_OPT_BACKGROUND (opts);
    blit->bgcolor |= !(GET_OPT_CLEAR (opts) && (area > 0)) ?
        DISBALE_BACKGROUND_MASK : 0x00;

    GST_C2D_UNLOCK (convert);

    // Fetch the target blit surface parameters.
    status = convert->QuerySurface (blit->surface.id, &(blit->surface.bits),
        &(blit->surface.type), &(blit->surface.width), &(blit->surface.height),
        &(blit->surface.format));
    GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (C2D_STATUS_OK == status, NULL,
        g_array_free (blits, TRUE); g_array_free (requests, TRUE),
        "Failed to query output surface %x parameters!", surface_id);

    // Optimize the blit object to use an existing output surface.
    gst_c2d_blit_optimize (blits, blit);

    // Fill the surface if there is visible background area.
    if (!(blit->bgcolor & DISBALE_BACKGROUND_MASK)) {
      GST_LOG ("Fill output surface %x", blit->surface.id);

      status = convert->FillSurface (blit->surface.id, blit->bgcolor, NULL);
      GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (C2D_STATUS_OK == status, NULL,
          g_array_free (blits, TRUE); g_array_free (requests, TRUE),
          "FillSurface failed for surface %x, error: %d!", surface_id, status);
    }

    GST_LOG ("Draw output surface %x", blit->surface.id);

    status = convert->Draw (blit->surface.id, 0, NULL, 0, 0,
        (C2D_OBJECT*) blit->objects->data, blit->objects->len);
    GST_C2D_RETURN_VAL_IF_FAIL_WITH_CLEAN (C2D_STATUS_OK == status, NULL,
        g_array_free (blits, TRUE); g_array_free (requests, TRUE),
        "Draw failed for surface %x, error: %d!", surface_id, status);

    g_array_index (requests, guint, idx) = blit->surface.id;
  }

  // Release the memory for the blit compositions.
  g_array_free (blits, TRUE);

  return requests;
}

gboolean
gst_c2d_video_converter_wait_request (GstC2dVideoConverter *convert,
    gpointer request_id)
{
  GArray *requests = (GArray*) request_id;
  C2D_STATUS status = C2D_STATUS_OK;
  guint idx = 0, surface_id = 0;
  gboolean success = TRUE;

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
