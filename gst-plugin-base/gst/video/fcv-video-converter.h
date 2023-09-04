/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_FCV_VIDEO_CONVERTER_H__
#define __GST_FCV_VIDEO_CONVERTER_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

// Composition flags valid only for the input frame.
#define GST_FCV_FLAG_FLIP_HORIZONTAL  (1 << 0)
#define GST_FCV_FLAG_FLIP_VERTICAL    (1 << 1)
#define GST_FCV_FLAG_ROTATE_90CW      (1 << 2)
#define GST_FCV_FLAG_ROTATE_180       (2 << 2)
#define GST_FCV_FLAG_ROTATE_90CCW     (3 << 2)

// Composition flags valid only for the output frame.
#define GST_FCV_FLAG_CLEAR_BACKGROUND (1 << 6)

#define GST_FCV_BLIT_INIT { NULL, NULL, NULL, 0, 0 }
#define GST_FCV_COMPOSITION_INIT { NULL, 0, NULL, 0, 0 }

/**
 * GstFcvOpMode:
 * @GST_FCV_OP_MODE_LOW_POWER: uses lowest power consuming implementation.
 * @GST_FCV_OP_MODE_PERFORMANCE: uses highest performance implementation.
 * @GST_FCV_OP_MODE_CPU_OFFLOAD: offloads as much of the CPU as possible.
 * @GST_FCV_OP_MODE_CPU_PERFORMANCE: uses CPU highest performance implementation.
 *
 * Defines operational mode of interface to allow the end developer to dictate
 * how the target optimized implementation should behave.
 */
typedef enum {
  GST_FCV_OP_MODE_LOW_POWER,
  GST_FCV_OP_MODE_PERFORMANCE,
  GST_FCV_OP_MODE_CPU_OFFLOAD,
  GST_FCV_OP_MODE_CPU_PERFORMANCE,
} GstFcvOpMode;

typedef struct _GstFcvVideoConverter GstFcvVideoConverter;
typedef struct _GstFcvBlit GstFcvBlit;
typedef struct _GstFcvComposition GstFcvComposition;


/**
 * GstFcvBlit:
 * @frame: Input video frame.
 * @sources: Source regions in the frame.
 * @destinations: Destination regions in the frame.
 * @n_regions: Number of Source - Destination region pairs.
 * @flags: Bitwise configuration mask for the input frame.
 *
 * Blit source. Input frame along with a possible crop and destination
 * rectangles and configuration mask.
 */
struct _GstFcvBlit
{
  GstVideoFrame     *frame;

  GstVideoRectangle *sources;
  GstVideoRectangle *destinations;
  guint8            n_regions;

  guint64           flags;
};

/**
 * GstFcvComposition:
 * @blits: Array of blit objects.
 * @n_blits: Number of blit objects.
 * @frame: Output video frame where the blit objects will be placed.
 * @bgcolor: Background color to be applied if CLEAR_BACKGROUND flag is present.
 * @flags: Bitwise configuration mask for the output.
 *
 * Blit composition.
 */
struct _GstFcvComposition
{
  GstFcvBlit    *blits;
  guint         n_blits;

  GstVideoFrame *frame;
  guint32       bgcolor;

  guint64       flags;
};

/**
 * gst_fcv_video_converter_new:
 *
 * Initialize instance of FastCV converter module.
 *
 * return: Pointer to FastCV converter on success or NULL on failure
 */
GST_VIDEO_API GstFcvVideoConverter *
gst_fcv_video_converter_new     (GstFcvOpMode opmode);

/**
 * gst_fcv_video_converter_free:
 * @convert: Pointer to FastCV converter module
 *
 * Deinitialise the FastCV converter instance.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_fcv_video_converter_free    (GstFcvVideoConverter * convert);

/**
 * gst_fcv_video_converter_compose:
 * @convert: Pointer to FastCV converter instance
 * @compositions: Array of composition frames
 * @n_compositions: Number of compositions
 *
 * Submit the a number of video composition which will be executed together.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_fcv_video_converter_compose (GstFcvVideoConverter * convert,
                                 GstFcvComposition * compositions,
                                 guint n_compositions);

G_END_DECLS

#endif // __GST_FCV_VIDEO_CONVERTER_H__
