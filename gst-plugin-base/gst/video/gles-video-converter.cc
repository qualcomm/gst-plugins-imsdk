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

#define GPOINTER_TO_GUINT64(p) ((guint64) (p))
#define GUINT64_TO_POINTER(p)  ((gpointer) (p))

#define GST_GLES_GET_LOCK(obj) (&((GstGlesVideoConverter *)obj)->lock)
#define GST_GLES_LOCK(obj)     g_mutex_lock (GST_GLES_GET_LOCK(obj))
#define GST_GLES_UNLOCK(obj)   g_mutex_unlock (GST_GLES_GET_LOCK(obj))

#define GST_GLES_INPUT_QUARK   g_quark_from_static_string ("Input")

typedef struct _GstGlesSurface GstGlesSurface;

struct _GstGlesSurface
{
  // Surface ID.
  guint64 id;
  // Number of times that this surface was referenced.
  guint n_refs;
};

struct _GstGlesVideoConverter
{
  // Global mutex lock.
  GMutex          lock;

  // Map of buffer FDs and their corresponding GstGlesSurface.
  GHashTable      *insurfaces;
  GHashTable      *outsurfaces;

  // TODO: Update fence objects to store the FDs data to avoid below map
  // Map of request_id and the corresponding buffer FDs that doesn't need cache
  GHashTable      *nocache;

  // List of not yet processed IB2C fence objects.
  GList           *fences;

  // IB2C engine interface.
  ::ib2c::IEngine *engine;
};

static gint
gst_video_format_to_ib2c_format (GstVideoFormat format, const guint64 flags)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return ::ib2c::ColorFormat::kNV12;
    case GST_VIDEO_FORMAT_NV12_Q08C:
      return ::ib2c::ColorFormat::kNV12 | ::ib2c::ColorMode::kUBWC;
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
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kRGB888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kRGB161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kRGB323232F;

      // Default value.
      return ::ib2c::ColorFormat::kRGB888;
    case GST_VIDEO_FORMAT_BGR:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kBGR888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kBGR161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kBGR323232F;

      // Default value.
      return ::ib2c::ColorFormat::kBGR888;
    case GST_VIDEO_FORMAT_RGBA:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kRGBA8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kRGBA16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kRGBA32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kRGBA8888;
    case GST_VIDEO_FORMAT_BGRA:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kBGRA8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kBGRA16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kBGRA32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kBGRA8888;
    case GST_VIDEO_FORMAT_ARGB:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kARGB8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kARGB16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kARGB32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kARGB8888;
    case GST_VIDEO_FORMAT_ABGR:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kABGR8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kABGR16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kABGR32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kABGR8888;
    case GST_VIDEO_FORMAT_RGBx:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kRGBX8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kRGBX16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kRGBX32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kRGBX8888;
    case GST_VIDEO_FORMAT_BGRx:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kBGRX8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kBGRX16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kBGRX32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kBGRX8888;
    case GST_VIDEO_FORMAT_xRGB:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kXRGB8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kXRGB16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kXRGB32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kXRGB8888;
    case GST_VIDEO_FORMAT_xBGR:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kXBGR8888I;
      else if (flags == GST_VCE_FLAG_F16_FORMAT)
        return ::ib2c::ColorFormat::kXBGR16161616F;
      else if (flags == GST_VCE_FLAG_F32_FORMAT)
        return ::ib2c::ColorFormat::kXBGR32323232F;

      // Default value.
      return ::ib2c::ColorFormat::kXBGR8888;
    case GST_VIDEO_FORMAT_GRAY8:
      if (flags == GST_VCE_FLAG_I8_FORMAT)
        return ::ib2c::ColorFormat::kGRAY8I;

      // Default value.
      return ::ib2c::ColorFormat::kGRAY8;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }

  return 0;
}

static guint64
gst_gles_create_surface (GstGlesVideoConverter * convert, const gchar * direction,
    const GstVideoFrame * frame, const guint64 flags)
{
  GstMemory *memory = NULL;
  const gchar *format = NULL, *mode = "";
  ::ib2c::Surface surface;
  uint32_t type = 0;
  guint64 surface_id = 0;

  type |= (g_quark_from_static_string (direction) == GST_GLES_INPUT_QUARK) ?
      ::ib2c::SurfaceFlags::kInput : ::ib2c::SurfaceFlags::kOutput;

  memory = gst_buffer_peek_memory (frame->buffer, 0);

  if ((memory == NULL) || !gst_is_fd_memory (memory)) {
    GST_ERROR ("%s buffer memory is not FD backed!", direction);
    return 0;
  }

  format = gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame));

  surface.fd = gst_fd_memory_get_fd (memory);
  surface.width = GST_VIDEO_FRAME_WIDTH (frame);
  surface.height = GST_VIDEO_FRAME_HEIGHT (frame);
  surface.size = gst_buffer_get_size (frame->buffer);
  surface.nplanes = GST_VIDEO_FRAME_N_PLANES (frame);

  surface.format =
      gst_video_format_to_ib2c_format (GST_VIDEO_FRAME_FORMAT (frame), flags);

  if (flags == GST_VCE_FLAG_F16_FORMAT)
    mode = " FLOAT16";
  else if (flags == GST_VCE_FLAG_F32_FORMAT)
    mode = " FLOAT32";
  else if (flags == GST_VCE_FLAG_I8_FORMAT)
    mode = " SIGNED";

  GST_TRACE ("%s surface FD[%d] - Width[%u] Height[%u] Format[%s%s] Planes[%u]",
      direction, surface.fd, surface.width, surface.height, format, mode,
      surface.nplanes);

  surface.stride0 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  surface.offset0 = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  GST_TRACE ("%s surface FD[%d] - Stride0[%u] Offset0[%u]", direction,
      surface.fd, surface.stride0, surface.offset0);

  surface.stride1 = (surface.nplanes >= 2) ?
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1) : 0;
  surface.offset1 = (surface.nplanes >= 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) : 0;

  GST_TRACE ("%s surface FD[%d] - Stride1[%u] Offset1[%u]", direction,
      surface.fd, surface.stride1, surface.offset1);

  surface.stride2 = (surface.nplanes >= 3) ?
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2) : 0;
  surface.offset2 = (surface.nplanes >= 3) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 2) : 0;

  GST_TRACE ("%s surface FD[%d] - Stride2[%u] Offset2[%u]", direction,
      surface.fd, surface.stride2, surface.offset2);

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
    convert->engine->DestroySurface(glsurface->id);
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
gst_gles_free_cache (gpointer key, gpointer value, gpointer userdata)
{
  GArray *fds = (GArray *) (value);

  g_array_free(fds, TRUE);
}

static void
gst_gles_update_object (::ib2c::Object * object, const guint64 surface_id,
    const GstVideoBlit * vblit, const GstVideoFrame * outframe)
{
  gint x = 0, y = 0, width = 0, height = 0;

  object->id = surface_id;
  object->mask = 0;

  object->alpha = vblit->alpha;
  GST_TRACE ("Input surface %lx - Global alpha: %u", surface_id, object->alpha);

  // Setup the source rectangle.
  if ((vblit->source.w != 0) && (vblit->source.h != 0)) {
    x = vblit->source.x;
    y = vblit->source.y;
    width = vblit->source.w;
    height = vblit->source.h;
  }

  width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (vblit->frame) :
      MIN (width, GST_VIDEO_FRAME_WIDTH (vblit->frame) - x);
  height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (vblit->frame) :
      MIN (height, GST_VIDEO_FRAME_HEIGHT (vblit->frame) - y);

  object->source.x = x;
  object->source.y = y;
  object->source.w = width;
  object->source.h = height;

  if ((vblit->flip == GST_VCE_FLIP_VERTICAL) ||
      (vblit->flip == GST_VCE_FLIP_BOTH)) {
    object->mask |= ::ib2c::ConfigMask::kVFlip;
    GST_TRACE ("Input surface %lx - Flip Vertically", surface_id);
  }

  if ((vblit->flip == GST_VCE_FLIP_HORIZONTAL) ||
      (vblit->flip == GST_VCE_FLIP_BOTH)) {
    object->mask |= ::ib2c::ConfigMask::kHFlip;
    GST_TRACE ("Input surface %lx - Flip Horizontally", surface_id);
  }

  // Reset the local dimension variables.
  x = y = width = height = 0;

  // Setup the target rectangle.
  if ((vblit->destination.w != 0) && (vblit->destination.h != 0)) {
    x = vblit->destination.x;
    y = vblit->destination.y;
    width = vblit->destination.w;
    height = vblit->destination.h;
  }

  object->destination.x = ((width != 0) && (height != 0)) ? x : 0;
  object->destination.y = ((width != 0) && (height != 0)) ? y : 0;

  // Setup rotation angle and adjustments.
  switch (vblit->rotate) {
    case GST_VCE_ROTATE_90:
    {
      gint dar_n = 0, dar_d = 0;

      gst_util_fraction_multiply (
          GST_VIDEO_FRAME_WIDTH (vblit->frame),
          GST_VIDEO_FRAME_HEIGHT (vblit->frame),
          GST_VIDEO_INFO_PAR_N (&(vblit->frame)->info),
          GST_VIDEO_INFO_PAR_D (&(vblit->frame)->info),
          &dar_n, &dar_d
      );

      GST_TRACE ("Input surface %lx - rotate 90° clockwise", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width != 0) ? width :
          GST_VIDEO_FRAME_HEIGHT (outframe) * dar_d / dar_n;
      height = (height != 0) ? height : GST_VIDEO_FRAME_HEIGHT (outframe);

      // Align to multiple of 4 due to hardware requirements.
      width = ((width % 4) >= 2) ? GST_ROUND_UP_4 (width) :
          GST_ROUND_DOWN_4 (width);

      object->destination.w = width;
      object->destination.h = height;

      x = ((vblit->destination.w != 0) && (vblit->destination.h != 0)) ?
          x : (GST_VIDEO_FRAME_WIDTH (outframe) - width) / 2;

      // Adjust the target rectangle coordinates.
      object->destination.x = GST_VIDEO_FRAME_WIDTH (outframe) - (x + width);
      object->destination.y = y;

      object->rotation = 90.0;
      break;
    }
    case GST_VCE_ROTATE_180:
      GST_TRACE ("Input surface %lx - rotate 180°", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (outframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (outframe) : height;

      object->destination.w = width;
      object->destination.h = height;

      object->rotation = 180.0;
      break;
    case GST_VCE_ROTATE_270:
    {
      gint dar_n = 0, dar_d = 0;

      gst_util_fraction_multiply (
          GST_VIDEO_FRAME_WIDTH (vblit->frame),
          GST_VIDEO_FRAME_HEIGHT (vblit->frame),
          GST_VIDEO_INFO_PAR_N (&(vblit->frame)->info),
          GST_VIDEO_INFO_PAR_D (&(vblit->frame)->info),
          &dar_n, &dar_d
      );

      GST_TRACE ("Input surface %lx - rotate 90° counter-clockwise", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width != 0) ? width :
          GST_VIDEO_FRAME_HEIGHT (outframe) * dar_d / dar_n;
      height = (height != 0) ? height : GST_VIDEO_FRAME_HEIGHT (outframe);

      // Align to multiple of 4 due to hardware requirements.
      width = ((width % 4) >= 2) ? GST_ROUND_UP_4 (width) :
          GST_ROUND_DOWN_4 (width);

      object->destination.w = width;
      object->destination.h = height;

      x = ((vblit->destination.w != 0) && (vblit->destination.h != 0)) ?
          x : (GST_VIDEO_FRAME_WIDTH (outframe) - width) / 2;

      // Adjust the target rectangle coordinates.
      object->destination.x = GST_VIDEO_FRAME_WIDTH (outframe) - (x + width);
      object->destination.y = y;

      object->rotation = 270.0;
      break;
    }
    default:
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (outframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (outframe) : height;

      object->destination.w = width;
      object->destination.h = height;

      object->rotation = 0.0;
      break;
  }

  GST_TRACE ("Input surface %lx - Source rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->source.x, object->source.y,
      object->source.w, object->source.h);

  GST_TRACE ("Input surface %lx - Target rectangle: x(%d) y(%d) w(%d) h(%d)",
      surface_id, object->destination.x, object->destination.y,
      object->destination.w, object->destination.h);
}

static guint64
gst_gles_retrieve_surface_id (GstGlesVideoConverter * convert,
    GHashTable * surfaces, const gchar * direction,
    const GstVideoFrame * vframe, const guint64 flags)
{
  GstMemory *memory = NULL;
  GstGlesSurface *glsurface = NULL;
  guint fd = 0;
  guint64 surface_id = 0;

  // Get the 1st (and only) memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (vframe->buffer, 0);

  if ((memory == NULL) || !gst_is_fd_memory (memory)) {
    GST_ERROR ("Buffer %p does not have FD memory!", vframe->buffer);
    return 0;
  }

  // Get the input buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (surfaces, GUINT_TO_POINTER (fd))) {
    // Create an input surface and add its ID to the input hash table.
    surface_id =
        gst_gles_create_surface (convert, direction, vframe, flags);

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
  guint idx = 0, num = 0, n_blits = 0;
  guint64 surface_id = 0;
  GArray *fds = NULL;

  std::vector<::ib2c::Composition> comps;

  fds = g_array_new (FALSE, FALSE, sizeof (guint));

  g_return_val_if_fail (fds != NULL, FALSE);

  for (idx = 0; idx < n_compositions; idx++) {
    GstVideoFrame *outframe = compositions[idx].frame;
    GstVideoBlit *blits = compositions[idx].blits;

    n_blits = compositions[idx].n_blits;

    // Sanity checks, output frame and blit entries must not be NULL.
    g_return_val_if_fail (outframe != NULL, FALSE);
    g_return_val_if_fail ((blits != NULL) && (n_blits != 0), FALSE);

    std::vector<::ib2c::Object> objects;

    // Iterate over the input blit entries and update each IB2C object.
    for (num = 0; num < n_blits; num++) {
      GstVideoBlit *blit = &(blits[num]);

      GST_GLES_LOCK (convert);

      surface_id = gst_gles_retrieve_surface_id (convert, convert->insurfaces,
          "Input", blit->frame, 0);

      GST_GLES_UNLOCK (convert);

      if (surface_id == 0) {
        GST_ERROR ("Failed to get surface ID for input buffer %p at index %u "
            "in composition %u!", blit->frame->buffer, num, idx);
        return FALSE;
      }

      if (blit->frame->buffer->pool == NULL) {
        GstMemory *memory = NULL;
        guint fd = 0;

        memory = gst_buffer_peek_memory (blit->frame->buffer, 0);
        fd = gst_fd_memory_get_fd (memory);
        g_array_append_val (fds, fd);
      }

      ::ib2c::Object object;

      gst_gles_update_object (&object, surface_id, blit, outframe);
      objects.push_back(object);
    }

    GST_GLES_LOCK (convert);

    surface_id = gst_gles_retrieve_surface_id (convert, convert->outsurfaces,
        "Output", outframe, compositions[idx].flags);

    GST_GLES_UNLOCK (convert);

    if (surface_id == 0) {
      GST_ERROR ("Failed to get surface ID for output buffer %p in "
          "composition %u!", outframe->buffer, idx);
      return FALSE;
    }

    uint32_t color = compositions[idx].bgcolor;
    bool clear = compositions[idx].bgfill;

    std::vector<::ib2c::Normalize> normalization;

    normalization.push_back(::ib2c::Normalize (
        compositions[idx].scales[0], compositions[idx].offsets[0]));
    normalization.push_back(::ib2c::Normalize (
        compositions[idx].scales[1], compositions[idx].offsets[1]));
    normalization.push_back(::ib2c::Normalize (
        compositions[idx].scales[2], compositions[idx].offsets[2]));
    normalization.push_back(::ib2c::Normalize (
        compositions[idx].scales[3], compositions[idx].offsets[3]));

    comps.push_back(std::move(
        std::make_tuple(surface_id, color, clear, normalization, objects)));
  }

  try {
    if (fence != NULL) {
      // Call IB2C Compose API with synchronous set to false.
      std::uintptr_t id = convert->engine->Compose (comps, false);
      *fence = reinterpret_cast<gpointer>(id);

      GST_GLES_LOCK (convert);
      convert->fences = g_list_append (convert->fences, *fence);
      g_hash_table_insert (convert->nocache, GUINT_TO_POINTER (*fence), fds);
      GST_GLES_UNLOCK (convert);
    } else {
      // Call IB2C Compose API with synchronous set to true.
      convert->engine->Compose (comps, true);

      GST_GLES_LOCK (convert);

      // Destroy the surfaces which doesn't need cache
      gst_gles_remove_input_surfaces (convert, fds);
      g_array_free (fds, TRUE);

      GST_GLES_UNLOCK (convert);
    }
  } catch (std::exception& e) {
    GST_ERROR ("Failed to submit draw objects, error: '%s'!", e.what());
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_gles_video_converter_wait_fence (GstGlesVideoConverter * convert,
    gpointer fence)
{
  GArray *fds = NULL;
  gboolean success = FALSE;

  try {
    convert->engine->Finish (reinterpret_cast<std::intptr_t>(fence));
  } catch (std::exception& e) {
    GST_ERROR ("Failed to process fence %p, error: '%s'!", fence, e.what());
    return FALSE;
  }

  GST_GLES_LOCK (convert);
  convert->fences = g_list_remove (convert->fences, fence);

  success = g_hash_table_lookup_extended (convert->nocache,
      GUINT_TO_POINTER (fence), NULL, (gpointer *) &fds);
  if (success) {
    // Destroy the surfaces which doesn't need cache
    gst_gles_remove_input_surfaces (convert, fds);
    g_array_free (fds, TRUE);
    g_hash_table_remove (convert->nocache, GUINT_TO_POINTER (fence));
  }

  GST_GLES_UNLOCK (convert);

  return TRUE;
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

  if (convert->insurfaces != NULL) {
    g_hash_table_foreach (convert->insurfaces, gst_gles_destroy_surface, convert);
    g_hash_table_remove_all (convert->insurfaces);
  }

  if (convert->outsurfaces != NULL) {
    g_hash_table_foreach (convert->outsurfaces, gst_gles_destroy_surface, convert);
    g_hash_table_remove_all (convert->outsurfaces);
  }

  if (convert->nocache != NULL) {
    g_hash_table_foreach (convert->nocache, gst_gles_free_cache, NULL);
    g_hash_table_remove_all (convert->nocache);
  }

  GST_GLES_UNLOCK (convert);

  return;
}

GstGlesVideoConverter *
gst_gles_video_converter_new (GstStructure * settings)
{
  GstGlesVideoConverter *convert = NULL;

  convert = g_slice_new0 (GstGlesVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  try {
    convert->engine = ::ib2c::NewGlEngine();
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

  if ((convert->nocache = g_hash_table_new (NULL, NULL)) == NULL) {
    GST_ERROR ("Failed to create hash table for cache surfaces!");
    goto cleanup;
  }

  GST_INFO ("Created GLES Converter %p", convert);
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

  if (convert->nocache != NULL) {
    g_hash_table_foreach (convert->nocache, gst_gles_free_cache, NULL);
    g_hash_table_destroy (convert->nocache);
  }

  if (convert->engine != NULL)
    delete convert->engine;

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed GLES converter: %p", convert);
  g_slice_free (GstGlesVideoConverter, convert);
}
