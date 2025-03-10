/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_VIDEO_UTILS_H__
#define __GST_QTI_VIDEO_UTILS_H__

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_ROI_META_CAST(obj) ((GstVideoRegionOfInterestMeta *) obj)

#define GST_CAPS_FEATURE_MEMORY_GBM  "memory:GBM"

/**
 * gst_gbm_qcom_backend_is_supported:
 *
 * Helper function for checking whether the QCOM GBM backend is supported.
 *
 * return: TRUE if supported or FALSE if not supported
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
 * return: TRUE if supported or FALSE if not supported
 */
GST_VIDEO_API gboolean
gst_video_retrieve_gpu_alignment (GstVideoInfo * info, GstVideoAlignment * align);

/**
 * gst_video_calculate_common_alignment:
 *
 * Helper function for calculating the commmon alignment between two video
 * alignment structures.
 *
 * return: Video alignment struct with calculated common values
 */
GST_VIDEO_API GstVideoAlignment
gst_video_calculate_common_alignment (GstVideoAlignment * l_align,
                                      GstVideoAlignment * r_align);

/**
 * gst_query_get_video_alignment:
 * @query: #GstQuery with allocation information.
 * @align: #GstVideoAlignment from the GST_VIDEO_META in the query.
 *
 * Helper function to parse the query to get video alignment from allocation
 * meta.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_query_get_video_alignment (GstQuery * query, GstVideoAlignment * align);

/**
 * gst_buffer_get_video_region_of_interest_metas_parent_id:
 * @buffer: The #GstBuffer to which to copy the meta.
 * @parent_id: The metadata parent ID for which to check.
 *
 * Helper function for finding all GstVideoRegionOfInterestMeta on buffer with
 * the given parent id.
 *
 * return: Pointer to list of #GstVideoRegionOfInterestMeta if any where found
 */
GST_VIDEO_API GList *
gst_buffer_get_video_region_of_interest_metas_parent_id (GstBuffer * buffer,
                                                         const gint parent_id);

/**
 * gst_buffer_copy_video_region_of_interest_meta:
 * @buffer: The #GstBuffer to which to copy the meta.
 * @meta: The #GstVideoRegionOfInterestMeta which will be copied.
 *
 * Helper function for copying ROI meta belonging to a different buffer into another.
 *
 * return: Pointer to the newly allocated ROI meta
 */
GST_VIDEO_API GstVideoRegionOfInterestMeta *
gst_buffer_copy_video_region_of_interest_meta (GstBuffer * buffer,
                                               GstVideoRegionOfInterestMeta * meta);

/**
 * gst_video_region_of_interest_coordinates_correction:
 * @meta: The #GstVideoRegionOfInterestMeta whos coordinates will be corrected.
 * @source: Source offset coordinates and dimensions for scale calculation.
 * @destination: Destination offset coordinates and dimensions for scale calculation.
 *
 * Helper function for correcting region coordinates based on a source and
 * destionation rectangles. Used primarily when transfering ROI meta from one
 * buffer into another with some source to destination transformation.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_video_region_of_interest_coordinates_correction (
    GstVideoRegionOfInterestMeta * roimeta, GstVideoRectangle * source,
    GstVideoRectangle * destination);

G_END_DECLS

#endif // __GST_QTI_VIDEO_UTILS_H__
