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
typedef struct _GstVideoQuadrilateral GstVideoQuadrilateral;

/**
 * GstVideoDataType:
 * @GST_VIDEO_DATA_TYPE_U8: Unsigned integer 8-bits.
 * @GST_VIDEO_DATA_TYPE_I8: Signed integer 8-bits.
 * @GST_VIDEO_DATA_TYPE_U16: Unsigned integer 16-bits.
 * @GST_VIDEO_DATA_TYPE_I16: Signed integer 16-bits.
 * @GST_VIDEO_DATA_TYPE_U32: Unsigned integer 32-bits.
 * @GST_VIDEO_DATA_TYPE_I32: Signed integer 32-bits.
 * @GST_VIDEO_DATA_TYPE_U64: Unsigned integer 64-bits.
 * @GST_VIDEO_DATA_TYPE_I64: Signed integer 64-bits.
 * @GST_VIDEO_DATA_TYPE_F16: Floating point 16-bits.
 * @GST_VIDEO_DATA_TYPE_F32: Floating point 32-bits.
 *
 * The size and type of the data contained in each component of a video pixel.
 */
typedef enum {
  GST_VIDEO_DATA_TYPE_U8,
  GST_VIDEO_DATA_TYPE_I8,
  GST_VIDEO_DATA_TYPE_U16,
  GST_VIDEO_DATA_TYPE_I16,
  GST_VIDEO_DATA_TYPE_U32,
  GST_VIDEO_DATA_TYPE_I32,
  GST_VIDEO_DATA_TYPE_U64,
  GST_VIDEO_DATA_TYPE_I64,
  GST_VIDEO_DATA_TYPE_F16,
  GST_VIDEO_DATA_TYPE_F32
} GstVideoDataType;

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
 * GstVideoQuadrilateral:
 * @a: Upper-left point coordinate.
 * @b: Bottom-left point coordinate.
 * @c: Upper-right point coordinate.
 * @d: Bottom-right point coordinate.
 *
 * Quadrilateral defined with the coordinates of its 4 points.
 *
 *  a               c
 *   +-------------+
 *   |             |
 *   |             |
 *   |             |
 *   +-------------+
 *  b               d
 */
struct _GstVideoQuadrilateral
{
  GstVideoPoint a;
  GstVideoPoint b;
  GstVideoPoint c;
  GstVideoPoint d;
};

/**
 * gst_video_data_type_get_size:
 * @datatype: A #GstVideoDataType for a video frame.
 *
 * Helper function for getting the size of the data type for a video frame.
 *
 * Returns: size of the data type in bytes.
 */
GST_VIDEO_API guint
gst_video_data_type_get_size (GstVideoDataType datatype);

/**
 * gst_video_data_type_to_string:
 * @datatype: A #GstVideoDataType for a video frame.
 *
 * Helper function for getting the string name of given data type.
 *
 * Returns: (transfer none) (nullable): The name of the data type.
 */
GST_VIDEO_API const gchar *
gst_video_data_type_to_string (GstVideoDataType datatype);

/**
 * gst_video_point_affine_transform:
 * @point: A #GstVideoPoint to which the affine matrix will be applied.
 * @matrix: (array fixed-size=9) (element-type gdouble):
 *          A 3x3 affine transformation matrix.
 *
 * Helper function for adjusting coordinates of a 2D point with affine matrix.
 */
GST_VIDEO_API void
gst_video_point_affine_transform (GstVideoPoint * point, gdouble matrix[3][3]);

/**
 * gst_video_quadrilateral_is_rectangle:
 * @quadrilateral: A #GstVideoQuadrilateral
 *
 * Helper function for checking whether a #GstVideoQuadrilateral is rectangular.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_video_quadrilateral_is_rectangle (const GstVideoQuadrilateral * quadrilateral);

/**
 * gst_video_quadrilateral_from_rectangle:
 * @quadrilateral: A #GstVideoQuadrilateral
 * @rectangle: A #GstVideoRectangle
 *
 * Helper function for converting a rectangle into a #GstVideoQuadrilateral.
 */
GST_VIDEO_API void
gst_video_quadrilateral_from_rectangle (GstVideoQuadrilateral * quadrilateral,
                                        const GstVideoRectangle * rectangle);

/**
 * gst_video_quadrilateral_to_rectangle:
 * @quadrilateral: A #GstVideoQuadrilateral
 * @rectangle: A #GstVideoRectangle
 *
 * Helper function for converting a rectangular quadrilateral into the more
 * convinient #GstVideoRectangle.
 */
GST_VIDEO_API void
gst_video_quadrilateral_to_rectangle (const GstVideoQuadrilateral * quadrilateral,
                                      GstVideoRectangle * rectangle);

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

/**
 * gst_video_frame_normalize_ip:
 * @vframe: A #GstVideoFrame
 * @datatype: The #GstVideoDataType in the video frame
 * @offsets: (array fixed-size=4) (element-type gdouble):
 *           Component offset factors, used in the normalize operation.
 * @scales: (array fixed-size=4) (element-type gdouble):
 *          Component scale factors, used in the normalize operation.
 *
 * Helper function for normalizing video frame inplace.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_video_frame_normalize_ip (GstVideoFrame * vframe, GstVideoDataType datatype,
                              gdouble offsets[GST_VIDEO_MAX_COMPONENTS],
                              gdouble scales[GST_VIDEO_MAX_COMPONENTS]);

G_END_DECLS

#endif // __GST_QTI_VIDEO_UTILS_H__
