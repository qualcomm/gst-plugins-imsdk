/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ALLOCATOR_H__
#define __GST_QTI_ALLOCATOR_H__

#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS

#define GST_TYPE_QTI_ALLOCATOR (gst_qti_allocator_get_type ())
#define GST_QTI_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QTI_ALLOCATOR, \
      GstQtiAllocator))
#define GST_IS_QTI_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QTI_ALLOCATOR))
#define GST_QTI_ALLOCATOR_CAST(obj) ((GstQtiAllocator*)(obj))
#define GST_QTI_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QTI_ALLOCATOR, \
      GstQtiAllocatorClass))
#define GST_IS_QTI_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QTI_ALLOCATOR))
#define GST_QTI_ALLOCATOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QTI_ALLOCATOR, \
      GstQtiAllocatorClass))

#define DMA_HEAP_SYSTEM "/dev/dma_heap/system"

typedef struct _GstQtiAllocator GstQtiAllocator;
typedef struct _GstQtiAllocatorClass GstQtiAllocatorClass;
typedef struct _GstQtiAllocatorPrivate GstQtiAllocatorPrivate;

struct _GstQtiAllocator
{
  GstFdAllocator parent;

  GstQtiAllocatorPrivate *priv;
};

struct _GstQtiAllocatorClass
{
  GstFdAllocatorClass parent;
};

GST_EXPORT
GType          gst_qti_allocator_get_type (void) G_GNUC_CONST;

GST_EXPORT
GstAllocator * gst_qti_allocator_new (const gchar * dma_heap_name);

GST_EXPORT
void           gst_qti_allocator_start (GstQtiAllocator * qtiallocator, guint max_memory_blocks);

GST_EXPORT
gboolean       gst_qti_allocator_stop (GstQtiAllocator * qtiallocator);

G_END_DECLS

#endif /* __GST_QTI_ALLOCATOR_H__ */
