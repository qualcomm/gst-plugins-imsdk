/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_CLASSIFICATION_META_H__
#define __GST_VIDEO_CLASSIFICATION_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/utils/common-utils.h>

G_BEGIN_DECLS

#define GST_VIDEO_CLASSIFICATION_META_API_TYPE \
    (gst_video_classification_meta_api_get_type())
#define GST_VIDEO_CLASSIFICATION_META_INFO \
    (gst_video_classification_meta_get_info())
#define GST_VIDEO_CLASSIFICATION_META_CAST(obj) \
    ((GstVideoClassificationMeta *) obj)

typedef struct _GstVideoClassificationMeta GstVideoClassificationMeta;

/**
 * GstVideoClassificationMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @parent_id: Identifier of its parent ROI, used when this meta was derived.
 * @labels: A #GArray of #GstClassLabel
 *
 * Extra buffer metadata describing the video frame content.
 */
struct _GstVideoClassificationMeta {
  GstMeta meta;

  guint   id;
  gint    parent_id;

  GArray  *labels;
};

GST_VIDEO_API GType
gst_video_classification_meta_api_get_type (void);

GST_VIDEO_API const GstMetaInfo *
gst_video_classification_meta_get_info (void);

/**
 * gst_buffer_add_video_classification_meta:
 * @buffer: A #GstBuffer
 * @labels: (transfer full) (element-type GstClassLabel):
 *          A #GArray containing GstClassLabel values
 *
 * Attaches GstVideoClassificationMeta metadata to @buffer with the given labels.
 *
 * Returns: (transfer none): the #GstVideoClassificationMeta on @buffer.
 */
GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_add_video_classification_meta (GstBuffer * buffer, GArray * labels);

/**
 * gst_buffer_get_video_classification_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstVideoClassificationMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstVideoClassificationMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoClassificationMeta with lowest
 *          id (usually 0) or %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_get_video_classification_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_video_classification_meta_id:
 * @buffer: A #GstBuffer
 * @id: A metadata id
 *
 * Find the #GstVideoClassificationMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstVideoClassificationMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoClassificationMeta with @id
 *          or %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_get_video_classification_meta_id (GstBuffer * buffer, guint id);

/**
 * gst_buffer_get_video_classification_metas_parent_id:
 * @buffer: A #GstBuffer
 * @parent_id: A parent metadata id
 *
 * Find the #GstVideoClassificationMeta on @buffer with the given @parent_id.
 *
 * Buffers can contain multiple #GstVideoClassificationMeta metadata items.
 *
 * Returns: (transfer full) (element-type GstVideoClassificationMeta) (nullable):
 *          list of #GstVideoRegionOfInterestMeta with @parent_id or %NULL when
 *          there is no such metadata on @buffer.
 */
GST_VIDEO_API GList *
gst_buffer_get_video_classification_metas_parent_id (GstBuffer * buffer,
                                                     const gint parent_id);

/**
 * gst_buffer_copy_video_classification_meta: (skip):
 *
 * WARNING: INTERNAL USAGE ONLY. Subject to change.
 */
GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_copy_video_classification_meta (GstBuffer * buffer,
                                           GstVideoClassificationMeta * meta);

G_END_DECLS

#endif /* __GST_VIDEO_CLASSIFICATION_META_H__ */
