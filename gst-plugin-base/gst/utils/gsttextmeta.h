/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_TEXT_META_H__
#define __GST_TEXT_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TEXT_META_API_TYPE (gst_text_meta_api_get_type())
#define GST_TEXT_META_INFO (gst_text_meta_get_info())
#define GST_TEXT_META_CAST(obj) ((GstTextMeta *) obj)

typedef struct _GstTextMeta GstTextMeta;

/**
 * GstTextMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @parent_id: Identifier of its parent ROI, used when this meta was derived.
 * @contents: Text contents.
 * @confidence: Confidence score.
 * @color: Optional color value.
 * @xtraparams: #GstStructure containing additional parameters.
 *
 * Extra buffer metadata describing the video frame content.
 */
struct _GstTextMeta {
  GstMeta meta;

  guint        id;
  gint         parent_id;

  // entry
  gchar        *contents;
  gdouble      confidence;
  guint32      color;
  GstStructure *xtraparams;
};

GST_API GType
gst_text_meta_api_get_type (void);

GST_API const GstMetaInfo *
gst_text_meta_get_info (void);

GST_API GstTextMeta *
gst_buffer_add_text_meta (GstBuffer * buffer, const gchar * contents,
                          const gdouble confidence, const guint32 color);

GST_API GstTextMeta *
gst_buffer_get_text_meta (GstBuffer * buffer);

GST_API GstTextMeta *
gst_buffer_get_text_meta_id (GstBuffer * buffer, guint id);

GST_API GList *
gst_buffer_get_text_metas_parent_id (GstBuffer * buffer, const gint parent_id);

GST_API GstTextMeta *
gst_buffer_copy_text_meta (GstBuffer * buffer, GstTextMeta * meta);

G_END_DECLS

#endif /* __GST_TEXT_META_H__ */
