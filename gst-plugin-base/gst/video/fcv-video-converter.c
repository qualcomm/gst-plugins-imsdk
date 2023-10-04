/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "fcv-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>

#include <fastcv/fastcv.h>


#define GST_CAT_DEFAULT gst_video_converter_engine_debug

#define LOAD_FCV_SYMBOL(c, name) \
  load_symbol ((gpointer*)&(c->name), c->fcvhandle, "fcv"#name);

// Convinient macros for calling the FastCV functions.
#define GST_FCV_YUV_TO_YUV(c, in, out, s_luma, s_chroma, d_luma, d_chroma) \
    c->ColorYCbCr##in##PseudoPlanarToYCbCr##out##PseudoPlanaru8 (          \
        s_luma->data, s_chroma->data, s_luma->width, s_luma->height,       \
        s_luma->stride, s_chroma->stride, d_luma->data, d_chroma->data,    \
        d_luma->stride, d_chroma->stride)
#define GST_FCV_YUV_TO_RGB(c, in, out, s_luma, s_chroma, d_rgb)        \
    c->ColorYCbCr##in##PseudoPlanarTo##out##u8 (                       \
        s_luma->data, s_chroma->data, s_luma->width, s_luma->height,   \
        s_luma->stride, s_chroma->stride, d_rgb->data,  d_rgb->stride)
#define GST_FCV_RGB_TO_YUV(c, in, out, s_rgb, d_luma, d_chroma)         \
    c->Color##in##ToYCbCr##out##PseudoPlanaru8 (                        \
        s_rgb->data, s_rgb->width, s_rgb->height, s_rgb->stride,        \
        d_luma->data, d_chroma->data, d_luma->stride, d_chroma->stride)
#define GST_FCV_RGB_TO_RGB(c, in, out, s_rgb, d_rgb)                     \
    c->Color##in##To##out##u8 (s_rgb->data, s_rgb->width, s_rgb->height, \
        s_rgb->stride, d_rgb->data, d_rgb->stride)
#define GST_FCV_CHROMA_SWAP(c, s_chroma, d_chroma)                         \
    c->ColorCbCrSwapu8 (s_chroma->data, s_chroma->width, s_chroma->height, \
        s_chroma->stride, d_chroma->data, d_chroma->stride)
#define GST_FCV_SCALE_LUMA(c, s_luma, d_luma)                                   \
    c->Scaleu8_v2 (s_luma->data, s_luma->width, s_luma->height, s_luma->stride, \
        d_luma->data, d_luma->width, d_luma->height, d_luma->stride,            \
        FASTCV_INTERPOLATION_TYPE_NEAREST_NEIGHBOR, FASTCV_BORDER_REPLICATE, 0)
#define GST_FCV_SCALE_DOWN_CHROMA(c, s_chroma, d_chroma)                     \
    c->ScaleDownMNInterleaveu8 (s_chroma->data, s_chroma->width,             \
        s_chroma->height, s_chroma->stride, d_chroma->data, d_chroma->width, \
        d_chroma->height, d_chroma->stride);
#define GST_FCV_SCALE_UP_CHROMA(c, s_chroma, d_chroma)                       \
    c->ScaleUpPolyInterleaveu8 (s_chroma->data, s_chroma->width,             \
        s_chroma->height, s_chroma->stride, d_chroma->data, d_chroma->width, \
        d_chroma->height, d_chroma->stride);
#define GST_FCV_ROTATE_LUMA(c, s_luma, d_luma, rotate)             \
    c->RotateImageu8 (s_luma->data, s_luma->width, s_luma->height, \
        s_luma->stride, d_luma->data, d_luma->stride, rotate)
#define GST_FCV_ROTATE_CHROMA(c, s_chroma, d_chroma, rotate)                  \
    c->RotateImageInterleavedu8 (s_chroma->data, s_chroma->width,             \
        s_chroma->height, s_chroma->stride, d_chroma->data, d_chroma->stride, \
        rotate)
#define GST_FCV_FLIP_LUMA(c, s_luma, d_luma, flip)                           \
    c->Flipu8 (s_luma->data, s_luma->width, s_luma->height,  s_luma->stride, \
        d_luma->data, d_luma->stride, flip)
#define GST_FCV_FLIP_CHROMA(c, s_chroma, d_chroma, flip)           \
    c->Flipu16 (s_chroma->data, s_chroma->width, s_chroma->height, \
        s_chroma->stride, d_chroma->data, d_chroma->stride, flip)

// Convinient macros for printing plane values.
#define GST_FCV_PLANE_FORMAT "ux%u Stride[%u] Data[%p]"
#define GST_FCV_PLANE_ARGS(plane) \
    (plane)->width, (plane)->height, (plane)->stride, (plane)->data

#define EXTRACT_RED_VALUE(color)    ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_VALUE(color)  ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_VALUE(color)   ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_VALUE(color)  ((color) & 0xFF)

#define GST_FCV_GET_LOCK(obj)       (&((GstFcvVideoConverter *)obj)->lock)
#define GST_FCV_LOCK(obj)           g_mutex_lock (GST_FCV_GET_LOCK(obj))
#define GST_FCV_UNLOCK(obj)         g_mutex_unlock (GST_FCV_GET_LOCK(obj))

#define GST_FCV_INVALID_STAGE_ID    (-1)
#define GST_FCV_MAX_DRAW_OBJECTS    50

#define GST_FCV_WIDTH_ALIGN         8

typedef struct _GstFcvPlane GstFcvPlane;
typedef struct _GstFcvObject GstFcvObject;
typedef struct _GstFcvStageBuffer GstFcvStageBuffer;

enum {
  GST_FCV_FLAG_GRAY   = (1 << 0),
  GST_FCV_FLAG_RGB    = (1 << 1),
  GST_FCV_FLAG_YUV    = (1 << 2),
  GST_FCV_FLAG_STAGED = (1 << 3),
  GST_FCV_FLAG_I32    = (1 << 4),
  GST_FCV_FLAG_U32    = (1 << 5),
  GST_FCV_FLAG_F16    = (1 << 6),
  GST_FCV_FLAG_F32    = (1 << 7),
};

/**
 * GstFcvPlane:
 * @stgid: Index of the used staging buffer or -1 if created from original frame.
 * @width: Width of the plane in pixels.
 * @height: Height of the plane in pixels.
 * @data: Pointer to bytes of data.
 * @stride: Aligned width of the plane in bytes.
 *
 * Blit plane.
 */
struct _GstFcvPlane
{
  gint     stgid;
  guint32  width;
  guint32  height;
  gpointer data;
  guint32  stride;
};

/**
 * GstFcvObject:
 * @format: Gstreamer video format.
 * @flags: Bit mask containing format family.
 * @flip: Flip direction or 0 if none.
 * @rotate: Clockwise rotation degrees or 0 if none.
 * @planes: Array of blit planes.
 * @n_planes: Number of used planes based on format.
 *
 * Blit object.
 */
struct _GstFcvObject
{
  GstVideoFormat format;
  guint32        flags;

  guint          rotate;
  guint          flip;

  GstFcvPlane    planes[GST_VIDEO_MAX_PLANES];
  guint8         n_planes;
};

/**
 * GstFcvStageBuffer:
 * @idx: Index of in the staging list.
 * @data: Pointer to bytes of data.
 * @size: Total number of bytes.
 * @used: Whether the buffer is currently used by some operaion.
 *
 * Blit staging buffer.
 */
struct _GstFcvStageBuffer
{
  guint    idx;
  gpointer data;
  guint    size;
  gboolean used;
};

struct _GstFcvVideoConverter
{
  // Global mutex lock.
  GMutex   lock;

  // Staging buffers used as intermediaries during the FastCV operations.
  GArray   *stgbufs;

  // FastCV library handle.
  gpointer fcvhandle;

  // FastCV library APIs.
  FASTCV_API int (*SetOperationMode) (fcvOperationMode mode);
  FASTCV_API void (*CleanUp) (void);

  FASTCV_API void (*SetElementsu8) (
      uint8_t *__restrict destination, uint32_t d_width, uint32_t d_height,
      uint32_t d_stride, uint8_t value, const uint8_t *__restrict mask,
      uint32_t m_stride);
  FASTCV_API void (*SetElementsc3u8) (
      uint8_t *__restrict destination, uint32_t d_width, uint32_t d_height,
      uint32_t d_stride, uint8_t value1, uint8_t value2, uint8_t value3,
      const uint8_t *__restrict mask, uint32_t m_stride);
  FASTCV_API void  (*SetElementsc4u8) (
      uint8_t *__restrict destination, uint32_t d_width, uint32_t d_height,
      uint32_t d_stride, uint8_t value1, uint8_t value2, uint8_t value3,
      uint8_t value4, const uint8_t *__restrict mask, uint32_t m_stride);

  FASTCV_API void (*Flipu8) (
      const uint8_t *source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *destination, uint32_t d_stride,
      fcvFlipDir direction);
  FASTCV_API void (*Flipu16) (
      const uint16_t *source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint16_t *destination, uint32_t d_stride,
      fcvFlipDir direction);
  FASTCV_API fcvStatus (*RotateImageu8) (
      const uint8_t *source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride,uint8_t *destination, uint32_t d_stride,
      fcvRotateDegree degree);
  FASTCV_API fcvStatus (*RotateImageInterleavedu8) (
      const uint8_t *source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *destination, uint32_t d_stride,
      fcvRotateDegree degree);

  FASTCV_API fcvStatus (*Scaleu8_v2) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_width,
      uint32_t d_height, uint32_t d_stride, fcvInterpolationType interpolation,
      fcvBorderType border_type, uint8_t border_value);
  FASTCV_API void (*ScaleUpPolyInterleaveu8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_width,
      uint32_t d_height, uint32_t d_stride);
  FASTCV_API void (*ScaleDownMNInterleaveu8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_width,
      uint32_t d_height, uint32_t d_stride);

  FASTCV_API void (*ColorCbCrSwapu8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);

  FASTCV_API void (*ColorYCbCr420PseudoPlanarToYCbCr444PseudoPlanaru8) (
      const uint8_t *s_luma, const uint8_t *__restrict s_chroma, uint32_t s_width,
      uint32_t s_height, uint32_t s_luma_stride, uint32_t s_chroma_stride,
      uint8_t *d_luma, uint8_t *__restrict d_chroma, uint32_t d_luma_stride,
      uint32_t d_chroma_stride);
  FASTCV_API void (*ColorYCbCr420PseudoPlanarToYCbCr422PseudoPlanaru8) (
      const uint8_t *s_luma, const uint8_t *__restrict s_chroma, uint32_t s_width,
      uint32_t s_height, uint32_t s_luma_stride, uint32_t s_chroma_stride,
      uint8_t *d_luma, uint8_t *__restrict d_chroma, uint32_t d_luma_stride,
      uint32_t d_chroma_stride);

  FASTCV_API void (*ColorYCbCr422PseudoPlanarToYCbCr444PseudoPlanaru8) (
      const uint8_t *s_luma, const uint8_t *__restrict s_chroma, uint32_t s_width,
      uint32_t s_height, uint32_t s_luma_stride, uint32_t s_chroma_stride,
      uint8_t *d_luma, uint8_t *__restrict d_chroma, uint32_t d_luma_stride,
      uint32_t d_chroma_stride);
  FASTCV_API void (*ColorYCbCr422PseudoPlanarToYCbCr420PseudoPlanaru8) (
      const uint8_t *s_luma, const uint8_t *__restrict s_chroma, uint32_t s_width,
      uint32_t s_height, uint32_t s_luma_stride, uint32_t s_chroma_stride,
      uint8_t *d_luma, uint8_t *__restrict d_chroma, uint32_t d_luma_stride,
      uint32_t d_chroma_stride);

  FASTCV_API void (*ColorYCbCr444PseudoPlanarToYCbCr422PseudoPlanaru8) (
      const uint8_t *s_luma, const uint8_t *__restrict s_chroma, uint32_t s_width,
      uint32_t s_height, uint32_t s_luma_stride, uint32_t s_chroma_stride,
      uint8_t *d_luma, uint8_t *__restrict d_chroma, uint32_t d_luma_stride,
      uint32_t d_chroma_stride);
  FASTCV_API void (*ColorYCbCr444PseudoPlanarToYCbCr420PseudoPlanaru8) (
      const uint8_t *s_luma, const uint8_t *__restrict s_chroma, uint32_t s_width,
      uint32_t s_height, uint32_t s_luma_stride, uint32_t s_chroma_stride,
      uint8_t *d_luma, uint8_t *__restrict d_chroma, uint32_t d_luma_stride,
      uint32_t d_chroma_stride);

  FASTCV_API void (*ColorYCbCr420PseudoPlanarToRGB565u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorYCbCr420PseudoPlanarToRGB888u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorYCbCr420PseudoPlanarToRGBA8888u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);

  FASTCV_API void (*ColorYCbCr422PseudoPlanarToRGB565u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorYCbCr422PseudoPlanarToRGB888u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorYCbCr422PseudoPlanarToRGBA8888u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);

  FASTCV_API void (*ColorYCbCr444PseudoPlanarToRGB565u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorYCbCr444PseudoPlanarToRGB888u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorYCbCr444PseudoPlanarToRGBA8888u8) (
      const uint8_t *__restrict s_luma, const uint8_t *__restrict s_chroma,
      uint32_t s_width, uint32_t s_height, uint32_t s_luma_stride,
      uint32_t s_chroma_stride, uint8_t *__restrict destination, uint32_t d_stride);

  FASTCV_API void (*ColorRGB565ToYCbCr444PseudoPlanaru8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict d_luma, uint8_t *__restrict d_chroma,
      uint32_t d_luma_stride, uint32_t d_chroma_stride);
  FASTCV_API void (*ColorRGB565ToYCbCr422PseudoPlanaru8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict d_luma, uint8_t *__restrict d_chroma,
      uint32_t d_luma_stride, uint32_t d_chroma_stride);
  FASTCV_API void (*ColorRGB565ToYCbCr420PseudoPlanaru8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict d_luma, uint8_t *__restrict d_chroma,
      uint32_t d_luma_stride, uint32_t d_chroma_stride);

  FASTCV_API void (*ColorRGB888ToYCbCr444PseudoPlanaru8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict d_luma, uint8_t *__restrict d_chroma,
      uint32_t d_luma_stride, uint32_t d_chroma_stride);
  FASTCV_API void (*ColorRGB888ToYCbCr422PseudoPlanaru8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict d_luma, uint8_t *__restrict d_chroma,
      uint32_t d_luma_stride, uint32_t d_chroma_stride);
  FASTCV_API void (*ColorRGB888ToYCbCr420PseudoPlanaru8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict d_luma, uint8_t *__restrict d_chroma,
      uint32_t d_luma_stride, uint32_t d_chroma_stride);

  FASTCV_API void (*ColorRGB565ToBGR565u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB565ToRGB888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB565ToRGBA8888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB565ToBGR888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB565ToBGRA8888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);

  FASTCV_API void (*ColorRGB888ToBGR888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB888ToRGB565u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB888ToRGBA8888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB888ToBGR565u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGB888ToBGRA8888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);

  FASTCV_API void (*ColorRGBA8888ToBGRA8888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGBA8888ToRGB565u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGBA8888ToRGB888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGBA8888ToBGR565u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
  FASTCV_API void (*ColorRGBA8888ToBGR888u8) (
      const uint8_t *__restrict source, uint32_t s_width, uint32_t s_height,
      uint32_t s_stride, uint8_t *__restrict destination, uint32_t d_stride);
};

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);

  if (NULL == *(method)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }

  return TRUE;
}

static inline gint
gst_fcv_get_opmode (const GstStructure * settings)
{
  gint value = GST_FCV_OP_MODE_PERFORMANCE;

  if ((settings == NULL) ||
      !gst_structure_has_field (settings, GST_VCE_OPT_FCV_OP_MODE))
    return value;

  gst_structure_get_enum (settings, GST_VCE_OPT_FCV_OP_MODE, G_TYPE_ENUM, &value);
  return value;
}

static inline void
gst_fcv_stage_buffer_free (gpointer data)
{
  GstFcvStageBuffer *buffer = (GstFcvStageBuffer *) data;
  g_free (buffer->data);
}

static inline guint
gst_fcv_translate_flip_flags (const guint64 flags)
{
  guint64 mask = GST_VCE_FLAG_FLIP_V | GST_VCE_FLAG_FLIP_H;

  switch (flags & mask) {
    case (GST_VCE_FLAG_FLIP_V | GST_VCE_FLAG_FLIP_H):
      return FASTCV_FLIP_BOTH;
    case GST_VCE_FLAG_FLIP_V:
      return FASTCV_FLIP_VERT;
    case GST_VCE_FLAG_FLIP_H:
      return FASTCV_FLIP_HORIZ;
    default:
      break;
  }

  return 0;
}

static inline guint
gst_fcv_translate_rotation_flags (const guint64 flags)
{
  switch (flags & GST_VCE_ROTATION_MASK) {
    case GST_VCE_FLAG_ROTATE_90:
      return FASTCV_ROTATE_90;
    case GST_VCE_FLAG_ROTATE_180:
      return FASTCV_ROTATE_180;
    case GST_VCE_FLAG_ROTATE_270:
      return FASTCV_ROTATE_270;
    default:
      break;
  }

  return 0;
}

static inline guint
gst_fcv_regions_overlapping_area (GstVideoRectangle * l_rect,
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
gst_fcv_composition_blit_area (GstVideoFrame * outframe, GstVideoBlit * blits,
    guint index, guint r_idx)
{
  GstVideoBlit *blit = NULL;
  GstVideoRectangle *region = NULL, *l_region = NULL;
  guint num = 0, n = 0, area = 0;

  // Fetch the blit at current index to which we will compare all others.
  blit = &(blits[index]);

  // If there are no destination regions then the whole frame is the region.
  if (r_idx < blit->n_regions)
    return GST_VIDEO_FRAME_WIDTH (outframe) * GST_VIDEO_FRAME_HEIGHT (outframe);

  // Calculate the destination area filled with frame content.
  region = &(blit->destinations[r_idx]);
  area = region->w * region->h;

  // Iterate destination region for each blit and subtract overlapping area.
  for (num = 0; num <= index; num++) {
    // No destination regions in that blit object, whole frame is the region.
    if (blits[num].n_regions == 0)
      return GST_VIDEO_FRAME_WIDTH (outframe) * GST_VIDEO_FRAME_HEIGHT (outframe);

    // Subtract overlapping are of the destination regions in that blit object.
    for (n = 0; n < blits[num].n_regions; n++) {
      l_region = &(blits[num].destinations[n]);
      area -= gst_fcv_regions_overlapping_area (region, l_region);
    }
  }

  return area;
}

static inline void
gst_fcv_copy_object (GstFcvObject * l_object, GstFcvObject * r_object)
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
gst_fcv_update_object (GstFcvObject * object, const gchar * type,
    const GstVideoFrame * frame, const GstVideoRectangle * region,
    const guint64 flags)
{
  const gchar *mode = NULL;
  gint x = 0, y = 0, width = 0, height = 0;

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  // Take the region values only if they are valid.
  if ((region != NULL) && (region->w != 0) && (region->h != 0) &&
      (width >= (region->x + region->w)) && (height >= (region->y + region->h))) {
    x = region->x;
    y = region->y;
    width = region->w;
    height = region->h;
  }

  switch (flags & GST_VCE_FORMAT_MASK) {
    case GST_VCE_FLAG_F16_FORMAT:
      object->flags = GST_FCV_FLAG_F16;
      mode = "FLOAT16";
      break;
    case GST_VCE_FLAG_F32_FORMAT:
      object->flags = GST_FCV_FLAG_F32;
      mode = "FLOAT32";
      break;
    case GST_VCE_FLAG_I32_FORMAT:
      object->flags = GST_FCV_FLAG_I32;
      mode = "INT32";
      break;
    case GST_VCE_FLAG_U32_FORMAT:
      object->flags = GST_FCV_FLAG_U32;
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
    object->flags |= GST_FCV_FLAG_YUV;
  else if (GST_VIDEO_INFO_IS_RGB (&(frame->info)))
    object->flags |= GST_FCV_FLAG_RGB;
  else if (GST_VIDEO_INFO_IS_GRAY (&(frame->info)))
    object->flags |= GST_FCV_FLAG_GRAY;

  object->flip = gst_fcv_translate_flip_flags (flags);
  object->rotate = gst_fcv_translate_rotation_flags (flags);

  object->format = GST_VIDEO_FRAME_FORMAT (frame);
  object->n_planes = GST_VIDEO_FRAME_N_PLANES (frame);

  // Initialize the mandatory first plane.
  object->planes[0].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  object->planes[0].width = width;
  object->planes[0].height = height;

  // Add the offset to the region of interest to the data pointer.
  object->planes[0].data =
      (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0) +
          (y * object->planes[0].stride) + x);
  object->planes[0].stgid = GST_FCV_INVALID_STAGE_ID;

  // Initialize the secondary plane depending on the format.
  switch (object->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = GST_ROUND_UP_2 (width) / 2;
      object->planes[1].height = GST_ROUND_UP_2 (height) / 2;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              ((GST_ROUND_UP_2 (y) / 2) * object->planes[1].stride) +
                  GST_ROUND_UP_2 (x));
      object->planes[1].stgid = GST_FCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = GST_ROUND_UP_2 (width) / 2;
      object->planes[1].height = height;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              (y * object->planes[1].stride) + GST_ROUND_UP_2 (x));
      object->planes[1].stgid = GST_FCV_INVALID_STAGE_ID;
      break;
    case GST_VIDEO_FORMAT_NV24:
      object->planes[1].stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      object->planes[1].width = width * 2;
      object->planes[1].height = height;
      object->planes[1].data =
          (gpointer) ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 1) +
              (y * object->planes[1].stride) + (x * 2));
      object->planes[1].stgid = GST_FCV_INVALID_STAGE_ID;
      break;
    default:
      // No need for initialize anything in te secondary plane.
      break;
  }

  GST_TRACE ("%s Buffer %p - Object Format: %s%s", type, frame->buffer,
      gst_video_format_to_string (object->format), mode);
  GST_TRACE ("%s Buffer %p - Object Plane 0: %" GST_FCV_PLANE_FORMAT, type,
      frame->buffer, GST_FCV_PLANE_ARGS (&(object->planes[0])));
  GST_TRACE ("%s Buffer %p - Object Plane 1: %" GST_FCV_PLANE_FORMAT, type,
      frame->buffer, GST_FCV_PLANE_ARGS (&(object->planes[1])));

  return;
}

static inline GstFcvStageBuffer *
gst_fcv_video_converter_fetch_stage_buffer (GstFcvVideoConverter * convert,
    guint size)
{
  GstFcvStageBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < convert->stgbufs->len; idx++) {
    buffer = &(g_array_index (convert->stgbufs, GstFcvStageBuffer, idx));

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
  buffer = &(g_array_index (convert->stgbufs, GstFcvStageBuffer, idx));

  buffer->idx = idx;
  buffer->data = g_malloc (size);
  buffer->size = size;
  buffer->used = TRUE;

  GST_TRACE ("Allocated staging buffer at index %u, data %p and size %u",
      buffer->idx, buffer->data, buffer->size);

  return buffer;
}

static inline void
gst_fcv_video_converter_release_stage_buffer (GstFcvVideoConverter * convert,
    guint idx)
{
  GstFcvStageBuffer *buffer = NULL;

  buffer = &(g_array_index (convert->stgbufs, GstFcvStageBuffer, idx));
  buffer->used = FALSE;

  GST_TRACE ("Released staging buffer at index %u, data %p and size %u",
      buffer->idx, buffer->data, buffer->size);
}

static inline gboolean
gst_fcv_video_converter_stage_object_init (GstFcvVideoConverter * convert,
    GstFcvObject * obj, guint width, guint height, GstVideoFormat format)
{
  GstFcvStageBuffer *buffer = NULL;
  guint idx = 0, size = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->n_planes = 1;
      obj->flags = GST_FCV_FLAG_GRAY;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 2;
      obj->n_planes = 1;
      obj->flags = GST_FCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 3;
      obj->n_planes = 1;
      obj->flags = GST_FCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width) * 4;
      obj->n_planes = 1;
      obj->flags = GST_FCV_FLAG_RGB;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].width = GST_ROUND_UP_8 (width) / 2;
      obj->planes[1].height =  GST_ROUND_UP_2 (height) / 2;
      obj->planes[1].stride = GST_ROUND_UP_8 (width);
      obj->n_planes = 2;
      obj->flags = GST_FCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].width = GST_ROUND_UP_8 (width) / 2;
      obj->planes[1].height =  height;
      obj->planes[1].stride = GST_ROUND_UP_8 (width);
      obj->n_planes = 2;
      obj->flags = GST_FCV_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_NV24:
      obj->planes[0].width = GST_ROUND_UP_8 (width);
      obj->planes[0].height = height;
      obj->planes[0].stride = GST_ROUND_UP_8 (width);
      obj->planes[1].width = GST_ROUND_UP_8 (width) * 2;
      obj->planes[1].height =  height;
      obj->planes[1].stride = GST_ROUND_UP_8 (width) * 2;
      obj->n_planes = 2;
      obj->flags = GST_FCV_FLAG_YUV;
      break;
    default:
      GST_ERROR ("Unknown format %s", gst_video_format_to_string (format));
      return FALSE;
  }

  obj->format = format;
  obj->flags |= GST_FCV_FLAG_STAGED;

  obj->flip = 0;
  obj->rotate = 0;

  // Fetch stage buffer for each plane and set the data pointer and index.
  for (idx = 0; idx < obj->n_planes; idx++) {
    size = GST_ROUND_UP_128 (obj->planes[idx].stride * obj->planes[idx].height);
    buffer = gst_fcv_video_converter_fetch_stage_buffer (convert, size);

    obj->planes[idx].data = buffer->data;
    obj->planes[idx].stgid = buffer->idx;

    GST_TRACE ("Stage Object %s Plane %u: %" GST_FCV_PLANE_FORMAT,
        gst_video_format_to_string (obj->format), idx,
        GST_FCV_PLANE_ARGS (&(obj->planes[idx])));
  }

  return TRUE;
}

static inline void
gst_fcv_video_converter_stage_object_deinit (GstFcvVideoConverter * convert,
    GstFcvObject * obj)
{
  guint num = 0, stgid = 0;

  for (num = 0; num < obj->n_planes; num++) {
    stgid = obj->planes[num].stgid;
    gst_fcv_video_converter_release_stage_buffer (convert, stgid);
  }
}

static inline gboolean
gst_fcv_video_converter_stage_plane_init (GstFcvVideoConverter * convert,
    GstFcvPlane * plane, guint32 width, guint32 height, guint32 stride)
{
  GstFcvStageBuffer *buffer = NULL;

  plane->width = width;
  plane->height = height;
  plane->stride = stride;

  buffer = gst_fcv_video_converter_fetch_stage_buffer (convert,
      GST_ROUND_UP_128 (plane->stride * plane->height));
  g_return_val_if_fail (buffer != NULL, FALSE);

  plane->data = buffer->data;
  plane->stgid = buffer->idx;

  GST_LOG ("Stage Plane: %" GST_FCV_PLANE_FORMAT, GST_FCV_PLANE_ARGS (plane));
  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_compute_conversion (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_plane = NULL, *d_plane = NULL;
  guint8 *s_data = NULL, *d_data = NULL;
  guint idx = 0, num = 0, n_bytes = 0;

  g_return_val_if_fail (s_obj->format == d_obj->format, FALSE);
  g_return_val_if_fail (s_obj->n_planes == d_obj->n_planes, FALSE);

  for (idx = 0; idx < d_obj->n_planes; idx++) {
    s_plane = &(s_obj->planes[idx]);
    d_plane = &(d_obj->planes[idx]);

    GST_LOG ("Source Plane %u: %" GST_FCV_PLANE_FORMAT, idx,
        GST_FCV_PLANE_ARGS (s_plane));
    GST_LOG ("Destination Plane %u: %" GST_FCV_PLANE_FORMAT, idx,
        GST_FCV_PLANE_ARGS (d_plane));

    g_return_val_if_fail (s_plane->height == d_plane->height, FALSE);
    g_return_val_if_fail (s_plane->width >= d_plane->width, FALSE);

    switch (d_obj->format) {
      case GST_VIDEO_FORMAT_RGB16:
      case GST_VIDEO_FORMAT_BGR16:
        n_bytes = d_plane->width * 2;
        break;
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_BGR:
        n_bytes = d_plane->width * 3;
        break;
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_RGBx:
      case GST_VIDEO_FORMAT_BGRx:
        n_bytes = d_plane->width * 4;
        break;
      default:
        n_bytes = d_plane->width;
        break;
    }

    for (num = 0; num < d_plane->height; num++) {
      s_data = ((guint8*) s_plane->data) + (num * s_plane->stride);
      d_data = ((guint8*) d_plane->data) + (num * d_plane->stride);

      // TODO This will cut up to 7 pixels of data. Look for better method.
      memcpy (d_data, s_data, n_bytes);
    }
  }

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_fcv_copy_object (d_obj, s_obj);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_yuv_to_yuv (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_luma = NULL, *s_chroma = NULL, *d_luma = NULL, *d_chroma = NULL;
  GstFcvPlane l_chroma = { GST_FCV_INVALID_STAGE_ID, 0, 0, NULL, 0 };

  // Convenient local pointers to the source and destination planes.
  s_luma = &(s_obj->planes[0]);
  s_chroma = &(s_obj->planes[1]);

  d_luma = &(d_obj->planes[0]);
  d_chroma = &(d_obj->planes[1]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_luma));
  GST_LOG ("Source %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_chroma));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_luma));
  GST_LOG ("Destination %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_chroma));

  // Form a unique ID based on the formats for the conversion lookup cases.
  switch (s_obj->format + (d_obj->format << 16)) {
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_NV21 << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_NV12 << 16):
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_NV61 << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_NV16 << 16):
      // Same formats but differ only in the order of the chroma components.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, d_chroma);
      // Chroma components have been swapped, use scale to copy the luma plane.
      GST_FCV_SCALE_LUMA (convert, s_luma, d_luma);
      break;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_NV61 << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_NV16 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_NV16 << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_NV61 << 16):
      GST_FCV_YUV_TO_YUV (convert, 420, 422, s_luma, s_chroma, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_NV24 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_NV24 << 16):
      GST_FCV_YUV_TO_YUV (convert, 420, 444, s_luma, s_chroma, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_NV21 << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_NV12 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_NV12 << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_NV21 << 16):
      GST_FCV_YUV_TO_YUV (convert, 422, 420, s_luma, s_chroma, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_NV24 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_NV24 << 16):
      GST_FCV_YUV_TO_YUV (convert, 422, 444, s_luma, s_chroma, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_NV21 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_NV12 << 16):
      GST_FCV_YUV_TO_YUV (convert, 444, 420, s_luma, s_chroma, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_NV61 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_NV16 << 16):
      GST_FCV_YUV_TO_YUV (convert, 444, 422, s_luma, s_chroma, d_luma, d_chroma);
      break;
    default:
      GST_ERROR ("Unsupported format conversion from '%s' to '%s'!",
          gst_video_format_to_string (s_obj->format),
          gst_video_format_to_string (d_obj->format));
      return FALSE;
  }

  // Free any local storage used from chroma swap.
  if ((l_chroma.data != NULL) && (l_chroma.stgid != GST_FCV_INVALID_STAGE_ID))
      gst_fcv_video_converter_release_stage_buffer (convert, l_chroma.stgid);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_yuv_to_rgb (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_luma = NULL, *s_chroma = NULL, *d_rgb = NULL;
  GstFcvPlane l_chroma = { GST_FCV_INVALID_STAGE_ID, 0, 0, NULL, 0 };

  // Convenient local pointers to the source and destination planes.
  s_luma = &(s_obj->planes[0]);
  s_chroma = &(s_obj->planes[1]);

  d_rgb = &(d_obj->planes[0]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_luma));
  GST_LOG ("Source %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_chroma));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_rgb));

  // Form a unique ID based on the formats for the conversion lookup cases.
  switch (s_obj->format + (d_obj->format << 16)) {
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGR16 << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGB16 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGB16 << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGR16 << 16):
      GST_FCV_YUV_TO_RGB (convert, 420, RGB565, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGR << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGB << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGB << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGR << 16):
      GST_FCV_YUV_TO_RGB (convert, 420, RGB888, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_BGRx << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_RGBx << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_NV12 + (GST_VIDEO_FORMAT_RGBx << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_NV21 + (GST_VIDEO_FORMAT_BGRx << 16):
      GST_FCV_YUV_TO_RGB (convert, 420, RGBA8888, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_BGR16 << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_RGB16 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_RGB16 << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_BGR16 << 16):
      GST_FCV_YUV_TO_RGB (convert, 422, RGB565, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_BGR << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_RGB << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_RGB << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_BGR << 16):
      GST_FCV_YUV_TO_RGB (convert, 422, RGB888, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_BGRx << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_RGBx << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_NV16 + (GST_VIDEO_FORMAT_RGBx << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_NV61 + (GST_VIDEO_FORMAT_BGRx << 16):
      GST_FCV_YUV_TO_RGB (convert, 422, RGBA8888, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_BGR16 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_RGB16 << 16):
      GST_FCV_YUV_TO_RGB (convert, 444, RGB565, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_BGR << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_RGB << 16):
      GST_FCV_YUV_TO_RGB (convert, 444, RGB888, s_luma, s_chroma, d_rgb);
      break;
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_BGRx << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          s_chroma->width, s_chroma->height, s_chroma->stride);

      // Place the swapped chroma components in the temporary local storage.
      GST_FCV_CHROMA_SWAP (convert, s_chroma, (&l_chroma));
      // Set the source chroma plane pointer to the local swapped plane.
      s_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_NV24 + (GST_VIDEO_FORMAT_RGBx << 16):
      GST_FCV_YUV_TO_RGB (convert, 444, RGBA8888, s_luma, s_chroma, d_rgb);
      break;
    default:
      GST_ERROR ("Unsupported format conversion from '%s' to '%s'!",
          gst_video_format_to_string (s_obj->format),
          gst_video_format_to_string (d_obj->format));
      return FALSE;
  }

  // Free any local storage used from chroma swap.
  if ((l_chroma.data != NULL) && (l_chroma.stgid != GST_FCV_INVALID_STAGE_ID))
      gst_fcv_video_converter_release_stage_buffer (convert, l_chroma.stgid);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_rgb_to_yuv (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_rgb = NULL, *d_luma = NULL, *d_chroma = NULL;
  GstFcvPlane l_chroma = { GST_FCV_INVALID_STAGE_ID, 0, 0, NULL, 0 };

  // Convenient local pointers to the source and destination planes.
  s_rgb = &(s_obj->planes[0]);

  d_luma = &(d_obj->planes[0]);
  d_chroma = &(d_obj->planes[1]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_rgb));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_luma));
  GST_LOG ("Destination %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_chroma));

  // Form a unique ID based on the formats for the conversion lookup cases.
  switch (s_obj->format + (d_obj->format << 16)) {
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_NV12 << 16):
    case GST_VIDEO_FORMAT_BGR16 + (GST_VIDEO_FORMAT_NV21 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          d_chroma->width, d_chroma->height, d_chroma->stride);

      // Set the destination chroma plane pointer to the local swapped plane.
      d_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_BGR16 + (GST_VIDEO_FORMAT_NV12 << 16):
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_NV21 << 16):
      GST_FCV_RGB_TO_YUV (convert, RGB565, 420, s_rgb, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_NV16 << 16):
    case GST_VIDEO_FORMAT_BGR16 + (GST_VIDEO_FORMAT_NV61 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          d_chroma->width, d_chroma->height, d_chroma->stride);

      // Set the destination chroma plane pointer to the local swapped plane.
      d_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_BGR16 + (GST_VIDEO_FORMAT_NV16 << 16):
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_NV61 << 16):
      GST_FCV_RGB_TO_YUV (convert, RGB565, 422, s_rgb, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_NV24 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          d_chroma->width, d_chroma->height, d_chroma->stride);

      // Set the destination chroma plane pointer to the local swapped plane.
      d_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_BGR16 + (GST_VIDEO_FORMAT_NV24 << 16):
      GST_FCV_RGB_TO_YUV (convert, RGB565, 444, s_rgb, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV12 << 16):
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV21 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          d_chroma->width, d_chroma->height, d_chroma->stride);

      // Set the destination chroma plane pointer to the local swapped plane.
      d_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV12 << 16):
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV21 << 16):
      GST_FCV_RGB_TO_YUV (convert, RGB888, 420, s_rgb, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV16 << 16):
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV61 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          d_chroma->width, d_chroma->height, d_chroma->stride);

      // Set the destination chroma plane pointer to the local swapped plane.
      d_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV16 << 16):
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV61 << 16):
      GST_FCV_RGB_TO_YUV (convert, RGB888, 422, s_rgb, d_luma, d_chroma);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_NV24 << 16):
      // Fetch temporary local storage for the swapped source chroma plane.
      gst_fcv_video_converter_stage_plane_init (convert, &l_chroma,
          d_chroma->width, d_chroma->height, d_chroma->stride);

      // Set the destination chroma plane pointer to the local swapped plane.
      d_chroma = &l_chroma;

      __attribute__ ((fallthrough));
    case GST_VIDEO_FORMAT_BGR + (GST_VIDEO_FORMAT_NV24 << 16):
      GST_FCV_RGB_TO_YUV (convert, RGB888, 444, s_rgb, d_luma, d_chroma);
      break;
    default:
      GST_ERROR ("Unsupported format conversion from '%s' to '%s'!",
          gst_video_format_to_string (s_obj->format),
          gst_video_format_to_string (d_obj->format));
      return FALSE;
  }

  // If an intermediary was used for the chroma plane, do swap now to destination.
  if ((l_chroma.data != NULL) && (l_chroma.stgid != GST_FCV_INVALID_STAGE_ID)) {
    // Restore the destination chroma plane pointer to the original value.
    d_chroma = &(d_obj->planes[1]);

    // Perform the actual chroma swap from temporary local storage to destination.
    GST_FCV_CHROMA_SWAP (convert, (&l_chroma), d_chroma);

    // Free the intermediary local chroma plane.
    gst_fcv_video_converter_release_stage_buffer (convert, l_chroma.stgid);
  }

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_rgb_to_rgb (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_rgb = NULL, *d_rgb = NULL;

  // Convenient local pointers to the source and destination planes.
  s_rgb = &(s_obj->planes[0]);
  d_rgb = &(d_obj->planes[0]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_rgb));
  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_rgb));

  // Form a unique ID based on the formats for the conversion lookup cases.
  switch (s_obj->format + (d_obj->format << 16)) {
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_BGR16 << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB565, BGR565, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_RGB << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB565, RGB888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_RGBx << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB565, RGBA8888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_BGR << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB565, BGR888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_RGB16 + (GST_VIDEO_FORMAT_BGRx << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB565, BGRA8888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGR << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB888, BGR888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGB16 << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB888, RGB565, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGBA << 16):
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_RGBx << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB888, RGBA8888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGR16 << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB888, BGR565, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_RGB + (GST_VIDEO_FORMAT_BGRx << 16):
      GST_FCV_RGB_TO_RGB (convert, RGB888, BGRA8888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGRx << 16):
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGRA << 16):
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGRx << 16):
      GST_FCV_RGB_TO_RGB (convert, RGBA8888, BGRA8888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_RGB16 << 16):
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_RGB16 << 16):
      GST_FCV_RGB_TO_RGB (convert, RGBA8888, RGB565, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_RGB << 16):
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_RGB << 16):
      GST_FCV_RGB_TO_RGB (convert, RGBA8888, RGB888, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGR16 << 16):
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGR16 << 16):
      GST_FCV_RGB_TO_RGB (convert, RGBA8888, BGR565, s_rgb, d_rgb);
      break;
    case GST_VIDEO_FORMAT_RGBA + (GST_VIDEO_FORMAT_BGR << 16):
    case GST_VIDEO_FORMAT_RGBx + (GST_VIDEO_FORMAT_BGR << 16):
      GST_FCV_RGB_TO_RGB (convert, RGBA8888, BGR888, s_rgb, d_rgb);
      break;
    default:
      GST_ERROR ("Unsupported format conversion from '%s' to '%s'!",
          gst_video_format_to_string (s_obj->format),
          gst_video_format_to_string (d_obj->format));
      return FALSE;
  }

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_color_transform (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvObject l_obj = { 0, };
  gboolean success = FALSE, resize = FALSE, transform = FALSE, aligned = FALSE;
  guint flip = 0, rotate = 0;

  // Cache the flip and rotation flags, will be later reset on the source.
  flip = s_obj->flip;
  rotate = s_obj->rotate;

  resize = (s_obj->planes[0].height != d_obj->planes[0].height) ||
      (s_obj->planes[0].width != d_obj->planes[0].width);
  transform = (s_obj->rotate != 0) || (s_obj->flip != 0);

  // Unaligned output RGB formats require an intermeadiary buffer.
  aligned = ((d_obj->planes[0].width % GST_FCV_WIDTH_ALIGN) == 0) ? TRUE : FALSE;

  // Use stage if resize/flip/rotate or unaligned RGB is pending.
  if (((d_obj->flags & GST_FCV_FLAG_RGB) && !aligned) || resize || transform) {
    GstVideoFormat format = d_obj->format;
    guint width = s_obj->planes[0].width;
    guint height = s_obj->planes[0].height;

    // Override format if resize/flip/rotate are pending and destination is RGB.
    if ((d_obj->flags & GST_FCV_FLAG_RGB) && ((aligned && resize) || transform))
      format = GST_VIDEO_FORMAT_NV12;

    // Temporary store the destination object data into local intermediary.
    gst_fcv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_fcv_video_converter_stage_object_init (convert, d_obj,
        width, height, format);
    g_return_val_if_fail (success, FALSE);
  }

  if ((s_obj->flags & GST_FCV_FLAG_YUV) && (d_obj->flags & GST_FCV_FLAG_YUV))
    success = gst_fcv_video_converter_yuv_to_yuv (convert, s_obj, d_obj);
  else if ((s_obj->flags & GST_FCV_FLAG_YUV) && (d_obj->flags & GST_FCV_FLAG_RGB))
    success = gst_fcv_video_converter_yuv_to_rgb (convert, s_obj, d_obj);
  else if ((s_obj->flags & GST_FCV_FLAG_RGB) && (d_obj->flags & GST_FCV_FLAG_YUV))
    success = gst_fcv_video_converter_rgb_to_yuv (convert, s_obj, d_obj);
  else if ((s_obj->flags & GST_FCV_FLAG_RGB) && (d_obj->flags & GST_FCV_FLAG_RGB))
    success = gst_fcv_video_converter_rgb_to_rgb (convert, s_obj, d_obj);
  else
    GST_ERROR ("Unsupported color conversion families!");

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_fcv_copy_object (d_obj, s_obj);

  // Transfer any pending flip and/or rotate operation on the source object.
  s_obj->flip = flip;
  s_obj->rotate = rotate;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_copy_object (&l_obj, d_obj);

  return success;
}

static inline gboolean
gst_fcv_video_converter_downscale (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_luma = NULL, *s_chroma = NULL;
  GstFcvPlane *d_luma = NULL, *d_chroma = NULL;
  GstFcvObject l_obj = { 0, };
  guint flip = 0, rotate = 0;
  gboolean rotation = FALSE;

  g_return_val_if_fail (!(s_obj->flags & GST_FCV_FLAG_RGB), FALSE);

  // Cache the flip and rotation flags, will be later reset on the source.
  flip = s_obj->flip;
  rotate = s_obj->rotate;

  rotation = (rotate == FASTCV_ROTATE_90) || (rotate == FASTCV_ROTATE_270);

  // Use stage object if format or stride differs, or 90/270 rotation is pending.
  if ((s_obj->format != d_obj->format) || rotation) {
    guint width = 0, height = 0;
    gboolean success = FALSE;

    // Dimensions are swapped if 90/270 degree rotation is pending.
    width = rotation ? d_obj->planes[0].height : d_obj->planes[0].width;
    height = rotation ? d_obj->planes[0].width : d_obj->planes[0].height;

    // Temporary store the destination object data into local intermediary.
    gst_fcv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_fcv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  // Convenient local pointers to the source and destination planes.
  s_luma = &(s_obj->planes[0]);
  s_chroma = &(s_obj->planes[1]);

  d_luma = &(d_obj->planes[0]);
  d_chroma = &(d_obj->planes[1]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_luma));
  GST_LOG ("Source %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_chroma));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_luma));
  GST_LOG ("Destination %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_chroma));

  GST_FCV_SCALE_LUMA (convert, s_luma, d_luma);

  if ((s_obj->flags & GST_FCV_FLAG_YUV) && (d_obj->flags & GST_FCV_FLAG_YUV))
    GST_FCV_SCALE_DOWN_CHROMA (convert, s_chroma, d_chroma);

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_fcv_copy_object (d_obj, s_obj);

  // Transfer any pending flip and/or rotate operation on the source object.
  s_obj->flip = flip;
  s_obj->rotate = rotate;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_upscale (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_luma = NULL, *s_chroma = NULL;
  GstFcvPlane *d_luma = NULL, *d_chroma = NULL;
  GstFcvObject l_obj = { 0, };
  guint flip = 0, rotate = 0;
  gboolean rotation = FALSE;

  g_return_val_if_fail (!(s_obj->flags & GST_FCV_FLAG_RGB), FALSE);

  // Cache the flip and rotation flags, will be later reset on the source.
  flip = s_obj->flip;
  rotate = s_obj->rotate;

  rotation = (rotate == FASTCV_ROTATE_90) || (rotate == FASTCV_ROTATE_270);

  // Use stage object if format or stride differs, or 90/270 rotation is pending.
  if ((s_obj->format != d_obj->format) || rotation) {
    guint width = 0, height = 0;
    gboolean success = FALSE;

    // Dimensions are swapped if 90/270 degree rotation is pending.
    width = rotation ? d_obj->planes[0].height : d_obj->planes[0].width;
    height = rotation ? d_obj->planes[0].width : d_obj->planes[0].height;

    // Temporary store the destination object data into local intermediary.
    gst_fcv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_fcv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  // Convenient local pointers to the source and destination planes.
  s_luma = &(s_obj->planes[0]);
  s_chroma = &(s_obj->planes[1]);

  d_luma = &(d_obj->planes[0]);
  d_chroma = &(d_obj->planes[1]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_luma));
  GST_LOG ("Source %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_chroma));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_luma));
  GST_LOG ("Destination %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_chroma));

  GST_FCV_SCALE_LUMA (convert, s_luma, d_luma);

  if ((s_obj->flags & GST_FCV_FLAG_YUV) && (d_obj->flags & GST_FCV_FLAG_YUV))
    GST_FCV_SCALE_UP_CHROMA (convert, s_chroma, d_chroma);

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_fcv_copy_object (d_obj, s_obj);

  // Transfer any pending flip and/or rotate operation on the source object.
  s_obj->flip = flip;
  s_obj->rotate = rotate;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_rotate (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_luma = NULL, *s_chroma = NULL;
  GstFcvPlane *d_luma = NULL, *d_chroma = NULL;
  GstFcvObject l_obj = { 0, };
  guint flip = 0, rotate = 0;
  gboolean resize = FALSE;

  g_return_val_if_fail (!(s_obj->flags & GST_FCV_FLAG_RGB), FALSE);

  // Cache the flip and rotation flags, will be later reset on the source.
  flip = s_obj->flip;
  rotate = s_obj->rotate;

  // Raise the resize flag if source and detination resolution are different.
  if ((rotate == FASTCV_ROTATE_90) || (rotate == FASTCV_ROTATE_270)) {
    resize = ((s_obj->planes[0].width != d_obj->planes[0].height) ||
        (s_obj->planes[0].height != d_obj->planes[0].width)) ? TRUE : FALSE;
  } else {
    resize = ((s_obj->planes[0].width != d_obj->planes[0].width) ||
        (s_obj->planes[0].height != d_obj->planes[0].height)) ? TRUE : FALSE;
  }

  // Use stage object if format or stride differs or resize is pending.
  if ((s_obj->format != d_obj->format) || resize) {
    guint width = 0, height = 0;
    gboolean success = FALSE;

    width = s_obj->planes[0].width;
    height = s_obj->planes[0].height;

    // Dimensions are swapped if 90/270 degree rotation is required with resize.
    if (resize && (rotate == FASTCV_ROTATE_90 || rotate == FASTCV_ROTATE_270)) {
      width = s_obj->planes[0].height;
      height = s_obj->planes[0].width;
    }

    // Temporary store the destination object data into local intermediary.
    gst_fcv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_fcv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  // Convenient local pointers to the source and destination planes.
  s_luma = &(s_obj->planes[0]);
  s_chroma = &(s_obj->planes[1]);

  d_luma = &(d_obj->planes[0]);
  d_chroma = &(d_obj->planes[1]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_luma));
  GST_LOG ("Source %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_chroma));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_luma));
  GST_LOG ("Destination %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_chroma));

  GST_FCV_ROTATE_LUMA (convert, s_luma, d_luma, rotate);

  if ((s_obj->flags & GST_FCV_FLAG_YUV) && (d_obj->flags & GST_FCV_FLAG_YUV))
    GST_FCV_ROTATE_CHROMA (convert, s_chroma, d_chroma, rotate);

  // If source is a stage object from previous operation, release stage buffers.
  if (s_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_fcv_copy_object (d_obj, s_obj);

  // Transfer any pending flip and reset rotate operation on the source object.
  s_obj->flip = flip;
  s_obj->rotate = 0;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_flip (GstFcvVideoConverter * convert,
    GstFcvObject * s_obj, GstFcvObject * d_obj)
{
  GstFcvPlane *s_luma = NULL, *s_chroma = NULL;
  GstFcvPlane *d_luma = NULL, *d_chroma = NULL;
  GstFcvObject l_obj = { 0, };
  guint flip = 0, rotate = 0;
  gboolean resize = FALSE;

  g_return_val_if_fail (!(s_obj->flags & GST_FCV_FLAG_RGB), FALSE);

  // Cache the flip and rotation flags, will be later reset on the source.
  flip = s_obj->flip;
  rotate = s_obj->rotate;

  resize = (s_obj->planes[0].height != d_obj->planes[0].height) ||
      (s_obj->planes[0].width != d_obj->planes[0].width);

  // If source is a stage object and upscale is pending, do in-place flip.
  if (resize && (s_obj->flags & GST_FCV_FLAG_STAGED)) {
    // Source is a stage object and resize is pending, do in-place flip.
    gst_fcv_copy_object (s_obj, d_obj);
  } else if ((s_obj->format != d_obj->format) || resize) {
    // Use stage object as format or stride differs or resize is pending.
    guint width = 0, height = 0;
    gboolean success = FALSE;

    // Dimensions are swapped if 90/270 degree rotation is required with resize.
    if (resize && (rotate == FASTCV_ROTATE_90 || rotate == FASTCV_ROTATE_270)) {
      width = s_obj->planes[0].height;
      height = s_obj->planes[0].width;
    } else {
      width = s_obj->planes[0].width;
      height = s_obj->planes[0].height;
    }

    // Temporary store the destination object data into local intermediary.
    gst_fcv_copy_object (d_obj, &l_obj);

    // Override destination object with stage object data, revert it later.
    success = gst_fcv_video_converter_stage_object_init (convert, d_obj,
        width, height, s_obj->format);
    g_return_val_if_fail (success, FALSE);
  }

  // Convenient local pointers to the source and destination planes.
  s_luma = &(s_obj->planes[0]);
  s_chroma = &(s_obj->planes[1]);

  d_luma = &(d_obj->planes[0]);
  d_chroma = &(d_obj->planes[1]);

  GST_LOG ("Source %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_luma));
  GST_LOG ("Source %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (s_obj->format), GST_FCV_PLANE_ARGS (s_chroma));

  GST_LOG ("Destination %s Plane 0: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_luma));
  GST_LOG ("Destination %s Plane 1: %" GST_FCV_PLANE_FORMAT,
      gst_video_format_to_string (d_obj->format), GST_FCV_PLANE_ARGS (d_chroma));

  GST_FCV_FLIP_LUMA (convert, s_luma, d_luma, flip);

  if ((s_obj->flags & GST_FCV_FLAG_YUV) && (d_obj->flags & GST_FCV_FLAG_YUV))
    GST_FCV_FLIP_CHROMA (convert, s_chroma, d_chroma, flip);

  // Not in-place and source is a stage object from previous operation, release it.
  if ((d_obj != s_obj) && (s_obj->flags & GST_FCV_FLAG_STAGED))
    gst_fcv_video_converter_stage_object_deinit (convert, s_obj);

  // Set the destination/stage object as source for the next operation.
  gst_fcv_copy_object (d_obj, s_obj);

  // Transfer any pending rotate and reset flip operation on the source object.
  s_obj->flip = 0;
  s_obj->rotate = rotate;

  // Restore the original destination object in case a stage was used.
  if (d_obj->flags & GST_FCV_FLAG_STAGED)
    gst_fcv_copy_object (&l_obj, d_obj);

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_fill_background (GstFcvVideoConverter * convert,
    GstVideoFrame * frame, guint32 color)
{
  guint8 red = 0x00, green = 0x00, blue = 0x00, alpha = 0x00;
  guint8 luma = 0x00, cb = 0x00, cr = 0x00;

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
      GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
      gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_NV12:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), luma, luma, luma, luma,
          NULL, 0);
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_WIDTH (frame) / 4,
          GST_ROUND_UP_2 (GST_VIDEO_FRAME_HEIGHT (frame)) / 2,
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1), cb, cr, cb, cr, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_NV21:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), luma, luma, luma, luma,
          NULL, 0);
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_WIDTH (frame) / 4,
          GST_ROUND_UP_2 (GST_VIDEO_FRAME_HEIGHT (frame)) / 2,
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1), cr, cb, cr, cb, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_NV16:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), luma, luma, luma, luma,
          NULL, 0);
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1), cb, cr, cb, cr, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_NV61:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), luma, luma, luma, luma,
          NULL, 0);
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1), cr, cb, cr, cb, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_NV24:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame) / 4, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), luma, luma, luma, luma,
          NULL, 0);
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 1),
          GST_VIDEO_FRAME_WIDTH (frame) / 2, GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1), cb, cr, cb, cr, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_RGB:
      convert->SetElementsc3u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), red, green, blue, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_BGR:
      convert->SetElementsc3u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), blue,
          green, red, NULL, 0);
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), red, green, blue, alpha,
          NULL, 0);
      break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      convert->SetElementsc4u8 (GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
          GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0), blue, green, red, alpha,
          NULL, 0);
      break;
    default:
      GST_ERROR ("Unsupported format %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
      return FALSE;
  }

  return TRUE;
}

static inline gboolean
gst_fcv_video_converter_process (GstFcvVideoConverter * convert,
    GstFcvObject * objects, guint n_objects)
{
  GstFcvObject *s_obj = NULL, *d_obj = NULL;
  guint idx = 0;
  gfloat w_scale = 0.0, h_scale = 0.0, scale = 0.0, flip = 0, rotate = 0;
  gboolean downscale = FALSE, upscale = FALSE, aligned = FALSE;
  gboolean normalize = FALSE;

  for (idx = 0; idx < n_objects; idx += 2) {
    s_obj = &(objects[idx]);
    d_obj = &(objects[idx + 1]);

    flip = s_obj->flip;
    rotate = s_obj->rotate;

    // Calculte the width and height scale ratios.
    if (rotate == 0 || rotate == FASTCV_ROTATE_180) {
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
        (rotate == 0) && (flip == 0) && (s_obj->format == d_obj->format));

    // Use upscale if output is bigger or same scale but reversed dimensions.
    upscale = (scale > 1.0) ||
        (scale == 1.0 && w_scale != 1.0 && h_scale != 1.0 && rotate == 0);

    // Unaligned output RGB formats require additional processing at the end.
    aligned = ((d_obj->planes[0].width % GST_FCV_WIDTH_ALIGN) == 0) ?
        TRUE : FALSE;

    // None 8-bit unsigned integer RGB formats require normalization.
    normalize = (d_obj->flags & GST_FCV_FLAG_F16) ||
        (d_obj->flags & GST_FCV_FLAG_F32) || (d_obj->flags & GST_FCV_FLAG_I32) ||
        (d_obj->flags & GST_FCV_FLAG_U32);

    // First, check if we need to do color conversion to YUV on the source.
    // Upcscale/Downscale/Rotate/Flip require non-RGB input and output.
    if ((downscale || upscale || (rotate != 0) || (flip != 0)) &&
        (s_obj->flags & GST_FCV_FLAG_RGB) &&
        !gst_fcv_video_converter_color_transform (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to convert RGB input into YUV before other conversions!");
      return FALSE;
    }

    // Second, do downscale if required so that next operations are less costly.
    if (downscale && !gst_fcv_video_converter_downscale (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to downscale image!");
      return FALSE;
    }

    // Third, perform image rotate if necessary.
    if ((rotate != 0) && !gst_fcv_video_converter_rotate (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to rotate image!");
      return FALSE;
    }

    // Fourth, perform image flip if necessary.
    if ((flip != 0) && !gst_fcv_video_converter_flip (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to flip image!");
      return FALSE;
    }

    // Fifth, if output is upscaled RGB, upscale before color conversion.
    if (upscale && (d_obj->flags & GST_FCV_FLAG_RGB) &&
        !gst_fcv_video_converter_upscale (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to upscale image before RGB conversion!");
      return FALSE;
    }

    // Sixth, perform final color conversion if necessary.
    if ((s_obj->format != d_obj->format) &&
        !gst_fcv_video_converter_color_transform (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to convert image format!");
      return FALSE;
    }

    // Seventh, perform image upscale for GRAY/YUV output images if necessary.
    if (upscale && !(d_obj->flags & GST_FCV_FLAG_RGB) &&
        !gst_fcv_video_converter_upscale (convert, s_obj, d_obj)) {
      GST_ERROR ("Failed to upscale image!");
      return FALSE;
    }

    // Lastly, perform unaligned conversion or normalization if necessary.
    if ((d_obj->flags & GST_FCV_FLAG_RGB || d_obj->flags & GST_FCV_FLAG_GRAY) &&
        (!aligned || normalize))
      gst_fcv_video_converter_compute_conversion (convert, s_obj, d_obj);
  }

  return TRUE;
}

gboolean
gst_fcv_video_converter_compose (GstFcvVideoConverter * convert,
    GstVideoComposition * compositions, guint n_compositions, gpointer * fence)
{
  GstFcvObject objects[GST_FCV_MAX_DRAW_OBJECTS] = { 0, };
  guint32 idx = 0, num = 0, n_objects = 0, area = 0;

  // TODO: Implement async operations via threads.
  if (fence != NULL)
    GST_WARNING ("Asynchronous composition operations are not supported!");

  for (idx = 0; idx < n_compositions; idx++) {
    GstVideoFrame *outframe = compositions[idx].frame;
    GstVideoBlit *blits = compositions[idx].blits;
    guint n_blits = compositions[idx].n_blits;

    // Sanity checks, output frame and blit entries must not be NULL.
    g_return_val_if_fail (outframe != NULL, FALSE);
    g_return_val_if_fail ((blits != NULL) && (n_blits != 0), FALSE);

    // Total area of the output frame that is to be used in later calculations
    // to determine whether there are unoccupied background pixels to be filled.
    area = GST_VIDEO_FRAME_WIDTH (outframe) * GST_VIDEO_FRAME_HEIGHT (outframe);

    // Iterate over the input blit entries and update each FCV object.
    for (num = 0; num < n_blits; num++) {
      GstVideoBlit *blit = &(blits[num]);
      guint r_idx = 0;

      // Update a new FCV object (at least 1) for each source/destnation pair.
      do {
        GstFcvObject *object = NULL;
        GstVideoRectangle *region = NULL;

        if (n_objects >= GST_FCV_MAX_DRAW_OBJECTS) {
          GST_ERROR ("Number of objects exceeds %d!", GST_FCV_MAX_DRAW_OBJECTS);
          return FALSE;
        }

        // Intialization of the source FCV object.
        object = &(objects[n_objects]);
        region = (r_idx < blit->n_regions) ? &(blit->sources[r_idx]) : NULL;

        gst_fcv_update_object (object, "Source", blit->frame, region, blit->flags);

        // Intialization of the destination FCV object.
        object = &(objects[n_objects + 1]);
        region = (r_idx < blit->n_regions) ? &(blit->destinations[r_idx]) : NULL;

        gst_fcv_update_object (object, "Destination", outframe, region,
            compositions[idx].flags);

        // Subtract blit area from total area.
        if (area != 0)
          area -= gst_fcv_composition_blit_area (outframe, blits, num, r_idx);

        // Increment the objects counter by 2 for for Source/Destination pair.
        n_objects += 2;
      } while (++r_idx < blit->n_regions);
    }

    if ((compositions[idx].flags & GST_VCE_FLAG_FILL_BACKGROUND) && (area > 0)) {
      guint32 color = compositions[idx].bgcolor;
      gst_fcv_video_converter_fill_background (convert, outframe, color);
    }

    if (!gst_fcv_video_converter_process (convert, objects, n_objects)) {
      GST_ERROR ("Failed to process frames for composition %u!", idx);
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_fcv_video_converter_wait_fence (GstFcvVideoConverter * convert,
    gpointer fence)
{
  GST_WARNING ("Not implemented!");

  return TRUE;
}

void
gst_fcv_video_converter_flush (GstFcvVideoConverter * convert)
{
  GST_WARNING ("Not implemented!");
}

GstFcvVideoConverter *
gst_fcv_video_converter_new (GstStructure * settings)
{
  GstFcvVideoConverter *convert = NULL;
  gboolean success = TRUE;
  gint opmode = FASTCV_OP_PERFORMANCE;

  convert = g_slice_new0 (GstFcvVideoConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  if ((convert->fcvhandle = dlopen ("libfastcvopt.so", RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open FastCV library, error: %s!", dlerror());
    goto cleanup;
  }

  // Load FastCV library symbols.
  success &= LOAD_FCV_SYMBOL (convert, SetOperationMode);
  success &= LOAD_FCV_SYMBOL (convert, CleanUp);

  success &= LOAD_FCV_SYMBOL (convert, SetElementsc3u8);
  success &= LOAD_FCV_SYMBOL (convert, SetElementsc4u8);

  success &= LOAD_FCV_SYMBOL (convert, Flipu8);
  success &= LOAD_FCV_SYMBOL (convert, Flipu16);
  success &= LOAD_FCV_SYMBOL (convert, RotateImageu8);
  success &= LOAD_FCV_SYMBOL (convert, RotateImageInterleavedu8);

  success &= LOAD_FCV_SYMBOL (convert, Scaleu8_v2);
  success &= LOAD_FCV_SYMBOL (convert, ScaleUpPolyInterleaveu8);
  success &= LOAD_FCV_SYMBOL (convert, ScaleDownMNInterleaveu8);

  success &= LOAD_FCV_SYMBOL (convert, ColorCbCrSwapu8);

  success &= LOAD_FCV_SYMBOL (convert,
      ColorYCbCr420PseudoPlanarToYCbCr444PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert,
      ColorYCbCr420PseudoPlanarToYCbCr422PseudoPlanaru8);

  success &= LOAD_FCV_SYMBOL (convert,
      ColorYCbCr422PseudoPlanarToYCbCr444PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert,
      ColorYCbCr422PseudoPlanarToYCbCr420PseudoPlanaru8);

  success &= LOAD_FCV_SYMBOL (convert,
      ColorYCbCr444PseudoPlanarToYCbCr422PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert,
      ColorYCbCr444PseudoPlanarToYCbCr420PseudoPlanaru8);

  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr420PseudoPlanarToRGB565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr420PseudoPlanarToRGB888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr420PseudoPlanarToRGBA8888u8);

  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr422PseudoPlanarToRGB565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr422PseudoPlanarToRGB888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr422PseudoPlanarToRGBA8888u8);

  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr444PseudoPlanarToRGB565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr444PseudoPlanarToRGB888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorYCbCr444PseudoPlanarToRGBA8888u8);

  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToYCbCr444PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToYCbCr422PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToYCbCr420PseudoPlanaru8);

  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToYCbCr444PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToYCbCr422PseudoPlanaru8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToYCbCr420PseudoPlanaru8);

  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToBGR565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToRGB888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToRGBA8888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToBGR888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB565ToBGRA8888u8);

  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToBGR888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToRGB565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToRGBA8888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToBGR565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGB888ToBGRA8888u8);

  success &= LOAD_FCV_SYMBOL (convert, ColorRGBA8888ToBGRA8888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGBA8888ToRGB565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGBA8888ToRGB888u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGBA8888ToBGR565u8);
  success &= LOAD_FCV_SYMBOL (convert, ColorRGBA8888ToBGR888u8);

  // Check whether symbol loading was successful.
  if (!success)
    goto cleanup;

  convert->stgbufs = g_array_new (FALSE, TRUE, sizeof (GstFcvStageBuffer));
  if (convert->stgbufs == NULL) {
    GST_ERROR ("Failed to create array for the staging buffers!");
    goto cleanup;
  }

  // Set clearing function for the allocated stage memory.
  g_array_set_clear_func (convert->stgbufs, gst_fcv_stage_buffer_free);

  switch (gst_fcv_get_opmode (settings)) {
    case GST_FCV_OP_MODE_LOW_POWER:
      opmode = FASTCV_OP_LOW_POWER;
      GST_INFO ("Operation mode: LOW_POWER");
      break;
    case GST_FCV_OP_MODE_PERFORMANCE:
      opmode = FASTCV_OP_PERFORMANCE;
      GST_INFO ("Operation mode: PERFORMANCE");
      break;
    case GST_FCV_OP_MODE_CPU_OFFLOAD:
      opmode = FASTCV_OP_CPU_OFFLOAD;
      GST_INFO ("Operation mode: CPU_OFFLOAD");
      break;
    case GST_FCV_OP_MODE_CPU_PERFORMANCE:
      opmode = FASTCV_OP_CPU_PERFORMANCE;
      GST_INFO ("Operation mode: CPU_PERFORMANCE");
      break;
    default:
      GST_WARNING ("Unknown mode set, defaulting to PERFORMANCE");
      break;
  }

  if (convert->SetOperationMode (opmode) != 0) {
    GST_ERROR ("Failed to set operational mode!");
    goto cleanup;
  }

  GST_INFO ("Created FastCV Converter %p", convert);
  return convert;

cleanup:
  gst_fcv_video_converter_free (convert);
  return NULL;
}

void
gst_fcv_video_converter_free (GstFcvVideoConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->stgbufs != NULL)
    g_array_free (convert->stgbufs, TRUE);

  if (convert->CleanUp != NULL)
    convert->CleanUp ();

  if (convert->fcvhandle != NULL)
    dlclose (convert->fcvhandle);

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed FastCV converter: %p", convert);
  g_slice_free (GstFcvVideoConverter, convert);
}
