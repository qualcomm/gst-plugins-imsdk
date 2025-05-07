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

#define DEFAULT_PAGE_ALIGNMENT 4096

GST_DEBUG_CATEGORY_STATIC (gst_qtiallocator_debug);
#define GST_CAT_DEFAULT gst_qtiallocator_debug

struct _GstQtiAllocatorPrivate
{
  // DMA/ION device FD
  gint             devfd;
  GstFdMemoryFlags memflags;

  // Mutex for protecting insert/remove from the data map and memory queue.
  GMutex           lock;
  // Map of data FDs and ION handles on case ION memory is used.
  GHashTable       *datamap;

  // Total number of allocated memory blocks.
  guint            n_allocated_memory;
  // Maximum number of allocated memory blocks, set via the start() API.
  guint            max_memory_blocks;
  // Queue of memory blocks which have been orphaned when stolen and then returned.
  // Initialized when the start() API is called and used in conjunction with a pool.
  GstDataQueue     *mem_queue;
};

#define parent_class gst_qti_allocator_parent_class

G_DEFINE_TYPE_WITH_PRIVATE (GstQtiAllocator, gst_qti_allocator,
    GST_TYPE_FD_ALLOCATOR);

static gboolean
gst_data_queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstQtiAllocator *qtiallocator = (GstQtiAllocator *) checkdata;
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;

  if ((priv->max_memory_blocks) != 0 && (visible >= priv->max_memory_blocks)) {
    GST_TRACE_OBJECT (qtiallocator, "Reached queue limit of %d blocks!",
        priv->max_memory_blocks);
    return TRUE;
  }

  return FALSE;
}

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;

  gst_memory_unref (GST_MEMORY_CAST (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_qti_allocator_memory_dispose (GstMiniObject * obj)
{
  GstMemory *memory = GST_MEMORY_CAST (obj);
  GstQtiAllocator *qtiallocator = (GstQtiAllocator *) memory->allocator;
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;
  GstDataQueueItem *item = NULL;

  // If memory queue was deinitialized then directly free the memory block.
  if (priv->mem_queue == NULL)
    return TRUE;

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (gst_memory_ref (memory));
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  if (!gst_data_queue_push (priv->mem_queue, item)) {
    item->destroy (item);
    return TRUE;
  }

  GST_LOG_OBJECT (qtiallocator, "Memory %p enqueued back to queue", memory);
  return FALSE;
}

static GstMemory *
gst_qti_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstQtiAllocator *qtiallocator = GST_QTI_ALLOCATOR (allocator);
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;
  GstMemory *memory = NULL;
#if defined(HAVE_LINUX_DMA_HEAP_H)
  struct dma_heap_allocation_data alloc_data;
#else // !defined(HAVE_LINUX_DMA_HEAP_H)
  struct ion_allocation_data alloc_data;
#if !defined(TARGET_ION_ABI_VERSION)
  struct ion_fd_data fd_data;
#endif // !defined(TARGET_ION_ABI_VERSION)
#endif // defined(HAVE_LINUX_DMA_HEAP_H)
  GstMapInfo mapinfo = {0,};
  gsize maxsize = 0, align = DEFAULT_PAGE_ALIGNMENT;
  gint result = 0, fd = -1;

  g_mutex_lock (&priv->lock);

  // Check if there is a memory queue and an available memory block in it.
  if (priv->mem_queue != NULL && (!gst_data_queue_is_empty (priv->mem_queue) ||
          ((priv->max_memory_blocks != 0) &&
              (priv->n_allocated_memory == priv->max_memory_blocks)))) {
    GstDataQueueItem *item = NULL;

    if (priv->n_allocated_memory == priv->max_memory_blocks)
      GST_LOG_OBJECT (qtiallocator, "Wait for free memory");

    if (gst_data_queue_pop (priv->mem_queue, &item)) {
      memory = gst_memory_ref (GST_MEMORY_CAST (item->object));
      item->destroy (item);
    }
  }

  // Found an available memory block, return it immediately.
  if (memory != NULL) {
    GST_LOG_OBJECT (qtiallocator, "Reusing preallocated memory %p", memory);
    goto cleanup;
  }

  // Couldn't find an available memory block, allocate a new one.
  align = MAX (params->align, DEFAULT_PAGE_ALIGNMENT);

  // Calculate the common alignment for the memory block.
  while (((params->align > 0) && (align % params->align != 0)) ||
         (align % DEFAULT_PAGE_ALIGNMENT != 0))
    align += MAX (params->align, DEFAULT_PAGE_ALIGNMENT);

  maxsize = size + params->prefix + params->padding;
  maxsize = GST_ROUND_UP_N (maxsize, align);

  alloc_data.fd = 0;
  alloc_data.len = maxsize;

#if defined(HAVE_LINUX_DMA_HEAP_H)
  // Permissions for the memory to be allocated.
  alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
  alloc_data.heap_flags = 0;
#else // !defined(HAVE_LINUX_DMA_HEAP_H)
  alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc_data.flags = ION_FLAG_CACHED;

#if !defined(TARGET_ION_ABI_VERSION)
  alloc_data.align = DEFAULT_PAGE_ALIGNMENT;
#endif // !defined(TARGET_ION_ABI_VERSION)
#endif // defined(HAVE_LINUX_DMA_HEAP_H)

#if defined(HAVE_LINUX_DMA_HEAP_H)
  result = ioctl (priv->devfd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
#else // !defined(HAVE_LINUX_DMA_HEAP_H)
  result = ioctl (priv->devfd, ION_IOC_ALLOC, &alloc_data);
#endif // defined(HAVE_LINUX_DMA_HEAP_H)

  if (result != 0) {
    GST_ERROR_OBJECT (qtiallocator, "Failed to allocate memory on device fd: "
        "%d, error: %s", priv->devfd, g_strerror (errno));
    goto cleanup;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  fd_data.handle = alloc_data.handle;
  result = ioctl (priv->devfd, ION_IOC_MAP, &fd_data);

  if (result != 0) {
    GST_ERROR_OBJECT (qtiallocator, "Failed to map ION memory on device fd: "
        "%d, error: %s", priv->devfd, g_strerror (errno));
    ioctl (priv->devfd, ION_IOC_FREE, &alloc_data.handle);
    goto cleanup;
  }

  fd = fd_data.fd;

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd),
      GSIZE_TO_POINTER (alloc_data.handle));
#else
  fd = alloc_data.fd;
#endif // TARGET_ION_ABI_VERSION

  memory = gst_fd_allocator_alloc (allocator, fd, size, priv->memflags);
  GST_MINI_OBJECT_FLAG_SET (memory, params->flags);

  GST_DEBUG_OBJECT (qtiallocator, "Allocated memory %p of size %" G_GSIZE_FORMAT
      ", FD %d, flags %d, align %" G_GSIZE_FORMAT ", prefix %" G_GSIZE_FORMAT
      ", padding %" G_GSIZE_FORMAT, memory, maxsize, fd, params->flags,
      params->align, params->prefix, params->padding);

  // TODO: As a precaution map the memory as READ/WRITE when keep_mapped flag
  // is set to avoid case where the memory is mapped with READ only access
  // initially and later on it cannot be mapped with WRITE access.
  if (!gst_memory_map (memory, &mapinfo, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (qtiallocator, "Failed to map memory %p", memory);
    gst_memory_unref (memory);
    return NULL;
  }

  if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (mapinfo.data, 0, params->prefix);

  if (params->padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset (mapinfo.data + params->prefix + size, 0, params->padding);

  gst_memory_unmap (memory, &mapinfo);

  if (priv->mem_queue != NULL) {
    g_return_val_if_fail (memory->mini_object.dispose == NULL, NULL);
    memory->mini_object.dispose = (GstMiniObjectDisposeFunction)
        gst_qti_allocator_memory_dispose;
  }

  priv->n_allocated_memory++;

cleanup:
  g_mutex_unlock (&priv->lock);
  return memory;
}

static void
gst_qti_allocator_free (GstAllocator * allocator, GstMemory * memory)
{
  GstQtiAllocator *qtiallocator = GST_QTI_ALLOCATOR (allocator);
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;
  gint fd = gst_fd_memory_get_fd (memory);

  GST_DEBUG_OBJECT (qtiallocator, "Closing memory %p with FD %d", memory, fd);

  g_mutex_lock (&priv->lock);

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  ion_user_handle_t handle = GPOINTER_TO_SIZE (
      g_hash_table_lookup (priv->datamap, GINT_TO_POINTER (fd)));

  if (ioctl (priv->devfd, ION_IOC_FREE, &handle) < 0)
    GST_ERROR_OBJECT (qtiallocator, "Failed to free handle for memory FD %d!", fd);

  g_hash_table_remove (priv->datamap, GINT_TO_POINTER (fd));
#endif // TARGET_ION_ABI_VERSION

  close (fd);
  priv->n_allocated_memory--;

  g_mutex_unlock (&priv->lock);
}

static void
gst_qti_allocator_finalize (GObject * obj)
{
  GstQtiAllocator *qtiallocator = GST_QTI_ALLOCATOR (obj);
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;

  if (priv->devfd >= 0) {
    GST_INFO_OBJECT (qtiallocator, "Closing device FD %d", priv->devfd);
    close (priv->devfd);
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  if (priv->datamap != NULL)
    g_hash_table_destroy (priv->datamap);
#endif // TARGET_ION_ABI_VERSION

  g_mutex_clear (&priv->lock);

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
gst_qti_allocator_init (GstQtiAllocator * qtiallocator)
{
  qtiallocator->priv = (GstQtiAllocatorPrivate *)
      gst_qti_allocator_get_instance_private (qtiallocator);

  g_mutex_init (&(qtiallocator->priv->lock));

  qtiallocator->priv->devfd = -1;
  qtiallocator->priv->datamap = NULL;
  qtiallocator->priv->mem_queue = NULL;
  qtiallocator->priv->max_memory_blocks = 0;

  GST_OBJECT_FLAG_SET (qtiallocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_qti_allocator_new (GstFdMemoryFlags memflags)
{
  GstQtiAllocator *allocator = NULL;
  GstQtiAllocatorPrivate *priv = NULL;

  allocator = g_object_new (GST_TYPE_QTI_ALLOCATOR, NULL);
  g_return_val_if_fail (allocator != NULL, NULL);

  priv = allocator->priv;
  priv->memflags = memflags;

  GST_INFO_OBJECT (allocator, "Open /dev/dma_heap/qcom,system");
  priv->devfd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  if (priv->devfd < 0) {
    GST_WARNING_OBJECT (allocator, "Failed to open /dev/dma_heap/qcom,system, "
        "error: %s! Falling back to /dev/ion", g_strerror (errno));
    priv->devfd = open ("/dev/ion", O_RDONLY | O_CLOEXEC);
  }

  if (priv->devfd < 0) {
    GST_ERROR_OBJECT (allocator, "Failed to open DMA/ION device FD, error: %s!",
        g_strerror (errno));
    gst_object_unref (allocator);
    return NULL;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  priv->datamap = g_hash_table_new (NULL, NULL);
#endif // TARGET_ION_ABI_VERSION

  return GST_ALLOCATOR_CAST (allocator);
}

void
gst_qti_allocator_start (GstQtiAllocator * qtiallocator, guint max_memory_blocks)
{
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;

  GST_DEBUG_OBJECT (qtiallocator, "Starting ...");

  if (priv->mem_queue != NULL) {
    GST_INFO_OBJECT (qtiallocator, "Allocator is already active");
    return;
  }

  priv->max_memory_blocks = max_memory_blocks;
  priv->mem_queue = gst_data_queue_new (gst_data_queue_is_full_cb, NULL, NULL,
      qtiallocator);

  if (priv->max_memory_blocks == 0)
    GST_INFO_OBJECT (qtiallocator, "Allocator has not limit for memory blocks");

  gst_data_queue_set_flushing (priv->mem_queue, FALSE);
  GST_DEBUG_OBJECT (qtiallocator, "Started successfully");
}

void
gst_qti_allocator_stop (GstQtiAllocator * qtiallocator)
{
  GstQtiAllocatorPrivate *priv = qtiallocator->priv;
  GstDataQueue *mem_queue = NULL;

  GST_DEBUG_OBJECT (qtiallocator, "Stoping ...");

  if (priv->mem_queue == NULL) {
    GST_INFO_OBJECT (qtiallocator, "Allocator is not active");
    return;
  }

  // Steal the memory queue pointer in order to propery flush and free it.
  mem_queue = g_steal_pointer (&(priv->mem_queue));

  gst_data_queue_set_flushing (mem_queue, TRUE);
  gst_data_queue_flush (mem_queue);

  gst_clear_object (&mem_queue);
  priv->max_memory_blocks = 0;

  GST_DEBUG_OBJECT (qtiallocator, "Stopped successfully");
}
