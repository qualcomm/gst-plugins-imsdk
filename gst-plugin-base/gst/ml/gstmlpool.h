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

#ifndef __GST_ML_POOL_H__
#define __GST_ML_POOL_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>
#include <gst/ml/ml-info.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_POOL (gst_ml_buffer_pool_get_type ())
#define GST_ML_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ML_POOL, GstMLBufferPool))
#define GST_ML_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ML_POOL, GstMLBufferPoolClass))
#define GST_IS_ML_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ML_POOL))
#define GST_IS_ML_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ML_POOL))
#define GST_ML_POOL_CAST(obj) ((GstMLBufferPool*)(obj))

/**
 * GST_ML_BUFFER_POOL_TYPE_ION:
 *
 * Type of memory that the pool will use for allocating buffers.
 */
#define GST_ML_BUFFER_POOL_TYPE_ION "GstMlBufferPoolTypeIonMemory"

/**
 * GST_ML_BUFFER_POOL_TYPE_SYSTEM:
 *
 * Type of memory that the pool will use for allocating buffers.
 */
#define GST_ML_BUFFER_POOL_TYPE_SYSTEM "GstMlBufferPoolTypeSystemMemory"

typedef struct _GstMLBufferPool GstMLBufferPool;
typedef struct _GstMLBufferPoolClass GstMLBufferPoolClass;
typedef struct _GstMLBufferPoolPrivate GstMLBufferPoolPrivate;

/**
 * GST_ML_BUFFER_POOL_OPTION_TENSOR_META:
 *
 * An option that can be activated on bufferpool to request ML tensor metadata
 * on buffers from the pool.
 */
#define GST_ML_BUFFER_POOL_OPTION_TENSOR_META "GstBufferPoolOptionMLTensorMeta"

struct _GstMLBufferPool
{
  GstBufferPool parent;

  GstMLBufferPoolPrivate *priv;
};

struct _GstMLBufferPoolClass
{
  GstBufferPoolClass parent;
};

GST_API
GType             gst_ml_buffer_pool_get_type (void);

GST_API
GstBufferPool *   gst_ml_buffer_pool_new      (const gchar * memtype);

GST_API
const GstMLInfo * gst_ml_buffer_pool_get_info (GstBufferPool * pool);

G_END_DECLS

#endif /* __GST_ML_POOL_H__ */
