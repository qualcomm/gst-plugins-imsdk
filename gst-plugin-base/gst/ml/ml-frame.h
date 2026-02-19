/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_ML_FRAME_H__
#define __GST_ML_FRAME_H__

#include <gst/ml/ml-type.h>
#include <gst/ml/ml-info.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_FRAME           (gst_ml_frame_get_type())
GST_API GType gst_ml_frame_get_type (void);

typedef struct _GstMLFrame GstMLFrame;
typedef struct _GstMLTensor GstMLTensor;

/**
 * GstMLFrame:
 * @info: A #GstMLInfo
 * @buffer: Mapped buffer containing the tensor memory blocks
 * @mapinfo: (array fixed-size=GST_ML_MAX_TENSORS) (element-type GstMapInfo):
 *           Mappings of the tensor memory blocks
 * @refcount: Reference counter
 *
 * A ML frame obtained either from gst_ml_frame_new() or locally instaciated
 * and populated from gst_ml_frame_map()
 */
struct _GstMLFrame {
  GstMLInfo  info;

  GstBuffer  *buffer;

  GstMapInfo mapinfo[GST_ML_MAX_TENSORS];

  /*< private >*/
  gatomicrefcount refcount;
};

/**
 * GstMLTensor:
 * @type: A #GstMLType
 * @n_dimensions: Number of tensor dimensions
 * @dimensions: Dimension values
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
 * gst_ml_frame_new: (constructor)
 *
 * Allocate a new #GstMLFrame that is also zero initialized.
 *
 * Returns: (transfer full): A new #GstMLFrame.
 */
GST_API GstMLFrame*
gst_ml_frame_new (void);

/**
 * gst_ml_frame_ref:
 * @frame: A #GstMLFrame
 *
 * Atomically increments the reference count of @frame by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer full): The passed in `GstMLFrame`
 */
GST_API GstMLFrame *
gst_ml_frame_ref (GstMLFrame * frame);

/**
 * gst_ml_frame_unref:
 * @frame: (transfer full):  A #GstMLFrame
 *
 * Atomically decrements the reference count of @frame by one. If the
 * reference count drops to 0, free a GstMLFrame structure previously
 * allocated with gst_ml_frame_new().
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_frame_unref (GstMLFrame * frame);

/**
 * gst_ml_frame_map:
 * @frame: (out): Pointer to #GstMLFrame
 * @info: A #GstMLInfo
 * @buffer: The #GstBuffer to map
 * @flags: #GstMapFlags
 *
 * Use info and buffer to fill in the values of frame.
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

#define GST_ML_FRAME_TYPE(f)           (GST_ML_INFO_TYPE(&(f)->info))
#define GST_ML_FRAME_N_TENSORS(f)      (GST_ML_INFO_N_TENSORS(&(f)->info))
#define GST_ML_FRAME_TENSOR_SIZE(f,n)  (gst_ml_info_tensor_size (&(f)->info,n))
#define GST_ML_FRAME_N_DIMENSIONS(f,n) (GST_ML_INFO_N_DIMENSIONS(&(f)->info,n))
#define GST_ML_FRAME_DIM(f,n,m)        (GST_ML_INFO_TENSOR_DIM(&(f)->info,n,m))

#define GST_ML_FRAME_N_BLOCKS(f)       (gst_buffer_n_memory ((f)->buffer))
#define GST_ML_FRAME_BLOCK_DATA(f,n)   ((f)->mapinfo[n].data)
#define GST_ML_FRAME_BLOCK_SIZE(f,n)   ((f)->mapinfo[n].size)

G_END_DECLS

#endif /* __GST_ML_FRAME_H__ */
