/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_C2D_VIDEO_CONVERTER_H__
#define __GST_C2D_VIDEO_CONVERTER_H__

#include <gst/video/video-converter.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL:
 *
 * #G_TYPE_BOOLEAN, flip output horizontally, default FALSE
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL \
    "GstC2dVideoConverter.flip-horizontal"

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL:
 *
 * #G_TYPE_BOOLEAN, flip output horizontally, default FALSE
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL \
    "GstC2dVideoConverter.flip-vertical"

/**
 * GstC2dVideoRotateMode:
 * @GST_C2D_VIDEO_ROTATE_NONE: disable rotation of the output
 * @GST_C2D_VIDEO_ROTATE_90_CW: rotate output 90 degrees clockwise
 * @GST_C2D_VIDEO_ROTATE_90_CCW: rotate output 90 degrees counter-clockwise
 * @GST_C2D_VIDEO_ROTATE_180: rotate output 180 degrees
 *
 * Different output rotation modes
 */
typedef enum {
  GST_C2D_VIDEO_ROTATE_NONE,
  GST_C2D_VIDEO_ROTATE_90_CW,
  GST_C2D_VIDEO_ROTATE_90_CCW,
  GST_C2D_VIDEO_ROTATE_180,
} GstC2dVideoRotateMode;

GST_VIDEO_API GType gst_c2d_video_rotate_mode_get_type (void);
#define GST_TYPE_C2D_VIDEO_ROTATE_MODE (gst_c2d_video_rotate_mode_get_type())

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_ROTATION:
 *
 * #GST_TYPE_C2D_VIDEO_ROTATE_MODE, set the output rotation flags
 * Default is #GST_C2D_VIDEO_ROTATE_NONE.
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_ROTATE_MODE \
    "GstC2dVideoConverter.rotate-mode"

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_SRC_X:
 *
 * #G_TYPE_INT, source x position to start conversion, default 0
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_SRC_X \
    "GstC2dVideoConverter.src-x"

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_SRC_Y:
 *
 * #G_TYPE_INT, source y position to start conversion, default 0
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_SRC_Y \
    "GstC2dVideoConverter.src-y"

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_SRC_WIDTH:
 *
 * #G_TYPE_INT, source width to convert, default source width
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_SRC_WIDTH \
    "GstC2dVideoConverter.src-width"

/**
 * GST_C2D_VIDEO_CONVERTER_OPT_SRC_HEIGHT:
 *
 * #G_TYPE_INT, source height to convert, default source height
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_SRC_HEIGHT \
    "GstC2dVideoConverter.src-height"

/**
 * GST_VIDEO_CONVERTER_OPT_DEST_X:
 *
 * #G_TYPE_INT, x position in the destination frame, default 0
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_DEST_X \
    "GstC2dVideoConverter.dest-x"
/**
 * GST_VIDEO_CONVERTER_OPT_DEST_Y:
 *
 * #G_TYPE_INT, y position in the destination frame, default 0
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_DEST_Y \
    "GstC2dVideoConverter.dest-y"
/**
 * GST_VIDEO_CONVERTER_OPT_DEST_WIDTH:
 *
 * #G_TYPE_INT, width in the destination frame, default destination width
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_DEST_WIDTH \
    "GstC2dVideoConverter.dest-width"
/**
 * GST_VIDEO_CONVERTER_OPT_DEST_HEIGHT:
 *
 * #G_TYPE_INT, height in the destination frame, default destination height
 */
#define GST_C2D_VIDEO_CONVERTER_OPT_DEST_HEIGHT \
    "GstC2dVideoConverter.dest-height"

typedef struct _GstC2dVideoConverter GstC2dVideoConverter;

GST_VIDEO_API GstC2dVideoConverter *
gst_c2d_video_converter_new        (GstVideoInfo *input,
                                    GstVideoInfo *output,
                                    GstStructure *config);

GST_VIDEO_API void
gst_c2d_video_converter_free       (GstC2dVideoConverter *convert);

GST_VIDEO_API gboolean
gst_c2d_video_converter_set_config (GstC2dVideoConverter *convert,
                                    GstStructure *configuration);

GST_VIDEO_API const GstStructure *
gst_c2d_video_converter_get_config (GstC2dVideoConverter *convert);

GST_VIDEO_API void
gst_c2d_video_converter_frame      (GstC2dVideoConverter *convert,
                                    const GstVideoFrame *inframe,
                                    GstVideoFrame *outframe);

G_END_DECLS

#endif /* __GST_C2D_VIDEO_CONVERTER_H__ */
