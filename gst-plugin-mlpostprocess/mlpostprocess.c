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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlpostprocess.h"

#include <gst/ml/gstmlpool.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/gstimagepool.h>

#define GST_CAT_DEFAULT gst_ml_post_process_debug
GST_DEBUG_CATEGORY (gst_ml_post_process_debug);

#define gst_ml_post_process_parent_class parent_class
G_DEFINE_TYPE (GstMLPostProcess, gst_ml_post_process, GST_TYPE_BASE_TRANSFORM);

#define GST_ML_POST_PROCESS_VIDEO_FORMATS \
    "{ RGBA, RGBx }"

#define GST_ML_POST_PROCESS_SRC_CAPS                            \
    "video/x-raw, "                                             \
    "format = (string) " GST_ML_POST_PROCESS_VIDEO_FORMATS "; " \
    "text/x-raw, "                                              \
    "format = (string) { utf8 }; "                              \
    "neural-network/tensors"

#define GST_ML_POST_PROCESS_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE             0
#define DEFAULT_PROP_LABELS             NULL
#define DEFAULT_PROP_NUM_RESULTS        5
#define DEFAULT_PROP_SETTINGS           NULL
#define DEFAULT_PROP_STABILIZATION      FALSE

#define DEFAULT_MIN_BUFFERS             2
#define DEFAULT_MAX_BUFFERS             10
#define DEFAULT_VIDEO_WIDTH             320
#define DEFAULT_VIDEO_HEIGHT            320

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_LABELS,
  PROP_NUM_RESULTS,
  PROP_SETTINGS,
  PROP_STABILIZATION,
};

enum
{
  SIGNAL_IMAGE_CLASSIFICATIONS,
  SIGNAL_AUDIO_CLASSIFICATIONS,
  SIGNAL_DETECTIONS,
  SIGNAL_POSES,
  SIGNAL_SEGMENTATIONS,
  SIGNAL_DEPTH_MAPS,
  SIGNAL_TENSORS,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GstCaps *
gst_ml_post_process_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_ML_POST_PROCESS_SINK_CAPS);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_post_process_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_ML_POST_PROCESS_SRC_CAPS);

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_ML_POST_PROCESS_VIDEO_FORMATS));

      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_post_process_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_post_process_sink_caps ());
}

static GstPadTemplate *
gst_ml_post_process_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_post_process_src_caps ());
}

static GQuark
gst_ml_post_process_signal_get_type (GstMLPostProcess * postprocess)
{
  gboolean connected = FALSE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_IMAGE_CLASSIFICATIONS], 0, FALSE);

  if (connected)
    return GST_IMAGE_CLASSIFICATION_TYPE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_AUDIO_CLASSIFICATIONS], 0, FALSE);

  if (connected)
    return GST_AUDIO_CLASSIFICATION_TYPE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_DETECTIONS], 0, FALSE);

  if (connected)
    return GST_DETECTION_TYPE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_POSES], 0, FALSE);

  if (connected)
    return GST_POSE_TYPE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_SEGMENTATIONS], 0, FALSE);

  if (connected)
    return GST_SEGMENTATION_TYPE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_DEPTH_MAPS], 0, FALSE);

  if (connected)
    return GST_DEPTH_MAP_TYPE;

  connected = g_signal_has_handler_pending (postprocess,
      signals[SIGNAL_TENSORS], 0, FALSE);

  if (connected)
    return GST_TENSOR_TYPE;

  return g_quark_from_string ("unknown");
}

static inline void
gst_ml_detection_displacement_correction (GstMLDetection * l_detection,
    GstMLDetections * detections)
{
  GstMLDetection *r_detection = NULL;
  gfloat score = 0.0;
  guint idx = 0;

  if (detections == NULL)
    return;

  for (idx = 0; idx < gst_ml_detections_size (detections);  idx++) {
    r_detection = gst_ml_detections_entry (detections, idx);

    // If labels do not match, continue with next list entry.
    if (l_detection->name != r_detection->name)
      continue;

    score = gst_ml_detection_intersection_score (l_detection, r_detection);

    // If the score is below the threshold, continue with next list entry.
    if (score <= DISPLACEMENT_THRESHOLD)
      continue;

    // Previously detected box overlaps at ~95 % with current one, use it.
    l_detection->top = r_detection->top;
    l_detection->left = r_detection->left;
    l_detection->bottom = r_detection->bottom;
    l_detection->right = r_detection->right;

    break;
  }
}

static inline void
gst_ml_classifications_serialize (GstMLClassifications * predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  guint idx = 0, sequence_idx = 0, id = 0;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  for (idx = 0; idx < gst_ml_classifications_size (predictions); idx++) {
    GstMLClassification *entry = gst_ml_classifications_entry (predictions, idx);
    GstStructure *structure = gst_ml_classification_to_structure (entry);

    id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_classifications_visualize (GstMLClassifications * predictions,
    G_GNUC_UNUSED GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  guint idx = 0, length = 0;
  gboolean success = TRUE;

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  length = gst_ml_classifications_size (predictions);

  for (idx = 0; (idx < length) && success; idx++) {
    GstMLClassification *entry = gst_ml_classifications_entry (predictions, idx);
    const gchar *label = g_quark_to_string (entry->name);

    success = gst_cairo_draw_label (context, idx, label, entry->color);
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_detections_serialize (GstMLDetections * predictions, guint stage_id,
    GstStructure * mlparam, GValue * list)
{
  GstStructure *structure = NULL;
  guint idx = 0, sequence_idx = 0, id = 0;
  gdouble matrix[3][3] = {};
  gboolean correction = FALSE;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  for (idx = 0; idx < gst_ml_detections_size (predictions); idx++) {
    GstMLDetection *entry = gst_ml_detections_entry (predictions, idx);

    if (correction)
      gst_ml_detection_affine_transform (entry, matrix);

    structure = gst_ml_detection_to_structure (entry);
    id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_detections_visualize (GstMLDetections * predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  guint idx = 0, num = 0, length = 0;
  gdouble matrix[3][3] = {};
  gboolean success = TRUE, correction = FALSE;

  roimeta = gst_buffer_setup_image_region (vframe->buffer, mlparam);
  correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  length = gst_ml_detections_size (predictions);

  for (idx = 0; (idx < length) && success; idx++) {
    GstMLDetection *entry = gst_ml_detections_entry (predictions, idx);
    GstMLKeypoint *keypoint = NULL;

    if (correction)
      gst_ml_detection_affine_transform (entry, matrix);

    success = gst_cairo_draw_detection (context, entry, roimeta);

    if ((entry->landmarks == NULL) || !success)
      continue;

    for (num = 0; (num < entry->landmarks->len) && success; num++) {
      keypoint = &(g_array_index (entry->landmarks, GstMLKeypoint, num));
      success = gst_cairo_draw_keypoint (context, keypoint, roimeta);
    }
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_poses_serialize (GstMLPoses * predictions, guint stage_id,
    GstStructure * mlparam, GValue * list)
{
  GstStructure *structure = NULL;
  guint idx = 0, sequence_idx = 0, id = 0;
  gdouble matrix[3][3] = {};
  gboolean correction = FALSE;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  for (idx = 0; idx < gst_ml_poses_size (predictions); idx++) {
    GstMLPose *entry = gst_ml_poses_entry (predictions, idx);

    if (correction)
      gst_ml_pose_affine_transform (entry, matrix);

    structure = gst_ml_pose_to_structure (entry);
    id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_poses_visualize (GstMLPoses * predictions, GstStructure * mlparam,
    GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  guint idx = 0, num = 0, length = 0;
  gdouble matrix[3][3] = {};
  gboolean success = TRUE, correction = FALSE;

  roimeta = gst_buffer_setup_image_region (vframe->buffer, mlparam);
  correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  length = gst_ml_poses_size (predictions);

  for (idx = 0; (idx < length) && success; idx++) {
    GstMLPose *entry = gst_ml_poses_entry (predictions, idx);
    GstMLKeypoint *keypoint = NULL;
    GstMLKeypointLink *link = NULL;

    for (num = 0; (num < entry->keypoints->len) && success; num++) {
      keypoint = &(g_array_index (entry->keypoints, GstMLKeypoint, num));

      if (correction)
        gst_ml_keypoint_affine_transform (keypoint, matrix);

      success = gst_cairo_draw_keypoint (context, keypoint, roimeta);
    }

    if ((entry->links == NULL) || !success)
      continue;

    for (num = 0; (num < entry->links->len) && success; num++) {
      link = &(g_array_index (entry->links, GstMLKeypointLink, num));

      if (correction) {
        gst_ml_keypoint_affine_transform (&(link->l_kp), matrix);
        gst_ml_keypoint_affine_transform (&(link->r_kp), matrix);
      }

      success = gst_cairo_draw_link (context, link, roimeta);
    }
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_segmentations_serialize (GstMLSegmentations * predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  GstStructure *structure = NULL;
  guint idx = 0, sequence_idx = 0, id = 0;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  for (idx = 0; idx < gst_ml_segmentations_size (predictions); idx++) {
    GstMLSegmentation *entry = gst_ml_segmentations_entry (predictions, idx);

    structure = gst_ml_segmentation_to_structure (entry);
    id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_segmentations_visualize (GstMLSegmentations * predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  guint idx = 0, length = 0;
  gboolean success = TRUE;

  roimeta = gst_buffer_setup_image_region (vframe->buffer, mlparam);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  length = gst_ml_segmentations_size (predictions);

  for (idx = 0; (idx < length) && success; idx++) {
    GstMLSegmentation *entry = gst_ml_segmentations_entry (predictions, idx);

    success = gst_cairo_draw_mask (context,
        GST_UINT32_PTR_CAST (entry->colors->data), entry->n_rows,
        entry->n_columns, roimeta);
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_depth_maps_serialize (GstMLDepthMaps * predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  GstStructure *structure = NULL;
  guint idx = 0, sequence_idx = 0, id = 0;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  for (idx = 0; idx < gst_ml_depth_maps_size (predictions); idx++) {
    GstMLDepthMap *entry = gst_ml_depth_maps_entry (predictions, idx);

    structure = gst_ml_depth_map_to_structure (entry);
    id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_depth_maps_visualize (GstMLDepthMaps * predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  guint idx = 0, length = 0;
  gboolean success = TRUE;

  roimeta = gst_buffer_setup_image_region (vframe->buffer, mlparam);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  length = gst_ml_depth_maps_size (predictions);

  for (idx = 0; (idx < length) && success; idx++) {
    GstMLDepthMap *entry = gst_ml_depth_maps_entry (predictions, idx);

    success = gst_cairo_draw_mask (context,
        GST_UINT32_PTR_CAST (entry->colors->data), entry->n_rows,
        entry->n_columns, roimeta);
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static void
gst_ml_post_process_detection_stabilization (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLDetections * predictions)
{
  GstMLDetections *stashed = NULL;
  guint idx = 0;

  stashed = g_list_nth_data (postprocess->stashedpredictions, batch_idx);

  for (idx = 0; idx < gst_ml_detections_size (predictions); idx++) {
    GstMLDetection *entry = gst_ml_detections_entry (predictions, idx);

    // Overwrite current box with previously detected one if required.
    gst_ml_detection_displacement_correction (entry, stashed);
  }

  // Stash the previous detections results.
  if (stashed != NULL) {
    postprocess->stashedpredictions =
        g_list_remove (postprocess->stashedpredictions, stashed);
    gst_ml_detections_unref (stashed);
  }

  postprocess->stashedpredictions = g_list_append (
      postprocess->stashedpredictions, gst_ml_detections_copy (predictions));
}

static gboolean
gst_ml_post_process_signal_classifications (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  GstMLClassifications *predictions = gst_ml_classifications_new ();
  guint signal_id = SIGNAL_IMAGE_CLASSIFICATIONS, stage_id = 0;
  gboolean success = FALSE;

  if (GST_IS_AUDIO_CLASSIFICATION (postprocess->type))
    signal_id = SIGNAL_AUDIO_CLASSIFICATIONS;

  g_signal_emit (postprocess, signals[signal_id], 0, mlframe, mlparam,
      predictions, &success);

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process batch %u!", batch_idx);
    goto cleanup;
  }
  gst_ml_classifications_sort (predictions);

  if (gst_ml_classifications_size (predictions) > postprocess->n_results)
    gst_ml_classifications_resize (predictions, postprocess->n_results);

  stage_id = postprocess->stage_id;

  if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = (GstVideoFrame *) output;
    success = gst_ml_classifications_visualize (predictions, mlparam, vframe);
  } else if (postprocess->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = (GValue *) output;
    gst_ml_classifications_serialize (predictions, stage_id, mlparam, list);
  }

cleanup:
  g_clear_pointer (&predictions, gst_ml_classifications_unref);
  return success;
}

static gboolean
gst_ml_post_process_signal_detections (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  GstMLDetections *predictions = gst_ml_detections_new ();
  guint stage_id = postprocess->stage_id;
  gboolean success = FALSE;

  g_signal_emit (postprocess, signals[SIGNAL_DETECTIONS], 0, mlframe, mlparam,
      predictions, &success);

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process batch %u!", batch_idx);
    goto cleanup;
  }
  gst_ml_detections_sort (predictions);

  if (gst_ml_detections_size (predictions) > postprocess->n_results)
    gst_ml_detections_resize (predictions, postprocess->n_results);

  // Apply stabilization for fluctuating bboxes
  if (postprocess->stabilization)
    gst_ml_post_process_detection_stabilization (postprocess, batch_idx, predictions);

  if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = (GstVideoFrame *) output;
    success = gst_ml_detections_visualize (predictions, mlparam, vframe);
  } else if (postprocess->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = (GValue *) output;
    gst_ml_detections_serialize (predictions, stage_id, mlparam, list);
  }

cleanup:
  g_clear_pointer (&predictions, gst_ml_detections_unref);
  return success;
}

static gboolean
gst_ml_post_process_signal_poses (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  GstMLPoses *predictions = gst_ml_poses_new ();
  guint stage_id = postprocess->stage_id;
  gboolean success = FALSE;

  g_signal_emit (postprocess, signals[SIGNAL_POSES], 0, mlframe, mlparam,
      predictions, &success);

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process batch %u!", batch_idx);
    goto cleanup;
  }
  gst_ml_poses_sort (predictions);

  if (gst_ml_poses_size (predictions) > postprocess->n_results)
    gst_ml_poses_resize (predictions, postprocess->n_results);

  if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = (GstVideoFrame *) output;
    success = gst_ml_poses_visualize (predictions, mlparam, vframe);
  } else if (postprocess->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = (GValue *) output;
    gst_ml_poses_serialize (predictions, stage_id, mlparam, list);
  }

cleanup:
  g_clear_pointer (&predictions, gst_ml_poses_unref);
  return success;
}

static gboolean
gst_ml_post_process_signal_segmentations (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  GstMLSegmentations *predictions = gst_ml_segmentations_new ();
  guint stage_id = postprocess->stage_id;
  gboolean success = FALSE;

  g_signal_emit (postprocess, signals[SIGNAL_SEGMENTATIONS], 0, mlframe,
      mlparam, predictions, &success);

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process batch %u!", batch_idx);
    goto cleanup;
  }

  if (gst_ml_segmentations_size (predictions) > postprocess->n_results)
    gst_ml_segmentations_resize (predictions, postprocess->n_results);

  if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = (GstVideoFrame *) output;
    success = gst_ml_segmentations_visualize (predictions, mlparam, vframe);
  } else if (postprocess->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *value = (GValue *) output;
    gst_ml_segmentations_serialize (predictions, stage_id, mlparam, value);
  }

cleanup:
  g_clear_pointer (&predictions, gst_ml_segmentations_unref);
  return success;
}

static gboolean
gst_ml_post_process_signal_depth_maps (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  GstMLDepthMaps *predictions = gst_ml_depth_maps_new ();
  guint stage_id = postprocess->stage_id;
  gboolean success = FALSE;

  g_signal_emit (postprocess, signals[SIGNAL_DEPTH_MAPS], 0, mlframe,
      mlparam, predictions, &success);

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process batch %u!", batch_idx);
    goto cleanup;
  }

  if (gst_ml_depth_maps_size (predictions) > postprocess->n_results)
    gst_ml_depth_maps_resize (predictions, postprocess->n_results);

  if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = (GstVideoFrame *) output;
    success = gst_ml_depth_maps_visualize (predictions, mlparam, vframe);
  } else if (postprocess->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *value = (GValue *) output;
    gst_ml_depth_maps_serialize (predictions, stage_id, mlparam, value);
  }

cleanup:
  g_clear_pointer (&predictions, gst_ml_depth_maps_unref);
  return success;
}

static gboolean
gst_ml_post_process_signal_tensors (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  gboolean success = FALSE;

  g_signal_emit (postprocess, signals[SIGNAL_TENSORS], 0, mlframe, mlparam,
        ((GstMLFrame*) output), &success);

  if (!success)
    GST_ERROR_OBJECT (postprocess, "Failed to process batch %u!", batch_idx);

  return success;
}

static gboolean
gst_ml_post_process_module_execute (GstMLPostProcess * postprocess,
    guint batch_idx, GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  return gst_ml_engine_execute (
      postprocess->engine, batch_idx, mlframe, mlparam, output);
}

static gboolean
gst_ml_post_process_tensors (GstMLPostProcess * postprocess,
    GstMLFrame * mlframe, GstBuffer * outbuffer)
{
  GstMLFrame outmlframe = { 0, };
  guint idx = 0, num = 0, n_batch = 0;
  gboolean success = TRUE;

  n_batch = GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0);

  if (!gst_ml_frame_map (&outmlframe, NULL, outbuffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (postprocess, "Failed to map output ML buffer!");
    return FALSE;
  }

  // Set batch size for each tensor to 1 as they are processed sequentially.
  for (idx = 0; idx < GST_ML_FRAME_N_TENSORS (mlframe); ++idx)
    mlframe->info.tensors[idx][0] = 1;

  for (idx = 0; idx < GST_ML_FRAME_N_TENSORS (&outmlframe); ++idx)
    outmlframe.info.tensors[idx][0] = 1;

  // Iterate all batches and execute the process
  for (idx = 0; (idx < n_batch) && success; ++idx) {
    GstStructure *mlparam = g_ptr_array_index (postprocess->mlparams, idx);

    success = postprocess->process (postprocess, idx, mlframe, mlparam, &outmlframe);

    // Update each tensor data pointer for next iteration.
    for (num = 0; num < GST_ML_FRAME_N_TENSORS (mlframe); num++)
      mlframe->mapinfo[num].data += GST_ML_FRAME_TENSOR_SIZE (mlframe, num);

    for (num = 0; num < GST_ML_FRAME_N_TENSORS (&outmlframe); num++)
      outmlframe.mapinfo[num].data += GST_ML_FRAME_TENSOR_SIZE (&outmlframe, num);
  }

  gst_ml_frame_unmap (&outmlframe);
  return success;
}

static gboolean
gst_ml_post_process_video (GstMLPostProcess * postprocess,
    GstMLFrame * mlframe, GstBuffer * outbuffer)
{
  GstVideoFrame vframe = { 0, };
  GstMapFlags flags = GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
  guint idx = 0, num = 0, n_batch = 0;
  gboolean success = TRUE;

  n_batch = GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0);

  if ((gst_buffer_get_size (outbuffer) != 0) &&
      !gst_video_frame_map (&vframe, postprocess->vinfo, outbuffer, flags)) {
    GST_ERROR_OBJECT (postprocess, "Failed to map output ML buffer!");
    return FALSE;
  }

  // Set batch size for each tensor to 1 as they are processed sequentially.
  for (idx = 0; idx < GST_ML_FRAME_N_TENSORS (mlframe); ++idx)
    mlframe->info.tensors[idx][0] = 1;

  // Iterate all batches and execute the process
  for (idx = 0; (idx < n_batch) && success; ++idx) {
    GstStructure *mlparam = g_ptr_array_index (postprocess->mlparams, idx);

    success = postprocess->process (postprocess, idx, mlframe, mlparam, &vframe);

    // Update each tensor data pointer for next iteration.
    for (num = 0; num < GST_ML_FRAME_N_TENSORS (mlframe); num++)
      mlframe->mapinfo[num].data += GST_ML_FRAME_TENSOR_SIZE (mlframe, num);
  }

  gst_video_frame_unmap (&vframe);
  return success;
}

static gboolean
gst_ml_post_process_text (GstMLPostProcess * postprocess,
    GstMLFrame * mlframe, GstBuffer * outbuffer)
{
  GValue list = G_VALUE_INIT, array = G_VALUE_INIT;
  guint idx = 0, num = 0, n_batch = 0;
  gboolean success = TRUE;

  g_value_init (&list, GST_TYPE_LIST);
  n_batch = GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0);

  // Set batch size for each tensor to 1 as they are processed sequentially.
  for (idx = 0; idx < GST_ML_FRAME_N_TENSORS (mlframe); ++idx)
    mlframe->info.tensors[idx][0] = 1;

  // Iterate all batches and execute the process
  for (idx = 0; (idx < n_batch) && success; ++idx) {
    GstStructure *mlparam = g_ptr_array_index (postprocess->mlparams, idx);

    g_value_init (&array, GST_TYPE_ARRAY);

    if (GST_ML_FRAME_N_TENSORS (mlframe) != 0)
      success = postprocess->process (postprocess, idx, mlframe, mlparam, &array);

    gst_ml_predictions_list_append (&list, postprocess->type, &array, mlparam);

    // Update each tensor data pointer for next iteration.
    for (num = 0; num < GST_ML_FRAME_N_TENSORS (mlframe); num++)
      mlframe->mapinfo[num].data += GST_ML_FRAME_TENSOR_SIZE (mlframe, num);
  }

  gst_buffer_serialize_and_take_value (outbuffer, &list);
  return success;
}

static GstBufferPool *
gst_ml_post_process_create_pool (GstMLPostProcess * postprocess,
    GstCaps * caps, GstVideoAlignment * align)
{
  GstStructure *structure = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  guint size = 0;

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "neural-network/tensors"))
    pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_DMA);
  else if (gst_structure_has_name (structure, "video/x-raw"))
    pool = gst_image_buffer_pool_new ();

  if (pool == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to create buffer pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (postprocess, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (postprocess, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  structure = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_allocator (structure, allocator, NULL);
  g_object_unref (allocator);

  if (GST_IS_ML_POOL (pool)) {
    GstMLInfo mlinfo = { 0, };

    gst_buffer_pool_config_add_option (structure,
        GST_ML_BUFFER_POOL_OPTION_TENSOR_META);
    gst_buffer_pool_config_add_option (structure,
        GST_ML_BUFFER_POOL_OPTION_KEEP_MAPPED);

    gst_ml_info_from_caps (&mlinfo, caps);
    size = gst_ml_info_size (&mlinfo);
  } else if (GST_IS_IMAGE_BUFFER_POOL (pool)) {
    GstVideoInfo vinfo = { 0, };

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option (structure,
        GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);
    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

    gst_video_info_from_caps (&vinfo, caps);
    gst_buffer_pool_config_set_video_alignment (structure, align);
    gst_video_info_align (&vinfo, align);
    size = vinfo.size;
  }

  gst_buffer_pool_config_set_params (structure, caps, size, DEFAULT_MIN_BUFFERS,
      DEFAULT_MAX_BUFFERS);

  if (!gst_buffer_pool_set_config (pool, structure)) {
    GST_WARNING_OBJECT (postprocess, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  return pool;
}

static gboolean
gst_ml_post_process_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *structure = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = {};
  GstVideoAlignment align = {0,};
  guint size = 0, minbuffers = 0, maxbuffers = 0;

  gst_clear_object (&(postprocess->outpool));
  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (structure, "neural-network/tensors") &&
      !gst_structure_has_name (structure, "video/x-raw"))
     return TRUE;

  if (gst_query_parse_video_alignment (query, &align)) {
    GST_DEBUG_OBJECT (postprocess, "Downstream alignment: padding (top: %u "
        "bottom: %u left: %u right: %u) stride (%u, %u, %u, %u)",
        align.padding_top, align.padding_bottom, align.padding_left,
        align.padding_right, align.stride_align[0], align.stride_align[1],
        align.stride_align[2], align.stride_align[3]);
  }

  // Create a new buffer pool.
  pool = gst_ml_post_process_create_pool (postprocess, caps, &align);
  if (pool == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to create buffer pool!");
    return FALSE;
  }

  postprocess->outpool = pool;

  // Get the configured pool properties in order to set in query.
  structure = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (structure, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (structure, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (structure);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers, maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);

  return TRUE;
}

static GstFlowReturn
gst_ml_post_process_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstBufferPool *pool = postprocess->outpool;
  guint idx = 0, n_batch = 0;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (postprocess, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (pool != NULL) {
    if (!gst_buffer_pool_is_active (pool) &&
        !gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (postprocess, "Failed to activate output buffer pool!");
      return GST_FLOW_ERROR;
    }

    // Input is marked as GAP, nothing to process. Create a GAP output buffer.
    if ((gst_buffer_get_size (inbuffer) == 0) &&
        GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
      *outbuffer = gst_buffer_new ();
      GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
    }

    if ((*outbuffer == NULL) &&
        gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (postprocess, "Failed to create output buffer!");
      return GST_FLOW_ERROR;
    }
  } else {
    *outbuffer = gst_buffer_new ();
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Clear previously stored values and populate the new ml params for later use.
  g_ptr_array_remove_range (postprocess->mlparams, 0, postprocess->mlparams->len);
  n_batch = GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0);

  for (idx = 0; idx < n_batch; ++idx) {
    GstProtectionMeta *pmeta = gst_buffer_get_protection_meta_id (inbuffer,
        gst_batch_channel_name (idx));

    g_ptr_array_add (postprocess->mlparams, pmeta->info);

    if (pool == NULL || !GST_IS_ML_POOL (pool))
      continue;

    // Propagate ML protection meta downstream if output is a tensor.
    pmeta = gst_buffer_add_protection_meta (*outbuffer,
        gst_structure_copy (pmeta->info));

    // TODO: We should only add the mandatory fields instead of removing some.
    gst_structure_remove_field (pmeta->info, "input-region-x");
    gst_structure_remove_field (pmeta->info, "input-region-y");
    gst_structure_remove_field (pmeta->info, "input-region-width");
    gst_structure_remove_field (pmeta->info, "input-region-height");
    gst_structure_remove_field (pmeta->info, "input-tensor-width");
    gst_structure_remove_field (pmeta->info, "input-tensor-height");
  }

  return GST_FLOW_OK;
}

static gboolean
gst_ml_post_process_sink_event (GstBaseTransform * base, GstEvent * event)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      if (!GST_IS_DETECTION (postprocess->type))
        break;

      const GstStructure *structure = gst_event_get_structure (event);

      // Not a supported custom event, pass it to the default handling function.
      if ((structure == NULL) ||
          !gst_structure_has_name (structure, "ml-detection-information"))
        break;

      // Consume downstream information from previous postprocess stage.
      gst_event_unref (event);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (base, event);
}

static GstCaps *
gst_ml_post_process_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstCaps *tmplcaps = NULL, *result = NULL, *intersection = NULL;
  guint idx = 0, num = 0, length = 0;

  GST_DEBUG_OBJECT (postprocess, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (postprocess, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    if (NULL == postprocess->engine) {
      GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
      tmplcaps = gst_pad_get_pad_template_caps (pad);
    } else {
      tmplcaps = gst_ml_engine_get_caps (postprocess->engine);
    }
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    tmplcaps = gst_pad_get_pad_template_caps (pad);
  }

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = NULL;
    GstCapsFeatures *features = NULL;

    for (num = 0; num < gst_caps_get_size (caps); num++) {
      const GValue *value = NULL;

      structure = gst_caps_get_structure (tmplcaps, idx);
      features = gst_caps_get_features (tmplcaps, idx);

      // Segmentation and Super Resolution do not support text outputs.
      if (gst_structure_has_name (structure, "text/x-raw") &&
          GST_IS_SUPER_RESOLUTION (postprocess->type))
        continue;

      // Make a copy that will be modified.
      structure = gst_structure_copy (structure);

      // Extract the rate from incoming caps and propagate it to result caps.
      value = gst_structure_get_value (gst_caps_get_structure (caps, num),
          (direction == GST_PAD_SRC) ? "framerate" : "rate");

      // Skip if there is no value or if current caps structure is text.
      if (value != NULL && !gst_structure_has_name (structure, "text/x-raw")) {
        gst_structure_set_value (structure,
            (direction == GST_PAD_SRC) ? "rate" : "framerate", value);
      }

      // If this is already expressed by the existing caps skip this structure.
      if (gst_caps_is_subset_structure_full (result, structure, features)) {
        gst_structure_free (structure);
        continue;
      }

      gst_caps_append_structure_full (result, structure,
          gst_caps_features_copy (features));
    }
  }

  gst_caps_unref (tmplcaps);

  if (filter != NULL) {
    intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (postprocess, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_post_process_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstStructure *output = NULL;
  GstQuery *query = NULL;
  const GValue *value = NULL;
  guint width = 0, height = 0;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (postprocess, "Trying to fixate output caps %"
      GST_PTR_FORMAT " in direction %s based on caps %" GST_PTR_FORMAT,
      outcaps, (direction == GST_PAD_SINK) ? "sink" : "src", incaps);

  // Query upstream pre-process plugin about the inference parameters.
  query = gst_query_new_custom (GST_QUERY_CUSTOM,
      gst_structure_new_empty ("ml-preprocess-information"));

  if (gst_pad_peer_query (base->sinkpad, query)) {
    const GstStructure *stucture = gst_query_get_structure (query);

    gst_structure_get_uint (stucture, "stage-id", &(postprocess->stage_id));
    GST_DEBUG_OBJECT (postprocess, "Queried stage ID: %u", postprocess->stage_id);

    // Get the width and height of model input image tensor as possible output.
    if (gst_ml_structure_has_source_dimensions (stucture))
      gst_ml_structure_get_source_dimensions (stucture, &width, &height);
  } else {
    // TODO: Temporary workaround. Need to be addressed proerly.
    // In case of daisycahin it is possible to negotiate wrong stage-id without
    // thrwing an error.
    GST_WARNING_OBJECT (postprocess, "Failed to receive preprocess information!");
  }

  // Free the query instance as it is no longer needed and we are the owners.
  gst_query_unref (query);

  if (gst_structure_has_field (output, "format")) {
    // Fixate the output format.
    value = gst_structure_get_value (output, "format");

    if (!gst_value_is_fixed (value)) {
      gst_structure_fixate_field (output, "format");
      value = gst_structure_get_value (output, "format");
    }

    GST_DEBUG_OBJECT (postprocess, "Output format fixed to: %s",
        g_value_get_string (value));
  }

  if (gst_structure_has_name (output, "video/x-raw")) {
    gint par_n = 0, par_d = 0;
    gboolean islabels = FALSE;

    // Fixate output PAR if not already fixated..
    value = gst_structure_get_value (output, "pixel-aspect-ratio");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, 1, 1, NULL);
      value = gst_structure_get_value (output, "pixel-aspect-ratio");
    }

    par_d = gst_value_get_fraction_denominator (value);
    par_n = gst_value_get_fraction_numerator (value);

    GST_DEBUG_OBJECT (postprocess, "Output PAR fixed to: %d/%d", par_n, par_d);

    islabels = GST_IS_IMAGE_CLASSIFICATION (postprocess->type) ||
        GST_IS_AUDIO_CLASSIFICATION (postprocess->type);

    // For super-resolution expect the tensor resolution as video resolution.
    if (GST_IS_SUPER_RESOLUTION (postprocess->type)) {
      value = gst_structure_get_value (
          gst_caps_get_structure (incaps, 0), "dimensions");

      // Expected single tensor in dimensions with 3 of 4 elements.
      if ((value == NULL) || (gst_value_array_get_size (value) != 1)) {
        GST_ERROR_OBJECT (postprocess, "Unexpected number of input tensors!");
        return NULL;
      }

      value = gst_value_array_get_value (value, 0);

      if ((value == NULL) || (gst_value_array_get_size (value) < 3)) {
        GST_ERROR_OBJECT (postprocess, "Unexpected input tensor dimensions!");
        return NULL;
      }

      width = g_value_get_int (gst_value_array_get_value (value, 2));
      height = g_value_get_int (gst_value_array_get_value (value, 1));
    } else if (!GST_IS_SEGMENTATION (postprocess->type) &&
               !GST_IS_DEPTH_MAP (postprocess->type)) {
      // Reset the width and height from query for non-segmentation post-process.
      width = height = 0;
    }

    // Retrieve the output width and height.
    value = gst_structure_get_value (output, "width");

    if ((width != 0) && (value != NULL) && gst_value_is_fixed (value)) {
      GST_ERROR_OBJECT (postprocess, "Fixated width in filter caps is not "
          "supported with current post-process type!");
      return NULL;
    } else if ((NULL == value) || !gst_value_is_fixed (value)) {
      if ((width == 0) && islabels)
        width = GST_ROUND_UP_4 (DEFAULT_FONT_SIZE * MAX_TEXT_LENGTH * 3 / 5);
      else if (width == 0)
        width = DEFAULT_VIDEO_WIDTH;

      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }

    width = g_value_get_int (value);
    value = gst_structure_get_value (output, "height");

    if ((height != 0) && (value != NULL) && gst_value_is_fixed (value)) {
      GST_ERROR_OBJECT (postprocess, "Fixated height in filter caps is not "
          "supported with current post-process type!");
      return NULL;
    } else if ((NULL == value) || !gst_value_is_fixed (value)) {
      if ((height == 0) && islabels)
        height = GST_ROUND_UP_4 (DEFAULT_FONT_SIZE * postprocess->n_results);
      else if (height == 0)
        height = DEFAULT_VIDEO_HEIGHT;

      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }

    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (postprocess, "Output width and height fixated to: %dx%d",
        width, height);
  }

  // Fixate any remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (postprocess, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_post_process_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstStructure *structure = NULL;
  gboolean success = FALSE;

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

 if (gst_structure_has_name (structure, "video/x-raw"))
    postprocess->outmode = GST_OUTPUT_MODE_VIDEO;
  else if (gst_structure_has_name (structure, "text/x-raw"))
    postprocess->outmode = GST_OUTPUT_MODE_TEXT;
  else if (gst_structure_has_name (structure, "neural-network/tensors"))
    postprocess->outmode = GST_OUTPUT_MODE_TENSORS;

  if (postprocess->engine != NULL) {
    GstCaps *modulecaps = gst_ml_engine_get_caps (postprocess->engine);

    if (!gst_caps_can_intersect (incaps, modulecaps)) {
      GST_ELEMENT_ERROR (postprocess, RESOURCE, FAILED, (NULL),
          ("Module caps %" GST_PTR_FORMAT " do not intersect with the "
          "negotiated caps %" GST_PTR_FORMAT "!", modulecaps, incaps));
      gst_caps_unref (modulecaps);
      return FALSE;
    }

    gst_caps_unref (modulecaps);

    success = gst_ml_engine_configure (postprocess->engine,
        postprocess->stage_id, postprocess->n_results, postprocess->outmode,
        postprocess->stabilization, postprocess->labels, postprocess->settings);

    if (!success) {
      GST_ELEMENT_ERROR (postprocess, RESOURCE, FAILED, (NULL),
          ("Failed to configure engine!"));
      return FALSE;
    }
  }

  g_clear_pointer (&postprocess->mlinfo, gst_ml_info_free);
  postprocess->mlinfo = gst_ml_info_new ();

  if (!gst_ml_info_from_caps (postprocess->mlinfo, incaps)) {
    GST_ELEMENT_ERROR (postprocess, CORE, CAPS, (NULL),
        ("Failed to get input ML info from caps %" GST_PTR_FORMAT "!", incaps));
    return FALSE;
  }

  if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO) {
    if (GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0) > 1) {
      GST_ELEMENT_ERROR (postprocess, CORE, FAILED, (NULL),
          ("Batched input tensors with video output is not supported!"));
      return FALSE;
    }

    g_clear_pointer (&postprocess->vinfo, gst_video_info_free);
    postprocess->vinfo = gst_video_info_new ();

    if (!gst_video_info_from_caps (postprocess->vinfo, outcaps)) {
      GST_ERROR_OBJECT (postprocess, "Failed to get output video info from caps"
          " %" GST_PTR_FORMAT "!", outcaps);
      return FALSE;
    }
  }

  if (GST_IS_DETECTION (postprocess->type)) {
    // Inform any ML pre-process downstream about it's ROI stage ID.
    structure = gst_structure_new ("ml-detection-information", "stage-id",
        G_TYPE_UINT, postprocess->stage_id, NULL);

    GST_DEBUG_OBJECT (postprocess, "Send stage ID %u", postprocess->stage_id);

    success = gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (postprocess),
        gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure));

    if (!success) {
      // TODO: Temporary workaround. Need to be addressed proerly.
      // In case of daisycahin it is possible to negotiate wrong stage-id without
      // thrwing an error.
      GST_WARNING_OBJECT (postprocess, "Failed to send ML info downstream!");
    }
  }

  GST_DEBUG_OBJECT (postprocess, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (postprocess, "Output caps: %" GST_PTR_FORMAT, outcaps);

  gst_base_transform_set_passthrough (base, FALSE);
  return TRUE;
}

static GstStateChangeReturn
gst_ml_video_post_process_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GEnumClass *eclass = NULL;
      GEnumValue *evalue = NULL;

      if (DEFAULT_PROP_MODULE == postprocess->mdlenum) {
        GST_INFO_OBJECT (postprocess, "Module name not set, checking whether "
            "a callback is connected to one of the 'process' signals");

        postprocess->type = gst_ml_post_process_signal_get_type (postprocess);

        if (postprocess->type == g_quark_from_string ("unknown")) {
          GST_ERROR_OBJECT (postprocess, "No 'process' signal is connected!");
          return GST_STATE_CHANGE_FAILURE;
        }

        // Choose processing function based on connected signal type.
        if (GST_IS_IMAGE_CLASSIFICATION (postprocess->type) ||
            GST_IS_AUDIO_CLASSIFICATION (postprocess->type))
          postprocess->process = gst_ml_post_process_signal_classifications;
        else if (GST_IS_DETECTION (postprocess->type))
          postprocess->process = gst_ml_post_process_signal_detections;
        else if (GST_IS_POSE (postprocess->type))
          postprocess->process = gst_ml_post_process_signal_poses;
        else if (GST_IS_SEGMENTATION (postprocess->type))
          postprocess->process = gst_ml_post_process_signal_segmentations;
        else if (GST_IS_DEPTH_MAP (postprocess->type))
          postprocess->process = gst_ml_post_process_signal_depth_maps;
        else if (GST_IS_TENSOR (postprocess->type))
          postprocess->process = gst_ml_post_process_signal_tensors;

        GST_INFO_OBJECT (postprocess, "Using 'process' signal of type '%s'",
            g_quark_to_string (postprocess->type));
        break;
      }

      eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_ML_MODULES));
      evalue = g_enum_get_value (eclass, postprocess->mdlenum);

      gst_ml_engine_free (postprocess->engine);
      postprocess->engine = gst_ml_engine_new (evalue->value_nick);

      if (postprocess->engine == NULL) {
        GST_ERROR_OBJECT (postprocess, "Module creation failed!");
        return GST_STATE_CHANGE_FAILURE;
      }

      // Set processing function to use the module API.
      postprocess->process = gst_ml_post_process_module_execute;
      postprocess->type = gst_ml_engine_get_type (postprocess->engine);

      GST_INFO_OBJECT (postprocess, "Using module of type '%s'",
          g_quark_to_string (postprocess->type));
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_clear_pointer (&postprocess->engine, gst_ml_engine_free);
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ml_post_process_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstMLFrame mlframe = { 0, };
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Perform pre-processing on the input buffer.
  time = gst_util_get_timestamp ();

  if ((gst_buffer_get_size (inbuffer) != 0) &&
      !gst_ml_frame_map (&mlframe, postprocess->mlinfo, inbuffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (postprocess, "Failed to map input ML buffer!");
    return GST_FLOW_ERROR;
  }

  if (postprocess->outmode == GST_OUTPUT_MODE_TENSORS)
    success = gst_ml_post_process_tensors (postprocess, &mlframe, outbuffer);
  else if (postprocess->outmode == GST_OUTPUT_MODE_VIDEO)
    success = gst_ml_post_process_video (postprocess, &mlframe, outbuffer);
  else if (postprocess->outmode == GST_OUTPUT_MODE_TEXT)
    success = gst_ml_post_process_text (postprocess, &mlframe, outbuffer);

  gst_ml_frame_unmap (&mlframe);

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process input tensors!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (postprocess, "Postprocess took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_post_process_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (object);

  switch (prop_id) {
    case PROP_MODULE:
      postprocess->mdlenum = g_value_get_enum (value);
      break;
    case PROP_LABELS:
      g_free (postprocess->labels);
      postprocess->labels = g_strdup (g_value_get_string (value));
      break;
    case PROP_NUM_RESULTS:
      postprocess->n_results = g_value_get_uint (value);
      break;
    case PROP_SETTINGS:
      g_free (postprocess->settings);
      postprocess->settings = g_strdup (g_value_get_string (value));
      break;
    case PROP_STABILIZATION:
      postprocess->stabilization = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_post_process_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (object);

  switch (prop_id) {
     case PROP_MODULE:
      g_value_set_enum (value, postprocess->mdlenum);
      break;
    case PROP_LABELS:
      g_value_set_string (value, postprocess->labels);
      break;
    case PROP_NUM_RESULTS:
      g_value_set_uint (value, postprocess->n_results);
      break;
    case PROP_SETTINGS:
      g_value_set_string (value, postprocess->settings);
      break;
    case PROP_STABILIZATION:
      g_value_set_boolean (value, postprocess->stabilization);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_post_process_finalize (GObject * object)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (object);

  if (postprocess->stashedpredictions != NULL) {
    g_list_free_full (postprocess->stashedpredictions,
        (GDestroyNotify) gst_ml_detections_unref);
  }

  g_ptr_array_free (postprocess->mlparams, TRUE);
  gst_ml_engine_free (postprocess->engine);

  if (postprocess->mlinfo != NULL)
    gst_ml_info_free (postprocess->mlinfo);

  if (postprocess->vinfo != NULL)
    gst_video_info_free (postprocess->vinfo);

  if (postprocess->outpool != NULL)
    gst_object_unref (postprocess->outpool);

  if (postprocess->labels)
    g_free (postprocess->labels);

  if (postprocess->settings)
    g_free (postprocess->settings);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (postprocess));
}

static void
gst_ml_post_process_class_init (GstMLPostProcessClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_post_process_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_post_process_get_property);
  gobject->finalize  = GST_DEBUG_FUNCPTR (gst_ml_post_process_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          GST_TYPE_ML_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_LABELS,
      g_param_spec_string ("labels", "Labels",
          "Labels filename.  Applicable only for some modules.",
          DEFAULT_PROP_LABELS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_NUM_RESULTS,
      g_param_spec_uint ("results", "Results",
          "Number of results to display. Applicable only for some modules.",
          0, 50, DEFAULT_PROP_NUM_RESULTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_SETTINGS,
      g_param_spec_string ("settings", "Settings",
          "Settings used by the chosen engine for post-processing. "
          "Applicable only for some modules.", DEFAULT_PROP_SETTINGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_STABILIZATION,
      g_param_spec_boolean ("bbox-stabilization", "stabilization",
          "Enable lightweight stabilization for object detection prediction.",
          DEFAULT_PROP_STABILIZATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_IMAGE_CLASSIFICATIONS] =
      g_signal_new ("process-image-classification", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_CLASSIFICATIONS);
  signals[SIGNAL_AUDIO_CLASSIFICATIONS] =
      g_signal_new ("process-audio-classification", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_CLASSIFICATIONS);
  signals[SIGNAL_DETECTIONS] =
      g_signal_new ("process-object-detection", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_DETECTIONS);
  signals[SIGNAL_POSES] =
      g_signal_new ("process-pose-estimation", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_POSES);
  signals[SIGNAL_SEGMENTATIONS] =
      g_signal_new ("process-segmentation", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_SEGMENTATIONS);
  signals[SIGNAL_DEPTH_MAPS] =
      g_signal_new ("process-depth-estimation", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_DEPTH_MAPS);
  signals[SIGNAL_TENSORS] =
      g_signal_new ("process-tensors", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
          0, NULL, NULL, NULL, G_TYPE_BOOLEAN, 3, GST_TYPE_ML_FRAME,
          GST_TYPE_STRUCTURE, GST_TYPE_ML_FRAME);

  gst_element_class_set_static_metadata (element,
      "Machine Learning postprocess", "Filter/Effect/Converter",
      "Machine Learning plugin for postprocess", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_post_process_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_post_process_src_template ());

  element->change_state =
      GST_DEBUG_FUNCPTR (gst_ml_video_post_process_change_state);

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_prepare_output_buffer);

  base->sink_event = GST_DEBUG_FUNCPTR (gst_ml_post_process_sink_event);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_post_process_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_post_process_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_post_process_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_post_process_transform);

  GST_DEBUG_CATEGORY_INIT (gst_ml_post_process_debug, "qtimlpostprocess", 0,
      "QTI ML post process plugin");
}

static void
gst_ml_post_process_init (GstMLPostProcess * postprocess)
{
  postprocess->outpool = NULL;
  postprocess->mlinfo = NULL;
  postprocess->vinfo = NULL;

  postprocess->stage_id = 0;

  postprocess->outmode = GST_OUTPUT_MODE_UNKNOWN;
  postprocess->type = g_quark_from_string ("unknown");
  postprocess->engine = NULL;
  postprocess->mlparams = g_ptr_array_new ();

  postprocess->stashedpredictions = NULL;

  postprocess->mdlenum = DEFAULT_PROP_MODULE;
  postprocess->labels = DEFAULT_PROP_LABELS;
  postprocess->n_results = DEFAULT_PROP_NUM_RESULTS;
  postprocess->settings = DEFAULT_PROP_SETTINGS;
  postprocess->stabilization = DEFAULT_PROP_STABILIZATION;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (postprocess), TRUE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlpostprocess", GST_RANK_NONE,
      GST_TYPE_ML_POST_PROCESS);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlpostprocess,
    "QTI Machine Learning plugin for post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
