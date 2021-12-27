/*
* Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <stdint.h>

#include <gst/allocators/gstfdmemory.h>
#include <adreno/image_convert.h>
#include <drm/drm_fourcc.h>
#include <gbm_priv.h>

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

#define DRM_FMT_STRING(x) \
    (x) & 0xFF, ((x) >> 8) & 0xFF, ((x) >> 16) & 0xFF, ((x) >> 24) & 0xFF

#define DEFAULT_OPT_OUTPUT_WIDTH     0
#define DEFAULT_OPT_OUTPUT_HEIGHT    0
#define DEFAULT_OPT_DEST_X           0
#define DEFAULT_OPT_DEST_Y           0
#define DEFAULT_OPT_DEST_WIDTH       0
#define DEFAULT_OPT_DEST_HEIGHT      0
#define DEFAULT_OPT_RSCALE           128.0
#define DEFAULT_OPT_GSCALE           128.0
#define DEFAULT_OPT_BSCALE           128.0
#define DEFAULT_OPT_ASCALE           128.0
#define DEFAULT_OPT_QSCALE           128.0
#define DEFAULT_OPT_ROFFSET          0.0
#define DEFAULT_OPT_GOFFSET          0.0
#define DEFAULT_OPT_BOFFSET          0.0
#define DEFAULT_OPT_AOFFSET          0.0
#define DEFAULT_OPT_QOFFSET          0.0

#define DEFAULT_OPT_NORMALIZE        FALSE
#define DEFAULT_OPT_QUANTIZE         FALSE
#define DEFAULT_OPT_CONVERT_TO_UINT8 FALSE
#define DEFAULT_OPT_UBWC_FORMAT      FALSE

#define GET_OPT_OUTPUT_WIDTH(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH, v)
#define GET_OPT_OUTPUT_HEIGHT(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT, v)
#define GET_OPT_RSCALE(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_RSCALE, v)
#define GET_OPT_GSCALE(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_GSCALE, v)
#define GET_OPT_BSCALE(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BSCALE, v)
#define GET_OPT_ASCALE(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ASCALE, v)
#define GET_OPT_QSCALE(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QSCALE, v)
#define GET_OPT_ROFFSET(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET, v)
#define GET_OPT_GOFFSET(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET, v)
#define GET_OPT_BOFFSET(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET, v)
#define GET_OPT_AOFFSET(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET, v)
#define GET_OPT_QOFFSET(s, v) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QOFFSET, v)
#define GET_OPT_NORMALIZE(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE, v)
#define GET_OPT_QUANTIZE(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE, v)
#define GET_OPT_CONVERT_TO_UINT8(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_CONVERT_TO_UINT8, v)
#define GET_OPT_DEST_X(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_X, v)
#define GET_OPT_DEST_Y(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_Y, v)
#define GET_OPT_DEST_WIDTH(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_WIDTH, v)
#define GET_OPT_DEST_HEIGHT(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_HEIGHT, v)
#define GET_OPT_UBWC_FORMAT(s) get_opt_boolean(s, \
    GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT, DEFAULT_OPT_UBWC_FORMAT)

#define GST_GLES_GET_LOCK(obj) (&((GstGlesConverter *)obj)->lock)
#define GST_GLES_LOCK(obj)     g_mutex_lock (GST_GLES_GET_LOCK(obj))
#define GST_GLES_UNLOCK(obj)   g_mutex_unlock (GST_GLES_GET_LOCK(obj))

#define GST_CAT_DEFAULT ensure_debug_category()

struct _GstGlesConverter
{
  // Global mutex lock.
  GMutex                    lock;

  // List of options for for the clip API.
  GList                     *clipopts;
  // Options for the process API.
  GstStructure              *procopts;

  // DataConverter to construct the converter pipeline
  ::QImgConv::DataConverter *engine;
};

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
get_opt_int (const GstStructure * options, const gchar * opt, gint value)
{
  gint result;
  return gst_structure_get_int (options, opt, &result) ? result : value;
}

static gboolean
get_opt_boolean (const GstStructure * options, const gchar * opt, gboolean value)
{
  gboolean result;
  return gst_structure_get_boolean (options, opt, &result) ? result : value;
}

static gboolean
update_options (GQuark field, const GValue * value, gpointer userdata)
{
  gst_structure_id_set_value (GST_STRUCTURE_CAST (userdata), field, value);
  return TRUE;
}

static gint
gst_video_format_to_drm_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return DRM_FORMAT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return DRM_FORMAT_NV21;
    case GST_VIDEO_FORMAT_I420:
      return DRM_FORMAT_YUV420;
    case GST_VIDEO_FORMAT_YV12:
      return DRM_FORMAT_YVU420;
    case GST_VIDEO_FORMAT_YUV9:
      return DRM_FORMAT_YUV410;
    case GST_VIDEO_FORMAT_YVU9:
      return DRM_FORMAT_YVU410;
    case GST_VIDEO_FORMAT_NV16:
      return DRM_FORMAT_YUV422;
    case GST_VIDEO_FORMAT_NV61:
      return DRM_FORMAT_YVU422;
    case GST_VIDEO_FORMAT_YUY2:
      return DRM_FORMAT_YUYV;
    case GST_VIDEO_FORMAT_UYVY:
      return DRM_FORMAT_UYVY;
    case GST_VIDEO_FORMAT_YVYU:
      return DRM_FORMAT_YVYU;
    case GST_VIDEO_FORMAT_VYUY:
      return DRM_FORMAT_VYUY;
    case GST_VIDEO_FORMAT_BGRx:
      return DRM_FORMAT_BGRX8888;
    case GST_VIDEO_FORMAT_RGBx:
      return DRM_FORMAT_RGBX8888;
    case GST_VIDEO_FORMAT_xBGR:
      return DRM_FORMAT_XBGR8888;
    case GST_VIDEO_FORMAT_xRGB:
      return DRM_FORMAT_XRGB8888;
    case GST_VIDEO_FORMAT_RGBA:
      return DRM_FORMAT_RGBA8888;
    case GST_VIDEO_FORMAT_BGRA:
      return DRM_FORMAT_BGRA8888;
    case GST_VIDEO_FORMAT_ABGR:
      return DRM_FORMAT_ABGR8888;
    case GST_VIDEO_FORMAT_BGR:
      return DRM_FORMAT_BGR888;
    case GST_VIDEO_FORMAT_RGB:
      return DRM_FORMAT_RGB888;
    case GST_VIDEO_FORMAT_BGR16:
      return DRM_FORMAT_BGR565;
    case GST_VIDEO_FORMAT_RGB16:
      return DRM_FORMAT_RGB565;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }
  return 0;
}

static gint
gst_video_normalization_format (const GstVideoFrame * frame)
{
  GstVideoFormat format = GST_VIDEO_FRAME_FORMAT (frame);
  guint bpp = GST_VIDEO_FRAME_SIZE (frame) /
      (GST_VIDEO_FRAME_WIDTH (frame) * GST_VIDEO_FRAME_HEIGHT (frame));

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
      return ((bpp / 3) == 4) ?
          GBM_FORMAT_RGB323232F : GBM_FORMAT_RGB161616F;
    case GST_VIDEO_FORMAT_RGBA:
      return ((bpp / 4) == 4) ?
          GBM_FORMAT_RGBA32323232F : GBM_FORMAT_RGBA16161616F;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }

  return 0;
}

static void
gst_extract_rectangles (const GstStructure * opts, const gchar * opt,
    const GstVideoFrame * frame, ::QImgConv::Rec ** rectangles, guint * n_rects)
{
  ::QImgConv::Rec *rects = NULL;
  guint idx = 0, num = 0, n_entries = 0;
  const GValue *entries = NULL;
  const gchar *type = NULL;

  type = (g_strcmp0 (opt, GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES) == 0) ?
      "Source" : "Destination";

  entries = gst_structure_get_value (opts, opt);
  n_entries = (entries == NULL) ? 0 : gst_value_array_get_size (entries);

  // Make sure that at least 1 entry is allocated and filled.
  rects = new (std::nothrow) ::QImgConv::Rec[(n_entries == 0) ? 1 : n_entries];

  // In case there are no rectangles use the whole frame.
  rects[0].x = 0;
  rects[0].y = 0;
  rects[0].width = GST_VIDEO_FRAME_WIDTH (frame);
  rects[0].height = GST_VIDEO_FRAME_HEIGHT (frame);

  for (idx = 0; idx < n_entries; idx++) {
    const GValue *entry = gst_value_array_get_value (entries, idx);

    // Entries that do not have at least 4 values are going to be ignored.
    if (gst_value_array_get_size (entry) != 4) {
      GST_WARNING ("%s rectangle at index %u does not contain exactly 4"
          "values, using whole input frame as source!", type, idx);
      continue;
    }

    rects[num].x = g_value_get_int (gst_value_array_get_value (entry, 0));
    rects[num].y = g_value_get_int (gst_value_array_get_value (entry, 1));
    rects[num].width = g_value_get_int (gst_value_array_get_value (entry, 2));
    rects[num].height = g_value_get_int (gst_value_array_get_value (entry, 3));

    GST_TRACE ("%s rectangle[%u]: [%u %u %u %u]", type, num,
        rects[num].x, rects[num].y, rects[num].width, rects[num].height);

    num++;
  }

  *rectangles = rects;
  *n_rects = (n_entries == 0) ? 1 : num;
}

static gboolean
gst_video_converter_update_image (::QImgConv::Image * image, gboolean input,
    const GstVideoFrame * frame)
{
  GstMemory *memory = NULL;
  const gchar *imgtype = NULL;

  imgtype = input ? "Input" : "Output";

  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_GLES_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), FALSE,
      "%s buffer memory is not FD backed!", imgtype);

  image->fd = gst_fd_memory_get_fd (memory);
  image->width = GST_VIDEO_FRAME_WIDTH (frame);
  image->height = GST_VIDEO_FRAME_HEIGHT (frame);
  image->format = gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (frame));
  image->numPlane = GST_VIDEO_FRAME_N_PLANES (frame);

  GST_TRACE ("%s image FD[%d] - Width[%u] Height[%u] Format[%c%c%c%c]"
      " Planes[%u]", imgtype, image->fd, image->width, image->height,
      DRM_FMT_STRING (image->format), image->numPlane);

  image->plane0Stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  image->plane0Offset = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  GST_TRACE ("%s image FD[%d] - Stride0[%u] Offset0[%u]", imgtype, image->fd,
      image->plane0Stride, image->plane0Offset);

  image->plane1Stride = (image->numPlane >= 2) ?
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1) : 0;
  image->plane1Offset = (image->numPlane >= 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) : 0;

  GST_TRACE ("%s image FD[%d] - Stride1[%u] Offset1[%u]", imgtype,
      image->fd, image->plane1Stride, image->plane1Offset);

  image->isLinear = (image->plane0Stride % 128 != 0) ? true : false;

  return TRUE;
}

GstGlesConverter *
gst_gles_video_converter_new ()
{
  GstGlesConverter *convert = NULL;

  convert = g_slice_new0 (GstGlesConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  convert->engine = new (std::nothrow) ::QImgConv::DataConverter();
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->engine != NULL, NULL,
      gst_gles_video_converter_free (convert), "Failed to create GLES engine!");

  convert->procopts = gst_structure_new_empty ("Output");
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->procopts != NULL, NULL,
      gst_gles_video_converter_free (convert), "Failed to create OPTS struct!");

  GST_INFO ("Created GLES Converter %p", convert);
  return convert;
}

void
gst_gles_video_converter_free (GstGlesConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->clipopts != NULL)
    g_list_free_full (convert->clipopts, (GDestroyNotify) gst_structure_free);

  if (convert->procopts != NULL)
    gst_structure_free (convert->procopts);

  if (convert->engine != NULL)
    delete convert->engine;

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed GLES converter: %p", convert);
  g_slice_free (GstGlesConverter, convert);
}

gboolean
gst_gles_video_converter_set_clip_opts (GstGlesConverter * convert,
    guint index, GstStructure *opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_GLES_LOCK(convert);

  if (index > g_list_length (convert->clipopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_GLES_UNLOCK (convert);
    return FALSE;
  } else if ((index == g_list_length (convert->clipopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_GLES_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->clipopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->clipopts = g_list_remove (convert->clipopts,
        g_list_nth_data (convert->clipopts, index));
    GST_GLES_UNLOCK (convert);
    return TRUE;
  }

  if (index == g_list_length (convert->clipopts)) {
    GST_LOG ("Add a new opts structure in the list at index %u", index);

    convert->clipopts = g_list_append (convert->clipopts,
        gst_structure_new_empty ("Input"));
  }

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options,
      g_list_nth_data (convert->clipopts, index));
  gst_structure_free (opts);

  GST_GLES_UNLOCK (convert);

  return TRUE;
}

gboolean
gst_gles_video_converter_set_process_opts (GstGlesConverter * convert,
    GstStructure * opts)
{
  GstVideoRectangle destination;
  gint width, height;
  gfloat rscale, gscale, bscale, ascale, qscale;
  gfloat roffset, goffset, boffset, aoffset, qoffset;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_GLES_LOCK(convert);

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options, convert->procopts);
  gst_structure_free (opts);

  width  = GET_OPT_OUTPUT_WIDTH (convert->procopts, DEFAULT_OPT_OUTPUT_WIDTH);
  height = GET_OPT_OUTPUT_HEIGHT (convert->procopts, DEFAULT_OPT_OUTPUT_HEIGHT);

  destination.x = GET_OPT_DEST_X (convert->procopts, DEFAULT_OPT_DEST_X);
  destination.y = GET_OPT_DEST_Y (convert->procopts, DEFAULT_OPT_DEST_Y);
  destination.w = GET_OPT_DEST_WIDTH (convert->procopts, DEFAULT_OPT_DEST_WIDTH);
  destination.h = GET_OPT_DEST_HEIGHT (convert->procopts, DEFAULT_OPT_DEST_HEIGHT);

  rscale  = GET_OPT_RSCALE (convert->procopts, DEFAULT_OPT_RSCALE);
  gscale  = GET_OPT_GSCALE (convert->procopts, DEFAULT_OPT_GSCALE);
  bscale  = GET_OPT_BSCALE (convert->procopts, DEFAULT_OPT_BSCALE);
  ascale  = GET_OPT_ASCALE (convert->procopts, DEFAULT_OPT_ASCALE);
  qscale  = GET_OPT_QSCALE (convert->procopts, DEFAULT_OPT_QSCALE);

  roffset = GET_OPT_ROFFSET (convert->procopts, DEFAULT_OPT_ROFFSET);
  goffset = GET_OPT_GOFFSET (convert->procopts, DEFAULT_OPT_GOFFSET);
  boffset = GET_OPT_BOFFSET (convert->procopts, DEFAULT_OPT_BOFFSET);
  aoffset = GET_OPT_AOFFSET (convert->procopts, DEFAULT_OPT_AOFFSET);
  qoffset = GET_OPT_QOFFSET (convert->procopts, DEFAULT_OPT_QOFFSET);

  GST_GLES_UNLOCK (convert);

  std::vector<std::string> composition;

  if (width == 0 || height == 0) {
    GST_ERROR ("Invalid output dimensions: %dx%d!", width, height);
    return FALSE;
  }

  composition.push_back (convert->engine->Resize (width, height));

  // Apply destination rectangle only if necessary.
  if ((destination.w != 0) && (destination.h != 0) &&
      !(destination.y == 0 && destination.x == 0 &&
          destination.w == width && destination.h == height)) {
    ::QImgConv::Rec rectangle;
    ::QImgConv::Color color = {0.0,0.0,0.0,0.0};

    if (((destination.x + destination.w) > width) ||
        ((destination.y + destination.h) > height)) {
      GST_ERROR ("Destination rectangle [%d %d %d %d] is outside the output "
          "dimensions %dx%d!", destination.x, destination.y, destination.w,
          destination.h, width , height);
      return FALSE;
    }

    rectangle.x = destination.x;
    rectangle.y = destination.y;
    rectangle.width  = destination.w;
    rectangle.height = destination.h;

    composition.push_back (convert->engine->Letterbox (rectangle, color));
  }

  if (GET_OPT_NORMALIZE (convert->procopts, DEFAULT_OPT_NORMALIZE)) {
    GST_DEBUG ("Normalize Scale [%f %f %f %f] Offset [%f %f %f %f]",
        rscale, gscale, bscale, ascale, roffset, goffset, boffset, aoffset);
    composition.push_back (convert->engine->Normalize (
        (1.0 / rscale), (1.0 / gscale), (1.0 / bscale), (1.0 / ascale),
        roffset, goffset, boffset, aoffset));
  }

  if (GET_OPT_QUANTIZE (convert->procopts, DEFAULT_OPT_QUANTIZE)) {
    GST_DEBUG ("Quantize Scale [%f] Offset [%f]", qscale, qoffset);
    composition.push_back (convert->engine->Quantize ((1.0 / qscale), qoffset));
  }

  if (GET_OPT_CONVERT_TO_UINT8 (convert->procopts, DEFAULT_OPT_CONVERT_TO_UINT8))
    composition.push_back (convert->engine->ConverttoUINT8());

  for (auto const& op : composition)
    GST_DEBUG ("Composing DataConverter with %s", op.c_str());

  if (convert->engine->Compose(composition) != ::QImgConv::STATUS_OK) {
    GST_ERROR ("Failed to compose the GLES engine operations!");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_gles_video_converter_clip (GstGlesConverter * convert,
    GstVideoFrame * inframes, guint n_inputs, GstVideoFrame * outframes,
    guint n_outputs)
{
  ::QImgConv::Image *inimages = NULL, *outimages = NULL;
  ::QImgConv::STATUS status = ::QImgConv::STATUS_OK;
  gboolean success = FALSE;
  guint idx = 0;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs != 0), FALSE);
  g_return_val_if_fail ((outframes != NULL) && (n_outputs != 0), FALSE);

  if ((n_inputs > 1) && (n_outputs > 1)) {
    GST_ERROR ("Mutiple input frames to multiple output frames not supported! "
        "Only (N to 1) or (1 -> N) modes are supported!");
    return FALSE;
  }

  outimages = new (std::nothrow) QImgConv::Image[n_outputs];
  GST_GLES_RETURN_VAL_IF_FAIL (outimages != NULL, FALSE,
      "Failed to allocate output images!");

  for (idx = 0; idx < n_outputs; idx++) {
    const GstVideoFrame *frame = &outframes[idx];

    success = gst_video_converter_update_image (&outimages[idx], false, frame);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, delete[] outimages,
        "Failed to update output image at index %u !", idx);
  }

  inimages = new (std::nothrow) QImgConv::Image[n_inputs];
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (inimages != NULL, FALSE,
      delete[] outimages, "Failed to allocate input images!");

  for (idx = 0; idx < n_inputs; idx++) {
    const GstVideoFrame *frame = &inframes[idx];
    const GstStructure *opts = NULL;
    ::QImgConv::Rec *inrects = NULL, *outrects = NULL;
    guint n_inrects = 0, n_outrects = 0, n_rects = 0;

    if (NULL == frame->buffer)
      continue;

    success = gst_video_converter_update_image (&inimages[idx], true, frame);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, delete[] inimages;
        delete[] outimages, "Failed to update input image at index %u !", idx);

    // Initialize empty options structure in case none have been set.
    if (idx >= g_list_length (convert->clipopts)) {
      convert->clipopts = g_list_append (convert->clipopts,
          gst_structure_new_empty ("Input"));
    }

    // Get the options for current input buffer.
    opts = GST_STRUCTURE (g_list_nth_data (convert->clipopts, idx));

    // In case the UBWC format option is set override the format.
    if (GET_OPT_UBWC_FORMAT (opts) && (inimages[idx].format == DRM_FORMAT_NV12))
      inimages[idx].format = GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC;

    gst_extract_rectangles (opts, GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES,
        &inframes[idx], &inrects, &n_inrects);
    gst_extract_rectangles (opts, GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES,
        &outframes[idx], &outrects, &n_outrects);

    // Clip the number of rectangles to the lower count.
    n_rects = (n_inrects < n_outrects) ? n_inrects : n_outrects;

    status = convert->engine->Clip (inimages[idx], inrects, outrects,
        outimages, n_rects, ((n_outputs == 1) ? true : false), false);

    delete[] inrects;
    delete[] outrects;

    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == ::QImgConv::STATUS_OK,
        FALSE, delete[] inimages; delete[] outimages,
        "Failed to setup clip parameters!");
  }

  status = convert->engine->Run ();
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == ::QImgConv::STATUS_OK,
      FALSE, delete[] inimages; delete[] outimages, "Failed to perform clip!");

  delete[] inimages;
  delete[] outimages;

  return TRUE;
}

gboolean
gst_gles_video_converter_process (GstGlesConverter * convert,
    GstVideoFrame * inframes, guint n_inputs, GstVideoFrame * outframes,
    guint n_outputs)
{
  ::QImgConv::Image *inimages = NULL, *outimages = NULL;
  ::QImgConv::STATUS status = ::QImgConv::STATUS_OK;
  gboolean success = FALSE;
  guint idx = 0;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs != 0), FALSE);
  g_return_val_if_fail ((outframes != NULL) && (n_outputs != 0), FALSE);

  inimages = new (std::nothrow) QImgConv::Image[n_inputs];
  GST_GLES_RETURN_VAL_IF_FAIL (inimages != NULL, FALSE,
      "Failed to allocate input images!");

  for (idx = 0; idx < n_inputs; idx++) {
    const GstVideoFrame *frame = &inframes[idx];

    if (NULL == frame->buffer)
      continue;

    success = gst_video_converter_update_image (&inimages[idx], true, frame);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, delete[] inimages,
        "Failed to update QImgConv image at index %u !", idx);

    // In case the UBWC format option is set override the format.
    if (GET_OPT_UBWC_FORMAT (convert->procopts) &&
        (inimages[idx].format == DRM_FORMAT_NV12))
      inimages[idx].format = GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC;
  }

  outimages = new (std::nothrow) QImgConv::Image[n_outputs];
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (outimages != NULL, FALSE,
      delete[] inimages, "Failed to allocate output images!");

  for (idx = 0; idx < n_outputs; idx++) {
    const GstVideoFrame *frame = &outframes[idx];

    success = gst_video_converter_update_image (&outimages[idx], false, frame);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, delete[] inimages;
        delete[] outimages, "Failed to update output image at index %u !", idx);

    // Override format in case normalization was enabled.
    if (GET_OPT_NORMALIZE (convert->procopts, DEFAULT_OPT_NORMALIZE))
      outimages[idx].format = gst_video_normalization_format (frame);

    // Override isLinear to be always true when normalization is enabled.
    if (GET_OPT_NORMALIZE (convert->procopts, DEFAULT_OPT_NORMALIZE))
      outimages[idx].isLinear = true;
  }

  GST_GLES_LOCK (convert);

  status = convert->engine->DoPreProcess (inimages, outimages, n_inputs,
      (n_inputs / n_outputs));

  GST_GLES_UNLOCK (convert);

  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == ::QImgConv::STATUS_OK,
      FALSE, delete[] inimages; delete[] outimages, "Failed to process frames!");

  delete[] outimages;
  delete[] inimages;

  return TRUE;
}
