/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlvconverter.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/gfx/gfx-utils.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_ml_video_converter_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_converter_debug);

#define gst_ml_video_converter_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoConverter, gst_ml_video_converter,
    GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MIN_BUFFERS     2
#define DEFAULT_PROP_MAX_BUFFERS     24

#define DEFAULT_PROP_CONVERSION_MODE   GST_ML_CONVERSION_MODE_IMAGE_NON_CUMULATIVE
#define DEFAULT_PROP_ENGINE_BACKEND    (gst_video_converter_default_backend())
#define DEFAULT_PROP_IMAGE_DISPOSITION GST_ML_VIDEO_DISPOSITION_TOP_LEFT
#define DEFAULT_PROP_SUBPIXEL_LAYOUT   GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR
#define DEFAULT_PROP_MEAN              0.0
#define DEFAULT_PROP_SIGMA             1.0

// 1.0 / (2^8 - 1)
#define FLOAT_CONVERSION_SIGMA         (1.0 / 255.0)
// 2^8 / 2
#define INT8_CONVERSION_OFFSET         128.0
// 2^16 / 2
#define INT16_CONVERSION_OFFSET        32768.0
// (2^16 - 1) / (2^8 - 1)
#define UINT16_CONVERSION_SIGMA        257.0
// 2^32 / 2
#define INT32_CONVERSION_OFFSET        2147483648.0
// (2^32 - 1) / (2^8 - 1)
#define UINT32_CONVERSION_SIGMA        16843009.0
// 2^64 / 2
#define INT64_CONVERSION_OFFSET        9223372036854775808.0
// (2^64 - 1) / (2^8 - 1)
#define UINT64_CONVERSION_SIGMA        72340172838076673.0

#define MATRIX_MAX_SIZE                8

#define GET_MEAN_VALUE(mean, idx) (mean->len > idx) ? \
    g_array_index (mean, gdouble, idx) : DEFAULT_PROP_MEAN
#define GET_SIGMA_VALUE(sigma, idx) (sigma->len > idx) ? \
    g_array_index (sigma, gdouble, idx) : DEFAULT_PROP_SIGMA

#define GST_CONVERSION_MODE_IS_NON_CUMULATIVE(mode) \
  (mode == GST_ML_CONVERSION_MODE_IMAGE_NON_CUMULATIVE || \
      mode == GST_ML_CONVERSION_MODE_ROI_NON_CUMULATIVE)
#define GST_CONVERSION_MODE_IS_CUMULATIVE(mode) \
  (mode == GST_ML_CONVERSION_MODE_IMAGE_CUMULATIVE || \
      mode == GST_ML_CONVERSION_MODE_ROI_CUMULATIVE)
#define GST_CONVERSION_MODE_IS_IMAGE(mode) \
  (mode == GST_ML_CONVERSION_MODE_IMAGE_NON_CUMULATIVE || \
      mode == GST_ML_CONVERSION_MODE_IMAGE_CUMULATIVE)
#define GST_CONVERSION_MODE_IS_ROI(mode) \
  (mode == GST_ML_CONVERSION_MODE_ROI_NON_CUMULATIVE || \
      mode == GST_ML_CONVERSION_MODE_ROI_CUMULATIVE)


#define GST_ML_VIDEO_FORMATS \
    "{ RGBA, BGRA, ABGR, ARGB, RGBx, BGRx, xRGB, xBGR, BGR, RGB, GRAY8, NV12, NV21, YUY2, UYVY, NV12_Q08C }"

#define GST_ML_TENSOR_TYPES "{ INT8, UINT8, INT16, UINT16, INT32, UINT32, INT64, UINT64, FLOAT16, FLOAT32 }"

#define GST_ML_VIDEO_CONVERTER_SRC_CAPS    \
    "neural-network/tensors, "             \
    "type = (string) " GST_ML_TENSOR_TYPES

#define GST_ML_TENSOR_LAYOUT_NHWC \
    (GstTensorLayout){ .n = 0, .d = -1, .h = 1, .w = 2, .c = 3 }
#define GST_ML_TENSOR_LAYOUT_NCHW \
    (GstTensorLayout){ .n = 0, .d = -1, .h = 2, .w = 3, .c = 1 }
#define GST_ML_TENSOR_LAYOUT_NDHWC \
    (GstTensorLayout){ .n = 0, .d = 1, .h = 2, .w = 3, .c = 4 }

#define GST_ML_INFO_TENSOR_DIM_N(tensorlayout, mlinfo) \
    GST_ML_INFO_TENSOR_DIM(mlinfo, 0, tensorlayout.n)
#define GST_ML_INFO_TENSOR_DIM_D(tensorlayout, mlinfo) \
    ((tensorlayout.d == -1) ? 1 : \
        GST_ML_INFO_TENSOR_DIM(mlinfo, 0, tensorlayout.d))
#define GST_ML_INFO_TENSOR_DIM_H(tensorlayout, mlinfo) \
    GST_ML_INFO_TENSOR_DIM(mlinfo, 0, tensorlayout.h)
#define GST_ML_INFO_TENSOR_DIM_W(tensorlayout, mlinfo) \
    GST_ML_INFO_TENSOR_DIM(mlinfo, 0, tensorlayout.w)
#define GST_ML_INFO_TENSOR_DIM_C(tensorlayout, mlinfo) \
    GST_ML_INFO_TENSOR_DIM(mlinfo, 0, tensorlayout.c)

enum
{
  PROP_0,
  PROP_CONVERSION_MODE,
  PROP_ENGINE_BACKEND,
  PROP_IMAGE_DISPOSITION,
  PROP_SUBPIXEL_LAYOUT,
  PROP_MEAN,
  PROP_SIGMA,
};

static GstStaticCaps gst_ml_video_converter_static_src_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CONVERTER_SRC_CAPS);


static GstCaps *
gst_ml_video_converter_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_ML_VIDEO_FORMATS));

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_ML_VIDEO_FORMATS));

      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_converter_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_converter_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_converter_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_converter_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_converter_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_converter_src_caps ());
}

GType
gst_ml_conversion_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_CONVERSION_MODE_IMAGE_NON_CUMULATIVE,
        "ROI meta is ignored. Immediatelly process incoming buffers irrelevant "
        "of whether there are enough image memory blocks to fill the requested "
        "tensor batch size.",
        "image-batch-non-cumulative"
    },
    { GST_ML_CONVERSION_MODE_IMAGE_CUMULATIVE,
        "ROI meta is ignored. Accumulate buffers until there are enough image "
        "memory blocks to fill the requested tensor batch size. Accumulation "
        "is interrupted early if a GAP buffer is received.",
        "image-batch-cumulative"
    },
    { GST_ML_CONVERSION_MODE_ROI_NON_CUMULATIVE,
        "Use only ROI metas to fill tensor batch size. Immediatelly process "
        "incoming buffers irrelevant of whether there are enough ROI metas to "
        "fill the requested tensor batch size. In case no ROI meta is present "
        "a GAP buffer will be produced.",
        "roi-batch-non-cumulative"
    },
    { GST_ML_CONVERSION_MODE_ROI_CUMULATIVE,
        "Use only ROI metas to fill tensor batch size. Accumulate buffers until "
        "there are enough ROI metas to fill the requested tensor batch size. "
        "Accumulation is interrupted early if a GAP buffer is received or if "
        "there are no ROI metas present inside the received buffer.",
        "roi-batch-cumulative"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLVideoConversionMode", variants);

  return gtype;
}

GType
gst_ml_video_disposition_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_ML_VIDEO_DISPOSITION_TOP_LEFT,
        "Preserve the source image AR (Aspect Ratio) during scaledown and place "
        "it in the top-left corner of the output tensor", "top-left"
    },
    { GST_ML_VIDEO_DISPOSITION_CENTRE,
        "Preserve the source image AR (Aspect Ratio) during scaledown and place "
        "it in the centre of the output tensor", "centre"
    },
    { GST_ML_VIDEO_DISPOSITION_STRETCH,
        "Ignore the source image AR (Aspect Ratio) and if required stretch it's "
        "AR in order to fit completely inside the output tensor", "stretch"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLVideoDisposition", variants);

  return gtype;
}

GType
gst_ml_video_pixel_layout_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR,
        "Regular subpixel layout e.g. RGB, RGBA, RGBx, etc.", "regular"
    },
    { GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE,
        "Reverse subpixel layout e.g. BGR, BGRA, BGRx, etc.", "reverse"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLVideoPixelLayout", variants);

  return gtype;
}

static inline gboolean
is_conversion_required (GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  gboolean conversion = FALSE;

  // Conversion is required if input and output formats are different.
  conversion |=  GST_VIDEO_FRAME_FORMAT (inframe) !=
      GST_VIDEO_FRAME_FORMAT (outframe);
  // Conversion is required if input and output strides are different.
  conversion |= GST_VIDEO_FRAME_PLANE_STRIDE (inframe, 0) !=
      GST_VIDEO_FRAME_PLANE_STRIDE (outframe, 0);
  // Conversion is required if input and output heights are different.
  conversion |= GST_VIDEO_FRAME_HEIGHT (inframe) !=
      GST_VIDEO_FRAME_HEIGHT (outframe);

  return conversion;
}

static inline void
init_formats (GValue * formats, ...)
{
  GValue format = G_VALUE_INIT;
  gchar *string = NULL;
  va_list args;

  g_value_init (formats, GST_TYPE_LIST);
  va_start (args, formats);

  while ((string = va_arg (args, gchar *))) {
    g_value_init (&format, G_TYPE_STRING);
    g_value_set_string (&format, string);

    gst_value_list_append_value (formats, &format);
    g_value_unset (&format);
  }

  va_end (args);
}

static GstTensorLayout
gst_ml_info_get_layout (GstMLInfo *mlinfo)
{
  if (GST_ML_INFO_N_DIMENSIONS (mlinfo, 0) == 5) {
    return GST_ML_TENSOR_LAYOUT_NDHWC;
  } else if ((GST_ML_INFO_TENSOR_DIM (mlinfo, 0, 3) > 4) &&
      (GST_ML_INFO_TENSOR_DIM (mlinfo, 0, 1) <= 4)) {
    return GST_ML_TENSOR_LAYOUT_NCHW;
  }
  return GST_ML_TENSOR_LAYOUT_NHWC;
}

static inline gdouble
gst_ml_convert_uint8_to_mltype (GstMLType mltype, gdouble value) {

  switch (mltype) {
    case GST_ML_TYPE_INT8:
      value = value - INT8_CONVERSION_OFFSET;
      break;
    case GST_ML_TYPE_UINT8:
      // Nothing to do
      break;
    case GST_ML_TYPE_INT16:
      value = value * UINT16_CONVERSION_SIGMA - INT16_CONVERSION_OFFSET;
      break;
    case GST_ML_TYPE_UINT16:
      value = value * UINT16_CONVERSION_SIGMA;
      break;
    case GST_ML_TYPE_INT32:
      value = value * UINT32_CONVERSION_SIGMA - INT32_CONVERSION_OFFSET;
      break;
    case GST_ML_TYPE_UINT32:
      value = value * UINT32_CONVERSION_SIGMA;
      break;
    case GST_ML_TYPE_INT64:
      value = value * UINT64_CONVERSION_SIGMA - INT64_CONVERSION_OFFSET;
      break;
    case GST_ML_TYPE_UINT64:
      value = value * UINT64_CONVERSION_SIGMA;
      break;
    case GST_ML_TYPE_FLOAT16:
    case GST_ML_TYPE_FLOAT32:
      value = value * FLOAT_CONVERSION_SIGMA;
      break;
    default:
      GST_ERROR ("Unsupported mltype: %d", mltype);
      break;
  }

  return value;
}

static inline gboolean
gst_region_of_interest_is_valid (GstVideoRegionOfInterestMeta * roimeta,
    const GArray * roi_stage_ids)
{
  guint idx = 0, stage_id = 0;

  for (idx = 0; idx < roi_stage_ids->len; idx++) {
    stage_id = g_array_index (roi_stage_ids, guint, idx);

    if (GST_META_ID_GET_STAGE (roimeta->id) == stage_id)
      return TRUE;
  }

  return FALSE;
}

static inline guint
gst_buffer_get_region_of_interest_meta_index (GstBuffer * buffer,
    const gint roi_id, const GArray * roi_stage_ids)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;
  guint index = 0;

  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    if (roi_id == roimeta->id)
      break;

    if (gst_region_of_interest_is_valid (roimeta, roi_stage_ids))
      index++;
  }

  return index;
}

static inline guint
gst_buffer_get_region_of_interest_n_meta (GstBuffer * buffer,
    const GArray * roi_stage_ids)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;
  guint n_metas = 0;

  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    if (roi_stage_ids->len != 0) {
      // Check if the ROI has a valid stage ID.
      n_metas += gst_region_of_interest_is_valid (roimeta, roi_stage_ids) ? 1 : 0;
    } else {
      // The stage IDs array is empty, there are no restriction for teh ROIs.
      n_metas++;
    }
  }

  return n_metas;
}

static inline GstBuffer *
gst_buffer_new_from_parent_memory (GstBuffer * buffer, guint index, guint depth)
{
  GstBuffer *newbuffer = NULL;
  GstVideoMeta *vmeta = NULL;
  GstProtectionMeta *pmeta = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;
  gint stream_id = -1;

  // Create a new buffer to placehold a reference to a single GstMemory block.
  newbuffer = gst_buffer_new ();

  // Append the memory block from input buffer into the new buffer.
  gst_buffer_append_memory (newbuffer, gst_buffer_get_memory (buffer, index));
  // Add parent meta, input buffer won't be released until new buffer is freed.
  gst_buffer_add_parent_buffer_meta (newbuffer, buffer);

  // Copy video metadata for current memory block into the new buffer.
  if ((vmeta = gst_buffer_get_video_meta_id (buffer, index)) != NULL)
    gst_buffer_add_video_meta_full (newbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        vmeta->format, vmeta->width, vmeta->height, vmeta->n_planes,
        vmeta->offset, vmeta->stride);

  // Extract the stream ID embedded in the offset field for this memory block.
  stream_id = gst_mux_buffer_get_memory_stream_id (buffer, index / depth);

  // Set the stream ID inside the offset field of the child buffer.
  GST_BUFFER_OFFSET (newbuffer) = stream_id;

  // Use the timestamp of the muxed buffer, it could be used downstream for
  // synchronization for the post-processing result with the muxed buffer.
  GST_BUFFER_TIMESTAMP (newbuffer) = GST_BUFFER_TIMESTAMP (buffer);

  // Get the the stream protection meta structure with that memory index.
  pmeta = gst_buffer_get_protection_meta_id (buffer,
      gst_mux_stream_name (stream_id));

  // Extract the original timestamp and place it in the DTS field as the PTS is
  // occupied, later it will be propagate via the protection meta downstream.
  gst_structure_get_uint64 (pmeta->info, "timestamp",
      &(GST_BUFFER_DTS (newbuffer)));

  gst_structure_get_uint64 (pmeta->info, "duration",
      &(GST_BUFFER_DURATION (newbuffer)));
  gst_structure_get_uint (pmeta->info, "flags",
      &(GST_BUFFER_FLAGS (newbuffer)));

  // Transfer ROIs associated with the stream ID for this memory block.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    guint id = roimeta->id;

    if (!(id & (stream_id << GST_MUX_STREAM_ID_OFFSET)))
      continue;

    roimeta = gst_buffer_add_video_region_of_interest_meta_id (newbuffer,
        roimeta->roi_type, roimeta->x, roimeta->y, roimeta->w, roimeta->h);
    roimeta->id = id;
  }

  return newbuffer;
}

static inline void
gst_video_frame_unmap_and_reset (GstVideoFrame * frame)
{
  gst_video_frame_unmap (frame);

  frame->buffer = NULL;
  frame->id = 0;
  frame->flags = 0;
  frame->meta = NULL;

  memset (frame->data, 0, sizeof (gpointer) * GST_VIDEO_MAX_PLANES);
  memset (frame->map, 0, sizeof (GstMapInfo) * GST_VIDEO_MAX_PLANES);

  gst_video_info_init (&(frame->info));
}

static gboolean
gst_matrix_inverse (const gdouble matrix[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE],
    const guint size, gdouble inverse[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE])
{
  guint row = 0, column = 0, idx = 0, num = 0, maxrow = 0;
  gdouble value = 0.0, augmented[MATRIX_MAX_SIZE][2 * MATRIX_MAX_SIZE] = {0};

  // Form augmented matrix with the help of input and an identity matrix.
  for (row = 0; row < size; row++) {
    for (column = 0; column < size; column++) {
      augmented[row][column] = matrix[row][column];
      augmented[row][size + column] = (row == column) ? 1.0 : 0.0;
    }
  }

  // Find the inverse matrix using Gauss-Jordan elimination.
  for (row = 0; row < size; row++, maxrow = row) {
    // Find the row with the highest pivot point among current and remaining rows.
    for (idx = row + 1; idx < size; idx++)
      maxrow = fabs(augmented[idx][row]) > fabs(augmented[maxrow][row]) ? idx : maxrow;

    // Check if pivot is 0 - meaning a singular matrix.
    if (augmented[maxrow][row] == 0.0)
      return FALSE;

    // Swap rows if there is a nother row with higher pivot value.
    for (column = 0; (maxrow != row) && (column < (2 * size)); column++) {
      value = augmented[row][column];
      augmented[row][column] = augmented[maxrow][column];
      augmented[maxrow][column] = value;
    }

    // Normalize the current row by making the value of the pivot element to 1.
    value = augmented[row][row];

    for (column = 0; column < (2 * size); column++)
      augmented[row][column] /= value;

    // Eliminate all elements above and under the pivot point (row-reduction).
    for (idx = 0, column = row; idx < size; idx++) {
      if (idx == column)
        continue;

      value = augmented[idx][column];

      for (num = 0; num < (2 * size); num++)
        augmented[idx][num] -= augmented[row][num] * value;
    }
  }

  // Extract the inverse matrix values from the augmented matrix.
  for (row = 0; row < size; row++)
    for (column = 0; column < size; column++)
      inverse[row][column] = augmented[row][size + column];

  return TRUE;
}

static gboolean
gst_video_source_inverse_affine_matrix (GstVideoQuadrilateral * source,
    gdouble matrix[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE])
{
  GstVideoPoint inpoints[4] = {0}, outpoints[4] = {0};
  gdouble intermediary[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE] = {0};
  gdouble inverse[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE] = {0};
  gdouble affine[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE] = {0};
  guint idx = 0, num = 0, row = 0, column = 0;

  inpoints[0] = source->a;
  inpoints[1] = source->b;
  inpoints[2] = source->c;
  inpoints[3] = source->d;

  // End points are in relative coordinates as post-process outputs are relative.
  outpoints[0] = (GstVideoPoint) {0, 0};
  outpoints[1] = (GstVideoPoint) {0, 1};
  outpoints[2] = (GstVideoPoint) {1, 0};
  outpoints[3] = (GstVideoPoint) {1, 1};

  // Represent the system of 8 linear equations for the 4 points as a matrix.
  // x' = (A0 * x + A1 * y + A2) / (C0 * x + C1 * y + C2)
  // y' = (B0 * x + B1 * y + B2) / (C0 * x + C1 * y + C2)
  //
  // System has 8 unknowns with C2 element equal to 1. After simplification:
  // | ax  ay  1   0   0  0  -ax*ax'  -ay*ax' | | A0 |   | ax' |
  // |  0   0  0  ax  ay  1  -ax*ay'  -ay*ay' | | A1 |   | ay' |
  // | bx  by  1   0   0  0  -bx*bx'  -by*bx' | | A2 |   | bx' |
  // |  0   0  0  bx  by  1  -bx*by'  -by*by' | | B0 | = | by' |
  // | cx  cy  1   0   0  0  -cx*cx'  -cy*cx' | | B1 |   | cx' |
  // |  0   0  0  cx  cy  1  -cx*cy'  -cy*cy' | | B2 |   | cy' |
  // | dx  dy  1   0   0  0  -dx*dx'  -dy*dx' | | C0 |   | dx' |
  // |  0   0  0  dx  dy  1  -dx*dy'  -dy*dy' | | C1 |   | dy' |
  for (num = 0, idx = 0; num < 4; num++, idx += 2) {
    intermediary[idx][0] = inpoints[num].x;
    intermediary[idx][1] = inpoints[num].y;
    intermediary[idx][2] = 1;
    intermediary[idx][6] = -inpoints[num].x * outpoints[num].x;
    intermediary[idx][7] = -inpoints[num].y * outpoints[num].x;

    intermediary[idx + 1][3] = inpoints[num].x;
    intermediary[idx + 1][4] = inpoints[num].y;
    intermediary[idx + 1][5] = 1;
    intermediary[idx + 1][6] = -inpoints[num].x * outpoints[num].y;
    intermediary[idx + 1][7] = -inpoints[num].y * outpoints[num].y;
  }

  // Find the inverse matrix for usage in A x X = B -> inv(A) x B = X
  if (!gst_matrix_inverse (intermediary, 8, inverse))
    return FALSE;

  // Find the matrix for the affine transformation from source to destination.
  for (idx = 0; idx < 8; idx++, row = (idx / 3), column = (idx % 3)) {
    for (num = 0; num < 4; num++) {
      affine[row][column] += inverse[idx][num * 2] * outpoints[num].x;
      affine[row][column] += inverse[idx][num * 2 + 1] * outpoints[num].y;
    }
  }

  // Last element C2 of the affine matrix is 1 as per initial condition.
  affine[2][2] = 1.0;

  // Find the inverse matrix which post-process will use for correction.
  if (!gst_matrix_inverse (affine, 3, matrix))
    return FALSE;

  return TRUE;
}

static GstProtectionMeta*
gst_ml_video_converter_retrieve_protection_meta (GstMLVideoConverter * mlconverter,
      GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstProtectionMeta *pmeta = NULL;
  const gchar *name = gst_batch_channel_name (mlconverter->batch_idx);

  // If protection meta is already initialized return that instance.
  if ((pmeta = gst_buffer_get_protection_meta_id (outbuffer, name)) != NULL)
    return pmeta;

  // Add protection meta containing information for decryption downstream.
  pmeta = gst_buffer_add_protection_meta (outbuffer,
      gst_structure_new_empty (name));

  // Add input tensor resolution for tensor result decryption downstream.
  gst_ml_structure_set_source_dimensions (pmeta->info,
      GST_ML_INFO_TENSOR_DIM_W (mlconverter->tensorlayout, mlconverter->mlinfo),
      GST_ML_INFO_TENSOR_DIM_H (mlconverter->tensorlayout, mlconverter->mlinfo));

  // Propagate the current index in the sequence and total sequence numbers.
  gst_structure_set (pmeta->info,
      "sequence-index", G_TYPE_UINT, mlconverter->seq_idx,
      "sequence-num-entries", G_TYPE_UINT, mlconverter->n_seq_entries, NULL);

  // Propagate the timestamp, could be used for synchronization downstream.
  gst_structure_set (pmeta->info,
      "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (inbuffer), NULL);

  // For muxed streams propagate the original buffer timestamp and stream ID.
  // The ID is taken from offset field while timestamp from DTS field.
  if (GST_VIDEO_INFO_MULTIVIEW_MODE (mlconverter->ininfo) ==
          GST_VIDEO_MULTIVIEW_MODE_SEPARATED) {
    gst_structure_set (pmeta->info,
        "stream-id", G_TYPE_INT, GST_BUFFER_OFFSET (inbuffer),
        "stream-timestamp", G_TYPE_UINT64, GST_BUFFER_DTS (inbuffer), NULL);
  }

  return pmeta;
}

static gboolean
gst_ml_video_converter_update_source (GstMLVideoConverter * mlconverter,
    GstVideoRegionOfInterestMeta * roimeta, GstVideoBlit * vblit,
    GstProtectionMeta * pmeta)
{
  GstVideoQuadrilateral *source = NULL;
  GstStructure *param = NULL, *xtraparams = NULL;
  const GValue *value = NULL;
  GValue matvals = G_VALUE_INIT, val = G_VALUE_INIT;
  gdouble matrix[3][3] = {0}, inverse[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE] = {0};
  gdouble intermediary[MATRIX_MAX_SIZE][MATRIX_MAX_SIZE] = {0};
  guint idx = 0, num = 0, row = 0, column = 0;

  source = &(vblit->source);
  vblit->mask |= GST_VCE_MASK_SOURCE;

  // Initialize the source region with full dimensions of the blit frame.
  source->a.x = source->a.y = source->b.x = source->c.y = 0;
  source->c.x = source->d.x = GST_VIDEO_FRAME_WIDTH (vblit->frame);
  source->b.y = source->d.y = GST_VIDEO_FRAME_HEIGHT (vblit->frame);

  if (roimeta == NULL)
    return TRUE;

  // Update the quadrilateral coordinates with the data from the ROI meta.
  source->a = (GstVideoPoint){roimeta->x, roimeta->y};
  source->b = (GstVideoPoint){roimeta->x, roimeta->y + roimeta->h};
  source->c = (GstVideoPoint){roimeta->x + roimeta->w, roimeta->y};
  source->d = (GstVideoPoint){roimeta->x + roimeta->w, roimeta->y + roimeta->h};

  GST_TRACE_OBJECT (mlconverter, "ROI[%d %d %d %d]: A(%f, %f) B(%f, %f) "
      "C(%f, %f) D(%f, %f)", roimeta->x, roimeta->y, roimeta->w, roimeta->h,
      source->a.x, source->a.y, source->b.x, source->b.y, source->c.x,
      source->c.y, source->d.x, source->d.y);

  // Propagate the ID of the ROI from which this batch position was created.
  // TODO Protection meta needs revision when tensors with depth are involved.
  gst_structure_set (pmeta->info, "parent-id", G_TYPE_INT, roimeta->id, NULL);

  param = gst_video_region_of_interest_meta_get_param (roimeta, "ObjectDetection");
  if (param == NULL)
    return TRUE;

  if ((value = gst_structure_get_value (param, "xtraparams")) == NULL)
    return TRUE;

  xtraparams = GST_STRUCTURE (g_value_get_boxed (value));
  value = gst_structure_get_value (xtraparams, "affine-matrix");

  if (value == NULL)
    return TRUE;

  // Expected number of values is 9 for a 3x3 affine matrix.
  if (gst_value_array_get_size (value) != 9) {
    GST_ERROR_OBJECT (mlconverter, "Invalid number of values in the "
        "'affine-matrix' field!");
    return FALSE;
  }

  for (idx = 0; idx < 9; idx++, row = (idx / 3), num = (idx % 3))
    matrix[row][num] = g_value_get_double (gst_value_array_get_value (value, idx));

  gst_video_point_affine_correction (&(source->a), matrix);
  gst_video_point_affine_correction (&(source->b), matrix);
  gst_video_point_affine_correction (&(source->c), matrix);
  gst_video_point_affine_correction (&(source->d), matrix);

  GST_TRACE_OBJECT (mlconverter, "Affine transformation quadrilateral: A(%f, %f)"
      " B(%f, %f) C(%f, %f) D(%f, %f)", source->a.x, source->a.y, source->b.x,
      source->b.y, source->c.x, source->c.y, source->d.x, source->d.y);

  // Calcualte the inverse affine matrix for source -> relative destination.
  if (!gst_video_source_inverse_affine_matrix (source, inverse)) {
    GST_ERROR_OBJECT (mlconverter, "The inverse affine matrix is singular!");
    return FALSE;
  }

  // Matrix for converting the coordinates after inverse affine into relative.
  intermediary[0][0] = 1.0 / roimeta->w;
  intermediary[1][1] = 1.0 / roimeta->h;
  intermediary[0][2] = -((gdouble) roimeta->x / roimeta->w);
  intermediary[1][2] = -((gdouble) roimeta->y / roimeta->h);
  intermediary[2][2] = 1.0;

  // Multiply the matrices from above in order to get final matrix.
  for (row = 0; row < 3; row++) {
    for (column = 0; column < 3; column++) {
      matrix[row][column] = 0;

      for (num = 0; num < 3; num++)
        matrix[row][column] += intermediary[row][num] * inverse[num][column];
    }
  }

  // Create an inverse affine matrix entry for correction downstream.
  g_value_init (&matvals, GST_TYPE_ARRAY);
  g_value_init (&val, G_TYPE_DOUBLE);

  for (row = 0; row < 3; row++) {
    for (column = 0; column < 3; column++) {
      g_value_set_double (&val, matrix[row][column]);
      gst_value_array_append_value (&matvals, &val);
    }
  }

  // Propagate the affine matrix for correction of the results in post-process.
  gst_structure_take_value (pmeta->info, "inverse-affine-matrix", &matvals);

  g_value_unset (&val);
  g_value_unset (&matvals);

  return TRUE;
}

static void
gst_ml_video_converter_update_destination (GstMLVideoConverter * mlconverter,
    GstVideoBlit * vblit, GstProtectionMeta * pmeta)
{
  GstVideoQuadrilateral *source = NULL;
  GstVideoRectangle *destination = NULL;
  guint n_batch = 0, depth = 0;
  gint inwidth = 0, inheight = 0, maxwidth = 0, maxheight = 0;

  destination = &(vblit->destination);
  vblit->mask |= GST_VCE_MASK_DESTINATION;

  n_batch = GST_ML_INFO_TENSOR_DIM_N (mlconverter->tensorlayout,
      mlconverter->mlinfo);
  depth = GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout,
      mlconverter->mlinfo);

  maxwidth = GST_VIDEO_FRAME_WIDTH (mlconverter->composition.frame);
  maxheight = GST_VIDEO_FRAME_HEIGHT (mlconverter->composition.frame) /
      (n_batch * depth);

  destination->y = destination->x = 0;
  destination->w = maxwidth;
  destination->h = maxheight;

  // If the image disposition is simply to stretch simply return, nothing to do.
  if (mlconverter->disposition == GST_ML_VIDEO_DISPOSITION_STRETCH)
    goto exit;

  source = &(vblit->source);

  // Get the dimensions of the source rectangle, even if it is tilted.
  inwidth = sqrt (((source->a.x - source->c.x) * (source->a.x - source->c.x)) +
      ((source->a.y - source->c.y) * (source->a.y - source->c.y)));
  inheight = sqrt (((source->a.x - source->b.x) * (source->a.x - source->b.x)) +
      ((source->a.y - source->b.y) * (source->a.y - source->b.y)));

  // Disposition is one of two modes with AR (Aspect Ratio) preservation.
  // Recalculate the destination width or height depending on the ratios.
  if ((inwidth * destination->h) > (inheight * destination->w))
    destination->h = gst_util_uint64_scale_int (maxwidth, inheight, inwidth);
  else if ((inwidth * destination->h) < (inheight * destination->w))
    destination->w = gst_util_uint64_scale_int (maxheight, inwidth, inheight);

  // No additional adjustment to the X and Y axis are needed, simply return.
  if (mlconverter->disposition == GST_ML_VIDEO_DISPOSITION_TOP_LEFT)
    goto exit;

  // Additional correction of X and Y axis for centred image disposition.
  destination->x = (maxwidth - destination->w) / 2;
  destination->y += (maxheight - destination->h) / 2;

exit:
  // Populate the tensor region actually populated with data for decryption.
  // Each region is given in separate coordinates, exclude the later added offset.
  // TODO Protection meta needs revision when tensors with depth are involved.
  gst_ml_structure_set_source_region (pmeta->info, destination);
}

static gint
gst_ml_video_converter_update_blit_params (GstMLVideoConverter * mlconverter,
    GstBuffer * inbuffer)
{
  GstVideoComposition *composition = NULL;
  GstVideoBlit *vblit = NULL;
  GstBuffer *outbuffer = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  GstProtectionMeta *pmeta = NULL;
  GstVideoQuadrilateral *source = NULL;
  GstVideoRectangle *destination = NULL;
  gpointer state = NULL;
  guint idx = 0, num = 0, depth = 0, n_batch = 0, n_regions = 1, n_positions = 0;
  gboolean success = FALSE;

  composition = &(mlconverter->composition);
  outbuffer = composition->frame->buffer;

  // Expected tensor batch size and depth.
  n_batch = GST_ML_INFO_TENSOR_DIM_N (mlconverter->tensorlayout,
      mlconverter->mlinfo);
  depth = GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout,
      mlconverter->mlinfo);

  if GST_CONVERSION_MODE_IS_ROI (mlconverter->mode) {
    n_regions = gst_buffer_get_region_of_interest_n_meta (inbuffer,
        mlconverter->roi_stage_ids);
  }

  GST_TRACE_OBJECT (mlconverter, "Number of Source/Destination regions "
      "(Initial): [%u]", n_regions);

  // Decrease the regions if some of them were previously processed.
  if (mlconverter->next_roi_id != -1) {
    n_regions -= gst_buffer_get_region_of_interest_meta_index (inbuffer,
        mlconverter->next_roi_id, mlconverter->roi_stage_ids);
  }

  GST_TRACE_OBJECT (mlconverter, "Number of Source/Destination regions "
      "(Intermediary): [%u]", n_regions);

  // Calculate the number of remaining positions.
  n_positions = (depth * (n_batch - mlconverter->batch_idx)) +
      ((mlconverter->depth_idx == 0) ? 0 : (depth - mlconverter->depth_idx));

  // Limit the regions to the number of remaining tensor positions if necessary.
  n_regions = MIN (n_positions, n_regions);

  GST_TRACE_OBJECT (mlconverter, "Number of Source/Destination regions "
      "(Final): [%u]", n_regions);

  do {
    // Increment the sequence index if this is the start of a new batch of depth.
    if (mlconverter->depth_idx == 0)
      mlconverter->seq_idx++;

    // Index and convinient pointer to the current blit object.
    idx = composition->n_blits;
    vblit = &(composition->blits[idx]);

    success = gst_video_frame_map (vblit->frame, mlconverter->ininfo, inbuffer,
        GST_MAP_READ);
    if (!success) {
      GST_ERROR_OBJECT (mlconverter, "Failed to map input frame!");
      return -1;
    }

    if (GST_CONVERSION_MODE_IS_ROI (mlconverter->mode)) {
      roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state);

      // Loop until the stashed ROI meta ID is reached and continue from there.
      while (((mlconverter->next_roi_id != -1) &&
                (roimeta->id != mlconverter->next_roi_id)) ||
          !gst_region_of_interest_is_valid (roimeta, mlconverter->roi_stage_ids))
        roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state);

      // Reset the stashed ROI ID in case it was previously set.
      mlconverter->next_roi_id = -1;
    } else { // GST_CONVERSION_MODE_IS_IMAGE (mlconverter->mode)
      // Check whether the image was produced as a split from some base image.
      while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state)) != NULL) {
        if (roimeta->roi_type == g_quark_from_static_string ("ImageRegion"))
          break;
      }
    }

    // TODO Protection meta needs revision.
    pmeta = gst_ml_video_converter_retrieve_protection_meta (mlconverter,
        inbuffer, outbuffer);

    // Update blit source quadrilateral and decryption meta based on the ROI meta.
    if (!gst_ml_video_converter_update_source (mlconverter, roimeta, vblit, pmeta))
      return -1;

    // Update blit destination rectangle based on the disposition.
    gst_ml_video_converter_update_destination (mlconverter, vblit, pmeta);

    source = &(vblit->source);
    destination = &(vblit->destination);

    // Add the Y axis offset for this ROI meta in the output buffer.
    destination->y += (mlconverter->batch_idx * depth + mlconverter->depth_idx) *
        (GST_VIDEO_FRAME_HEIGHT (composition->frame) / (n_batch * depth));

    // Increment the tracker for the current depth position and if this is the
    // end of a new batch of depth positions then increment the batch index.
    if (++mlconverter->depth_idx == depth)
      GST_BUFFER_OFFSET (outbuffer) |= 1 << mlconverter->batch_idx++;

    GST_TRACE_OBJECT (mlconverter, "Sequence[%u / %u] Batch[%u / %u] Depth[%u "
        "/ %u] Source[%u]: [A(%f, %f) B(%f, %f) C(%f, %f) D(%f, %f)] Destination"
        "[%d %d %d %d]", mlconverter->seq_idx, mlconverter->n_seq_entries,
        mlconverter->batch_idx, n_batch, mlconverter->depth_idx, depth, idx,
        source->a.x, source->a.y, source->b.x, source->b.y, source->c.x,
        source->c.y, source->d.x, source->d.y, destination->x, destination->y,
        destination->w, destination->h);

    // Increament the number of populated blits.
    composition->n_blits++;

    // Filled all the depth positions in current batch, reset the depth index.
    if (mlconverter->depth_idx == depth)
      mlconverter->depth_idx = 0;

    // Increment the index for src/dest regions and loop if it's within range.
  } while (++num < n_regions);

  // Stash the next suitable ROI meta ID if not all ROI metas were processed.
  if (mlconverter->mode == GST_ML_CONVERSION_MODE_ROI_CUMULATIVE) {
    roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state);

    while ((roimeta != NULL) &&
           !gst_region_of_interest_is_valid (roimeta, mlconverter->roi_stage_ids))
      roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state);

    mlconverter->next_roi_id = (roimeta != NULL) ? roimeta->id : -1;
  }

  GST_TRACE_OBJECT (mlconverter, "Stashed ROI ID [%d]", mlconverter->next_roi_id);

  // Return the number of filled batch positions (regions).
  return n_regions;
}

static void
gst_ml_video_converter_cleanup_composition (GstMLVideoConverter * mlconverter)
{
  GstVideoComposition *composition = NULL;
  GstVideoBlit *blit = NULL;
  guint idx = 0, n_batch = 0, depth = 0;

  composition = &(mlconverter->composition);

  // Reset the number of blits back to the maximum number of tensors.
  depth = GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout,
      mlconverter->mlinfo);
  n_batch = GST_ML_INFO_TENSOR_DIM_N (mlconverter->tensorlayout,
      mlconverter->mlinfo);

  composition->n_blits = n_batch * depth;

  // Deallocate region rectangles, unmap frames and unreference buffers.
  for (idx = 0; idx < composition->n_blits; idx++) {
    blit = &(composition->blits[idx]);

    if (blit->frame->buffer != NULL)
      gst_video_frame_unmap_and_reset (blit->frame);
  }

  if (composition->frame->buffer != NULL)
    gst_video_frame_unmap_and_reset (composition->frame);
}

static gboolean
gst_ml_video_converter_setup_composition (GstMLVideoConverter * mlconverter,
    GstBuffer * outbuffer)
{
  GstVideoComposition *composition = NULL;
  GstBuffer *inbuffer = NULL, *buffer = NULL;
  GstVideoMultiviewMode mview_mode = GST_VIDEO_MULTIVIEW_MODE_NONE;
  guint n_batch = 0, depth = 0, n_positions = 0;
  gint n_memory = 0, mem_idx = 0, n_roi_meta = 0, n_filled_positions = 0;
  gboolean success = FALSE;

  composition = &(mlconverter->composition);
  composition->n_blits = 0;

  success = gst_video_frame_map (composition->frame, mlconverter->vinfo,
      outbuffer, GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);
  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to map output frame!");
    return FALSE;
  }

  mview_mode = GST_VIDEO_INFO_MULTIVIEW_MODE (mlconverter->ininfo);

  n_batch = GST_ML_INFO_TENSOR_DIM_N (mlconverter->tensorlayout,
      mlconverter->mlinfo);
  depth = GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout,
      mlconverter->mlinfo);

  // Expected tensor positions, when it reaches 0 all positions are populated.
  n_positions = n_batch * depth;

  // Pop buffers from the queue and fill the blit parameters of the composition.
  while (n_positions != 0 && (inbuffer = g_queue_pop_head (mlconverter->bufqueue))) {
    GST_TRACE_OBJECT (mlconverter, "Processing %" GST_PTR_FORMAT, inbuffer);

    // Get current memory index and number of memory blocks in the buffer.
    mem_idx = (mlconverter->next_mem_idx != -1) ? mlconverter->next_mem_idx : 0;
    n_memory = gst_buffer_n_memory (inbuffer);

    // If previous sequence was completed, set the trackers for the new sequence.
    if (mlconverter->seq_idx == mlconverter->n_seq_entries) {
      mlconverter->seq_idx = 0;

      // For ROI modes use the total number of ROI meta inside current buffer.
      // For image mode use the total number of memory blocks (muxed stream).
      if (GST_CONVERSION_MODE_IS_IMAGE (mlconverter->mode)) {
        // Divide by depth as that number of blocks belong to the same batch.
        mlconverter->n_seq_entries = n_memory / depth;
      } else { // GST_CONVERSION_MODE_IS_ROI (mlconverter->mode)
        // TODO Add handling for depth and ROI modes.
        mlconverter->n_seq_entries = gst_buffer_get_region_of_interest_n_meta (
            inbuffer,  mlconverter->roi_stage_ids);
      }

      // Limit to the batch size if operating in any of the non cumulative modes.
      // In non-cumulative modes we fill up to the batch size.
      if (GST_CONVERSION_MODE_IS_NON_CUMULATIVE (mlconverter->mode))
        mlconverter->n_seq_entries = MIN (n_batch, mlconverter->n_seq_entries);
    }

    // Initially assign the fetched input buffer to the working buffer pointer.
    buffer = inbuffer;

    do {
      if (mview_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED) {
        // Input is muxed stream separate each memory block into child buffer.
        buffer = gst_buffer_new_from_parent_memory (inbuffer, mem_idx, depth);

        n_roi_meta = gst_buffer_get_region_of_interest_n_meta (buffer,
            mlconverter->roi_stage_ids);

        if (GST_CONVERSION_MODE_IS_ROI (mlconverter->mode) && (n_roi_meta == 0)) {
          GST_TRACE_OBJECT (mlconverter, "Muxed stream buffer doesn't contain "
              "any ROI metas for memory block at '%u', skipping!", mem_idx);
          gst_buffer_unref (buffer);
          continue;
        }

        GST_TRACE_OBJECT (mlconverter, "Using muxed memory block at '%d' - %"
            GST_PTR_FORMAT, mem_idx, buffer);
      }

      n_filled_positions =
          gst_ml_video_converter_update_blit_params (mlconverter, buffer);

      if (!(success = (n_filled_positions > -1)))
        goto cleanup;

      // Decrease the batch size with the number of filled positions.
      n_positions -= n_filled_positions;

      // Release the reference to the child input buffer, no longer needed.
      // Child buffer will be fully released when the associated blits are reset.
      if (mview_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
        gst_buffer_unref (buffer);

      // Process until batch is filled or no more buffers.
    } while ((++mem_idx < n_memory) && (n_positions != 0));

    // Get the previous memory index if there are unprocessed ROI metas in it.
    if (mlconverter->next_roi_id != -1)
      mem_idx--;

    // Save the memory index if not all memory blocks were processed.
    if (GST_CONVERSION_MODE_IS_CUMULATIVE (mlconverter->mode))
      mlconverter->next_mem_idx = (mem_idx < n_memory) ? mem_idx : -1;

    GST_TRACE_OBJECT (mlconverter, "Stashed memory index [%d]",
        mlconverter->next_mem_idx);

    // Release the reference to the main input buffer, no longer needed.
    // The buffer will be fully released when all video blits have finished.
    gst_buffer_unref (inbuffer);
  }

  // Reset the global trackers for batch and depth position for next setup call.
  mlconverter->batch_idx = mlconverter->depth_idx = 0;

  GST_TRACE_OBJECT (mlconverter, "Output %" GST_PTR_FORMAT,
      composition->frame->buffer);

cleanup:
  if (!success && (buffer != NULL) && (buffer != inbuffer))
    gst_buffer_unref (buffer);

  if (!success && (inbuffer != NULL))
    gst_buffer_unref (inbuffer);

  if (!success)
    gst_ml_video_converter_cleanup_composition (mlconverter);

  return success;
}

static gboolean
gst_ml_video_converter_prepare_buffer_queues (GstMLVideoConverter * mlconverter,
    GstBuffer * inbuffer)
{
  GList *l = NULL;
  guint n_batch = 0, depth = 0, n_regions = 0, n_memory = 0;

  // Expected tensor batch and depth size.
  n_batch = GST_ML_INFO_TENSOR_DIM_N (mlconverter->tensorlayout,
      mlconverter->mlinfo);
  depth = GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout,
      mlconverter->mlinfo);

  // Verify that buffer has enough memory blocks when tensor has depth.
  if (mlconverter->mode == GST_ML_CONVERSION_MODE_IMAGE_NON_CUMULATIVE) {
    n_memory = gst_buffer_n_memory (inbuffer);

    if ((n_memory % depth) != 0) {
      GST_ERROR_OBJECT (mlconverter, "Input buffer has %d memory blocks but "
          "expecting multiples of depth %d, dropping buffer!", n_memory, depth);
      return FALSE;
    }
  } else if (mlconverter->mode == GST_ML_CONVERSION_MODE_ROI_NON_CUMULATIVE) {
    // TODO Add handling for depth and ROI_NON_CUMULATIVE
  }

  // A non-accumulative conversion mode, place the buffer in the internal queue
  // and return TRUE in order to process it immediately.
  if (GST_CONVERSION_MODE_IS_NON_CUMULATIVE (mlconverter->mode)) {
    g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
    return TRUE;
  }

  // Input is GAP, return TRUE in order to process buffers in the internal queue
  // and set buffer as queued_buf to the base class for subsequent processing.
  if ((gst_buffer_get_size (inbuffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    GST_BASE_TRANSFORM (mlconverter)->queued_buf = gst_buffer_ref (inbuffer);
    return TRUE;
  }

  // TODO Add handling for depth and ROI_CUMULATIVE
  if (mlconverter->mode == GST_ML_CONVERSION_MODE_ROI_CUMULATIVE) {
    // Accumulative ROI batch mode, base decisions on the number of ROI metas.
    n_regions = gst_buffer_get_region_of_interest_n_meta (inbuffer,
        mlconverter->roi_stage_ids);

    // Buffer does not contain ROI metas, process buffers in the internal queue
    // and set buffer as queued_buf to the base class for subsequent processing.
    if (n_regions == 0) {
      GST_BASE_TRANSFORM (mlconverter)->queued_buf = gst_buffer_ref (inbuffer);
      return TRUE;
    }

    // Calculate the total number of ROI metas.
    for (l = g_queue_peek_head_link (mlconverter->bufqueue); l != NULL; l = l->next) {
      GstBuffer *buffer = GST_BUFFER (l->data);

      n_regions += gst_buffer_get_region_of_interest_n_meta (buffer,
          mlconverter->roi_stage_ids);
    }

    // Decrease the ROI count if some of the ROIs were previously processed.
    if (mlconverter->next_roi_id != -1) {
      n_regions -= gst_buffer_get_region_of_interest_meta_index (inbuffer,
          mlconverter->next_roi_id, mlconverter->roi_stage_ids);
    }

    if (n_regions < n_batch) {
      // Not enough ROIs, stash current buffer and check again on next buffer.
      g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
      return FALSE;
    } else if (n_regions == n_batch) {
      // Enough ROIs in the internal queue and this buffer, place the buffer in
      // the internal queue and return TRUE in order to process it immediately.
      g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
      return TRUE;
    } else { // (n_regions > n_batch)
      // Buffer has more than enough ROI for more batch sizes, place it in the
      // internal queuein order to process it immediately and set the buffer
      // as the base class queued_buf for subsequent processing.
      g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
      GST_BASE_TRANSFORM (mlconverter)->queued_buf = gst_buffer_ref (inbuffer);
      return TRUE;
    }
  } else { // GST_ML_CONVERSION_MODE_IMAGE_CUMULATIVE
    n_memory = gst_buffer_n_memory (inbuffer);

    // Calculate the total number of image memories.
    for (l = g_queue_peek_head_link (mlconverter->bufqueue); l != NULL; l = l->next)
      n_memory += gst_buffer_n_memory (GST_BUFFER (l->data));

    // Decrease the image block count if some of the memories were already processed.
    if (mlconverter->next_mem_idx != -1)
      n_memory -= mlconverter->next_mem_idx;

    if (n_memory < n_batch * depth) {
      // Not enough image blocks, stash current buffer and check again on next buffer.
      g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
      return FALSE;
    } else if (n_memory == n_batch * depth) {
      // Enough image blocks in the internal queue and this buffer, place the
      // buffer in the internal queue and return TRUE in order to process it.
      g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
      return TRUE;
    } else { // (n_memory > n_batch * depth)
      // Buffer has more than enough image blocks for more batch sizes, place
      // it in the internal queue in order to process it immediately and set
      // the buffer as the base class queued_buf for subsequent processing.
      g_queue_push_tail (mlconverter->bufqueue, gst_buffer_ref (inbuffer));
      GST_BASE_TRANSFORM (mlconverter)->queued_buf = gst_buffer_ref (inbuffer);
      return TRUE;
    }
  }

  GST_ERROR_OBJECT (mlconverter, "Improper handling of the buffer queues!");
  return FALSE;
}

static gboolean
gst_ml_video_converter_normalize (GstMLVideoConverter * mlconverter,
    GstVideoBlit * vblit, GstVideoFrame * outframe)
{
  guint8 *indata = NULL;
  gpointer outdata = NULL;
  GstVideoRectangle source = {0};
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gdouble mean[4] = {0}, sigma[4] = {0};
  gint inidx = 0, instride = 0, row = 0, column = 0, outwidth = 0, outheight = 0;
  guint idx = 0, outidx = 0, num = 0, bpp = 0;

  gst_video_quadrilateral_to_rectangle (&(vblit->source), &source);

  // Sanity checks, input and output frame must differ only in type.
  g_return_val_if_fail (GST_VIDEO_FRAME_FORMAT (vblit->frame) ==
      GST_VIDEO_FRAME_FORMAT (outframe), FALSE);

  g_return_val_if_fail (source.w == vblit->destination.w, FALSE);
  g_return_val_if_fail (source.h == vblit->destination.h, FALSE);

  // Retrive the input frame Bytes Per Pixel for later calculations.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (vblit->frame->info.finfo) *
      GST_VIDEO_FORMAT_INFO_N_COMPONENTS (vblit->frame->info.finfo);
  bpp /= 8;

  // Convinient local variables for per channel mean and sigma values.
  for (idx = 0; idx < bpp; idx++) {
    mean[idx] = GET_MEAN_VALUE (mlconverter->mean, idx);
    sigma[idx] = GET_SIGMA_VALUE (mlconverter->sigma, idx);
  }

  indata = GST_VIDEO_FRAME_PLANE_DATA (vblit->frame, 0);
  outdata = GST_VIDEO_FRAME_PLANE_DATA (outframe, 0);
  mltype = GST_ML_INFO_TYPE (mlconverter->mlinfo);

  outwidth = GST_VIDEO_FRAME_WIDTH (outframe);
  outheight = GST_VIDEO_FRAME_HEIGHT (outframe);
  instride = GST_VIDEO_FRAME_PLANE_STRIDE (vblit->frame, 0);

  for (row = 0; row < outheight; row++) {
    outidx = row * outwidth * bpp;

    for (column = 0; column < outwidth; column++, inidx = -1) {
      // Take the value from source only if it is within its coordinates.
      if ((row >= vblit->destination.y) && (column >= vblit->destination.x) &&
          (row < (vblit->destination.y + vblit->destination.h)) &&
          (column < (vblit->destination.x + vblit->destination.w))) {
        inidx = (source.y + (row - vblit->destination.y)) * instride;
        inidx += (source.x + (column - vblit->destination.x)) * bpp;
      }

      // Assign a normalized value for each byte in the pixel.
      for (num = 0; num < bpp; num++, outidx++) {
        gdouble value = (inidx != -1) ? indata[inidx++] : 0;

        // Convert value to actual tensor type and apply normalization.
        value = gst_ml_convert_uint8_to_mltype (mltype, value);
        value = (value - mean[num]) * sigma[num];

        gst_ml_tensor_assign_value (mltype, outdata, outidx, value);
      }
    }
  }

  return TRUE;
}

static GstCaps *
gst_ml_video_converter_translate_ml_caps (GstMLVideoConverter * mlconverter,
    const GstCaps * caps)
{
  GstCaps *result = NULL, *tmplcaps = NULL;
  GstMLInfo mlinfo;
  GstTensorLayout tensorlayout;
  gint idx = 0, length = 0;

  tmplcaps = gst_caps_new_empty ();

  if (gst_gbm_qcom_backend_is_supported ()) {
    gst_caps_append_structure_full (tmplcaps,
        gst_structure_new_empty ("video/x-raw"),
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
  }

  gst_caps_append_structure (tmplcaps,
      gst_structure_new_empty ("video/x-raw"));

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return tmplcaps;

  if (!gst_caps_is_fixed (caps) || !gst_ml_info_from_caps (&mlinfo, caps))
    return tmplcaps;

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (tmplcaps, idx);
    GstCapsFeatures *features = gst_caps_get_features (tmplcaps, idx);

    GValue formats = G_VALUE_INIT;
    const GValue *value = NULL;

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (result, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Get tensor layout based on tensor dimensions
    tensorlayout = gst_ml_info_get_layout (&mlinfo);

    gst_structure_set (structure,
        "height", G_TYPE_INT, GST_ML_INFO_TENSOR_DIM_H (tensorlayout, &mlinfo),
        "width", G_TYPE_INT, GST_ML_INFO_TENSOR_DIM_W (tensorlayout, &mlinfo),
        NULL);

    // 4th dimension corresponds to the bit depth.
    if (GST_ML_INFO_TENSOR_DIM_C (tensorlayout, &mlinfo) == 1) {
      init_formats (&formats, "GRAY8", NULL);
    } else if (GST_ML_INFO_TENSOR_DIM_C (tensorlayout, &mlinfo) == 3) {
      if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR) {
        if (tensorlayout.c == GST_ML_TENSOR_LAYOUT_NCHW.c)
          init_formats (&formats, "RGBP", NULL);
        else
          init_formats (&formats, "RGB", NULL);
      } else if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE) {
        if (tensorlayout.c == GST_ML_TENSOR_LAYOUT_NCHW.c)
          init_formats (&formats, "BGRP", NULL);
        else
          init_formats (&formats, "BGR", NULL);
      }
    } else if (GST_ML_INFO_TENSOR_DIM_C (tensorlayout, &mlinfo) == 4) {
      if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR)
        init_formats (&formats, "RGBA", "RGBx", "ARGB", "xRGB", NULL);
      else if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE)
        init_formats (&formats, "BGRA", "BGRx", "ABGR", "xBGR", NULL);
    }

    gst_structure_set_value (structure, "format", &formats);
    g_value_unset (&formats);

    // Extract the frame rate from ML and propagate it to the video caps.
    value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

    if (value != NULL)
      gst_structure_set_value (structure, "framerate", value);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));
  }

  gst_caps_unref (tmplcaps);

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_video_converter_translate_video_caps (GstMLVideoConverter * mlconverter,
    const GstCaps * caps)
{
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GValue dimensions = G_VALUE_INIT, entry = G_VALUE_INIT, dimension = G_VALUE_INIT;
  const GValue *value;

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return gst_caps_new_empty_simple ("neural-network/tensors");

  result = gst_caps_new_simple ("neural-network/tensors",
      "type", G_TYPE_STRING, gst_ml_type_to_string (GST_ML_TYPE_UINT8),
      NULL);

  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "width");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  value = gst_structure_get_value (structure, "height");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  value = gst_structure_get_value (structure, "format");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  g_value_init (&dimensions, GST_TYPE_ARRAY);
  g_value_init (&entry, GST_TYPE_ARRAY);
  g_value_init (&dimension, G_TYPE_INT);

  g_value_set_int (&dimension, 1);
  gst_value_array_append_value (&entry, &dimension);

  // 2nd dimension is video height.
  gst_value_array_append_value (&entry,
      gst_structure_get_value (structure, "height"));

  // 3rd dimension is video width.
  gst_value_array_append_value (&entry,
      gst_structure_get_value (structure, "width"));

  value = gst_structure_get_value (structure, "format");

  // 4th dimension is video channels number.
  switch (gst_video_format_from_string (g_value_get_string (value))) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      g_value_set_int (&dimension, 4);
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      g_value_set_int (&dimension, 3);
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      g_value_set_int (&dimension, 1);
      break;
    default:
      GST_WARNING_OBJECT (mlconverter, "Unsupported format: %s, "
          "falling back to RGB!", g_value_get_string (value));
      g_value_set_int (&dimension, 3);
      break;
  }

  gst_value_array_append_value (&entry, &dimension);
  g_value_unset (&dimension);

  gst_value_array_append_value (&dimensions, &entry);
  g_value_unset (&entry);

  gst_caps_set_value (result, "dimensions", &dimensions);
  g_value_unset (&dimensions);

  // Extract the frame rate from video and propagate it to the ML caps.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      "framerate");

  if (value != NULL)
    gst_caps_set_value (result, "rate", value);

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstBufferPool *
gst_ml_video_converter_create_pool (GstMLVideoConverter * mlconverter,
    GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;
  guint size = 1, stride = 0, alignment = 0;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlconverter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (mlconverter, "Uses DMA memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_DMA);

  config = gst_buffer_pool_get_config (pool);

  alignment = gst_gfx_adreno_get_alignment ();
  stride = GST_ML_INFO_TENSOR_DIM_W (mlconverter->tensorlayout, &info) *
      GST_ML_INFO_TENSOR_DIM_C (mlconverter->tensorlayout, &info);

  size *= GST_ML_INFO_TENSOR_DIM_N (mlconverter->tensorlayout, &info);
  size *= GST_ROUND_UP_N (stride, alignment);
  size *= GST_ROUND_UP_4 (
      GST_ML_INFO_TENSOR_DIM_H (mlconverter->tensorlayout, &info));
  size *= GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout, &info);
  size *= gst_ml_type_get_size (info.type);

  gst_buffer_pool_config_set_params (config, caps, size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_KEEP_MAPPED);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_CONTINUOUS);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (mlconverter, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_video_converter_propose_allocation (GstBaseTransform * base,
    GstQuery * inquery, GstQuery * outquery)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info = {0,};
  guint size = 0, minbuffers = 0;
  gboolean needpool = FALSE;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (
        base, inquery, outquery))
    return FALSE;

  // No input query, nothing to do.
  if (NULL == inquery)
    return TRUE;

  // // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (mlconverter, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get video info!");
    return FALSE;
  }

  if (needpool) {
    GstStructure *config = NULL;
    GstAllocator *allocator = NULL;
    GstVideoAlignment align = {0,};

    if ((pool = gst_image_buffer_pool_new ()) == NULL) {
      GST_ERROR_OBJECT (mlconverter, "Failed to create image pool!");
      return FALSE;
    }

    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      allocator = gst_fd_allocator_new ();
      GST_INFO_OBJECT (mlconverter, "Buffer pool uses GBM memory");
    } else {
      allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
      GST_INFO_OBJECT (mlconverter, "Buffer pool uses DMA memory");
    }

    if (allocator == NULL) {
      GST_ERROR_OBJECT (mlconverter, "Failed to create allocator");
      gst_clear_object (&pool);
      return FALSE;
    }

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_set_allocator (config, allocator, NULL);
    g_object_unref (allocator);

    gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

    if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
      GST_ERROR_OBJECT (mlconverter, "Failed to get alignment!");
      gst_clear_object (&pool);
      return FALSE;
    }

    gst_buffer_pool_config_set_params (config, caps, info.size,
        DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &align);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_WARNING_OBJECT (mlconverter, "Failed to set pool configuration!");
      gst_clear_object (&pool);
      return FALSE;
    }
  }

  // Get the size from video info.
  size = GST_VIDEO_INFO_SIZE (&info);

  minbuffers = GST_ML_INFO_TENSOR_DIM_D (mlconverter->tensorlayout,
      mlconverter->mlinfo);

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, needpool ? pool : NULL, size,
        minbuffers, 0);

  if (pool != NULL)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (outquery, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_ml_video_converter_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (mlconverter, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (mlconverter->outpool)
    gst_object_unref (mlconverter->outpool);

  // Create a new pool in case none was proposed in the query.
  if (!(pool = gst_ml_video_converter_create_pool (mlconverter, caps))) {
    GST_ERROR_OBJECT (mlconverter, "Failed to create buffer pool!");
    return FALSE;
  }

  mlconverter->outpool = pool;

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);

  gst_query_add_allocation_meta (query, GST_ML_TENSOR_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_ml_video_converter_query (GstBaseTransform * base,
    GstPadDirection direction, GstQuery * query)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_writable_structure (query);

      // Not a supported custom event, pass it to the default handling function.
      if ((structure == NULL) ||
          !gst_structure_has_name (structure, "ml-preprocess-information"))
        break;

      gst_structure_set (structure, "stage-id", G_TYPE_UINT,
          mlconverter->stage_id, NULL);

      GST_DEBUG_OBJECT (mlconverter, "Stage ID %u", mlconverter->stage_id);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (base, direction, query);
}

static gboolean
gst_ml_video_converter_sink_event (GstBaseTransform * base, GstEvent * event)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      guint src_stage_id = 0;

      // Not a supported custom event, pass it to the default handling function.
      if ((structure == NULL) ||
          !gst_structure_has_name (structure, "ml-detection-information"))
        break;

      // Get the ID of the previous stage and update the ID of current stage.
      gst_structure_get_uint (structure, "stage-id", &src_stage_id);

      // Set the source stage ID if not explicitly set.
      g_array_append_val (mlconverter->roi_stage_ids, src_stage_id);
      GST_DEBUG_OBJECT (mlconverter, "Source Stage ID: %u", src_stage_id);

      // Pass to default handling function to propagate to the post-process.
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (base, event);
}

static GstCaps *
gst_ml_video_converter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *result = NULL, *intersection = NULL;
  GstPad *pad = NULL;
  const GValue *rate = NULL, *dims = NULL, *depth = NULL;
  gint idx = 0, length = 0;
  GstStructure *structure = NULL;

  GST_DEBUG_OBJECT (mlconverter, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (mlconverter, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SINK) {
    pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  } else {
    pad = GST_BASE_TRANSFORM_SINK_PAD (base);

    result = gst_pad_get_pad_template_caps (pad);

    // Try to negotiate precice video caps if engine is NONE.
    if (mlconverter->backend == GST_VCE_BACKEND_NONE) {
      GstCaps *videocaps = NULL;
      gint idx = 0, length = 0, maxwidth = 0, maxheight = 0;
      GstCaps *localcaps = gst_caps_copy (caps);

      // Removing non-fixated framerate field for caps translation
      if (!gst_caps_is_empty (localcaps)) {
        structure = gst_caps_get_structure (localcaps, 0);
        gst_structure_remove_field (structure, "rate");
      }

      videocaps = gst_ml_video_converter_translate_ml_caps (mlconverter, localcaps);
      length = gst_caps_get_size (videocaps);
      gst_caps_unref (localcaps);

      for (idx = 0; idx < length; idx++) {
        structure = gst_caps_get_structure (videocaps, idx);

        if (!gst_structure_has_field (structure, "width") &&
            !gst_structure_has_field (structure, "height"))
          continue;

        gst_structure_get_int (structure, "width", &maxwidth);
        gst_structure_get_int (structure, "height", &maxheight);

        gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, maxwidth,
            "height", GST_TYPE_INT_RANGE, 1, maxheight, NULL);

        if (mlconverter->disposition != GST_ML_VIDEO_DISPOSITION_STRETCH) {
          gst_structure_set (structure,
              "pixel-aspect-ratio", GST_TYPE_FRACTION , 1, 1, NULL);
        }
      }

      intersection = gst_caps_intersect_full (result, videocaps,
          GST_CAPS_INTERSECT_FIRST);

      gst_caps_unref (videocaps);
      gst_caps_unref (result);

      result = intersection;
    }
  }

  // Extract the framerate and propagate it to result caps.
  if (!gst_caps_is_empty (caps)) {
    structure = gst_caps_get_structure (caps, 0);

    rate = gst_structure_get_value (structure,
        (direction == GST_PAD_SRC) ? "rate" : "framerate");

    if (direction == GST_PAD_SRC) {
      dims = gst_structure_get_value (structure, "dimensions");

      if (dims != NULL && gst_value_is_fixed (dims)) {
        dims = gst_value_array_get_value (dims, 0);

        // NDHWC tensor format, extract the depth value.
        if (gst_value_array_get_size (dims) == 5)
          depth = gst_value_array_get_value (dims, 1);
      }
    }
  }

  result = gst_caps_make_writable (result);
  length = gst_caps_get_size (result);

  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (result, idx);

    if (rate != NULL) {
      gst_structure_set_value (structure,
          (direction == GST_PAD_SRC) ? "framerate" : "rate", rate);
    }

    if (depth != NULL && direction == GST_PAD_SRC) {
      const gchar *viewmode = gst_video_multiview_mode_to_caps_string (
          GST_VIDEO_MULTIVIEW_MODE_SEPARATED);

      gst_structure_set_value (structure, "views", depth);
      gst_structure_set (structure, "multiview-mode", G_TYPE_STRING, viewmode,
          NULL);
    }
  }

  if (filter != NULL) {
    intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_video_converter_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *mlcaps = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (mlconverter, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT " in direction %s",
      outcaps, incaps, (direction == GST_PAD_SINK) ? "sink" : "src");

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  mlcaps = gst_ml_video_converter_translate_video_caps (mlconverter, incaps);

  value = gst_structure_get_value (
    gst_caps_get_structure (outcaps, 0), "dimensions");

  if (NULL == value || !gst_value_is_fixed (value)) {
    value = gst_structure_get_value (
        gst_caps_get_structure (mlcaps, 0), "dimensions");
    gst_caps_set_value (outcaps, "dimensions", value);
  }

  value = gst_structure_get_value (
      gst_caps_get_structure (outcaps, 0), "type");

  if (NULL == value || !gst_value_is_fixed (value)) {
    value = gst_structure_get_value (
        gst_caps_get_structure (mlcaps, 0), "type");
    gst_caps_set_value (outcaps, "type", value);
  }

  gst_caps_unref (mlcaps);
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (mlconverter, "Fixated caps: %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static gboolean
gst_ml_video_converter_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *othercaps = NULL;
  GstVideoInfo ininfo, outinfo;
  GstMLInfo mlinfo;
  guint idx = 0, bpp = 0, padding = 0, n_bytes = 0, n_planes = 0, size = 0;
  gboolean passthrough = FALSE;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get input video info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&mlinfo, outcaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get output ML info from caps"
        " %" GST_PTR_FORMAT "!", outcaps);
    return FALSE;
  }

  // Get tensor layout based on tensor dimensions
  mlconverter->tensorlayout = gst_ml_info_get_layout (&mlinfo);

  othercaps = gst_ml_video_converter_translate_ml_caps (mlconverter, outcaps);
  othercaps = gst_caps_fixate (othercaps);

  if (!gst_video_info_from_caps (&outinfo, othercaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get output video info from caps %"
        GST_PTR_FORMAT "!", othercaps);
    gst_caps_unref (othercaps);
    return FALSE;
  }

  gst_caps_unref (othercaps);

  if ((mlconverter->tensorlayout.d != -1) &&
      GST_CONVERSION_MODE_IS_ROI (mlconverter->mode)) {
    GST_ERROR_OBJECT (mlconverter, "Tensors with depth are not allowed in ROI!");
    return FALSE;
  }

  // Get the number of bytes that represent a give ML type.
  n_bytes = gst_ml_type_get_size (mlinfo.type);

  n_planes = GST_VIDEO_FORMAT_INFO_N_PLANES (outinfo.finfo);

  for (idx = 0; idx < n_planes; idx++) {
    // Retrieve the Bytes Per Pixel in order to calculate the line padding.
    bpp = GST_VIDEO_INFO_COMP_PSTRIDE (&outinfo, idx);

    // For padding calculations use the video meta if present.
    padding = GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, idx) -
        (GST_VIDEO_INFO_WIDTH (&outinfo) * bpp);

    // Remove any padding from output video info as tensors require none.
    GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, idx) -= padding;
    // Adjust the stride for some tensor types.
    GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, idx) *= n_bytes;

    // Calculate the new updated size without padding
    GST_VIDEO_INFO_SIZE (&outinfo) -= padding *
        GST_VIDEO_INFO_COMP_HEIGHT (&outinfo, idx);

    // Update the offset to the next plane with adjusted size of current plane.
    if ((idx + 1) < n_planes) {
      size = GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, idx) *
          GST_VIDEO_INFO_COMP_HEIGHT (&outinfo, idx);

      GST_VIDEO_INFO_PLANE_OFFSET (&outinfo, (idx + 1)) =
          GST_VIDEO_INFO_PLANE_OFFSET (&outinfo, idx) + size;
    }
  }

  // Additionally adjust the total size depending on the ML type.
  GST_VIDEO_INFO_SIZE (&outinfo) *= n_bytes;

  // Additionally adjust the total size depending on the depth size.
  GST_VIDEO_INFO_SIZE (&outinfo) *= GST_ML_INFO_TENSOR_DIM_D (
      mlconverter->tensorlayout, &mlinfo);
  // Adjust height with the depth number of the tensor.
  GST_VIDEO_INFO_HEIGHT (&outinfo) *= GST_ML_INFO_TENSOR_DIM_D (
      mlconverter->tensorlayout, &mlinfo);

  // Additionally adjust the total size depending on the batch size.
  GST_VIDEO_INFO_SIZE (&outinfo) *= GST_ML_INFO_TENSOR_DIM_N (
      mlconverter->tensorlayout, &mlinfo);
  // Adjust height with the batch number of the tensor (1st dimension).
  GST_VIDEO_INFO_HEIGHT (&outinfo) *= GST_ML_INFO_TENSOR_DIM_N (
      mlconverter->tensorlayout, &mlinfo);

  passthrough =
      GST_VIDEO_INFO_SIZE (&ininfo) == GST_VIDEO_INFO_SIZE (&outinfo) &&
      GST_VIDEO_INFO_WIDTH (&ininfo) == GST_VIDEO_INFO_WIDTH (&outinfo) &&
      GST_VIDEO_INFO_HEIGHT (&ininfo) == GST_VIDEO_INFO_HEIGHT (&outinfo) &&
      GST_VIDEO_INFO_FORMAT (&ininfo) == GST_VIDEO_INFO_FORMAT (&outinfo);

  gst_base_transform_set_passthrough (base, passthrough);
  gst_base_transform_set_in_place (base, FALSE);

  if (mlconverter->ininfo != NULL)
    gst_video_info_free (mlconverter->ininfo);
  if (mlconverter->vinfo != NULL)
    gst_video_info_free (mlconverter->vinfo);
  if (mlconverter->mlinfo != NULL)
    gst_ml_info_free (mlconverter->mlinfo);

  mlconverter->ininfo = gst_video_info_copy (&ininfo);
  mlconverter->vinfo = gst_video_info_copy (&outinfo);
  mlconverter->mlinfo = gst_ml_info_copy (&mlinfo);

  // Initialize video converter engine.
  if (mlconverter->converter != NULL)
    gst_video_converter_engine_free (mlconverter->converter);

  mlconverter->converter =
      gst_video_converter_engine_new (mlconverter->backend, NULL);

  // Initialize converter composition which will be reused for each conversion.
  mlconverter->composition.n_blits = GST_ML_INFO_TENSOR_DIM_D (
      mlconverter->tensorlayout, &mlinfo);
  mlconverter->composition.n_blits *= GST_ML_INFO_TENSOR_DIM_N (
      mlconverter->tensorlayout, &mlinfo);

  mlconverter->composition.blits =
      g_new0 (GstVideoBlit, mlconverter->composition.n_blits);

  for (idx = 0; idx < mlconverter->composition.n_blits; idx++) {
    GstVideoBlit *blit = &(mlconverter->composition.blits[idx]);

    blit->frame = g_slice_new0 (GstVideoFrame);
    blit->mask = 0;

    blit->alpha = G_MAXUINT8;
    blit->rotate = GST_VCE_ROTATE_0;
  }

  mlconverter->composition.frame = g_slice_new0 (GstVideoFrame);
  mlconverter->composition.datatype = 0;

  mlconverter->composition.bgcolor = 0x00000000;
  mlconverter->composition.bgfill = TRUE;

  if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_INT64)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_I64;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_UINT64)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_U64;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_INT32)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_I32;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_UINT32)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_U32;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_INT16)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_I16;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_UINT16)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_U16;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_FLOAT16)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_F16;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_FLOAT32)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_F32;
  else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_INT8)
    mlconverter->composition.datatype |= GST_VCE_DATA_TYPE_I8;

  for (idx = 0; idx < GST_VCE_MAX_CHANNELS; idx++) {
    mlconverter->composition.offsets[idx] = (idx < mlconverter->mean->len) ?
        g_array_index (mlconverter->mean, gdouble, idx) : DEFAULT_PROP_MEAN;
    mlconverter->composition.scales[idx] = (idx < mlconverter->sigma->len) ?
        g_array_index (mlconverter->sigma, gdouble, idx) : DEFAULT_PROP_SIGMA;
  }

  GST_DEBUG_OBJECT (mlconverter, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (mlconverter, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static gboolean
gst_ml_video_converter_start (GstBaseTransform * base)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  GST_INFO_OBJECT (mlconverter, "Initiate processing");
  return TRUE;
}

static gboolean
gst_ml_video_converter_stop (GstBaseTransform * base)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  mlconverter->seq_idx = 0;
  mlconverter->n_seq_entries = 0;

  mlconverter->batch_idx = 0;
  mlconverter->depth_idx = 0;

  mlconverter->next_roi_id = -1;
  mlconverter->next_mem_idx = -1;

  g_queue_clear_full (mlconverter->bufqueue, (GDestroyNotify) gst_buffer_unref);

  GST_INFO_OBJECT (mlconverter, "All processing has been stopped");
  return TRUE;
}

static GstFlowReturn
gst_ml_video_converter_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GQueue *bufqueue = mlconverter->bufqueue;
  GstBufferPool *pool = mlconverter->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_TRACE_OBJECT (mlconverter, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP and no previous buffers. Create a GAP output buffer.
  if (g_queue_is_empty (bufqueue) && (gst_buffer_get_size (inbuffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  // Mode is one of the ROI modes and there are no previous buffers.
  // Check whether there are ROI metas suitable for processing.
  if ((*outbuffer == NULL) && g_queue_is_empty (bufqueue) &&
      GST_CONVERSION_MODE_IS_ROI (mlconverter->mode)) {
    GstVideoRegionOfInterestMeta *roimeta = NULL;
    gpointer state = NULL;

    roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state);

    // Check if there is atleast one suitable meta.
    while ((roimeta != NULL) &&
           !gst_region_of_interest_is_valid (roimeta, mlconverter->roi_stage_ids))
      roimeta = GST_BUFFER_ITERATE_ROI_METAS (inbuffer, state);

    if (roimeta == NULL)
      *outbuffer = gst_buffer_new ();
  }

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (mlconverter, "Failed to acquire output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // If the output buffer is an empty shell, setup flags and additiona batch metas.
  if (gst_buffer_get_size (*outbuffer) == 0) {
    GstStructure *structure = NULL;

    if (GST_VIDEO_INFO_MULTIVIEW_MODE (mlconverter->ininfo) ==
            GST_VIDEO_MULTIVIEW_MODE_SEPARATED) {
      GstMeta *meta = NULL;
      gpointer state = NULL;
      guint idx = 0;

      // Muxed streams, attach protection meta for each of muxed streams.
      while ((meta = gst_buffer_iterate_meta_filtered (inbuffer, &state,
                  GST_PROTECTION_META_API_TYPE))) {
        GstProtectionMeta *pmeta = GST_PROTECTION_META_CAST (meta);
        GstClockTime timestamp = GST_CLOCK_TIME_NONE;
        guint stream_id = 0;

        sscanf (gst_structure_get_name (pmeta->info), "mux-stream-%2u", &stream_id);
        gst_structure_get_uint64 (pmeta->info, "timestamp", &timestamp);

        structure = gst_structure_new (gst_batch_channel_name (idx++),
            "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (inbuffer),
            "sequence-index", G_TYPE_UINT, 1,
            "sequence-num-entries", G_TYPE_UINT,  1,
            "stream-id", G_TYPE_INT, stream_id,
            "stream-timestamp", G_TYPE_UINT64, timestamp, NULL);

        gst_buffer_add_protection_meta (*outbuffer, structure);
      }
    } else {
      // Non-muxed stream, attach a single protection meta
      structure = gst_structure_new (gst_batch_channel_name (0),
          "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (inbuffer),
          "sequence-index", G_TYPE_UINT, 1,
          "sequence-num-entries",G_TYPE_UINT, 1, NULL);

      gst_buffer_add_protection_meta (*outbuffer, structure);
    }

    GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ml_video_converter_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstVideoFrame *inframe = NULL, *outframe = NULL;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  guint n_blits = 0;
  gboolean success = TRUE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  if (!gst_ml_video_converter_prepare_buffer_queues (mlconverter, inbuffer)) {
    GST_TRACE_OBJECT (mlconverter, "Internal buffer queues not yet ready");
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  }

  time = gst_util_get_timestamp ();
  success = gst_ml_video_converter_setup_composition (mlconverter, outbuffer);

  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to setup composition!");
    return GST_FLOW_ERROR;
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (mlconverter, "DMA IOCTL SYNC START failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  n_blits = mlconverter->composition.n_blits;
  inframe = mlconverter->composition.blits[0].frame;
  outframe = mlconverter->composition.frame;

  // Perform transformation only when custom normalization coefficients are set,
  // when there are multiple blit elements (buffers), or when there is only a
  // single blit element which does not have the required parameters for output.
  if (mlconverter->backend != GST_VCE_BACKEND_NONE &&
      ((n_blits > 1) || is_conversion_required (inframe, outframe) ||
          ((mlconverter->mean->len != 0) && (mlconverter->sigma->len != 0)))) {
    success = gst_video_converter_engine_compose (mlconverter->converter,
        &(mlconverter->composition), 1, NULL);
  } else {
    // There is not need for frame conversion, apply only normalization.
    success = gst_ml_video_converter_normalize (mlconverter,
        &(mlconverter->composition.blits[0]), outframe);
  }

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING_OBJECT (mlconverter, "DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H

  gst_ml_video_converter_cleanup_composition (mlconverter);
  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to process buffers!");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (mlconverter, "Conversion took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  switch (prop_id) {
    case PROP_CONVERSION_MODE:
      mlconverter->mode = g_value_get_enum (value);
      break;
    case PROP_ENGINE_BACKEND:
      mlconverter->backend = g_value_get_enum (value);
      break;
    case PROP_IMAGE_DISPOSITION:
      mlconverter->disposition = g_value_get_enum (value);
      break;
    case PROP_SUBPIXEL_LAYOUT:
      mlconverter->pixlayout = g_value_get_enum (value);
      break;
    case PROP_MEAN:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        gdouble val = g_value_get_double (gst_value_array_get_value (value, idx));
        g_array_append_val (mlconverter->mean, val);
      }
      break;
    }
    case PROP_SIGMA:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        gdouble val = g_value_get_double (gst_value_array_get_value (value, idx));
        g_array_append_val (mlconverter->sigma, val);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  switch (prop_id) {
    case PROP_CONVERSION_MODE:
      g_value_set_enum (value, mlconverter->mode);
      break;
    case PROP_ENGINE_BACKEND:
      g_value_set_enum (value, mlconverter->backend);
      break;
    case PROP_IMAGE_DISPOSITION:
      g_value_set_enum (value, mlconverter->disposition);
      break;
    case PROP_SUBPIXEL_LAYOUT:
      g_value_set_enum (value, mlconverter->pixlayout);
      break;
    case PROP_MEAN:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_DOUBLE);

      for (idx = 0; idx < mlconverter->mean->len; idx++) {
        g_value_set_double (&val, g_array_index (mlconverter->mean, gdouble, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    case PROP_SIGMA:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_DOUBLE);

      for (idx = 0; idx < mlconverter->sigma->len; idx++) {
        g_value_set_double (&val, g_array_index (mlconverter->sigma, gdouble, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_converter_finalize (GObject * object)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);
  guint idx = 0;

  g_queue_free_full (mlconverter->bufqueue, (GDestroyNotify) gst_buffer_unref);

  if (mlconverter->sigma != NULL)
    g_array_free (mlconverter->sigma, TRUE);

  if (mlconverter->mean != NULL)
    g_array_free (mlconverter->mean, TRUE);

  for (idx = 0; idx < mlconverter->composition.n_blits; idx++)
    g_slice_free (GstVideoFrame, mlconverter->composition.blits[idx].frame);

  g_free (mlconverter->composition.blits);
  g_slice_free (GstVideoFrame, mlconverter->composition.frame);

  if (mlconverter->converter != NULL)
    gst_video_converter_engine_free (mlconverter->converter);

  if (mlconverter->mlinfo != NULL)
    gst_ml_info_free (mlconverter->mlinfo);

  if (mlconverter->vinfo != NULL)
    gst_video_info_free (mlconverter->vinfo);

  if (mlconverter->ininfo != NULL)
    gst_video_info_free (mlconverter->ininfo);

  if (mlconverter->outpool != NULL)
    gst_object_unref (mlconverter->outpool);

  if (mlconverter->roi_stage_ids != NULL)
    g_array_free (mlconverter->roi_stage_ids, TRUE);

  gst_ml_stage_unregister_unique_index (mlconverter->stage_id);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (mlconverter));
}

static void
gst_ml_video_converter_class_init (GstMLVideoConverterClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_converter_debug, "qtimlvconverter", 0,
      "QTI ML video converter plugin");

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_video_converter_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_video_converter_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_converter_finalize);

  g_object_class_install_property (gobject, PROP_CONVERSION_MODE,
      g_param_spec_enum ("mode", "Mode", "Conversion mode",
          GST_TYPE_ML_CONVERSION_MODE, DEFAULT_PROP_CONVERSION_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_ENGINE_BACKEND,
      g_param_spec_enum ("engine", "Engine",
          "Engine backend used for the conversion operations",
          GST_TYPE_VCE_BACKEND, DEFAULT_PROP_ENGINE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_IMAGE_DISPOSITION,
      g_param_spec_enum ("image-disposition", "Image Disposition",
          "Aspect Ratio and placement of the image inside the output tensor",
          GST_TYPE_ML_VIDEO_DISPOSITION, DEFAULT_PROP_IMAGE_DISPOSITION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_SUBPIXEL_LAYOUT,
      g_param_spec_enum ("subpixel-layout", "Subpixel Layout",
          "Arrangement of the image pixels insize the output tensor",
          GST_TYPE_ML_VIDEO_PIXEL_LAYOUT, DEFAULT_PROP_SUBPIXEL_LAYOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_MEAN,
     gst_param_spec_array ("mean", "Mean Subtraction",
          "Channels mean subtraction values for FLOAT tensors "
          "('<R, G, B>', '<R, G, B, A>', '<G>')",
          g_param_spec_double ("value", "Mean Value",
              "One of B, G or R value.", 0.0, 255.0, DEFAULT_PROP_MEAN,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_SIGMA,
     gst_param_spec_array ("sigma", "Sigma Values",
          "Channel divisor values for FLOAT tensors "
          "('<R, G, B>', '<R, G, B, A>', '<G>')",
          g_param_spec_double ("value", "Sigma Value",
              "One of B, G or R value.", 0.0, 255.0, DEFAULT_PROP_SIGMA,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning Video Converter", "Filter/Video/Scaler",
      "Parse an video streams into a ML stream", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_converter_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_converter_src_template ());

  base->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_propose_allocation);
  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_decide_allocation);

  base->query = GST_DEBUG_FUNCPTR (gst_ml_video_converter_query);
  base->sink_event = GST_DEBUG_FUNCPTR (gst_ml_video_converter_sink_event);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_video_converter_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_converter_set_caps);

  base->start = GST_DEBUG_FUNCPTR (gst_ml_video_converter_start);
  base->stop = GST_DEBUG_FUNCPTR (gst_ml_video_converter_stop);

  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_prepare_output_buffer);
  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_converter_transform);
}

static void
gst_ml_video_converter_init (GstMLVideoConverter * mlconverter)
{
  mlconverter->ininfo = NULL;

  mlconverter->vinfo = NULL;
  mlconverter->mlinfo = NULL;

  mlconverter->stage_id = gst_ml_stage_get_unique_index ();
  mlconverter->roi_stage_ids = g_array_new (FALSE, FALSE, sizeof (guint));

  mlconverter->outpool = NULL;

  mlconverter->bufqueue = g_queue_new ();

  mlconverter->seq_idx = 0;
  mlconverter->n_seq_entries = 0;

  mlconverter->batch_idx = 0;
  mlconverter->depth_idx = 0;

  mlconverter->next_roi_id = -1;
  mlconverter->next_mem_idx = -1;

  mlconverter->tensorlayout = GST_ML_TENSOR_LAYOUT_NHWC;

  mlconverter->converter = NULL;

  mlconverter->backend = DEFAULT_PROP_ENGINE_BACKEND;
  mlconverter->disposition = DEFAULT_PROP_IMAGE_DISPOSITION;
  mlconverter->pixlayout = DEFAULT_PROP_SUBPIXEL_LAYOUT;
  mlconverter->mean = g_array_new (FALSE, FALSE, sizeof (gdouble));
  mlconverter->sigma = g_array_new (FALSE, FALSE, sizeof (gdouble));

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (mlconverter), TRUE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvconverter", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_CONVERTER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvconverter,
    "QTI Machine Learning plugin for converting video stream into ML stream",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
