/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gles-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>
#include <cstdint>
#include <cmath>

#include <gst/gfx/ib2c.h>


#define GST_CAT_DEFAULT gst_video_converter_engine_debug

#define GPOINTER_TO_GUINT64(p)   ((guint64) (p))
#define GUINT64_TO_POINTER(p)    ((gpointer) (p))

#define GST_GLES_GET_LOCK(obj)   (&((GstGlesVideoConverter *)obj)->lock)
#define GST_GLES_LOCK(obj)       g_mutex_lock (GST_GLES_GET_LOCK(obj))
#define GST_GLES_UNLOCK(obj)     g_mutex_unlock (GST_GLES_GET_LOCK(obj))

#define GST_GLES_INPUT_QUARK     g_quark_from_static_string ("Input")

#define GST_GLES_IS_QC_VENDOR(v) \
    (g_quark_from_static_string (v) == g_quark_from_static_string ("Qualcomm"))

typedef struct _GstGlesSurface GstGlesSurface;
typedef struct _GstNormalizeRequest GstNormalizeRequest;

struct _GstGlesSurface
{
  // Surface ID.
  guint64 id;
  // Number of times that this surface was referenced.
  guint n_refs;
};

struct _GstNormalizeRequest
{
  // Video frame which will be normalized.
  GstBuffer          *buffer;
  const GstVideoInfo *info;

  // Offset and scale factors for each component of the pixel.
  gdouble            offsets[GST_VCE_MAX_CHANNELS];
  gdouble            scales[GST_VCE_MAX_CHANNELS];

  // The data type of the frame pixels.
  guint64            datatype;
};

struct _GstGlesVideoConverter
{
  // Global mutex lock.
  GMutex          lock;

  // The company responsible for this GL implementation.
  const gchar     *vendor;
  // The name of the GL renderer.
  const gchar     *renderer;

  // Map of buffer FDs and their corresponding GstGlesSurface.
  GHashTable      *insurfaces;
  GHashTable      *outsurfaces;

  // TODO: Update fence objects to store the FDs data to avoid below map
  // Map of request_id and the corresponding buffer FDs that doesn't need cache
  GHashTable      *nocache;

  // Map of fence object and corresponding CPU offloaded normalization requests.
  GHashTable      *normrequests;

  // List of not yet processed IB2C fence objects.
  GList           *fences;

  // IB2C engine interface.
  ::ib2c::IEngine *engine;
};

static gint
gst_video_format_to_ib2c_format (GstVideoFormat format, const guint64 datatype)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return ::ib2c::ColorFormat::kNV12;
    case GST_VIDEO_FORMAT_NV12_Q08C:
      return ::ib2c::ColorFormat::kNV12 | ::ib2c::ColorMode::kUBWC;
    case GST_VIDEO_FORMAT_P010_10LE:
      return ::ib2c::ColorFormat::kP010;
    case GST_VIDEO_FORMAT_NV21:
      return ::ib2c::ColorFormat::kNV21;
    case GST_VIDEO_FORMAT_NV16:
      return ::ib2c::ColorFormat::kNV16;
    case GST_VIDEO_FORMAT_NV61:
      return ::ib2c::ColorFormat::kNV61;
    case GST_VIDEO_FORMAT_NV24:
      return ::ib2c::ColorFormat::kNV24;
    case GST_VIDEO_FORMAT_YUY2:
      return ::ib2c::ColorFormat::kYUYV;
    case GST_VIDEO_FORMAT_UYVY:
      return ::ib2c::ColorFormat::kUYVY;
    case GST_VIDEO_FORMAT_YVYU:
      return ::ib2c::ColorFormat::kYVYU;
    case GST_VIDEO_FORMAT_VYUY:
      return ::ib2c::ColorFormat::kVYUY;
    case GST_VIDEO_FORMAT_RGB:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kRGB888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kRGB888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kRGB161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kRGB161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kRGB161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kRGB323232F;

      return -1;
    case GST_VIDEO_FORMAT_BGR:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kBGR888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kBGR888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kBGR161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kBGR161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kBGR161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kBGR323232F;

      return -1;
    case GST_VIDEO_FORMAT_RGBA:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kRGBA8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kRGBA8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kRGBA16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kRGBA16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kRGBA16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kRGBA32323232F;

      return -1;
    case GST_VIDEO_FORMAT_BGRA:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kBGRA8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kBGRA8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kBGRA16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kBGRA16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kBGRA16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kBGRA32323232F;

      return -1;
    case GST_VIDEO_FORMAT_ARGB:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kARGB8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kARGB8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kARGB16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kARGB16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kARGB16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kARGB32323232F;

      return -1;
    case GST_VIDEO_FORMAT_ABGR:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kABGR8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kABGR8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kABGR16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kABGR16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kABGR16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kABGR32323232F;

      return -1;
    case GST_VIDEO_FORMAT_RGBx:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kRGBX8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kRGBX8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kRGBX16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kRGBX16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kRGBX16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kRGBX32323232F;

      return -1;
    case GST_VIDEO_FORMAT_BGRx:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kBGRX8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kBGRX8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kBGRX16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kBGRX16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kBGRX16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kBGRX32323232F;

      return -1;
    case GST_VIDEO_FORMAT_xRGB:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kXRGB8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kXRGB8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kXRGB16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kXRGB16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kXRGB16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kXRGB32323232F;

      return -1;
    case GST_VIDEO_FORMAT_xBGR:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kXBGR8888;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kXBGR8888I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kXBGR16161616;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kXBGR16161616I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kXBGR16161616F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kXBGR32323232F;

      return -1;
    case GST_VIDEO_FORMAT_GRAY8:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kGRAY8;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kGRAY8I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kGRAY16;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kGRAY16I;

      return -1;
    case GST_VIDEO_FORMAT_RGBP:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kR8G8B8;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kR8G8B8I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kR16G16B16;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kR16G16B16I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kR16G16B16F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kR32G32B32F;

      return -1;
    case GST_VIDEO_FORMAT_BGRP:
      if (datatype == GST_VCE_DATA_TYPE_U8)
        return ::ib2c::ColorFormat::kB8G8R8;
      else if (datatype == GST_VCE_DATA_TYPE_I8)
        return ::ib2c::ColorFormat::kB8G8R8I;
      else if (datatype == GST_VCE_DATA_TYPE_U16)
        return ::ib2c::ColorFormat::kB16G16R16;
      else if (datatype == GST_VCE_DATA_TYPE_I16)
        return ::ib2c::ColorFormat::kB16G16R16I;
      else if (datatype == GST_VCE_DATA_TYPE_F16)
        return ::ib2c::ColorFormat::kB16G16R16F;
      else if (datatype == GST_VCE_DATA_TYPE_F32)
        return ::ib2c::ColorFormat::kB32G32R32F;

      return -1;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }

  return -1;
}

static guint64
gst_gles_create_surface (GstGlesVideoConverter * convert, const gchar * direction,
    GstBuffer * buffer, const GstVideoInfo * info, guint64 datatype)
{
  GstMemory *memory = NULL;
  const gchar *mode = NULL;
  ::ib2c::Surface surface;
  guint type = 0, idx = 0, num = 0, n_planes = 0, n_views = 0, bytedepth = 1;
  gint format = 0;
  guint64 surface_id = 0;

  type |= (g_quark_from_static_string (direction) == GST_GLES_INPUT_QUARK) ?
      ::ib2c::SurfaceFlags::kInput : ::ib2c::SurfaceFlags::kOutput;

  memory = gst_buffer_peek_memory (buffer, 0);

  if ((memory == NULL) || !gst_is_fd_memory (memory)) {
    GST_ERROR ("%s buffer memory is not FD backed!", direction);
    return 0;
  }

  surface.fd = gst_fd_memory_get_fd (memory);
  surface.width = GST_VIDEO_INFO_WIDTH (info);
  surface.height = GST_VIDEO_INFO_HEIGHT (info);
  surface.size = gst_buffer_get_size (buffer);

  if (datatype == GST_VCE_DATA_TYPE_I8)
    mode = " INT8";
  else if (datatype == GST_VCE_DATA_TYPE_U16)
    mode = " UINT16";
  else if (datatype == GST_VCE_DATA_TYPE_I16)
    mode = " INT16";
  else if (datatype == GST_VCE_DATA_TYPE_U32)
    mode = " UINT32";
  else if (datatype == GST_VCE_DATA_TYPE_I32)
    mode = " INT32";
  else if (datatype == GST_VCE_DATA_TYPE_U64)
    mode = " UINT64";
  else if (datatype == GST_VCE_DATA_TYPE_I64)
    mode = " INT64";
  else if (datatype == GST_VCE_DATA_TYPE_F16)
    mode = " FLOAT16";
  else if (datatype == GST_VCE_DATA_TYPE_F32)
    mode = " FLOAT32";
  else
    mode = " UINT8";

  // TODO: Workaround. Remove once GLES supports these pixel types.
  // Overwrite data type in some cases and set variable for stride correction.
  // Normalization to end pixel type will be done after all other operations.
  if (datatype == GST_VCE_DATA_TYPE_U32 || datatype == GST_VCE_DATA_TYPE_I32) {
    bytedepth = 4;
    datatype = GST_VCE_DATA_TYPE_U8;
  } else if (datatype == GST_VCE_DATA_TYPE_U64 || datatype == GST_VCE_DATA_TYPE_I64) {
    bytedepth = 8;
    datatype = GST_VCE_DATA_TYPE_U8;
  } else if (GST_GLES_IS_QC_VENDOR (convert->vendor) &&
      (datatype == GST_VCE_DATA_TYPE_U16 || datatype == GST_VCE_DATA_TYPE_I16)) {
    bytedepth = 2;
    datatype = GST_VCE_DATA_TYPE_U8;
  } else if ((GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_GRAY8) &&
      (datatype == GST_VCE_DATA_TYPE_F16 || datatype == GST_VCE_DATA_TYPE_F32)) {
    bytedepth = (datatype == GST_VCE_DATA_TYPE_F32) ? 4 : 2;
    datatype = GST_VCE_DATA_TYPE_U8;
  }

  format =
      gst_video_format_to_ib2c_format (GST_VIDEO_INFO_FORMAT (info), datatype);

  if (format == (-1)) {
    GST_ERROR ("Unsupported format %s%s combination!",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)), mode);
    return 0;
  }

  surface.format = format;

  n_planes = GST_VIDEO_INFO_N_PLANES (info);
  n_views = GST_VIDEO_INFO_VIEWS (info);

  GST_TRACE ("%s surface FD[%d] - Width[%u] Height[%u] Format[%s%s] Planes[%u]"
      " Views[%u]", direction, surface.fd, surface.width, surface.height,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)), mode,
      n_planes, n_views);

  // Reset number of views for the calculations below, when RGB is interleaved.
  if ((GST_VIDEO_INFO_FORMAT (info) != GST_VIDEO_FORMAT_RGBP) &&
      (GST_VIDEO_INFO_FORMAT (info) != GST_VIDEO_FORMAT_BGRP))
    n_views = 1;

  for (num = 0; num < n_planes; num++) {
    ::ib2c::Plane plane;

    plane.stride = GST_VIDEO_INFO_PLANE_STRIDE (info, num);
    plane.offset = GST_VIDEO_INFO_PLANE_OFFSET (info, num);

    // Correction of the stride for some formats, NOP otherwise.
    plane.stride /= bytedepth;

    for (idx = (num * n_views); idx < (n_views * (num + 1)); idx++) {
      // Correction of the offset as this is sub plane for the planar RGB.
      if (idx != (num * n_views)) {
        plane.offset += GST_VIDEO_INFO_PLANE_STRIDE (info, idx) *
            GST_VIDEO_INFO_COMP_HEIGHT (info, idx) / n_views;
      }

      surface.planes.push_back(plane);

      GST_TRACE ("%s surface FD[%d] - Plane[%u] Stride[%u] Offset[%u]",
          direction, surface.fd, idx, plane.stride, plane.offset);
    }
  }

  try {
    surface_id = convert->engine->CreateSurface (surface, type);
    GST_DEBUG ("Created %s surface with id %lx", direction, surface_id);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create %s surface, error: '%s'!", direction, e.what());
    return 0;
  }

  return surface_id;
}

static void
gst_gles_destroy_surface (gpointer key, gpointer value, gpointer userdata)
{
  GstGlesVideoConverter *convert = (GstGlesVideoConverter*) userdata;
  GstGlesSurface *glsurface = (GstGlesSurface*) value;

  try {
    convert->engine->DestroySurface (glsurface->id);
    GST_DEBUG ("Destroying surface with id %lx", glsurface->id);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to destroy IB2C surface, error: '%s'!", e.what());
    return;
  }

  g_slice_free (GstGlesSurface, glsurface);
  return;
}

static void
gst_gles_remove_input_surfaces (GstGlesVideoConverter * convert, GArray * fds)
{
  GstGlesSurface *glsurface = NULL;
  guint idx = 0, fd;
  gboolean success;

  for (idx = 0; idx < fds->len; idx++) {
    fd = g_array_index (fds, guint, idx);

    success = g_hash_table_lookup_extended (convert->insurfaces,
        GUINT_TO_POINTER (fd), NULL, ((gpointer *) &glsurface));

    if (!success)
      continue;

    if (--(glsurface->n_refs) != 0)
      continue;

    try {
      GST_DEBUG ("Destroying surface with id %lx", glsurface->id);
      convert->engine->DestroySurface(glsurface->id);
    } catch (std::exception& e) {
      GST_ERROR ("Failed to destroy IB2C surface, error: '%s'!", e.what());
      return;
    }

    g_hash_table_remove (convert->insurfaces, GUINT_TO_POINTER (fd));
    g_slice_free (GstGlesSurface, glsurface);
  }
}

static void
gst_gles_update_object (::ib2c::Object * object, const guint64 surface_id,
    const GstVideoBlit * vblit, GstVideoComposition * composition)
{
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  gint x = 0, y = 0, width = 0, height = 0;

  object->id = surface_id;
  object->mask = 0;

  object->alpha = vblit->alpha;
  GST_TRACE ("Input surface %lx - Global alpha: %u", surface_id, object->alpha);

  // Setup the source quadrilateral.
  if (vblit->mask & GST_VCE_MASK_SOURCE) {
    object->source.a = ::ib2c::Point(vblit->source.a.x, vblit->source.a.y);
    object->source.b = ::ib2c::Point(vblit->source.b.x, vblit->source.b.y);
    object->source.c = ::ib2c::Point(vblit->source.c.x, vblit->source.c.y);
    object->source.d = ::ib2c::Point(vblit->source.d.x, vblit->source.d.y);

    object->mask |= ::ib2c::ConfigMask::kSource;
  }

  if (vblit->mask & GST_VCE_MASK_FLIP_VERTICAL) {
    object->mask |= ::ib2c::ConfigMask::kVFlip;
    GST_TRACE ("Input surface %lx - Flip Vertically", surface_id);
  }

  if (vblit->mask & GST_VCE_MASK_FLIP_HORIZONTAL) {
    object->mask |= ::ib2c::ConfigMask::kHFlip;
    GST_TRACE ("Input surface %lx - Flip Horizontally", surface_id);
  }

  // Setup the target rectangle.
  if (vblit->mask & GST_VCE_MASK_DESTINATION) {
    x = object->destination.x = vblit->destination.x;
    y = object->destination.y = vblit->destination.y;
    width = object->destination.w = vblit->destination.w;
    height = object->destination.h = vblit->destination.h;

    object->mask |= ::ib2c::ConfigMask::kDestination;
  } else {
    width = GST_VIDEO_INFO_WIDTH (composition->info);
    height = GST_VIDEO_INFO_HEIGHT (composition->info);
  }

  if (vblit->mask & GST_VCE_MASK_ROTATION)
    rotate = vblit->rotate;

  // Setup rotation angle and adjustments.
  switch (rotate) {
    case GST_VCE_ROTATE_90:
      GST_TRACE ("Input surface %lx - rotate 90° clockwise", surface_id);

      object->rotation = 90.0;
      object->mask |= ::ib2c::ConfigMask::kRotation;
      break;
    case GST_VCE_ROTATE_180:
      GST_TRACE ("Input surface %lx - rotate 180°", surface_id);

      object->rotation = 180.0;
      object->mask |= ::ib2c::ConfigMask::kRotation;
      break;
    case GST_VCE_ROTATE_270:
      GST_TRACE ("Input surface %lx - rotate 90° counter-clockwise", surface_id);

      object->rotation = 270.0;
      object->mask |= ::ib2c::ConfigMask::kRotation;
      break;
    default:
      object->rotation = 0.0;
      break;
  }

  GST_TRACE ("Input surface %lx - Source quadrilateral: A(%f, %f) B(%f, %f) "
      "C(%f, %f) D(%f, %f)", surface_id, object->source.a.x, object->source.a.y,
      object->source.b.x, object->source.b.y, object->source.c.x,
      object->source.c.y, object->source.d.x, object->source.d.y);

  GST_TRACE ("Input surface %lx - Target rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, x, y, width, height);
}

static guint64
gst_gles_retrieve_surface_id (GstGlesVideoConverter * convert,
    GHashTable * surfaces, const gchar * direction,
    GstBuffer * buffer, const GstVideoInfo * info, const guint64 flags)
{
  GstMemory *memory = NULL;
  GstGlesSurface *glsurface = NULL;
  guint fd = 0;
  guint64 surface_id = 0;

  // Get the 1st (and only) memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (buffer, 0);

  if ((memory == NULL) || !gst_is_fd_memory (memory)) {
    GST_ERROR ("Buffer %p does not have FD memory!", buffer);
    return 0;
  }

  // Get the input buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (surfaces, GUINT_TO_POINTER (fd))) {
    // Create an input surface and add its ID to the input hash table.
    surface_id =
        gst_gles_create_surface (convert, direction, buffer, info, flags);

    if (surface_id == 0) {
      GST_ERROR ("Failed to create surface!");
      return 0;
    }

    glsurface = g_slice_new (GstGlesSurface);
    glsurface->id = surface_id;
    glsurface->n_refs = 1;

    g_hash_table_insert (surfaces, GUINT_TO_POINTER (fd), glsurface);
  } else {
    // Get the input surface ID from the input hash table.
    glsurface = (GstGlesSurface*) (
        g_hash_table_lookup (surfaces, GUINT_TO_POINTER (fd)));
    surface_id = glsurface->id;

    glsurface->n_refs += 1;
  }

  return surface_id;
}

gboolean
gst_gles_video_converter_compose (GstGlesVideoConverter * convert,
    GstVideoComposition * compositions, guint n_compositions, gpointer * fence)
{
  GArray *fds = NULL, *normalizations = NULL;
  GstNormalizeRequest *normrequest = NULL;
  guint idx = 0, num = 0, n_blits = 0, n_normalizations = 0;
  guint64 surface_id = 0;
  gboolean success = TRUE, normalize = FALSE;

  std::vector<::ib2c::Composition> comps;

  fds = g_array_new (FALSE, FALSE, sizeof (guint));

  normalizations = g_array_sized_new (FALSE, FALSE, sizeof (GstNormalizeRequest),
      n_compositions);
  g_array_set_size (normalizations, n_compositions);

  g_return_val_if_fail (fds != NULL, FALSE);

  for (idx = 0; idx < n_compositions; idx++) {
    GstVideoComposition *composition = &(compositions[idx]);
    GstBuffer *outbuffer = composition->buffer;
    GstVideoBlit *blits = composition->blits;

    n_blits = composition->n_blits;

    // Sanity checks, output frame and blit entries must not be NULL.
    g_return_val_if_fail (outbuffer != NULL, FALSE);
    g_return_val_if_fail ((blits != NULL) && (n_blits != 0), FALSE);

    std::vector<::ib2c::Object> objects;

    // Iterate over the input blit entries and update each IB2C object.
    for (num = 0; num < n_blits; num++) {
      GstVideoBlit *blit = &(blits[num]);

      GST_GLES_LOCK (convert);

      surface_id = gst_gles_retrieve_surface_id (convert, convert->insurfaces,
          "Input", blit->buffer, blit->info, GST_VCE_DATA_TYPE_U8);

      GST_GLES_UNLOCK (convert);

      if (!(success = (surface_id != 0))) {
        GST_ERROR ("Failed to get surface ID for input buffer %p at index %u "
            "in composition %u!", blit->buffer, num, idx);
        goto cleanup;
      }

      if (blit->buffer->pool == NULL) {
        GstMemory *memory = NULL;
        guint fd = 0;

        memory = gst_buffer_peek_memory (blit->buffer, 0);
        fd = gst_fd_memory_get_fd (memory);
        g_array_append_val (fds, fd);
      }

      ::ib2c::Object object;

      gst_gles_update_object (&object, surface_id, blit, composition);
      objects.push_back(object);
    }

    GST_GLES_LOCK (convert);

    surface_id = gst_gles_retrieve_surface_id (convert, convert->outsurfaces,
        "Output", outbuffer, composition->info, composition->datatype);
    GST_GLES_UNLOCK (convert);

    if (!(success = (surface_id != 0))) {
      GST_ERROR ("Failed to get surface ID for output buffer %p in "
          "composition %u!", outbuffer, idx);
      goto cleanup;
    }

    uint32_t color = composition->bgcolor;
    bool clear = composition->bgfill;

    std::vector<::ib2c::Normalize> normalization;

    normalization.push_back(::ib2c::Normalize (
        composition->scales[0], composition->offsets[0]));
    normalization.push_back(::ib2c::Normalize (
        composition->scales[1], composition->offsets[1]));
    normalization.push_back(::ib2c::Normalize (
        composition->scales[2], composition->offsets[2]));
    normalization.push_back(::ib2c::Normalize (
        composition->scales[3], composition->offsets[3]));

    comps.push_back(std::move(
        std::make_tuple(surface_id, color, clear, normalization, objects)));

    // TODO: Workaround. Remove once GLES supports these pixel types.
    normalize = (composition->datatype == GST_VCE_DATA_TYPE_U32) ||
        (composition->datatype == GST_VCE_DATA_TYPE_I32) ||
        (composition->datatype == GST_VCE_DATA_TYPE_U64) ||
        (composition->datatype == GST_VCE_DATA_TYPE_I64);

    normalize |= GST_GLES_IS_QC_VENDOR (convert->vendor) &&
        (composition->datatype == GST_VCE_DATA_TYPE_U16 ||
            composition->datatype == GST_VCE_DATA_TYPE_I16);

    normalize |= (GST_VIDEO_INFO_FORMAT (composition->info) == GST_VIDEO_FORMAT_GRAY8) &&
        (composition->datatype == GST_VCE_DATA_TYPE_F16 ||
            composition->datatype == GST_VCE_DATA_TYPE_F32);

    if (!normalize)
      continue;

    // CPU Normalization requests for unsupported formats and types.
    normrequest = &(g_array_index (normalizations, GstNormalizeRequest,
        n_normalizations++));

    normrequest->buffer = composition->buffer;
    normrequest->info = composition->info;
    normrequest->datatype = composition->datatype;

    memcpy (normrequest->offsets, composition->offsets, sizeof (normrequest->offsets));
    memcpy (normrequest->scales, composition->scales, sizeof (normrequest->scales));
  }

  // Resize the CPU normalization requests to the actual number.
  g_array_set_size (normalizations, n_normalizations);

  try {
    if (fence != NULL) {
      // Call IB2C Compose API with synchronous set to false.
      std::uintptr_t id = convert->engine->Compose (comps, false);
      *fence = reinterpret_cast<gpointer>(id);

      GST_GLES_LOCK (convert);

      convert->fences = g_list_append (convert->fences, *fence);

      g_hash_table_insert (convert->nocache, *fence, g_steal_pointer (&fds));
      g_hash_table_insert (convert->normrequests, *fence,
          g_steal_pointer (&normalizations));

      GST_GLES_UNLOCK (convert);
    } else {
      // Call IB2C Compose API with synchronous set to true.
      convert->engine->Compose (comps, true);

      GST_GLES_LOCK (convert);

      // Destroy the surfaces which doesn't need cache
      gst_gles_remove_input_surfaces (convert, fds);

      GST_GLES_UNLOCK (convert);

      for (idx = 0; idx < normalizations->len; idx++) {
        GstVideoFrame frame;

        normrequest = &(g_array_index (normalizations, GstNormalizeRequest, idx));

        success = gst_video_frame_map (&frame, normrequest->info,
            normrequest->buffer,
            (GstMapFlags)(GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF));

        if (!success) {
          GST_ERROR_OBJECT (convert, "Failed to map buffer!");
          continue;
        }

        success &= gst_video_frame_normalize_ip (&frame,
            normrequest->datatype, normrequest->offsets, normrequest->scales);

        gst_video_frame_unmap (&frame);
      }
    }
  } catch (std::exception& e) {
    GST_ERROR ("Failed to submit draw objects, error: '%s'!", e.what());
    success = FALSE;
  }

cleanup:
  if (fds != NULL)
    g_array_unref (fds);

  if (normalizations != NULL)
    g_array_unref (normalizations);

  return success;
}

gboolean
gst_gles_video_converter_wait_fence (GstGlesVideoConverter * convert,
    gpointer fence)
{
  GArray *fds = NULL, *normalizations = NULL;
  GstNormalizeRequest *normrequest = NULL;
  guint idx = 0;
  gboolean success = TRUE;

  try {
    convert->engine->Finish (reinterpret_cast<std::intptr_t>(fence));
  } catch (std::exception& e) {
    GST_ERROR ("Failed to process fence %p, error: '%s'!", fence, e.what());
    success = FALSE;
  }

  GST_GLES_LOCK (convert);
  convert->fences = g_list_remove (convert->fences, fence);

  // Destroy the surfaces which doesn't need cache
  fds = (GArray*) g_hash_table_lookup (convert->nocache, GUINT_TO_POINTER (fence));

  if (fds != NULL) {
    gst_gles_remove_input_surfaces (convert, fds);
    g_hash_table_remove (convert->nocache, GUINT_TO_POINTER (fence));
  }

  // Perform normalization if there are cached parameters for it.
  normalizations = (GArray*) g_hash_table_lookup (convert->normrequests, fence);

  // Increase the reference count and remove it from table before unlock.
  if (normalizations != NULL)
    normalizations = g_array_ref (normalizations);

  g_hash_table_remove (convert->normrequests, fence);

  GST_GLES_UNLOCK (convert);

  // Skip normalization logic if fence request did not finish properly.
  if (!success)
    goto cleanup;

  for (idx = 0; normalizations != NULL && idx < normalizations->len; idx++) {
    GstVideoFrame frame;

    normrequest = &(g_array_index (normalizations, GstNormalizeRequest, idx));

    success = gst_video_frame_map (&frame, normrequest->info, normrequest->buffer,
        (GstMapFlags)(GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF));

    if (!success) {
      GST_ERROR_OBJECT (convert, "Failed to map buffer!");
      continue;
    }

    success &= gst_video_frame_normalize_ip (&frame,
        normrequest->datatype, normrequest->offsets, normrequest->scales);

    gst_video_frame_unmap (&frame);
  }

cleanup:
  g_clear_pointer (&normalizations, (GDestroyNotify) g_array_unref);
  return success;
}

void
gst_gles_video_converter_flush (GstGlesVideoConverter * convert)
{
  GList *list = NULL;

  GST_GLES_LOCK (convert);

  GST_LOG ("Forcing pending requests to complete");

  for (list = convert->fences; list != NULL; list = list->next) {
    gpointer fence = list->data;

    try {
      convert->engine->Finish (reinterpret_cast<std::intptr_t>(fence));
    } catch (std::exception& e) {
      GST_ERROR ("Failed to process fence %p, error: '%s'!", fence, e.what());
    }
  }

  g_clear_pointer (&(convert->fences), (GDestroyNotify) g_list_free);

  GST_LOG ("Finished pending requests");

  g_hash_table_foreach (convert->insurfaces, gst_gles_destroy_surface, convert);
  g_hash_table_remove_all (convert->insurfaces);

  g_hash_table_foreach (convert->outsurfaces, gst_gles_destroy_surface, convert);
  g_hash_table_remove_all (convert->outsurfaces);

  g_hash_table_remove_all (convert->nocache);
  g_hash_table_remove_all (convert->normrequests);

  GST_GLES_UNLOCK (convert);
}

GstGlesVideoConverter *
gst_gles_video_converter_new (GstStructure * settings)
{
  GstGlesVideoConverter *convert = NULL;

  convert = g_slice_new0 (GstGlesVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  try {
    convert->engine = ::ib2c::NewGlEngine(&convert->vendor, &convert->renderer);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create and init new engine, error: '%s'!", e.what());
    goto cleanup;
  }

  if ((convert->insurfaces = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for source surfaces!");
    goto cleanup;
  }

  if ((convert->outsurfaces = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for target surfaces!");
    goto cleanup;
  }

  convert->nocache = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) g_array_unref);

  if (convert->nocache == NULL) {
    GST_ERROR ("Failed to create hash table for cache surfaces!");
    goto cleanup;
  }

  convert->normrequests = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) g_array_unref);

  if (convert->normrequests == NULL) {
    GST_ERROR ("Failed to create hash table for normalization requests!");
    goto cleanup;
  }

  GST_INFO ("Created GLES Converter %p - Vendor: %s, Renderer: %s", convert,
      convert->vendor, convert->renderer);
  return convert;

cleanup:
  gst_gles_video_converter_free (convert);
  return NULL;
}

void
gst_gles_video_converter_free (GstGlesVideoConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->fences != NULL)
    g_list_free (convert->fences);

  if (convert->insurfaces != NULL) {
    g_hash_table_foreach (convert->insurfaces, gst_gles_destroy_surface, convert);
    g_hash_table_destroy(convert->insurfaces);
  }

  if (convert->outsurfaces != NULL) {
    g_hash_table_foreach (convert->outsurfaces, gst_gles_destroy_surface, convert);
    g_hash_table_destroy (convert->outsurfaces);
  }

  if (convert->nocache != NULL)
    g_hash_table_destroy (convert->nocache);

  if (convert->normrequests != NULL)
    g_hash_table_destroy (convert->normrequests);

  if (convert->engine != NULL)
    delete convert->engine;

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed GLES converter: %p", convert);
  g_slice_free (GstGlesVideoConverter, convert);
}
