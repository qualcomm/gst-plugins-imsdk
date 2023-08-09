/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_FCV_VIDEO_CONVERTER_H__
#define __GST_FCV_VIDEO_CONVERTER_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_SRC_RECTANGLES
 *
 * #G_TYPE_ARRAY: Array of source GstVideoRectangle.
 * Default: NULL
 *
 * Not applicable for output.
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_SRC_RECTANGLES \
    "GstFcvVideoConverter.source-rectangles"

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_DEST_RECTANGLES
 *
 * #G_TYPE_ARRAY: Array of destination GstVideoRectangle.
 * Default: NULL
 *
 * Not applicable for output.
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_DEST_RECTANGLES \
    "GstFcvVideoConverter.destination-rectangles"

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL:
 *
 * #G_TYPE_BOOLEAN, flip output horizontally
 * Default: FALSE
 *
 * Not applicable for output
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL \
    "GstFcvVideoConverter.flip-horizontal"

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_FLIP_VERTICAL:
 *
 * #G_TYPE_BOOLEAN, flip output horizontally
 * Default: FALSE
 *
 * Not applicable for output
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_FLIP_VERTICAL \
    "GstFcvVideoConverter.flip-vertical"

/**
 * GstFcvVideoRotate:
 * @GST_FCV_VIDEO_ROTATE_NONE: disable rotation of the output
 * @GST_FCV_VIDEO_ROTATE_90_CW: rotate output 90 degrees clockwise
 * @GST_FCV_VIDEO_ROTATE_90_CCW: rotate output 90 degrees counter-clockwise
 * @GST_FCV_VIDEO_ROTATE_180: rotate output 180 degrees
 *
 * Different output rotation modes
 */
typedef enum {
  GST_FCV_VIDEO_ROTATE_NONE,
  GST_FCV_VIDEO_ROTATE_90_CW,
  GST_FCV_VIDEO_ROTATE_90_CCW,
  GST_FCV_VIDEO_ROTATE_180,
} GstFcvVideoRotate;

GST_VIDEO_API GType gst_fcv_video_rotation_get_type (void);
#define GST_TYPE_FCV_VIDEO_ROTATION (gst_fcv_video_rotation_get_type())

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_ROTATION:
 *
 * #GST_TYPE_FCV_VIDEO_ROTATION, set the output rotation flags
 * Default: #GST_FCV_VIDEO_ROTATE_NONE.
 *
 * Not applicable for output
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_ROTATION \
    "GstFcvVideoConverter.rotation"

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_BACKGROUND:
 *
 * #G_TYPE_UINT, background color
 * Default: 0x00000000
 *
 * Not applicable for input.
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_BACKGROUND \
    "GstFcvVideoConverter.background"

/**
 * GST_FCV_VIDEO_CONVERTER_OPT_CLEAR:
 *
 * #G_TYPE_BOOLEAN, clear image pixels and apply background color
 * Default: TRUE
 *
 * Not applicable for input.
 */
#define GST_FCV_VIDEO_CONVERTER_OPT_CLEAR \
    "GstFcvVideoConverter.clear-background"

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
typedef struct _GstFcvComposition GstFcvComposition;

/**
 * GstFcvComposition:
 * @inframes: Array of input video frames
 * @n_inputs: Number of input frames
 * @outframe: Output video frame
 *
 * Blit composition. Input frames will be placed in the output frame based
 * on a previously set configuration.
 */
struct _GstFcvComposition
{
  GstVideoFrame *inframes;
  guint         n_inputs;
  GstVideoFrame *outframe;
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
 * gst_fcv_video_converter_set_input_opts:
 * @convert: Pointer to FastCV converter instance
 * @index: Input frame index
 * @opts: Pointer to structure containing options
 *
 * Configure source and destination rectangles, rotation, and other parameters
 * that are going to be used on the input frame with given index. Index is
 * accumulative across all compositions. If there are 3 compositions each with
 * 2 input frames then the indexes for the 3rd composition will be 5 and 6.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_fcv_video_converter_set_input_opts (GstFcvVideoConverter * convert,
                                        guint index, GstStructure * opts);

/**
 * gst_fcv_video_converter_set_process_opts:
 * @convert: Pointer to FastCV converter instance
 * @index: Output frame index.
 * @opts: Pointer to structure containing options
 *
 * Configure a set of operations that will be performed on the output frame
 * for each composition. The index will corresppond to the index of the
 * video frame composition submited in submit_request API as each one contains
 * a single output frame.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_fcv_video_converter_set_output_opts (GstFcvVideoConverter * convert,
                                         guint index, GstStructure * opts);

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
