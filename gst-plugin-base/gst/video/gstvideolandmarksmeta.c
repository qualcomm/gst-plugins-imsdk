/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstvideolandmarksmeta.h"

static gboolean
gst_video_landmarks_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoLandmarksMeta *vlmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

  vlmeta->id = 0;
  vlmeta->parent_id = -1;

  vlmeta->confidence = 0.0;
  vlmeta->keypoints = NULL;
  vlmeta->links = NULL;
  vlmeta->xtraparams = NULL;

  return TRUE;
}

static void
gst_video_landmarks_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoLandmarksMeta *vlmeta = GST_VIDEO_LANDMARKS_META_CAST (meta);

  g_array_free (vlmeta->keypoints, TRUE);

  if (NULL != vlmeta->links)
    g_array_free (vlmeta->links, TRUE);

  if (NULL != vlmeta->xtraparams)
    gst_structure_free (vlmeta->xtraparams);
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
  dmeta->parent_id = smeta->parent_id;

  if (smeta->xtraparams != NULL)
    dmeta->xtraparams = gst_structure_copy (smeta->xtraparams);

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

GList *
gst_buffer_get_video_landmarks_metas_parent_id (GstBuffer * buffer,
    const gint parent_id)
{
  GList *metalist = NULL;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api != GST_VIDEO_LANDMARKS_META_API_TYPE)
      continue;

    if (GST_VIDEO_LANDMARKS_META_CAST (meta)->parent_id == parent_id)
      metalist = g_list_prepend (metalist, meta);
  }
  return metalist;
}

GstVideoLandmarksMeta *
gst_buffer_copy_video_landmarks_meta (GstBuffer * buffer,
    GstVideoLandmarksMeta * meta)
{
  GstVideoLandmarksMeta *newmeta = NULL;

  newmeta = gst_buffer_add_video_landmarks_meta (buffer, meta->confidence,
      g_array_copy (meta->keypoints), g_array_copy (meta->links));

  newmeta->id = meta->id;
  newmeta->parent_id = meta->parent_id;

  if (meta->xtraparams != NULL)
    newmeta->xtraparams = gst_structure_copy (meta->xtraparams);

  return newmeta;
}

void
gst_video_landmarks_coordinates_correction (GstVideoLandmarksMeta * meta,
    GstVideoRectangle * source, GstVideoRectangle * destination)
{
  GstVideoKeypoint *kp = NULL;
  gdouble w_scale = 0.0, h_scale = 0.0;
  guint idx = 0;

  gst_util_fraction_to_double (destination->w, source->w, &w_scale);
  gst_util_fraction_to_double (destination->h, source->h, &h_scale);

  // Correct the X and Y of each keypoint bases on the regions.
  for (idx = 0; idx < meta->keypoints->len; idx++) {
    kp = &(g_array_index (meta->keypoints, GstVideoKeypoint, idx));

    kp->x = ((kp->x - source->x) * w_scale) + destination->x;
    kp->y = ((kp->y - source->y) * h_scale) + destination->y;
  }
}
