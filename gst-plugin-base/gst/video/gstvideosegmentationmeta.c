/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstvideosegmentationmeta.h"

static gboolean
gst_video_segmentation_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoSegmentationMeta *segmeta = GST_VIDEO_SEGMENTATION_META_CAST (meta);

  segmeta->id = 0;
  segmeta->parent_id = -1;

  segmeta->labelmask = NULL;
  segmeta->colormask = NULL;
  segmeta->n_rows = 0;
  segmeta->n_columns = 0;

  segmeta->xtraparams = NULL;
  return TRUE;
}

static void
gst_video_segmentation_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoSegmentationMeta *segmeta = GST_VIDEO_SEGMENTATION_META_CAST (meta);

  g_clear_pointer (&segmeta->labelmask, g_array_unref);
  g_clear_pointer (&segmeta->colormask, g_array_unref);
  g_clear_pointer (&segmeta->xtraparams, gst_structure_free);
}

static gboolean
gst_video_segmentation_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoSegmentationMeta *dmeta = NULL, *smeta = NULL;

  if (!GST_META_TRANSFORM_IS_COPY (type)) {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }

  smeta = GST_VIDEO_SEGMENTATION_META_CAST (meta);

  dmeta = gst_buffer_add_video_segmentation_meta (transbuffer,
      g_array_copy (smeta->labelmask), g_array_copy (smeta->colormask),
      smeta->n_columns, smeta->n_rows);

  if (NULL == dmeta)
    return FALSE;

  dmeta->id = smeta->id;
  dmeta->parent_id = smeta->parent_id;

  GST_DEBUG ("Duplicate Video Segmentation metadata");
  return TRUE;
}

GType
gst_video_segmentation_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstVideoSegmentationMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_video_segmentation_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info = gst_meta_register (
        GST_VIDEO_SEGMENTATION_META_API_TYPE, "GstVideoSegmentationMeta",
        sizeof (GstVideoSegmentationMeta), gst_video_segmentation_meta_init,
        gst_video_segmentation_meta_free, gst_video_segmentation_meta_transform);

    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstVideoSegmentationMeta *
gst_buffer_add_video_segmentation_meta (GstBuffer * buffer, GArray * labelmask,
    GArray * colormask, guint n_columns, guint n_rows)
{
  GstVideoSegmentationMeta *meta = NULL;

  if ((colormask != NULL) && (labelmask->len != colormask->len)) {
    GST_ERROR ("Length of labels and colors do not match!");
    goto error;
  }

  meta = GST_VIDEO_SEGMENTATION_META_CAST (
      gst_buffer_add_meta (buffer, GST_VIDEO_SEGMENTATION_META_INFO, NULL));

  if (NULL == meta) {
    GST_ERROR ("Failed to add Video Segmentation meta to buffer %p!", buffer);
    goto error;
  }

  meta->labelmask = labelmask;
  meta->colormask = colormask;
  meta->n_rows = n_rows;
  meta->n_columns = n_columns;

  return meta;

error:
  if (colormask != NULL)
    g_array_free (colormask, TRUE);

  g_array_free (labelmask, TRUE);
  return NULL;
}

GstVideoSegmentationMeta *
gst_buffer_get_video_segmentation_meta (GstBuffer * buffer)
{
  const GstMetaInfo *info = GST_VIDEO_SEGMENTATION_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;
  GstVideoSegmentationMeta *outmeta = NULL, *segmeta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      segmeta = GST_VIDEO_SEGMENTATION_META_CAST (meta);

      if (segmeta->id == 0)
        return segmeta;

      if (outmeta == NULL || segmeta->id < outmeta->id)
        outmeta = segmeta;
    }
  }
  return NULL;
}

GstVideoSegmentationMeta *
gst_buffer_get_video_segmentation_meta_id (GstBuffer * buffer, guint id)
{
  const GstMetaInfo *info = GST_VIDEO_SEGMENTATION_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      if (GST_VIDEO_SEGMENTATION_META_CAST (meta)->id == id)
        return GST_VIDEO_SEGMENTATION_META_CAST (meta);
    }
  }
  return NULL;
}

GList *
gst_buffer_get_video_segmentation_metas_parent_id (GstBuffer * buffer,
    const gint parent_id)
{
  GList *metalist = NULL;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api != GST_VIDEO_SEGMENTATION_META_API_TYPE)
      continue;

    if (GST_VIDEO_SEGMENTATION_META_CAST (meta)->parent_id == parent_id)
      metalist = g_list_prepend (metalist, meta);
  }
  return metalist;
}
