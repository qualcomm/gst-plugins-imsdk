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

#define DEFAULT_OPT_RESIZE_WIDTH  0
#define DEFAULT_OPT_RESIZE_HEIGHT 0
#define DEFAULT_OPT_DEST_X        0
#define DEFAULT_OPT_DEST_Y        0
#define DEFAULT_OPT_DEST_WIDTH    0
#define DEFAULT_OPT_DEST_HEIGHT   0
#define DEFAULT_OPT_RSCALE        1.0
#define DEFAULT_OPT_GSCALE        1.0
#define DEFAULT_OPT_BSCALE        1.0
#define DEFAULT_OPT_ASCALE        1.0
#define DEFAULT_OPT_QSCALE        1.0
#define DEFAULT_OPT_ROFFSET       0.0
#define DEFAULT_OPT_GOFFSET       0.0
#define DEFAULT_OPT_BOFFSET       0.0
#define DEFAULT_OPT_AOFFSET       0.0
#define DEFAULT_OPT_QOFFSET       0.0

#define DEFAULT_OPT_RESIZE           FALSE
#define DEFAULT_OPT_NORMALIZE        FALSE
#define DEFAULT_OPT_QUANTIZE         FALSE
#define DEFAULT_OPT_CONVERT_TO_UINT8 FALSE
#define DEFAULT_OPT_CROP             NULL

#define GET_OPT_RESIZE_WIDTH(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_RESIZE_WIDTH, v)
#define GET_OPT_RESIZE_HEIGHT(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_RESIZE_HEIGHT, v)
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
#define GET_OPT_RESIZE(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_RESIZE, v)
#define GET_OPT_NORMALIZE(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_NORMALIZE, v)
#define GET_OPT_QUANTIZE(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_QUANTIZE, v)
#define GET_OPT_CROP(s, v) get_opt_value (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_CROP, v)
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

#define GST_GLES_GET_LOCK(obj) (&((GstGlesConverter *)obj)->lock)
#define GST_GLES_LOCK(obj)     g_mutex_lock (GST_GLES_GET_LOCK(obj))
#define GST_GLES_UNLOCK(obj)   g_mutex_unlock (GST_GLES_GET_LOCK(obj))

#define GST_CAT_DEFAULT ensure_debug_category()

struct _GstGlesConverter
{
  // Mutex lock synchronizing between threads
  GMutex lock;

  // options for the output frame
  GstStructure *options;

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

static const void *
get_opt_value (const GstStructure * options, const gchar* opt, gpointer userdata)
{
  return (gst_structure_get_field_type (options, opt) == GST_TYPE_LIST) ?
      gst_structure_get_value (options, opt) : userdata;
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

GstGlesConverter *
gst_gles_video_converter_new ()
{
  GstGlesConverter *convert = NULL;

  convert = g_slice_new0 (GstGlesConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  convert->engine = new (std::nothrow) ::QImgConv::DataConverter();
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->engine != NULL, NULL,
      gst_gles_video_converter_free (convert), "Failed to create GLES engine!");

  convert->options = gst_structure_new_empty ("options");
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->options != NULL, NULL,
      gst_gles_video_converter_free (convert), "Failed to create OPTS struct!");

  GST_INFO ("Created GLES Converter %p", convert);
  return convert;
}

void
gst_gles_video_converter_free (GstGlesConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->options != NULL)
    gst_structure_free (convert->options);

  if (convert->engine != NULL)
    delete convert->engine;

  GST_INFO ("Destroyed GLES converter: %p", convert);
  g_slice_free (GstGlesConverter, convert);
}

gboolean
gst_gles_video_converter_set_ops (GstGlesConverter * convert, GstStructure * opts)
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
  gst_structure_foreach (opts, update_options, convert->options);
  gst_structure_free (opts);

  width  = GET_OPT_RESIZE_WIDTH (convert->options, DEFAULT_OPT_RESIZE_WIDTH);
  height = GET_OPT_RESIZE_HEIGHT (convert->options, DEFAULT_OPT_RESIZE_HEIGHT);

  destination.x = GET_OPT_DEST_X (convert->options, DEFAULT_OPT_DEST_X);
  destination.y = GET_OPT_DEST_Y (convert->options, DEFAULT_OPT_DEST_Y);
  destination.w = GET_OPT_DEST_WIDTH (convert->options, DEFAULT_OPT_DEST_WIDTH);
  destination.h = GET_OPT_DEST_HEIGHT (convert->options, DEFAULT_OPT_DEST_HEIGHT);

  rscale  = GET_OPT_RSCALE (convert->options, DEFAULT_OPT_RSCALE);
  gscale  = GET_OPT_GSCALE (convert->options, DEFAULT_OPT_GSCALE);
  bscale  = GET_OPT_BSCALE (convert->options, DEFAULT_OPT_BSCALE);
  ascale  = GET_OPT_ASCALE (convert->options, DEFAULT_OPT_ASCALE);
  qscale  = GET_OPT_QSCALE (convert->options, DEFAULT_OPT_QSCALE);
  roffset = GET_OPT_ROFFSET (convert->options, DEFAULT_OPT_ROFFSET);
  goffset = GET_OPT_GOFFSET (convert->options, DEFAULT_OPT_GOFFSET);
  boffset = GET_OPT_BOFFSET (convert->options, DEFAULT_OPT_BOFFSET);
  aoffset = GET_OPT_AOFFSET (convert->options, DEFAULT_OPT_AOFFSET);
  qoffset = GET_OPT_QOFFSET (convert->options, DEFAULT_OPT_QOFFSET);

  GST_GLES_UNLOCK (convert);

  std::vector<std::string> composition;

  if (width == 0 || height == 0) {
    GST_ERROR ("Invalid rezise dimensions: %dx%d!", width, height);
    return FALSE;
  }

  if (GET_OPT_RESIZE (convert->options, DEFAULT_OPT_RESIZE))
    composition.push_back (convert->engine->Resize (width, height));

  if (GET_OPT_NORMALIZE (convert->options, DEFAULT_OPT_NORMALIZE))
    composition.push_back (convert->engine->Normalize (rscale, gscale, bscale,
        ascale, roffset, goffset, boffset, aoffset));

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

  if (GET_OPT_QUANTIZE (convert->options, DEFAULT_OPT_QUANTIZE))
    composition.push_back (convert->engine->Quantize (qscale, qoffset));

  if (GET_OPT_CONVERT_TO_UINT8 (convert->options, DEFAULT_OPT_CONVERT_TO_UINT8))
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
gst_gles_video_converter_set_crop_ops (GstGlesConverter * convert,
    GstStructure * crop_opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (crop_opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_GLES_LOCK(convert);

  // Iterate over the fields in the crop opts structure and update them.
  gst_structure_foreach (crop_opts, update_options, convert->options);
  gst_structure_free (crop_opts);

  GST_DEBUG_OBJECT (convert, "Cropping Enabled with Gles Converter");
  GST_GLES_UNLOCK(convert);
  return TRUE;
}

gboolean
gst_gles_video_converter_process (GstGlesConverter * convert,
    GstVideoFrame * inframes, guint n_inframes, GstVideoFrame * cropframe,
    GstVideoFrame * outframe)
{
  GstMemory *memory = NULL;
  ::QImgConv::Image *inimages = NULL, *cropimage = NULL, *outimage = NULL;
  ::QImgConv::Rec *src_viewports = NULL, *dst_viewports = NULL;
  ::QImgConv::STATUS status = ::QImgConv::STATUS_OK;
  guint n_clips = 0;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inframes != 0), FALSE);
  g_return_val_if_fail (outframe != NULL, FALSE);

  inimages = new (std::nothrow) QImgConv::Image[n_inframes];
  GST_GLES_RETURN_VAL_IF_FAIL (inimages != NULL, FALSE,
      "Failed to allocate memory for input QImgConv::Image!");

  for (guint idx = 0; idx < n_inframes; idx++) {
    memory = gst_buffer_peek_memory (inframes[idx].buffer, 0);

    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (gst_is_fd_memory (memory), FALSE,
        delete inimages, "Input buffer memory is not FD backed!");

    inimages[idx].fd = gst_fd_memory_get_fd (memory);
    inimages[idx].width = GST_VIDEO_FRAME_WIDTH (&inframes[idx]);
    inimages[idx].height = GST_VIDEO_FRAME_HEIGHT (&inframes[idx]);

    inimages[idx].format =
        gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (&inframes[idx]));
    inimages[idx].isLinear = (inimages[idx].width % 128 != 0) ? true : false;

    GST_TRACE ("Input image[%u] with FD %d, dimensions %ux%u and format "
        "%c%c%c%c", idx, inimages[idx].fd, inimages[idx].width,
        inimages[idx].height, DRM_FMT_STRING (inimages[idx].format));
  }

  outimage = new (std::nothrow) ::QImgConv::Image();
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (outimage != NULL, FALSE,
      delete inimages, "Failed to allocate memory for output QImgConv::Image!");

  memory = gst_buffer_peek_memory (outframe->buffer, 0);
  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (gst_is_fd_memory (memory), FALSE,
      delete inimages; delete outimage, "Output buffer memory is not FD backed!");

  outimage->fd = gst_fd_memory_get_fd (memory);
  outimage->width = GST_VIDEO_FRAME_WIDTH (outframe);
  outimage->height = GST_VIDEO_FRAME_HEIGHT (outframe);

  outimage->format =
      gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (outframe));
  outimage->isLinear = (outimage->width % 128 != 0) ? true : false;

  GST_TRACE ("Output image with FD %d, dimensions %ux%u and format %c%c%c%c",
      outimage->fd, outimage->width, outimage->height,
      DRM_FMT_STRING (outimage->format));

  if ((cropframe != NULL) &&
      (GET_OPT_CROP(convert->options, DEFAULT_OPT_CROP) != NULL)) {

    const GValue *rects = (const GValue *)GET_OPT_CROP(convert->options,
        DEFAULT_OPT_CROP);
    n_clips = gst_value_list_get_size(rects);

    cropimage = new (std::nothrow) ::QImgConv::Image();
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (cropimage != NULL, FALSE,
        delete inimages; delete outimage, "Failed to allocate memory for"
        "crop QImgConv::Image!");

    memory = gst_buffer_peek_memory (cropframe->buffer, 0);
    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (gst_is_fd_memory (memory), FALSE,
        delete inimages; delete outimage; delete cropimage, "Crop buffer"
        "memory is not FD backed!");

    cropimage->fd = gst_fd_memory_get_fd (memory);
    cropimage->width = GST_VIDEO_FRAME_WIDTH (cropframe);
    cropimage->height = GST_VIDEO_FRAME_HEIGHT (cropframe);

    cropimage->format =
        gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (cropframe));
    cropimage->isLinear = (cropimage->width % 128 != 0) ? true : false;

    GST_TRACE ("Crop image with FD %d, dimensions %ux%u and format %c%c%c%c",
        cropimage->fd, cropimage->width, cropimage->height,
        DRM_FMT_STRING (cropimage->format));

    src_viewports = new (std::nothrow) QImgConv::Rec[n_clips];

    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (src_viewports != NULL, FALSE,
        delete inimages; delete outimage; delete cropimage, "Failed to"
        " allocate memory for src viewports QImgConv::Rec!");

    dst_viewports = new (std::nothrow) QImgConv::Rec[n_clips];

    GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (dst_viewports != NULL, FALSE,
        delete inimages; delete outimage; delete cropimage;
        delete src_viewports, "Failed to allocate memory for dst viewports"
        " QImgConv::Rec!");

    for (uint idx = 0; idx < n_clips; idx++) {
        const GValue *rect = gst_value_list_get_value (rects, idx);

        src_viewports[idx].x =
            g_value_get_int (gst_value_array_get_value (rect, 0));
        src_viewports[idx].y =
            g_value_get_int (gst_value_array_get_value (rect, 1));
        src_viewports[idx].width =
            g_value_get_int (gst_value_array_get_value (rect, 2));
        src_viewports[idx].height =
            g_value_get_int (gst_value_array_get_value (rect, 3));

        dst_viewports[idx].x = 0;
        dst_viewports[idx].y = (GST_VIDEO_FRAME_HEIGHT (cropframe)*idx)/n_clips;
        dst_viewports[idx].width = GST_VIDEO_FRAME_WIDTH (cropframe);
        dst_viewports[idx].height = GST_VIDEO_FRAME_HEIGHT (cropframe)/n_clips;
    }
  }

  GST_GLES_LOCK (convert);

  if ((cropimage!= NULL) &&
      (GET_OPT_CROP(convert->options, DEFAULT_OPT_CROP)!= NULL)) {
  // When Crop operation is set, always a single inframe is passed
    status = convert->engine->Clip (inimages[0], src_viewports, dst_viewports,
        cropimage, n_clips, true);

    if (status == ::QImgConv::STATUS_OK)
    // When cropping n_inframes is 1
      status =  convert->engine->DoPreProcess (cropimage, outimage, 1, 1);

    delete cropimage;
    delete src_viewports;
    delete dst_viewports;

  } else {
  // Pass down batch size the same as number of inframes to DoPreProcess call.
    status = convert->engine->DoPreProcess (inimages, outimage, n_inframes,
        n_inframes);
  }

  GST_GLES_UNLOCK (convert);

  GST_GLES_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == ::QImgConv::STATUS_OK,
      FALSE, delete inimages; delete outimage, "Failed to process frames!");

  GST_LOG ("Processed output image with FD %d", outimage->fd);

  delete inimages;
  delete outimage;

  return TRUE;
}
