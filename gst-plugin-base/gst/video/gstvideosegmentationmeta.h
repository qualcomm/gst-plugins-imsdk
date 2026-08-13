/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_SEGMENTATION_META_H__
#define __GST_VIDEO_SEGMENTATION_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_SEGMENTATION_META_API_TYPE \
    (gst_video_segmentation_meta_api_get_type())
#define GST_VIDEO_SEGMENTATION_META_INFO \
    (gst_video_segmentation_meta_get_info())
#define GST_VIDEO_SEGMENTATION_META_CAST(obj) \
    ((GstVideoSegmentationMeta *) obj)

typedef struct _GstVideoSegmentationMeta GstVideoSegmentationMeta;

/**
 * GstVideoSegmentationMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @parent_id: Identifier of its parent ROI, used when this meta was derived.
 * @labelmask: (element-type GQuark):
 *             A #GArray of #GQuark class label for each index in the mask.
 * @colormask: (optional) (element-type guint32):
 *             A #GArray of #guint32 color for each index in the mask.
 * @n_columns: Number of columns in the mask.
 * @n_rows: Number of rows in the mask.
 * @xtraparams: (optional): A #GstStructure containing additional parameters.
 *
 * Extra buffer metadata describing segmentation mask for an image or part of it.
 */
struct _GstVideoSegmentationMeta {
  GstMeta           meta;

  guint             id;
  gint              parent_id;

  GArray            *labelmask;
  GArray            *colormask;

  guint             n_columns;
  guint             n_rows;

  GstStructure      *xtraparams;
};

GST_VIDEO_API GType
gst_video_segmentation_meta_api_get_type (void);

GST_VIDEO_API const GstMetaInfo *
gst_video_segmentation_meta_get_info (void);

/**
 * gst_buffer_add_video_segmentation_meta:
 * @buffer: A #GstBuffer
 * @labelmask: (transfer full) (element-type GQuark):
 *             A #GArray of #GQuark class label for each index in the mask.
 * @colormask: (optional) (transfer full) (element-type guint32):
 *             A #GArray of #guint32 color for each index in the mask.
 * @n_columns: Number of columns in the mask.
 * @n_rows: Number of rows in the mask.
 *
 * Attaches GstVideoSegmentationMeta metadata to @buffer with the given parameters.
 *
 * Returns: (transfer none): The #GstVideoSegmentationMeta on @buffer.
 */
GST_VIDEO_API GstVideoSegmentationMeta *
gst_buffer_add_video_segmentation_meta (GstBuffer * buffer, GArray * labelmask,
                                        GArray * colormask, guint n_columns,
                                        guint n_rows);

/**
 * gst_buffer_get_video_segmentation_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstVideoSegmentationMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstVideoSegmentationMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoSegmentationMeta with lowest id
 *          (usually 0) or %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoSegmentationMeta *
gst_buffer_get_video_segmentation_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_video_segmentation_meta_id:
 * @buffer: A #GstBuffer
 * @id: A metadata id
 *
 * Find the #GstVideoSegmentationMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstVideoSegmentationMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoSegmentationMeta with @id or
 *          %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoSegmentationMeta *
gst_buffer_get_video_segmentation_meta_id (GstBuffer * buffer, guint id);

/**
 * gst_buffer_get_video_segmentation_metas_parent_id:
 * @buffer: A #GstBuffer
 * @parent_id: A parent metadata id
 *
 * Find the #GstVideoSegmentationMeta on @buffer with the given @parent_id.
 *
 * Buffers can contain multiple #GstVideoSegmentationMeta metadata items.
 *
 * Returns: (transfer container) (element-type GstVideoSegmentationMeta) (nullable):
 *          list of #GstVideoSegmentationMeta with @parent_id or %NULL when there
 *          is no such metadata on @buffer. Free the list with g_list_free();
 *          the metadata items it holds remain owned by @buffer.
 */
GST_VIDEO_API GList *
gst_buffer_get_video_segmentation_metas_parent_id (GstBuffer * buffer,
                                                   const gint parent_id);

G_END_DECLS

#endif /* __GST_VIDEO_SEGMENTATION_META_H__ */
