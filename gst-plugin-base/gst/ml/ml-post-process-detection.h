/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_POST_PROCESS_DETECTION_H__
#define __GST_QTI_ML_POST_PROCESS_DETECTION_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/ml-post-process-keypoint.h>

G_BEGIN_DECLS

// Non-maximum Suppression (NMS) threshold (50%), corresponding to 2/3 overlap.
#define GST_ML_DETECTION_NMS_THRESHOLD 0.5F

typedef struct _GstMLDetection GstMLDetection;
typedef struct _GstMLDetections GstMLDetections;

#define GST_TYPE_ML_DETECTION            (gst_ml_detection_get_type ())
GST_API GType gst_ml_detection_get_type  (void);

#define GST_TYPE_ML_DETECTIONS           (gst_ml_detections_get_type ())
GST_API GType gst_ml_detections_get_type (void);

/**
 * GstMLDetection:
 * @name: Name of the prediction.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @color: Possible color that is associated with this prediction.
 * @left: X axis coordinate of upper-left corner.
 * @top: Y axis coordinate of upper-left corner.
 * @right: X axis coordinate of lower-right corner.
 * @bottom: Y axis coordinate of lower-right corner.
 * @landmarks: (optional) (element-type GstMLKeypoint) (transfer full):
 *             A #GArray of GstMLKeypoint
 * @xtraparams: (optional): A #GstStructure with custom parameters. The names
 *              for the those parameters inside the #GstStructure must be lower
 *              case with dash ('-') for whitepace e.g. "param-example-name".
 *              The following parameter names are forbidden: 'confidence',
 *              'keypoints' and 'links'. The name given to the structure
 *              on creation must use the reserved naming "ExtraParams".
 *
 * Information describing prediction result from object detection models.
 *
 * The fields top, left, bottom and right must be set in (0.0 to 1.0) relative
 * coordinate system.
 */
struct _GstMLDetection {
  GQuark         name;
  gfloat         confidence;
  guint          color;

  gfloat         left;
  gfloat         top;
  gfloat         right;
  gfloat         bottom;

  GArray         *landmarks;
  GstStructure   *xtraparams;
};

/**
 * gst_ml_detection_reset:
 * @detection: A #GstMLDetection
 *
 * Free any allocated resources owned by the structure and reset its fields.
 */
GST_API void
gst_ml_detection_reset (GstMLDetection * detection);

/**
 * gst_ml_detection_copy:
 * @detection: A #GstMLDetection
 *
 * Copy a GstMLDetection structure.
 *
 * Returns: (transfer full): a new #GstMLDetection.
 */
GST_API GstMLDetection *
gst_ml_detection_copy (const GstMLDetection * detection);

/**
 * gst_ml_detection_free:
 * @detection: A #GstMLDetection
 *
 * Free a GstMLDetection structure previously allocated with
 * gst_ml_detection_copy().
 */
GST_API void
gst_ml_detection_free (GstMLDetection * detection);

/**
 * gst_ml_detection_relative_transform:
 * @detection: A #GstMLDetection
 * @region: Region in the tensor containg the actual data.
 * @clamp: Whether to clamp values outside the region dimensions.
 *
 * Adjust detection coordinates to within the region which actually contains
 * data and transforming them to relative.
 */
GST_API void
gst_ml_detection_relative_transform (GstMLDetection * detection,
                                     const GstVideoRectangle * region,
                                     const gboolean clamp);

/**
 * gst_ml_detection_affine_transform:
 * @detection: A #GstMLDetection
 * @matrix: (array fixed-size=9) (element-type gdouble):
 *          A 3x3 affine transformation matrix.
 *
 * Helper function for adjusting coordinates of a ML detection with affine matrix.
 */
GST_API void
gst_ml_detection_affine_transform (GstMLDetection * detection, gdouble matrix[3][3]);

/**
 * gst_ml_detection_intersection_score:
 * @l_detecton: Left (or First) #GstMLDetection entry.
 * @r_detecton: Right (or Second) #GstMLDetection entry.
 *
 * Return a score representing how much current detection overlaps with another.
 *
 * Returns: Score from 0.0 (no overlap) to 1.0 (fully overlapping)
 */
GST_API gfloat
gst_ml_detection_intersection_score (const GstMLDetection * l_detecton,
                                     const GstMLDetection * r_detecton);

/**
 * gst_ml_detections_new: (constructor)
 *
 * Allocate a new #GstMLDetections that is also initialized.
 *
 * Returns: (transfer full): a new #GstMLDetections.
 */
GST_API GstMLDetections*
gst_ml_detections_new (void);

/**
 * gst_ml_detections_new_sized: (constructor)
 * @size: number of elements preallocated
 *
 * Allocate a new #GstMLDetections with @size elements preallocated.
 *
 * Returns: (transfer full): a new #GstMLDetections.
 */

GST_API GstMLDetections*
gst_ml_detections_new_sized (guint size);

/**
 * gst_ml_detections_ref: (skip)
 * @detections: (transfer none): A #GstMLDetections
 *
 * Atomically increments the reference count of @detections by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer none): A pointer to the object passed in @detections
 */
GST_API GstMLDetections*
gst_ml_detections_ref (GstMLDetections * detections);

/**
 * gst_ml_detections_unref: (skip)
 * @detections: (transfer none):  A #GstMLDetections
 *
 * Atomically decrements the reference count of @detections by one. If the
 * reference count drops to 0, free the GstMLDetections.
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_detections_unref (GstMLDetections * detections);

/**
 * gst_ml_detections_copy:
 * @detections: A #GstMLDetections
 *
 * Copy a GstMLDetections structure.
 *
 * Returns: (transfer full): a new #GstMLDetections.
 */
GST_API GstMLDetections *
gst_ml_detections_copy (const GstMLDetections * detections);

/**
 * gst_ml_detections_append:
 * @detections: A #GstMLDetections
 * @detection: A #GstMLDetection
 *
 * Adds the value on to the end of the GstMLDetections list.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_detections_append (GstMLDetections * detections,
                          const GstMLDetection * detection);

/**
 * gst_ml_detections_insert:
 * @detections: A #GstMLDetections
 * @index: the index at which to insert the new element
 * @detection: A #GstMLDetection
 *
 * Insert element into a GstMLDetections at the given index.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_detections_insert (GstMLDetections * detections, guint index,
                          const GstMLDetection * detection);

/**
 * gst_ml_detections_remove:
 * @detections: A #GstMLDetections
 * @index: the index of the element to remove
 *
 * Removes the element at the given index from the detections list.
 * The following elements are moved down one place.
 */
GST_API void
gst_ml_detections_remove (GstMLDetections * detections, guint index);

/**
 * gst_ml_detections_entry:
 * @detections: A #GstMLDetections
 * @index: the index of the element to return
 *
 * Returns: (transfer none): the #GstMLDetection at the given index.
 */
GST_API GstMLDetection*
gst_ml_detections_entry (GstMLDetections * detections, guint index);

/**
 * gst_ml_detections_size:
 * @detections: A #GstMLDetections
 *
 * Returns: number of elements in A #GstMLDetections
 */
GST_API guint
gst_ml_detections_size (GstMLDetections * detections);

/**
 * gst_ml_detections_resize:
 * @detections: A #GstMLDetections
 * @size: the new size of the GstMLDetections list
 *
 * Sets the size of the array, expanding it if necessary.
 */
GST_API void
gst_ml_detections_resize (GstMLDetections * detections, guint size);

/**
 * gst_ml_detections_sort:
 * @detections: A #GstMLDetections
 *
 * Sort elements in a GstMLDetections list by confidence score.
 */
GST_API void
gst_ml_detections_sort (GstMLDetections * detections);

/**
 * gst_ml_detections_non_max_suppression:
 * @detections: (transfer none) (element-type GstMLDetection):
 *              A #GArray of #GstMLDetection values
 * @detecton: A #GstMLDetection
 * @threshold: NMS threshold
 *
 * Helper function for Non-Max Suppression (NMS) algorithm.
 *
 * Returns: a value of (-1) if confidence is lower then any in the list.
 *          a value of (>= 0) if no prediction with the same label is present
 *          in the list or if its confidence is higher then any in the list.
 */
GST_API gint
gst_ml_detections_non_max_suppression (GstMLDetections * detections,
                                       const GstMLDetection * detecton,
                                       const gfloat threshold);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_DETECTION_H__
