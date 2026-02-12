/*
 * Copyright (c) 2021 Yifu Zhang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <fstream>
#include <string>

#include "STrack.h"
#include "utils_log.h"

#define QMOT_DEBUG_FUNCTIONS 0
#define QMOT_DEBUG_MESSAGE 0

struct ByteTrackerObject {
  // cv::Rect_<float> rect; // DG - remove opencv dependency
  float bounding_box[4] = { 0, 0, 0, 0 }; // x0, y0, x1, y1
  int   label;
  float prob;
};


class ByteTrackerConfig {
 public:
  ByteTrackerConfig() {
    frame_rate = 30;
    track_buffer = 30;
    wh_smooth_factor = 0.9f;

    // tracker confidence thresholds
    track_thresh = 0.5f; // high threshold of detection confidence for 1st round of matching
    high_thresh = 0.6f; // threshold of detection confidence for initialize new track
  }

  ~ByteTrackerConfig() {};

 public:
  int   frame_rate;
  int   track_buffer;

  float wh_smooth_factor;

  // tracker confidence thresholds
  float track_thresh; // high threshold of detection confidence for 1st round of matching
  float high_thresh; // threshold of detection confidence for initialize new track
};


class BYTETracker {
 public:
  BYTETracker(const ByteTrackerConfig &config);
  ~BYTETracker();

  vector<STrack> update(const vector<ByteTrackerObject>& objects);
  // Scalar get_color(int idx);

 private:
  vector<STrack*> joint_stracks(vector<STrack*> &tlista, vector<STrack> &tlistb);
  vector<STrack> joint_stracks(vector<STrack> &tlista, vector<STrack> &tlistb);
  vector<STrack*> joint_stracks(vector<STrack*> &tlista, vector<STrack*> &tlistb);

  vector<STrack> sub_stracks(vector<STrack> &tlista, vector<STrack> &tlistb);
  vector<STrack*> sub_stracks(vector<STrack*> &tlista, vector<STrack*> &tlistb);
  void remove_duplicate_stracks(vector<STrack> &resa, vector<STrack> &resb,
      vector<STrack> &stracksa, vector<STrack> &stracksb);

  void linear_assignment(vector<vector<float> > &cost_matrix,
      uint32_t cost_matrix_size, uint32_t cost_matrix_size_size, float thresh,
      vector<vector<uint32_t> > &matches, vector<uint32_t> &unmatched_a,
      vector<uint32_t> &unmatched_b);
  vector<vector<float> > iou_distance(vector<STrack*> &atracks,
      vector<STrack> &btracks, uint32_t &dist_size, uint32_t &dist_size_size);
  vector<vector<float> > iou_distance(vector<STrack*> &atracks,
      vector<STrack*> &btracks, uint32_t &dist_size, uint32_t &dist_size_size);
  vector<vector<float> > iou_distance(vector<STrack> &atracks,
      vector<STrack> &btracks);
  vector<vector<float> > ious(vector<vector<float> > &atlbrs,
      vector<vector<float> > &btlbrs);
  float compute_iou(float box1_x1, float box1_y1, float box1_x2, float box1_y2,
      float box2_x1, float box2_y1, float box2_x2, float box2_y2);
  float compute_intersection_over_self(float box1_x1, float box1_y1,
      float box1_x2, float box1_y2, float box2_x1, float box2_y1,
      float box2_x2, float box2_y2);

  double lapjv(const vector<vector<float> > &cost, vector<uint32_t> &rowsol,
      vector<uint32_t> &colsol, bool extend_cost = false,
      float cost_limit = LONG_MAX, bool return_cost = true);

  void print_statistics();

#if QMOT_DEBUG_FUNCTIONS
  bool save_file(const char *dstfile, const void *ptr, uint32_t size);
#endif /* QMOT_DEBUG_FUNCTIONS */

 private:
  float                           track_thresh;
  float                           high_thresh;
  float                           match_thresh;
  int                             frame_id;
  int                             max_time_lost;

  vector<STrack*>                 m_tracked_stracks;
  vector<STrack*>                 m_lost_stracks;
  vector<STrack*>                 m_removed_stracks;
  byte_kalman::KalmanFilter       kalman_filter;

  float                           track_wh_smooth_factor;
};
