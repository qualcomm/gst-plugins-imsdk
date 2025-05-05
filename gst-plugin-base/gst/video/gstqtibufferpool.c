/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqtibufferpool.h"

#include <gst/video/gstvideometa.h>

#include <gst/allocators/gstqtiallocator.h>

GST_DEBUG_CATEGORY_STATIC (gst_qti_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_qti_buffer_pool_debug

struct _GstQtiBufferPoolPrivate
{
  GstAllocator        *allocator;
  GstAllocationParams params;

  GstVideoInfo        vinfo;
  gboolean            add_vmeta;

  GstVideoAlignment   align;
};

#define parent_class gst_qti_buffer_pool_parent_class

G_DEFINE_TYPE_WITH_PRIVATE (GstQtiBufferPool, gst_qti_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

static gboolean
gst_qti_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstQtiBufferPool *qpool = GST_QTI_BUFFER_POOL_CAST (pool);
  GstQtiBufferPoolPrivate *priv = qpool->priv;
  GstCaps *caps = NULL;
  GstVideoInfo info;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  gboolean need_alignment = FALSE;
  guint size, min_buffers, max_buffers;
  guint idx;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
      &max_buffers)) {
    GST_WARNING_OBJECT (qpool, "Config get params failed");
    return FALSE;
  }

  if (!caps) {
    GST_WARNING_OBJECT (qpool, "Caps in config are empty");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (qpool, "Failed to get video info from caps");
    return FALSE;
  }

  gst_buffer_pool_config_get_allocator (config, &allocator, &params);
  priv->params = params;

  if (allocator) {
    gst_clear_object (&priv->allocator);
    priv->allocator = gst_object_ref (allocator);
  } else {
    GST_ERROR_OBJECT (qpool, "No allocator set in pool config");
    return FALSE;
  }

  priv->add_vmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);
  need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (need_alignment && priv->add_vmeta) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->align);

    if (!gst_video_info_align (&info, &priv->align)) {
      GST_WARNING_OBJECT (qpool, "Failed to align");
      return FALSE;
    }

    gst_buffer_pool_config_set_video_alignment (config, &priv->align);
  }

  info.size = MAX (size, info.size);

  GST_LOG_OBJECT (qpool, "Configured format: %s, width: %d, height: %d, size: %"
      G_GSIZE_FORMAT, gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&info)),
      GST_VIDEO_INFO_WIDTH (&info), GST_VIDEO_INFO_HEIGHT (&info),
      GST_VIDEO_INFO_SIZE (&info));

  for (idx = 0; idx < info.finfo->n_planes; idx++) {
    GST_LOG_OBJECT (qpool, "Configuration for plane %d stride: %d, offset: %"
        G_GSIZE_FORMAT, idx, GST_VIDEO_INFO_PLANE_STRIDE (&info, idx),
        GST_VIDEO_INFO_PLANE_OFFSET (&info, idx));
  }

  gst_buffer_pool_config_set_params (config, caps, info.size, min_buffers,
      max_buffers);

  priv->vinfo = info;

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_qti_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstQtiBufferPool *qpool;
  GstQtiBufferPoolPrivate *priv;
  GstVideoInfo *info;

  qpool = GST_QTI_BUFFER_POOL(pool);
  priv = qpool->priv;
  info = &priv->vinfo;

  *buffer =
      gst_buffer_new_allocate (priv->allocator, info->size, &priv->params);
  if (*buffer == NULL) {
    GST_ERROR_OBJECT (qpool, "Failed to allocate new buffer");
    return GST_FLOW_ERROR;
  }

  if (priv->add_vmeta) {
    GstStructure *config = gst_buffer_pool_get_config (pool);
    GstVideoMeta *vmeta;

    GST_DEBUG_OBJECT (pool, "Adding GstVideoMeta");

    vmeta = gst_buffer_add_video_meta_full (*buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);

    gst_video_meta_set_alignment (vmeta, priv->align);

    if (config)
      gst_structure_free (config);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_qti_buffer_pool_start (GstBufferPool * pool)
{
  GstQtiBufferPool *qpool = GST_QTI_BUFFER_POOL (pool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstQtiBufferPoolPrivate *priv = qpool->priv;
  GstStructure *config = NULL;
  guint size, max_buffers;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (qpool, "Starting pool");

  config = gst_buffer_pool_get_config (pool);
  success = gst_buffer_pool_config_get_params (config, NULL, &size, NULL,
      &max_buffers);
  if (!success) {
    GST_ERROR_OBJECT (qpool, "Failed to get config from pool");
    gst_structure_free (config);
    return FALSE;
  }

  if (GST_IS_QTI_ALLOCATOR (priv->allocator)) {
    GstQtiAllocator *qalloc = GST_QTI_ALLOCATOR_CAST (priv->allocator);

    gst_qti_allocator_start (qalloc, max_buffers);
  }

  if (!pclass->start (pool)) {
    GST_ERROR_OBJECT (qpool, "Failed to start buffer pool");
    gst_structure_free (config);
    return FALSE;
  }

  GST_DEBUG_OBJECT (qpool, "Started buffer pool %" GST_PTR_FORMAT, pool);
  gst_structure_free (config);
  return TRUE;
}

static gboolean
gst_qti_buffer_pool_stop (GstBufferPool * pool)
{
  GstQtiBufferPool *qpool = GST_QTI_BUFFER_POOL (pool);
  GstQtiBufferPoolPrivate *priv = qpool->priv;
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);

  GST_DEBUG_OBJECT (qpool, "Stopping pool");

  if (GST_IS_QTI_ALLOCATOR (priv->allocator))
    gst_qti_allocator_stop (GST_QTI_ALLOCATOR (priv->allocator));

  return pclass->stop (pool);
}

static void
gst_qti_buffer_pool_finalize (GObject * object)
{
  GstQtiBufferPool *qpool = GST_QTI_BUFFER_POOL_CAST (object);

  GST_LOG_OBJECT (qpool, "Finalize buffer pool %p", qpool);

  if (qpool->priv->allocator)
    gst_object_unref (qpool->priv->allocator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_qti_buffer_pool_class_init (GstQtiBufferPoolClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (klass);

  obj_class->finalize = gst_qti_buffer_pool_finalize;

  pclass->set_config = gst_qti_buffer_pool_set_config;
  pclass->alloc_buffer = gst_qti_buffer_pool_alloc_buffer;
  pclass->start = gst_qti_buffer_pool_start;
  pclass->stop = gst_qti_buffer_pool_stop;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "qtibufferpool", 0,
      "QTI buffer pool");
}

static void
gst_qti_buffer_pool_init (GstQtiBufferPool * pool)
{
  pool->priv = gst_qti_buffer_pool_get_instance_private (pool);
}

GstBufferPool *
gst_qti_buffer_pool_new ()
{
  GstQtiBufferPool *qpool;

  qpool = g_object_new (GST_TYPE_QTI_BUFFER_POOL, NULL);
  gst_object_ref_sink (qpool);

  return GST_BUFFER_POOL_CAST (qpool);
}
