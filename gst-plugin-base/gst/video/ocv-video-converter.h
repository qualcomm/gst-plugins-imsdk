/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_OCV_VIDEO_CONVERTER_H__
#define __GST_OCV_VIDEO_CONVERTER_H__

#include "video-converter-engine.h"

G_BEGIN_DECLS

typedef struct _GstOcvVideoConverter GstOcvVideoConverter;

/**
 * gst_ocv_video_converter_new:
 * @settings: Structure with optional settings.
 *
 * Initialize instance of OpenCV converter backend.
 *
 * return: Pointer to OpenCV converter on success or NULL on failure
 */
GST_VIDEO_API GstOcvVideoConverter *
gst_ocv_video_converter_new (GstStructure * settings);

/**
 * gst_ocv_video_converter_free:
 * @convert: Pointer to OpenCV converter backend
 *
 * Deinitialise the OpenCV converter.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_ocv_video_converter_free (GstOcvVideoConverter * convert);

/**
 * gst_ocv_video_converter_compose:
 * @convert: Pointer to OpenCV converter backend.
 * @compositions: Array of composition frames.
 * @n_compositions: Number of compositions.
 * @fence: Optional fence to be filled if provided and used for async operation.
 *
 * Submit the a number of video composition which will be executed together.
 *
 * return: Pointer to request on success or NULL on failure
 */
GST_VIDEO_API gboolean
gst_ocv_video_converter_compose (GstOcvVideoConverter * convert,
                                 GstVideoComposition * compositions,
                                 guint n_compositions, gpointer * fence);

/**
 * gst_ocv_video_converter_wait_fence:
 * @convert: Pointer to OpenCV converter backend.
 * @fence: Asynchronously fence object associated with a compose request.
 *
 * Wait for the sumbitted to the GPU compositions to finish.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_ocv_video_converter_wait_fence (GstOcvVideoConverter * convert,
                                    gpointer fence);

/**
 * gst_ocv_video_converter_flush:
 * @convert: Pointer to OpenCV converter backend.
 *
 * Wait for compositions sumbitted to the GPU to finish and flush cached data.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_ocv_video_converter_flush (GstOcvVideoConverter * convert);

G_END_DECLS

#endif // __GST_OCV_VIDEO_CONVERTER_H__
