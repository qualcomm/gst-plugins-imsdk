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
 * _GstMLFrame:
 * @info: The #GstMLInfo
 * @buffer: Mapped buffer containing the tensor memory blocks
 * @map: Mappings of the tensor memory blocks
 *
 * A ML frame obtained from gst_ml_frame_map()
 */
struct _GstMLFrame {
  GstMLInfo  info;

  GstBuffer  *buffer;

  GstMapInfo map[GST_ML_MAX_TENSORS];
};

GST_API
gboolean  gst_ml_frame_map   (GstMLFrame * frame, const GstMLInfo * info,
                              GstBuffer * buffer, GstMapFlags flags);

GST_API
void      gst_ml_frame_unmap (GstMLFrame * frame);


#define GST_ML_FRAME_TYPE(f)           (GST_ML_INFO_TYPE(&(f)->info))
#define GST_ML_FRAME_N_TENSORS(f)      (GST_ML_INFO_N_TENSORS(&(f)->info))
#define GST_ML_FRAME_N_DIMENSIONS(f,n) (GST_ML_INFO_N_DIMENSIONS(&(f)->info,n))
#define GST_ML_FRAME_DIM(f,n,m)        (GST_ML_INFO_TENSOR_DIM(&(f)->info,n,m))

#define GST_ML_FRAME_N_BLOCKS(f)       (gst_buffer_n_memory ((f)->buffer))
#define GST_ML_FRAME_BLOCK_DATA(f,n)   ((f)->map[n].data)
#define GST_ML_FRAME_BLOCK_SIZE(f,n)   ((f)->map[n].size)

G_END_DECLS

#endif /* __GST_ML_FRAME_H__ */
