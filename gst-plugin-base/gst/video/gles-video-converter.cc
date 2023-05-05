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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gles-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>
#include <cstdint>
#include <cmath>

#include <iot-core-algs/ib2c.h>

#define GST_GLES_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define GPOINTER_TO_ULONG(p)         ((gulong) (p))

#define DEFAULT_OPT_FLIP_HORIZONTAL  FALSE
#define DEFAULT_OPT_FLIP_VERTICAL    FALSE
#define DEFAULT_OPT_ALPHA            1.0
#define DEFAULT_OPT_ROTATION         GST_GLES_VIDEO_ROTATE_NONE
#define DEFAULT_OPT_BACKGROUND       0x00000000
#define DEFAULT_OPT_CLEAR            TRUE
#define DEFAULT_OPT_RSCALE           1.0
#define DEFAULT_OPT_GSCALE           1.0
#define DEFAULT_OPT_BSCALE           1.0
#define DEFAULT_OPT_ASCALE           1.0
#define DEFAULT_OPT_ROFFSET          0.0
#define DEFAULT_OPT_GOFFSET          0.0
#define DEFAULT_OPT_BOFFSET          0.0
#define DEFAULT_OPT_AOFFSET          0.0
#define DEFAULT_OPT_FLOAT16_FORMAT   FALSE
#define DEFAULT_OPT_FLOAT32_FORMAT   FALSE
#define DEFAULT_OPT_UBWC_FORMAT      FALSE

#define GET_OPT_FLIP_HORIZONTAL(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL, DEFAULT_OPT_FLIP_HORIZONTAL)
#define GET_OPT_FLIP_VERTICAL(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_FLIP_VERTICAL, DEFAULT_OPT_FLIP_VERTICAL)
#define GET_OPT_ALPHA(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ALPHA, DEFAULT_OPT_ALPHA)
#define GET_OPT_ROTATION(s) get_opt_enum(s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ROTATION, GST_TYPE_GLES_VIDEO_ROTATION, \
    DEFAULT_OPT_ROTATION)
#define GET_OPT_BACKGROUND(s) get_opt_uint (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BACKGROUND, DEFAULT_OPT_BACKGROUND)
#define GET_OPT_CLEAR(s) get_opt_boolean(s, \
    GST_GLES_VIDEO_CONVERTER_OPT_CLEAR, DEFAULT_OPT_CLEAR)
#define GET_OPT_RSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_RSCALE, DEFAULT_OPT_RSCALE)
#define GET_OPT_GSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_GSCALE, DEFAULT_OPT_GSCALE)
#define GET_OPT_BSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BSCALE, DEFAULT_OPT_BSCALE)
#define GET_OPT_ASCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ASCALE, DEFAULT_OPT_ASCALE)
#define GET_OPT_ROFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET, DEFAULT_OPT_ROFFSET)
#define GET_OPT_GOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET, DEFAULT_OPT_GOFFSET)
#define GET_OPT_BOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET, DEFAULT_OPT_BOFFSET)
#define GET_OPT_AOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET, DEFAULT_OPT_AOFFSET)
#define GET_OPT_FLOAT16_FORMAT(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_FLOAT16_FORMAT, DEFAULT_OPT_FLOAT16_FORMAT)
#define GET_OPT_FLOAT32_FORMAT(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_FLOAT32_FORMAT, DEFAULT_OPT_FLOAT32_FORMAT)
#define GET_OPT_UBWC_FORMAT(s) get_opt_boolean(s, \
    GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT, DEFAULT_OPT_UBWC_FORMAT)

#define GST_GLES_GET_LOCK(obj) (&((GstGlesVideoConverter *)obj)->lock)
#define GST_GLES_LOCK(obj)     g_mutex_lock (GST_GLES_GET_LOCK(obj))
#define GST_GLES_UNLOCK(obj)   g_mutex_unlock (GST_GLES_GET_LOCK(obj))

#define GST_CAT_DEFAULT ensure_debug_category()

struct _GstGlesVideoConverter
{
  // Global mutex lock.
  GMutex          lock;

  // List of surface options for each input frame.
  GList           *inopts;
  // List of options performed for each output frame.
  GList           *outopts;

  // Map of buffer FDs and their corresponding GLES surface ID.
  GHashTable      *insurfaces;
  GHashTable      *outsurfaces;

  // IB2C library handle.
  gpointer        ib2chandle;

  // DataConverter to construct the converter pipeline
  //::QImgConv::DataConverter *engine;
  ::ib2c::IEngine *engine;
};

enum
{
  GST_GLES_INPUT,
  GST_GLES_OUTPUT,
};

enum
{
  GST_GLES_UBWC_FORMAT_FLAG    = (1 << 0),
  GST_GLES_FLOAT16_FORMAT_FLAG = (1 << 1),
  GST_GLES_FLOAT32_FORMAT_FLAG = (1 << 2),
};

/// Mutex for protecting the static reference counter.
G_LOCK_DEFINE_STATIC (gles);

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("gles-video-converter",
        0, "GLES video converter");
    g_once_init_leave (&catonce, catdone);
  }

  return (GstDebugCategory *) catonce;
}

static gdouble
get_opt_double (const GstStructure * options, const gchar * opt, gdouble value)
{
  gdouble result;
  return gst_structure_get_double (options, opt, &result) ? result : value;
}

static gint
get_opt_uint (const GstStructure * options, const gchar * opt, guint value)
{
  guint result;
  return gst_structure_get_uint (options, opt, &result) ? result : value;
}

static gboolean
get_opt_boolean (const GstStructure * options, const gchar * opt, gboolean value)
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

GType
gst_gles_video_rotation_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_GLES_VIDEO_ROTATE_NONE,
      "No rotation", "none"
    },
    { GST_GLES_VIDEO_ROTATE_90_CW,
      "Rotate 90 degrees clockwise", "90CW"
    },
    { GST_GLES_VIDEO_ROTATE_90_CCW,
      "Rotate 90 degrees counter-clockwise", "90CCW"
    },
    { GST_GLES_VIDEO_ROTATE_180,
      "Rotate 180 degrees", "180"
    },
    { 0, NULL, NULL },
  };

  G_LOCK (gles);

  if (!gtype)
    gtype = g_enum_register_static ("GstGlesVideoRotation", variants);

  G_UNLOCK (gles);

  return gtype;
}

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
gst_create_surface (GstGlesVideoConverter * convert, const guint direction,
    const GstVideoFrame * frame, guint bits)
{
  GstMemory *memory = NULL;
  const gchar *name = NULL, *format = NULL, *mode = "";
  ::ib2c::Surface surface;
  uint32_t flags = 0;
  guint64 surface_id = 0;

  if (direction == GST_GLES_INPUT) {
    name = "Input";
    flags |= ::ib2c::SurfaceFlags::kInput;
  } else {
    name = "Output";
    flags |= ::ib2c::SurfaceFlags::kOutput;
  }

  format = gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame));

  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_GLES_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), FALSE,
      "%s buffer memory is not FD backed!", name);

  surface.fd = gst_fd_memory_get_fd (memory);
  surface.format = gst_video_format_to_ib2c_format (GST_VIDEO_FRAME_FORMAT (frame));
  surface.width = GST_VIDEO_FRAME_WIDTH (frame);
  surface.height = GST_VIDEO_FRAME_HEIGHT (frame);
  surface.size = gst_buffer_get_size (frame->buffer);
  surface.nplanes = GST_VIDEO_FRAME_N_PLANES (frame);

  // In case the format has UBWC enabled append additional format mask.
  if (bits & GST_GLES_UBWC_FORMAT_FLAG) {
    surface.format |= ::ib2c::ColorMode::kUBWC;
    mode = " UBWC";
  } else if (bits & GST_GLES_FLOAT16_FORMAT_FLAG) {
    surface.format |= ::ib2c::ColorMode::kFloat16;
    mode = " FLOAT16";
  } else if (bits & GST_GLES_FLOAT32_FORMAT_FLAG) {
    surface.format |= ::ib2c::ColorMode::kFloat32;
    mode = " FLOAT32";
  }

  GST_TRACE ("%s surface FD[%d] - Width[%u] Height[%u] Format[%s%s] Planes[%u]",
      name, surface.fd, surface.width, surface.height, format, mode,
      surface.nplanes);

  surface.stride0 = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  surface.offset0 = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  GST_TRACE ("%s surface FD[%d] - Stride0[%u] Offset0[%u]", name, surface.fd,
      surface.stride0, surface.offset0);

  surface.stride1 = (surface.nplanes >= 2) ?
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1) : 0;
  surface.offset1 = (surface.nplanes >= 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) : 0;

  GST_TRACE ("%s surface FD[%d] - Stride1[%u] Offset1[%u]", name,
      surface.fd, surface.stride1, surface.offset1);

  surface.stride2 = (surface.nplanes >= 3) ?
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2) : 0;
  surface.offset2 = (surface.nplanes >= 3) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 2) : 0;

  GST_TRACE ("%s surface FD[%d] - Stride2[%u] Offset2[%u]", name,
      surface.fd, surface.stride2, surface.offset2);

  try {
    surface_id = convert->engine->CreateSurface (surface, flags);
    GST_DEBUG ("Created %s surface with id %lx", name, surface_id);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create %s surface, error: '%s'!", name, e.what());
    return 0;
  }

  return surface_id;
}

static void
gst_destroy_surface (gpointer key, gpointer value, gpointer userdata)
{
  GstGlesVideoConverter *convert = (GstGlesVideoConverter*) userdata;
  guint64 surface_id = GPOINTER_TO_ULONG (value);

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
gst_extract_rectangles (const GstStructure * opts,
    std::vector<::ib2c::Region>& srcrects, std::vector<::ib2c::Region>& dstrects,
    guint& n_rects)
{
  const GValue *srclist = NULL, *dstlist = NULL, *entry = NULL;
  guint idx = 0, n_srcrects = 0, n_dstrects = 0;

  srclist = gst_structure_get_value (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES);
  dstlist = gst_structure_get_value (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES);

  // Make sure that there is at least one new rectangle in the lists.
  n_srcrects = (srclist == NULL) ? 1 : gst_value_array_get_size (srclist);
  n_dstrects = (dstlist == NULL) ? 1 : gst_value_array_get_size (dstlist);

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

  n_srcrects = (srclist == NULL) ? 0 : gst_value_array_get_size (srclist);
  n_dstrects = (dstlist == NULL) ? 0 : gst_value_array_get_size (dstlist);

  for (idx = 0; idx < n_rects; idx++) {
    ::ib2c::Region rectangle;

    entry = (n_srcrects != 0) ? gst_value_array_get_value (srclist, idx) : NULL;

    if ((entry != NULL) && gst_value_array_get_size (entry) == 4) {
      rectangle.x = g_value_get_int (gst_value_array_get_value (entry, 0));
      rectangle.y = g_value_get_int (gst_value_array_get_value (entry, 1));
      rectangle.w = g_value_get_int (gst_value_array_get_value (entry, 2));
      rectangle.h = g_value_get_int (gst_value_array_get_value (entry, 3));
    } else if (entry != NULL) {
      GST_WARNING ("Source rectangle at index %u does not contain "
          "exactly 4 values, using default values!", idx);
    }

    srcrects.push_back(rectangle);

    entry = (n_dstrects != 0) ? gst_value_array_get_value (dstlist, idx) : NULL;

    if ((entry != NULL) && gst_value_array_get_size (entry) == 4) {
      rectangle.x = g_value_get_int (gst_value_array_get_value (entry, 0));
      rectangle.y = g_value_get_int (gst_value_array_get_value (entry, 1));
      rectangle.w = g_value_get_int (gst_value_array_get_value (entry, 2));
      rectangle.h = g_value_get_int (gst_value_array_get_value (entry, 3));
    } else if (entry != NULL) {
      GST_WARNING ("Destination rectangle at index %u does not contain "
          "exactly 4 values, using default values!", idx);
    }

    dstrects.push_back(rectangle);
  }
}

static void
gst_update_object (::ib2c::Object * object, guint64 surface_id, GstStructure * opts,
    const GstVideoFrame * inframe, const ::ib2c::Region * srcrect,
    const GstVideoFrame * outframe, const ::ib2c::Region * dstrect)
{
  gint x = 0, y = 0, width = 0, height = 0;

  object->id = surface_id;
  object->mask = 0;

  // Transform alpha from double (0.0 - 1.0) to integer (0 - 255).
  object->alpha = G_MAXUINT8 * GET_OPT_ALPHA (opts);
  GST_TRACE ("Input surface %lx - Global alpha: %u", surface_id, object->alpha);

  // Setup the source rectangle.
  x = srcrect->x;
  y = srcrect->y;
  width = srcrect->w;
  height = srcrect->h;

  width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (inframe) :
      MIN (width, GST_VIDEO_FRAME_WIDTH (inframe) - x);
  height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (inframe) :
      MIN (height, GST_VIDEO_FRAME_HEIGHT (inframe) - y);

  object->source.x = x;
  object->source.y = y;
  object->source.w = width;
  object->source.h = height;

  if (GET_OPT_FLIP_VERTICAL (opts)) {
    object->mask |= ::ib2c::ConfigMask::kVFlip;
    GST_TRACE ("Input surface %lx - Flip Vertically", surface_id);
  }

  if (GET_OPT_FLIP_HORIZONTAL (opts)) {
    object->mask |= ::ib2c::ConfigMask::kHFlip;
    GST_TRACE ("Input surface %lx - Flip Horizontally", surface_id);
  }

  // Setup the target rectangle.
  x = dstrect->x;
  y = dstrect->y;
  width = dstrect->w;
  height = dstrect->h;

  object->destination.x = ((width != 0) && (height != 0)) ? x : 0;
  object->destination.y = ((width != 0) && (height != 0)) ? y : 0;;

  // Setup rotation angle and adjustments.
  switch (GET_OPT_ROTATION (opts)) {
    case GST_GLES_VIDEO_ROTATE_90_CW:
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

      x = (dstrect->w && dstrect->h) ?
          x : (GST_VIDEO_FRAME_WIDTH (outframe) - width) / 2;

      // Adjust the target rectangle coordinates.
      object->destination.x = GST_VIDEO_FRAME_WIDTH (outframe) - (x + width);
      object->destination.y = y;

      object->rotation = 90.0;
      break;
    }
    case GST_GLES_VIDEO_ROTATE_180:
      GST_TRACE ("Input surface %lx - rotate 180°", surface_id);

      // Adjust the target rectangle dimensions.
      width = (width == 0) ? GST_VIDEO_FRAME_WIDTH (outframe) : width;
      height = (height == 0) ? GST_VIDEO_FRAME_HEIGHT (outframe) : height;

      object->destination.w = width;
      object->destination.h = height;

      object->rotation = 180.0;
      break;
    case GST_GLES_VIDEO_ROTATE_90_CCW:
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

      x = (dstrect->w && dstrect->h) ?
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
gst_retrieve_surface_id (GstGlesVideoConverter * convert, GHashTable * surfaces,
    guint direction, const GstVideoFrame * vframe, const GstStructure * opts)
{
  GstMemory *memory = NULL;
  guint fd = 0, bits = 0;
  guint64 surface_id = 0;

  // Get the 1st (and only) memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (vframe->buffer, 0);
  GST_GLES_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), 0,
      "Input buffer %p does not have FD memory!", vframe->buffer);

  // Get the input buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (surfaces, GUINT_TO_POINTER (fd))) {
    bits = GET_OPT_UBWC_FORMAT (opts) ? GST_GLES_UBWC_FORMAT_FLAG : 0;
    bits |= GET_OPT_FLOAT16_FORMAT (opts) ? GST_GLES_FLOAT16_FORMAT_FLAG : 0;
    bits |= GET_OPT_FLOAT32_FORMAT (opts) ? GST_GLES_FLOAT32_FORMAT_FLAG : 0;

    // Create an input surface and add its ID to the input hash table.
    surface_id = gst_create_surface (convert, direction, vframe, bits);
    GST_GLES_RETURN_VAL_IF_FAIL (surface_id != 0, 0,
        "Failed to create surface!");

    g_hash_table_insert (surfaces, GUINT_TO_POINTER (fd),
        GUINT_TO_POINTER (surface_id));
  } else {
    // Get the input surface ID from the input hash table.
    surface_id = GPOINTER_TO_ULONG (
        g_hash_table_lookup (surfaces, GUINT_TO_POINTER (fd)));
  }

  return surface_id;
}

GstGlesVideoConverter *
gst_gles_video_converter_new ()
{
  GstGlesVideoConverter *convert = NULL;

  convert = g_slice_new0 (GstGlesVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  convert->ib2chandle = dlopen ("libIB2C.so", RTLD_NOW);
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->ib2chandle != NULL, NULL,
      gst_gles_video_converter_free (convert),
      "Failed to open IB2C library, error: %s!", dlerror());

  ::ib2c::NewIEngine NewEngine =
      (::ib2c::NewIEngine) dlsym(convert->ib2chandle, IB2C_ENGINE_NEW_FUNC);

  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (NewEngine != NULL, NULL,
      gst_gles_video_converter_free (convert),
      "Failed to load IB2C symbol, error: %s!", dlerror());

  try {
    convert->engine = NewEngine();
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create and init new engine, error: '%s'!", e.what());
    gst_gles_video_converter_free (convert);
    return NULL;
  }

  convert->insurfaces = g_hash_table_new (NULL, NULL);
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->insurfaces != NULL, NULL,
      gst_gles_video_converter_free (convert),
      "Failed to create hash table for source surfaces!");

  convert->outsurfaces = g_hash_table_new (NULL, NULL);
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->outsurfaces != NULL, NULL,
      gst_gles_video_converter_free (convert),
      "Failed to create hash table for target surfaces!");

  GST_INFO ("Created GLES Converter %p", convert);
  return convert;
}

void
gst_gles_video_converter_free (GstGlesVideoConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->inopts != NULL)
    g_list_free_full (convert->inopts, (GDestroyNotify) gst_structure_free);

  if (convert->outopts != NULL)
    g_list_free_full (convert->outopts, (GDestroyNotify) gst_structure_free);

  if (convert->insurfaces != NULL) {
    g_hash_table_foreach (convert->insurfaces, gst_destroy_surface, convert);
    g_hash_table_destroy(convert->insurfaces);
  }

  if (convert->outsurfaces != NULL) {
    g_hash_table_foreach (convert->outsurfaces, gst_destroy_surface, convert);
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

gboolean
gst_gles_video_converter_set_input_opts (GstGlesVideoConverter * convert,
    guint index, GstStructure *opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_GLES_LOCK (convert);

  if ((index >= g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_GLES_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->inopts = g_list_remove (convert->inopts,
        g_list_nth_data (convert->inopts, index));
    GST_GLES_UNLOCK (convert);
    return TRUE;
  } else if (index > g_list_length (convert->inopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_GLES_UNLOCK (convert);
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

  GST_GLES_UNLOCK (convert);

  return TRUE;
}

gboolean
gst_gles_video_converter_set_output_opts (GstGlesVideoConverter * convert,
    guint index, GstStructure * opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);

  GST_GLES_LOCK(convert);

  if ((index >= g_list_length (convert->outopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_GLES_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->outopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->outopts = g_list_remove (convert->outopts,
        g_list_nth_data (convert->outopts, index));
    GST_GLES_UNLOCK (convert);
    return TRUE;
  } else if (index > g_list_length (convert->outopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_GLES_UNLOCK (convert);
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

  GST_GLES_UNLOCK (convert);

  return TRUE;
}

gpointer
gst_gles_video_converter_submit_request (GstGlesVideoConverter * convert,
    GstGlesComposition * compositions, guint n_compositions)
{
  GstStructure *opts = NULL;
  guint idx = 0, num = 0, n_rects = 0, n_inputs = 0, offset = 0;
  guint64 surface_id = 0;

  g_return_val_if_fail (convert != NULL, NULL);
  g_return_val_if_fail ((compositions != NULL) && (n_compositions != 0), NULL);

  std::vector<::ib2c::Composition> blits;

  GST_GLES_LOCK (convert);

  for (idx = 0; idx < n_compositions; idx++) {
    const GstVideoFrame *outframe = compositions[idx].outframe;
    const GstVideoFrame *inframes = compositions[idx].inframes;

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

    std::vector<::ib2c::Object> objects;

    // Iterate over the input frames for current composition.
    for (num = 0; num < n_inputs; num++) {
      const GstVideoFrame *inframe = &(inframes[num]);

      if (NULL == inframe->buffer)
        continue;

      // Initialize empty options structure in case none have been set.
      if ((num + offset) >= g_list_length (convert->inopts)) {
        convert->inopts =
            g_list_append (convert->inopts, gst_structure_new_empty ("options"));
      }

      // Get the options for current input buffer.
      opts = GST_STRUCTURE (g_list_nth_data (convert->inopts, (num + offset)));

      surface_id = gst_retrieve_surface_id (convert, convert->insurfaces,
          GST_GLES_INPUT, inframe, opts);
      GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (surface_id != 0, NULL,
          GST_GLES_UNLOCK (convert), "Failed to get surface ID for input buffer!");

      // Extract the source and destination rectangles.
      std::vector<::ib2c::Region> srcrects, dstrects;
      gst_extract_rectangles (opts, srcrects, dstrects, n_rects);

      // Fill a separate GLES object for each rectangle pair in this input frame.
      while (n_rects-- != 0) {
        ::ib2c::Object object;

        gst_update_object (&object, surface_id, opts, inframe,
            &srcrects[n_rects], outframe, &dstrects[n_rects]);

        objects.push_back(object);
      }
    }

    // Increate the offset to the input frame options.
    offset += n_inputs;

    // Get the options for current output frame.
    opts = GST_STRUCTURE (g_list_nth_data (convert->outopts, idx));

    surface_id = gst_retrieve_surface_id (convert, convert->outsurfaces,
        GST_GLES_OUTPUT, outframe, opts);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (surface_id != 0, NULL,
        GST_GLES_UNLOCK (convert), "Failed to get surface ID for output buffer!");

    uint32_t color = GET_OPT_BACKGROUND (opts);
    bool clear = GET_OPT_CLEAR (opts);

    std::vector<::ib2c::Normalize> normalization;

    normalization.push_back(
        ::ib2c::Normalize (GET_OPT_RSCALE (opts), GET_OPT_ROFFSET (opts)));
    normalization.push_back(
        ::ib2c::Normalize (GET_OPT_GSCALE (opts), GET_OPT_GOFFSET (opts)));
    normalization.push_back(
        ::ib2c::Normalize (GET_OPT_BSCALE (opts), GET_OPT_BOFFSET (opts)));
    normalization.push_back(
        ::ib2c::Normalize (GET_OPT_ASCALE (opts), GET_OPT_AOFFSET (opts)));

    blits.push_back(std::move(
        std::make_tuple(surface_id, color, clear, normalization, objects)));
  }

  GST_GLES_UNLOCK (convert);

  std::uintptr_t request_id;

  try {
    request_id = convert->engine->Compose (blits);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to submit draw objects, error: '%s'!", e.what());
    return NULL;
  }

  return reinterpret_cast<gpointer>(request_id);
}

gboolean
gst_gles_video_converter_wait_request (GstGlesVideoConverter * convert,
    gpointer request_id)
{
  g_return_val_if_fail (convert != NULL, FALSE);

  if (request_id == NULL)
    return TRUE;

  try {
    convert->engine->Finish (reinterpret_cast<std::intptr_t>(request_id));
  } catch (std::exception& e) {
    GST_ERROR ("Failed to process request ID, error: '%s'!", e.what());
    return FALSE;
  }

  return TRUE;
}

void
gst_gles_video_converter_flush (GstGlesVideoConverter * convert)
{
  g_return_if_fail (convert != NULL);

  // try {
  //   convert->engine->Finish (reinterpret_cast<std::intptr_t>(nullptr));
  // } catch (std::exception& e) {
  //   GST_ERROR ("Failed to process frames, error: '%s'!", e.what());
  // }

  return;
}
