/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_CONVERTER_ENGINE_H__
#define __GST_VIDEO_CONVERTER_ENGINE_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_video_converter_engine_debug);


// Composition flags valid only for the output frame.
#define GST_VCE_FLAG_F32_FORMAT      (1 << 0)
#define GST_VCE_FLAG_F16_FORMAT      (2 << 0)
#define GST_VCE_FLAG_I32_FORMAT      (3 << 0)
#define GST_VCE_FLAG_U32_FORMAT      (4 << 0)

#define GST_VCE_BLIT_INIT \
    { NULL, FALSE, NULL, NULL, 0, 255, GST_VCE_ROTATE_0, GST_VCE_FLIP_NONE }
#define GST_VCE_COMPOSITION_INIT \
    { NULL, 0, NULL, FALSE, 0, FALSE, { 0.0, 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0, 0.0 }, 0 }

// Maximum number of image channels, used for normalization offsets and scales.
#define GST_VCE_MAX_CHANNELS         4

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

/**
 * GstVideoConvBackend:
 * @GST_VCE_BACKEND_C2D: Use C2D based video converter.
 * @GST_VCE_BACKEND_GLES: Use OpenGLES based video converter.
 * @GST_VCE_BACKEND_FCV: Use FastCV based video converter.
 *
 * The backend of the video converter engine.
 */
typedef enum {
  GST_VCE_BACKEND_C2D,
  GST_VCE_BACKEND_GLES,
  GST_VCE_BACKEND_FCV,
} GstVideoConvBackend;

GST_VIDEO_API GType gst_video_converter_backend_get_type (void);
#define GST_TYPE_VCE_BACKEND (gst_video_converter_backend_get_type())

/**
 * GstVideoConvRotate:
 * @GST_VCE_ROTATE_0: No rotation.
 * @GST_VCE_ROTATE_90: Rotate frame 90 degrees clockwise.
 * @GST_VCE_ROTATE_180: Rotate frame 180 degrees clockwise.
 * @GST_VCE_ROTATE_270: Rotate frame 270 degrees clockwise.
 *
 * Rotation degrees.
 */
typedef enum {
  GST_VCE_ROTATE_0   = 0,
  GST_VCE_ROTATE_90  = 90,
  GST_VCE_ROTATE_180 = 180,
  GST_VCE_ROTATE_270 = 270,
} GstVideoConvRotate;

/**
 * GstVideoConvFlip:
 * @GST_VCE_FLIP_NONE: No flip.
 * @GST_VCE_FLIP_HORIZONTAL: Flip frame horizontally.
 * @GST_VCE_FLIP_VERTICAL: Flip frame vertically.
 * @GST_VCE_FLIP_BOTH: Flip frame both horizontally and vertically.
 *
 * Flip direction.
 */
typedef enum {
  GST_VCE_FLIP_NONE       = 0,
  GST_VCE_FLIP_HORIZONTAL = 1,
  GST_VCE_FLIP_VERTICAL   = 2,
  GST_VCE_FLIP_BOTH       = 3,
} GstVideoConvFlip;

/**
 * GstVideoBlit:
 * @frame: Input video frame.
 * @isubwc: Whether the frame has Universal Bandwidth Compression.
 * @sources: Source regions in the frame.
 * @destinations: Destination regions in the frame.
 * @n_regions: Number of Source - Destination region pairs.
 * @alpha: Global alpha, 0 = fully transparent, 255 = fully opaque.
 * @rotate: The degrees at which the frame will be rotatte.
 * @flip: The directions at which the frame will be flipped.
 *
 * Blit object. Input frame along with a possible crop and destination
 * rectangles and configuration mask.
 */
struct _GstVideoBlit
{
  GstVideoFrame      *frame;
  gboolean           isubwc;

  GstVideoRectangle  *sources;
  GstVideoRectangle  *destinations;
  guint8             n_regions;

  guint8             alpha;
  GstVideoConvRotate rotate;
  GstVideoConvFlip   flip;
};

/**
 * GstVideoComposition:
 * @blits: Array of blit objects.
 * @n_blits: Number of blit objects.
 * @frame: Output video frame where the blit objects will be placed.
 * @isubwc: Whether the frame has Universal Bandwidth Compression.
 * @bgcolor: Background color to be applied if bgfill is set to TRUE.
 * @bgfill: Whether to fill the background of the frame image with bgcolor.
 * @offsets: Channel offset factors, used in normalize float operation.
 * @scales: Channel scale factors, used in normalize float operation.
 * @flags: Bitwise configuration mask for the output.
 *
 * Blit composition.
 */
struct _GstVideoComposition
{
  GstVideoBlit   *blits;
  guint         n_blits;

  GstVideoFrame *frame;
  gboolean      isubwc;

  guint32       bgcolor;
  gboolean      bgfill;

  gdouble       offsets[GST_VCE_MAX_CHANNELS];
  gdouble       scales[GST_VCE_MAX_CHANNELS];

  guint64       flags;
};

/**
 * gst_video_converter_default_backend:
 *
 * Retrieve the default vide converter backend.
 *
 * return: #GstVideoConvBackend
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
 * return: Pointer to video converter on success or NULL on failure
 */
GST_VIDEO_API GstVideoConvEngine *
gst_video_converter_engine_new (GstVideoConvBackend backend,
                                GstStructure * settings);

/**
 * gst_video_converter_engine_free:
 * @engine: Pointer to video converter engine.
 *
 * Deinitialise the video converter engine.
 *
 * return: NONE
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
 * return: TRUE on success or FALSE on failure
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
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_video_converter_engine_wait_fence (GstVideoConvEngine * engine,
                                       gpointer fence);

/**
 * gst_video_converter_engine_flush:
 * @engine: Pointer to video converter engine.
 *
 * Wait for compositions sumbitted to the engine to finish and flush cached data.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_video_converter_engine_flush (GstVideoConvEngine * engine);

G_END_DECLS

#endif // __GST_VIDEO_CONVERTER_ENGINE_H__
