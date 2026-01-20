/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "gstcamerameta.h"
#include <string.h>

GType
gst_camera_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { "camera", NULL };

  if (g_once_init_enter (&type)) {
      GType _type = gst_meta_api_type_register ("GstCameraMetaAPI", tags);
      g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_camera_meta_init (GstMeta *meta, gpointer params, GstBuffer *buffer)
{
  GstCameraMeta *cmeta = (GstCameraMeta*) meta;
  cmeta->metadata = NULL;
  return TRUE;
}

static void
gst_camera_meta_free (GstMeta *meta, GstBuffer *buffer)
{
  GstCameraMeta *cmeta = (GstCameraMeta*) meta;
  // Do not free cmeta->metadata if owned externally
  cmeta->metadata = NULL;
}

static gboolean
gst_camera_meta_transform (GstBuffer *dest, GstMeta *meta,
    GstBuffer *src, GQuark type, gpointer data)
{
  GstCameraMeta *src_meta = (GstCameraMeta*) meta;
  GstCameraMeta *dest_meta =
      gst_buffer_add_camera_meta (dest, src_meta->metadata);
  return dest_meta != NULL;
}

const GstMetaInfo*
gst_camera_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter (&info)) {
    const GstMetaInfo *meta_info = gst_meta_register (
        GST_CAMERA_META_API_TYPE,
        "GstCameraMeta",
        sizeof (GstCameraMeta),
        gst_camera_meta_init,
        gst_camera_meta_free,
        gst_camera_meta_transform
    );
    g_once_init_leave (&info, meta_info);
  }
  return info;
}

GstCameraMeta*
gst_buffer_add_camera_meta (GstBuffer *buffer,
    struct camera_metadata *metadata)
{
  GstCameraMeta *meta =
      (GstCameraMeta*)gst_buffer_add_meta (buffer, GST_CAMERA_META_INFO, NULL);
  if (meta) {
    meta->metadata = metadata;
  }
  return meta;
}

GstCameraMeta*
gst_buffer_get_camera_meta (GstBuffer *buffer)
{
  return (GstCameraMeta*)gst_buffer_get_meta (buffer, GST_CAMERA_META_API_TYPE);
}
