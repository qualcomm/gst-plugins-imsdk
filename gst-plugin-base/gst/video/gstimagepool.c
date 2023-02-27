/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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

#include "gstimagepool.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <gbm.h>
#include <gbm_priv.h>

#ifdef HAVE_MMM_COLOR_FMT_H
#include <display/media/mmm_color_fmt.h>
#else
#include <media/msm_media_info.h>
#define MMM_COLOR_FMT_NV12_UBWC COLOR_FMT_NV12_UBWC
#define MMM_COLOR_FMT_ALIGN MSM_MEDIA_ALIGN
#define MMM_COLOR_FMT_Y_META_STRIDE VENUS_Y_META_STRIDE
#define MMM_COLOR_FMT_Y_META_SCANLINES VENUS_Y_META_SCANLINES
#endif // HAVE_MMM_COLOR_FMT_H

#if defined(HAVE_LINUX_DMA_HEAP_H)
#include <linux/dma-heap.h>
#else
#include <linux/ion.h>
#include <linux/msm_ion.h>
#endif // HAVE_LINUX_DMA_HEAP_H

GST_DEBUG_CATEGORY_STATIC (gst_image_pool_debug);
#define GST_CAT_DEFAULT gst_image_pool_debug

#define GST_IS_GBM_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_IMAGE_BUFFER_POOL_TYPE_GBM))
#define GST_IS_ION_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_IMAGE_BUFFER_POOL_TYPE_ION))

#define DEFAULT_PAGE_ALIGNMENT 4096

struct _GstImageBufferPoolPrivate
{
  GstVideoInfo        info;
  gboolean            addmeta;
  gboolean            isubwc;
  gboolean            keepmapped;

  GstAllocator        *allocator;
  GstAllocationParams params;
  GQuark              memtype;

  // Either ION, DMA or GBM device FD.
  gint                devfd;

  // GBM library handle;
  gpointer            gbmhandle;
  // GBM device handle;
  struct gbm_device   *gbmdevice;

  // Map of data FDs and ION handles on case ION memory is used OR
  // map of data FDs and GBM buffer objects if GBM memory is used.
  GHashTable          *datamap;
  // Mutex for protecting insert/remove from the data map.
  GMutex              lock;

  // GBM library APIs
  struct gbm_device * (*gbm_create_device) (gint fd);
  void (*gbm_device_destroy)(struct gbm_device * gbm);
  struct gbm_bo * (*gbm_bo_create) (struct gbm_device * gbm, guint width,
                                    guint height, guint format, guint flags);
  void (*gbm_bo_destroy) (struct gbm_bo * bo);
  gint (*gbm_bo_get_fd) (struct gbm_bo *bo);
  gint (*gbm_perform) (int operation,...);
};

#define gst_image_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstImageBufferPool, gst_image_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static gint
gst_video_format_to_gbm_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return GBM_FORMAT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return GBM_FORMAT_NV21_ZSL;
    case GST_VIDEO_FORMAT_YUY2:
      return GBM_FORMAT_YCrCb_422_I;
    case GST_VIDEO_FORMAT_UYVY:
      return GBM_FORMAT_UYVY;
    case GST_VIDEO_FORMAT_BGRx:
      return GBM_FORMAT_BGRX8888;
    case GST_VIDEO_FORMAT_BGRA:
      return GBM_FORMAT_BGRA8888;
    case GST_VIDEO_FORMAT_RGBx:
      return GBM_FORMAT_RGBX8888;
    case GST_VIDEO_FORMAT_xBGR:
      return GBM_FORMAT_XBGR8888;
    case GST_VIDEO_FORMAT_RGBA:
      return GBM_FORMAT_RGBA8888;
    case GST_VIDEO_FORMAT_ABGR:
      return GBM_FORMAT_ABGR8888;
    case GST_VIDEO_FORMAT_RGB:
      return GBM_FORMAT_RGB888;
    case GST_VIDEO_FORMAT_BGR:
      return GBM_FORMAT_BGR888;
    case GST_VIDEO_FORMAT_BGR16:
      return GBM_FORMAT_BGR565;
    case GST_VIDEO_FORMAT_RGB16:
      return GBM_FORMAT_RGB565;
    default:
      GST_ERROR ("Unsupported format %s!", gst_video_format_to_string (format));
  }
  return -1;
}

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

static void
close_gbm_device (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;

  if (priv->gbmdevice != NULL) {
    GST_INFO_OBJECT (vpool, "Closing GBM device %p", priv->gbmdevice);
    priv->gbm_device_destroy (priv->gbmdevice);
  }

  if (priv->devfd >= 0) {
    GST_INFO_OBJECT (vpool, "Closing GBM device FD %d", priv->devfd);
    close (priv->devfd);
  }

  if (priv->gbmhandle != NULL) {
    GST_INFO_OBJECT (vpool, "Closing GBM handle %p", priv->gbmhandle);
    dlclose (priv->gbmhandle);
  }

  g_hash_table_destroy (priv->datamap);
}

static gboolean
open_gbm_device (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;
  gboolean success = TRUE;

  // Load GBM library.
  priv->gbmhandle = dlopen("libgbm.so", RTLD_NOW);
  if (NULL == priv->gbmhandle) {
    GST_ERROR ("Failed to open GBM library, error: %s!", dlerror());
    return FALSE;
  }

  // Load GBM library symbols.
  success &= load_symbol ((gpointer*)&priv->gbm_create_device, priv->gbmhandle,
      "gbm_create_device");
  success &= load_symbol ((gpointer*)&priv->gbm_device_destroy, priv->gbmhandle,
      "gbm_device_destroy");
  success &= load_symbol ((gpointer*)&priv->gbm_bo_create, priv->gbmhandle,
      "gbm_bo_create");
  success &= load_symbol ((gpointer*)&priv->gbm_bo_destroy, priv->gbmhandle,
      "gbm_bo_destroy");
  success &= load_symbol ((gpointer*)&priv->gbm_bo_get_fd, priv->gbmhandle,
      "gbm_bo_get_fd");
  success &= load_symbol ((gpointer*)&priv->gbm_perform, priv->gbmhandle,
      "gbm_perform");

  if (!success) {
    close_gbm_device (vpool);
    return FALSE;
  }

  GST_INFO_OBJECT (vpool, "Open /dev/dma_heap/qcom,system");
  priv->devfd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  if (priv->devfd < 0) {
    GST_WARNING_OBJECT (vpool, "Falling back to /dev/ion");
    priv->devfd = open ("/dev/ion", O_RDONLY | O_CLOEXEC);
  }

  if (priv->devfd < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to open GBM device FD!");
    close_gbm_device (vpool);
    return FALSE;
  }

  GST_INFO_OBJECT (vpool, "Opened GBM device FD %d", priv->devfd);

  priv->gbmdevice = priv->gbm_create_device (priv->devfd);
  if (NULL == priv->gbmdevice) {
    GST_ERROR_OBJECT (vpool, "Failed to create GBM device!");
    close_gbm_device (vpool);
    return FALSE;
  }

  priv->datamap = g_hash_table_new (NULL, NULL);

  GST_INFO_OBJECT (vpool, "Created GBM handle %p", priv->gbmdevice);
  return TRUE;
}

static GstMemory *
gbm_device_alloc (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;
  struct gbm_bo *bo = NULL;
  GstFdMemoryFlags flags = GST_FD_MEMORY_FLAG_DONT_CLOSE;
  gint fd, format, usage = 0;

  format = gst_video_format_to_gbm_format (GST_VIDEO_INFO_FORMAT (&priv->info));
  g_return_val_if_fail (format >= 0, NULL);

  usage |= priv->isubwc ? GBM_BO_USAGE_UBWC_ALIGNED_QTI : 0;

  bo = priv->gbm_bo_create (priv->gbmdevice, GST_VIDEO_INFO_WIDTH (&priv->info),
       GST_VIDEO_INFO_HEIGHT (&priv->info), format, usage);
  if (NULL == bo) {
    GST_ERROR_OBJECT (vpool, "Failed to allocate GBM memory!");
    return NULL;
  }

  fd = priv->gbm_bo_get_fd (bo);

  g_mutex_lock (&priv->lock);
  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd), bo);
  g_mutex_unlock (&priv->lock);

  GST_DEBUG_OBJECT (vpool, "Allocated GBM memory FD %d", fd);

  if (priv->keepmapped)
    flags |= GST_FD_MEMORY_FLAG_KEEP_MAPPED;

  return gst_fd_allocator_alloc (priv->allocator, fd, priv->info.size, flags);
}

static void
gbm_device_free (GstImageBufferPool * vpool, gint fd)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;

  GST_DEBUG_OBJECT (vpool, "Closing GBM memory FD %d", fd);

  g_mutex_lock (&priv->lock);

  struct gbm_bo *bo = g_hash_table_lookup (priv->datamap, GINT_TO_POINTER (fd));
  g_hash_table_remove (priv->datamap, GINT_TO_POINTER (fd));

  g_mutex_unlock (&priv->lock);

  priv->gbm_bo_destroy (bo);
}

static gboolean
open_ion_device (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;

  GST_INFO_OBJECT (vpool, "Open /dev/dma_heap/qcom,system");
  priv->devfd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  if (priv->devfd < 0) {
    GST_WARNING_OBJECT (vpool, "Falling back to /dev/ion");
    priv->devfd = open ("/dev/ion", O_RDONLY | O_CLOEXEC);
  }

  if (priv->devfd < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to open ION device FD!");
    return FALSE;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  priv->datamap = g_hash_table_new (NULL, NULL);
#endif // TARGET_ION_ABI_VERSION

  GST_INFO_OBJECT (vpool, "Opened ION device FD %d", priv->devfd);
  return TRUE;
}

static void
close_ion_device (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;

  if (priv->devfd >= 0) {
    GST_INFO_OBJECT (vpool, "Closing ION device FD %d", priv->devfd);
    close (priv->devfd);
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  g_hash_table_destroy (priv->datamap);
#endif // TARGET_ION_ABI_VERSION
}

static GstMemory *
ion_device_alloc (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;
  GstFdMemoryFlags flags = GST_FD_MEMORY_FLAG_DONT_CLOSE;
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
  alloc_data.len = GST_VIDEO_INFO_SIZE (&priv->info);

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
    GST_ERROR_OBJECT (vpool, "Failed to allocate ION memory!");
    return NULL;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  fd_data.handle = alloc_data.handle;

  result = ioctl (priv->devfd, ION_IOC_MAP, &fd_data);
  if (result != 0) {
    GST_ERROR_OBJECT (vpool, "Failed to map memory to FD!");
    ioctl (priv->devfd, ION_IOC_FREE, &alloc_data.handle);
    return NULL;
  }

  fd = fd_data.fd;

  g_hash_table_insert (priv->datamap, GINT_TO_POINTER (fd),
      GSIZE_TO_POINTER (alloc_data.handle));
#else
  fd = alloc_data.fd;
#endif // TARGET_ION_ABI_VERSION

  GST_DEBUG_OBJECT (vpool, "Allocated ION memory FD %d", fd);

  if (priv->keepmapped)
    flags |= GST_FD_MEMORY_FLAG_KEEP_MAPPED;

  // Wrap the allocated FD in FD backed allocator.
  return gst_fd_allocator_alloc (priv->allocator, fd, priv->info.size, flags);
}

static void
ion_device_free (GstImageBufferPool * vpool, gint fd)
{
  GST_DEBUG_OBJECT (vpool, "Closing ION memory FD %d", fd);

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  ion_user_handle_t handle = GPOINTER_TO_SIZE (
      g_hash_table_lookup (vpool->priv->datamap, GINT_TO_POINTER (fd)));

  if (ioctl (vpool->priv->devfd, ION_IOC_FREE, &handle) < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to free handle for memory FD %d!", fd);
  }

  g_hash_table_remove (vpool->priv->datamap, GINT_TO_POINTER (fd));
#endif // TARGET_ION_ABI_VERSION

  close (fd);
}

static const gchar **
gst_image_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_IMAGE_BUFFER_POOL_OPTION_UBWC_MODE,
    GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED,
    NULL
  };
  return options;
}

static gboolean
gst_image_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  gboolean success;
  GstVideoInfo info;
  GstCaps *caps;
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
    GST_ERROR_OBJECT (vpool, "Allocator missing from configuration!");
    return FALSE;
  } else if (NULL == allocator) {
    // No allocator set in configuration, create default FD allocator.
    if (NULL == (allocator = gst_fd_allocator_new ())) {
      GST_ERROR_OBJECT (vpool, "Failed to create FD allocator!");
      return FALSE;
    }
  }

  if (!GST_IS_FD_ALLOCATOR (allocator)) {
     GST_ERROR_OBJECT (vpool, "Allocator %p is not FD backed!", allocator);
     return FALSE;
  }

  GST_DEBUG_OBJECT (pool, "Video dimensions %dx%d, caps %" GST_PTR_FORMAT,
      info.width, info.height, caps);

  priv->params = params;
  info.size = MAX (size, info.size);
  priv->info = info;

  // Check whether we should allocate ubwc buffers.
  priv->isubwc = gst_buffer_pool_config_has_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_UBWC_MODE);

  // Check whether we should keep buffer memory mapped.
  priv->keepmapped = gst_buffer_pool_config_has_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  // GBM library has its own alignment for the allocated buffers so update
  // the size, stride and offset for the buffer planes in the video info.
  if (GST_IS_GBM_MEMORY_TYPE (vpool->priv->memtype)) {
    struct gbm_buf_info bufinfo = { 0, };
    guint stride, scanline, usage = 0;

    bufinfo.width = GST_VIDEO_INFO_WIDTH (&priv->info);
    bufinfo.height = GST_VIDEO_INFO_HEIGHT (&priv->info);
    bufinfo.format = gst_video_format_to_gbm_format (
        GST_VIDEO_INFO_FORMAT (&priv->info));

    usage |= priv->isubwc ? GBM_BO_USAGE_UBWC_ALIGNED_QTI : 0;

    priv->gbm_perform (GBM_PERFORM_GET_BUFFER_SIZE_DIMENSIONS, &bufinfo,
        usage, &stride, &scanline, &size);

    GST_VIDEO_INFO_PLANE_STRIDE (&priv->info, 0) = stride;
    GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 0) = 0;

    // TODO: Workaroud for GBM incorect stride
    if (bufinfo.format == GBM_FORMAT_RGB888)
      GST_VIDEO_INFO_PLANE_STRIDE (&priv->info, 0) *= 3;

    // Check for a second plane and fill its stride and offset.
    if (GST_VIDEO_INFO_N_PLANES (&priv->info) >= 2) {
      GST_VIDEO_INFO_PLANE_STRIDE (&priv->info, 1) = stride;
      GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 1) = stride * scanline;

      // For UBWC formats there is very specific UV plane offset.
      if (priv->isubwc && (bufinfo.format = GBM_FORMAT_NV12)) {
        guint metastride, metascanline;

        metastride = MMM_COLOR_FMT_Y_META_STRIDE (MMM_COLOR_FMT_NV12_UBWC, bufinfo.width);
        metascanline = MMM_COLOR_FMT_Y_META_SCANLINES (MMM_COLOR_FMT_NV12_UBWC, bufinfo.height);

        GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 1) =
            MMM_COLOR_FMT_ALIGN (stride * scanline, DEFAULT_PAGE_ALIGNMENT) +
            MMM_COLOR_FMT_ALIGN (metastride * metascanline, DEFAULT_PAGE_ALIGNMENT);
      }
    }

    priv->info.size = MAX (size, priv->info.size);
  }

  // Remove cached allocator.
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  priv->allocator = allocator;
  gst_object_ref (priv->allocator);

  // Enable metadata based on configuration of the pool.
  priv->addmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_buffer_pool_config_set_params (config, caps, priv->info.size, minbuffers,
      maxbuffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_image_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;
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
gst_image_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

  if (GST_IS_GBM_MEMORY_TYPE (vpool->priv->memtype)) {
    gbm_device_free (vpool, fd);
  } else if (GST_IS_ION_MEMORY_TYPE (vpool->priv->memtype)) {
    ion_device_free (vpool, fd);
  }
  gst_buffer_unref (buffer);
}

static void
gst_image_buffer_pool_reset (GstBufferPool * pool, GstBuffer * buffer)
{
  GstImageBufferPoolPrivate *priv = GST_IMAGE_BUFFER_POOL (pool)->priv;

  // Resize the buffer to the original size otherwise it will be discarded
  // due to the mismatch during the default implementation of release_buffer.
  gst_buffer_resize (buffer, 0, priv->info.size);

  GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (pool, buffer);
}

static void
gst_image_buffer_pool_finalize (GObject * object)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (object);
  GstImageBufferPoolPrivate *priv = vpool->priv;

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

  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_image_buffer_pool_class_init (GstImageBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_image_buffer_pool_finalize;

  pool->get_options = gst_image_buffer_pool_get_options;
  pool->set_config = gst_image_buffer_pool_set_config;
  pool->alloc_buffer = gst_image_buffer_pool_alloc;
  pool->free_buffer = gst_image_buffer_pool_free;
  pool->reset_buffer = gst_image_buffer_pool_reset;

  GST_DEBUG_CATEGORY_INIT (gst_image_pool_debug, "image-pool", 0,
      "image-pool object");
}

static void
gst_image_buffer_pool_init (GstImageBufferPool * vpool)
{
  vpool->priv = gst_image_buffer_pool_get_instance_private (vpool);
  vpool->priv->devfd = -1;

  g_mutex_init (&vpool->priv->lock);
}


GstBufferPool *
gst_image_buffer_pool_new (const gchar * type)
{
  GstImageBufferPool *vpool;
  gboolean success = FALSE;

  vpool = g_object_new (GST_TYPE_IMAGE_BUFFER_POOL, NULL);

  vpool->priv->memtype = g_quark_from_string (type);

  if (GST_IS_GBM_MEMORY_TYPE (vpool->priv->memtype)) {
    GST_INFO_OBJECT (vpool, "Using GBM memory");
    success = open_gbm_device (vpool);
  } else if (GST_IS_ION_MEMORY_TYPE (vpool->priv->memtype)) {
    GST_INFO_OBJECT (vpool, "Using ION memory");
    success = open_ion_device (vpool);
  } else {
    GST_ERROR_OBJECT (vpool, "Invalid memory type %s!",
        g_quark_to_string (vpool->priv->memtype));
    success = FALSE;
  }

  if (!success) {
    gst_object_unref (vpool);
    return NULL;
  }

  GST_INFO_OBJECT (vpool, "New video buffer pool %p", vpool);
  return GST_BUFFER_POOL_CAST (vpool);
}

const GstVideoInfo *
gst_image_buffer_pool_get_info (GstBufferPool * pool)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);

  g_return_val_if_fail (vpool != NULL, NULL);

  return &vpool->priv->info;
}
