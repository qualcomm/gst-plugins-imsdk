/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_VIDEO_UTILS_H__
#define __GST_QTI_VIDEO_UTILS_H__

#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * gst_is_gbm_supported:
 *
 * Helper function for checking whether the QCOM GBM backend is supported.
 *
 * TODO: Rename the function to gst_gbm_qcom_backend_is_supported in order to
 * better reflect its purpose.
 *
 * return: TRUE if supported or FALSE if not supported
 */
GST_VIDEO_API gboolean
gst_is_gbm_supported (void);

/**
 * gst_adreno_utils_compute_alignment:
 * @width: Width of the image in pixels.
 * @height: Height of the image in pixels.
 * @format: The format of the image.
 * @stride: Calculated width with alignment in bytes.
 * @scanline: Calculated height with alignment in bytes.
 *
 * Helper function for calculating stride and scanline for given image width
 * and height based on the format and alignment requirements of the adreno GPU.
 *
 * TODO: This function needs to be replaced with a function for properly
 * requesting the hardware alignment requirements.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_adreno_utils_compute_alignment(guint width, guint height,
                                   GstVideoFormat format, gint * stride,
                                   gint * scanline);

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

G_END_DECLS

#endif // __GST_QTI_VIDEO_UTILS_H__
