/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_VIDEO_UTILS_H__
#define __GST_QTI_VIDEO_UTILS_H__

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_ROI_META_CAST(obj) ((GstVideoRegionOfInterestMeta *) obj)

#define GST_CAPS_FEATURE_MEMORY_GBM  "memory:GBM"

typedef struct _GstVideoPoint GstVideoPoint;

/**
 * GstVideoPoint:
 * @x: X Axis coordinate in pixels.
 * @y: Y Axis coordinate in pixels.
 *
 * Point coordinates in pixels.
 */
struct _GstVideoPoint
{
  gfloat x;
  gfloat y;
};

/**
 * gst_gbm_qcom_backend_is_supported:
 *
 * Helper function for checking whether the QCOM GBM backend is supported.
 *
 * Returns: TRUE if supported or FALSE if not supported
 */
GST_VIDEO_API gboolean
gst_gbm_qcom_backend_is_supported (void);

/**
 * gst_video_retrieve_gpu_alignment:
 * @info: #GstVideoInfo structure which will be adjusted with the alignment.
 * @align: #GstVideoAlignment structure which will populated.
 *
 * Helper function for retrieving the alignment requirements of the GPU.
 *
 * Returns: TRUE if supported or FALSE if not supported
 */
GST_VIDEO_API gboolean
gst_video_retrieve_gpu_alignment (GstVideoInfo * info, GstVideoAlignment * align);

/**
 * gst_video_alignment_update:
 * @align: the #GstVideoAlignment entry which will be updated
 * @otheralign: the other #GstVideoAlignment entry with which to do calculations
 *
 * Helper function for updating a video alignemnt strcuture wuth the calculated
 * commmon alignment between it and another video alignemnt structure.
 */
GST_VIDEO_API void
gst_video_alignment_update (GstVideoAlignment * align,
                            const GstVideoAlignment * otheralign);

/**
 * gst_query_parse_video_alignment:
 * @query: #GstQuery with allocation information.
 * @align: #GstVideoAlignment filled from the GST_VIDEO_META in the query.
 *
 * Helper function to parse the query to get video alignment from allocation
 * meta.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_query_parse_video_alignment (GstQuery * query, GstVideoAlignment * align);

/**
 * gst_buffer_get_video_region_of_interest_metas_parent_id:
 * @buffer: A #GstBuffer
 * @parent_id: A parent metadata id
 *
 * Find the #GstVideoRegionOfInterestMeta on @buffer with the given @parent_id.
 *
 * Buffers can contain multiple #GstVideoRegionOfInterestMeta metadata items.
 *
 * Returns: (transfer full) (element-type GstVideoRegionOfInterestMeta) (nullable):
 *          list of #GstVideoRegionOfInterestMeta with @parent_id or %NULL when
 *          there is no such metadata on @buffer.
 */
GST_VIDEO_API GList *
gst_buffer_get_video_region_of_interest_metas_parent_id (GstBuffer * buffer,
                                                         const gint parent_id);

/**
 * gst_buffer_copy_video_region_of_interest_meta: (skip):
 *
 * WARNING: INTERNAL USAGE ONLY. Subject to change.
 */
GST_VIDEO_API GstVideoRegionOfInterestMeta *
gst_buffer_copy_video_region_of_interest_meta (GstBuffer * buffer,
                                               GstVideoRegionOfInterestMeta * meta);

/**
 * gst_video_region_of_interest_meta_transform_coordinates: (skip):
 *
 * WARNING: INTERNAL USAGE ONLY. Subject to change.
 */
GST_VIDEO_API void
gst_video_region_of_interest_meta_transform_coordinates (
    GstVideoRegionOfInterestMeta * roimeta, const GstVideoRectangle * source,
    const GstVideoRectangle * destination);

/**
 * gst_buffer_has_valid_parent_meta:
 * @buffer: The #GstBuffer containing the metadata.
 * @parent_id: The parent metadata ID to validate.
 *
 * Helper function to check if the given parent ID refers to a valid
 * GstVideoRegionOfInterestMeta that is not of type "ImageRegion".
 * Used to determine whether a metadata entry should retain its parent
 * association for further processing.
 *
 * Returns: TRUE if the parent is not of type "ImageRegion", FALSE otherwise.
 */
GST_VIDEO_API gboolean
gst_buffer_has_valid_parent_meta (GstBuffer * buffer, gint parent_id);

/**
 * gst_video_point_affine_transform:
 * @point: #GstVideoPoint to which the affine matrix will be applied.
 * @matrix: Affine transformation matrix.
 *
 * Helper function for adjusting coordinates of a 2D point with affine matrix.
 */
GST_VIDEO_API void
gst_video_point_affine_transform (GstVideoPoint * point, gdouble matrix[3][3]);

/**
 * gst_video_info_modify_with_meta:
 * @info: #GstVideoInfo to write the correct values in
 * @meta: #GstVideoMeta from which to take the correct values
 *
 * Helper function to derive some information from GstVideoMeta
 *
 * Returns: TRUE if meta isn't null and the basic info matches in both structs
 */
GST_VIDEO_API gboolean
gst_video_info_modify_with_meta (GstVideoInfo * info, const GstVideoMeta * meta);

G_END_DECLS

#endif // __GST_QTI_VIDEO_UTILS_H__
