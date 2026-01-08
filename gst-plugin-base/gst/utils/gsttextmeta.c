/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gsttextmeta.h"

static gboolean
gst_text_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstTextMeta *textmeta = GST_TEXT_META_CAST (meta);

  textmeta->id = 0;
  textmeta->parent_id = -1;
  textmeta->contents = NULL;
  textmeta->confidence = 0.0;
  textmeta->color = 0x000000FF;
  textmeta->xtraparams = NULL;

  return TRUE;
}

static void
gst_text_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstTextMeta *textmeta = GST_TEXT_META_CAST (meta);

  if (textmeta->contents != NULL) {
    g_free (textmeta->contents);
    textmeta->contents = NULL;
  }

  if (textmeta->xtraparams != NULL) {
    gst_structure_free (textmeta->xtraparams);
    textmeta->xtraparams = NULL;
  }
}

static gboolean
gst_text_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstTextMeta *dmeta = NULL, *smeta = NULL;

  if (!GST_META_TRANSFORM_IS_COPY (type)) {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }

  smeta = GST_TEXT_META_CAST (meta);
  dmeta = gst_buffer_add_text_meta (transbuffer, smeta->contents,
      smeta->confidence, smeta->color);

  if (NULL == dmeta)
    return FALSE;

  if (smeta->xtraparams != NULL)
    dmeta->xtraparams = gst_structure_copy (smeta->xtraparams);

  dmeta->id = smeta->id;
  dmeta->parent_id = smeta->parent_id;

  GST_DEBUG ("Duplicate Text Generation metadata");
  return TRUE;
}

GType
gst_text_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstTextMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_text_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info = gst_meta_register (
        GST_TEXT_META_API_TYPE, "GstTextMeta",
        sizeof (GstTextMeta), gst_text_meta_init,
        gst_text_meta_free, gst_text_meta_transform);

    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstTextMeta *
gst_buffer_add_text_meta (GstBuffer * buffer, const gchar * contents,
    const gdouble confidence, const guint32 color)
{
  g_return_val_if_fail (contents != NULL, NULL);

  GstTextMeta *outmeta;

  outmeta = GST_TEXT_META_CAST (
      gst_buffer_add_meta (buffer, GST_TEXT_META_INFO, NULL));

  if (NULL == outmeta) {
    GST_ERROR ("Failed to add Text Generation meta to buffer %p!", buffer);
    return NULL;
  }

  outmeta->contents = g_strdup (contents);
  outmeta->confidence = confidence;
  outmeta->color = color;

  return outmeta;
}

GstTextMeta *
gst_buffer_get_text_meta (GstBuffer * buffer)
{
  const GstMetaInfo *info = GST_TEXT_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;
  GstTextMeta *outmeta = NULL, *textmeta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      textmeta = GST_TEXT_META_CAST (meta);

      if (textmeta->id == 0)
        return textmeta;

      if (outmeta == NULL || textmeta->id < outmeta->id)
        outmeta = textmeta;
    }
  }
  return NULL;
}

GstTextMeta *
gst_buffer_get_text_meta_id (GstBuffer * buffer, guint id)
{
  const GstMetaInfo *info = GST_TEXT_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      if (GST_TEXT_META_CAST (meta)->id == id)
        return GST_TEXT_META_CAST (meta);
    }
  }
  return NULL;
}

GList *
gst_buffer_get_text_metas_parent_id (GstBuffer * buffer, const gint parent_id)
{
  GList *metalist = NULL;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api != GST_TEXT_META_API_TYPE)
      continue;

    if (GST_TEXT_META_CAST (meta)->parent_id == parent_id)
      metalist = g_list_prepend (metalist, meta);
  }
  return metalist;
}

// TODO: NEEDED?
GstTextMeta *
gst_buffer_copy_text_meta (GstBuffer * buffer, GstTextMeta * meta)
{
  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (meta != NULL, NULL);
  g_return_val_if_fail (meta->contents != NULL, NULL);

  GstTextMeta *newmeta = gst_buffer_add_text_meta (buffer, meta->contents,
      meta->confidence, meta->color);

  if (NULL == newmeta)
    return NULL;

  if (meta->xtraparams != NULL)
    newmeta->xtraparams = gst_structure_copy (meta->xtraparams);

  newmeta->id = meta->id;
  newmeta->parent_id = meta->parent_id;

  return newmeta;
}
