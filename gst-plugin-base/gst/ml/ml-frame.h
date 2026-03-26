/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_ML_FRAME_H__
#define __GST_ML_FRAME_H__

#include <gst/ml/ml-type.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/gstmlmeta.h>

G_BEGIN_DECLS

#define GST_ML_FRAME_TYPE(f)           (GST_ML_INFO_TYPE(&(f)->info))
#define GST_ML_FRAME_N_TENSORS(f)      (GST_ML_INFO_N_TENSORS(&(f)->info))
#define GST_ML_FRAME_TENSOR_SIZE(f,n)  (gst_ml_info_tensor_size (&(f)->info,n))
#define GST_ML_FRAME_N_DIMENSIONS(f,n) (GST_ML_INFO_N_DIMENSIONS(&(f)->info,n))
#define GST_ML_FRAME_DIM(f,n,m)        (GST_ML_INFO_TENSOR_DIM(&(f)->info,n,m))

#define GST_ML_FRAME_N_BLOCKS(f)       (gst_buffer_n_memory ((f)->buffer))
#define GST_ML_FRAME_BLOCK_DATA(f,n)   ((f)->mapinfo[n].data)
#define GST_ML_FRAME_BLOCK_SIZE(f,n)   ((f)->mapinfo[n].size)

#define GST_TYPE_ML_FRAME           (gst_ml_frame_get_type())
GST_API GType gst_ml_frame_get_type (void);

typedef struct _GstMLFrame GstMLFrame;
typedef struct _GstMLTensor GstMLTensor;

/**
 * GstMLFrame:
 * @info: A #GstMLInfo
 * @buffer: Mapped #GstBuffer containing the tensor memory blocks
 * @mapinfo: (array fixed-size=GST_ML_MAX_TENSORS) (element-type GstMapInfo):
 *           Mappings of the tensor memory blocks
 *
 * A ML frame obtained either from gst_ml_frame_new() or locally instaciated
 * and populated from gst_ml_frame_map()
 */
struct _GstMLFrame {
  GstMLInfo  info;

  GstBuffer  *buffer;

  GstMapInfo mapinfo[GST_ML_MAX_TENSORS];
};

/**
 * GstMLTensor:
 * @type: A #GstMLType
 * @n_dimensions: Number of tensor dimensions
 * @dimensions: (array fixed-size=GST_ML_TENSOR_MAX_DIMS) (element-type guint):
 *              Dimension values
 * @data: (array length=size): Mapped tensor data
 * @size: Size of @data in bytes
 *
 * Information describing tensor.
 */
struct _GstMLTensor {
  GstMLType type;

  guint     n_dimensions;
  guint     dimensions[GST_ML_TENSOR_MAX_DIMS];

  guint8    *data;
  gsize     size;
};

/**
 * gst_ml_frame_map:
 * @frame: (out): Pointer to #GstMLFrame
 * @info: (nullable): A #GstMLInfo
 * @buffer: (transfer none): The #GstBuffer for mapping.
 * @flags: #GstMapFlags
 *
 * Use optional ML info and buffer to fill in the values of frame.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_frame_map (GstMLFrame * frame, const GstMLInfo * info,
                  GstBuffer * buffer, GstMapFlags flags);

/**
 * gst_ml_frame_unmap:
 * @frame: A #GstMLFrame
 *
 * Unmap the memory previously mapped with gst_ml_frame_map().
 */
GST_API void
gst_ml_frame_unmap (GstMLFrame * frame);

/**
 * gst_ml_frame_get_tensor: (skip)
 * @frame: A #GstMLFrame
 * @index: Tensor index
 *
 * Get wrapper structure for easier access to the Tensor data and parameters.
 *
 * Returns: (transfer full): A #GstMLTensor
 */
GST_API GstMLTensor
gst_ml_frame_get_tensor (GstMLFrame * frame, guint index);

G_END_DECLS

#endif /* __GST_ML_FRAME_H__ */
