/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#include "video_transform_buffer_pool.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <gbm.h>
#include <gbm_priv.h>
#include <linux/ion.h>
#include <linux/msm_ion.h>

#define GST_IS_GBM_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_VTRANS_BUFFER_POOL_TYPE_GBM))

#define GST_IS_ION_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_VTRANS_BUFFER_POOL_TYPE_ION))

#define DEFAULT_ION_ALIGNMENT 4096

GST_DEBUG_CATEGORY_STATIC (gst_vtrans_pool_debug);
#define GST_CAT_DEFAULT gst_vtrans_pool_debug

struct _GstVTransBufferPoolPrivate
{
  GstVideoInfo info;
  gboolean addmeta;

  GstAllocator *allocator;
  GstAllocationParams params;
  GQuark memtype;

  gint devicefd;
  struct gbm_device *gbmhandle;

  // Map of data FDs and ION handles on case ION memory is used OR
  // map of data FDs and GBM buffer objects if GBM memory is used.
  GHashTable *datamap;
};

#define gst_vtrans_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVTransBufferPool,
    gst_vtrans_buffer_pool, GST_TYPE_BUFFER_POOL);

static gint
gst_video_format_to_gbm_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return GBM_FORMAT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return GBM_FORMAT_NV21_ZSL;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case GST_VIDEO_FORMAT_xRGB:
      return GBM_FORMAT_XRGB8888;
    case GST_VIDEO_FORMAT_ARGB:
      return GBM_FORMAT_ARGB8888;
    case GST_VIDEO_FORMAT_xBGR:
      return GBM_FORMAT_XBGR8888;
    case GST_VIDEO_FORMAT_RGBx:
      return GBM_FORMAT_RGBX8888;
    case GST_VIDEO_FORMAT_ABGR:
      return GBM_FORMAT_ABGR8888;
    case GST_VIDEO_FORMAT_RGBA:
      return GBM_FORMAT_RGBA8888;
    case GST_VIDEO_FORMAT_RGB:
      return GBM_FORMAT_RGB888;
    case GST_VIDEO_FORMAT_BGR16:
      return GBM_FORMAT_BGR565;
    case GST_VIDEO_FORMAT_RGB16:
      return GBM_FORMAT_RGB565;
#else
    case GST_VIDEO_FORMAT_BGRx:
      return GBM_FORMAT_XRGB8888;
    case GST_VIDEO_FORMAT_BGRA:
      return GBM_FORMAT_ARGB8888;
    case GST_VIDEO_FORMAT_RGBx:
      return GBM_FORMAT_XBGR8888;
    case GST_VIDEO_FORMAT_xBGR:
      return GBM_FORMAT_RGBX8888;
    case GST_VIDEO_FORMAT_RGBA:
      return GBM_FORMAT_ABGR8888;
    case GST_VIDEO_FORMAT_ABGR:
      return GBM_FORMAT_RGBA8888;
    case GST_VIDEO_FORMAT_BGR:
      return GBM_FORMAT_RGB888;
    case GST_VIDEO_FORMAT_BGR16:
      return GBM_FORMAT_RGB565;
    case GST_VIDEO_FORMAT_RGB16:
      return GBM_FORMAT_BGR565;
#endif
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }
  return -1;
}

static gboolean
open_gbm_device (GstVTransBufferPool * vpool)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  // Due to limitation in the GBM implementation we need to open /dev/ion
  // instead of /dev/dri/card0.
  priv->devicefd = open ("/dev/ion", O_RDWR);

  if (priv->devicefd < 0) {
    GST_WARNING_OBJECT (vpool, "Falling back to /dev/ion");
    priv->devicefd = open ("/dev/ion", O_RDWR);
  }

  if (priv->devicefd < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to open GBM device FD!");
    return FALSE;
  }

  GST_INFO_OBJECT (vpool, "Opened GBM device FD %d", priv->devicefd);

  priv->gbmhandle = gbm_create_device (priv->devicefd);
  if (NULL == priv->gbmhandle) {
    GST_ERROR_OBJECT (vpool, "Failed to create GBM handle!");
    close (priv->devicefd);
    return FALSE;
  }

  priv->datamap = g_hash_table_new (NULL, NULL);

  GST_INFO_OBJECT (vpool, "Created GBM handle %p", priv->gbmhandle);
  return TRUE;
}

static void
close_gbm_device (GstVTransBufferPool * vpool)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  if (priv->gbmhandle != NULL) {
    GST_INFO_OBJECT (vpool, "Closing GBM handle %p", priv->gbmhandle);
    gbm_device_destroy (priv->gbmhandle);
  }

  if (priv->devicefd >= 0) {
    GST_INFO_OBJECT (vpool, "Closing GBM device FD %d", priv->devicefd);
    close (priv->devicefd);
  }

  g_hash_table_destroy (priv->datamap);
}

static GstMemory *
gbm_device_alloc (GstVTransBufferPool * vpool)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  struct gbm_bo *bo;
  gint gbmformat, usage, fd;

  gbmformat = gst_video_format_to_gbm_format (GST_VIDEO_INFO_FORMAT (&priv->info));
  g_return_val_if_fail (gbmformat >= 0, NULL);

  usage = GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT;

  bo = gbm_bo_create (priv->gbmhandle, GST_VIDEO_INFO_WIDTH (&priv->info),
       GST_VIDEO_INFO_HEIGHT (&priv->info), gbmformat, usage);
  if (NULL == bo) {
    GST_ERROR_OBJECT (vpool, "Failed to allocate GBM memory!");
    return FALSE;
  }

  fd = gbm_bo_get_fd (bo);

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd), bo);

  GST_DEBUG_OBJECT (vpool, "Allocated GBM memory FD %d", fd);

  // Wrap the allocated FD in FD backed allocator.
  return gst_fd_allocator_alloc (priv->allocator, fd, priv->info.size,
      GST_FD_MEMORY_FLAG_DONT_CLOSE);
}

static void
gbm_device_free (GstVTransBufferPool * vpool, gint fd)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  GST_DEBUG_OBJECT (vpool, "Closing GBM memory FD %d", fd);

  struct gbm_bo *bo = g_hash_table_lookup (priv->datamap, GINT_TO_POINTER (fd));
  gbm_bo_destroy (bo);

  g_hash_table_remove (priv->datamap, GINT_TO_POINTER (fd));
}

static gboolean
open_ion_device (GstVTransBufferPool * vpool)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  priv->devicefd = open ("/dev/ion", O_RDWR);
  if (priv->devicefd < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to open ION device FD!");
    return FALSE;
  }

#ifndef TARGET_ION_ABI_VERSION
  priv->datamap = g_hash_table_new (NULL, NULL);
#endif

  GST_INFO_OBJECT (vpool, "Opened ION device FD %d", priv->devicefd);
  return TRUE;
}

static void
close_ion_device (GstVTransBufferPool * vpool)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  if (priv->devicefd >= 0) {
    GST_INFO_OBJECT (vpool, "Closing ION device FD %d", priv->devicefd);
    close (priv->devicefd);
  }

#ifndef TARGET_ION_ABI_VERSION
  g_hash_table_destroy (priv->datamap);
#endif
}

static GstMemory *
ion_device_alloc (GstVTransBufferPool * vpool)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;
  gint result = 0, fd = -1;

#ifndef TARGET_ION_ABI_VERSION
  struct ion_fd_data fd_data;
#endif
  struct ion_allocation_data alloc_data;

  alloc_data.len = GST_VIDEO_INFO_SIZE (&priv->info);
#ifndef TARGET_ION_ABI_VERSION
  alloc_data.align = DEFAULT_ION_ALIGNMENT;
#endif
  alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc_data.flags = 0;

  result = ioctl (priv->devicefd, ION_IOC_ALLOC, &alloc_data);
  if (result != 0) {
    GST_ERROR_OBJECT (vpool, "Failed to allocate ION memory!");
    return NULL;
  }

#ifndef TARGET_ION_ABI_VERSION
  fd_data.handle = alloc_data.handle;

  result = ioctl (priv->devicefd, ION_IOC_MAP, &fd_data);
  if (result != 0) {
    GST_ERROR_OBJECT (vpool, "Failed to map memory to FD!");
    ioctl (priv->devicefd, ION_IOC_FREE, &alloc_data.handle);
    return NULL;
  }

  fd = fd_data.fd;

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd),
      GINT_TO_POINTER (alloc_data.handle));
#else
  fd = alloc_data.fd;
#endif

  GST_DEBUG_OBJECT (vpool, "Allocated ION memory FD %d", fd);

  // Wrap the allocated FD in FD backed allocator.
  return gst_fd_allocator_alloc (priv->allocator, fd, priv->info.size,
      GST_FD_MEMORY_FLAG_DONT_CLOSE);
}

static void
ion_device_free (GstVTransBufferPool * vpool, gint fd)
{
  GstVTransBufferPoolPrivate *priv = vpool->priv;
  
  GST_DEBUG_OBJECT (vpool, "Closing ION memory FD %d", fd);

#ifndef TARGET_ION_ABI_VERSION
  ion_user_handle_t handle = GPOINTER_TO_INT (
      g_hash_table_lookup (priv->datamap, GINT_TO_POINTER (fd)));

  if (ioctl (priv->devicefd, ION_IOC_FREE, &handle) < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to free handle for memory FD %d!", fd);
  }

  g_hash_table_remove (priv->datamap, GINT_TO_POINTER (fd));
#endif
  
  close (fd);
}

static const gchar **
vtrans_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_BUFFER_POOL_OPTION_VIDEO_META,
    NULL
  };
  return options;
}

static gboolean
vtrans_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstVTransBufferPool *vpool = GST_VIDEO_TRANS_BUFFER_POOL_CAST (pool);
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  gboolean success;
  GstVideoInfo info;
  GstCaps *caps;
  GstVideoAlignment alignment;
  guint size, minbuffers, maxbuffers;
  GstAllocator *allocator;
  GstAllocationParams params;

  success = gst_buffer_pool_config_get_params (config, &caps, &size,
      &minbuffers, &maxbuffers);

  if (!success) {
    GST_ERROR_OBJECT (vpool, "Invalid configuration!");
    return FALSE;
  } else if (caps == NULL) {
    GST_ERROR_OBJECT (vpool, "Caps missing from configuration");
    return FALSE;
  }

  // Now parse the caps from the configuration.
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vpool, "Failed getting geometry from caps %"
        GST_PTR_FORMAT, caps);
    return FALSE;
  } else if (size < info.size) {
    GST_ERROR_OBJECT (pool, "Provided size is to small for the caps: %u < %"
        G_GSIZE_FORMAT, size, info.size);
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (vpool, "Allocator missing from configuration");
    return FALSE;
  } else if (!GST_IS_FD_ALLOCATOR (allocator)) {
    GST_ERROR_OBJECT (vpool, "Allocator %p is not FD backed!", allocator);
    return FALSE;
  }

  GST_DEBUG_OBJECT (pool, "Video dimensions %dx%d, caps %" GST_PTR_FORMAT,
      info.width, info.height, caps);

  priv->params = params;
  info.size = MAX (size, info.size);
  priv->info = info;

  // Remove cached allocator.
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  priv->allocator = allocator;
  gst_object_ref (priv->allocator);

  // Enable metadata based on configuration of the pool.
  priv->addmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_buffer_pool_config_set_params (config, caps, info.size, minbuffers,
      maxbuffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
vtrans_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstVTransBufferPool *vpool = GST_VIDEO_TRANS_BUFFER_POOL_CAST (pool);
  GstVTransBufferPoolPrivate *priv = vpool->priv;
  GstVideoInfo *info = &priv->info;
  GstMemory *memory = NULL;
  GstBuffer *newbuffer = NULL;

  if (GST_IS_GBM_MEMORY_TYPE (priv->memtype)) {
    memory = gbm_device_alloc (vpool);
  } else if (GST_IS_ION_MEMORY_TYPE (priv->memtype)) {
    memory = ion_device_alloc (vpool);
  }

  if (NULL == memory) {
    GST_WARNING_OBJECT (pool, "Failed to allocate memory!");
    return GST_FLOW_ERROR;
  }

  // Create a GstBuffer.
  newbuffer = gst_buffer_new ();

  // Append the FD backed memory to the newly created GstBuffer.
  gst_buffer_append_memory(newbuffer, memory);

  if (priv->addmeta) {
    GST_DEBUG_OBJECT (vpool, "Adding GstVideoMeta");

    gst_buffer_add_video_meta_full (
        newbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride
    );
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
vtrans_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstVTransBufferPool *vpool = GST_VIDEO_TRANS_BUFFER_POOL_CAST (pool);
  gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

  if (GST_IS_GBM_MEMORY_TYPE (vpool->priv->memtype)) {
    gbm_device_free (vpool, fd);
  } else if (GST_IS_ION_MEMORY_TYPE (vpool->priv->memtype)) {
    ion_device_free (vpool, fd);
  }
  gst_buffer_unref (buffer);
}

static void
gst_vtrans_buffer_pool_finalize (GObject * object)
{
  GstVTransBufferPool *vpool = GST_VIDEO_TRANS_BUFFER_POOL_CAST (object);
  GstVTransBufferPoolPrivate *priv = vpool->priv;

  GST_INFO_OBJECT (vpool, "Finalize video buffer pool %p", vpool);

  if (priv->allocator) {
    GST_INFO_OBJECT (vpool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
  }

  if (GST_IS_GBM_MEMORY_TYPE (priv->memtype)) {
    close_gbm_device (vpool);
  } else if (GST_IS_ION_MEMORY_TYPE (priv->memtype)) {
    close_ion_device (vpool);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vtrans_buffer_pool_class_init (GstVTransBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_vtrans_buffer_pool_finalize;

  pool->get_options = vtrans_buffer_pool_get_options;
  pool->set_config = vtrans_buffer_pool_set_config;
  pool->alloc_buffer = vtrans_buffer_pool_alloc;
  pool->free_buffer = vtrans_buffer_pool_free;

  GST_DEBUG_CATEGORY_INIT (gst_vtrans_pool_debug,
      "video-transform-pool", 0, "video-transform-pool object");
}

static void
gst_vtrans_buffer_pool_init (GstVTransBufferPool * vpool)
{
  vpool->priv = gst_vtrans_buffer_pool_get_instance_private (vpool);
}


GstBufferPool *
gst_vtrans_buffer_pool_new (const gchar * type)
{
  GstVTransBufferPool *vpool;
  gboolean success = FALSE;

  vpool = g_object_new (GST_TYPE_VTRANS_BUFFER_POOL, NULL);

  vpool->priv->memtype = g_quark_from_static_string (type);

  if (GST_IS_GBM_MEMORY_TYPE (vpool->priv->memtype)) {
    GST_INFO_OBJECT (vpool, "Using GBM memory");
    success = open_gbm_device (vpool);
  } else if (GST_IS_ION_MEMORY_TYPE (vpool->priv->memtype)) {
    GST_INFO_OBJECT (vpool, "Using ION memory");
    success = open_ion_device (vpool);
  }

  if (!success) {
    gst_object_unref (vpool);
    return NULL;
  }

  GST_INFO_OBJECT (vpool, "New video buffer pool %p", vpool);
  return GST_BUFFER_POOL_CAST (vpool);
}

const GstVideoInfo *
gst_vtrans_buffer_pool_get_info (GstBufferPool * pool)
{
  GstVTransBufferPool *vpool = GST_VIDEO_TRANS_BUFFER_POOL_CAST (pool);

  g_return_val_if_fail (vpool != NULL, NULL);

  return &vpool->priv->info;
}
