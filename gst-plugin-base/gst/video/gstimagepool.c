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
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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
#ifdef HAVE_GBM_PRIV_H
#include <gbm_priv.h>
#endif // HAVE_GBM_PRIV_H

#ifdef HAVE_MMM_COLOR_FMT_H
#include <display/media/mmm_color_fmt.h>
#elif defined(HAVE_MSM_MEDIA_INFO_H)
#include <media/msm_media_info.h>
#define MMM_COLOR_FMT_NV12_UBWC COLOR_FMT_NV12_UBWC
#define MMM_COLOR_FMT_NV12_BPP10_UBWC COLOR_FMT_NV12_BPP10_UBWC
#define MMM_COLOR_FMT_P010_UBWC COLOR_FMT_P010_UBWC
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

#define DEFAULT_PAGE_ALIGNMENT 4096

struct _GstImageBufferPoolPrivate
{
  GstVideoInfo        info;
  GstVideoAlignment   align;

  gboolean            addmeta;
  GstFdMemoryFlags    memflags;

  GstAllocator        *allocator;
  GstAllocationParams params;

  // GBM device FD.
  gint                gbmfd;
  // GBM library handle;
  gpointer            gbmhandle;
  // GBM device handle;
  struct gbm_device   *gbmdevice;

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
#ifdef HAVE_GBM_PRIV_H
    case GST_VIDEO_FORMAT_NV12:
      return GBM_FORMAT_NV12;
    case GST_VIDEO_FORMAT_NV12_Q08C:
      return GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC;
    case GST_VIDEO_FORMAT_NV21:
      return GBM_FORMAT_NV21_ZSL;
    case GST_VIDEO_FORMAT_YUY2:
      return GBM_FORMAT_YCrCb_422_I;
    case GST_VIDEO_FORMAT_UYVY:
      return GBM_FORMAT_UYVY;
    case GST_VIDEO_FORMAT_P010_10LE:
      return GBM_FORMAT_YCbCr_420_P010_VENUS;
    case GST_VIDEO_FORMAT_NV12_Q10LE32C:
      return GBM_FORMAT_YCbCr_420_TP10_UBWC;
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
#if defined(GBM_FORMAT_R8)
    case GST_VIDEO_FORMAT_GRAY8:
      return GBM_FORMAT_R8;
#endif // GBM_FORMAT_R8
#endif // HAVE_GBM_PRIV_H
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
gbm_device_close (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;

  if (priv->gbmdevice != NULL) {
    GST_INFO_OBJECT (vpool, "Closing GBM device %p", priv->gbmdevice);
    priv->gbm_device_destroy (priv->gbmdevice);
  }

  if (priv->gbmfd >= 0) {
    GST_INFO_OBJECT (vpool, "Closing GBM device FD %d", priv->gbmfd);
    close (priv->gbmfd);
  }

  if (priv->gbmhandle != NULL) {
    GST_INFO_OBJECT (vpool, "Closing GBM handle %p", priv->gbmhandle);
    dlclose (priv->gbmhandle);
  }

  if (priv->datamap != NULL)
    g_hash_table_destroy (priv->datamap);
}

static gboolean
gbm_device_open (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;
  gboolean success = TRUE;
  guint32 dubplicate = 0;

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
    gbm_device_close (vpool);
    return FALSE;
  }

  GST_INFO_OBJECT (vpool, "Open /dev/dma_heap/qcom,system");
  priv->gbmfd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  if (priv->gbmfd < 0) {
    GST_WARNING_OBJECT (vpool, "Failed to open /dev/dma_heap/qcom,system, "
        "error: %s! Falling back to /dev/dma_heap/system", g_strerror (errno));
    priv->gbmfd = open ("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
  }

  if (priv->gbmfd < 0) {
    GST_WARNING_OBJECT (vpool, "Falling back to /dev/ion");
    priv->gbmfd = open ("/dev/ion", O_RDONLY | O_CLOEXEC);
  }

  if (priv->gbmfd < 0) {
    GST_ERROR_OBJECT (vpool, "Failed to open GBM device FD!");
    gbm_device_close (vpool);
    return FALSE;
  }

  GST_INFO_OBJECT (vpool, "Opened GBM device FD %d", priv->gbmfd);

  priv->gbmdevice = priv->gbm_create_device (priv->gbmfd);
  if (NULL == priv->gbmdevice) {
    GST_ERROR_OBJECT (vpool, "Failed to create GBM device!");
    gbm_device_close (vpool);
    return FALSE;
  }

#if defined(GBM_PERFORM_GET_FD_WITH_NEW)
  // Check if the FD returned at gbm_bo_get_fd() is duplicated.
  priv->gbm_perform (GBM_PERFORM_GET_FD_WITH_NEW, &dubplicate);
#endif // GBM_PERFORM_GET_FD_WITH_NEW

  // If the BO FD is duplicated then when buffer is free it will be closed.
  priv->memflags = (dubplicate == 0) ? GST_FD_MEMORY_FLAG_DONT_CLOSE : 0;

  priv->datamap = g_hash_table_new (NULL, NULL);

  GST_INFO_OBJECT (vpool, "Created GBM handle %p", priv->gbmdevice);
  return TRUE;
}

static GstMemory *
gbm_device_alloc (GstImageBufferPool * vpool)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;
  struct gbm_bo *bo = NULL;
  gint fd = -1, format = 0, usage = 0;

  format = gst_video_format_to_gbm_format (GST_VIDEO_INFO_FORMAT (&priv->info));
  g_return_val_if_fail (format >= 0, NULL);

#ifdef HAVE_GBM_PRIV_H
  if (GST_VIDEO_INFO_FORMAT (&priv->info) == GST_VIDEO_FORMAT_P010_10LE) {
    usage |= GBM_BO_USAGE_10BIT_QTI;
  } else if (GST_VIDEO_INFO_FORMAT (&priv->info) == GST_VIDEO_FORMAT_NV12_Q08C) {
    usage |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
  } else if (GST_VIDEO_INFO_FORMAT (&priv->info) == GST_VIDEO_FORMAT_NV12_Q10LE32C) {
    usage |= GBM_BO_USAGE_10BIT_TP_QTI;
    usage |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
  }
#endif // HAVE_GBM_PRIV_H

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

  return gst_fd_allocator_alloc (priv->allocator, fd, priv->info.size,
      priv->memflags);
}

static void
gbm_device_free (GstImageBufferPool * vpool, gint fd)
{
  GstImageBufferPoolPrivate *priv = vpool->priv;
  struct gbm_bo *bo = NULL;

  GST_DEBUG_OBJECT (vpool, "Closing GBM memory FD %d", fd);

  g_mutex_lock (&priv->lock);

  bo = g_hash_table_lookup (priv->datamap, GINT_TO_POINTER (fd));
  g_hash_table_remove (priv->datamap, GINT_TO_POINTER (fd));

  g_mutex_unlock (&priv->lock);

  priv->gbm_bo_destroy (bo);
}

static const gchar **
gst_image_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT,
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
  GstCaps *caps = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info = {0,};
  GstAllocationParams params = {0,};
  guint size = 0, minbuffers = 0, maxbuffers = 0;
  gboolean success = FALSE, keepmapped = FALSE, need_alignment = FALSE;

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

  // Check whether we should keep buffer memory mapped.
  keepmapped = gst_buffer_pool_config_has_option (config,
    GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (keepmapped)
    priv->memflags |= GST_FD_MEMORY_FLAG_KEEP_MAPPED;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (vpool, "Allocator missing from configuration!");
    return FALSE;
  } else if (NULL == allocator) {
    // No allocator set in configuration, create default QTI allocator.
    if (NULL == (allocator = gst_qti_allocator_new (priv->memflags))) {
      GST_ERROR_OBJECT (vpool, "Failed to create QTI allocator!");
      return FALSE;
    }
  }

  if (!(GST_IS_FD_ALLOCATOR (allocator) || GST_IS_QTI_ALLOCATOR (allocator))) {
     GST_ERROR_OBJECT (vpool, "Allocator %p is not FD backed!", allocator);
     return FALSE;
  }

  GST_DEBUG_OBJECT (pool, "Video dimensions %dx%d, caps %" GST_PTR_FORMAT,
      info.width, info.height, caps);

  // Enable metadata based on configuration of the pool.
  priv->addmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (need_alignment && priv->addmeta) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->align);

    if (!gst_video_info_align (&info, &priv->align)) {
      GST_ERROR_OBJECT (vpool, "Failed to align video info!");
      return FALSE;
    }

    gst_buffer_pool_config_set_video_alignment (config, &priv->align);
  }

  priv->params = params;
  info.size = MAX (size, info.size);
  priv->info = info;

  // Allocate GBM memory when the allocator is FD backed but not QTI allocator.
  if (!GST_IS_QTI_ALLOCATOR (allocator) && !gbm_device_open (vpool)) {
    GST_ERROR_OBJECT (vpool, "Failed to open GBM device!");
    return FALSE;
  }

#ifdef HAVE_GBM_PRIV_H
  // GBM library has its own alignment for the allocated buffers so update
  // the size, stride and offset for the buffer planes in the video info.
  // Use only if the allocator is not a QTI allocator.
  if (!GST_IS_QTI_ALLOCATOR (allocator)) {
    struct gbm_buf_info bufinfo = { 0, };
    guint stride, scanline, usage = 0;

    bufinfo.width = GST_VIDEO_INFO_WIDTH (&priv->info);
    bufinfo.height = GST_VIDEO_INFO_HEIGHT (&priv->info);
    bufinfo.format = gst_video_format_to_gbm_format (
        GST_VIDEO_INFO_FORMAT (&priv->info));

    if (GST_VIDEO_INFO_FORMAT (&priv->info) == GST_VIDEO_FORMAT_P010_10LE) {
      usage |= GBM_BO_USAGE_10BIT_QTI;
    } else if (GST_VIDEO_INFO_FORMAT (&priv->info) == GST_VIDEO_FORMAT_NV12_Q10LE32C) {
      usage |= GBM_BO_USAGE_10BIT_TP_QTI;
      usage |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
    } else if (GST_VIDEO_INFO_FORMAT (&priv->info) == GST_VIDEO_FORMAT_NV12_Q08C) {
      usage |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
    }

    priv->gbm_perform (GBM_PERFORM_GET_BUFFER_STRIDE_SCANLINE_SIZE, &bufinfo,
        usage, &stride, &scanline, &size);

    GST_VIDEO_INFO_PLANE_STRIDE (&priv->info, 0) = stride;
    GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 0) = 0;

    // Check for a second plane and fill its stride and offset.
    if (GST_VIDEO_INFO_N_PLANES (&priv->info) >= 2) {
      GST_VIDEO_INFO_PLANE_STRIDE (&priv->info, 1) = stride;
      GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 1) = stride * scanline;

      // For UBWC formats there is very specific UV plane offset.
      if (bufinfo.format == GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC) {
        guint metastride, metascanline;

        metastride = MMM_COLOR_FMT_Y_META_STRIDE (
            MMM_COLOR_FMT_NV12_UBWC, bufinfo.width);
        metascanline = MMM_COLOR_FMT_Y_META_SCANLINES (
            MMM_COLOR_FMT_NV12_UBWC, bufinfo.height);

        GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 1) =
            MMM_COLOR_FMT_ALIGN (stride * scanline, DEFAULT_PAGE_ALIGNMENT) +
            MMM_COLOR_FMT_ALIGN (metastride * metascanline, DEFAULT_PAGE_ALIGNMENT);
      } else if (bufinfo.format == GBM_FORMAT_YCbCr_420_TP10_UBWC) {
        guint metastride, metascanline;

        metastride = MMM_COLOR_FMT_Y_META_STRIDE (
            MMM_COLOR_FMT_NV12_BPP10_UBWC, bufinfo.width);
        metascanline = MMM_COLOR_FMT_Y_META_SCANLINES (
            MMM_COLOR_FMT_NV12_BPP10_UBWC, bufinfo.height);

        GST_VIDEO_INFO_PLANE_OFFSET (&priv->info, 1) =
            MMM_COLOR_FMT_ALIGN (stride * scanline, DEFAULT_PAGE_ALIGNMENT) +
            MMM_COLOR_FMT_ALIGN (metastride * metascanline, DEFAULT_PAGE_ALIGNMENT);
      }
    }

    priv->info.size = MAX (size, priv->info.size);
  }
#endif // HAVE_GBM_PRIV_H

  // Remove cached allocator and take the new one.
  gst_clear_object (&(priv->allocator));
  priv->allocator = gst_object_ref (allocator);

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

  if (GST_IS_QTI_ALLOCATOR (priv->allocator)) {
    newbuffer = gst_buffer_new_allocate (priv->allocator, info->size,
        &priv->params);

    if (newbuffer == NULL) {
      GST_ERROR_OBJECT (vpool, "Failed to allocate new buffer");
      return GST_FLOW_ERROR;
    }
  } else {
    if ((memory = gbm_device_alloc (vpool)) == NULL) {
      GST_WARNING_OBJECT (pool, "Failed to allocate memory!");
      return GST_FLOW_ERROR;
    }

    // Create a GstBuffer and Append the FD backed memory to it.
    newbuffer = gst_buffer_new ();
    gst_buffer_append_memory(newbuffer, memory);
  }

  if (priv->addmeta) {
    GstVideoMeta *vmeta = NULL;

    GST_DEBUG_OBJECT (vpool, "Adding GstVideoMeta");

    vmeta = gst_buffer_add_video_meta_full (
        newbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride
    );

    gst_video_meta_set_alignment (vmeta, priv->align);
  }

  // Initially map the buffer
  // If KEEP_MAPPED flag is set do initially map the buffer with READ and WRITE
  // access. This will solve the issue where if someone map the buffer with
  // READ only access at the begining after that it cannot be mapped with
  // WRITE access.
  // TODO: Remove once versions 1.16 and below are no longer supported.
  if (priv->memflags & GST_FD_MEMORY_FLAG_KEEP_MAPPED) {
    GstMapInfo map;

    GST_DEBUG_OBJECT (vpool, "Initially map the buffer");
    if (!gst_buffer_map (newbuffer, &map, GST_MAP_READWRITE)) {
      GST_ERROR ("Failed to map GST buffer!");
      return GST_FLOW_ERROR;
    }
    gst_buffer_unmap (newbuffer, &map);
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
gst_image_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  if (!GST_IS_QTI_ALLOCATOR (priv->allocator)) {
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));
    gbm_device_free (vpool, fd);
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

static gboolean
gst_image_buffer_pool_start (GstBufferPool * pool)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  if (GST_IS_QTI_ALLOCATOR (priv->allocator)) {
    GstStructure *config = NULL;
    guint maxbuffers = 0;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, NULL, NULL, &maxbuffers);
    gst_structure_free (config);

    gst_qti_allocator_start (GST_QTI_ALLOCATOR (priv->allocator), maxbuffers);
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
}

static gboolean
gst_image_buffer_pool_stop (GstBufferPool * pool)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  if (GST_IS_QTI_ALLOCATOR (priv->allocator))
    gst_qti_allocator_stop (GST_QTI_ALLOCATOR (priv->allocator));

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

static void
gst_image_buffer_pool_finalize (GObject * object)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (object);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  GST_INFO_OBJECT (vpool, "Finalize video buffer pool %p", vpool);

  if (!GST_IS_QTI_ALLOCATOR (priv->allocator))
    gbm_device_close (vpool);

  if (priv->allocator) {
    GST_INFO_OBJECT (vpool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
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
  pool->start = gst_image_buffer_pool_start;
  pool->stop = gst_image_buffer_pool_stop;

  GST_DEBUG_CATEGORY_INIT (gst_image_pool_debug, "image-pool", 0,
      "image-pool object");
}

static void
gst_image_buffer_pool_init (GstImageBufferPool * vpool)
{
  vpool->priv = gst_image_buffer_pool_get_instance_private (vpool);
  vpool->priv->gbmfd = -1;
  vpool->priv->memflags = GST_FD_MEMORY_FLAG_DONT_CLOSE;

  gst_video_alignment_reset (&vpool->priv->align);
  g_mutex_init (&vpool->priv->lock);
}


GstBufferPool *
gst_image_buffer_pool_new ()
{
  GstImageBufferPool *vpool;

  vpool = g_object_new (GST_TYPE_IMAGE_BUFFER_POOL, NULL);
  g_return_val_if_fail (vpool != NULL, NULL);

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
