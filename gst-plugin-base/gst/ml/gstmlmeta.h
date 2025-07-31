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
*/

#ifndef __GST_ML_META_H__
#define __GST_ML_META_H__

#include <gst/gst.h>
#include <gst/ml/ml-type.h>

G_BEGIN_DECLS

#define GST_ML_TENSOR_META_API_TYPE (gst_ml_tensor_meta_api_get_type())
#define GST_ML_TENSOR_META_INFO  (gst_ml_tensor_meta_get_info())

#define GST_ML_TENSOR_META_CAST(obj) ((GstMLTensorMeta *) obj)

typedef struct _GstMLTensorMeta GstMLTensorMeta;

/**
 * GstMLTensorMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @type: Tensor type
 * @n_dimensions: Number of tensor dimensions
 * @dimensions: Tensor dimensions values
 *
 * Extra buffer metadata describing ML tensor properties
 */
struct _GstMLTensorMeta {
  GstMeta   meta;

  guint     id;

  // Tensor parameters.
  GstMLType type;
  guint     n_dimensions;
  guint     dimensions[GST_ML_TENSOR_MAX_DIMS];

  // Dequantization parameters.
  gfloat    qscale;
  gfloat    qoffset;
};

GST_API
GType gst_ml_tensor_meta_api_get_type (void);

GST_API
const GstMetaInfo * gst_ml_tensor_meta_get_info (void);

GST_API
GstMLTensorMeta * gst_buffer_add_ml_tensor_meta (GstBuffer * buffer,
    const GstMLType type, const guint n_dimensions,
    const guint dimensions[GST_ML_TENSOR_MAX_DIMS]);

GST_API
GstMLTensorMeta * gst_buffer_get_ml_tensor_meta (GstBuffer * buffer);

GST_API
GstMLTensorMeta * gst_buffer_get_ml_tensor_meta_id (GstBuffer * buffer,
                                                    guint id);

GST_API
gsize gst_ml_meta_tensor_size  (const GstMLTensorMeta * meta);

G_END_DECLS

#endif /* __GST_ML_META_H__ */
