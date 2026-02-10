/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_ML_FRAME_H__
#define __GST_ML_FRAME_H__

#include <gst/ml/ml-type.h>
#include <gst/ml/ml-info.h>

G_BEGIN_DECLS

typedef struct _GstMLFrame GstMLFrame;

/**
 * GstMLFrame:
 * @info: A #GstMLInfo
 * @buffer: Mapped buffer containing the tensor memory blocks
 * @mapinfo: (array fixed-size=GST_ML_MAX_TENSORS) (element-type Gst.MapInfo):
 *           Mappings of the tensor memory blocks
 *
 * A ML frame obtained from gst_ml_frame_map()
 */
struct _GstMLFrame {
  GstMLInfo  info;

  GstBuffer  *buffer;

  GstMapInfo mapinfo[GST_ML_MAX_TENSORS];
};

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
