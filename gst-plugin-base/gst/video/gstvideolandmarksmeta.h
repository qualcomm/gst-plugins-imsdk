/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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
 * @xtraparams: #GstStructure containing additional parameters.
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

GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_add_video_landmarks_meta (GstBuffer * buffer, gdouble confidence,
                                     GArray * keypoints, GArray * links);

GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_get_video_landmarks_meta (GstBuffer * buffer);

GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_get_video_landmarks_meta_id (GstBuffer * buffer, guint id);

GST_VIDEO_API GList *
gst_buffer_get_video_landmarks_metas_parent_id (GstBuffer * buffer,
                                                const gint parent_id);

GST_VIDEO_API GstVideoLandmarksMeta *
gst_buffer_copy_video_landmarks_meta (GstBuffer * buffer,
                                      GstVideoLandmarksMeta * meta);

GST_VIDEO_API void
gst_video_landmarks_coordinates_correction (GstVideoLandmarksMeta * meta,
                                            GstVideoRectangle * source,
                                            GstVideoRectangle * destination);

G_END_DECLS

#endif /* __GST_VIDEO_LANDMARKS_META_H__ */
