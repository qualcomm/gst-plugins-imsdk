/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_CLASSIFICATION_META_H__
#define __GST_VIDEO_CLASSIFICATION_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_CLASSIFICATION_META_API_TYPE \
    (gst_video_classification_meta_api_get_type())
#define GST_VIDEO_CLASSIFICATION_META_INFO \
    (gst_video_classification_meta_get_info())
#define GST_VIDEO_CLASSIFICATION_META_CAST(obj) \
    ((GstVideoClassificationMeta *) obj)

typedef struct _GstClassLabel GstClassLabel;
typedef struct _GstVideoClassificationMeta GstVideoClassificationMeta;

/**
 * GstClassLabel:
 * @name: Label name.
 * @confidence: Confidence score.
 * @color: Optional color value.
 *
 * Generic helper structure representing a class label.
 */
struct _GstClassLabel {
  GQuark  name;
  gdouble confidence;
  guint32 color;
};

/**
 * GstVideoClassificationMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @labels: A #GArray of #GstClassLabel
 *
 * Extra buffer metadata describing the video frame content.
 */
struct _GstVideoClassificationMeta {
  GstMeta meta;

  guint   id;

  GArray  *labels;
};

GST_VIDEO_API GType
gst_video_classification_meta_api_get_type (void);

GST_VIDEO_API const GstMetaInfo *
gst_video_classification_meta_get_info (void);

GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_add_video_classification_meta (GstBuffer * buffer, GArray * labels);

GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_get_video_classification_meta (GstBuffer * buffer);

GST_VIDEO_API GstVideoClassificationMeta *
gst_buffer_get_video_classification_meta_id (GstBuffer * buffer, guint id);

G_END_DECLS

#endif /* __GST_VIDEO_CLASSIFICATION_META_H__ */
