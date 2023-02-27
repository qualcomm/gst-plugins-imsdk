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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "gstmempool.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#if defined(HAVE_LINUX_DMA_HEAP_H)
#include <linux/dma-heap.h>
#else
#include <linux/ion.h>
#include <linux/msm_ion.h>
#endif // HAVE_LINUX_DMA_HEAP_H


GST_DEBUG_CATEGORY_STATIC (gst_mem_pool_debug);
#define GST_CAT_DEFAULT gst_mem_pool_debug

#define GST_IS_SYSTEM_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_MEMORY_BUFFER_POOL_TYPE_SYSTEM))
#define GST_IS_ION_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_MEMORY_BUFFER_POOL_TYPE_ION))

#define DEFAULT_PAGE_ALIGNMENT 4096

struct _GstMemBufferPoolPrivate
{
  GList               *memsizes;

  GstAllocator        *allocator;
  GstAllocationParams params;
  GQuark              memtype;

  // Either ION device FD.
  gint                devfd;

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  // Map of data FDs and ION handles on case ION memory is used OR
  GHashTable          *datamap;
#endif // TARGET_ION_ABI_VERSION
};

#define gst_mem_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstMemBufferPool, gst_mem_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static gboolean
open_ion_device (GstMemBufferPool * mempool)
{
  GstMemBufferPoolPrivate *priv = mempool->priv;

  GST_INFO_OBJECT (mempool, "Open /dev/dma_heap/qcom,system");
  priv->devfd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  if (priv->devfd < 0) {
    GST_WARNING_OBJECT (mempool, "Falling back to /dev/ion");
    priv->devfd = open ("/dev/ion", O_RDONLY | O_CLOEXEC);
  }

  if (priv->devfd < 0) {
    GST_ERROR_OBJECT (mempool, "Failed to open ION device FD!");
    return FALSE;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  priv->datamap = g_hash_table_new (NULL, NULL);
#endif // TARGET_ION_ABI_VERSION

  GST_INFO_OBJECT (mempool, "Opened ION device FD %d", priv->devfd);
  return TRUE;
}

static void
close_ion_device (GstMemBufferPool * mempool)
{
  GstMemBufferPoolPrivate *priv = mempool->priv;

  if (priv->devfd >= 0) {
    GST_INFO_OBJECT (mempool, "Closing ION device FD %d", priv->devfd);
    close (priv->devfd);
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  g_hash_table_destroy (priv->datamap);
#endif // TARGET_ION_ABI_VERSION
}

static GstMemory *
ion_device_alloc (GstMemBufferPool * mempool, gint size)
{
  GstMemBufferPoolPrivate *priv = mempool->priv;
  gint result = 0, fd = -1;

#if defined(HAVE_LINUX_DMA_HEAP_H)
  struct dma_heap_allocation_data alloc_data;
#else
  struct ion_allocation_data alloc_data;
#if !defined(TARGET_ION_ABI_VERSION)
  struct ion_fd_data fd_data;
#endif // TARGET_ION_ABI_VERSION
#endif

  alloc_data.fd = 0;
  alloc_data.len = size;

#if defined(HAVE_LINUX_DMA_HEAP_H)
  // Permissions for the memory to be allocated.
  alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
  alloc_data.heap_flags = 0;
#else
  alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc_data.flags = ION_FLAG_CACHED;

#if !defined(TARGET_ION_ABI_VERSION)
  alloc_data.align = DEFAULT_PAGE_ALIGNMENT;
#endif // TARGET_ION_ABI_VERSION
#endif

#if defined(HAVE_LINUX_DMA_HEAP_H)
  result = ioctl (priv->devfd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
#else
  result = ioctl (priv->devfd, ION_IOC_ALLOC, &alloc_data);
#endif

  if (result != 0) {
    GST_ERROR_OBJECT (mempool, "Failed to allocate ION memory!");
    return NULL;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  fd_data.handle = alloc_data.handle;

  result = ioctl (priv->devfd, ION_IOC_MAP, &fd_data);
  if (result != 0) {
    GST_ERROR_OBJECT (mempool, "Failed to map memory to FD!");
    ioctl (priv->devfd, ION_IOC_FREE, &alloc_data.handle);
    return NULL;
  }

  fd = fd_data.fd;

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd),
      GSIZE_TO_POINTER (alloc_data.handle));
#else
  fd = alloc_data.fd;
#endif

  GST_DEBUG_OBJECT (mempool, "Allocated ION memory FD %d", fd);

  // Wrap the allocated FD in FD backed allocator.
  return gst_fd_allocator_alloc (priv->allocator, fd, size,
      GST_FD_MEMORY_FLAG_DONT_CLOSE);
}

static void
ion_device_free (GstMemBufferPool * mempool, gint fd)
{
  GST_DEBUG_OBJECT (mempool, "Closing ION memory FD %d", fd);

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  ion_user_handle_t handle = GPOINTER_TO_SIZE (
      g_hash_table_lookup (mempool->priv->datamap, GINT_TO_POINTER (fd)));

  if (ioctl (mempool->priv->devfd, ION_IOC_FREE, &handle) < 0) {
    GST_ERROR_OBJECT (mempool, "Failed to free handle for memory FD %d!", fd);
  }

  g_hash_table_remove (mempool->priv->datamap, GINT_TO_POINTER (fd));
#endif // TARGET_ION_ABI_VERSION

  close (fd);
}

static const gchar **
gst_mem_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { NULL };
  return options;
}

static gboolean
gst_mem_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstMemBufferPool *mempool = GST_MEM_BUFFER_POOL (pool);
  GstMemBufferPoolPrivate *priv = mempool->priv;
  GstAllocator *allocator = NULL;
  const GValue *memblocks = NULL;
  GstAllocationParams params = { 0, };
  guint size = 0;

  if (!gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL)) {
    GST_ERROR_OBJECT (mempool, "Invalid configuration!");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (mempool, "Allocator missing from configuration!");
    return FALSE;
  } else if ((NULL == allocator) && GST_IS_ION_MEMORY_TYPE (priv->memtype)) {
    // No allocator set in configuration, create default FD allocator.
    if (NULL == (allocator = gst_fd_allocator_new ())) {
      GST_ERROR_OBJECT (mempool, "Failed to create FD allocator!");
      return FALSE;
    }
  } else if ((NULL == allocator) && GST_IS_SYSTEM_MEMORY_TYPE (priv->memtype)) {
    allocator = gst_allocator_find (GST_ALLOCATOR_SYSMEM);
    // No allocator set in configuration, create default SYSTEM allocator.
    if (NULL == (allocator = gst_allocator_find (GST_ALLOCATOR_SYSMEM))) {
      GST_ERROR_OBJECT (mempool, "Failed to create SYSTEM allocator!");
      return FALSE;
    }
  }

  if (GST_IS_ION_MEMORY_TYPE (priv->memtype) && !GST_IS_FD_ALLOCATOR (allocator)) {
    GST_ERROR_OBJECT (mempool, "Allocator %p is not FD backed!", allocator);
    return FALSE;
  }

  if ((memblocks = gst_structure_get_value (config, "memory-blocks")) != NULL) {
    guint n_blocks = gst_value_array_get_size (memblocks);
    GST_INFO_OBJECT (mempool, "%d memory blocks found", n_blocks);

    for (guint i = 0; i < n_blocks; i++) {
      const GValue *value = gst_value_array_get_value (memblocks, i);
      priv->memsizes = g_list_append (
          priv->memsizes, GUINT_TO_POINTER (g_value_get_uint (value)));
    }
  } else {
    priv->memsizes = g_list_append (priv->memsizes, GUINT_TO_POINTER (size));
  }

  priv->params = params;

  // Remove cached allocator.
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  priv->allocator = allocator;
  gst_object_ref (priv->allocator);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_mem_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstMemBufferPool *mempool = GST_MEM_BUFFER_POOL (pool);
  GstMemBufferPoolPrivate *priv = mempool->priv;
  GstBuffer *newbuffer = NULL;
  GList *list = NULL;

  // Create a GstBuffer.
  newbuffer = gst_buffer_new ();

  for (list = priv->memsizes; list != NULL; list = list->next) {
    GstMemory *memory = NULL;
    guint blocksize = GPOINTER_TO_UINT (list->data);

    if (GST_IS_SYSTEM_MEMORY_TYPE (priv->memtype)) {
      memory = gst_allocator_alloc (priv->allocator, blocksize, &(priv->params));
    } else if (GST_IS_ION_MEMORY_TYPE (priv->memtype)) {
      memory = ion_device_alloc (mempool, blocksize);
    }

    if (memory == NULL) {
      GST_WARNING_OBJECT (pool, "Failed to allocate memory block!");
      gst_buffer_unref (newbuffer);
      return GST_FLOW_ERROR;
    }
    // Append the memory to the newly created GstBuffer.
    gst_buffer_append_memory (newbuffer, memory);
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
gst_mem_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstMemBufferPool *mempool = GST_MEM_BUFFER_POOL (pool);
  GstMemBufferPoolPrivate *priv = mempool->priv;
  guint idx = 0, length = 0;

  length = g_list_length (priv->memsizes);

  for (idx = 0; (idx < length) && GST_IS_ION_MEMORY_TYPE (priv->memtype); idx++) {
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, idx));
    ion_device_free (mempool, fd);
  }

  gst_buffer_unref (buffer);
}

static void
gst_mem_buffer_pool_reset (GstBufferPool * pool, GstBuffer * buffer)
{
  GstMemBufferPoolPrivate *priv = GST_MEM_BUFFER_POOL (pool)->priv;
  guint idx = 0, length = 0;

  length = gst_buffer_n_memory (buffer);

  // Sanity check.
  g_return_if_fail (length == g_list_length (priv->memsizes));

  // Resize the buffer to the original size otherwise it will be discarded
  // due to the mismatch during the default implementation of release_buffer.
  for (idx = 0; idx < length; idx++) {
    guint blocksize = GPOINTER_TO_UINT (g_list_nth_data (priv->memsizes, idx));
    gst_buffer_resize_range (buffer, idx, 1, 0, blocksize);
  }

  GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (pool, buffer);
}

static void
gst_mem_buffer_pool_finalize (GObject * object)
{
  GstMemBufferPool *mempool = GST_MEM_BUFFER_POOL (object);
  GstMemBufferPoolPrivate *priv = mempool->priv;

  GST_INFO_OBJECT (mempool, "Finalize buffer pool %p", mempool);

  if (priv->allocator) {
    GST_INFO_OBJECT (mempool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
  }

  if (priv->memsizes != NULL) {
    g_list_free (priv->memsizes);
    priv->memsizes = NULL;
  }

  close_ion_device (mempool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mem_buffer_pool_class_init (GstMemBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_mem_buffer_pool_finalize;

  pool->get_options = gst_mem_buffer_pool_get_options;
  pool->set_config = gst_mem_buffer_pool_set_config;
  pool->alloc_buffer = gst_mem_buffer_pool_alloc;
  pool->free_buffer = gst_mem_buffer_pool_free;
  pool->reset_buffer = gst_mem_buffer_pool_reset;

  GST_DEBUG_CATEGORY_INIT (gst_mem_pool_debug, "mem-pool", 0,
      "mem-pool object");
}

static void
gst_mem_buffer_pool_init (GstMemBufferPool * mempool)
{
  mempool->priv = gst_mem_buffer_pool_get_instance_private (mempool);
  mempool->priv->devfd = -1;
  mempool->priv->memsizes = NULL;
}


GstBufferPool *
gst_mem_buffer_pool_new (const gchar * type)
{
  GstMemBufferPool *mempool = NULL;
  gboolean success = FALSE;

  mempool = g_object_new (GST_TYPE_MEM_BUFFER_POOL, NULL);
  mempool->priv->memtype = g_quark_from_string (type);

  if (GST_IS_SYSTEM_MEMORY_TYPE (mempool->priv->memtype)) {
    GST_INFO_OBJECT (mempool, "Using SYSTEM memory");
    success = TRUE;
  } else if (GST_IS_ION_MEMORY_TYPE (mempool->priv->memtype)) {
    GST_INFO_OBJECT (mempool, "Using ION memory");
    success = open_ion_device (mempool);
  } else {
    GST_ERROR_OBJECT (mempool, "Invalid memory type %s!",
        g_quark_to_string (mempool->priv->memtype));
    success = FALSE;
  }

  if (!success) {
    gst_object_unref (mempool);
    return NULL;
  }

  GST_INFO_OBJECT (mempool, "New buffer pool %p", mempool);
  return GST_BUFFER_POOL_CAST (mempool);
}
