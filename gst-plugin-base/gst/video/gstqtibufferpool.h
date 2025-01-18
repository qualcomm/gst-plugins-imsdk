/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_BUFFER_POOL_H__
#define __GST_QTI_BUFFER_POOL_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

#define GST_TYPE_QTI_BUFFER_POOL \
  (gst_qti_buffer_pool_get_type ())
#define GST_QTI_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QTI_BUFFER_POOL, \
      GstQtiBufferPool))
#define GST_QTI_BUFFER_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QTI_BUFFER_POOL, \
      GstQtiBufferPoolClass))
#define GST_IS_QTI_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QTI_BUFFER_POOL))
#define GST_IS_QTI_BUFFER_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QTI_BUFFER_POOL))
#define GST_QTI_BUFFER_POOL_CAST(obj) ((GstQtiBufferPool *) (obj))

typedef struct _GstQtiBufferPool GstQtiBufferPool;
typedef struct _GstQtiBufferPoolClass GstQtiBufferPoolClass;
typedef struct _GstQtiBufferPoolPrivate GstQtiBufferPoolPrivate;

struct _GstQtiBufferPool
{
  GstVideoBufferPool parent;

  GstQtiBufferPoolPrivate *priv;
};

struct _GstQtiBufferPoolClass
{
  GstVideoBufferPoolClass parent;
};

GType gst_qti_buffer_pool_get_type (void);

GstBufferPool * gst_qti_buffer_pool_new ();

G_END_DECLS

#endif /* __GST_QTI_BUFFER_POOL_H__ */
