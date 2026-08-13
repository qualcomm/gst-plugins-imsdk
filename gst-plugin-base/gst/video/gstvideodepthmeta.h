/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_DEPTH_META_H__
#define __GST_VIDEO_DEPTH_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_DEPTH_META_API_TYPE \
    (gst_video_depth_meta_api_get_type())
#define GST_VIDEO_DEPTH_META_INFO \
    (gst_video_depth_meta_get_info())
#define GST_VIDEO_DEPTH_META_CAST(obj) \
    ((GstVideoDepthMeta *) obj)

typedef struct _GstVideoDepthMeta GstVideoDepthMeta;

/**
 * GstVideoDepthMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @parent_id: Identifier of its parent ROI, used when this meta was derived.
 * @depthvals: (element-type gdouble):
 *             A #GArray of #gdouble depth value for each index in the map.
 * @colormask: (optional) (element-type guint32):
 *             A #GArray of #guint32 color for each index in the map.
 * @n_columns: Number of columns in the map.
 * @n_rows: Number of rows in the map.
 * @xtraparams: (optional): A #GstStructure containing additional parameters.
 *
 * Extra buffer metadata describing depth map for an image or part of it.
 */
struct _GstVideoDepthMeta {
  GstMeta           meta;

  guint             id;
  gint              parent_id;

  GArray            *depthvals;
  GArray            *colormask;

  guint             n_columns;
  guint             n_rows;

  GstStructure      *xtraparams;
};

GST_VIDEO_API GType
gst_video_depth_meta_api_get_type (void);

GST_VIDEO_API const GstMetaInfo *
gst_video_depth_meta_get_info (void);


/**
 * gst_buffer_add_video_depth_meta:
 * @buffer: A #GstBuffer
 * @depthvals: (transfer full) (element-type gdouble):
 *             A #GArray of #gdouble depth value for each index in the map.
 * @colormask: (optional) (transfer full) (element-type guint32):
 *             A #GArray of #guint32 color for each index in the map.
 * @n_columns: Number of columns in the map.
 * @n_rows: Number of rows in the map.
 *
 * Attaches GstVideoDepthMeta metadata to @buffer with the given parameters.
 *
 * Returns: (transfer none): The #GstVideoDepthMeta on @buffer.
 */
GST_VIDEO_API GstVideoDepthMeta *
gst_buffer_add_video_depth_meta (GstBuffer * buffer, GArray * depthvals,
                                 GArray * colormask, guint n_columns,
                                 guint n_rows);

/**
 * gst_buffer_get_video_depth_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstVideoDepthMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstVideoDepthMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoDepthMeta with lowest id
 *          (usually 0) or %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoDepthMeta *
gst_buffer_get_video_depth_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_video_depth_meta_id:
 * @buffer: A #GstBuffer
 * @id: A metadata id
 *
 * Find the #GstVideoDepthMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstVideoDepthMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoDepthMeta with @id or
 *          %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoDepthMeta *
gst_buffer_get_video_depth_meta_id (GstBuffer * buffer, guint id);

/**
 * gst_buffer_get_video_depth_metas_parent_id:
 * @buffer: A #GstBuffer
 * @parent_id: A parent metadata id
 *
 * Find the #GstVideoDepthMeta on @buffer with the given @parent_id.
 *
 * Buffers can contain multiple #GstVideoDepthMeta metadata items.
 *
 * Returns: (transfer container) (element-type GstVideoDepthMeta) (nullable):
 *          list of #GstVideoDepthMeta with @parent_id or %NULL when there
 *          is no such metadata on @buffer. Free the list with g_list_free();
 *          the metadata items it holds remain owned by @buffer.
 */
GST_VIDEO_API GList *
gst_buffer_get_video_depth_metas_parent_id (GstBuffer * buffer,
                                            const gint parent_id);

G_END_DECLS

#endif /* __GST_VIDEO_DEPTH_META_H__ */
