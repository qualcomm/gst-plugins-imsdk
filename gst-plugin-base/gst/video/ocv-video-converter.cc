/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocv-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>

#include <opencv4/opencv2/opencv.hpp>

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

#define OPENCV_FLIP_HORIZ           1
#define OPENCV_FLIP_VERT            0
#define OPENCV_FLIP_BOTH            -1

#define GST_OCV_INVALID_STAGE_ID    (-1)
#define GST_OCV_MAX_DRAW_OBJECTS    50

#define GST_OCV_WIDTH_ALIGN         8

#define GST_OCV_GET_FLIP(flip) \
    ((flip == GST_VCE_FLIP_BOTH) ? OPENCV_FLIP_BOTH : \
    ((flip == GST_VCE_FLIP_HORIZONTAL) ? OPENCV_FLIP_HORIZ : \
    ((flip == GST_VCE_FLIP_VERTICAL) ? OPENCV_FLIP_VERT : 0)))

#define GST_OCV_GET_ROTATE(rotate) \
    ((rotate == GST_VCE_ROTATE_90) ? cv::ROTATE_90_CLOCKWISE : \
    ((rotate == GST_VCE_ROTATE_180) ? cv::ROTATE_180 : \
    ((rotate == GST_VCE_ROTATE_270) ? cv::ROTATE_90_COUNTERCLOCKWISE : 0)))

typedef struct _GstOcvPlane GstOcvPlane;
typedef struct _GstOcvObject GstOcvObject;
typedef struct _GstOcvStageBuffer GstOcvStageBuffer;

enum {
  GST_OCV_FLAG_GRAY   = (1 << 0),
  GST_OCV_FLAG_RGB    = (1 << 1),
  GST_OCV_FLAG_YUV    = (1 << 2),
  GST_OCV_FLAG_STAGED = (1 << 3),
  GST_OCV_FLAG_I32    = (1 << 4),
  GST_OCV_FLAG_U32    = (1 << 5),
  GST_OCV_FLAG_F16    = (1 << 6),
  GST_OCV_FLAG_F32    = (1 << 7),
};

/**
 * GstOcvPlane:
 * @stgid: Index of the used staging buffer or -1 if created from original frame.
 * @width: Width of the plane in pixels.
 * @height: Height of the plane in pixels.
 * @data: Pointer to bytes of data.
 * @stride: Aligned width of the plane in bytes.
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
  gint     channels;
};

/**
 * GstOcvObject:
 * @format: Gstreamer video format.
 * @flags: Bit mask containing format family.
 * @flip: Flip direction or 0 if none.
 * @rotate: Clockwise rotation degrees or 0 if none.
 * @planes: Array of blit planes.
 * @n_planes: Number of used planes based on format.
 *
 * Blit object.
 */
struct _GstOcvObject
{
  GstVideoFormat     format;
  guint32            flags;

  GstVideoConvRotate rotate;
  GstVideoConvFlip   flip;
  gboolean           resize;
  gboolean           cvt_color;

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
    const GstVideoConvFlip flip, const GstVideoConvRotate rotate,
    const guint64 flags)
{
  const gchar *mode = NULL;
  gint x = 0, y = 0, width = 0, height = 0;
  guint bpp = 0;

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  // Take the region values only if they are valid.
  if ((region->w != 0) && (region->h != 0) &&
      (width >= (region->x + region->w)) && (height >= (region->y + region->h))) {
    x = region->x;
    y = region->y;
    width = region->w;
    height = region->h;
  }

  switch (flags) {
    case GST_VCE_FLAG_F16_FORMAT:
      object->flags = GST_OCV_FLAG_F16;
      mode = "FLOAT16";
      break;
    case GST_VCE_FLAG_F32_FORMAT:
      object->flags = GST_OCV_FLAG_F32;
      mode = "FLOAT32";
      break;
    case GST_VCE_FLAG_I32_FORMAT:
      object->flags = GST_OCV_FLAG_I32;
      mode = "INT32";
      break;
    case GST_VCE_FLAG_U32_FORMAT:
      object->flags = GST_OCV_FLAG_U32;
      mode = "UINT32";
      break;
    default:
      break;
  }

  GST_TRACE ("%s Buffer %p - %ux%u %s%s", type, frame->buffer,
      GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
      gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)), mode);
  GST_TRACE ("%s Buffer %p - Plane 0: Stride[%u] Data[%p]", type,
      frame->buffer, GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 0));
  GST_TRACE ("%s Buffer %p - Plane 1: Stride[%u] Data[%p]", type,
      frame->buffer, GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1),
      GST_VIDEO_FRAME_PLANE_DATA (frame, 1));
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

  object->planes[0].width = GST_ROUND_DOWN_8 (width);
  object->planes[0].height = GST_ROUND_DOWN_2 (height);

  bpp = GST_VIDEO_INFO_COMP_PSTRIDE(&(frame->info), 0);

  // Add the offset to the region of interest to the data pointer.
  object->planes[0].data =
      (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0) +
          (y * object->planes[0].stride) + x * bpp);

  object->planes[0].stgid = GST_OCV_INVALID_STAGE_ID;
  object->planes[0].channels = CV_8UC3;

  // Initialize the secondary plane depending on the format.
  switch (object->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      object->planes[0].channels = CV_8UC1;
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = GST_ROUND_UP_2 (width) / 2;
      object->planes[1].height = GST_ROUND_UP_2 (height) / 2;
      object->planes[1].channels = CV_8UC2;

      bpp = GST_VIDEO_INFO_COMP_PSTRIDE(&(frame->info), 1);
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[1].stride) +
                  GST_ROUND_UP_2 (x * bpp));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      object->planes[0].channels = CV_8UC1;
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = GST_ROUND_UP_2 (width) / 2;
      object->planes[1].height = height;
      object->planes[1].channels = CV_8UC2;

      bpp = GST_VIDEO_INFO_COMP_PSTRIDE(&(frame->info), 1);
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              (y * object->planes[1].stride) + GST_ROUND_UP_2 (x * bpp));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_NV24:
      object->planes[0].channels = CV_8UC1;
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = width * 2;
      object->planes[1].height = height;
      object->planes[1].channels = CV_8UC2;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              (y * object->planes[1].stride) + (x * 2));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      object->planes[0].channels = CV_8UC1;
      // Update plane 0 offset.
      object->planes[0].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0) +
              (y * object->planes[0].stride) + x * 2);
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = GST_ROUND_UP_2 (width);
      object->planes[1].height = GST_ROUND_UP_2 (height) / 2;
      object->planes[1].channels = CV_8UC2;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[1].stride) + (x * 2));
      object->planes[1].stgid = GST_OCV_INVALID_STAGE_ID;
      break;
    default:
      // No need for initialize anything in te secondary plane.
      break;
  }

  GST_TRACE ("%s Buffer %p - Object Format: %s%s", type, frame->buffer,
      gst_video_format_to_string (object->format), mode);
  GST_TRACE ("%s Buffer %p - Object Plane 0: %" GST_OCV_PLANE_FORMAT, type,
      frame->buffer, GST_OCV_PLANE_ARGS (&(object->planes[0])));
  GST_TRACE ("%s Buffer %p - Object Plane 1: %" GST_OCV_PLANE_FORMAT, type,
      frame->buffer, GST_OCV_PLANE_ARGS (&(object->planes[1])));

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

    buffer->used = true;

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
  buffer->used = true;

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
  buffer->used = false;

  GST_TRACE ("Released staging buffer at index %u, data %p and size %u",
      buffer->idx, buffer->data, buffer->size);
}

// TODO: Try to remove this
static inline gboolean
gst_ocv_video_converter_stage_object_init (GstOcvVideoConverter * convert,
    GstOcvObject * obj, guint width, guint height, GstVideoFormat format)
{
  GstOcvStageBuffer *buffer = NULL;
  guint idx = 0, size = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].channels = CV_8UC1;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_GRAY;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[0].channels = CV_8UC3;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 3;
      obj->planes[0].channels = CV_8UC3;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 4;
      obj->planes[0].channels = CV_8UC3;
      obj->n_planes = 1;
      obj->flags = GST_OCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].channels = CV_8UC1;
      obj->planes[1].width = GST_ROUND_UP_8 (width) / 2;
      obj->planes[1].height =  GST_ROUND_UP_2 (height) / 2;
      obj->planes[1].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].channels = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].channels = CV_8UC1;
      obj->planes[1].width = GST_ROUND_UP_8 (width) / 2;
      obj->planes[1].height =  height;
      obj->planes[1].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].channels = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_NV24:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[0].channels = CV_8UC1;
      obj->planes[1].width = GST_ROUND_UP_8 (width) * 2;
      obj->planes[1].height =  height;
      obj->planes[1].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[1].channels = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[0].channels = CV_8UC1;
      obj->planes[1].width = GST_ROUND_UP_8 (width);
      obj->planes[1].height =  GST_ROUND_UP_2 (height) / 2;
      obj->planes[1].stride = GST_ROUND_UP_8 (width) * 2;
      obj->planes[1].channels = CV_8UC2;
      obj->n_planes = 2;
      obj->flags = GST_OCV_FLAG_YUV;
      break;
    default:
      GST_ERROR ("Unknown format %s", gst_video_format_to_string (format));
      return false;
  }

  obj->format = format;
  obj->flags |= GST_OCV_FLAG_STAGED;

  obj->flip = GST_VCE_FLIP_NONE;
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

  return true;
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
  switch (s_obj->format + (d_obj->format << 16)) {
    // YUV to RGB
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_NV12;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_YUV2RGB_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_YUV2BGR_NV21;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_NV12;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_YUV2RGBA_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_YUV2BGRA_NV21;
    // YUV to RGBx; Might not work.
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_YUV2RGBA_NV12;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_YUV2BGRA_NV12;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGBx << 16):
      return cv::COLOR_YUV2RGBA_NV21;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGRx << 16):
      return cv::COLOR_YUV2BGRA_NV21;
    // RGB to YUV
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV12 << 16):
      return cv::COLOR_RGB2YUV_I420;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV21 << 16):
      return cv::COLOR_RGB2YUV_YV12;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV12 << 16):
      return cv::COLOR_BGR2YUV_I420;
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV21 << 16):
      return cv::COLOR_BGR2YUV_YV12;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_NV12 << 16):
      return cv::COLOR_RGBA2YUV_I420;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_NV21 << 16):
      return cv::COLOR_RGBA2YUV_YV12;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_NV12 << 16):
      return cv::COLOR_BGRA2YUV_I420;
    case GST_VIDEO_FORMAT_BGRA + (GST_VIDEO_FORMAT_NV21 << 16):
      return cv::COLOR_BGRA2YUV_YV12;
    // RGBx to YUV; Might not work.
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_NV12 << 16):
      return cv::COLOR_RGBA2YUV_I420;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_NV21 << 16):
      return cv::COLOR_RGBA2YUV_YV12;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_NV12 << 16):
      return cv::COLOR_BGRA2YUV_I420;
    case GST_VIDEO_FORMAT_BGRx + (GST_VIDEO_FORMAT_NV21 << 16):
      return cv::COLOR_BGRA2YUV_YV12;
    // RGB to RGB
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_RGB2BGR;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_RGB2BGRA;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGRx << 16): // RGBx !!
      return cv::COLOR_RGB2BGRA;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGBA << 16):
      return cv::COLOR_RGB2RGBA;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGBx << 16): // RGBx !!
      return cv::COLOR_RGB2RGBA;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_RGB << 16):
      return cv::COLOR_RGBA2RGB;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGR << 16):
      return cv::COLOR_RGBA2BGR;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGRA << 16):
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGRx << 16): // RGBx !!
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_RGB << 16): // RGBx !!
      return cv::COLOR_RGBA2RGB;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGR << 16): // RGBx !!
      return cv::COLOR_RGBA2BGR;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGRA << 16): // RGBx !!
      return cv::COLOR_RGBA2BGRA;
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGRx << 16): // RGBx !!
      return cv::COLOR_RGBA2BGRA;
    default:
      GST_ERROR ("Unsupported format conversion from '%s' to '%s'!",
          gst_video_format_to_string (s_obj->format),
          gst_video_format_to_string (d_obj->format));
      return 0;
  }
}

static inline gboolean
gst_ocv_video_converter_rotate (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  GstVideoConvFlip flip = GST_VCE_FLIP_NONE;
  gint rotate = 0, idx = 0;
  gboolean resize = false, cvt_color = false;

  // Cache the flip, rotation, resize and color convert flags.
  flip = s_obj->flip;
  rotate = GST_OCV_GET_ROTATE (s_obj->rotate);
  resize = s_obj->resize;
  cvt_color = s_obj->cvt_color;

  // Use stage object if other operations are pending
  if (s_obj->resize || s_obj->flip || s_obj->cvt_color) {
    guint width = 0, height = 0;
    gboolean success = false;

    GST_DEBUG ("In rotate staging block");

    width = s_obj->planes[0].width;
    height = s_obj->planes[0].height;

    // Dimensions are swapped if 90/270 degree rotation is required.
    if (rotate == cv::ROTATE_90_CLOCKWISE || rotate == cv::ROTATE_90_COUNTERCLOCKWISE) {
      width = s_obj->planes[0].height;
      height = s_obj->planes[0].width;
    }

    // Temporary store the destination object data into local intermediary.
    gst_ocv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_ocv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, false);
  }

  GST_INFO ("OCV rotate: %d; GST rotate: %d", rotate, s_obj->rotate);

  if (((s_obj->flags & GST_OCV_FLAG_YUV) || (s_obj->flags & GST_OCV_FLAG_RGB)) &&
      ((d_obj->flags & GST_OCV_FLAG_YUV) || (d_obj->flags & GST_OCV_FLAG_RGB))) {
    for (idx = 0; idx < s_obj->n_planes; idx++) {
      GstOcvPlane *src_plane = &s_obj->planes[idx], *dst_plane = &d_obj->planes[idx];

      GST_DEBUG ("Creating %dx%d src matrix from plane %d with channel type %d",
          src_plane->height, src_plane->width, idx, src_plane->channels);

      cv::Mat src_mat (src_plane->height, src_plane->width, src_plane->channels,
          src_plane->data, src_plane->stride);

      GST_DEBUG ("Creating %dx%d dest matrix from plane %d with channel type %d",
          dst_plane->height, dst_plane->width, idx, dst_plane->channels);

      cv::Mat dst_mat (dst_plane->height, dst_plane->width, dst_plane->channels,
          dst_plane->data, dst_plane->stride);

      cv::rotate (src_mat, dst_mat, rotate);
    }
  } else {
    GST_WARNING ("Unknown format, or src and dst plane formats don't match!");
    return false;
  }

  GST_DEBUG ("Rotated all planes");

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending resize, flip and color convert and reset rotate
  s_obj->flip = flip;
  s_obj->rotate = GST_VCE_ROTATE_0;
  s_obj->cvt_color = cvt_color;
  s_obj->resize = resize;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_copy_object (&l_obj, d_obj);

  return true;
}

static inline gboolean
gst_ocv_video_converter_flip (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  gint flip = 0, idx = 0;
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  gboolean resize = false, cvt_color = false;

  // Cache the flip, rotation, resize and color convert flags.
  flip = GST_OCV_GET_FLIP (s_obj->flip);
  rotate = s_obj->rotate;
  resize = s_obj->resize;
  cvt_color = s_obj->cvt_color;

  // Use stage object if other operations are pending
  if (s_obj->resize || s_obj->rotate || s_obj->cvt_color) {
    guint width = 0, height = 0;
    gboolean success = false;

    GST_DEBUG ("In flip staging block");

    width = s_obj->planes[0].width;
    height = s_obj->planes[0].height;

    // Dimensions are swapped if 90/270 degree rotation is required with resize.
    if (resize && (rotate == GST_VCE_ROTATE_90 || rotate == GST_VCE_ROTATE_270)) {
      width = s_obj->planes[0].height;
      height = s_obj->planes[0].width;
    }

    // Temporary store the destination object data into local intermediary.
    gst_ocv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_ocv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, false);
  }

  GST_INFO ("OCV flip: %d; GST flip: %d", flip, s_obj->flip);

  if (((s_obj->flags & GST_OCV_FLAG_YUV) || (s_obj->flags & GST_OCV_FLAG_RGB)) &&
      ((d_obj->flags & GST_OCV_FLAG_YUV) || (d_obj->flags & GST_OCV_FLAG_RGB))) {
    for (idx = 0; idx < s_obj->n_planes; idx++) {
      GstOcvPlane *src_plane = &s_obj->planes[idx], *dst_plane = &d_obj->planes[idx];

      GST_DEBUG ("Creating %dx%d src matrix from plane %d with channel type %d",
          src_plane->height, src_plane->width, idx, src_plane->channels);

      cv::Mat src_mat (src_plane->height, src_plane->width, src_plane->channels,
          src_plane->data, src_plane->stride);

      GST_DEBUG ("Creating %dx%d dest matrix from plane %d with channel type %d",
          dst_plane->height, dst_plane->width, idx, dst_plane->channels);

      cv::Mat dst_mat (dst_plane->height, dst_plane->width, dst_plane->channels,
          dst_plane->data, dst_plane->stride);

      cv::flip (src_mat, dst_mat, flip);
    }
  } else {
    GST_WARNING ("Unknown format, or src and dst plane formats don't match!");
    return false;
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending resize, rotate and color convert and reset flip
  s_obj->flip = GST_VCE_FLIP_NONE;
  s_obj->rotate = rotate;
  s_obj->cvt_color = cvt_color;
  s_obj->resize = resize;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED) {
    gst_ocv_copy_object (&l_obj, d_obj);
    GST_DEBUG ("Unstage in flip");
  }

  return true;
}

static inline gboolean
gst_ocv_video_converter_resize (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  GstOcvObject l_obj = {};
  gint idx = 0;
  GstVideoConvFlip flip = GST_VCE_FLIP_NONE;
  GstVideoConvRotate rotate = GST_VCE_ROTATE_0;
  gboolean cvt_color = false;

  // Cache the flip, rotation, resize and color convert flags.
  flip = s_obj->flip;
  rotate = s_obj->rotate;
  cvt_color = s_obj->cvt_color;

  // Use stage object if other operations are pending
  if (s_obj->flip || s_obj->rotate || s_obj->cvt_color) {
    guint width = 0, height = 0;
    gboolean success = false;

    GST_DEBUG ("In resize staging block");

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
    g_return_val_if_fail (success, false);
  }

  if (((s_obj->flags & GST_OCV_FLAG_YUV) || (s_obj->flags & GST_OCV_FLAG_RGB)) &&
      ((d_obj->flags & GST_OCV_FLAG_YUV) || (d_obj->flags & GST_OCV_FLAG_RGB))) {
    for (idx = 0; idx < s_obj->n_planes; idx++) {
      GstOcvPlane *src_plane = &s_obj->planes[idx], *dst_plane = &d_obj->planes[idx];

      GST_DEBUG ("Creating %dx%d src matrix from plane %d with channel type %d",
          src_plane->height, src_plane->width, idx, src_plane->channels);

      cv::Mat src_mat (src_plane->height, src_plane->width, src_plane->channels,
          src_plane->data, src_plane->stride);

      GST_DEBUG ("Creating %dx%d dest matrix from plane %d with channel type %d",
          dst_plane->height, dst_plane->width, idx, dst_plane->channels);

      cv::Mat dst_mat (dst_plane->height, dst_plane->width, dst_plane->channels,
          dst_plane->data, dst_plane->stride);

      cv::resize (src_mat, dst_mat, dst_mat.size(), 0, 0);
    }
  } else {
    GST_WARNING ("Unknown format!");
    return false;
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  // Transfer any pending rotate, flip and color convert and reset resize
  s_obj->flip = flip;
  s_obj->rotate = rotate;
  s_obj->cvt_color = cvt_color;
  s_obj->resize = false;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_copy_object (&l_obj, d_obj);

  return true;
}

static inline gboolean
gst_ocv_video_converter_cvt_color (GstOcvVideoConverter * convert,
    GstOcvObject * s_obj, GstOcvObject * d_obj)
{
  gboolean success = false;
  cv::Mat y_plane (s_obj->planes[0].height, s_obj->planes[0].width,
      s_obj->planes[0].channels, s_obj->planes[0].data, s_obj->planes[0].stride);
  cv::Mat uv_plane (s_obj->planes[1].height, s_obj->planes[1].width,
      s_obj->planes[1].channels, s_obj->planes[1].data, s_obj->planes[1].stride);

  GST_INFO ("created y_plane with %d rows (height) and %d cols (width) from"
      "s_obj->plane[0] with .width: %d; .height: %d; .stide: %d",
      y_plane.rows, y_plane.cols,
      s_obj->planes[0].width, s_obj->planes[0].height, s_obj->planes[0].stride);

  GST_INFO ("created uv_plane with %d rows (height) and %d cols (width) from"
      "s_obj->plane[1] with .width: %d; .height: %d; .stide: %d",
      uv_plane.rows, uv_plane.cols,
      s_obj->planes[1].width, s_obj->planes[1].height, s_obj->planes[1].stride);

  if (((s_obj->flags & GST_OCV_FLAG_YUV) || (s_obj->flags & GST_OCV_FLAG_RGB)) &&
      ((d_obj->flags & GST_OCV_FLAG_YUV) || (d_obj->flags & GST_OCV_FLAG_RGB))) {
    gint conversion_mode = gst_ocv_get_conversion_mode (s_obj, d_obj);

    if (conversion_mode) {
      cv::Mat output_matrix (d_obj->planes[0].height, d_obj->planes[0].width,
          d_obj->planes[0].channels, d_obj->planes[0].data, d_obj->planes[0].stride);

      GST_INFO ("created output_matrix with %d rows (height) and %d cols (width)"
          " from d_obj->plane[0] with .width: %d; .height: %d; .stide: %d",
          output_matrix .rows, output_matrix.cols, d_obj->planes[0].width,
          d_obj->planes[0].height, d_obj->planes[0].stride);

      GST_INFO ("before conversion output_matrix has size: rows: %d, cols: %d",
          output_matrix.rows, output_matrix.cols);

      GST_INFO ("y_plane type: %d, size:%dx%d", y_plane.type(), y_plane.cols,
          y_plane.rows);
      GST_INFO ("uv_plane type: %d, size:%dx%d", uv_plane.type(), uv_plane.cols,
          uv_plane.rows);
      GST_INFO ("output_matrix type: %d, size:%dx%d", output_matrix.type(),
          output_matrix.cols, output_matrix.rows);

      cv::cvtColorTwoPlane (y_plane, uv_plane, output_matrix,
          gst_ocv_get_conversion_mode (s_obj, d_obj));
      GST_WARNING ("after conversion output_matrix has size: rows: %d, cols: %d",
          output_matrix.rows, output_matrix.cols);

      success = true;
    } else {
      GST_ERROR ("Unsupported color conversion mode!");
    }
  } else {
    GST_ERROR ("Unsupported color conversion families!");
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_OCV_FLAG_STAGED)
    gst_ocv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_ocv_copy_object (d_obj, s_obj);

  return success;
}

static inline gboolean
gst_ocv_video_converter_process (GstOcvVideoConverter * convert,
    GstOcvObject * objects, guint n_objects, GstVideoFrame * outframe)
{
  GstOcvObject *s_obj = NULL, *d_obj = NULL;
  guint idx = 0, flip = 0, rotate = 0;
  gfloat w_scale = 0.0, h_scale = 0.0, scale = 0.0;
  gboolean downscale = false, upscale = false, cvt_color = false;

  GST_WARNING ("In function ocv_process, n_objects is: %d", n_objects);

  for (idx = 0; idx < n_objects; idx += 2) {
    s_obj = &(objects[idx]);
    d_obj = &(objects[idx + 1]);

    GST_INFO ("In process: s_obj: plane[0].width: %d; plane[0].height: %d; "
        "plane[0].stride: %d", s_obj->planes[0].width, s_obj->planes[0].height,
        s_obj->planes[0].stride);
    GST_INFO ("In process: s_obj: plane[1].width: %d; plane[1].height: %d; "
        "plane[b].stride: %d", s_obj->planes[1].width, s_obj->planes[1].height,
        s_obj->planes[1].stride);
    GST_INFO ("In process: d_obj: plane[0].width: %d; plane[0].height: %d; "
        "plane[0].stride: %d", d_obj->planes[0].width, d_obj->planes[0].height,
        d_obj->planes[0].stride);
    GST_INFO ("In process: d_obj: plane[1].width: %d; plane[1].height: %d; "
        "plane[1].stride: %d", d_obj->planes[1].width, d_obj->planes[1].height,
        d_obj->planes[1].stride);

    flip = s_obj->flip;
    rotate = s_obj->rotate;

    w_scale = ((gfloat) d_obj->planes[0].height) / s_obj->planes[0].width;
    h_scale = ((gfloat) d_obj->planes[0].width) / s_obj->planes[0].height;

    // Calculate the combined scale factor.
    scale = w_scale * h_scale;

    // Use downscale if output is smaller or for simple copy of a region.
    downscale = (scale < 1.0) || ((w_scale == 1.0) && (h_scale == 1.0) &&
        (rotate == 0) && (flip == 0) && (s_obj->format == d_obj->format) &&
        (s_obj->format != GST_VIDEO_FORMAT_P010_10LE));

    // Use upscale if output is bigger or same scale but reversed dimensions.
    upscale = (scale > 1.0) ||
        (scale == 1.0 && w_scale != 1.0 && h_scale != 1.0 && rotate == 0);

    cvt_color = s_obj->format != d_obj->format;

    GST_INFO ("Starting processing; flip is: %d, rotate: %d, downscale: %d, "
        "upscale: %d, scale: %f, color convert: %d", flip, rotate, downscale,
        upscale, scale, cvt_color);

    if ((rotate == GST_VCE_ROTATE_90) || (rotate == GST_VCE_ROTATE_270)) {
      s_obj->resize = ((s_obj->planes[0].width != d_obj->planes[0].height) ||
          (s_obj->planes[0].height != d_obj->planes[0].width)) ? true : false;
    } else {
      s_obj->resize = ((s_obj->planes[0].width != d_obj->planes[0].width) ||
          (s_obj->planes[0].height != d_obj->planes[0].height)) ? true : false;
    }
    s_obj->cvt_color = cvt_color;

    // First, do downscale if required so that next operations are less costly.
    if (downscale && !gst_ocv_video_converter_resize (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to resize image!");
      return false;
    }

    // Second, perform image rotate if necessary.
    if ((rotate != 0) && !gst_ocv_video_converter_rotate (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to rotate image!");
      return false;
    }

    // Third, perform image flip if necessary.
    if ((flip != 0) && !gst_ocv_video_converter_flip (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to flip image!");
      return false;
    }

    // Fourth, perform color conversion if necessary.
    if (cvt_color && !gst_ocv_video_converter_cvt_color (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to convert image format!");
      return false;
    }

    // Fifth, upscale output if needed
    if (upscale && !gst_ocv_video_converter_resize (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to upscale image!");
      return false;
    }
  }

  return true;
}

gboolean
gst_ocv_video_converter_compose (GstOcvVideoConverter * convert,
    GstVideoComposition * compositions, guint n_compositions, gpointer * fence)
{
  GstOcvObject objects[GST_OCV_MAX_DRAW_OBJECTS] = {};
  guint32 idx = 0, n_objects = 0, /*area = 0, */num = 0;

  // TODO: Implement async operations via threads.
  if (fence != NULL)
    GST_WARNING ("Asynchronous composition operations are not supported!");

  for (idx = 0; idx < n_compositions; idx++) {
    GstVideoFrame *outframe = compositions[idx].frame;
    GstVideoBlit *blits = compositions[idx].blits;
    guint n_blits = compositions[idx].n_blits;

    // Iterate over the input blit entries and update each OCV object.
    for (num = 0; num < n_blits; num++) {
      GstVideoBlit *blit = &(blits[num]);
      GstOcvObject *object = NULL;

      if (n_objects >= GST_OCV_MAX_DRAW_OBJECTS) {
        GST_ERROR ("Number of objects exceeds %d!", GST_OCV_MAX_DRAW_OBJECTS);
        return false;
      }

      // Intialization of the source OCV object.
      object = &(objects[n_objects]);

      gst_ocv_update_object (object, "Source", blit->frame, &(blit->source),
          blit->flip, blit->rotate, 0);

      // Intialization of the destination OCV object.
      object = &(objects[n_objects + 1]);

      gst_ocv_update_object (object, "Destination", outframe, &(blit->destination),
          GST_VCE_FLIP_NONE, GST_VCE_ROTATE_0, compositions[idx].flags);

      // Increment the objects counter by 2 for for Source/Destination pair.
      n_objects += 2;
    }

    // Sanity checks, output frame and blit entries must not be NULL.
    g_return_val_if_fail (outframe != NULL && blits != NULL, false);

    for (num = 0; num < n_blits; num++) {
      if (!gst_ocv_video_converter_process (convert, objects, n_objects, outframe)) {
        GST_ERROR ("Failed to process frames for composition %u!", idx);
        return false;
      }
    }
  }

  return true;
}

gboolean
gst_ocv_video_converter_wait_fence (GstOcvVideoConverter * convert, gpointer fence)
{
  GST_WARNING ("Not implemented!");

  return true;
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
  gboolean success = true;

  convert = g_slice_new0 (GstOcvVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  // Check whether symbol loading was successful.
  if (!success)
    goto cleanup;

  convert->stgbufs = g_array_new (false, true, sizeof (GstOcvStageBuffer));
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
    g_array_free (convert->stgbufs, true);

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed OpenCV converter: %p", convert);
  g_slice_free (GstOcvVideoConverter, convert);
}
