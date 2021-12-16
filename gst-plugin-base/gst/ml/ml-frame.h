/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#define GST_ML_FRAME_N_BLOCKS(f)       (gst_buffer_n_memory ((f)->buffer))
#define GST_ML_FRAME_BLOCK_DATA(f,n)   ((f)->map[n].data)
#define GST_ML_FRAME_BLOCK_SIZE(f,n)   ((f)->map[n].size)

G_END_DECLS

#endif /* __GST_ML_FRAME_H__ */
