/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_TEXT_META_H__
#define __GST_TEXT_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TEXT_META_API_TYPE  (gst_text_meta_api_get_type())
#define GST_TEXT_META_INFO      (gst_text_meta_get_info())
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
 * Extra buffer metadata describing the frame content.
 */
struct _GstTextMeta {
  GstMeta      meta;

  guint        id;
  gint         parent_id;

  gchar        *contents;
  gdouble      confidence;
  guint32      color;

  GstStructure *xtraparams;
};

GST_API GType
gst_text_meta_api_get_type (void);

GST_API const GstMetaInfo *
gst_text_meta_get_info (void);

/**
 * gst_buffer_add_text_meta:
 * @buffer: A #GstBuffer
 * @contents: Text paragraph.
 * @confidence: Confidence score in the range 0.0 to 100.0
 * @color: Color in which the text will be presented if visualized.
 *
 * Attaches GstTextMeta metadata to @buffer with the given parameters.
 *
 * Returns: (transfer none): the #GstTextMeta on @buffer.
 */
GST_API GstTextMeta *
gst_buffer_add_text_meta (GstBuffer * buffer, const gchar * contents,
                          const gdouble confidence, const guint32 color);

/**
 * gst_buffer_get_text_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstTextMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstTextMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstTextMeta with lowest id
 *          (usually 0) or %NULL when there is no such metadata on @buffer.
 */
GST_API GstTextMeta *
gst_buffer_get_text_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_text_meta_id:
 * @buffer: A #GstBuffer
 * @id: A metadata id
 *
 * Find the #GstTextMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstTextMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstTextMeta with @id or
 *          %NULL when there is no such metadata on @buffer.
 */
GST_API GstTextMeta *
gst_buffer_get_text_meta_id (GstBuffer * buffer, guint id);

/**
 * gst_buffer_get_text_metas_parent_id:
 * @buffer: A #GstBuffer
 * @parent_id: A parent metadata id
 *
 * Find the #GstTextMeta on @buffer with the given @parent_id.
 *
 * Buffers can contain multiple #GstTextMeta metadata items.
 *
 * Returns: (transfer full) (element-type GstTextMeta) (nullable):
 *          list of #GstTextMeta with @parent_id or %NULL when there
 *          is no such metadata on @buffer.
 */
GST_API GList *
gst_buffer_get_text_metas_parent_id (GstBuffer * buffer, const gint parent_id);

G_END_DECLS

#endif /* __GST_TEXT_META_H__ */
