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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GST_QTI_ML_MODULE_VIDEO_DETECTION_H__
#define __GST_QTI_ML_MODULE_VIDEO_DETECTION_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/gstmlmodule.h>

G_BEGIN_DECLS

typedef struct _GstMLBoxEntry GstMLBoxEntry;
typedef struct _GstMLBoxPrediction GstMLBoxPrediction;

/**
 * GstMLBoxEntry:
 * @name: Name of the prediction.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @color: Possible color that is associated with this prediction.
 * @top: Y axis coordinate of upper-left corner.
 * @left: X axis coordinate of upper-left corner.
 * @bottom: Y axis coordinate of lower-right corner.
 * @right: X axis coordinate of lower-right corner.
 *
 * Information describing prediction result from object detection models.
 * All fields are mandatory and need to be filled by the submodule.
 *
 * The fields top, left, bottom and right must be set in (0.0 to 1.0) relative
 * coordinate system.
 */
struct _GstMLBoxEntry {
  GQuark name;
  gfloat confidence;

  guint  color;

  gfloat top;
  gfloat left;
  gfloat bottom;
  gfloat right;
};

/**
 * GstMLBoxPrediction:
 * @batch_idx: Position of the entries in the batch.
 * @entries: GArray of #GstMLBoxEntry.
 * @info: Additonal info structure, beloging to the batch #GstProtectionMeta
 *        in the ML tensor buffer from which the prediction result was produced.
 *        Ownership is still with that tensor buffer.
 *
 * Information describing a group of prediction results beloging to the same batch.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct _GstMLBoxPrediction {
  guint8             batch_idx;
  GArray             *entries;
  const GstStructure *info;
};

/**
 * gst_ml_box_prediction_cleanup:
 * @prediction: Pointer to the ML box prediction.
 *
 * Helper function for freeing any resources allocated owned by the prediction.
 *
 * return: None
 */
GST_API void
gst_ml_box_prediction_cleanup (GstMLBoxPrediction * prediction);

/**
 * gst_ml_box_compare_entries:
 * @l_entry: Left (or First) ML box post-processing entry.
 * @r_entry: Right (or Second) ML box post-processing entry.
 *
 * Helper function for comparing two ML box entries.
 *
 * return: -1 (l_entry > r_entry), 1 (l_entry < r_entry) and 0 (l_entry == r_entry)
 */
GST_API gint
gst_ml_box_compare_entries (const GstMLBoxEntry * l_entry,
                            const GstMLBoxEntry * r_entry);

/**
 * gst_ml_box_relative_translation:
 * @box: Pointer to the ML box entry which will be modified.
 * @width: Width of the tensor.
 * @height: Height of the tensor.
 *
 * Helper function for transforming ML box dimensions from absolute to relative.
 *
 * return: None
 */
GST_API void
gst_ml_box_relative_translation (GstMLBoxEntry * box, gint width, gint height);

/**
 * gst_ml_box_transform_dimensions:
 * @box: Pointer to the ML box entry which will be modified.
 * @region: Region in the tensor containg the actual data.
 *
 * Helper function for adjusting ML box dimensions to within the region
 * which actually contains data and transforming them to relative.
 *
 * return: None
 */
GST_API void
gst_ml_box_transform_dimensions (GstMLBoxEntry * box, GstVideoRectangle * region);

/**
 * gst_ml_box_intersection_score:
 * @l_box: Left (or First) ML box post-processing entry.
 * @r_box: Right (or Second) ML box post-processing entry.
 *
 * Helper function for scoring how much two entries are overlapping.
 *
 * return: Score from 0.0 (no overlap) to 1.0 (fully overlapping)
 */
GST_API gfloat
gst_ml_boxes_intersection_score (GstMLBoxEntry * l_box, GstMLBoxEntry * r_box);

/**
 * gst_ml_box_non_max_suppression:
 * @l_box: Pointer to ML box post-processing entry.
 * @boxes: GArray of #GstMLBoxEntry.
 *
 * Helper function for Non-Max Suppression (NMS) algorithm.
 *
 * return: (-2) If confidence of the prediction is lower then any in the list.
 *         (-1) If no prediction with the same label is present in the list.
 *         (>= 0) If confidence of the prediction is higher then any in the list.
 */
GST_API gint
gst_ml_box_non_max_suppression (GstMLBoxEntry * l_box, GArray * boxes);

/**
 * gst_ml_module_video_detection_execute:
 * @module: Pointer to ML post-processing module.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @predictions: GArray of #GstMLBoxPrediction.
 *
 * Convenient wrapper function used on plugin level to call the module
 * 'gst_ml_module_process' API via 'gst_ml_module_execute' wrapper in order
 * to process input tensors.
 *
 * Post-processing module must define the 3rd argument of the implemented
 * 'gst_ml_module_process' API as 'GArray *'.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_video_detection_execute (GstMLModule * module, GstMLFrame * mlframe,
                                       GArray * predictions);

G_END_DECLS

#endif // __GST_QTI_ML_MODULE_VIDEO_DETECTION_H__
