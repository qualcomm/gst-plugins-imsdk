/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_VIDEO_CONVERTER_ENGINE_PARAM_H__
#define __GST_VIDEO_CONVERTER_ENGINE_PARAM_H__

#include <gst/video/video.h>
#include <gst/video/video-utils.h>

G_BEGIN_DECLS

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

typedef struct _GstVideoBlit GstVideoBlit;
typedef struct _GstVideoBlits GstVideoBlits;
typedef struct _GstVideoComposition GstVideoComposition;

#define GST_TYPE_VIDEO_BLITS (gst_video_blits_get_type ())
GST_VIDEO_API GType gst_video_blits_get_type (void);

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

#define GST_TYPE_FCV_OP_MODE (gst_fcv_op_mode_get_type())
GST_VIDEO_API GType gst_fcv_op_mode_get_type (void);

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
 * gst_video_blits_new: (constructor)
 *
 * Allocate a new #GstVideoBlits that is also initialized.
 *
 * Returns: (transfer full): A new #GstVideoBlits.
 */
GST_VIDEO_API GstVideoBlits*
gst_video_blits_new (void);

/**
 * gst_video_blits_new_sized: (constructor)
 * @size: Number of elements to preallocate.
 *
 * Allocate a new #GstVideoBlits with @size elements preallocated.
 *
 * Returns: (transfer full): A new #GstVideoBlits.
 */

GST_VIDEO_API GstVideoBlits*
gst_video_blits_new_sized (guint size);

/**
 * gst_video_blits_new_take: (constructor)
 * @data: An array of #GstVideoBlit elements.
 * @size: Number of elements in @data.
 *
 * Create a new #GstVideoBlits by taking ownership of the @data array with @size.
 *
 * Returns: (transfer full): A new #GstVideoBlits.
 */

GST_VIDEO_API GstVideoBlits*
gst_video_blits_new_take (gpointer data, guint size);

/**
 * gst_video_blits_ref:
 * @vblits: A #GstVideoBlits
 *
 * Atomically increments the reference count of @vblits by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer full): The passed in `GstVideoBlits`
 */
GST_VIDEO_API GstVideoBlits*
gst_video_blits_ref (GstVideoBlits * vblits);

/**
 * gst_video_blits_unref:
 * @vblits: (transfer full): A #GstVideoBlits
 *
 * Atomically decrements the reference count of @vblits by one. If the
 * reference count drops to 0, free the GstVideoBlits.
 * This function is thread-safe and may be called from any thread.
 */
GST_VIDEO_API void
gst_video_blits_unref (GstVideoBlits * vblits);

/**
 * gst_video_blits_steal:
 * @vblits: A #GstVideoBlits
 * @size: A pointer to retrieve the number of elements of the original array.
 *
 * Frees the data in the array and free the GstVideoBlits structure.
 *
 * Returns: (transfer full): The allocated raw GstVideoBlit data.
 */
GST_VIDEO_API gpointer
gst_video_blits_steal (GstVideoBlits * vblits, guint * size);

/**
 * gst_video_blits_copy:
 * @vblits: A #GstVideoBlits
 *
 * Copy a GstVideoBlits structure.
 *
 * Returns: (transfer full): A new #GstVideoBlits.
 */
GST_VIDEO_API GstVideoBlits *
gst_video_blits_copy (const GstVideoBlits * vblits);

/**
 * gst_video_blits_append:
 * @vblits: A #GstVideoBlits
 * @vblit: A #GstVideoBlit
 *
 * Adds the value on to the end of the GstVideoBlits list.
 * The list will grow in size automatically if necessary.
 */
GST_VIDEO_API void
gst_video_blits_append (GstVideoBlits * vblits, const GstVideoBlit * vblit);

/**
 * gst_video_blits_insert:
 * @vblits: A #GstVideoBlits
 * @index: the index at which to insert the new element
 * @vblit: A #GstVideoBlit
 *
 * Insert element into a GstVideoBlits at the given index.
 * The list will grow in size automatically if necessary.
 */
GST_VIDEO_API void
gst_video_blits_insert (GstVideoBlits * vblits, guint index,
                        const GstVideoBlit * vblit);

/**
 * gst_video_blits_remove:
 * @vblits: A #GstVideoBlits
 * @index: the index of the element to remove
 *
 * Removes the element at the given index from the vblits list.
 * The following elements are moved down one place.
 */
GST_VIDEO_API void
gst_video_blits_remove (GstVideoBlits * vblits, guint index);

/**
 * gst_video_blits_entry:
 * @vblits: A #GstVideoBlits
 * @index: the index of the element to return
 *
 * Returns: (transfer none): the #GstVideoBlit at the given index.
 */
GST_VIDEO_API GstVideoBlit*
gst_video_blits_entry (GstVideoBlits * vblits, guint index);

/**
 * gst_video_blits_size:
 * @vblits: A #GstVideoBlits
 *
 * Returns: number of elements in A #GstVideoBlits
 */
GST_VIDEO_API guint
gst_video_blits_size (GstVideoBlits * vblits);

/**
 * gst_video_blits_resize:
 * @vblits: A #GstVideoBlits
 * @size: the new size of the GstVideoBlits list
 *
 * Sets the size of the array, expanding it if necessary.
 */
GST_VIDEO_API void
gst_video_blits_resize (GstVideoBlits * vblits, guint size);

G_END_DECLS

#endif // __GST_VIDEO_CONVERTER_ENGINE_PARAM_H__
