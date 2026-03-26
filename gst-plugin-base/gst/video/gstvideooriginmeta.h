/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_ORIGIN_META_H__
#define __GST_VIDEO_ORIGIN_META_H__

#include "video-converter-engine.h"

G_BEGIN_DECLS

#define GST_VIDEO_ORIGIN_META_API_TYPE (gst_video_origin_meta_api_get_type())
#define GST_VIDEO_ORIGIN_META_INFO  (gst_video_origin_meta_get_info())

#define GST_VIDEO_ORIGIN_META_CAST(obj) ((GstVideoOriginMeta *) obj)

typedef struct _GstVideoOriginMeta GstVideoOriginMeta;

/**
* GstVideoOriginMeta:
* @meta: Parent #GstMeta
* @width: The width of the origin/source frame.
* @height: The height of the origin/source frame.
* @crop: The crop rectangle in origin/source frame
*     (filled with zero if no crop was done).
*
* Extra buffer metadata describing the origin/source frame
* from which this one was produced.
*/
struct _GstVideoOriginMeta {
  GstMeta           meta;

  guint             width;
  guint             height;

  GstVideoRectangle crop;
};

GST_VIDEO_API GType
gst_video_origin_meta_api_get_type (void);

GST_VIDEO_API const GstMetaInfo *
gst_video_origin_meta_get_info (void);

/**
 * gst_buffer_add_video_origin_meta:
 * @buffer: A #GstBuffer
 * @width: The width of the origin/source frame.
 * @height: The height of the origin/source frame.
 *
 * Attaches GstVideoOriginMeta metadata to @buffer with the given parameters.
 *
 * Returns: (transfer none): The #GstVideoOriginMeta on @buffer.
 */
GST_VIDEO_API GstVideoOriginMeta *
gst_buffer_add_video_origin_meta (GstBuffer * buffer, guint width,
                                  guint height);

/**
 * gst_buffer_get_video_origin_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstVideoOriginMeta on @buffer.
 *
 * Returns: (transfer none) (nullable): the #GstVideoOriginMeta or %NULL when
 *          there is no such metadata on @buffer.
 */
GST_VIDEO_API GstVideoOriginMeta *
gst_buffer_get_video_origin_meta (GstBuffer * buffer);

G_END_DECLS

#endif /* __GST_VIDEO_ORIGIN_META_H__ */
