/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_CONVERTER_ENGINE_H__
#define __GST_VIDEO_CONVERTER_ENGINE_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>
#include <gst/video/video-utils.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_video_converter_engine_debug);

// Bitwise flags for the configuration mask in #GstVideoBlit.
#define GST_VCE_MASK_SOURCE          (1 << 0)
#define GST_VCE_MASK_DESTINATION     (1 << 1)
#define GST_VCE_MASK_FLIP_VERTICAL   (1 << 2)
#define GST_VCE_MASK_FLIP_HORIZONTAL (1 << 3)
#define GST_VCE_MASK_ROTATION        (1 << 4)

#define GST_VCE_BLIT_INIT \
    { NULL, NULL, 0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}}, \
        {0, 0, 0, 0}, 255, GST_VIDEO_ROTATE_0 }
#define GST_VCE_COMPOSITION_INIT \
    { NULL, 0, NULL, NULL, 0, FALSE, { 0.0, 0.0, 0.0, 0.0 }, \
      { 1.0, 1.0, 1.0, 1.0 }, GST_VIDEO_DATA_TYPE_U8 }

/**
 * GST_VCE_OPT_FCV_OP_MODE:
 *
 * #GstFcvOpMode, set the operational mode of the FastCV converter
 * Default: #GST_FCV_OP_MODE_LOW_POWER.
 */
#define GST_VCE_OPT_FCV_OP_MODE "fcv-op-mode"

typedef struct _GstVideoConvEngine GstVideoConvEngine;
typedef struct _GstVideoBlit GstVideoBlit;
typedef struct _GstVideoComposition GstVideoComposition;

#define GST_TYPE_VIDEO_CONVERTER_ENGINE (gst_video_converter_engine_get_type())
G_DECLARE_FINAL_TYPE (GstVideoConvEngine, gst_video_converter_engine, GST,
    VIDEO_CONVERTER_ENGINE, GObject)

/**
 * GstFcvOpMode:
 * @GST_FCV_OP_MODE_LOW_POWER: Uses lowest power consuming implementation.
 * @GST_FCV_OP_MODE_PERFORMANCE: Uses highest performance implementation.
 * @GST_FCV_OP_MODE_CPU_OFFLOAD: Offloads as much of the CPU as possible.
 * @GST_FCV_OP_MODE_CPU_PERFORMANCE: Uses CPU highest performance implementation.
 *
 * Defines operational mode for the underlying FastCV based engine.
 */
typedef enum {
  GST_FCV_OP_MODE_LOW_POWER,
  GST_FCV_OP_MODE_PERFORMANCE,
  GST_FCV_OP_MODE_CPU_OFFLOAD,
  GST_FCV_OP_MODE_CPU_PERFORMANCE,
} GstFcvOpMode;

GST_API GType gst_fcv_op_mode_get_type (void);
#define GST_TYPE_FCV_OP_MODE (gst_fcv_op_mode_get_type())

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
 * GstVideoBlit:
 * @buffer: Input buffer.
 * @info: GstVideoInfo for mapping.
 * @mask: Bitwise configuration mask.
 * @source: Source quadrilateral in the input frame.
 * @destination: Destination rectangle in the output frame.
 * @alpha: Global alpha, 0 = fully transparent, 255 = fully opaque.
 * @rotate: The degrees at which the frame will be rotatte.
 * @flip: The directions at which the frame will be flipped.
 *
 * Blit object. Input buffer along with a possible crop and destination
 * rectangles, configuration mask and info for mapping.
 */
struct _GstVideoBlit
{
  GstBuffer             *buffer;
  GstVideoInfo          *info;

  guint32               mask;

  GstVideoQuadrilateral source;
  GstVideoRectangle     destination;

  guint8                alpha;
  GstVideoRotate        rotate;
};

/**
 * GstVideoComposition:
 * @blits: An array of #GstVideoBlit objects.
 * @n_blits: Number of #GstVideoBlit objects in the @blits array.
 * @buffer: The #GstBuffer used as output.
 * @info: A #GstVideoInfo for mapping.
 * @bgcolor: Background color to be applied if bgfill is set to TRUE.
 * @bgfill: Whether to fill the background of the frame image with bgcolor.
 * @offsets: (array fixed-size=4) (element-type gdouble):
 *           Component offset factors, used in the normalize operation.
 * @scales: (array fixed-size=4) (element-type gdouble):
 *          Component scale factors, used in the normalize operation.
 * @datatype: The #GstVideoDataType of the pixels in the output frame.
 *
 * Blit composition.
 */
struct _GstVideoComposition
{
  GstVideoBlit     *blits;
  guint            n_blits;

  GstBuffer        *buffer;
  GstVideoInfo     *info;

  guint32          bgcolor;
  gboolean         bgfill;

  gdouble          offsets[GST_VIDEO_MAX_COMPONENTS];
  gdouble          scales[GST_VIDEO_MAX_COMPONENTS];

  GstVideoDataType datatype;
};

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
