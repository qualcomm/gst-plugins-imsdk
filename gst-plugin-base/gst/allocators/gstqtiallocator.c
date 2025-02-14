/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqtiallocator.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef HAVE_LINUX_DMA_HEAP_H
#include <linux/dma-heap.h>
#else
#include <linux/ion.h>
#include <linux/msm_ion.h>
#endif // HAVE_LINUX_DMA_HEAP_H

#define DEFAULT_QUEUE_SIZE 8

GST_DEBUG_CATEGORY_STATIC (gst_qtiallocator_debug);
#define GST_CAT_DEFAULT gst_qtiallocator_debug

/**
 * GST_QTI_ALLOCATOR_DMA_QCOM_HEAP_SYSTEM:
 *
 * Memory will be allocated from the system DMA heap.
 */
#define GST_QTI_ALLOCATOR_DMA_QCOM_HEAP_SYSTEM "/dev/dma_heap/qcom,system"

enum
{
  PROP_0,
  PROP_DMA_HEAP
};

struct _GstQtiAllocatorPrivate
{
  // DMA device FD
  gint           devfd;

  GstAtomicQueue *mem_queue;
  GstPoll        *poll;
  guint          n_allocated_memory;
  guint          max_memory_blocks;
  gboolean       do_free;
};

#define parent_class gst_qti_allocator_parent_class

G_DEFINE_TYPE_WITH_PRIVATE (GstQtiAllocator, gst_qti_allocator,
    GST_TYPE_FD_ALLOCATOR);

static gboolean
gst_qti_allocator_memory_dispose (GstMiniObject * obj)
{
  GstMemory *mem = GST_MEMORY_CAST (obj);
  GstQtiAllocator *qtiallocator = (GstQtiAllocator *) mem->allocator;
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;

  if (priv->mem_queue && !priv->do_free) {
    GST_DEBUG_OBJECT (qtiallocator, "Enqueue memory %p back to free queue", mem);

    gst_atomic_queue_push (priv->mem_queue, gst_memory_ref (mem));
    gst_poll_write_control (priv->poll);
    return FALSE;
  }

  return TRUE;
}

static GstMemory *
gst_qti_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstQtiAllocator *qtiallocator = GST_QTI_ALLOCATOR (allocator);
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;
  GstMemory *mem = NULL;
  GstMapInfo map_info;
  gint res = 0;

  while (priv->mem_queue) {
    mem = gst_atomic_queue_pop (priv->mem_queue);
    if (G_LIKELY (mem)) {
      while (!gst_poll_read_control (priv->poll) &&
          (errno == EWOULDBLOCK || errno == EAGAIN))
        g_thread_yield ();

      GST_LOG_OBJECT (qtiallocator, "Reusing preallocated memory %p", mem);
      break;
    } else {
      if (priv->max_memory_blocks &&
          priv->n_allocated_memory >= priv->max_memory_blocks) {
        GST_LOG_OBJECT (qtiallocator, "Wait for free memory");
        if (!gst_poll_read_control (priv->poll)) {
          if (errno == EWOULDBLOCK || errno == EAGAIN) {
            gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
          } else {
            GST_ERROR_OBJECT (qtiallocator, "critical error");
            return NULL;
          }
        } else {
          gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
          gst_poll_write_control (priv->poll);
        }
      } else {
        break;
      }
    }
  }

  if (mem == NULL) {
#ifdef HAVE_LINUX_DMA_HEAP_H
    struct dma_heap_allocation_data alloc_data;
    alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
    alloc_data.heap_flags = 0;
#else
    struct ion_allocation_data alloc_data;
    alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
    alloc_data.flags = ION_FLAG_CACHED;
#endif // HAVE_LINUX_DMA_HEAP_H
    alloc_data.fd = 0;
    alloc_data.len = size;

#ifdef HAVE_LINUX_DMA_HEAP_H
    res = ioctl (priv->devfd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
#else
    res = ioctl (priv->devfd, ION_IOC_ALLOC, &alloc_data);
#endif // HAVE_LINUX_DMA_HEAP_H

    if (res != 0) {
      GST_ERROR_OBJECT (qtiallocator, "Failed to allocate DMA memory on"
          " device fd: %d : %s", priv->devfd, g_strerror (errno));
      return NULL;
    }

    mem = gst_fd_allocator_alloc (allocator, alloc_data.fd,
        size, GST_FD_MEMORY_FLAG_KEEP_MAPPED);

    // TODO: As a precaution map the memory as READ/WRITE when keep_mapped flag
    // is set to avoid case where the memory is mapped with READ only access
    // initially and later on it cannot be mapped with WRITE access.
    if (!gst_memory_map (mem, &map_info, GST_MAP_READWRITE)) {
      GST_ERROR_OBJECT (qtiallocator, "Failed to map gst memory %p", mem);
      return NULL;
    }
    gst_memory_unmap (mem, &map_info);

    GST_DEBUG_OBJECT (qtiallocator, "Allocated memory %p of size %"G_GSIZE_FORMAT
        ", flags %d, align %"G_GSIZE_FORMAT", prefix %"G_GSIZE_FORMAT" fd %d",
        mem, size, params->flags, params->align, params->prefix, alloc_data.fd);

    if (priv->mem_queue) {
      g_return_val_if_fail (mem->mini_object.dispose == NULL, NULL);
      mem->mini_object.dispose = (GstMiniObjectDisposeFunction)
          gst_qti_allocator_memory_dispose;
    }

    GST_OBJECT_LOCK (qtiallocator);
    priv->n_allocated_memory++;
    GST_OBJECT_UNLOCK (qtiallocator);
  }

  return mem;
}

static void
gst_qti_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstQtiAllocator *qtialloc = GST_QTI_ALLOCATOR (allocator);
  gint fd = gst_fd_memory_get_fd (mem);

  GST_DEBUG_OBJECT (qtialloc, "Freeing mem: %p fd: %d", mem, fd);

  close (fd);

  GST_OBJECT_LOCK (qtialloc);
  qtialloc->priv->n_allocated_memory--;
  GST_OBJECT_UNLOCK (qtialloc);
}

static void
gst_qti_allocator_finalize (GObject * obj)
{
  GstQtiAllocator *qtialloc = GST_QTI_ALLOCATOR (obj);

  close (qtialloc->priv->devfd);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_qti_allocator_class_init (GstQtiAllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *alloc_class = GST_ALLOCATOR_CLASS (klass);

  gobject_class->finalize = gst_qti_allocator_finalize;

  alloc_class->free = gst_qti_allocator_free;
  alloc_class->alloc = gst_qti_allocator_alloc;

  GST_DEBUG_CATEGORY_INIT (gst_qtiallocator_debug, "qtiallocator", 0,
      "QtiAllocator");
}

static void
gst_qti_allocator_init (GstQtiAllocator * qtialloc)
{
  qtialloc->priv = (GstQtiAllocatorPrivate *)
      gst_qti_allocator_get_instance_private (qtialloc);
  qtialloc->priv->mem_queue = NULL;

  GST_OBJECT_FLAG_SET (qtialloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_qti_allocator_new ()
{
  GstQtiAllocator *alloc = NULL;

  alloc = g_object_new (GST_TYPE_QTI_ALLOCATOR, NULL);

  alloc->priv->devfd = open (GST_QTI_ALLOCATOR_DMA_QCOM_HEAP_SYSTEM,
      O_RDONLY | O_CLOEXEC);
  if (alloc->priv->devfd < 0) {
    GST_ERROR_OBJECT (alloc, "Failed to open %s, error: %s",
        GST_QTI_ALLOCATOR_DMA_QCOM_HEAP_SYSTEM, g_strerror (errno));
    gst_object_unref (alloc);
    return NULL;
  }

  gst_object_ref_sink (alloc);
  return GST_ALLOCATOR_CAST (alloc);
}

void
gst_qti_allocator_start (GstQtiAllocator * qtiallocator, guint max_memory_blocks)
{
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;

  if (priv->mem_queue != NULL) {
    GST_INFO_OBJECT (qtiallocator, "Allocator is already active");
    return;
  }

  priv->max_memory_blocks = max_memory_blocks;
  priv->mem_queue = gst_atomic_queue_new (DEFAULT_QUEUE_SIZE);
  priv->poll = gst_poll_new_timer ();
  priv->do_free = FALSE;

  gst_poll_write_control (priv->poll);
}

gboolean
gst_qti_allocator_stop (GstQtiAllocator * qtiallocator)
{
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;
  GstMemory *mem = NULL;

  GST_DEBUG_OBJECT (qtiallocator, "Stop allocator");

  if (priv->mem_queue == NULL) {
    GST_INFO_OBJECT (qtiallocator, "Allocator is not active");
    return TRUE;
  }

  if (gst_atomic_queue_length (priv->mem_queue) != priv->n_allocated_memory) {
    GST_WARNING_OBJECT (qtiallocator, "%u buffers are still outstanding",
        priv->n_allocated_memory - gst_atomic_queue_length (priv->mem_queue));
    return FALSE;
  }

  priv->do_free = TRUE;

  while ((mem = gst_atomic_queue_pop (priv->mem_queue))) {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        g_thread_yield ();
        continue;
      } else {
        break;
      }
    }
    GST_LOG_OBJECT (qtiallocator, "freeing memory %p (%u remaining)",
        mem, priv->n_allocated_memory);
    gst_memory_unref (mem);
  }

  priv->n_allocated_memory = 0;
  gst_atomic_queue_unref (priv->mem_queue);
  gst_poll_free (priv->poll);

  return TRUE;
}
