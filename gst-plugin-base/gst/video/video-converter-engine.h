/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_CONVERTER_ENGINE_H__
#define __GST_VIDEO_CONVERTER_ENGINE_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>
#include <gst/video/video-utils.h>
#include <gst/video/video-converter-engine-param.h>

G_BEGIN_DECLS

typedef struct _GstVideoConvEngine GstVideoConvEngine;

#define GST_TYPE_VIDEO_CONVERTER_ENGINE (gst_video_converter_engine_get_type())
G_DECLARE_FINAL_TYPE (GstVideoConvEngine, gst_video_converter_engine, GST,
    VIDEO_CONVERTER_ENGINE, GObject)

/**
 * GstVideoConvBackend:
 * @GST_VCE_BACKEND_NONE: Do not use any backend
 * @GST_VCE_BACKEND_C2D: Use C2D based video converter.
 * @GST_VCE_BACKEND_GLES: Use OpenGLES based video converter.
 * @GST_VCE_BACKEND_FCV: Use FastCV based video converter.
 * @GST_VCE_BACKEND_OCV: Use OpenCV based video converter.
 *
 * The backend of the video converter engine.
 */
typedef enum {
  GST_VCE_BACKEND_NONE,
  GST_VCE_BACKEND_C2D,
  GST_VCE_BACKEND_GLES,
  GST_VCE_BACKEND_FCV,
  GST_VCE_BACKEND_OCV,
} GstVideoConvBackend;

GST_VIDEO_API GType gst_video_converter_backend_get_type (void);
#define GST_TYPE_VCE_BACKEND (gst_video_converter_backend_get_type())

/**
 * gst_video_converter_default_backend:
 *
 * Retrieve the default vide converter backend.
 *
 * Returns: the default #GstVideoConvBackend
 */
GST_VIDEO_API GstVideoConvBackend
gst_video_converter_default_backend (void);

/**
 * gst_video_converter_engine_new:
 * @backend: The type of the underlying converter.
 * @settings: Structure with backend specific options.
 *
 * Initialize instance of video converter engine.
 *
 * Returns: (transfer full): #GstVideoConvEngine on success or NULL on failure
 */
GST_VIDEO_API GstVideoConvEngine *
gst_video_converter_engine_new (GstVideoConvBackend backend,
                                GstStructure * settings);

/**
 * gst_video_converter_engine_free:
 * @engine: Pointer to video converter engine.
 *
 * Deinitialise the video converter engine.
 */
GST_VIDEO_API void
gst_video_converter_engine_free (GstVideoConvEngine * engine);

/**
 * gst_video_converter_engine_compose:
 * @engine: Pointer to video converter engine.
 * @compositions: Array of composition frames.
 * @n_compositions: Number of compositions.
 * @fence: Optional fence to be filled if provided and used for async operation.
 *
 * Submit the a number of video composition which will be executed together.
 *
 * An optional fence object may be passed to be filled by the engine in which
 * case the compsition operations will be performed asynchronously. If the
 * fence object is left NULL then the operation is performed synchronously.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_video_converter_engine_compose (GstVideoConvEngine * engine,
                                    GstVideoComposition * compositions,
                                    guint n_compositions, gpointer * fence);

/**
 * gst_video_converter_engine_wait_fence:
 * @engine: Pointer to video converter engine.
 * @fence: Asynchronously fence object associated with a compose request.
 *
 * Wait for the sumbitted to the engine compositions to finish.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_video_converter_engine_wait_fence (GstVideoConvEngine * engine,
                                       gpointer fence);

/**
 * gst_video_converter_engine_flush:
 * @engine: Pointer to video converter engine.
 *
 * Wait for compositions sumbitted to the engine to finish and flush cached data.
 */
GST_VIDEO_API void
gst_video_converter_engine_flush (GstVideoConvEngine * engine);

G_END_DECLS

#endif // __GST_VIDEO_CONVERTER_ENGINE_H__
