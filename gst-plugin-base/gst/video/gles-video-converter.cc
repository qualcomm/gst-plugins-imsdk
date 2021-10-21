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

#include <adreno/image_convert.h>
#include <drm/drm_fourcc.h>
#include <gbm.h>
#include <gbm_priv.h>
#include <gst/allocators/gstfdmemory.h>

#define RETURN_NULL_IF_FAIL_WITH_MSG(expr, cleanup, ...) \
    if (!(expr)) { \
        GST_ERROR(__VA_ARGS__); \
        cleanup; \
        return NULL; \
    }

#define DEFAULT_OPT_RESIZE_WIDTH  0
#define DEFAULT_OPT_RESIZE_HEIGHT 0
#define DEFAULT_OPT_RSCALE      1.0/255.0
#define DEFAULT_OPT_GSCALE      1.0/255.0
#define DEFAULT_OPT_BSCALE      1.0/255.0
#define DEFAULT_OPT_ASCALE      1.0/255.0
#define DEFAULT_OPT_QSCALE      1.0/255.0
#define DEFAULT_OPT_ROFFSET     0.0
#define DEFAULT_OPT_GOFFSET     0.0
#define DEFAULT_OPT_BOFFSET     0.0
#define DEFAULT_OPT_AOFFSET     0.0
#define DEFAULT_OPT_QOFFSET    -128.0
#define DEFAULT_OPT_DEST_WIDTH  0
#define DEFAULT_OPT_DEST_HEIGHT 0
#define DEFAULT_OPT_DEST_X      0
#define DEFAULT_OPT_DEST_Y      0

#define DEFAULT_OPT_RESIZE      FALSE
#define DEFAULT_OPT_NORMALIZE   FALSE
#define DEFAULT_OPT_QUANTIZE    FALSE
#define DEFAULT_OPT_CONVERT_TO_UINT8 FALSE
#define DEFAULT_OPT_DEST   FALSE

#define GST_CAT_DEFAULT ensure_debug_category()

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
#define GET_OPT_CONVERT_TO_UINT8(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_CONVERT_TO_UINT8, v)
#define GET_OPT_DEST(s, v) get_opt_boolean (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST, v)
#define GET_OPT_DEST_X(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_X, v)
#define GET_OPT_DEST_Y(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_Y, v)
#define GET_OPT_DEST_WIDTH(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_WIDTH, v)
#define GET_OPT_DEST_HEIGHT(s, v) get_opt_int (s, \
    GST_GLES_VIDEO_CONVERTER_OPT_DEST_HEIGHT, v)

#define GST_GLES_CONVERTER_GET_LOCK(obj) (&((GstGlesConverter *)obj)->lock)
#define GST_GLES_CONVERTER_LOCK(obj) \
    g_mutex_lock (GST_GLES_CONVERTER_GET_LOCK(obj))
#define GST_GLES_CONVERTER_UNLOCK(obj) \
    g_mutex_unlock (GST_GLES_CONVERTER_GET_LOCK(obj))

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

static gboolean
update_options (GQuark field, const GValue * value, gpointer userdata)
{
  gst_structure_id_set_value (GST_STRUCTURE_CAST (userdata), field, value);
  return TRUE;
}

//Instantiate the DataConverter object for the pipeline
GstGlesConverter *
gst_gles_converter_new ()
{
  GstGlesConverter* convert = NULL;
  convert = g_slice_new0(GstGlesConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  if ((convert->options = gst_structure_new_empty ("GlesConverterOpts"))
        == NULL) {
    g_slice_free (GstGlesConverter, convert);
    GST_ERROR ("Failed to create Gles Converter options!");
    return NULL;
  }

  GST_INFO ("Created Gles Converter object %p", convert);
  return convert;
}

//gst_gles_converter_set_ops: configure the conversion pipeline with the
//parameter settings passed through opts structure.
gboolean
gst_gles_converter_set_ops (GstGlesConverter * convert, GstStructure * opts)
{
  ::QImgConv::Rec rectangle;
  ::QImgConv::Color color = {0.0,0.0,0.0,0.0};
  std::vector<std::string> composition;
  gint width, height, rect_width, rect_height;
  gint rect_x, rect_y;
  gfloat rscale, gscale, bscale, ascale, qscale;
  gfloat roffset, goffset, boffset, aoffset, qoffset;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // locking the converter to set the opts and composition pipeline
  GST_GLES_CONVERTER_LOCK(convert);

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options, convert->options);
  gst_structure_free (opts);

  width  = GET_OPT_RESIZE_WIDTH (convert->options, DEFAULT_OPT_RESIZE_WIDTH);
  height = GET_OPT_RESIZE_HEIGHT (convert->options, DEFAULT_OPT_RESIZE_HEIGHT);
  rect_x =
    GET_OPT_DEST_X (convert->options, DEFAULT_OPT_DEST_X);
  rect_y =
    GET_OPT_DEST_Y (convert->options, DEFAULT_OPT_DEST_Y);
  rect_width  =
    GET_OPT_DEST_WIDTH (convert->options, DEFAULT_OPT_DEST_WIDTH);
  rect_height =
    GET_OPT_DEST_HEIGHT (convert->options, DEFAULT_OPT_DEST_HEIGHT);

  if (width == 0 || height == 0) {
    GST_ERROR ("Failed to set valid opts. Found Width:%d Height:%d",
        width, height);
    GST_GLES_CONVERTER_UNLOCK (convert);
    return FALSE;
  }

  if (rect_width == 0 || rect_height == 0) {
    GST_ERROR ("Failed to set valid opts. Found destination Width:%d Height:%d",
        rect_width, rect_height);
    GST_GLES_CONVERTER_UNLOCK (convert);
    return FALSE;
  }

  // Create DataConverter here to retain thread context for Compose and
  // DoPreProcess call
  if ((convert->engine = new ::QImgConv::DataConverter()) == NULL) {
    GST_ERROR ("Failed to create Gles DataConverter!");
    GST_GLES_CONVERTER_UNLOCK (convert);
    return FALSE;
  }

  GST_DEBUG ("Created Gles DataConverter object %p", convert->engine);

  rscale    = GET_OPT_RSCALE (convert->options, DEFAULT_OPT_RSCALE);
  gscale    = GET_OPT_GSCALE (convert->options, DEFAULT_OPT_RSCALE);
  bscale    = GET_OPT_BSCALE (convert->options, DEFAULT_OPT_RSCALE);
  ascale    = GET_OPT_ASCALE (convert->options, DEFAULT_OPT_ASCALE);
  qscale    = GET_OPT_QSCALE (convert->options, DEFAULT_OPT_QSCALE);
  roffset   = GET_OPT_ROFFSET (convert->options, DEFAULT_OPT_ROFFSET);
  goffset   = GET_OPT_GOFFSET (convert->options, DEFAULT_OPT_GOFFSET);
  boffset   = GET_OPT_BOFFSET (convert->options, DEFAULT_OPT_BOFFSET);
  aoffset   = GET_OPT_AOFFSET (convert->options, DEFAULT_OPT_AOFFSET);
  qoffset   = GET_OPT_ROFFSET (convert->options, DEFAULT_OPT_QOFFSET);

  if (GET_OPT_RESIZE (convert->options, DEFAULT_OPT_RESIZE))
    composition.push_back (convert->engine->Resize (width, height));

  if (GET_OPT_NORMALIZE (convert->options, DEFAULT_OPT_NORMALIZE))
    composition.push_back (convert->engine->Normalize (rscale, gscale, bscale,
        ascale, roffset, goffset, boffset, aoffset));

  if (GET_OPT_DEST (convert->options, DEFAULT_OPT_DEST)) {
    rectangle.x = rect_x;
    rectangle.y = rect_y;
    rectangle.width  = rect_width;
    rectangle.height = rect_height;
    composition.push_back (convert->engine->Letterbox (rectangle, color));
  }

  if (GET_OPT_QUANTIZE (convert->options, DEFAULT_OPT_QUANTIZE))
    composition.push_back (convert->engine->Quantize (qscale, qoffset));

  if (GET_OPT_CONVERT_TO_UINT8 (convert->options, DEFAULT_OPT_CONVERT_TO_UINT8))
    composition.push_back (convert->engine->ConverttoUINT8());

  for (auto op = composition.begin(); op != composition.end(); op++)
    GST_DEBUG ("Composing DataConverter with %s", (*op).c_str());

  if (convert->engine->Compose(composition) != ::QImgConv::STATUS_OK) {
    GST_ERROR ("Failed to compose the Gles engine operations!");
    GST_GLES_CONVERTER_UNLOCK (convert);
    return FALSE;
  }

  GST_GLES_CONVERTER_UNLOCK (convert);
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

//gst_gles_converter_process: Use convert and perform the preprocessing on the
//input images in batches. Each output Image is result of preprocessing
gboolean
gst_gles_converter_process (GstGlesConverter * convert,
    GstVideoFrame * inframes, guint n_inframes, GstVideoFrame * outframe)
{
  ::QImgConv::Image *inimages, *outimage;
  ::QImgConv::STATUS status = ::QImgConv::STATUS_OK;
  GstMemory *memory = NULL;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inframes != 0), FALSE);
  g_return_val_if_fail (outframe != NULL, FALSE);

  inimages = g_new0 (::QImgConv::Image, n_inframes);

  if (inimages == NULL) {
    GST_ERROR ("Couldn't allocate image objs for inframes. End of processing");
    return FALSE;
  }

  for (guint idx = 0; idx < n_inframes; idx++) {
    GST_INFO ("Conversion on Frame %p", &inframes[idx]);
    memory = gst_buffer_peek_memory (inframes[idx].buffer, 0);

    if (!gst_is_fd_memory (memory)) {
      GST_ERROR ("In Buffer memory not fd backed memory");
      g_free (inimages);
      return FALSE;
    }

    inimages[idx].fd = gst_fd_memory_get_fd (memory);
    inimages[idx].width = GST_VIDEO_FRAME_WIDTH (&inframes[idx]);
    inimages[idx].height = GST_VIDEO_FRAME_HEIGHT (&inframes[idx]);
    inimages[idx].format =
        gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (&inframes[idx]));
  }

  outimage = g_new0 (::QImgConv::Image, 1);

  if (outimage == NULL) {
    GST_ERROR ("Unable to create Engine output Image objects");
    g_free (inimages);
    return FALSE;
  }

  memory = gst_buffer_peek_memory (outframe->buffer, 0);

  if (!gst_is_fd_memory (memory)) {
    GST_ERROR ("Out Buffer memory not fd backed memory");
    g_free (inimages);
    g_free (outimage);
    return FALSE;
    }

  outimage->fd = gst_fd_memory_get_fd (memory);
  outimage->width = GST_VIDEO_FRAME_WIDTH (outframe);
  outimage->height = GST_VIDEO_FRAME_HEIGHT (outframe);
  outimage->format =
      gst_video_format_to_drm_format (GST_VIDEO_FRAME_FORMAT (outframe));

  GST_GLES_CONVERTER_LOCK (convert);
  GST_DEBUG ("Using Gles DataConverter object %p for preprocessing",
     convert->engine);
  // Pass down batch size the same as number of inframes to DoPreProcess call
  status = convert->engine->DoPreProcess (inimages, outimage, n_inframes,
      n_inframes);
  GST_GLES_CONVERTER_UNLOCK (convert);

  if (status != ::QImgConv::STATUS_OK) {
    GST_ERROR ("Unsuccessful processing operation");
    g_free (inimages);
    g_free (outimage);
    return FALSE;
  }

  GST_INFO ("Transformed to Outframe %p", outframe);
  return TRUE;
}

//gst_gles_converter_free: Cleanup the gles_convert handle
void
gst_gles_converter_free (GstGlesConverter * convert)
{
  GST_LOG("Cleaning up GstGlesConverter");

  if (convert == NULL)
    return;

  gst_structure_free (convert->options);

  if (convert->engine != NULL)
    delete convert->engine;

  GST_LOG ("Freed Gles Converter %p", convert);
  g_slice_free (GstGlesConverter, convert);
}
