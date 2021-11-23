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

#include "gstmlpool.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/ion.h>
#include <linux/msm_ion.h>

#include "gstmlmeta.h"

#define GST_IS_ION_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_ML_BUFFER_POOL_TYPE_ION))

#define GST_IS_SYSTEM_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_ML_BUFFER_POOL_TYPE_SYSTEM))

#define DEFAULT_ION_ALIGNMENT 4096

GST_DEBUG_CATEGORY_STATIC (gst_ml_pool_debug);
#define GST_CAT_DEFAULT gst_ml_pool_debug

struct _GstMLBufferPoolPrivate
{
  // Allocation memory type.
  GQuark memtype;

  GstAllocator *allocator;
  GstAllocationParams params;
  GstMLInfo info;

  gboolean addmeta;

  // ION device FD.
  gint devfd;

#ifndef TARGET_ION_ABI_VERSION
  // Map of data FDs and ION handles on case ION memory is used.
  GHashTable *datamap;
#endif
};

#define gst_ml_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstMLBufferPool,
    gst_ml_buffer_pool, GST_TYPE_BUFFER_POOL);

static gboolean
open_ion_device (GstMLBufferPool * mlpool)
{
  GstMLBufferPoolPrivate *priv = mlpool->priv;

  if (priv->devfd >= 0) {
    GST_DEBUG_OBJECT (mlpool, "ION device already opened");
    return TRUE;
  }

  priv->devfd = open ("/dev/ion", O_RDWR);
  if (priv->devfd < 0) {
    GST_ERROR_OBJECT (mlpool, "Failed to open ION device FD!");
    return FALSE;
  }

#ifndef TARGET_ION_ABI_VERSION
  priv->datamap = g_hash_table_new (NULL, NULL);
#endif

  GST_INFO_OBJECT (mlpool, "Opened ION device FD %d", priv->devfd);
  return TRUE;
}

static void
close_ion_device (GstMLBufferPool * mlpool)
{
  GstMLBufferPoolPrivate *priv = mlpool->priv;

  if (priv->devfd >= 0) {
    GST_INFO_OBJECT (mlpool, "Closing ION device FD %d", priv->devfd);
    close (priv->devfd);
  }

#ifndef TARGET_ION_ABI_VERSION
  g_hash_table_destroy (priv->datamap);
#endif
}

static GstMemory *
ion_device_alloc (GstMLBufferPool * mlpool, gsize size)
{
  GstMLBufferPoolPrivate *priv = mlpool->priv;
  gint result = 0, fd = -1;

#ifndef TARGET_ION_ABI_VERSION
  struct ion_fd_data fd_data;
#endif
  struct ion_allocation_data alloc_data;

  alloc_data.len = size;
#ifndef TARGET_ION_ABI_VERSION
  alloc_data.align = DEFAULT_ION_ALIGNMENT;
#endif
  alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc_data.flags = ION_FLAG_CACHED;

  result = ioctl (priv->devfd, ION_IOC_ALLOC, &alloc_data);
  if (result != 0) {
    GST_ERROR_OBJECT (mlpool, "Failed to allocate ION memory!");
    return NULL;
  }

#ifndef TARGET_ION_ABI_VERSION
  fd_data.handle = alloc_data.handle;

  result = ioctl (priv->devfd, ION_IOC_MAP, &fd_data);
  if (result != 0) {
    GST_ERROR_OBJECT (mlpool, "Failed to map memory to FD!");
    ioctl (priv->devfd, ION_IOC_FREE, &alloc_data.handle);
    return NULL;
  }

  fd = fd_data.fd;

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd),
      GINT_TO_POINTER (alloc_data.handle));
#else
  fd = alloc_data.fd;
#endif

  GST_DEBUG_OBJECT (mlpool, "Allocated ION memory FD %d", fd);

  // Wrap the allocated FD in FD backed allocator.
  return gst_fd_allocator_alloc (priv->allocator, fd, size,
      GST_FD_MEMORY_FLAG_DONT_CLOSE);
}

static void
ion_device_free (GstMLBufferPool * mlpool, gint fd)
{
  GST_DEBUG_OBJECT (mlpool, "Closing ION memory FD %d", fd);

#ifndef TARGET_ION_ABI_VERSION
  ion_user_handle_t handle = GPOINTER_TO_INT (
      g_hash_table_lookup (mlpool->priv->datamap, GINT_TO_POINTER (fd)));

  if (ioctl (mlpool->priv->devfd, ION_IOC_FREE, &handle) < 0) {
    GST_ERROR_OBJECT (mlpool, "Failed to free handle for memory FD %d!", fd);
  }

  g_hash_table_remove (mlpool->priv->datamap, GINT_TO_POINTER (fd));
#endif

  close (fd);
}

static const gchar **
gst_ml_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_ML_BUFFER_POOL_OPTION_TENSOR_META,
    NULL
  };
  return options;
}

static gboolean
gst_ml_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstMLBufferPool *mlpool = GST_ML_POOL_CAST (pool);
  GstMLBufferPoolPrivate *priv = mlpool->priv;

  GstCaps *caps;
  guint maxsize, minbuffers, maxbuffers;
  GstMLInfo info;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean success;

  success = gst_buffer_pool_config_get_params (config, &caps, &maxsize,
      &minbuffers, &maxbuffers);

  if (!success) {
    GST_ERROR_OBJECT (mlpool, "Invalid configuration!");
    return FALSE;
  } else if (caps == NULL) {
    GST_ERROR_OBJECT (mlpool, "Caps missing from configuration");
    return FALSE;
  }

  // Now parse the caps from the configuration.
  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlpool, "Failed getting geometry from caps %"
        GST_PTR_FORMAT, caps);
    return FALSE;
  } else if (maxsize != gst_ml_info_size (&info)) {
    GST_ERROR_OBJECT (pool, "Provided size is not equal for the caps: %u != %"
        G_GSIZE_FORMAT, maxsize, gst_ml_info_size (&info));
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (mlpool, "Allocator missing from configuration");
    return FALSE;
  } else if (GST_IS_ION_MEMORY_TYPE (priv->memtype) &&
      !GST_IS_FD_ALLOCATOR (allocator)) {
    GST_ERROR_OBJECT (mlpool, "Allocator %p is not FD backed!", allocator);
    return FALSE;
  } else if (GST_IS_SYSTEM_MEMORY_TYPE (priv->memtype) &&
      GST_IS_FD_ALLOCATOR (allocator)) {
    GST_ERROR_OBJECT (mlpool, "Allocator %p cannot be FD backed!", allocator);
    return FALSE;
  }

  GST_DEBUG_OBJECT (pool, "Caps %" GST_PTR_FORMAT, caps);

  priv->params = params;
  priv->info = info;

  // Remove cached allocator.
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  priv->allocator = allocator;
  gst_object_ref (priv->allocator);

  // Enable metadata based on configuration of the pool.
  priv->addmeta = gst_buffer_pool_config_has_option (config,
      GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  gst_buffer_pool_config_set_params (config, caps, maxsize, minbuffers,
      maxbuffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_ml_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstMLBufferPool *mlpool = GST_ML_POOL_CAST (pool);
  GstMLBufferPoolPrivate *priv = mlpool->priv;
  GstMemory *mem = NULL;
  GstBuffer *newbuffer = NULL;
  GstMLTensorMeta *meta = NULL;
  guint idx = 0, dim = 0, size = 0;

  // Create a GstBuffer.
  newbuffer = gst_buffer_new ();

  for (idx = 0; idx < priv->info.n_tensors; idx++) {
    size = gst_ml_info_tensor_size (&priv->info, idx);

    if (GST_IS_SYSTEM_MEMORY_TYPE (priv->memtype))
      mem = gst_allocator_alloc (priv->allocator, size, NULL);
    else if (GST_IS_ION_MEMORY_TYPE (priv->memtype))
      mem = ion_device_alloc (mlpool, size);

    if (NULL == mem) {
      GST_WARNING_OBJECT (mlpool, "Failed to allocate memory!");
      gst_buffer_unref (newbuffer);
      return GST_FLOW_ERROR;
    }
    // Append the FD backed memory to the newly created GstBuffer.
    gst_buffer_append_memory(newbuffer, mem);

    if (priv->addmeta) {
      guint n_dimensions = 0;

      GST_DEBUG_OBJECT (mlpool, "Adding GstMLTensorMeta");

      for (dim = 0; dim < GST_ML_TENSOR_MAX_DIMENSIONS; ++dim)
        n_dimensions += (priv->info.tensors[idx][dim] != 0) ? 1 : 0;

      meta = gst_buffer_add_ml_tensor_meta (newbuffer, priv->info.type,
          n_dimensions, priv->info.tensors[idx]);
      meta->id = idx;
    }
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
gst_ml_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstMLBufferPool *mlpool = GST_ML_POOL_CAST (pool);
  guint idx = 0;

  for (idx = 0; idx < gst_buffer_n_memory (buffer); idx++) {
    if (GST_IS_ION_MEMORY_TYPE (mlpool->priv->memtype)) {
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, idx));
      ion_device_free (mlpool, fd);
    } else if (GST_IS_SYSTEM_MEMORY_TYPE (mlpool->priv->memtype)) {
      // No additional handling is needed.
    }
  }

  gst_buffer_unref (buffer);
}

static void
gst_ml_buffer_pool_finalize (GObject * object)
{
  GstMLBufferPool *mlpool = GST_ML_POOL_CAST (object);
  GstMLBufferPoolPrivate *priv = mlpool->priv;

  GST_INFO_OBJECT (mlpool, "Finalize ML buffer pool %p", mlpool);

  if (priv->allocator) {
    GST_INFO_OBJECT (mlpool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
  }

  if (GST_IS_ION_MEMORY_TYPE (priv->memtype))
    close_ion_device (mlpool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ml_buffer_pool_class_init (GstMLBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_ml_buffer_pool_finalize;

  pool->get_options = gst_ml_buffer_pool_get_options;
  pool->set_config = gst_ml_buffer_pool_set_config;
  pool->alloc_buffer = gst_ml_buffer_pool_alloc;
  pool->free_buffer = gst_ml_buffer_pool_free;

  GST_DEBUG_CATEGORY_INIT (gst_ml_pool_debug, "mlpool", 0, "ML Buffer Pool");
}

static void
gst_ml_buffer_pool_init (GstMLBufferPool * mlpool)
{
  mlpool->priv = gst_ml_buffer_pool_get_instance_private (mlpool);
}

GstBufferPool *
gst_ml_buffer_pool_new (const gchar * memtype)
{
  GstMLBufferPool *mlpool;
  gboolean success = FALSE;

  g_return_val_if_fail (memtype != NULL, NULL);

  mlpool = g_object_new (GST_TYPE_ML_POOL, NULL);

  mlpool->priv->memtype = g_quark_from_static_string (memtype);
  mlpool->priv->devfd = -1;
  mlpool->priv->addmeta = FALSE;

  if (GST_IS_ION_MEMORY_TYPE (mlpool->priv->memtype)) {
    GST_INFO_OBJECT (mlpool, "Using ION memory");
    success = open_ion_device (mlpool);
  } else if (GST_IS_SYSTEM_MEMORY_TYPE (mlpool->priv->memtype)) {
    GST_INFO_OBJECT (mlpool, "Using SYSTEM memory");
    success = TRUE;
  } else {
    GST_ERROR_OBJECT (mlpool, "Invalid memory type %s!",
        g_quark_to_string (mlpool->priv->memtype));
    success = FALSE;
  }

  if (!success) {
    gst_object_unref (mlpool);
    return NULL;
  }

  GST_INFO_OBJECT (mlpool, "New ML buffer pool %p", mlpool);
  return GST_BUFFER_POOL_CAST (mlpool);
}

const GstMLInfo *
gst_ml_buffer_pool_get_info (GstBufferPool * pool)
{
  return &(GST_ML_POOL_CAST (pool)->priv->info);
}
