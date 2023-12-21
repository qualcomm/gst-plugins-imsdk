/*
* Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#include "gles-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>
#include <cstdint>
#include <cmath>

#include <iot-core-algs/ib2c.h>


#define GST_CAT_DEFAULT gst_video_converter_engine_debug

#define GPOINTER_TO_GUINT64(p) ((guint64) (p))
#define GUINT64_TO_POINTER(p)  ((gpointer) (p))

#define GST_GLES_GET_LOCK(obj) (&((GstGlesVideoConverter *)obj)->lock)
#define GST_GLES_LOCK(obj)     g_mutex_lock (GST_GLES_GET_LOCK(obj))
#define GST_GLES_UNLOCK(obj)   g_mutex_unlock (GST_GLES_GET_LOCK(obj))

#define GST_GLES_INPUT_QUARK   g_quark_from_static_string ("Input")

struct _GstGlesVideoConverter
{
  // Global mutex lock.
  GMutex          lock;

  // Map of buffer FDs and their corresponding GLES surface ID.
  GHashTable      *insurfaces;
  GHashTable      *outsurfaces;

  // List of not yet processed IB2C fence objects.
  GList           *fences;

  // IB2C library handle.
  gpointer        ib2chandle;

  // IB2C engine interface.
  ::ib2c::IEngine *engine;
};

static gint
gst_video_format_to_ib2c_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return ::ib2c::ColorFormat::kNV12;
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
    case GST_VIDEO_FORMAT_RGB16:
      return ::ib2c::ColorFormat::kRGB565;
    case GST_VIDEO_FORMAT_BGR16:
      return ::ib2c::ColorFormat::kBGR565;
    case GST_VIDEO_FORMAT_RGB:
      return ::ib2c::ColorFormat::kRGB888;
    case GST_VIDEO_FORMAT_BGR:
      return ::ib2c::ColorFormat::kBGR888;
    case GST_VIDEO_FORMAT_RGBA:
      return ::ib2c::ColorFormat::kRGBA8888;
    case GST_VIDEO_FORMAT_BGRA:
      return ::ib2c::ColorFormat::kBGRA8888;
    case GST_VIDEO_FORMAT_ARGB:
      return ::ib2c::ColorFormat::kARGB8888;
    case GST_VIDEO_FORMAT_ABGR:
      return ::ib2c::ColorFormat::kABGR8888;
    case GST_VIDEO_FORMAT_RGBx:
      return ::ib2c::ColorFormat::kRGBX8888;
    case GST_VIDEO_FORMAT_BGRx:
      return ::ib2c::ColorFormat::kBGRX8888;
    case GST_VIDEO_FORMAT_xRGB:
      return ::ib2c::ColorFormat::kXRGB8888;
    case GST_VIDEO_FORMAT_xBGR:
      return ::ib2c::ColorFormat::kXBGR8888;
    case GST_VIDEO_FORMAT_GRAY8:
      return ::ib2c::ColorFormat::kGRAY8;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }

  return 0;
}

static guint64
gst_gles_create_surface (GstGlesVideoConverter * convert, const gchar * direction,
    const GstVideoFrame * frame, const gboolean isubwc, const guint64 flags)
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
  surface.format = gst_video_format_to_ib2c_format (GST_VIDEO_FRAME_FORMAT (frame));
  surface.width = GST_VIDEO_FRAME_WIDTH (frame);
  surface.height = GST_VIDEO_FRAME_HEIGHT (frame);
  surface.size = gst_buffer_get_size (frame->buffer);
  surface.nplanes = GST_VIDEO_FRAME_N_PLANES (frame);

  // In case the format has UBWC enabled append additional format mask.
  if (isubwc) {
    surface.format |= ::ib2c::ColorMode::kUBWC;
    mode = " UBWC";
  } else if (flags == GST_VCE_FLAG_F16_FORMAT) {
    surface.format |= ::ib2c::ColorMode::kFloat16;
    mode = " FLOAT16";
  } else if (flags == GST_VCE_FLAG_F32_FORMAT) {
    surface.format |= ::ib2c::ColorMode::kFloat32;
    mode = " FLOAT32";
  }

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
  guint64 surface_id = GPOINTER_TO_GUINT64 (value);

  try {
    convert->engine->DestroySurface(surface_id);
    GST_DEBUG ("Destroying surface with id %lx", surface_id);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to destroy IB2C surface, error: '%s'!", e.what());
    return;
  }

  return;
}

static void
gst_gles_update_object (::ib2c::Object * object, const guint64 surface_id,
    const GstVideoFrame * inframe, const guint8 alpha,
    const GstVideoConvFlip flip, const GstVideoConvRotate rotate,
    const GstVideoRectangle * source, const GstVideoRectangle * destination,
    const GstVideoFrame * outframe)
{
  gint x = 0, y = 0, width = 0, height = 0;

  object->id = surface_id;
  object->mask = 0;

  object->alpha = alpha;
  GST_TRACE ("Input surface %lx - Global alpha: %u", surface_id, object->alpha);

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

  object->source.x = x;
  object->source.y = y;
  object->source.w = width;
  object->source.h = height;

  if ((flip == GST_VCE_FLIP_VERTICAL) || (flip == GST_VCE_FLIP_BOTH)) {
    object->mask |= ::ib2c::ConfigMask::kVFlip;
    GST_TRACE ("Input surface %lx - Flip Vertically", surface_id);
  }

  if ((flip == GST_VCE_FLIP_HORIZONTAL) || (flip == GST_VCE_FLIP_BOTH)) {
    object->mask |= ::ib2c::ConfigMask::kHFlip;
    GST_TRACE ("Input surface %lx - Flip Horizontally", surface_id);
  }

  // Setup the target rectangle.
  if (destination != NULL) {
    x = destination->x;
    y = destination->y;
    width = destination->w;
    height = destination->h;
  }

  object->destination.x = ((width != 0) && (height != 0)) ? x : 0;
  object->destination.y = ((width != 0) && (height != 0)) ? y : 0;

  // Setup rotation angle and adjustments.
  switch (rotate) {
    case GST_VCE_ROTATE_90:
    {
      gint dar_n = 0, dar_d = 0;

      gst_util_fraction_multiply (
          GST_VIDEO_FRAME_WIDTH (inframe), GST_VIDEO_FRAME_HEIGHT (inframe),
          GST_VIDEO_INFO_PAR_N (&(inframe)->info),
          GST_VIDEO_INFO_PAR_D (&(inframe)->info),
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

      x = (destination != NULL) ?
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
          GST_VIDEO_FRAME_WIDTH (inframe), GST_VIDEO_FRAME_HEIGHT (inframe),
          GST_VIDEO_INFO_PAR_N (&(inframe)->info),
          GST_VIDEO_INFO_PAR_D (&(inframe)->info),
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

      x = (destination != NULL) ?
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
    const GstVideoFrame * vframe, const gboolean isubwc, const guint64 flags)
{
  GstMemory *memory = NULL;
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
        gst_gles_create_surface (convert, direction, vframe, isubwc, flags);

    if (surface_id == 0) {
      GST_ERROR ("Failed to create surface!");
      return 0;
    }

    g_hash_table_insert (surfaces, GUINT_TO_POINTER (fd),
        GUINT64_TO_POINTER (surface_id));
  } else {
    // Get the input surface ID from the input hash table.
    surface_id = GPOINTER_TO_GUINT64 (
        g_hash_table_lookup (surfaces, GUINT_TO_POINTER (fd)));
  }

  return surface_id;
}

gboolean
gst_gles_video_converter_compose (GstGlesVideoConverter * convert,
    GstVideoComposition * compositions, guint n_compositions, gpointer * fence)
{
  guint idx = 0, num = 0, n_blits = 0;
  guint64 surface_id = 0;

  std::vector<::ib2c::Composition> comps;

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
      GstVideoRectangle *source = NULL, *destination = NULL;
      guint r_idx = 0;

      GST_GLES_LOCK (convert);

      surface_id = gst_gles_retrieve_surface_id (convert, convert->insurfaces,
          "Input", blit->frame, blit->isubwc, 0);

      GST_GLES_UNLOCK (convert);

      if (surface_id == 0) {
        GST_ERROR ("Failed to get surface ID for input buffer %p at index %u "
            "in composition %u!", blit->frame->buffer, num, idx);
        return FALSE;
      }

      // Update a new C2D object (at least 1) for each source/destnation pair.
      do {
        ::ib2c::Object object;

        source = (blit->n_regions != 0) ? &(blit->sources[r_idx]) : NULL;
        destination = (blit->n_regions != 0) ? &(blit->destinations[r_idx]) : NULL;

        gst_gles_update_object (&object, surface_id, blit->frame, blit->alpha,
            blit->flip, blit->rotate, source, destination, outframe);

        objects.push_back(object);
      } while (++r_idx < blit->n_regions);
    }

    GST_GLES_LOCK (convert);

    surface_id = gst_gles_retrieve_surface_id (convert, convert->outsurfaces,
        "Output", outframe, compositions[idx].isubwc, compositions[idx].flags);

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
      GST_GLES_UNLOCK (convert);
    } else {
      // Call IB2C Compose API with synchronous set to true.
      convert->engine->Compose (comps, true);
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
  try {
    convert->engine->Finish (reinterpret_cast<std::intptr_t>(fence));
  } catch (std::exception& e) {
    GST_ERROR ("Failed to process fence %p, error: '%s'!", fence, e.what());
    return FALSE;
  }

  GST_GLES_LOCK (convert);
  convert->fences = g_list_remove (convert->fences, fence);
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

  GST_GLES_UNLOCK (convert);

  return;
}

GstGlesVideoConverter *
gst_gles_video_converter_new (GstStructure * settings)
{
  GstGlesVideoConverter *convert = NULL;
  ::ib2c::NewIEngine NewEngine;

  convert = g_slice_new0 (GstGlesVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  if ((convert->ib2chandle = dlopen ("libIB2C.so", RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open IB2C library, error: %s!", dlerror());
    goto cleanup;
  }

  NewEngine = (::ib2c::NewIEngine) dlsym(convert->ib2chandle, IB2C_ENGINE_NEW_FUNC);
  if (NewEngine == NULL) {
    GST_ERROR ("Failed to load IB2C symbol, error: %s!", dlerror());
    goto cleanup;
  }

  try {
    convert->engine = NewEngine();
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

  if (convert->engine != NULL)
    delete convert->engine;

  if (convert->ib2chandle != NULL)
    dlclose (convert->ib2chandle);

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed GLES converter: %p", convert);
  g_slice_free (GstGlesVideoConverter, convert);
}
