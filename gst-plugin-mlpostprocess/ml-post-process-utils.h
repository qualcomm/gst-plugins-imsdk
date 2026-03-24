/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_POST_PROCESS_UTILS_H__
#define __GST_QTI_ML_POST_PROCESS_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/ml-frame.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-post-process-classification.h>
#include <gst/ml/ml-post-process-detection.h>
#include <gst/ml/ml-post-process-pose.h>
#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <cairo/cairo.h>

G_BEGIN_DECLS

#define GST_AUDIO_CLASSIFICATION_TYPE \
    g_quark_from_static_string ("audio-classification")
#define GST_IMAGE_CLASSIFICATION_TYPE \
    g_quark_from_static_string ("image-classification")
#define GST_DETECTION_TYPE \
    g_quark_from_static_string ("object-detection")
#define GST_POSE_TYPE \
    g_quark_from_static_string ("pose-estimation")
#define GST_SEGMENTATION_TYPE \
    g_quark_from_static_string ("image-segmentation")
#define GST_SUPER_RESOLUTION_TYPE \
    g_quark_from_static_string ("super-resolution")
#define GST_TENSOR_TYPE \
    g_quark_from_static_string ("tensor")

#define GST_IS_AUDIO_CLASSIFICATION(type) (type == GST_AUDIO_CLASSIFICATION_TYPE)
#define GST_IS_IMAGE_CLASSIFICATION(type) (type == GST_IMAGE_CLASSIFICATION_TYPE)
#define GST_IS_DETECTION(type)            (type == GST_DETECTION_TYPE)
#define GST_IS_POSE(type)                 (type == GST_POSE_TYPE)
#define GST_IS_SEGMENTATION(type)         (type == GST_SEGMENTATION_TYPE)
#define GST_IS_SUPER_RESOLUTION(type)     (type == GST_SUPER_RESOLUTION_TYPE)
#define GST_IS_TENSOR(type)               (type == GST_TENSOR_TYPE)

#define DEFAULT_FONT_SIZE      12
#define MAX_TEXT_LENGTH        25
#define DISPLACEMENT_THRESHOLD 0.75F

enum {
  GST_OUTPUT_MODE_UNKNOWN,
  GST_OUTPUT_MODE_VIDEO,
  GST_OUTPUT_MODE_TEXT,
  GST_OUTPUT_MODE_TENSORS
};

/**
 * gst_value_array_append_and_take_ml_structure:
 * @array: A #GValue of GST_TYPE_ARRAY.
 * @id: Unique ID which will be added to the prediction structure.
 * @structure: ML prediction result in #GstStructure form.
 *
 * Helper function for adding a ML prediction result in GstStructure form
 * to a GValue array for serialization.
 */
void
gst_value_array_append_and_take_ml_structure (GValue * array, const guint id,
                                              GstStructure * structure);

/**
 * gst_ml_predictions_list_append:
 * @array: A #GValue of GST_TYPE_LIST
 * @mltype: Post-process type
 * @results: A #GValue of GST_TYPE_ARRAY with ML pofredictions for current batch
 * @mlparam: A #GstStructure with service info for current batch
 *
 * Helper function for taking ownership of prediction results and adding them
 * to a GValue list for serialization.
 *
 * Additionally service information will be extracted from @mlparam and
 * populated in the list entry.
 */
void
gst_ml_predictions_list_append (GValue * list, GQuark mltype, GValue * results,
                                const GstStructure * mlparam);

/**
 * gst_buffer_serialize_and_take_value:
 * @buffer: A #GstBuffer
 * @value: A #GValue of GST_TYPE_LIST
 *
 * Helper function for taking ownership of GValue list with predictions,
 * serializing it and attaching it as GstMemory to the buffer.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_buffer_serialize_and_take_value (GstBuffer * buffer, GValue * value);

/**
 * gst_cairo_draw_setup:
 * @vframe: A #GstVideoFrame to be filled
 * @surface: Cairo surface for which will be populated
 * @context: Cairo context for which will be populated
 *
 * Helper function to prepare cairo for draw.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_cairo_draw_setup (GstVideoFrame * vframe, cairo_surface_t ** surface,
                      cairo_t ** context);

/**
 * gst_cairo_draw_cleanup:
 * @surface: Cairo surface for destruction
 * @context: Cairo context for destruction
 *
 * Helper function for cairo cleanup.
 */
void
gst_cairo_draw_cleanup (cairo_surface_t * surface, cairo_t * context);

/**
 * gst_cairo_draw_label:
 * @context: Cairo context
 * @index: Vertical offset index at which to draw
 * @label: Label text contents
 * @color: Color of the label
 *
 * Helper function for drawing classification label using cairo.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_cairo_draw_label (cairo_t * context, const guint index,
                      const gchar * label, const guint32 color);

/**
 * gst_cairo_draw_detection:
 * @context: Cairo context
 * @detection: A #GstMLKeypoint for visualization
 * @roimeta: Video region in which to draw the rectangle
 *
 * Helper function for drawing detection rectangle using cairo.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_cairo_draw_detection (cairo_t * context, GstMLDetection * detection,
                          GstVideoRegionOfInterestMeta * roimeta);

/**
 * gst_cairo_draw_keypoint:
 * @context: Cairo context
 * @keypoint: A #GstMLKeypoint for visualization
 * @roimeta: Video region in which to draw the keypoint
 *
 * Helper function for drawing keypoint using cairo.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_cairo_draw_keypoint (cairo_t * context, GstMLKeypoint * keypoint,
                         GstVideoRegionOfInterestMeta * roimeta);

/**
 * gst_cairo_draw_link:
 * @context: Cairo context
 * @link: A #GstMLKeypointLink for visualization
 * @roimeta: Video region in which to draw the link
 *
 * Helper function for drawing link between keypoints using cairo.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_cairo_draw_link (cairo_t * context, GstMLKeypointLink * link,
                     GstVideoRegionOfInterestMeta * roimeta);

/**
 * gst_buffer_setup_image_region:
 * @region: A #GstVideoRectangle
 * @mlparam: A #GstStructure with tensor image region.
 *
 * Helper function for exracting the image region in the input tensor of the
 * used model and setting up the image region in the buffer which will be
 * filled with actual data by attaching a new GstVideoRegionOfINterestMeta to
 * it of roi_type "ImageRegion".
 *
 * Returns: (transfer none): The #GstVideoRegionOfInterestMeta on buffer.
 */
GstVideoRegionOfInterestMeta *
gst_buffer_setup_image_region (GstBuffer * buffer, const GstStructure * mlparam);

/**
 * gst_ml_structure_get_inverse_affine_matrix:
 * @structure: #GstStructure for ML post-processing parameters.
 * @matrix: A 3x3 affine transformation matrix which will be populated.
 *
 * Helper function for retrieving the inverse affine matrix.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_ml_structure_get_inverse_affine_matrix (const GstStructure * structure,
                                            gdouble matrix[3][3]);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_UTILS_H__
