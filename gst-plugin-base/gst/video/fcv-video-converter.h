/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_FCV_VIDEO_CONVERTER_H__
#define __GST_FCV_VIDEO_CONVERTER_H__

#include "video-converter-engine.h"

G_BEGIN_DECLS

typedef struct _GstFcvVideoConverter GstFcvVideoConverter;

/**
 * gst_fcv_video_converter_new:
 * @settings: Structure with optional settings.
 *
 * Initialize instance of FastCV converter backend.
 *
 * return: Pointer to FastCV converter on success or NULL on failure
 */
GST_VIDEO_API GstFcvVideoConverter *
gst_fcv_video_converter_new (GstStructure * settings);

/**
 * gst_fcv_video_converter_free:
 * @convert: Pointer to FastCV converter backend
 *
 * Deinitialise the FastCV converter.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_fcv_video_converter_free (GstFcvVideoConverter * convert);

/**
 * gst_fcv_video_converter_compose:
 * @convert: Pointer to FastCV converter backend.
 * @compositions: Array of composition frames.
 * @n_compositions: Number of compositions.
 * @fence: Optional fence to be filled if provided and used for async operation.
 *
 * Submit the a number of video composition which will be executed together.
 *
 * return: Pointer to request on success or NULL on failure
 */
GST_VIDEO_API gboolean
gst_fcv_video_converter_compose (GstFcvVideoConverter * convert,
                                 GstVideoComposition * compositions,
                                 guint n_compositions, gpointer * fence);

/**
 * gst_fcv_video_converter_wait_fence:
 * @convert: Pointer to FastCV converter backend.
 * @fence: Asynchronously fence object associated with a compose request.
 *
 * Wait for the sumbitted to the GPU compositions to finish.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_fcv_video_converter_wait_fence (GstFcvVideoConverter * convert,
                                    gpointer fence);

/**
 * gst_fcv_video_converter_flush:
 * @convert: Pointer to FastCV converter backend.
 *
 * Wait for compositions sumbitted to the GPU to finish and flush cached data.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_fcv_video_converter_flush (GstFcvVideoConverter * convert);

G_END_DECLS

#endif // __GST_FCV_VIDEO_CONVERTER_H__
