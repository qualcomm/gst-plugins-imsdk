/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
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
 * ​​​​​Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <math.h>

#include <gst/ml/gstmlmeta.h>

#include "ml-video-posenet-module.h"

#define QUANTIZATION_HEATMAP_SCALE 0.0470588244497776f
#define QUANTIZATION_HEATMAP_OFFSET 128
#define QUANTIZATION_OFFSET_SCALE 0.3921568691730499f
#define QUANTIZATION_OFFSET_OFFSET 128
#define QUANTIZATION_DISPLACEMENT_SCALE 1.3875764608383179f
#define QUANTIZATION_DISPLACEMENT_OFFSET 117

#define POSE_MAX_COUNT 20
#define POSE_PART_MAX_COUNT 250
#define POSE_FEATURE_HEIGHT 31
#define POSE_FEATURE_WIDTH 41
#define POSE_FEATURE_MAP_SIZE (POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH)

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

#define clamp(v, min, max) ({                 \
  typeof(v) _v = (v);                         \
  typeof(min) _min = (min);                   \
  typeof(max) _max = (max);                   \
  _v < _min ? _min : (_v < _max ? _v : _max); \
})

typedef struct
{
  float output_stride;
  int max_pose_detections;
  float min_pose_score;
  float heatmap_score_threshold;
  int nms_radius_squared;
  int local_maximum_radius;
} PoseConfig;

typedef struct
{
  PoseConfig pose_config;

  // Floating point version of the tensors.
  float raw_heatmaps[POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH *
      POSENET_KP_COUNT];
  float raw_offsets[POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH *
      POSENET_KP_COUNT * 2];
  float raw_displacements[POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH *
      (POSENET_KP_COUNT - 1) * 4];

  // Tensors used for the pose decoding
  float offsets[POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH *
      POSENET_KP_COUNT * 2];
  float displacements_bwd[POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH *
      (POSENET_KP_COUNT - 1) * 2];
  float displacements_fwd[POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH *
      (POSENET_KP_COUNT - 1) * 2];
} GstPrivateModule;

typedef struct
{
  float x;
  float y;
} FloatCoord;

typedef struct
{
  int x;
  int y;
} IntCoord;

typedef struct
{
  float part_score;
  int keypoint_id;
  int x;
  int y;
} Part;

typedef struct
{
  float part_score;
  int keypoint_id;
  float x;
  float y;
} PartWithFloatCoord;

typedef struct
{
  int parent;
  int child;
} ParentChildTuple;

static ParentChildTuple parent_child_tuples[16] = {
  {POSENET_KP_NOSE, POSENET_KP_LEFT_EYE},
  {POSENET_KP_LEFT_EYE, POSENET_KP_LEFT_EAR},
  {POSENET_KP_NOSE, POSENET_KP_RIGHT_EYE},
  {POSENET_KP_RIGHT_EYE, POSENET_KP_RIGHT_EAR},
  {POSENET_KP_NOSE, POSENET_KP_LEFT_SHOULDER},
  {POSENET_KP_LEFT_SHOULDER, POSENET_KP_LEFT_ELBOW},
  {POSENET_KP_LEFT_ELBOW, POSENET_KP_LEFT_WRIST},
  {POSENET_KP_LEFT_SHOULDER, POSENET_KP_LEFT_HIP},
  {POSENET_KP_LEFT_HIP, POSENET_KP_LEFT_KNEE},
  {POSENET_KP_LEFT_KNEE, POSENET_KP_LEFT_ANKLE},
  {POSENET_KP_NOSE, POSENET_KP_RIGHT_SHOULDER},
  {POSENET_KP_RIGHT_SHOULDER, POSENET_KP_RIGHT_ELBOW},
  {POSENET_KP_RIGHT_ELBOW, POSENET_KP_RIGHT_WRIST},
  {POSENET_KP_RIGHT_SHOULDER, POSENET_KP_RIGHT_HIP},
  {POSENET_KP_RIGHT_HIP, POSENET_KP_RIGHT_KNEE},
  {POSENET_KP_RIGHT_KNEE, POSENET_KP_RIGHT_ANKLE}
};

static int
gst_ml_sort_part_score (const void *p_val1, const void *p_val2)
{
  const Part *p_score1 = (const Part *) (p_val1);
  const Part *p_score2 = (const Part *) (p_val2);

  if (p_score1->part_score > p_score2->part_score) {
    return -1;
  }
  if (p_score1->part_score < p_score2->part_score) {
    return 1;
  }
  return 0;
}

static void
gst_ml_do_heatmap_normalize (float *p_raw_scores, int raw_scores_size)
{
  for (int i = 0; i < raw_scores_size; i++) {
    p_raw_scores[i] = 1.0f / (1.0f + exp (-p_raw_scores[i]));
  }
}

static void
gst_ml_find_max_filter_for_vector (PoseConfig * p_pose_config,
    float *p_kp_scores_row, float *p_max_vals_row)
{
  float *p_old = p_kp_scores_row;
  float *p_new = p_max_vals_row;
  int max_idx = 0;
  int max_radius = p_pose_config->local_maximum_radius;
  int block_size = max_radius * 2 + 1;
  float max_val = p_old[0];

  // Initialization on left-boundary values
  for (int tmp_idx = 1; tmp_idx < block_size; tmp_idx++) {
    float cur_val = p_old[tmp_idx];
    if (max_val <= cur_val) {
      max_val = cur_val;
      max_idx = tmp_idx;
    }
  }
  for (int i = 0; i <= max_radius; i++) {
    p_new[i] = max_val;
  }

  // Process non-boundary values
  for (int tmp_idx = max_radius + 1;
      tmp_idx < (POSE_FEATURE_WIDTH - max_radius); tmp_idx++) {
    if (max_idx >= (tmp_idx - max_radius)) {
      int next_block_first_idx = tmp_idx + max_radius;
      if (p_old[max_idx] < p_old[next_block_first_idx]) {
        max_idx = next_block_first_idx;
        max_val = p_old[next_block_first_idx];
      }
    } else {
      max_idx = tmp_idx - max_radius;
      max_val = p_old[max_idx];
      int block_end = max_idx + block_size;

      for (int tmp_block_idx = max_idx; tmp_block_idx < block_end;
          tmp_block_idx++) {
        float tmp_val = p_old[tmp_block_idx];
        if (max_val <= tmp_val) {
          max_idx = tmp_block_idx;
          max_val = tmp_val;
        }
      }
    }
    p_new[tmp_idx] = max_val;
  }

  // Process right-boundary values
  for (int i = POSE_FEATURE_WIDTH - max_radius; i < POSE_FEATURE_WIDTH; i++) {
    p_new[i] = max_val;
  }
}

static void
gst_ml_find_max_filter_for_matrix (PoseConfig * p_pose_config,
    float *p_kp_scores, float *p_filtered_kp_scores)
{
  float tmp_matrix[POSE_FEATURE_MAP_SIZE] = { 0.0f };
  float tmp_matrix_transposed[POSE_FEATURE_MAP_SIZE] = { 0.0f };

  // Maximum filtering on each row of one matrix
  for (int row = 0; row < POSE_FEATURE_HEIGHT; row++) {
    float *p_old_row_first = p_kp_scores + row * POSE_FEATURE_WIDTH;
    float *p_new_row_first = tmp_matrix + row * POSE_FEATURE_WIDTH;
    gst_ml_find_max_filter_for_vector (p_pose_config, p_old_row_first,
        p_new_row_first);
  }

  // Transpose a matrix
  for (int row = 0; row < POSE_FEATURE_WIDTH; row++) {
    for (int col = 0; col < POSE_FEATURE_HEIGHT; col++) {
      tmp_matrix_transposed[row * POSE_FEATURE_HEIGHT + col] =
          tmp_matrix[col * POSE_FEATURE_WIDTH + row];
    }
  }

  // Maximum filtering on each column of a matrix (i.e. each row of the transposed matrix)
  for (int col = 0; col < POSE_FEATURE_WIDTH; col++) {
    float *p_old_row_first = tmp_matrix_transposed + col * POSE_FEATURE_HEIGHT;
    float *p_new_row_first = tmp_matrix + col * POSE_FEATURE_HEIGHT;
    gst_ml_find_max_filter_for_vector (p_pose_config, p_old_row_first,
        p_new_row_first);
  }

  // Transpose a matrix back
  for (int row = 0; row < POSE_FEATURE_HEIGHT; row++) {
    for (int col = 0; col < POSE_FEATURE_WIDTH; col++) {
      p_filtered_kp_scores[row * POSE_FEATURE_WIDTH + col] =
          tmp_matrix[col * POSE_FEATURE_HEIGHT + row];
    }
  }
}

static int
gst_ml_select_keypoint_with_score (PoseConfig * p_pose_config,
    float *p_raw_scores, Part * p_parts)
{
  float heatmap_score_threshold = p_pose_config->heatmap_score_threshold;
  int parts_count = 0;

  float kp_scores[POSE_FEATURE_MAP_SIZE] = { 0.0f };
  float filtered_kp_scores[POSE_FEATURE_MAP_SIZE] = { 0.0f };
  // Heatmap normalization to range [0, 1] via a Sigmoid function
  int raw_scores_size =
      POSE_FEATURE_HEIGHT * POSE_FEATURE_WIDTH * POSENET_KP_COUNT;
  gst_ml_do_heatmap_normalize (p_raw_scores, raw_scores_size);
  // Iterate over keypoints and apply local maximum filtering on the feature map/array corresponding to each keypoint
  for (int keypoint_id = 0; keypoint_id < POSENET_KP_COUNT; keypoint_id++) {
    // Apply thresholding over raw heatmap values to remove keypoints with low heatmap values
    for (int i = 0; i < POSE_FEATURE_HEIGHT; i++) {
      for (int j = 0; j < POSE_FEATURE_WIDTH; j++) {
        float temp_val = 0.0f;
        kp_scores[i * POSE_FEATURE_WIDTH + j] = 0.0f;
        filtered_kp_scores[i * POSE_FEATURE_WIDTH + j] = 0.0f;
        temp_val =
            p_raw_scores[i * POSE_FEATURE_WIDTH * POSENET_KP_COUNT +
            j * POSENET_KP_COUNT + keypoint_id];
        temp_val = (temp_val > heatmap_score_threshold) ? temp_val : 0.0f;
        kp_scores[i * POSE_FEATURE_WIDTH + j] = temp_val;
      }
    }

    // Apply maximum filtering on the heatmap corresponding to each keypoint
    gst_ml_find_max_filter_for_matrix (p_pose_config, kp_scores,
        filtered_kp_scores);

    for (int i = 0; i < POSE_FEATURE_HEIGHT; i++) {
      for (int j = 0; j < POSE_FEATURE_WIDTH; j++) {
        int tmp_idx = i * POSE_FEATURE_WIDTH + j;
        if ((kp_scores[tmp_idx] == filtered_kp_scores[tmp_idx])
            && (kp_scores[tmp_idx] > 0)) {
          p_parts[parts_count].y = i;
          p_parts[parts_count].x = j;
          p_parts[parts_count].part_score =
              p_raw_scores[i * POSE_FEATURE_WIDTH * POSENET_KP_COUNT +
              j * POSENET_KP_COUNT + keypoint_id];
          p_parts[parts_count].keypoint_id = keypoint_id;
          parts_count++;
        }
      }
    }
  }
  return parts_count;
}

static void
gst_ml_reshape_last_two_dimensions (float *p_raw_offsets,
    float *p_reshaped_offsets)
{
  int tmp_idx_raw_y = 0;
  int tmp_idx_new_y = 0;
  int tmp_idx_raw_x = 0;
  int tmp_idx_new_x = 0;

  // New shape: [height, width, num_keypoint, 2], old shape: [height, width, 2, num_keypoint]
  for (int i = 0; i < POSE_FEATURE_HEIGHT; i++) {
    for (int j = 0; j < POSE_FEATURE_WIDTH; j++) {
      for (int k = 0; k < POSENET_KP_COUNT; k++) {
        tmp_idx_new_y = i * POSE_FEATURE_WIDTH * POSENET_KP_COUNT * 2 +
            j * POSENET_KP_COUNT * 2 + k * 2;
        tmp_idx_new_x = tmp_idx_new_y + 1;
        tmp_idx_raw_y = i * POSE_FEATURE_WIDTH * POSENET_KP_COUNT * 2 +
            j * POSENET_KP_COUNT * 2 + k;
        tmp_idx_raw_x = tmp_idx_raw_y + POSENET_KP_COUNT;
        p_reshaped_offsets[tmp_idx_new_y] = p_raw_offsets[tmp_idx_raw_y];
        p_reshaped_offsets[tmp_idx_new_x] = p_raw_offsets[tmp_idx_raw_x];
      }
    }
  }
}

static void
gst_ml_reshape_displacements (float *p_raw_displacements,
    float *p_reshaped_displacements_bwd, float *p_reshaped_displacements_fwd)
{
  int tmp_idx_bwd_raw_y = 0;
  int tmp_idx_bwd_raw_x = 0;
  int tmp_idx_fwd_raw_y = 0;
  int tmp_idx_fwd_raw_x = 0;
  int tmp_idx_new_y = 0;
  int tmp_idx_new_x = 0;
  int num_edge = POSENET_KP_COUNT - 1;

  // New shape: [height, width, num_edge, 2] (one tensor for BWD and one tensor
  // for FWD), old shape: [height, width, 4, num_edge]
  for (int i = 0; i < POSE_FEATURE_HEIGHT; i++) {
    for (int j = 0; j < POSE_FEATURE_WIDTH; j++) {
      for (int k = 0; k < num_edge; k++) {
        tmp_idx_fwd_raw_y =
            i * POSE_FEATURE_WIDTH * num_edge * 4 + j * num_edge * 4 + k;
        tmp_idx_fwd_raw_x = tmp_idx_fwd_raw_y + num_edge;
        tmp_idx_bwd_raw_y =
            i * POSE_FEATURE_WIDTH * num_edge * 4 + j * num_edge * 4 +
            2 * num_edge + k;
        tmp_idx_bwd_raw_x = tmp_idx_bwd_raw_y + num_edge;
        tmp_idx_new_y =
            i * POSE_FEATURE_WIDTH * num_edge * 2 + j * num_edge * 2 + k * 2;
        tmp_idx_new_x = tmp_idx_new_y + 1;
        p_reshaped_displacements_bwd[tmp_idx_new_y] =
            p_raw_displacements[tmp_idx_bwd_raw_y];
        p_reshaped_displacements_bwd[tmp_idx_new_x] =
            p_raw_displacements[tmp_idx_bwd_raw_x];
        p_reshaped_displacements_fwd[tmp_idx_new_y] =
            p_raw_displacements[tmp_idx_fwd_raw_y];
        p_reshaped_displacements_fwd[tmp_idx_new_x] =
            p_raw_displacements[tmp_idx_fwd_raw_x];
      }
    }
  }
}

static int
gst_ml_do_nms_pose (GstPose * p_pose_results,
    int pose_count, int root_id, float radius_squared, FloatCoord cur_point)
{
  float tmpval = 0.0f;
  for (int i = 0; i < pose_count; i++) {
    tmpval = pow (cur_point.x - p_pose_results[i].keypoint[root_id].x, 2) +
        pow (cur_point.y - p_pose_results[i].keypoint[root_id].y, 2);
    if (tmpval < radius_squared)
      return 1;
  }

  return 0;
}

static void
gst_ml_propagate_keypoint (int edge_id, GstPoseKeypoint * p_keypoint_coords,
    int source_keypoint_id, int target_keypoint_id, float *p_scores,
    float *p_offsets, float *p_displacements, PoseConfig * p_pose_config,
    PartWithFloatCoord * p_cur_part)
{
  float output_stride = p_pose_config->output_stride;
  int num_edges = POSENET_KP_COUNT - 1;

  FloatCoord source_keypoint_coords = {
    .x = p_keypoint_coords[source_keypoint_id].x,
    .y = p_keypoint_coords[source_keypoint_id].y
  };
  IntCoord source_keypoint_indices = {
    .x = clamp (round (source_keypoint_coords.x / output_stride), 0,
        POSE_FEATURE_WIDTH - 1),
    .y = clamp (round (source_keypoint_coords.y / output_stride), 0,
        POSE_FEATURE_HEIGHT - 1)
  };

  int displaced_point_idx =
      source_keypoint_indices.y * POSE_FEATURE_WIDTH * num_edges * 2 +
      source_keypoint_indices.x * num_edges * 2 + edge_id * 2;

  FloatCoord displaced_point = {
    .x = source_keypoint_coords.x + p_displacements[displaced_point_idx + 1],
    .y = source_keypoint_coords.y + p_displacements[displaced_point_idx]
  };
  IntCoord displaced_point_indices = {
    .x = clamp (round (displaced_point.x / output_stride), 0,
        POSE_FEATURE_WIDTH - 1),
    .y = clamp (round (displaced_point.y / output_stride), 0,
        POSE_FEATURE_HEIGHT - 1)
  };

  int offsets_idx =
      displaced_point_indices.y * POSE_FEATURE_WIDTH * POSENET_KP_COUNT * 2 +
      displaced_point_indices.x * POSENET_KP_COUNT * 2 + target_keypoint_id * 2;

  p_cur_part->part_score =
      p_scores[displaced_point_indices.y * POSE_FEATURE_WIDTH *
      POSENET_KP_COUNT + displaced_point_indices.x * POSENET_KP_COUNT +
      target_keypoint_id];
  p_cur_part->x =
      displaced_point_indices.x * output_stride + p_offsets[offsets_idx + 1];
  p_cur_part->y =
      displaced_point_indices.y * output_stride + p_offsets[offsets_idx];
}

static void
gst_ml_decode_pose (float root_score, int root_id, FloatCoord root_image_coords,
    float *p_raw_heatmaps, float *p_offsets, float *p_displacements_bwd,
    float *p_displacements_fwd, PoseConfig * p_pose_config,
    GstPose * p_pose_result)
{
  int num_edges = POSENET_KP_COUNT - 1;
  PartWithFloatCoord tmp_part = { };

  for (int i = 0; i < POSENET_KP_COUNT; i++) {
    p_pose_result->keypoint[i].score = 0.0f;
    p_pose_result->keypoint[i].x = 0.0f;
    p_pose_result->keypoint[i].y = 0.0f;
  }
  p_pose_result->keypoint[root_id].score = root_score;
  p_pose_result->keypoint[root_id].x = root_image_coords.x;
  p_pose_result->keypoint[root_id].y = root_image_coords.y;

  // Backward search
  for (int edge = num_edges - 1; edge >= 0; edge--) {
    int target_keypoint_id = parent_child_tuples[edge].parent;
    int source_keypoint_id = parent_child_tuples[edge].child;

    if ((p_pose_result->keypoint[source_keypoint_id].score > 0.0f) &&
        (p_pose_result->keypoint[target_keypoint_id].score == 0.0f)) {
      gst_ml_propagate_keypoint (edge,
          p_pose_result->keypoint, source_keypoint_id, target_keypoint_id,
          p_raw_heatmaps, p_offsets, p_displacements_bwd, p_pose_config,
          &tmp_part);
      p_pose_result->keypoint[target_keypoint_id].score = tmp_part.part_score;
      p_pose_result->keypoint[target_keypoint_id].y = tmp_part.y;
      p_pose_result->keypoint[target_keypoint_id].x = tmp_part.x;
    }
  }

  // Forward search
  for (int edge = 0; edge < num_edges; edge++) {
    int source_keypoint_id = parent_child_tuples[edge].parent;
    int target_keypoint_id = parent_child_tuples[edge].child;

    if ((p_pose_result->keypoint[source_keypoint_id].score > 0.0f) &&
        (p_pose_result->keypoint[target_keypoint_id].score == 0.0f)) {
      gst_ml_propagate_keypoint (edge,
          p_pose_result->keypoint, source_keypoint_id, target_keypoint_id,
          p_raw_heatmaps, p_offsets, p_displacements_fwd, p_pose_config,
          &tmp_part);
      p_pose_result->keypoint[target_keypoint_id].score = tmp_part.part_score;
      p_pose_result->keypoint[target_keypoint_id].y = tmp_part.y;
      p_pose_result->keypoint[target_keypoint_id].x = tmp_part.x;
    }
  }
}

static float
gst_ml_calculate_pose_instance_score (GstPose * p_pose_results,
    int pose_count, GstPose * p_cur_pose_result, PoseConfig * p_pose_config)
{
  float not_overlapped_scores;
  float sum = 0.0f;

  if (0 != pose_count) {
    char flag_nms[POSE_MAX_COUNT * POSENET_KP_COUNT] = {0};

    // Calculate non-overlapped scores and apply non-maximum surpression (NMS) between poses
    for (int i = 0; i < pose_count; i++) {
      for (int j = 0; j < POSENET_KP_COUNT; j++) {
        int tmp_idx = i * POSENET_KP_COUNT + j;
        float dist_between_poses =
            pow (p_pose_results[i].keypoint[j].x -
                 p_cur_pose_result->keypoint[j].x, 2) +
            pow (p_pose_results[i].keypoint[j].y -
                 p_cur_pose_result->keypoint[j].y, 2);
        if (dist_between_poses > p_pose_config->nms_radius_squared) {
          flag_nms[tmp_idx] = 1;
        }
      }
    }

    for (int j = 0; j < POSENET_KP_COUNT; j++) {
      int flag_nms_keypoint = 1;
      for (int i = 0; i < pose_count; i++) {
        int tmp_idx = i * POSENET_KP_COUNT + j;
        if (0 == flag_nms[tmp_idx]) {
          flag_nms_keypoint = 0;
          break;
        }
      }

      if (flag_nms_keypoint) {
        sum += p_cur_pose_result->keypoint[j].score;
      }
    }
    not_overlapped_scores = sum / POSENET_KP_COUNT;

  } else {
    for (int i = 0; i < POSENET_KP_COUNT; i++) {
      sum += p_cur_pose_result->keypoint[i].score;
    }
    not_overlapped_scores = sum / POSENET_KP_COUNT;
  }

  return not_overlapped_scores;
}

static void
gst_ml_dequantize (float *dest, guint8 * src, size_t length, float scale,
    guint8 offset)
{
  for (size_t i = 0; i < length; ++i) {
    dest[i] = (float) (src[i] - offset) * scale;
  }
}

gpointer
gst_ml_video_posenet_module_init ()
{
  GstPrivateModule *module = NULL;

  module = g_slice_new0 (GstPrivateModule);
  g_return_val_if_fail (module != NULL, NULL);

  module->pose_config.output_stride = 16.0f;
  module->pose_config.max_pose_detections = POSE_MAX_COUNT;
  module->pose_config.min_pose_score = 0.10f;
  module->pose_config.heatmap_score_threshold = 0.35f;
  module->pose_config.nms_radius_squared = 20.0f * 20.0f;
  module->pose_config.local_maximum_radius = 1;

  return module;
}

void
gst_ml_video_posenet_module_deinit (gpointer instance)
{
  GstPrivateModule *module = instance;

  if (NULL == module)
    return;

  g_slice_free (GstPrivateModule, module);
}

gboolean
gst_ml_video_posenet_module_process (gpointer instance, GstBuffer * buffer,
    GList ** poses)
{
  GstPrivateModule *module = instance;

  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (poses != NULL, FALSE);

  if (gst_buffer_n_memory (buffer) != 3) {
    GST_ERROR ("Expecting 3 tensor memory blocks but received %u!",
        gst_buffer_n_memory (buffer));
    return FALSE;
  }

  for (guint idx = 0; idx < gst_buffer_n_memory (buffer); idx++) {
    GstMLTensorMeta *mlmeta = NULL;

    if (!(mlmeta = gst_buffer_get_ml_tensor_meta_id (buffer, idx))) {
      GST_ERROR ("Buffer has no ML meta for tensor %u!", idx);
      return FALSE;
    } else if (mlmeta->type != GST_ML_TYPE_UINT8) {
      GST_ERROR ("Buffer has unsupported type for tensor %u!", idx);
      return FALSE;
    }
  }

  // Dequantization of model outputs is needed,
  // because the postprocessing operates on float values
  {
    GstMapInfo info;
    if (!gst_buffer_map_range (buffer, 0, 1, &info, GST_MAP_READ)) {
      GST_ERROR ("Failed to map heatmaps memory block!");
      return FALSE;
    }
    gst_ml_dequantize (module->raw_heatmaps, info.data,
        ARRAY_LENGTH (module->raw_heatmaps),
        QUANTIZATION_HEATMAP_SCALE, QUANTIZATION_HEATMAP_OFFSET);
    gst_buffer_unmap (buffer, &info);
  }
  {
    GstMapInfo info;
    if (!gst_buffer_map_range (buffer, 1, 1, &info, GST_MAP_READ)) {
      GST_ERROR ("Failed to map offsets memory block!");
      return FALSE;
    }
    gst_ml_dequantize (module->raw_offsets, info.data,
        ARRAY_LENGTH (module->raw_offsets),
        QUANTIZATION_OFFSET_SCALE, QUANTIZATION_OFFSET_OFFSET);
    gst_buffer_unmap (buffer, &info);
  }
  {
    GstMapInfo info;
    if (!gst_buffer_map_range (buffer, 2, 1, &info, GST_MAP_READ)) {
      GST_ERROR ("Failed to map displacements memory block!");
      return FALSE;
    }
    gst_ml_dequantize (module->raw_displacements, info.data,
        ARRAY_LENGTH (module->raw_displacements),
        QUANTIZATION_DISPLACEMENT_SCALE, QUANTIZATION_DISPLACEMENT_OFFSET);
    gst_buffer_unmap (buffer, &info);
  }

  Part scored_parts[POSE_PART_MAX_COUNT];
  int parts_count = gst_ml_select_keypoint_with_score (&module->pose_config,
      module->raw_heatmaps, &scored_parts[0]);

  int pose_count = 0;

  GstPose pose_results[POSE_MAX_COUNT] = { 0 };

  // Sort selected keypoints according to heatmap scores
  qsort (scored_parts, parts_count, sizeof (Part), gst_ml_sort_part_score);

  // Reshape short-range offsets, mid-range displacements (backward), and mid-range displacements (forward)
  gst_ml_reshape_last_two_dimensions (module->raw_offsets, module->offsets);
  gst_ml_reshape_displacements (module->raw_displacements,
      module->displacements_bwd, module->displacements_fwd);

  // Generate human keypoint/part graph information

  // Search adjacent, connected keypoints and propagate pose information for each selected keypoint (i.e. root/seed)
  for (int i = 0; i < parts_count; i++) {
    float root_score = scored_parts[i].part_score;
    int root_id = scored_parts[i].keypoint_id;
    float root_x = scored_parts[i].x;
    float root_y = scored_parts[i].y;

    int tmp_idx =
        root_y * POSE_FEATURE_WIDTH * POSENET_KP_COUNT * 2 +
        root_x * POSENET_KP_COUNT * 2 + root_id * 2;
    FloatCoord root_image_coords = {
      root_x * module->pose_config.output_stride + module->offsets[tmp_idx + 1],
      root_y * module->pose_config.output_stride + module->offsets[tmp_idx]
    };

    // Check NMS for the current keypoint root/seed by comparing its location with those of detected poses
    if (gst_ml_do_nms_pose (&pose_results[0], pose_count, root_id,
            module->pose_config.nms_radius_squared, root_image_coords))
      continue;

    // Single-pose detection by starting from the current keypoint root/seed and searching adjacent keypoints
    GstPose instance_pose_results;
    gst_ml_decode_pose (root_score, root_id, root_image_coords,
        module->raw_heatmaps,
        module->offsets,
        module->displacements_bwd, module->displacements_fwd,
        &module->pose_config, &instance_pose_results);

    // Pose score calculation for a single pose instance
    float candidate_pose_instance_score =
        gst_ml_calculate_pose_instance_score (&pose_results[0], pose_count,
        &instance_pose_results, &module->pose_config);

    if (candidate_pose_instance_score > module->pose_config.min_pose_score) {
      pose_results[pose_count].pose_score = candidate_pose_instance_score;
      for (int j = 0; j < POSENET_KP_COUNT; j++) {
        pose_results[pose_count].keypoint[j] =
            instance_pose_results.keypoint[j];
      }
      ++pose_count;
    }

    if (pose_count >= module->pose_config.max_pose_detections) {
      break;
    }
  }

  GST_DEBUG ("Pose count: %d", pose_count);
  for (int i = 0; i < pose_count; ++i) {
    GST_DEBUG ("Pose: %2d, overall score = %.4f\n",
        i, pose_results[i].pose_score);
    for (int j = 0; j < POSENET_KP_COUNT; ++j) {
      GST_DEBUG ("Pose: %2d, Keypoint ID: %2d, score = %.4f, "
          "coords = [%.2f, %.2f]\n",
          i, j, pose_results[i].keypoint[j].score,
          pose_results[i].keypoint[j].x, pose_results[i].keypoint[j].y);
    }

    GstPose *pose = g_new (GstPose, 1);
    *pose = pose_results[i];
    *poses = g_list_append (*poses, pose);
  }

  return TRUE;
}
