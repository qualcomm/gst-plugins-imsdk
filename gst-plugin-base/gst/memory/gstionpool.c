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

#include "gstionpool.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/ion.h>
#include <linux/msm_ion.h>


GST_DEBUG_CATEGORY_STATIC (gst_ion_pool_debug);
#define GST_CAT_DEFAULT gst_ion_pool_debug

#define DEFAULT_PAGE_ALIGNMENT 4096

struct _GstIonBufferPoolPrivate
{
  GList               *memsizes;

  GstAllocator        *allocator;
  GstAllocationParams params;

  // Either ION device FD.
  gint                devfd;

#ifndef TARGET_ION_ABI_VERSION
  // Map of data FDs and ION handles on case ION memory is used OR
  GHashTable          *datamap;
#endif
};

#define gst_ion_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstIonBufferPool, gst_ion_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static gboolean
open_ion_device (GstIonBufferPool * ionpool)
{
  GstIonBufferPoolPrivate *priv = ionpool->priv;

  GST_INFO_OBJECT (ionpool, "Open /dev/dma_heap/qcom,system");
  priv->devfd = open ("/dev/dma_heap/qcom,system", O_RDWR);

  if (priv->devfd < 0) {
    GST_WARNING_OBJECT (ionpool, "Falling back to /dev/ion");
    priv->devfd = open ("/dev/ion", O_RDWR);
  }

  if (priv->devfd < 0) {
    GST_ERROR_OBJECT (ionpool, "Failed to open ION device FD!");
    return FALSE;
  }

#ifndef TARGET_ION_ABI_VERSION
  priv->datamap = g_hash_table_new (NULL, NULL);
#endif

  GST_INFO_OBJECT (ionpool, "Opened ION device FD %d", priv->devfd);
  return TRUE;
}

static void
close_ion_device (GstIonBufferPool * ionpool)
{
  GstIonBufferPoolPrivate *priv = ionpool->priv;

  if (priv->devfd >= 0) {
    GST_INFO_OBJECT (ionpool, "Closing ION device FD %d", priv->devfd);
    close (priv->devfd);
  }

#ifndef TARGET_ION_ABI_VERSION
  g_hash_table_destroy (priv->datamap);
#endif
}

static GstMemory *
ion_device_alloc (GstIonBufferPool * ionpool, gint size)
{
  GstIonBufferPoolPrivate *priv = ionpool->priv;
  gint result = 0, fd = -1;

#ifndef TARGET_ION_ABI_VERSION
  struct ion_fd_data fd_data;
#endif
  struct ion_allocation_data alloc_data;

  alloc_data.len = size;
#ifndef TARGET_ION_ABI_VERSION
  alloc_data.align = DEFAULT_PAGE_ALIGNMENT;
#endif
  alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc_data.flags = ION_FLAG_CACHED;

  result = ioctl (priv->devfd, ION_IOC_ALLOC, &alloc_data);
  if (result != 0) {
    GST_ERROR_OBJECT (ionpool, "Failed to allocate ION memory!");
    return NULL;
  }

#ifndef TARGET_ION_ABI_VERSION
  fd_data.handle = alloc_data.handle;

  result = ioctl (priv->devfd, ION_IOC_MAP, &fd_data);
  if (result != 0) {
    GST_ERROR_OBJECT (ionpool, "Failed to map memory to FD!");
    ioctl (priv->devfd, ION_IOC_FREE, &alloc_data.handle);
    return NULL;
  }

  fd = fd_data.fd;

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd),
      GSIZE_TO_POINTER (alloc_data.handle));
#else
  fd = alloc_data.fd;
#endif

  GST_DEBUG_OBJECT (ionpool, "Allocated ION memory FD %d", fd);

  // Wrap the allocated FD in FD backed allocator.
  return gst_fd_allocator_alloc (priv->allocator, fd, size,
      GST_FD_MEMORY_FLAG_DONT_CLOSE);
}

static void
ion_device_free (GstIonBufferPool * ionpool, gint fd)
{
  GST_DEBUG_OBJECT (ionpool, "Closing ION memory FD %d", fd);

#ifndef TARGET_ION_ABI_VERSION
  ion_user_handle_t handle = GPOINTER_TO_SIZE (
      g_hash_table_lookup (ionpool->priv->datamap, GINT_TO_POINTER (fd)));

  if (ioctl (ionpool->priv->devfd, ION_IOC_FREE, &handle) < 0) {
    GST_ERROR_OBJECT (ionpool, "Failed to free handle for memory FD %d!", fd);
  }

  g_hash_table_remove (ionpool->priv->datamap, GINT_TO_POINTER (fd));
#endif

  close (fd);
}

static const gchar **
gst_ion_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { NULL };
  return options;
}

static gboolean
gst_ion_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstIonBufferPool *ionpool = GST_ION_BUFFER_POOL (pool);
  GstIonBufferPoolPrivate *priv = ionpool->priv;
  GstAllocator *allocator = NULL;
  const GValue *memblocks = NULL;
  GstAllocationParams params = { 0, };
  guint size = 0;

  if (!gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL)) {
    GST_ERROR_OBJECT (ionpool, "Invalid configuration!");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (ionpool, "Allocator missing from configuration!");
    return FALSE;
  } else if (NULL == allocator) {
    // No allocator set in configuration, create default FD allocator.
    if (NULL == (allocator = gst_fd_allocator_new ())) {
      GST_ERROR_OBJECT (ionpool, "Failed to create FD allocator!");
      return FALSE;
    }
  }

  if (!GST_IS_FD_ALLOCATOR (allocator)) {
     GST_ERROR_OBJECT (ionpool, "Allocator %p is not FD backed!", allocator);
     return FALSE;
  }

  if ((memblocks = gst_structure_get_value (config, "memory-blocks")) != NULL) {
    guint n_blocks = gst_value_array_get_size (memblocks);
    GST_INFO_OBJECT (ionpool, "%d memory blocks found", n_blocks);

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
gst_ion_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstIonBufferPool *ionpool = GST_ION_BUFFER_POOL (pool);
  GstIonBufferPoolPrivate *priv = ionpool->priv;
  GstBuffer *newbuffer = NULL;
  GList *list = NULL;

  // Create a GstBuffer.
  newbuffer = gst_buffer_new ();

  for (list = priv->memsizes; list != NULL; list = list->next) {
    GstMemory *memory = NULL;
    guint blocksize = GPOINTER_TO_UINT (list->data);

    if ((memory = ion_device_alloc (ionpool, blocksize)) == NULL) {
      GST_WARNING_OBJECT (pool, "Failed to allocate memory block!");
      gst_buffer_unref (newbuffer);
      return GST_FLOW_ERROR;
    }
    // Append the FD backed memory to the newly created GstBuffer.
    gst_buffer_append_memory (newbuffer, memory);
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
gst_ion_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstIonBufferPool *ionpool = GST_ION_BUFFER_POOL (pool);
  GstIonBufferPoolPrivate *priv = ionpool->priv;

  for (guint i = 0; i < g_list_length (priv->memsizes); i++) {
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, i));
    ion_device_free (ionpool, fd);
  }

  gst_buffer_unref (buffer);
}

static void
gst_ion_buffer_pool_finalize (GObject * object)
{
  GstIonBufferPool *ionpool = GST_ION_BUFFER_POOL (object);
  GstIonBufferPoolPrivate *priv = ionpool->priv;

  GST_INFO_OBJECT (ionpool, "Finalize buffer pool %p", ionpool);

  if (priv->allocator) {
    GST_INFO_OBJECT (ionpool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
  }

  if (priv->memsizes != NULL) {
    g_list_free (priv->memsizes);
    priv->memsizes = NULL;
  }

  close_ion_device (ionpool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ion_buffer_pool_class_init (GstIonBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_ion_buffer_pool_finalize;

  pool->get_options = gst_ion_buffer_pool_get_options;
  pool->set_config = gst_ion_buffer_pool_set_config;
  pool->alloc_buffer = gst_ion_buffer_pool_alloc;
  pool->free_buffer = gst_ion_buffer_pool_free;

  GST_DEBUG_CATEGORY_INIT (gst_ion_pool_debug, "ion-pool", 0,
      "ion-pool object");
}

static void
gst_ion_buffer_pool_init (GstIonBufferPool * ionpool)
{
  ionpool->priv = gst_ion_buffer_pool_get_instance_private (ionpool);
  ionpool->priv->devfd = -1;
  ionpool->priv->memsizes = NULL;
}


GstBufferPool *
gst_ion_buffer_pool_new ()
{
  GstIonBufferPool *ionpool;

  ionpool = g_object_new (GST_TYPE_ION_BUFFER_POOL, NULL);

  if (!open_ion_device (ionpool)) {
    gst_object_unref (ionpool);
    return NULL;
  }

  GST_INFO_OBJECT (ionpool, "New buffer pool %p", ionpool);
  return GST_BUFFER_POOL_CAST (ionpool);
}
