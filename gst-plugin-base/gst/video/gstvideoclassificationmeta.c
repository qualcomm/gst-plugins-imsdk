/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstvideoclassificationmeta.h"

static gboolean
gst_video_classification_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoClassificationMeta *classmeta = GST_VIDEO_CLASSIFICATION_META_CAST (meta);

  classmeta->id = 0;
  classmeta->labels = NULL;

  return TRUE;
}

static void
gst_video_classification_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoClassificationMeta *classmeta = GST_VIDEO_CLASSIFICATION_META_CAST (meta);

  g_array_free (classmeta->labels, TRUE);
}

static gboolean
gst_video_classification_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoClassificationMeta *dmeta = NULL, *smeta = NULL;
  GArray *labels = NULL;
  guint idx = 0;

  if (!GST_META_TRANSFORM_IS_COPY (type)) {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }

  smeta = GST_VIDEO_CLASSIFICATION_META_CAST (meta);
  labels = g_array_copy (smeta->labels);

  // The GArray copy above naturally doesn't copy the data in pointers.
  // Iterate over the labels and deep copy any extra params.
  for (idx = 0; idx < labels->len; idx++) {
     GstClassLabel *label = &(g_array_index (labels, GstClassLabel, idx));

    if (label->xtraparams == NULL)
      continue;

    label->xtraparams = gst_structure_copy (label->xtraparams);
  }

  g_array_set_clear_func (labels,
      (GDestroyNotify) gst_video_classification_label_cleanup);

  dmeta = gst_buffer_add_video_classification_meta (transbuffer, labels);

  if (NULL == dmeta) {
    g_array_free (labels, TRUE);
    return FALSE;
  }

  dmeta->id = smeta->id;

  GST_DEBUG ("Duplicate Video Classification metadata");
  return TRUE;
}

void
gst_video_classification_label_cleanup (GstClassLabel * label)
{
  if (label->xtraparams != NULL)
    gst_structure_free (label->xtraparams);
}

GType
gst_video_classification_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstVideoClassificationMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_video_classification_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info = gst_meta_register (
        GST_VIDEO_CLASSIFICATION_META_API_TYPE, "GstVideoClassificationMeta",
        sizeof (GstVideoClassificationMeta), gst_video_classification_meta_init,
        gst_video_classification_meta_free, gst_video_classification_meta_transform);

    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstVideoClassificationMeta *
gst_buffer_add_video_classification_meta (GstBuffer * buffer, GArray * labels)
{
  GstVideoClassificationMeta *meta;

  meta = GST_VIDEO_CLASSIFICATION_META_CAST (
      gst_buffer_add_meta (buffer, GST_VIDEO_CLASSIFICATION_META_INFO, NULL));

  if (NULL == meta) {
    GST_ERROR ("Failed to add Video Classification meta to buffer %p!", buffer);
    return NULL;
  }

  meta->labels = labels;

  return meta;
}

GstVideoClassificationMeta *
gst_buffer_get_video_classification_meta (GstBuffer * buffer)
{
  const GstMetaInfo *info = GST_VIDEO_CLASSIFICATION_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;
  GstVideoClassificationMeta *outmeta = NULL, *classmeta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      classmeta = GST_VIDEO_CLASSIFICATION_META_CAST (meta);

      if (classmeta->id == 0)
        return classmeta;

      if (outmeta == NULL || classmeta->id < outmeta->id)
        outmeta = classmeta;
    }
  }
  return NULL;
}

GstVideoClassificationMeta *
gst_buffer_get_video_classification_meta_id (GstBuffer * buffer, guint id)
{
  const GstMetaInfo *info = GST_VIDEO_CLASSIFICATION_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      if (GST_VIDEO_CLASSIFICATION_META_CAST (meta)->id == id)
        return GST_VIDEO_CLASSIFICATION_META_CAST (meta);
    }
  }
  return NULL;
}
