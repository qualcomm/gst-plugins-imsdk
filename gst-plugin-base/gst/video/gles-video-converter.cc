/*
* Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#define EXTRACT_RED_COLOR(color)   (((color >> 24) & 0xFF) / 255.0)
#define EXTRACT_GREEN_COLOR(color) (((color >> 16) & 0xFF) / 255.0)
#define EXTRACT_BLUE_COLOR(color)  (((color >> 8) & 0xFF) / 255.0)
#define EXTRACT_ALPHA_COLOR(color) (((color) & 0xFF) / 255.0)

#define MAX_NUM_IN_IMAGES            100
#define MAX_NUM_OUT_IMAGES           25

#define DEFAULT_OPT_OUTPUT_WIDTH     0
#define DEFAULT_OPT_OUTPUT_HEIGHT    0
#define DEFAULT_OPT_BACKGROUND       0x00000000
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

#define GET_OPT_OUTPUT_WIDTH(s) get_opt_uint (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH, DEFAULT_OPT_OUTPUT_WIDTH)
#define GET_OPT_OUTPUT_HEIGHT(s) get_opt_uint (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT, DEFAULT_OPT_OUTPUT_HEIGHT)
#define GET_OPT_BACKGROUND(s) get_opt_uint (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BACKGROUND, DEFAULT_OPT_BACKGROUND)
#define GET_OPT_RSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_RSCALE, DEFAULT_OPT_RSCALE)
#define GET_OPT_GSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_GSCALE, DEFAULT_OPT_GSCALE)
#define GET_OPT_BSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BSCALE, DEFAULT_OPT_BSCALE)
#define GET_OPT_ASCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ASCALE, DEFAULT_OPT_ASCALE)
#define GET_OPT_QSCALE(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QSCALE, DEFAULT_OPT_QSCALE)
#define GET_OPT_ROFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_ROFFSET, DEFAULT_OPT_ROFFSET)
#define GET_OPT_GOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_GOFFSET, DEFAULT_OPT_GOFFSET)
#define GET_OPT_BOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_BOFFSET, DEFAULT_OPT_BOFFSET)
#define GET_OPT_AOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_AOFFSET, DEFAULT_OPT_AOFFSET)
#define GET_OPT_QOFFSET(s) get_opt_double (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QOFFSET, DEFAULT_OPT_QOFFSET)
#define GET_OPT_NORMALIZE(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE, DEFAULT_OPT_NORMALIZE)
#define GET_OPT_QUANTIZE(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE, DEFAULT_OPT_QUANTIZE)
#define GET_OPT_CONVERT_TO_UINT8(s) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_CONVERT_TO_UINT8, DEFAULT_OPT_CONVERT_TO_UINT8)
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

  // List of surface options for each input frame.
  GList                     *inopts;
  // Set of options performed for each output frame.
  GstStructure              *outopts;

  // DataConverter to construct the converter pipeline
  ::QImgConv::DataConverter *engine;
};

enum
{
  GST_GLES_INPUT,
  GST_GLES_OUTPUT,
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

static gboolean
gst_gles_video_converter_update_image (::QImgConv::Image * image,
    const guint direction, const GstVideoFrame * frame)
{
  GstMemory *memory = NULL;
  const gchar *type = NULL;

  type = (direction == GST_GLES_INPUT) ? "Input" : "Output";

  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_GLES_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), FALSE,
      "%s buffer memory is not FD backed!", type);

  image->fd = gst_fd_memory_get_fd (memory);
  image->width = GST_VIDEO_FRAME_WIDTH (frame);
  image->height = GST_VIDEO_FRAME_HEIGHT (frame);
  image->format = gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (frame));
  image->numPlane = GST_VIDEO_FRAME_N_PLANES (frame);

  GST_TRACE ("%s image FD[%d] - Width[%u] Height[%u] Format[%c%c%c%c]"
      " Planes[%u]", type, image->fd, image->width, image->height,
      DRM_FMT_STRING (image->format), image->numPlane);

  image->plane0Stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  image->plane0Offset = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  GST_TRACE ("%s image FD[%d] - Stride0[%u] Offset0[%u]", type, image->fd,
      image->plane0Stride, image->plane0Offset);

  image->plane1Stride = (image->numPlane >= 2) ?
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1) : 0;
  image->plane1Offset = (image->numPlane >= 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) : 0;

  GST_TRACE ("%s image FD[%d] - Stride1[%u] Offset1[%u]", type,
      image->fd, image->plane1Stride, image->plane1Offset);

  image->isLinear = (image->plane0Stride % 128 != 0) ? true : false;

  return TRUE;
}

static void
gst_gles_video_converter_extract_rectangles (const GstStructure * opts,
    const gchar * opt, std::vector<::QImgConv::Rec> &rectangles)
{
  const GValue *entries = NULL, *entry = NULL;
  const gchar *type = NULL;
  guint idx = 0, n_entries = 0, n_rects = 0;

  entries = gst_structure_get_value (opts, opt);
  n_entries = (entries == NULL) ? 0 : gst_value_array_get_size (entries);

  // Make sure that there is at least one new rectangle in the list.
  n_rects = (n_entries == 0) ? 1 : n_entries;

  type = (g_strcmp0 (opt, GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES) == 0) ?
      "Source" : "Destination";

  // Make sure that there is at least one new rectangle in the list.
  for (idx = 0; idx < n_rects; idx++) {
    ::QImgConv::Rec rectangle = {0,0,0,0};

    entry = (n_entries != 0) ? gst_value_array_get_value (entries, idx) : NULL;

    if ((entry != NULL) && (gst_value_array_get_size (entry) == 4)) {
      rectangle.x = g_value_get_int (gst_value_array_get_value (entry, 0));
      rectangle.y = g_value_get_int (gst_value_array_get_value (entry, 1));
      rectangle.width = g_value_get_int (gst_value_array_get_value (entry, 2));
      rectangle.height = g_value_get_int (gst_value_array_get_value (entry, 3));
    } else if (entry != NULL) {
      GST_WARNING ("%s rectangle at index %u does not contain exactly 4"
          "values, using default values!", type, idx);
    }

    rectangles.push_back(rectangle);
  }
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

  convert->outopts = gst_structure_new_empty ("Output");
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->outopts != NULL, NULL,
      gst_gles_video_converter_free (convert), "Failed to create OPTS struct!");

  GST_INFO ("Created GLES Converter %p", convert);
  return convert;
}

void
gst_gles_video_converter_free (GstGlesConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->inopts != NULL)
    g_list_free_full (convert->inopts, (GDestroyNotify) gst_structure_free);

  if (convert->outopts != NULL)
    gst_structure_free (convert->outopts);

  if (convert->engine != NULL)
    delete convert->engine;

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed GLES converter: %p", convert);
  g_slice_free (GstGlesConverter, convert);
}

gboolean
gst_gles_video_converter_set_input_opts (GstGlesConverter * convert,
    guint index, GstStructure *opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_GLES_LOCK(convert);

  if (index > g_list_length (convert->inopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_GLES_UNLOCK (convert);
    return FALSE;
  } else if ((index == g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_GLES_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->inopts = g_list_remove (convert->inopts,
        g_list_nth_data (convert->inopts, index));
    GST_GLES_UNLOCK (convert);
    return TRUE;
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
gst_gles_video_converter_set_output_opts (GstGlesConverter * convert,
    GstStructure * opts)
{
  guint width, height, colour;
  gfloat rscale, gscale, bscale, ascale, qscale;
  gfloat roffset, goffset, boffset, aoffset, qoffset;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_GLES_LOCK(convert);

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options, convert->outopts);
  gst_structure_free (opts);

  width  = GET_OPT_OUTPUT_WIDTH (convert->outopts);
  height = GET_OPT_OUTPUT_HEIGHT (convert->outopts);
  colour  = GET_OPT_BACKGROUND (convert->outopts);

  rscale  = GET_OPT_RSCALE (convert->outopts);
  gscale  = GET_OPT_GSCALE (convert->outopts);
  bscale  = GET_OPT_BSCALE (convert->outopts);
  ascale  = GET_OPT_ASCALE (convert->outopts);
  qscale  = GET_OPT_QSCALE (convert->outopts);

  roffset = GET_OPT_ROFFSET (convert->outopts);
  goffset = GET_OPT_GOFFSET (convert->outopts);
  boffset = GET_OPT_BOFFSET (convert->outopts);
  aoffset = GET_OPT_AOFFSET (convert->outopts);
  qoffset = GET_OPT_QOFFSET (convert->outopts);

  GST_GLES_UNLOCK (convert);

  std::vector<std::string> composition;

  if (width == 0 || height == 0) {
    GST_ERROR ("Invalid output dimensions: %ux%u!", width, height);
    return FALSE;
  }

  GST_DEBUG ("Resize dimensions: %ux%u", width, height);
  composition.push_back (convert->engine->Resize (width, height));

  ::QImgConv::Color qcolour = {0.0,0.0,0.0,0.0};

  qcolour.r = EXTRACT_RED_COLOR (colour);
  qcolour.g = EXTRACT_GREEN_COLOR (colour);
  qcolour.b = EXTRACT_BLUE_COLOR (colour);
  qcolour.a = EXTRACT_ALPHA_COLOR (colour);

  GST_DEBUG ("Background colour: 0x%x", colour);
  composition.push_back (convert->engine->Background (qcolour));

  if (GET_OPT_NORMALIZE (convert->outopts)) {
    GST_DEBUG ("Normalize Scale [%f %f %f %f] Offset [%f %f %f %f]",
        rscale, gscale, bscale, ascale, roffset, goffset, boffset, aoffset);
    composition.push_back (convert->engine->Normalize (
        (1.0 / rscale), (1.0 / gscale), (1.0 / bscale), (1.0 / ascale),
        roffset, goffset, boffset, aoffset));
  }

  if (GET_OPT_QUANTIZE (convert->outopts)) {
    GST_DEBUG ("Quantize Scale [%f] Offset [%f]", qscale, qoffset);
    composition.push_back (convert->engine->Quantize ((1.0 / qscale), qoffset));
  }

  if (GET_OPT_CONVERT_TO_UINT8 (convert->outopts))
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
gst_gles_video_converter_process (GstGlesConverter * convert,
    GstVideoFrame * inframes, guint n_inputs, GstVideoFrame * outframes,
    guint n_outputs)
{
  std::vector<::QImgConv::Image> inimages, outimages;
  std::vector<::QImgConv::Rec> srcrects, dstrects;
  gboolean success = FALSE;
  guint idx = 0, num = 0, n_rects = 0;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs != 0), FALSE);
  g_return_val_if_fail ((outframes != NULL) && (n_outputs != 0), FALSE);

  GST_GLES_LOCK (convert);

  for (idx = 0; idx < n_inputs; idx++) {
    const GstVideoFrame *frame = &inframes[idx];
    const GstStructure *opts = NULL;
    ::QImgConv::Image image;

    if (NULL == frame->buffer)
      continue;

    // Initialize empty options structure in case none have been set.
    if (idx >= g_list_length (convert->inopts))
      convert->inopts = g_list_append (convert->inopts,
          gst_structure_new_empty ("Input"));

    // Get the options for current input buffer.
    opts = GST_STRUCTURE (g_list_nth_data (convert->inopts, idx));

    success = gst_gles_video_converter_update_image (&image, GST_GLES_INPUT, frame);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, GST_GLES_UNLOCK (convert),
        "Failed to update QImgConv image at index %u !", idx);

    // In case the UBWC format option is set override the format.
    if (GET_OPT_UBWC_FORMAT (opts) && (image.format == DRM_FORMAT_NV12))
      image.format = GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC;

    // Set the start index to the number of initial rectangles.
    num = n_rects = dstrects.size();

    // Fill the source and destination rectangles.
    gst_gles_video_converter_extract_rectangles (opts,
        GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, srcrects);
    gst_gles_video_converter_extract_rectangles (opts,
        GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, dstrects);

    if (srcrects.size() > dstrects.size()) {
      GST_WARNING ("Number of source rectangles exceeds the number of "
          "destination rectangles, clipping!");
      n_rects = dstrects.size();
      srcrects.resize(n_rects);
    } else if (srcrects.size() < dstrects.size()) {
      GST_WARNING ("Number of destination rectangles exceeds the number of "
          "source rectangles, clipping!");
      n_rects = srcrects.size();
      dstrects.resize(n_rects);
    }

    n_rects = srcrects.size();

    // Iterate over the pairs of source and destination rectangles.
    while (num < n_rects) {
      // Use the same image for each pair of source and destination rectangles.
      inimages.push_back(image);

      if ((srcrects[num].width == 0) && (srcrects[num].height == 0)) {
        srcrects[num].x = srcrects[num].y = 0;
        srcrects[num].width = image.width;
        srcrects[num].height = image.height;
      }

      GST_TRACE ("Input image FD[%d] - Source rectangle[%u]: [%u %u %u %u]",
          image.fd, num, srcrects[num].x, srcrects[num].y, srcrects[num].width,
          srcrects[num].height);

      if ((dstrects[num].width == 0) && (dstrects[num].height == 0)) {
        dstrects[num].x = 0;
        dstrects[num].y = idx * GET_OPT_OUTPUT_HEIGHT (convert->outopts) / n_inputs;
        dstrects[num].width = GET_OPT_OUTPUT_WIDTH (convert->outopts);
        dstrects[num].height = GET_OPT_OUTPUT_HEIGHT (convert->outopts) / n_inputs;
      }

      GST_TRACE ("Input image FD[%d] - Target rectangle[%u]: [%u %u %u %u]",
          image.fd, num, dstrects[num].x, dstrects[num].y, dstrects[num].width,
          dstrects[num].height);

      // Increment the rectangles index.
      num++;
    }
  }

  for (idx = 0; idx < n_outputs; idx++) {
    const GstVideoFrame *frame = &outframes[idx];
    ::QImgConv::Image image;

    success = gst_gles_video_converter_update_image (&image, GST_GLES_OUTPUT,
        frame);

    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, GST_GLES_UNLOCK (convert),
        "Failed to update output image at index %u !", idx);

    // Override format in case normalization was enabled.
    if (GET_OPT_NORMALIZE (convert->outopts))
      image.format = gst_video_normalization_format (frame);

    // Override isLinear to be always true when normalization is enabled.
    if (GET_OPT_NORMALIZE (convert->outopts))
      image.isLinear = true;

    outimages.push_back(image);
  }

  ::QImgConv::STATUS status = convert->engine->DoPreProcess (
      inimages.data(), srcrects.data(), dstrects.data(), outimages.data(),
      inimages.size(), std::ceil(inimages.size() / outimages.size()));

  GST_GLES_UNLOCK (convert);

  GST_GLES_RETURN_VAL_IF_FAIL (status == ::QImgConv::STATUS_OK, FALSE,
      "Failed to process frames!");

  return TRUE;
}
