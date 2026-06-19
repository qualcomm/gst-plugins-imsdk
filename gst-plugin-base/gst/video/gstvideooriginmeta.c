/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstvideooriginmeta.h"

static gboolean
gst_video_origin_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoOriginMeta *ometa = GST_VIDEO_ORIGIN_META_CAST (meta);

  ometa->width = 0;
  ometa->height = 0;
  ometa->crop.x = 0;
  ometa->crop.y = 0;
  ometa->crop.w = 0;
  ometa->crop.h = 0;

  return TRUE;
}

static gboolean
gst_video_origin_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoOriginMeta *smeta, *dmeta;

  if (!GST_META_TRANSFORM_IS_COPY (type)) {
    /* Only copy transform is supported. */
    return FALSE;
  }

  smeta = GST_VIDEO_ORIGIN_META_CAST (meta);
  dmeta = gst_buffer_add_video_origin_meta (transbuf, smeta->width,
      smeta->height);

  if (dmeta == NULL)
    return FALSE;

  dmeta->crop = smeta->crop;

  return TRUE;
}

GType
gst_video_origin_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstVideoOriginMetaAPI",
        tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_video_origin_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info = gst_meta_register (
        GST_VIDEO_ORIGIN_META_API_TYPE, "GstVideoOriginMeta",
        sizeof (GstVideoOriginMeta), gst_video_origin_meta_init,
        NULL, gst_video_origin_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstVideoOriginMeta *
gst_buffer_add_video_origin_meta (GstBuffer * buffer, guint width, guint height)
{
  GstVideoOriginMeta *meta;

  meta = GST_VIDEO_ORIGIN_META_CAST (gst_buffer_add_meta (buffer,
      GST_VIDEO_ORIGIN_META_INFO, NULL));
  if (NULL == meta) {
    GST_ERROR ("Failed to add VideoOrigin meta to buffer %p!", buffer);
    return NULL;
  }

  meta->width = width;
  meta->height = height;

  return meta;
}

GstVideoOriginMeta *
gst_buffer_get_video_origin_meta (GstBuffer * buffer)
{
  GstVideoOriginMeta *vframe_meta;

  vframe_meta = GST_VIDEO_ORIGIN_META_CAST (gst_buffer_get_meta (buffer,
      GST_VIDEO_ORIGIN_META_API_TYPE));

  return vframe_meta;
}
