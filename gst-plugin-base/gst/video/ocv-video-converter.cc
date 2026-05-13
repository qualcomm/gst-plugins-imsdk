/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocv-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>

#include <opencv4/opencv2/opencv.hpp>

// Version-specific format conversions
#define GST_OCV_COLOR_RGB2YUV_UYVY 143 // cv::COLOR_RGB2YUV_UYVY = 143
#define GST_OCV_COLOR_BGR2YUV_UYVY 144 // cv::COLOR_BGR2YUV_UYVY = 144
#define GST_OCV_COLOR_RGBA2YUV_UYVY 145 // cv::COLOR_RGBA2YUV_UYVY = 145
#define GST_OCV_COLOR_BGRA2YUV_UYVY 146 // cv::COLOR_BGRA2YUV_UYVY = 146
#define GST_OCV_COLOR_RGB2YUV_YUY2 147 // cv::COLOR_RGB2YUV_YUY2 = 147
#define GST_OCV_COLOR_BGR2YUV_YUY2 148 // cv::COLOR_BGR2YUV_YUY2 = 148
#define GST_OCV_COLOR_RGB2YUV_YVYU 149 // cv::COLOR_RGB2YUV_YVYU = 149
#define GST_OCV_COLOR_BGR2YUV_YVYU 150 // cv::COLOR_BGR2YUV_YVYU = 150
#define GST_OCV_COLOR_RGBA2YUV_YUY2 151 // cv::COLOR_RGBA2YUV_YUY2 = 151
#define GST_OCV_COLOR_BGRA2YUV_YUY2 152 // cv::COLOR_BGRA2YUV_YUY2 = 152
#define GST_OCV_COLOR_RGBA2YUV_YVYU 153 // cv::COLOR_RGBA2YUV_YVYU = 153
#define GST_OCV_COLOR_BGRA2YUV_YVYU 154 // cv::COLOR_BGRA2YUV_YVYU = 154

#define GST_CAT_DEFAULT gst_video_converter_engine_debug

// Convinient macros for printing plane values.
#define GST_OCV_PLANE_FORMAT "ux%u Stride[%u] Data[%p]"
#define GST_OCV_PLANE_ARGS(plane) \
    (plane)->width, (plane)->height, (plane)->stride, (plane)->data

#define EXTRACT_RED_VALUE(color)    ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_VALUE(color)  ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_VALUE(color)   ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_VALUE(color)  ((color) & 0xFF)

#define GST_OCV_GET_LOCK(obj)       (&((GstOcvVideoConverter *)obj)->lock)
#define GST_OCV_LOCK(obj)           g_mutex_lock (GST_OCV_GET_LOCK(obj))
#define GST_OCV_UNLOCK(obj)         g_mutex_unlock (GST_OCV_GET_LOCK(obj))

#define GST_OCV_INVALID_STAGE_ID    (-1)
#define GST_OCV_MAX_DRAW_OBJECTS    50

#define GST_OCV_INVALID_CONVERSION  (-1)
#define GST_OCV_EXTERNAL_CONVERSION (cv::COLOR_COLORCVT_MAX)

#define GST_OCV_NEUTRAL_CHROMA      128
#define GST_OCV_FALLBACK_FORMAT     GST_VIDEO_FORMAT_NV12

#define FORMAT_IS_PACKED(format)    (format == GST_VIDEO_FORMAT_YUY2 || \
    format == GST_VIDEO_FORMAT_UYVY || format == GST_VIDEO_FORMAT_YVYU)

#define GST_OCV_OBJ_IS_YUV(obj)     (obj && (obj->flags & GST_OCV_FLAG_YUV))
#define GST_OCV_OBJ_IS_RGB(obj)     (obj && (obj->flags & GST_OCV_FLAG_RGB))
#define GST_OCV_OBJ_IS_GRAY(obj)    (obj && (obj->flags & GST_OCV_FLAG_GRAY))

#define GST_OCV_GET_FLIP(flip) \
    ((flip == GST_OCV_FLIP_HORIZONTAL) ? 1 : \
    ((flip == GST_OCV_FLIP_VERTICAL) ? 0 : \
    ((flip == GST_OCV_FLIP_BOTH) ? (-1) : 0)))

#define GST_OCV_GET_ROTATE(rotate) \
    ((rotate == GST_VCE_ROTATE_90) ? cv::ROTATE_90_CLOCKWISE : \
    ((rotate == GST_VCE_ROTATE_180) ? cv::ROTATE_180 : \
    ((rotate == GST_VCE_ROTATE_270) ? cv::ROTATE_90_COUNTERCLOCKWISE : 0)))

typedef struct _GstOcvPlane GstOcvPlane;
typedef struct _GstOcvObject GstOcvObject;
typedef struct _GstOcvStageBuffer GstOcvStageBuffer;

// Custom format conversions
enum {
  // NV12/NV21 -> NV12/NV21
  GST_OCV_COLOR_NV12_to_NV21 = (cv::COLOR_COLORCVT_MAX + 1),
  GST_OCV_COLOR_NV21_to_NV12 = (cv::COLOR_COLORCVT_MAX + 2),
  // NV12/NV21 -> I420
  GST_OCV_COLOR_NV12_to_I420 = (cv::COLOR_COLORCVT_MAX + 3),
  GST_OCV_COLOR_NV21_to_I420 = (cv::COLOR_COLORCVT_MAX + 4),
  // NV12/NV21 -> YV12
  GST_OCV_COLOR_NV12_to_YV12 = (cv::COLOR_COLORCVT_MAX + 5),
  GST_OCV_COLOR_NV21_to_YV12 = (cv::COLOR_COLORCVT_MAX + 6),
  // NV12/NV21 -> YUY2 (YUYV =
  GST_OCV_COLOR_NV12_to_YUY2 = (cv::COLOR_COLORCVT_MAX + 7),
  GST_OCV_COLOR_NV21_to_YUY2 = (cv::COLOR_COLORCVT_MAX + 8),
  // NV12/NV21 -> UYVY
  GST_OCV_COLOR_NV12_to_UYVY = (cv::COLOR_COLORCVT_MAX + 9),
  GST_OCV_COLOR_NV21_to_UYVY = (cv::COLOR_COLORCVT_MAX + 10),
  // NV12/NV21 -> YVYU
  GST_OCV_COLOR_NV12_to_YVYU = (cv::COLOR_COLORCVT_MAX + 11),
  GST_OCV_COLOR_NV21_to_YVYU = (cv::COLOR_COLORCVT_MAX + 12),
  // I420 -> NV12/NV21
  GST_OCV_COLOR_I420_to_NV12 = (cv::COLOR_COLORCVT_MAX + 13),
  GST_OCV_COLOR_I420_to_NV21 = (cv::COLOR_COLORCVT_MAX + 14),
  // YV12 -> NV12/NV21
  GST_OCV_COLOR_YV12_to_NV12 = (cv::COLOR_COLORCVT_MAX + 15),
  GST_OCV_COLOR_YV12_to_NV21 = (cv::COLOR_COLORCVT_MAX + 16),
  // YUY2 (YUYV)-> NV12/NV21
  GST_OCV_COLOR_YUY2_to_NV12 = (cv::COLOR_COLORCVT_MAX + 17),
  GST_OCV_COLOR_YUY2_to_NV21 = (cv::COLOR_COLORCVT_MAX + 18),
  // UYVY-> NV12/NV21
  GST_OCV_COLOR_UYVY_to_NV12 = (cv::COLOR_COLORCVT_MAX + 19),
  GST_OCV_COLOR_UYVY_to_NV21 = (cv::COLOR_COLORCVT_MAX + 20),
  // YVYU-> NV12/NV21
  GST_OCV_COLOR_YVYU_to_NV12 = (cv::COLOR_COLORCVT_MAX + 21),
  GST_OCV_COLOR_YVYU_to_NV21 = (cv::COLOR_COLORCVT_MAX + 22),
  // RGB/BGR -> NV12/NV21
  GST_OCV_COLOR_RGB_to_NV12 = (cv::COLOR_COLORCVT_MAX + 23),
  GST_OCV_COLOR_RGB_to_NV21 = (cv::COLOR_COLORCVT_MAX + 24),
  GST_OCV_COLOR_BGR_to_NV12 = (cv::COLOR_COLORCVT_MAX + 25),
  GST_OCV_COLOR_BGR_to_NV21 = (cv::COLOR_COLORCVT_MAX + 26),
  // RGBA/BGRA -> NV12/NV21
  GST_OCV_COLOR_RGBA_to_NV12 = (cv::COLOR_COLORCVT_MAX + 27),
  GST_OCV_COLOR_RGBA_to_NV21 = (cv::COLOR_COLORCVT_MAX + 28),
  GST_OCV_COLOR_BGRA_to_NV12 = (cv::COLOR_COLORCVT_MAX + 29),
  GST_OCV_COLOR_BGRA_to_NV21 = (cv::COLOR_COLORCVT_MAX + 30),
  // GRAY -> NV12/NV21
  GST_OCV_COLOR_GRAY_to_NV12 = (cv::COLOR_COLORCVT_MAX + 31),
  GST_OCV_COLOR_GRAY_to_NV21 = (cv::COLOR_COLORCVT_MAX + 32),
  // GRAY -> I420
  GST_OCV_COLOR_GRAY_to_I420 = (cv::COLOR_COLORCVT_MAX + 33),
  // GRAY -> YV12
  GST_OCV_COLOR_GRAY_to_YV12 = (cv::COLOR_COLORCVT_MAX + 34),
  // GRAY -> YUY2
  GST_OCV_COLOR_GRAY_to_YUY2 = (cv::COLOR_COLORCVT_MAX + 35),
  // GRAY -> UYVY
  GST_OCV_COLOR_GRAY_to_UYVY = (cv::COLOR_COLORCVT_MAX + 36),
  // GRAY -> YVYU
  GST_OCV_COLOR_GRAY_to_YVYU = (cv::COLOR_COLORCVT_MAX + 37),
};

enum {
  GST_OCV_FLAG_GRAY   = (1 << 0),
  GST_OCV_FLAG_RGB    = (1 << 1),
  GST_OCV_FLAG_YUV    = (1 << 2),
  GST_OCV_FLAG_STAGED = (1 << 3),
};

typedef enum {
  GST_OCV_FLIP_NONE,
  GST_OCV_FLIP_HORIZONTAL,
  GST_OCV_FLIP_VERTICAL,
  GST_OCV_FLIP_BOTH
} GstOpenCVFlip;

/**
 * GstOcvPlane:
 * @stgid: Index of the used staging buffer or -1 if created from original frame.
 * @width: Width of the plane in pixels.
 * @height: Height of the plane in pixels.
 * @data: Pointer to bytes of data.
 * @stride: Aligned width of the plane in bytes.
 * @type: The OpenCV matrix type of the plane.
 *
 * Blit plane.
 */
struct _GstOcvPlane
{
  gint     stgid;
  guint32  width;
  guint32  height;
  gpointer data;
  guint32  stride;
  gint     type;
};

/**
 * GstOcvObject:
 * @format: Gstreamer video format.
 * @flags: Bit mask containing format family.
 * @flip: Flip direction or 0 if none.
 * @rotate: Clockwise rotation degrees or 0 if none.
 * @resize: Whether the object needs to be upscaled or downscaled.
 * @planes: Array of blit planes.
 * @n_planes: Number of used planes based on format.
 *
 * Blit object.
 */
struct _GstOcvObject
{
  GstVideoFormat     format;
  guint32            flags;

  GstOpenCVFlip      flip;
  GstVideoConvRotate rotate;
  gboolean           resize;

  GstOcvPlane        planes[GST_VIDEO_MAX_PLANES];
  guint8             n_planes;
};

/**
 * GstOcvStageBuffer:
 * @idx: Index of in the staging list.
 * @data: Pointer to bytes of data.
 * @size: Total number of bytes.
 * @used: Whether the buffer is currently used by some operaion.
 *
 * Blit staging buffer.
 */
struct _GstOcvStageBuffer
{
  guint    idx;
  gpointer data;
  guint    size;
  gboolean used;
};

struct _GstOcvVideoConverter
{
  // Global mutex lock.
  GMutex   lock;

  // Staging buffers used as intermediaries during the OpenCV operations.
  GArray   *stgbufs;
};

static inline void
gst_ocv_stage_buffer_free (gpointer data)
{
  GstOcvStageBuffer *buffer = (GstOcvStageBuffer *) data;
  g_free (buffer->data);
}

static inline guint
gst_ocv_regions_overlapping_area (GstVideoRectangle * l_rect,
    GstVideoRectangle * r_rect)
{
  gint width = 0, height = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN ((l_rect->x + l_rect->w), (r_rect->x + r_rect->w));
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_rect->x, r_rect->x);

  // Negative width means that there is no overlapping, zero the value.
  width = (width < 0) ? 0 : width;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN ((l_rect->y + l_rect->h), (r_rect->y + r_rect->h));
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_rect->y, r_rect->y);

  // Negative height means that there is no overlapping, zero the value.
  height = (height < 0) ? 0 : height;

  return (width * height);
}

static inline guint
gst_ocv_composition_blit_area (GstVideoFrame * outframe, GstVideoBlit * blits,
    guint index)
{
  GstVideoBlit *blit = NULL;
  GstVideoRectangle *region = NULL, *l_region = NULL;
  guint num = 0, area = 0;

  // Fetch the blit at current index to which we will compare all others.
  blit = &(blits[index]);

  // If there are no destination region then the whole frame is the region.
  if ((blit->destination.w == 0) || (blit->destination.h == 0))
    return GST_VIDEO_FRAME_WIDTH (outframe) * GST_VIDEO_FRAME_HEIGHT (outframe);

  // Calculate the destination area filled with frame content.
  region = &(blit->destination);
  area = region->w * region->h;

  // Iterate destination region for each blit and subtract overlapping area.
  for (num = 0; num < index; num++) {
    // Subtract overlapping are of the destination regions in that blit object.
    l_region = &(blits[num].destination);
    area -= gst_ocv_regions_overlapping_area (region, l_region);
  }

  return area;
}

static inline void
gst_ocv_copy_object (GstOcvObject * l_object, GstOcvObject * r_object)
{
  guint idx = 0;

  r_object->n_planes = l_object->n_planes;

  for (idx = 0; idx < r_object->n_planes; idx++)
    r_object->planes[idx] = l_object->planes[idx];

  r_object->format = l_object->format;
  r_object->flags = l_object->flags;

  r_object->flip = l_object->flip;
  r_object->rotate = l_object->rotate;
}

static inline void
gst_ocv_update_object (GstOcvObject * object, const gchar * type,
    const GstVideoFrame * frame, const GstVideoRectangle * region,
    const GstOpenCVFlip flip, const GstVideoConvRotate rotate,
    const guint64 datatype)
{
  const gchar *mode = NULL;
  gint x = 0, y = 0, width = 0, height = 0, bpp = 0;

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  // Clip out of bounds regions so they don't go outside the full frame.
  x = MAX (region->x, 0);
  y = MAX (region->y, 0);
  width = GST_ROUND_DOWN_2 (MIN (((region->x + region->w) - x), (width - x)));
  height = GST_ROUND_DOWN_2 (MIN (((region->y + region->h) - y), (height - y)));

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

  GST_TRACE ("%s Buffer %p - %ux%u %s%s", type, frame->buffer,
      GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
      gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)), mode);
  GST_TRACE ("%s Buffer %p - Plane 0: Stride[%u] Data[%p]", type,
      frame->buffer, GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 0));
  GST_TRACE ("%s Buffer %p - Plane 1: Stride[%u] Data[%p]", type,
      frame->buffer, GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 1));
  GST_TRACE ("%s Buffer %p - Plane 2: Stride[%u] Data[%p]", type,
      frame->buffer, GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 2));
  GST_TRACE ("%s Buffer %p - Region: (%d - %d) %dx%d", type, frame->buffer,
      x, y, width, height);

  if (GST_VIDEO_INFO_IS_YUV (&(frame->info)))
    object->flags |= GST_OCV_FLAG_YUV;
  else if (GST_VIDEO_INFO_IS_RGB (&(frame->info)))
    object->flags |= GST_OCV_FLAG_RGB;
  else if (GST_VIDEO_INFO_IS_GRAY (&(frame->info)))
    object->flags |= GST_OCV_FLAG_GRAY;

  object->flip = flip;
  object->rotate = rotate;

  object->format = GST_VIDEO_FRAME_FORMAT (frame);
  object->n_planes = GST_VIDEO_FRAME_N_PLANES (frame);

  // Initialize the mandatory first plane.
  object->planes[0].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  // Reduce object stride to equivalent UINT8 as engine cannot operate otherwise.
  // Normalization to end pixel type will be done after all other operations.
  if (datatype == GST_VCE_DATA_TYPE_U16 || datatype == GST_VCE_DATA_TYPE_I16 ||
      datatype == GST_VCE_DATA_TYPE_F16)
    object->planes[0].stride /= 2;
  else if (datatype == GST_VCE_DATA_TYPE_U32 || datatype == GST_VCE_DATA_TYPE_I32 ||
      datatype == GST_VCE_DATA_TYPE_F32)
    object->planes[0].stride /= 4;
  else if (datatype == GST_VCE_DATA_TYPE_U64 || datatype == GST_VCE_DATA_TYPE_I64)
    object->planes[0].stride /= 8;

  object->planes[0].width = width;
  object->planes[0].height = height;

  bpp = GST_VIDEO_INFO_COMP_PSTRIDE (&(frame->info), 0);

  // Add the offset to the region of interest to the data pointer.
  object->planes[0].data =
      (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0) +
          (y * object->planes[0].stride) + x * bpp);

  object->planes[0].stgid = GST_OCV_INVALID_STAGE_ID;

  if (GST_OCV_OBJ_IS_YUV (object) || GST_OCV_OBJ_IS_GRAY (object))
    object->planes[0].type = CV_8UC1;
  else if (GST_OCV_OBJ_IS_RGB (object))
    object->planes[0].type = CV_8UC3;

  // Initialize the secondary plane depending on the format.
  switch (object->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = object->planes[0].width / 2;
      object->planes[1].height = object->planes[0].height / 2;
      object->planes[1].type = CV_8UC2;

      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[1].stride) +
                  GST_ROUND_UP_2 (x));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = object->planes[0].width / 2;
      object->planes[1].height = object->planes[0].height;
      object->planes[1].type = CV_8UC2;

      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              (y * object->planes[1].stride) + GST_ROUND_UP_2 (x));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_NV24:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = object->planes[0].width * 2;
      object->planes[1].height = object->planes[0].height;
      object->planes[1].type = CV_8UC2;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              (y * object->planes[1].stride) + (x * 2));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = object->planes[0].width / 2;
      object->planes[1].height = object->planes[0].height / 2;
      object->planes[1].type = CV_8UC1;

      object->planes[2].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 2);
      object->planes[2].width = object->planes[1].width;
      object->planes[2].height = object->planes[1].height;
      object->planes[2].type = CV_8UC1;

      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[1].stride) +
                  GST_ROUND_UP_2 (x));
      object->planes[2].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 2) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[2].stride) +
                  GST_ROUND_UP_2 (x));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      object->planes[2].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_YVYU:
      object->planes[0].type = CV_8UC2;
      object->planes[0].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame,0) +
              (y * object->planes[0].stride) + (x * 2));
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      // Update plane 0 offset.
      object->planes[0].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0) +
              (y * object->planes[0].stride) + x * 2);
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = object->planes[0].width;
      object->planes[1].height = object->planes[0].height / 2;
      object->planes[1].type = CV_8UC2;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[1].stride) + (x * 2));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      object->planes[0].type = CV_8UC4;
    default:
      // No need for initialize anything in the secondary plane.
      break;
  }

  GST_TRACE ("%s Buffer %p - Object Format: %s%s", type, frame->buffer,
      gst_video_format_to_string (object->format), mode);
  for (guint idx = 0; idx < object->n_planes; idx++) {
    GST_TRACE ("%s Buffer %p - Object Plane %d: %" GST_OCV_PLANE_FORMAT, type,
        frame->buffer, idx, GST_OCV_PLANE_ARGS (&(object->planes[idx])));
  }

  return;
}

static inline GstOcvStageBuffer *
gst_ocv_video_converter_fetch_stage_buffer (GstOcvVideoConverter * convert,
    guint size)
{
  GstOcvStageBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < convert->stgbufs->len; idx++) {
    buffer = &(g_array_index (convert->stgbufs, GstOcvStageBuffer, idx));

    // Frame does not have same format and equal or greater dimensions, continue.
    if (buffer->used || (buffer->size < size))
      continue;

    buffer->used = TRUE;

    GST_TRACE ("Using staging buffer at index %u, data %p and size %u",
        buffer->idx, buffer->data, buffer->size);

    return buffer;
  }

  // Increase the number of staged buffer and take a pointer to the new buffer.
  g_array_set_size (convert->stgbufs, convert->stgbufs->len + 1);
  buffer = &(g_array_index (convert->stgbufs, GstOcvStageBuffer, idx));

  buffer->idx = idx;
  buffer->data = g_malloc0 (size);
  buffer->size = size;
  buffer->used = TRUE;

  GST_TRACE ("Allocated staging buffer at index %u, data %p and size %u",
      buffer->idx, buffer->data, buffer->size);

  return buffer;
}

static inline void
gst_ocv_video_converter_release_stage_buffer (GstOcvVideoConverter * convert,
    guint idx)
{
  GstOcvStageBuffer *buffer = NULL;

  buffer = &(g_array_index (convert->stgbufs, GstOcvStageBuffer, idx));
  buffer->used = FALSE;

  GST_TRACE ("Released staging buffer at index %u, data %p and size %u",
      buffer->idx, buffer->data, buffer->size);
}

static inline gboolean
gst_ocv_video_converter_stage_object_init (GstOcvVideoConverter * convert,
    GstOcvObject * obj, guint width, guint height, GstVideoFormat format)
{
  GstOcvStageBuffer *buffer = NULL;
  guint idx = 0, size = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].type = CV_8UC1;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_GRAY;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[0].type = CV_8UC3;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 3;
      obj->planes[0].type = CV_8UC3;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 4;
      obj->planes[0].type = CV_8UC4;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].type = CV_8UC1;
      obj->planes[1].width = width / 2;
      obj->planes[1].height = height / 2;
      obj->planes[1].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].type = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].type = CV_8UC1;
      obj->planes[1].width = width / 2;
      obj->planes[1].height =  height;
      obj->planes[1].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].type = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_NV24:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].type = CV_8UC1;
      obj->planes[1].width = width * 2;
      obj->planes[1].height =  height;
      obj->planes[1].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[1].type = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].type = CV_8UC1;
      obj->planes[1].width = width / 2;
      obj->planes[1].height = height / 2;
      obj->planes[1].stride = GST_ROUND_UP_8 (width) / 2;
      obj->planes[1].type = CV_8UC1;
      obj->planes[2].width = width / 2;
      obj->planes[2].height = height / 2;
      obj->planes[2].stride = GST_ROUND_UP_8 (width) / 2;
      obj->planes[2].type = CV_8UC1;
      obj->n_planes = 3;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_YVYU:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[0].type = CV_8UC2;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      obj->planes[0].width = width;
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[0].type = CV_8UC1;
      obj->planes[1].width = width;
      obj->planes[1].height = height / 2;
      obj->planes[1].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[1].type = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    default:
      GST_ERROR ("Unknown format %s", gst_video_format_to_string (format));
      return FALSE;
  }

  obj->format = format;
  obj->flags |= GST_OCV_FLAG_STAGED;

  obj->flip = GST_OCV_FLIP_NONE;
  obj->rotate = GST_VCE_ROTATE_0;

  // Fetch stage buffer for each plane and set the data pointer and index.
  for (idx = 0; idx < obj->n_planes; idx++) {
    size = GST_ROUND_UP_128 (obj->planes[idx].stride * obj->planes[idx].height);
    buffer = gst_ocv_video_converter_fetch_stage_buffer (convert, size);

    obj->planes[idx].data = buffer->data;
    obj->planes[idx].stgid = buffer->idx;

    GST_TRACE ("Stage Object %s Plane %u: %" GST_OCV_PLANE_FORMAT,
        gst_video_format_to_string (obj->format), idx,
        GST_OCV_PLANE_ARGS (&(obj->planes[idx])));
  }

  return TRUE;
}

static inline void
gst_ocv_video_converter_stage_object_deinit (GstOcvVideoConverter * convert,
    GstOcvObject * obj)
{
  guint num = 0, stgid = 0;

  for (num = 0; num < obj->n_planes; num++) {
    stgid = obj->planes[num].stgid;
    gst_ocv_video_converter_release_stage_buffer (convert, stgid);
  }
}

static inline gint
gst_ocv_get_conversion_mode (GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GST_TRACE ("Obtaining format conversion code: %s to %s",
      gst_video_format_to_string (s_obj->format),
      gst_video_format_to_string (d_obj->format));

  switch (s_obj->format + (d_obj->format << 16)) {
    // NV12/NV21 -> NV21/NV12
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_NV12_to_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_NV21_to_NV12;
    // NV12/NV21 -> I420
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_I420 << 16):
      return GST_OCV_COLOR_NV12_to_I420;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_I420 << 16):
      return GST_OCV_COLOR_NV21_to_I420;
    // NV12/NV21 -> YV12
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_YV12 << 16):
      return GST_OCV_COLOR_NV12_to_YV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_YV12 << 16):
      return GST_OCV_COLOR_NV21_to_YV12;
    // NV12/NV21 -> YUY2
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_NV12_to_YUY2;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_NV21_to_YUY2;
    // NV12/NV21 -> UYVY
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_NV12_to_UYVY;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_NV21_to_UYVY;
    // NV12/NV21 -> YVYU
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_NV12_to_YVYU;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_NV21_to_YVYU;
    // NV12/NV21 -> RGB/BGR
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_NV12;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_NV21;
    // NV12/NV21 -> RGBA/BGRA
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_NV12;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_NV21;
    // NV12/NV21 -> RGBx/BGRx; Might not work.
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_YUV2RGBA_NV12;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_YUV2BGRA_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_YUV2RGBA_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_YUV2BGRA_NV21;
    // NV12/NV21 -> GRAY
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_NV21;
    // I420 -> NV12/NV21
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_I420_to_NV12;
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_I420_to_NV21;
    // I420 -> RGB/BGR
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_I420;
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_I420;
    // I420 -> RGBA/BGRA
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_I420;
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_I420;
    // I420 -> GRAY
    case GST_VIDEO_FORMAT_I420 + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_I420;
    // YV12 -> NV12/NV21
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_YV12_to_NV12;
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_YV12_to_NV21;
    // YV12 -> RGB/BGR
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_YV12;
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_YV12;
    // YV12 -> RGBA/BGRA
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_YV12;
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_YV12;
    // YV12 -> GRAY
    case GST_VIDEO_FORMAT_YV12 + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_YV12;
    // YUY2 (YUYV) -> NV12/NV21
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_YUY2_to_NV12;
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_YUY2_to_NV21;
    // YUY2 (YUYV) -> RGB/BGR
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_YUY2;
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_YUY2;
    // YUY2 (YUYV) -> RGBA/BGRA
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_YUY2;
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_YUY2;
    // YUY2 (YUYV) -> GRAY
    case GST_VIDEO_FORMAT_YUY2 + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_YUY2;
    // UYVY -> NV12/NV21
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_UYVY_to_NV12;
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_UYVY_to_NV21;
    // UYVY -> RGB/BGR
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_UYVY;
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_UYVY;
    // UYVY -> RGBA/BGRA
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_UYVY;
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_UYVY;
    // UYVY -> GRAY
    case GST_VIDEO_FORMAT_UYVY + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_UYVY;
    // YVYU -> NV12/NV21
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_YVYU_to_NV12;
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_YVYU_to_NV21;
    // YVYU -> RGB/BGR
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_YVYU;
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_YVYU;
    // YVYU -> RGBA/BGRA
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_YVYU;
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_YVYU;
    // YVYU -> GRAY
    case GST_VIDEO_FORMAT_YVYU + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_YUV2GRAY_YVYU;
    // RGB/BGR -> NV12/NV21
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_RGB_to_NV12;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_RGB_to_NV21;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_BGR_to_NV12;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_BGR_to_NV21;
    // RGB/BGR -> I420
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_I420 << 16):
      return cv::COLOR_RGB2YUV_I420;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_I420 << 16):
      return cv::COLOR_BGR2YUV_I420;
    // RGB/BGR -> YV12
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_YV12 << 16):
      return cv::COLOR_RGB2YUV_YV12;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_YV12 << 16):
      return cv::COLOR_BGR2YUV_YV12;
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 9)
    // RGB/BGR -> YUY2 (YUYV)
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_YUY2 << 16):
      return cv::COLOR_RGB2YUV_YUY2;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_YUY2 << 16):
      return cv::COLOR_BGR2YUV_YUY2;
    // RGB/BGR -> UYVY
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_UYVY << 16):
      return cv::COLOR_RGB2YUV_UYVY;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_UYVY << 16):
      return cv::COLOR_BGR2YUV_UYVY;
    // RGB/BGR -> YVYU
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_YVYU << 16):
      return cv::COLOR_RGB2YUV_YVYU;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_YVYU << 16):
      return cv::COLOR_BGR2YUV_YVYU;
#else
    // RGB/BGR -> YUY2 (YUYV)
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_RGB2YUV_YUY2;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_BGR2YUV_YUY2;
    // RGB/BGR -> UYVY
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_RGB2YUV_UYVY;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_BGR2YUV_UYVY;
    // RGB/BGR -> YVYU
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_RGB2YUV_YVYU;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_BGR2YUV_YVYU;
#endif // CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 9)
    // RGB/BGR -> BGR/RGB
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_RGB2BGR;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_BGR2RGB;
    // RGB/BGR -> RGBA/BGRA
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_RGB2BGRA;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_RGB2RGBA;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_BGR2BGRA;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_BGR2RGBA;
    // RGB/BGR -> RGBx/BGRx
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_RGB2BGRA;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_RGB2RGBA;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_BGR2BGRA;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_BGR2RGBA;
    // RGB/BGR -> GRAY
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_RGB2GRAY;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_BGR2GRAY;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_RGBA2GRAY;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_GRAY8 << 16):
      return cv::COLOR_BGRA2GRAY;
    // RGBA/BGRA -> NV12/NV21
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_RGBA_to_NV12;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_RGBA_to_NV21;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_BGRA_to_NV12;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_BGRA_to_NV21;
    // RGBA/BGRA -> I420
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_I420 << 16):
      return cv::COLOR_RGBA2YUV_I420;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_I420 << 16):
      return cv::COLOR_BGRA2YUV_I420;
    // RGBA/BGRA -> YV12
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_YV12 << 16):
      return cv::COLOR_RGBA2YUV_YV12;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_YV12 << 16):
      return cv::COLOR_BGRA2YUV_YV12;
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 9)
    // RGBA/BGRA -> YUY2 (YUYV)
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_YUY2 << 16):
      return cv::COLOR_RGBA2YUV_YUY2;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_YUY2 << 16):
      return cv::COLOR_BGRA2YUV_YUY2;
    // RGBA/BGRA -> UYVY
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_UYVY << 16):
      return cv::COLOR_RGBA2YUV_UYVY;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_UYVY << 16):
      return cv::COLOR_BGRA2YUV_UYVY;
    // RGBA/BGRA -> YVYU
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_YVYU << 16):
      return cv::COLOR_RGBA2YUV_YVYU;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_YVYU << 16):
      return cv::COLOR_BGRA2YUV_YVYU;
#else
    // RGBA/BGRA -> YUY2 (YUYV)
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_RGBA2YUV_YUY2;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_BGRA2YUV_YUY2;
    // RGBA/BGRA -> UYVY
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_RGBA2YUV_UYVY;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_BGRA2YUV_UYVY;
    // RGBA/BGRA -> YVYU
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_RGBA2YUV_YVYU;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_BGRA2YUV_YVYU;
#endif // CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 9)
    // RGBA/BGRA -> RGB/BGR
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_RGBA2RGB;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_RGBA2BGR;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_BGRA2RGB;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_BGRA2BGR;
    // RGBA/BGRA -> BGRA/RGBA
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_BGRA2RGBA;
    // RGBA/BGRA -> RGBx/BGRx
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_BGRA2RGBA;
    // RGBx/BGRx -> NV12/NV21
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_RGBA_to_NV12;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_RGBA_to_NV21;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_BGRA_to_NV12;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_BGRA_to_NV21;
    // RGBx/BGRx -> RGB/BGR
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_RGBA2RGB;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_RGBA2BGR;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_BGRA2RGB;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_BGRA2BGR;
    // RGBx/BGRx -> RGBA/BGRA
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_BGRA2RGBA;
    // RGBx/BGRx -> RGBx/BGRx
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_BGRA2RGBA;
    // GRAY -> NV12/NV21
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_NV12 << 16):
      return GST_OCV_COLOR_GRAY_to_NV12;
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_NV21 << 16):
      return GST_OCV_COLOR_GRAY_to_NV21;
    // GRAY -> I420
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_I420 << 16):
      return GST_OCV_COLOR_GRAY_to_I420;
    // GRAY -> YV12
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_YV12 << 16):
      return GST_OCV_COLOR_GRAY_to_YV12;
    // GRAY -> YUY2
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_YUY2 << 16):
      return GST_OCV_COLOR_GRAY_to_YUY2;
    // GRAY -> UYVY
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_UYVY << 16):
      return GST_OCV_COLOR_GRAY_to_UYVY;
    // GRAY -> YVYU
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_YVYU << 16):
      return GST_OCV_COLOR_GRAY_to_YVYU;
    // GRAY -> RGB/BGR
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_GRAY2BGR;
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_GRAY2RGB;
    // GRAY -> RGBA/BGRA
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_GRAY2BGRA;
    case GST_VIDEO_FORMAT_GRAY8 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_GRAY2RGBA;
    default:
      GST_ERROR ("Unsupported format conversion from '%s' to '%s'!",
          gst_video_format_to_string (s_obj->format),
          gst_video_format_to_string (d_obj->format));
      return GST_OCV_INVALID_CONVERSION;
  }
}

static inline gboolean
gst_ocv_video_converter_rotate (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  GstOpenCVFlip flip = GST_OCV_FLIP_NONE;
  guint8 idx = 0;
  gboolean resize = FALSE;

  GST_TRACE ("Performing rotate");

  // Cache the flip, rotation, resize and color convert flags.
  rotate = s_obj->rotate;
  flip = s_obj->flip;
  resize = s_obj->resize;

  // Use stage object if other operations are pending
  if (resize || (flip != GST_OCV_FLIP_NONE) || (s_obj->format != d_obj->format)) {
    guint width = 0, height = 0;
    gboolean success = FALSE;

    width = s_obj->planes[0].width;
    height = s_obj->planes[0].height;

    // Dimensions are swapped if 90/270 degree rotation is required.
    if (rotate == GST_VCE_ROTATE_90 || rotate == GST_VCE_ROTATE_270) {
      width = s_obj->planes[0].height;
      height = s_obj->planes[0].width;
    }

    // Temporary store the destination object data into local intermediary.
    gst_ocv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_ocv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  if ((!GST_OCV_OBJ_IS_YUV (s_obj) && !GST_OCV_OBJ_IS_RGB (s_obj) &&
      !GST_OCV_OBJ_IS_GRAY (s_obj)) ||
      (!GST_OCV_OBJ_IS_YUV (d_obj) && !GST_OCV_OBJ_IS_RGB (d_obj) &&
      !GST_OCV_OBJ_IS_GRAY (d_obj))) {
    GST_WARNING ("Unknown format, or src and dst plane formats don't match!");
    return FALSE;
  }

  for (idx = 0; idx < s_obj->n_planes; idx++) {
    GstOcvPlane *s_plane = &s_obj->planes[idx], *d_plane = &d_obj->planes[idx];

    cv::Mat src_mat (s_plane->height, s_plane->width, s_plane->type,
        s_plane->data, s_plane->stride);

    cv::Mat dst_mat (d_plane->height, d_plane->width, d_plane->type,
        d_plane->data, d_plane->stride);

    cv::rotate (src_mat, dst_mat, GST_OCV_GET_ROTATE (rotate));

    GST_LOG ("Rotated plane No. %u - Src dims: %d (width) x %d (height) @ %d "
        "(stride); Dst dims: %d (width) x %d (height) @ %d (stride)", idx,
        s_plane->width, s_plane->height, s_plane->stride,
        d_plane->width, d_plane->height, d_plane->stride);
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending resize, flip and color convert and reset rotate
  s_obj->flip = flip;
  s_obj->rotate = GST_VCE_ROTATE_0;
  s_obj->resize = resize;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_ocv_video_converter_flip (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  GstOpenCVFlip flip = GST_OCV_FLIP_NONE;
  guint8 idx = 0;
  gboolean resize = FALSE;

  GST_TRACE ("Performing flip");

  // Cache the flip, rotation, resize and color convert flags.
  rotate = s_obj->rotate;
  flip = s_obj->flip;
  resize = s_obj->resize;

  // Use stage object if other operations are pending
  if (resize || (rotate != GST_VCE_ROTATE_0) || (s_obj->format != d_obj->format)) {
    guint width = 0, height = 0;
    gboolean success = FALSE;

    width = s_obj->planes[0].width;
    height = s_obj->planes[0].height;

    // Dimensions are swapped if 90/270 degree rotation is required with resize.
    if (resize && (rotate == GST_VCE_ROTATE_90 ||
        rotate == GST_VCE_ROTATE_270)) {
      width = s_obj->planes[0].height;
      height = s_obj->planes[0].width;
    }

    // Temporary store the destination object data into local intermediary.
    gst_ocv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_ocv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  if ((!GST_OCV_OBJ_IS_YUV (s_obj) && !GST_OCV_OBJ_IS_RGB (s_obj) &&
      !GST_OCV_OBJ_IS_GRAY (s_obj)) ||
      (!GST_OCV_OBJ_IS_YUV (d_obj) && !GST_OCV_OBJ_IS_RGB (d_obj) &&
      !GST_OCV_OBJ_IS_GRAY (d_obj))) {
    GST_WARNING ("Unknown format, or src and dst plane formats don't match!");
    return FALSE;
  }

  for (idx = 0; idx < s_obj->n_planes; idx++) {
    GstOcvPlane *s_plane = &s_obj->planes[idx], *d_plane = &d_obj->planes[idx];

    cv::Mat src_mat (s_plane->height, s_plane->width, s_plane->type,
        s_plane->data, s_plane->stride);

    cv::Mat dst_mat (d_plane->height, d_plane->width, d_plane->type,
        d_plane->data, d_plane->stride);

    cv::flip (src_mat, dst_mat, GST_OCV_GET_FLIP (flip));

    GST_LOG ("Flipped plane No. %u - Src dims: %d (width) x %d (height) @ %d "
        "(stride); Dst dims: %d (width) x %d (height) @ %d (stride)", idx,
        s_plane->width, s_plane->height, s_plane->stride,
        d_plane->width, d_plane->height, d_plane->stride);
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending resize, rotate and color convert and reset flip
  s_obj->flip = GST_OCV_FLIP_NONE;
  s_obj->rotate = rotate;
  s_obj->resize = resize;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_ocv_video_converter_resize (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  GstOpenCVFlip flip = GST_OCV_FLIP_NONE;
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  guint8 idx = 0;

  GST_TRACE ("Performing resize");

  // Cache the flip, rotation, resize and color convert flags.
  flip = s_obj->flip;
  rotate = s_obj->rotate;

  // Use stage object if other operations are pending
  if ((flip != GST_OCV_FLIP_NONE) || (rotate != GST_VCE_ROTATE_0) ||
      (s_obj->format != d_obj->format)) {
    guint width = 0, height = 0;
    gboolean success = FALSE;

    width = d_obj->planes[0].width;
    height = d_obj->planes[0].height;

    // Dimensions are swapped if 90/270 degree rotation is required.
    if (rotate == GST_VCE_ROTATE_90 || rotate == GST_VCE_ROTATE_270) {
      width = d_obj->planes[0].height;
      height = d_obj->planes[0].width;
    }

    // Temporary store the destination object data into local intermediary.
    gst_ocv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_ocv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  if ((!GST_OCV_OBJ_IS_YUV (s_obj) && !GST_OCV_OBJ_IS_RGB (s_obj) &&
      !GST_OCV_OBJ_IS_GRAY (s_obj)) ||
      (!GST_OCV_OBJ_IS_YUV (d_obj) && !GST_OCV_OBJ_IS_RGB (d_obj) &&
      !GST_OCV_OBJ_IS_GRAY (d_obj))) {
    GST_WARNING ("Unknown format!");
    return FALSE;
  }

  for (idx = 0; idx < s_obj->n_planes; idx++) {
    GstOcvPlane *s_plane = &s_obj->planes[idx], *d_plane = &d_obj->planes[idx];

    cv::Mat src_mat (s_plane->height, s_plane->width, s_plane->type,
        s_plane->data, s_plane->stride);

    cv::Mat dst_mat (d_plane->height, d_plane->width, d_plane->type,
        d_plane->data, d_plane->stride);

    cv::resize (src_mat, dst_mat, dst_mat.size(), 0, 0);

    GST_LOG ("Resized plane No. %u - Src dims: %d (width) x %d (height) @ %d "
        "(stride); Dst dims: %d (width) x %d (height) @ %d (stride)", idx,
        s_plane->width, s_plane->height, s_plane->stride,
        d_plane->width, d_plane->height, d_plane->stride);
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending rotate, flip and color convert and reset resize
  s_obj->flip = flip;
  s_obj->rotate = rotate;
  s_obj->resize = FALSE;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline void
gst_ocv_video_converter_copy_plane (const GstOcvPlane * s_plane,
    const GstOcvPlane * d_plane)
{
  cv::Mat input_matrix (s_plane->height, s_plane->width, s_plane->type,
      s_plane->data, s_plane->stride);
  cv::Mat output_matrix (d_plane->height, d_plane->width, d_plane->type,
      d_plane->data, d_plane->stride);

  input_matrix.copyTo (output_matrix);
}

static inline gboolean
gst_ocv_video_converter_standard_conversion (GstOcvObject * s_obj,
    GstOcvObject * d_obj, gint conversion_mode)
{
  switch (s_obj->n_planes) {
    case 1:
    {
      try {
        cv::Mat input_matrix (s_obj->planes[0].height, s_obj->planes[0].width,
            s_obj->planes[0].type, s_obj->planes[0].data, s_obj->planes[0].stride);
        cv::Mat output_matrix (d_obj->planes[0].height, d_obj->planes[0].width,
            d_obj->planes[0].type, d_obj->planes[0].data, d_obj->planes[0].stride);

        cv::cvtColor (input_matrix, output_matrix, conversion_mode);

        return TRUE;
      } catch (const cv::Exception& e) {
        GST_ERROR ("OpenCV encountered error: %s", e.what());
        return FALSE;
      }
    }
    case 2:
    {
      try {
        cv::Mat y_plane (s_obj->planes[0].height, s_obj->planes[0].width,
            s_obj->planes[0].type, s_obj->planes[0].data, s_obj->planes[0].stride);
        cv::Mat uv_plane (s_obj->planes[1].height, s_obj->planes[1].width,
            s_obj->planes[1].type, s_obj->planes[1].data, s_obj->planes[1].stride);
        cv::Mat output_matrix (d_obj->planes[0].height, d_obj->planes[0].width,
            d_obj->planes[0].type, d_obj->planes[0].data, d_obj->planes[0].stride);

        cv::cvtColorTwoPlane (y_plane, uv_plane, output_matrix, conversion_mode);

        return TRUE;
      } catch (const cv::Exception& e) {
        GST_ERROR ("OpenCV encountered error: %s", e.what());
        return FALSE;
      }
    }
    default:
      return FALSE;
  }
}

static inline gboolean
gst_ocv_video_converter_yuv_to_yuv (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  guint32 x_idx = 0, y_idx = 0;

  switch (conversion_mode) {
    case GST_OCV_COLOR_NV12_to_NV21:
    case GST_OCV_COLOR_NV21_to_NV12:
    {
      // Copy Y plane
      gst_ocv_video_converter_copy_plane (&s_obj->planes[0], &d_obj->planes[0]);

      // Swap U and V values for uv plane
      for (y_idx = 0; y_idx < s_obj->planes[1].height; y_idx++) {
        guint8 *src_uv_row = (guint8 *) s_obj->planes[1].data +
            y_idx * s_obj->planes[1].stride;
        guint8 *dest_uv_row = (guint8 *) d_obj->planes[1].data +
            y_idx * d_obj->planes[1].stride;

        for (x_idx = 0; x_idx < 2 * s_obj->planes[1].width; x_idx += 2) {
          dest_uv_row[x_idx] = src_uv_row[x_idx + 1];
          dest_uv_row[x_idx + 1] = src_uv_row[x_idx];
        }
      }

      return TRUE;
    }
    case GST_OCV_COLOR_NV12_to_I420:
    case GST_OCV_COLOR_NV21_to_I420:
    case GST_OCV_COLOR_NV12_to_YV12:
    case GST_OCV_COLOR_NV21_to_YV12:
    {
      // Copy Y plane
      gst_ocv_video_converter_copy_plane (&s_obj->planes[0], &d_obj->planes[0]);

      // Fill U and V planes
      for (y_idx = 0; y_idx < s_obj->planes[1].height; y_idx++) {
        guint8 *src_uv_row = (guint8 *) s_obj->planes[1].data +
            y_idx * s_obj->planes[1].stride;
        guint8 *dest_chroma_row_0 = (guint8 *) d_obj->planes[1].data +
            y_idx * d_obj->planes[1].stride;
        guint8 *dest_chroma_row_1 = (guint8 *) d_obj->planes[2].data +
            y_idx * d_obj->planes[2].stride;

        guint8 *dest_u_row = dest_chroma_row_0;
        guint8 *dest_v_row = dest_chroma_row_1;

        if (d_obj->format == GST_VIDEO_FORMAT_YV12) {
          dest_u_row = dest_chroma_row_1;
          dest_v_row = dest_chroma_row_0;
        }

        for (x_idx = 0; x_idx < 2 * s_obj->planes[1].width; x_idx += 2) {
          const guint8 u = (guint8) src_uv_row[x_idx +
              ((s_obj->format == GST_VIDEO_FORMAT_NV12) ? 0 : 1)];
          const guint8 v = (guint8) src_uv_row[x_idx +
              ((s_obj->format == GST_VIDEO_FORMAT_NV12) ? 1 : 0)];

          dest_u_row[x_idx / 2] = u;
          dest_v_row[x_idx / 2] = v;
        }
      }

      return TRUE;
    }
    case GST_OCV_COLOR_NV12_to_YUY2:
    case GST_OCV_COLOR_NV21_to_YUY2:
    case GST_OCV_COLOR_NV12_to_UYVY:
    case GST_OCV_COLOR_NV21_to_UYVY:
    case GST_OCV_COLOR_NV12_to_YVYU:
    case GST_OCV_COLOR_NV21_to_YVYU:
    {
      // Process in blocks of 2x2 pixels as destination chroma is copied twice (lossy)
      //   Luma      Chroma              YUY2
      // | Y0 Y1 | + | U V |  --->  | Y0 U Y1 V |
      // | Y2 Y3 |            --->  | Y2 U Y3 V |

      //   Luma      Chroma              UYVY
      // | Y0 Y1 | + | U V |  --->  | U Y0 V Y1 |
      // | Y2 Y3 |            --->  | U Y2 V Y3 |

      //   Luma      Chroma              YVYU
      // | Y0 Y1 | + | U V |  --->  | Y0 V Y1 U |
      // | Y2 Y3 |            --->  | Y2 V Y3 U |

      for (y_idx = 0; y_idx < s_obj->planes[0].height; y_idx += 2) {
        const guint8 *y_row_0 = (guint8 *) s_obj->planes[0].data +
            y_idx * s_obj->planes[0].stride;
        const guint8 *y_row_1 = (guint8 *) s_obj->planes[0].data +
            (y_idx + 1) * s_obj->planes[0].stride;
        const guint8 *uv_row = (guint8 *) s_obj->planes[1].data +
            (y_idx / 2) * s_obj->planes[1].stride;
        guint8 *d_row_0 = (guint8 *) d_obj->planes[0].data +
            y_idx * d_obj->planes[0].stride;
        guint8 *d_row_1 = (guint8 *) d_obj->planes[0].data +
            (y_idx + 1) * d_obj->planes[0].stride;

        for (x_idx = 0; x_idx < s_obj->planes[0].width; x_idx += 2) {
          const guint32 row_idx = x_idx * 2;

          const guint8 u = (guint8) uv_row[x_idx +
              ((s_obj->format == GST_VIDEO_FORMAT_NV12) ? 0 : 1)];
          const guint8 v = (guint8) uv_row[x_idx +
              ((s_obj->format == GST_VIDEO_FORMAT_NV12) ? 1 : 0)];

          guint8 *y0 = &d_row_0[row_idx + 0];
          guint8 *u0 = &d_row_0[row_idx + 1];
          guint8 *y1 = &d_row_0[row_idx + 2];
          guint8 *v0 = &d_row_0[row_idx + 3];
          guint8 *y2 = &d_row_1[row_idx + 0];
          guint8 *u1 = &d_row_1[row_idx + 1];
          guint8 *y3 = &d_row_1[row_idx + 2];
          guint8 *v1 = &d_row_1[row_idx + 3];

          if (d_obj->format == GST_VIDEO_FORMAT_UYVY) {
            guint8 *temp = y0;
            y0 = u0;
            u0 = temp;

            temp = y1;
            y1 = v0;
            v0 = temp;

            temp = y2;
            y2 = u1;
            u1 = temp;

            temp = y3;
            y3 = v1;
            v1 = temp;
          } else if (d_obj->format == GST_VIDEO_FORMAT_YVYU) {
            guint8 *temp = u0;
            u0 = v0;
            v0 = temp;

            temp = u1;
            u1 = v1;
            v1 = temp;
          }

          *y0 = y_row_0[x_idx + 0]; // y0
          *u0 = u;                  // u0
          *y1 = y_row_0[x_idx + 1]; // y1
          *v0 = v;                  // v0
          *y2 = y_row_1[x_idx + 0]; // y2
          *u1 = u;                  // u1
          *y3 = y_row_1[x_idx + 1]; // y3
          *v1 = v;                  // v1
        }
      }

      return TRUE;
    }
    case GST_OCV_COLOR_I420_to_NV12:
    case GST_OCV_COLOR_I420_to_NV21:
    case GST_OCV_COLOR_YV12_to_NV12:
    case GST_OCV_COLOR_YV12_to_NV21:
    {
      // Copy Y plane
      gst_ocv_video_converter_copy_plane (&s_obj->planes[0], &d_obj->planes[0]);

      // Fill U and V planes
      for (y_idx = 0; y_idx < s_obj->planes[1].height; y_idx++) {
        guint8 *src_u_row = (guint8 *) s_obj->planes[1].data +
            y_idx * s_obj->planes[1].stride;
        guint8 *src_v_row = (guint8 *) s_obj->planes[2].data +
            y_idx * s_obj->planes[2].stride;
        guint8 *dest_uv_row = (guint8 *) d_obj->planes[1].data +
            y_idx * d_obj->planes[1].stride;

        if (s_obj->format == GST_VIDEO_FORMAT_YV12) {
          guint8 *temp = src_u_row;
          src_u_row = src_v_row;
          src_v_row = temp;
        }

        for (x_idx = 0; x_idx < 2 * s_obj->planes[1].width; x_idx += 2) {
          const guint8 u = (guint8) src_u_row[x_idx / 2];
          const guint8 v = (guint8) src_v_row[x_idx / 2];

          dest_uv_row[x_idx + 0] = (d_obj->format == GST_VIDEO_FORMAT_NV12) ? u : v;
          dest_uv_row[x_idx + 1] = (d_obj->format == GST_VIDEO_FORMAT_NV12) ? v : u;
        }
      }

      return TRUE;
    }
    case GST_OCV_COLOR_YUY2_to_NV12:
    case GST_OCV_COLOR_YUY2_to_NV21:
    case GST_OCV_COLOR_UYVY_to_NV12:
    case GST_OCV_COLOR_UYVY_to_NV21:
    case GST_OCV_COLOR_YVYU_to_NV12:
    case GST_OCV_COLOR_YVYU_to_NV21:
    {
      const guint8 *src_data = (guint8 *) s_obj->planes[0].data;
      guint8 *y_data = (guint8 *) d_obj->planes[0].data;
      guint8 *uv_data = (guint8 *) d_obj->planes[1].data;

      // Process in blocks of 2x2 pixels as destination chroma is interpolated.
      //       YUY2              Luma      Chroma
      // | Y0 U0 Y1 V0 |  ---> | Y0 Y1 | + | U V |
      // | Y2 U1 Y3 V1 |  ---> | Y2 Y3 | // U = (U0 + U1) / 2 and V = (V0 + V1) / 2

      // Process in blocks of 2x2 pixels as destination chroma is interpolated.
      //       UYVY              Luma      Chroma
      // | U0 Y0 V0 Y1 |  ---> | Y0 Y1 | + | U V |
      // | U1 Y2 V1 Y3 |  ---> | Y2 Y3 | // U = (U0 + U1) / 2 and V = (V0 + V1) / 2

      // Process in blocks of 2x2 pixels as destination chroma is interpolated.
      //       YVYU              Luma      Chroma
      // | Y0 V0 Y1 U0 |  ---> | Y0 Y1 | + | U V |
      // | Y2 V1 Y3 U1 |  ---> | Y2 Y3 | // U = (U0 + U1) / 2 and V = (V0 + V1) / 2

      for (y_idx = 0; y_idx < d_obj->planes[0].height; y_idx += 2) {
        const guint8 *src_row_0 = src_data + y_idx * s_obj->planes[0].stride;
        const guint8 *src_row_1 = src_data + (y_idx + 1) * s_obj->planes[0].stride;
        guint8 *y_row_0 = y_data + y_idx * d_obj->planes[0].stride;
        guint8 *y_row_1 = y_data + (y_idx + 1) * d_obj->planes[0].stride;
        guint8 *uv_row = uv_data + (y_idx / 2) * d_obj->planes[1].stride;

        for (x_idx = 0; x_idx < d_obj->planes[0].width; x_idx += 2) {
          const guint32 row_idx = x_idx * 2;
          guint8 y0 = src_row_0[row_idx + 0];
          guint8 u0 = src_row_0[row_idx + 1];
          guint8 y1 = src_row_0[row_idx + 2];
          guint8 v0 = src_row_0[row_idx + 3];

          if (s_obj->format == GST_VIDEO_FORMAT_UYVY) {
            y0 = src_row_0[row_idx + 1];
            u0 = src_row_0[row_idx + 0];
            y1 = src_row_0[row_idx + 3];
            v0 = src_row_0[row_idx + 2];
          } else if (s_obj->format == GST_VIDEO_FORMAT_YVYU) {
            u0 = src_row_0[row_idx + 3];
            v0 = src_row_0[row_idx + 1];
          }

          y_row_0[x_idx + 0] = y0;
          y_row_0[x_idx + 1] = y1;

          guint8 y2 = src_row_1[row_idx + 0];
          guint8 u1 = src_row_1[row_idx + 1];
          guint8 y3 = src_row_1[row_idx + 2];
          guint8 v1 = src_row_1[row_idx + 3];

          if (s_obj->format == GST_VIDEO_FORMAT_UYVY) {
            y2 = src_row_1[row_idx + 1];
            u1 = src_row_1[row_idx + 0];
            y3 = src_row_1[row_idx + 3];
            v1 = src_row_1[row_idx + 2];
          } else if (s_obj->format == GST_VIDEO_FORMAT_YVYU) {
            u1 = src_row_1[row_idx + 3];
            v1 = src_row_1[row_idx + 1];
          }

          y_row_1[x_idx + 0] = y2;
          y_row_1[x_idx + 1] = y3;

          const guint8 u = (guint8) ((u0 + u1 + 1) >> 1);
          const guint8 v = (guint8) ((v0 + v1 + 1) >> 1);

          uv_row[x_idx + 0] = (d_obj->format == GST_VIDEO_FORMAT_NV12) ? u : v;
          uv_row[x_idx + 1] = (d_obj->format == GST_VIDEO_FORMAT_NV12) ? v : u;
        }
      }

      return TRUE;
    }
    default:
      return FALSE;
  }
}

static inline gboolean
gst_ocv_video_converter_yuv_to_rgb (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  switch (conversion_mode)
  {
  case cv::COLOR_YUV2RGB_I420:
  case cv::COLOR_YUV2BGR_I420:
  case cv::COLOR_YUV2RGBA_I420:
  case cv::COLOR_YUV2BGRA_I420:
  case cv::COLOR_YUV2RGB_YV12:
  case cv::COLOR_YUV2BGR_YV12:
  case cv::COLOR_YUV2RGBA_YV12:
  case cv::COLOR_YUV2BGRA_YV12:
  {
    guint8 *packed = (guint8 *) malloc (
        s_obj->planes[0].height * s_obj->planes[0].stride +
        s_obj->planes[1].height * s_obj->planes[1].stride +
        s_obj->planes[2].height * s_obj->planes[2].stride);
    guint8 *cur_ptr = packed;

    memcpy (cur_ptr, (guint8 *) s_obj->planes[0].data,
        s_obj->planes[0].height * s_obj->planes[0].stride);

    cur_ptr += s_obj->planes[0].height * s_obj->planes[0].stride;
    memcpy (cur_ptr, (guint8 *) s_obj->planes[1].data,
        s_obj->planes[1].height * s_obj->planes[1].stride);

    cur_ptr += s_obj->planes[1].height * s_obj->planes[1].stride;
    memcpy (cur_ptr, (guint8 *) s_obj->planes[2].data,
        s_obj->planes[2].height * s_obj->planes[2].stride);

    cv::Mat input_matrix ((s_obj->planes[0].height * 3) / 2, s_obj->planes[0].width,
        s_obj->planes[0].type, packed, s_obj->planes[0].stride);

    cv::Mat output_matrix (d_obj->planes[0].height, d_obj->planes[0].width,
        d_obj->planes[0].type, d_obj->planes[0].data, d_obj->planes[0].stride);

    cv::cvtColor (input_matrix, output_matrix, conversion_mode);

    free (packed);

    return TRUE;
  }
  case cv::COLOR_YUV2RGB_NV12:
  case cv::COLOR_YUV2BGR_NV12:
  case cv::COLOR_YUV2RGB_NV21:
  case cv::COLOR_YUV2BGR_NV21:
  case cv::COLOR_YUV2RGBA_NV12:
  case cv::COLOR_YUV2BGRA_NV12:
  case cv::COLOR_YUV2RGBA_NV21:
  case cv::COLOR_YUV2BGRA_NV21:
  case cv::COLOR_YUV2RGB_YUY2:
  case cv::COLOR_YUV2BGR_YUY2:
  case cv::COLOR_YUV2RGB_UYVY:
  case cv::COLOR_YUV2BGR_UYVY:
  case cv::COLOR_YUV2RGB_YVYU:
  case cv::COLOR_YUV2BGR_YVYU:
  case cv::COLOR_YUV2RGBA_YUY2:
  case cv::COLOR_YUV2BGRA_YUY2:
  case cv::COLOR_YUV2RGBA_UYVY:
  case cv::COLOR_YUV2BGRA_UYVY:
  case cv::COLOR_YUV2RGBA_YVYU:
  case cv::COLOR_YUV2BGRA_YVYU:
    return gst_ocv_video_converter_standard_conversion (s_obj, d_obj,
        conversion_mode);

  default:
    return FALSE;
  }
}

static inline gboolean
gst_ocv_video_converter_yuv_to_gray (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  switch (conversion_mode) {
    case cv::COLOR_YUV2GRAY_NV12:
    // case cv::COLOR_YUV2GRAY_NV21: -- Duplicate
    // case cv::COLOR_YUV2GRAY_I420; -- Duplicate
    // case cv::COLOR_YUV2GRAY_YV12; -- Duplicate
    {
      // TODO: Fix to work with cvtColor
      gst_ocv_video_converter_copy_plane (&s_obj->planes[0], &d_obj->planes[0]);

      return TRUE;
    }
    case cv::COLOR_YUV2GRAY_YUY2:
    // case cv::COLOR_YUV2GRAY_YVYU: -- Duplicate
    case cv::COLOR_YUV2GRAY_UYVY:
      return gst_ocv_video_converter_standard_conversion (s_obj, d_obj,
          conversion_mode);

    default:
      return FALSE;
  }
}

static inline gboolean
gst_ocv_video_converter_rgb_to_yuv (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  switch (conversion_mode) {
    case GST_OCV_COLOR_RGB_to_NV12:
    case GST_OCV_COLOR_RGB_to_NV21:
    case GST_OCV_COLOR_BGR_to_NV12:
    case GST_OCV_COLOR_BGR_to_NV21:
    case GST_OCV_COLOR_RGBA_to_NV12:
    case GST_OCV_COLOR_RGBA_to_NV21:
    case GST_OCV_COLOR_BGRA_to_NV12:
    case GST_OCV_COLOR_BGRA_to_NV21:
    {
      const gfloat kr = 0.299, kg = 0.587, kb = 0.114;
      guint y_idx, x_idx = 0;

      for (y_idx = 0; y_idx < d_obj->planes[0].height; y_idx++) {
        guint8 *y_row = (guint8 *) d_obj->planes[0].data +
            y_idx * d_obj->planes[0].stride;
        guint8 *uv_row = (guint8 *) d_obj->planes[1].data +
            (y_idx / 2) * d_obj->planes[1].stride;

        for (x_idx = 0; x_idx < d_obj->planes[0].width; x_idx++) {
          gint rgb_idx = 0;
          guint32 u_sum = 0, v_sum = 0;
          guint8 red = 0, green = 0, blue = 0, step = 3;

          if (s_obj->format == GST_VIDEO_FORMAT_RGBA ||
              s_obj->format == GST_VIDEO_FORMAT_BGRA)
            step = 4;

          rgb_idx = (y_idx * s_obj->planes[0].stride) + x_idx * step;

          if (s_obj->format == GST_VIDEO_FORMAT_RGB ||
              s_obj->format == GST_VIDEO_FORMAT_RGBA) {
            red = ((guint8 *) s_obj->planes[0].data)[rgb_idx];
            green = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 1];
            blue = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 2];
          } else {
            red = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 2];
            green = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 1];
            blue = ((guint8 *) s_obj->planes[0].data)[rgb_idx];
          }

          guint8 luma = (red * kr) + (green * kg) + (blue * kb);
          y_row[x_idx] = luma;

          if (y_idx % 2 == 1 || x_idx % 2 == 1)
            continue;

          for (gint delta_y = 0; delta_y < 2; delta_y++) {
            for (gint delta_x = 0; delta_x < 2; delta_x++) {
              gint rgb_idx = 0;
              guint8 red = 0, green = 0, blue = 0;

              rgb_idx = ((y_idx + delta_y) * s_obj->planes[0].stride) +
                  (x_idx + delta_x) * step;

              if (s_obj->format == GST_VIDEO_FORMAT_RGB ||
                  s_obj->format == GST_VIDEO_FORMAT_RGBA) {
                red = ((guint8 *) s_obj->planes[0].data)[rgb_idx];
                green = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 1];
                blue = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 2];
              } else {
                red = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 2];
                green = ((guint8 *) s_obj->planes[0].data)[rgb_idx + 1];
                blue = ((guint8 *) s_obj->planes[0].data)[rgb_idx];
              }

              guint32 cb = 128 + (red * (-(kr / (1.0 - kb)) / 2)) +
                  (green * (-(kg / (1.0 - kb)) / 2)) + (blue * 0.5);
              guint32 cr = 128 + (red * 0.5) + (green * (-(kg / (1.0 - kr)) / 2)) +
                  (blue * (-(kb / (1.0 - kr)) / 2));

              u_sum += cb;
              v_sum += cr;
            }
          }

          if (d_obj->format == GST_VIDEO_FORMAT_NV12) {
            uv_row[x_idx] = (guint8) (u_sum / 4);
            uv_row[x_idx + 1] = (guint8) (v_sum / 4);
          } else {
            uv_row[x_idx] = (guint8) (v_sum / 4);
            uv_row[x_idx + 1] = (guint8) (u_sum / 4);
          }
        }
      }

      return TRUE;
    }
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 9)
    case cv::COLOR_RGB2YUV_YUY2:
    case cv::COLOR_BGR2YUV_YUY2:
    case cv::COLOR_RGBA2YUV_YUY2:
    case cv::COLOR_BGRA2YUV_YUY2:
    case cv::COLOR_RGB2YUV_UYVY:
    case cv::COLOR_BGR2YUV_UYVY:
    case cv::COLOR_RGBA2YUV_UYVY:
    case cv::COLOR_BGRA2YUV_UYVY:
    case cv::COLOR_RGB2YUV_YVYU:
    case cv::COLOR_BGR2YUV_YVYU:
    case cv::COLOR_RGBA2YUV_YVYU:
    case cv::COLOR_BGRA2YUV_YVYU:
#else
    case GST_OCV_COLOR_RGB2YUV_YUY2:
    case GST_OCV_COLOR_BGR2YUV_YUY2:
    case GST_OCV_COLOR_RGBA2YUV_YUY2:
    case GST_OCV_COLOR_BGRA2YUV_YUY2:
    case GST_OCV_COLOR_RGB2YUV_UYVY:
    case GST_OCV_COLOR_BGR2YUV_UYVY:
    case GST_OCV_COLOR_RGBA2YUV_UYVY:
    case GST_OCV_COLOR_BGRA2YUV_UYVY:
    case GST_OCV_COLOR_RGB2YUV_YVYU:
    case GST_OCV_COLOR_BGR2YUV_YVYU:
    case GST_OCV_COLOR_RGBA2YUV_YVYU:
    case GST_OCV_COLOR_BGRA2YUV_YVYU:
#endif // CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 9)
    {
      // Conversion is not supported on version below 4.9.0
      if (cv::getVersionMajor() < 4 ||
          (cv::getVersionMajor() == 4 && cv::getVersionMinor() < 9))
        return FALSE;
      else
        return gst_ocv_video_converter_standard_conversion (s_obj, d_obj,
            conversion_mode);
    }
    case cv::COLOR_RGB2YUV_I420:
    case cv::COLOR_BGR2YUV_I420:
    case cv::COLOR_RGBA2YUV_I420:
    case cv::COLOR_BGRA2YUV_I420:
    case cv::COLOR_RGB2YUV_YV12:
    case cv::COLOR_BGR2YUV_YV12:
    case cv::COLOR_RGBA2YUV_YV12:
    case cv::COLOR_BGRA2YUV_YV12:
    {
      guint8 *packed = (guint8 *) malloc (
          d_obj->planes[0].height * d_obj->planes[0].stride +
          d_obj->planes[1].height * d_obj->planes[1].stride +
          d_obj->planes[2].height * d_obj->planes[2].stride);
      guint8 *cur_ptr = packed;

      cv::Mat input_matrix (s_obj->planes[0].height, s_obj->planes[0].width,
          s_obj->planes[0].type, s_obj->planes[0].data, s_obj->planes[0].stride);

      cv::Mat output_matrix ((d_obj->planes[0].height * 3) / 2,
          d_obj->planes[0].width, d_obj->planes[0].type, packed,
          d_obj->planes[0].stride);

      cv::cvtColor (input_matrix, output_matrix, conversion_mode);

      memcpy ((guint8 *) d_obj->planes[0].data, cur_ptr,
          d_obj->planes[0].height * d_obj->planes[0].stride);

      cur_ptr += d_obj->planes[0].height * d_obj->planes[0].stride;
      memcpy ((guint8 *) d_obj->planes[1].data, cur_ptr,
          d_obj->planes[1].height * d_obj->planes[1].stride);

      cur_ptr += d_obj->planes[1].height * d_obj->planes[1].stride;
      memcpy ((guint8 *) d_obj->planes[2].data, cur_ptr,
          d_obj->planes[2].height * d_obj->planes[2].stride);

      free (packed);

      return TRUE;
    }

    default:
      return FALSE;
  }
}

static inline gboolean
gst_ocv_video_converter_rgb_to_rgb (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  return gst_ocv_video_converter_standard_conversion (s_obj, d_obj,
      conversion_mode);
}

static inline gboolean
gst_ocv_video_converter_rgb_to_gray (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  return gst_ocv_video_converter_standard_conversion (s_obj, d_obj,
      conversion_mode);
}

static inline gboolean
gst_ocv_video_converter_gray_to_yuv (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  guint x_idx = 0, y_idx = 0;

  switch (conversion_mode) {
    case GST_OCV_COLOR_GRAY_to_NV12:
    case GST_OCV_COLOR_GRAY_to_NV21:
    {
      gst_ocv_video_converter_copy_plane (&s_obj->planes[0], &d_obj->planes[0]);

      for (x_idx = 0; x_idx < d_obj->planes[1].height; x_idx++)  {
        for (y_idx = 0; y_idx < d_obj->planes[1].width * 2; y_idx++) {
          (((guint8 *) d_obj->planes[1].data) +
              x_idx * d_obj->planes[1].stride)[y_idx] = GST_OCV_NEUTRAL_CHROMA;
        }
      }

      return TRUE;
    }
    case GST_OCV_COLOR_GRAY_to_I420:
    case GST_OCV_COLOR_GRAY_to_YV12:
    {
      gst_ocv_video_converter_copy_plane (&s_obj->planes[0], &d_obj->planes[0]);

      for (x_idx = 0; x_idx < d_obj->planes[1].height; x_idx++)  {
        for (y_idx = 0; y_idx < d_obj->planes[1].width; y_idx++) {
          (((guint8 *) d_obj->planes[1].data) +
              x_idx * d_obj->planes[1].stride)[y_idx] = GST_OCV_NEUTRAL_CHROMA;
          (((guint8 *) d_obj->planes[2].data) +
              x_idx * d_obj->planes[2].stride)[y_idx] = GST_OCV_NEUTRAL_CHROMA;
        }
      }

      return TRUE;
    }
    case GST_OCV_COLOR_GRAY_to_YUY2:
    case GST_OCV_COLOR_GRAY_to_UYVY:
    case GST_OCV_COLOR_GRAY_to_YVYU:
      // Process in blocks of 2x2 pixels
      //   GRAY                YUY2
      // | Y0 Y1 |  --->  | Y0 128 Y1 128 |
      // | Y2 Y3 |  --->  | Y2 128 Y3 128 |

      //   GRAY                UYVY
      // | Y0 Y1 |  --->  | 128 Y0 128 Y1 |
      // | Y2 Y3 |  --->  | 128 Y2 128 Y3 |

      //   GRAY                YVYU  --  Identical of YUY2
      // | Y0 Y1 |  --->  | Y0 128 Y1 128 |
      // | Y2 Y3 |  --->  | Y2 128 Y3 128 |

      for (y_idx = 0; y_idx < s_obj->planes[0].height; y_idx += 2) {
        const guint8 *y_row_0 = (guint8 *) s_obj->planes[0].data +
            y_idx * s_obj->planes[0].stride;
        const guint8 *y_row_1 = (guint8 *) s_obj->planes[0].data +
            (y_idx + 1) * s_obj->planes[0].stride;
        guint8 *d_row_0 = (guint8 *) d_obj->planes[0].data +
            y_idx * d_obj->planes[0].stride;
        guint8 *d_row_1 = (guint8 *) d_obj->planes[0].data +
            (y_idx + 1) * d_obj->planes[0].stride;

        for (x_idx = 0; x_idx < s_obj->planes[0].width; x_idx += 2) {
          const guint32 row_idx = x_idx * 2;

          guint8 *y0 = &d_row_0[row_idx + 0];
          guint8 *u0 = &d_row_0[row_idx + 1];
          guint8 *y1 = &d_row_0[row_idx + 2];
          guint8 *v0 = &d_row_0[row_idx + 3];
          guint8 *y2 = &d_row_1[row_idx + 0];
          guint8 *u1 = &d_row_1[row_idx + 1];
          guint8 *y3 = &d_row_1[row_idx + 2];
          guint8 *v1 = &d_row_1[row_idx + 3];

          if (d_obj->format == GST_VIDEO_FORMAT_UYVY) {
            guint8 *temp = y0;
            y0 = u0;
            u0 = temp;

            temp = y1;
            y1 = v0;
            v0 = temp;

            temp = y2;
            y2 = u1;
            u1 = temp;

            temp = y3;
            y3 = v1;
            v1 = temp;
          } else if (d_obj->format == GST_VIDEO_FORMAT_YVYU) {
            guint8 *temp = u0;
            u0 = v0;
            v0 = temp;

            temp = u1;
            u1 = v1;
            v1 = temp;
          }

          *y0 = y_row_0[x_idx + 0];     // y0
          *u0 = GST_OCV_NEUTRAL_CHROMA; // u0
          *y1 = y_row_0[x_idx + 1];     // y1
          *v0 = GST_OCV_NEUTRAL_CHROMA; // v0
          *y2 = y_row_1[x_idx + 0];     // y2
          *u1 = GST_OCV_NEUTRAL_CHROMA; // u1
          *y3 = y_row_1[x_idx + 1];     // y3
          *v1 = GST_OCV_NEUTRAL_CHROMA; // v1
        }
      }

      return TRUE;

    default:
      return FALSE;
  }
}

static inline gboolean
gst_ocv_video_converter_gray_to_rgb (GstOcvObject * s_obj, GstOcvObject * d_obj,
    gint conversion_mode)
{
  return gst_ocv_video_converter_standard_conversion (s_obj, d_obj,
      conversion_mode);
}

static inline gboolean
gst_ocv_video_converter_cvt_color (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  gboolean success = FALSE;
  gint conversion_mode = gst_ocv_get_conversion_mode (s_obj, d_obj);

  GST_TRACE ("Performing color convert. Conversion code: %d", conversion_mode);

  if (GST_OCV_INVALID_CONVERSION == conversion_mode) {
    GST_ERROR ("Unsupported format conversion!");
    if (s_obj->flags & GST_OCV_FLAG_STAGED)
      gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

    return FALSE;
  }

  if (GST_OCV_OBJ_IS_YUV (s_obj) && GST_OCV_OBJ_IS_YUV (d_obj)) {
    success = gst_ocv_video_converter_yuv_to_yuv (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_YUV (s_obj) && GST_OCV_OBJ_IS_RGB (d_obj)) {
    success = gst_ocv_video_converter_yuv_to_rgb (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_YUV (s_obj) && GST_OCV_OBJ_IS_GRAY (d_obj)) {
    success = gst_ocv_video_converter_yuv_to_gray (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_RGB (s_obj) && GST_OCV_OBJ_IS_YUV (d_obj)) {
    success = gst_ocv_video_converter_rgb_to_yuv (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_RGB (s_obj) && GST_OCV_OBJ_IS_RGB (d_obj)) {
    success = gst_ocv_video_converter_rgb_to_rgb (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_RGB (s_obj) && GST_OCV_OBJ_IS_GRAY (d_obj)) {
    success = gst_ocv_video_converter_rgb_to_gray (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_GRAY (s_obj) && GST_OCV_OBJ_IS_YUV (d_obj)) {
    success = gst_ocv_video_converter_gray_to_yuv (s_obj, d_obj, conversion_mode);
  } else if (GST_OCV_OBJ_IS_GRAY (s_obj) && GST_OCV_OBJ_IS_RGB (d_obj)) {
    success = gst_ocv_video_converter_gray_to_rgb (s_obj, d_obj, conversion_mode);
  }

  if (success == FALSE) {
    GST_ERROR ("Format %s to %s conversion failed!",
        gst_video_format_to_string (s_obj->format),
        gst_video_format_to_string (d_obj->format));
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  return success;
}

static inline gboolean
gst_ocv_video_converter_prepare_frame (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  GstOpenCVFlip flip = GST_OCV_FLIP_NONE;
  gboolean resize = FALSE;

  // Cache the flip, rotation, resize and color convert flags.
  rotate = s_obj->rotate;
  flip = s_obj->flip;
  resize = s_obj->resize;

  // Use stage object if other operations are pending
  if (d_obj->format != GST_OCV_FALLBACK_FORMAT || resize ||
      (rotate != GST_VCE_ROTATE_0) || (flip != GST_OCV_FLIP_NONE)) {
    gboolean success = FALSE;

    // Temporary store the destination object data into local intermediary.
    gst_ocv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_ocv_video_converter_stage_object_init (convert, d_obj,
        s_obj->planes[0].width, s_obj->planes[0].height, GST_OCV_FALLBACK_FORMAT);
    g_return_val_if_fail (success, FALSE);
  }

  gst_ocv_video_converter_cvt_color (convert, s_obj, d_obj);

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending resize, rotate, color convert and flip
  s_obj->flip = flip;
  s_obj->rotate = rotate;
  s_obj->resize = resize;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_ocv_video_converter_fill_background (GstOcvVideoConverter * convert,
    GstVideoFrame * frame, guint32 color)
{
  gint height = GST_VIDEO_FRAME_HEIGHT (frame);
  gint width = GST_VIDEO_FRAME_WIDTH (frame);
  guint8 red = 0x00, green = 0x00, blue = 0x00, alpha = 0x00;
  guint8 luma = 0x00;
  guint16 cb = 0x00, cr = 0x00;

  red = EXTRACT_RED_VALUE (color);
  green = EXTRACT_GREEN_VALUE (color);
  blue = EXTRACT_BLUE_VALUE (color);
  alpha = EXTRACT_ALPHA_VALUE (color);

  // Convert color code BT601 YUV color scape.
  if (GST_VIDEO_INFO_IS_YUV (&(frame->info))) {
    gfloat kr = 0.299, kg = 0.587, kb = 0.114;

    luma = (red * kr) + (green * kg) + (blue * kb);
    cb = 128 + (red * (-(kr / (1.0 - kb)) / 2)) +
        (green * (-(kg / (1.0 - kb)) / 2)) + (blue * 0.5);
    cr = 128 + (red * 0.5) + (green * (-(kg / (1.0 - kr)) / 2)) +
        (blue * (-(kb / (1.0 - kr)) / 2));
  }

  GST_TRACE ("Fill buffer %p with 0x%X - %ux%u %s", frame->buffer, color,
      width, height, gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));

  // TODO: Fix YUV Formats
  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_GRAY8:
    {
      cv::Mat y_plane (height, width, CV_8UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      y_plane.setTo (luma);
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    {
      guint16 uv = (cr << 8) | cb;

      cv::Mat y_plane (height, width, CV_8UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      cv::Mat uv_plane (height / 2, width / 2, CV_16UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1));

      y_plane.setTo (luma);
      uv_plane.setTo (uv);
      break;
    }
    case GST_VIDEO_FORMAT_NV21:
    {
      guint16 vu = (cb << 8) | cr;

      cv::Mat y_plane (height, width, CV_8UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      cv::Mat uv_plane (height / 2, width / 2, CV_16UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1));

      y_plane.setTo (luma);
      uv_plane.setTo (vu);
      break;
    }
    case GST_VIDEO_FORMAT_NV16:
    {
      guint16 uv = (cr << 8) | cb;

      cv::Mat y_plane (height, width, CV_8UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      cv::Mat uv_plane (height, width / 2, CV_16UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1));

      y_plane.setTo (luma);
      uv_plane.setTo (uv);
      break;
    }
    case GST_VIDEO_FORMAT_NV61:
    {
      guint16 vu = (cb << 8) | cr;

      cv::Mat y_plane (height, width, CV_8UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      cv::Mat uv_plane (height, width / 2, CV_16UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1));

      y_plane.setTo (luma);
      uv_plane.setTo (vu);
      break;
    }
    case GST_VIDEO_FORMAT_NV24:
    {
      guint16 uv = (cr << 8) | cb;

      cv::Mat y_plane (height, width, CV_8UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      cv::Mat uv_plane (height, width, CV_16UC1,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1));

      y_plane.setTo (luma);
      uv_plane.setTo (uv);
      break;
    }
    case GST_VIDEO_FORMAT_RGB:
    {
      cv::Vec3b rgb_color (red, green, blue);

      cv::Mat rgb_plane (height, width, CV_8UC3,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      rgb_plane.setTo (rgb_color);
      break;
    }
    case GST_VIDEO_FORMAT_BGR:
    {
      cv::Vec3b bgr_color (blue, green, red);

      cv::Mat bgr_plane (height, width, CV_8UC3,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      bgr_plane.setTo (bgr_color);
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    {
      cv::Vec4b rgba_color (red, green, blue, alpha);

      cv::Mat rgba_plane (height, width, CV_8UC3,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      rgba_plane.setTo (rgba_color);
      break;
    }
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
    {
      cv::Vec4b bgra_color (blue, green, red, alpha);

      cv::Mat bgra_plane (height, width, CV_8UC3,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

      bgra_plane.setTo (bgra_color);
      break;
    }
    default:
    {
      GST_ERROR ("Unsupported format %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
      return FALSE;
    }
  }

  return TRUE;
}

static inline gboolean
gst_ocv_video_converter_process (GstOcvVideoConverter * convert,
    GstOcvObject * objects, guint n_objects)
{
  GstOcvObject *s_obj = NULL, *d_obj = NULL;
  guint idx = 0, flip = 0, rotate = 0;
  gfloat w_scale = 0.0, h_scale = 0.0, scale = 0.0;
  gboolean downscale = FALSE, upscale = FALSE, cvt_color = FALSE, normalize = FALSE;

  GST_TRACE ("Processing %d object pairs", n_objects / 2);

  for (idx = 0; idx < n_objects; idx += 2) {
    s_obj = &(objects[idx]);
    d_obj = &(objects[idx + 1]);

    flip = s_obj->flip;
    rotate = s_obj->rotate;

    // Calculte the width and height scale ratios.
    if ((rotate == GST_VCE_ROTATE_0) || (rotate == GST_VCE_ROTATE_180)) {
      w_scale = ((gfloat) d_obj->planes[0].width) / s_obj->planes[0].width;
      h_scale = ((gfloat) d_obj->planes[0].height) / s_obj->planes[0].height;
    } else {
      w_scale = ((gfloat) d_obj->planes[0].height) / s_obj->planes[0].width;
      h_scale = ((gfloat) d_obj->planes[0].width) / s_obj->planes[0].height;
    }

    // Calculate the combined scale factor.
    scale = w_scale * h_scale;

    // Use downscale if output is smaller or for simple copy of a region.
    downscale = (scale < 1.0) || ((w_scale == 1.0) && (h_scale == 1.0) &&
        (rotate == 0) && (flip == 0) && (s_obj->format == d_obj->format) &&
        (s_obj->format != GST_VIDEO_FORMAT_P010_10LE));

    // Use upscale if output is bigger or same scale but reversed dimensions.
    upscale = (scale > 1.0) ||
        (scale == 1.0 && w_scale != 1.0 && h_scale != 1.0 && rotate == 0);

    s_obj->resize = upscale || downscale;

    // Do intermediary format conversion if source is packed and either
    // a transformation is required or a direct conversion from packed to
    // destination format doesn't exist
    normalize = FORMAT_IS_PACKED (s_obj->format) &&
        ((flip || rotate || upscale || downscale) ||
        (gst_ocv_get_conversion_mode (s_obj, d_obj) == GST_OCV_INVALID_CONVERSION));

    if (normalize && !gst_ocv_video_converter_prepare_frame (convert, s_obj, d_obj))  {
      GST_ERROR ("Failed to prepare image!");
      return FALSE;
    }

    cvt_color = s_obj->format != d_obj->format;

    GST_LOG ("Starting processing of object pair %u; flip is: %d, rotate: %d, "
        "downscale: %d, upscale: %d, scale: %f, color convert: %d",
        idx / 2, flip, rotate, downscale, upscale, scale, cvt_color);

    // First, do downscale if required so that next operations are less costly.
    if (downscale && !gst_ocv_video_converter_resize (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to resize image!");
      return FALSE;
    }

    // Second, perform image rotate if necessary.
    if ((rotate != 0) && !gst_ocv_video_converter_rotate (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to rotate image!");
      return FALSE;
    }

    // Third, perform image flip if necessary.
    if ((flip != 0) && !gst_ocv_video_converter_flip (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to flip image!");
      return FALSE;
    }

    // Fourth, upscale output if needed
    if (upscale && !gst_ocv_video_converter_resize (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to upscale image!");
      return FALSE;
    }

    // Fifth, perform color conversion if necessary.
    if (cvt_color && !gst_ocv_video_converter_cvt_color (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to convert image format!");
      return FALSE;
    }

    GST_TRACE ("Object pair %u processed succesfully!", idx / 2);
  }

  return TRUE;
}

gboolean
gst_ocv_video_converter_compose (GstOcvVideoConverter * convert,
    GstVideoComposition * compositions, guint n_compositions, gpointer * fence)
{
  GstOcvObject objects[GST_OCV_MAX_DRAW_OBJECTS] = {};
  guint32 idx = 0, n_objects = 0, num = 0, area = 0;
  gboolean success = FALSE;

  // TODO: Implement async operations via threads.
  if (fence != NULL)
    GST_WARNING ("Asynchronous composition operations are not supported!");

  for (idx = 0; idx < n_compositions; idx++) {
    GstVideoFrame outframe;
    GArray *inframes = NULL;
    GstVideoComposition *composition = &(compositions[idx]);
    GstVideoBlit *blits = composition->blits;
    guint n_blits = composition->n_blits;

    inframes = g_array_sized_new (FALSE, FALSE, sizeof(GstVideoFrame),
        composition->n_blits);
    g_array_set_size (inframes, composition->n_blits);

    success = gst_video_frame_map (&outframe, composition->info,
        composition->buffer,
        (GstMapFlags)(GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF));

    if (!success) {
      GST_ERROR ("Failed to map input buffer!");
      return FALSE;
    }

    // Total area of the output frame that is to be used in later calculations
    // to determine whether there are unoccupied background pixels to be filled.
    area = GST_VIDEO_FRAME_WIDTH (&outframe) * GST_VIDEO_FRAME_HEIGHT (&outframe);

    // Iterate over the input blit entries and update each OCV object.
    for (num = 0; num < n_blits; num++) {
      GstVideoBlit *blit = &(blits[num]);
      GstOcvObject *object = NULL;
      GstVideoRectangle rectangle = {0, 0, 0, 0};
      GstOpenCVFlip flip = GST_OCV_FLIP_NONE;
      GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
      GstVideoFrame *inframe = &g_array_index (inframes, GstVideoFrame, num);

      success = gst_video_frame_map (inframe, blit->info, blit->buffer,
          (GstMapFlags)(GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF));

      if (!success) {
        GST_ERROR ("Failed to map input buffer!");
        return FALSE;
      }

      if (n_objects >= GST_OCV_MAX_DRAW_OBJECTS) {
        GST_ERROR ("Number of objects exceeds %d!", GST_OCV_MAX_DRAW_OBJECTS);
        return FALSE;
      }

      if ((blit->mask & GST_VCE_MASK_FLIP_VERTICAL) &&
          (blit->mask & GST_VCE_MASK_FLIP_HORIZONTAL))
        flip = GST_OCV_FLIP_BOTH;
      else if (blit->mask & GST_VCE_MASK_FLIP_VERTICAL)
        flip = GST_OCV_FLIP_VERTICAL;
      else if (blit->mask & GST_VCE_MASK_FLIP_HORIZONTAL)
        flip = GST_OCV_FLIP_HORIZONTAL;

      if (blit->mask & GST_VCE_MASK_ROTATION)
        rotate = blit->rotate;

      // Intialization of the source OCV object.
      object = &(objects[n_objects]);

      if (blit->mask & GST_VCE_MASK_SOURCE) {
        if (!gst_video_quadrilateral_is_rectangle (&(blit->source))) {
          GST_ERROR ("Composition %u: Blit %u: Source quadrilateral is not a "
              "rectangle! A(%f, %f) B(%f, %f) C(%fd, %f) D(%f, %f)", idx, num,
              blit->source.a.x, blit->source.a.y, blit->source.b.x,
              blit->source.b.y, blit->source.c.x, blit->source.c.y,
              blit->source.d.x, blit->source.d.y);
          return FALSE;
        }

        rectangle.x = blit->source.a.x;
        rectangle.y = blit->source.a.y;
        rectangle.w = blit->source.d.x - blit->source.a.x;
        rectangle.h = blit->source.d.y - blit->source.a.y;
      } else {
        rectangle.x = rectangle.y = 0;
        rectangle.w = GST_VIDEO_FRAME_WIDTH (inframe);
        rectangle.h = GST_VIDEO_FRAME_HEIGHT (inframe);
      }

      gst_ocv_update_object (object, "Source", inframe, &rectangle,
          flip, rotate, 0);

      // Intialization of the destination OCV object.
      object = &(objects[n_objects + 1]);

      // Setup the source quadrilateral.
      if (blit->mask & GST_VCE_MASK_DESTINATION) {
        rectangle = blit->destination;
      } else {
        rectangle.x = rectangle.y = 0;
        rectangle.w = GST_VIDEO_FRAME_WIDTH (&outframe);
        rectangle.h = GST_VIDEO_FRAME_HEIGHT (&outframe);
      }

      gst_ocv_update_object (object, "Destination", &outframe, &rectangle,
          GST_OCV_FLIP_NONE, GST_VCE_ROTATE_0, composition->datatype);

      // Subtract blit area from total area.
      if (area != 0)
        area -= gst_ocv_composition_blit_area (&outframe, blits, num);

      // Increment the objects counter by 2 for for Source/Destination pair.
      n_objects += 2;
    }

    // Sanity checks, output frame and blit entries must not be NULL.
    g_return_val_if_fail (&outframe != NULL && blits != NULL, FALSE);

    if (compositions[idx].bgfill && (area > 0)) {
      guint32 color = compositions[idx].bgcolor;
      gst_ocv_video_converter_fill_background (convert, &outframe, color);
    }

    if (!gst_ocv_video_converter_process (convert, objects, n_objects)) {
      GST_ERROR ("Failed to process frames for composition %u!", idx);
      return FALSE;
    }

    success = gst_video_frame_normalize_ip (&outframe,
        composition->datatype, composition->offsets, composition->scales);

    for (num = 0; num < inframes->len; num++) {
      GstVideoFrame *inframe = &g_array_index(inframes, GstVideoFrame, num);

      gst_video_frame_unmap(inframe);
    }

    g_array_free (inframes, TRUE);

    gst_video_frame_unmap (&outframe);

    if (!success) {
      GST_ERROR ("Failed to normalize output frame for composition %u!", idx);
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_ocv_video_converter_wait_fence (GstOcvVideoConverter * convert,
    gpointer fence)
{
  GST_WARNING ("Not implemented!");

  return TRUE;
}

void
gst_ocv_video_converter_flush (GstOcvVideoConverter * convert)
{
  GST_WARNING ("Not implemented!");
}

GstOcvVideoConverter *
gst_ocv_video_converter_new (GstStructure * settings)
{
  GstOcvVideoConverter *convert = NULL;
  gboolean success = TRUE;

  convert = g_slice_new0 (GstOcvVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  // Check whether symbol loading was successful.
  if (!success)
    goto cleanup;

  convert->stgbufs = g_array_new (FALSE, TRUE, sizeof (GstOcvStageBuffer));
  if (convert->stgbufs == NULL) {
    GST_ERROR ("Failed to create array for the staging buffers!");
    goto cleanup;
  }

  // Set clearing function for the allocated stage memory.
  g_array_set_clear_func (convert->stgbufs, gst_ocv_stage_buffer_free);

  GST_INFO ("Created OpenCV Converter %p", convert);
  return convert;

cleanup:
  gst_ocv_video_converter_free (convert);
  return NULL;
}

void
gst_ocv_video_converter_free (GstOcvVideoConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->stgbufs != NULL)
    g_array_free (convert->stgbufs, TRUE);

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed OpenCV converter: %p", convert);
  g_slice_free (GstOcvVideoConverter, convert);
}
