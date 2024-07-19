/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstvideolandmarksmeta.h"

static gboolean
gst_video_landmarks_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoLandmarksMeta *vlmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

  vlmeta->id = 0;

  vlmeta->confidence = 0.0;
  vlmeta->keypoints = NULL;
  vlmeta->links = NULL;

  return TRUE;
}

static void
gst_video_landmarks_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoLandmarksMeta *vlmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

  g_array_free (vlmeta->keypoints, TRUE);

  if (NULL != vlmeta->links)
    g_array_free (vlmeta->links, TRUE);
}

static gboolean
gst_video_landmarks_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoLandmarksMeta *dmeta, *smeta;
  GArray *keypoints = NULL, *links = NULL;

  if (!GST_META_TRANSFORM_IS_COPY (type)) {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }

  smeta = GST_VIDEO_LANDMARKS_META_CAST (meta);
  keypoints = g_array_copy (smeta->keypoints);

  if (smeta->links != NULL)
    links = g_array_copy (smeta->links);

  dmeta = gst_buffer_add_video_landmarks_meta (transbuffer, smeta->confidence,
      keypoints, links);

  if (NULL == dmeta) {
    g_array_free (keypoints, TRUE);
    return FALSE;
  }

  dmeta->id = smeta->id;

  GST_DEBUG ("Duplicate Video Landmarks metadata");
  return TRUE;
}

GType
gst_video_landmarks_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstVideoLandmarksMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_video_landmarks_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info = gst_meta_register (
        GST_VIDEO_LANDMARKS_META_API_TYPE, "GstVideoLandmarksMeta",
        sizeof (GstVideoLandmarksMeta), gst_video_landmarks_meta_init,
        gst_video_landmarks_meta_free, gst_video_landmarks_meta_transform);

    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstVideoLandmarksMeta *
gst_buffer_add_video_landmarks_meta (GstBuffer * buffer, gdouble confidence,
    GArray * keypoints, GArray * links)
{
  GstVideoLandmarksMeta *meta;

  meta = GST_VIDEO_LANDMARKS_META_CAST (
      gst_buffer_add_meta (buffer, GST_VIDEO_LANDMARKS_META_INFO, NULL));

  if (NULL == meta) {
    GST_ERROR ("Failed to add Video Landmarks meta to buffer %p!", buffer);
    return NULL;
  }

  meta->confidence = confidence;
  meta->keypoints = keypoints;
  meta->links = links;

  return meta;
}

GstVideoLandmarksMeta *
gst_buffer_get_video_landmarks_meta (GstBuffer * buffer)
{
  const GstMetaInfo *info = GST_VIDEO_LANDMARKS_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;
  GstVideoLandmarksMeta *outmeta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      GstVideoLandmarksMeta *vlmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

      if (vlmeta->id == 0)
        return vlmeta;

      if (outmeta == NULL || vlmeta->id < outmeta->id)
        outmeta = vlmeta;
    }
  }
  return NULL;
}

GstVideoLandmarksMeta *
gst_buffer_get_video_landmarks_meta_id (GstBuffer * buffer, guint id)
{
  const GstMetaInfo *info = GST_VIDEO_LANDMARKS_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      if (GST_VIDEO_LANDMARKS_META_CAST (meta)->id == id)
        return GST_VIDEO_LANDMARKS_META_CAST (meta);
    }
  }
  return NULL;
}
