/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_LANDMARKS_META_H__
#define __GST_VIDEO_LANDMARKS_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_LANDMARKS_META_API_TYPE \
    (gst_video_landmarks_meta_api_get_type())
#define GST_VIDEO_LANDMARKS_META_INFO  (gst_video_landmarks_meta_get_info())
#define GST_VIDEO_LANDMARKS_META_CAST(obj) ((GstVideoLandmarksMeta *) obj)

typedef struct _GstVideoKeypoint GstVideoKeypoint;
typedef struct _GstVideoKeypointLink GstVideoKeypointLink;
typedef struct _GstVideoLandmarksMeta GstVideoLandmarksMeta;


/**
 * GstVideoKeypoint:
 * @name: Label name.
 * @confidence: Confidence score.
 * @color: Optional color value.
 * @x: X axis coordinate of the keypoint in pixels.
 * @y: Y axis coordinate of the keypoint in pixels.
 *
 * Helper structure representing a keypoint in video frame.
 */
struct _GstVideoKeypoint {
  GQuark  name;

  gdouble confidence;
  guint32 color;

  gint    x;
  gint    y;
};

/**
 * GstVideoKeypointLink:
 * @s_kp_idx: Index of the source keypoint in the keypoints #GArray.
 * @d_kp_idx: Index of the destination keypoint in the keypoints #GArray.
 *
 * Helper structure representing a link between two video frame keypoints.
 */
struct _GstVideoKeypointLink {
  guint s_kp_idx;
  guint d_kp_idx;
};

/**
 * GstVideoLandmarksMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @parent_id: Identifier of its parent ROI, used when this meta was derived.
 * @confidence: Confidence score for the landmarks group as a whole.
 * @keypoints: A #GArray of #GstVideoKeypoint
 * @links: A #GArray of #GstVideoKeypointLink
 * @xtraparams: (optional): A #GstStructure containing additional parameters.
 *
 * Extra buffer metadata describing multiple video keypoints and their linkages.
 */
struct _GstVideoLandmarksMeta {
  GstMeta      meta;

  guint        id;
  gint         parent_id;

  gdouble      confidence;
  GArray       *keypoints;
  GArray       *links;

  GstStructure *xtraparams;
};

GST_VIDEO_API GType
gst_video_landmarks_meta_api_get_type (void);

GST_VIDEO_API const GstMetaInfo *
gst_video_landmarks_meta_get_info (void);

/**
 * gst_video_landmarks_meta_transform_coordinates: (skip):
 *
 * WARNING: INTERNAL USAGE ONLY. Subject to change.
 */
GST_VIDEO_API void
gst_video_landmarks_meta_transform_coordinates (GstVideoLandmarksMeta * meta,
                                                const GstVideoRectangle * source,
                                                const GstVideoRectangle * destination);

/**
 * gst_buffer_add_video_landmarks_meta:
 * @buffer: A #GstBuffer
 * @confidence: Confidence score in the range 0.0 to 100.0
 * @keypoints: (transfer full) (element-type GstVideoKeypoint):
 *             A #GArray containing GstVideoKeypoint values
 * @links: (transfer full) (element-type GstVideoKeypointLink):
 *         A #GArray containing GstVideoKeypointLink values
 *
 * Attaches GstVideoLandmarksMeta metadata to @buffer with the given parameters.
 *
 * Returns: (transfer none): the #GstVideoLandmarksMeta on @buffer.
 */
GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_add_video_landmarks_meta (GstBuffer * buffer, gdouble confidence,
                                     GArray * keypoints, GArray * links);

/**
 * gst_buffer_get_video_landmarks_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstVideoLandmarksMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstVideoLandmarksMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoLandmarksMeta with lowest id
 *          (usually 0) or %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_get_video_landmarks_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_video_landmarks_meta_id:
 * @buffer: A #GstBuffer
 * @id: A metadata id
 *
 * Find the #GstVideoLandmarksMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstVideoLandmarksMeta metadata items.
 *
 * Returns: (transfer none) (nullable): the #GstVideoLandmarksMeta with @id or
 *          %NULL when there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_get_video_landmarks_meta_id (GstBuffer * buffer, guint id);

/**
 * gst_buffer_get_video_landmarks_metas_parent_id:
 * @buffer: A #GstBuffer
 * @parent_id: A parent metadata id
 *
 * Find the #GstVideoLandmarksMeta on @buffer with the given @parent_id.
 *
 * Buffers can contain multiple #GstVideoLandmarksMeta metadata items.
 *
 * Returns: (transfer full) (element-type GstVideoLandmarksMeta) (nullable):
 *          list of #GstVideoLandmarksMeta with @parent_id or %NULL when there
 *          is no such metadata on @buffer.
 */
GST_VIDEO_API GList *
gst_buffer_get_video_landmarks_metas_parent_id (GstBuffer * buffer,
                                                const gint parent_id);

/**
 * gst_buffer_copy_video_landmarks_meta: (skip):
 *
 * WARNING: INTERNAL USAGE ONLY. Subject to change.
 */
GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_copy_video_landmarks_meta (GstBuffer * buffer,
                                      GstVideoLandmarksMeta * meta);

G_END_DECLS

#endif /* __GST_VIDEO_LANDMARKS_META_H__ */
