/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "auto-framing-filters.h"

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

#define AFR_FILTER_HISTORY_FRAMES_AVERAGE 3
#define AFR_FILTER_MIN_THRESHOLD 5

static float
average_list (const std::vector<float>::iterator first,
  const std::vector<float>::iterator last) {
  float max = *std::max_element(first, last, [](float a, float b) {
    return std::abs(a) < std::abs(b);
  });

  max = (max == 0) ? 1.0f : abs(max);

  double sum = 0.0f;
  for (auto ptr = first; ptr < last; ptr++) {
    sum += *ptr / max;
  }
  sum /= (last - first);

  return sum * max;
}

ThresholdFilter::ThresholdFilter (float pos_threshold, float size_threshold)
    : pos_threshold_(pos_threshold),
      size_threshold_(size_threshold) {}

void ThresholdFilter::SetPosThreshold (float pos_threshold) {
   pos_threshold_ = pos_threshold;
}

void ThresholdFilter::SetSizeThreshold (float size_threshold) {
  size_threshold_ = size_threshold;
}

VideoRectangleF ThresholdFilter::process (VideoRectangleF rect) {
  if (first_sample_) {
    curr_rect_ = rect;
    first_sample_ = false;
    return curr_rect_;
  }

  // If there is a movement already starded, use the size_moving_threshold
  float pos_threshold_x = curr_rect_.w * pos_threshold_ / 100;
  float pos_threshold_y = curr_rect_.h * pos_threshold_ / 100;
  float size_threshold_w = curr_rect_.w * size_threshold_ / 100;
  float size_threshold_h = curr_rect_.h * size_threshold_ / 100;

  // Protection if some one set too small margin. The issue is that margin
  // is percentage of crop window size. if crop window become too small
  // margin could be 0. For example 3% of 20x20 window is 0.6 i.e. int 0.
  pos_threshold_x = pos_threshold_x < AFR_FILTER_MIN_THRESHOLD ?
      AFR_FILTER_MIN_THRESHOLD : pos_threshold_x;
  pos_threshold_y = pos_threshold_y < AFR_FILTER_MIN_THRESHOLD ?
      AFR_FILTER_MIN_THRESHOLD : pos_threshold_y;
  size_threshold_w = size_threshold_w < AFR_FILTER_MIN_THRESHOLD ?
      AFR_FILTER_MIN_THRESHOLD : size_threshold_w;
  size_threshold_h = size_threshold_h < AFR_FILTER_MIN_THRESHOLD ?
      AFR_FILTER_MIN_THRESHOLD : size_threshold_h;

  if (abs (curr_rect_.x - rect.x) > pos_threshold_x ||
      abs (curr_rect_.y - rect.y) > pos_threshold_y) {
    curr_rect_.x = rect.x;
    curr_rect_.y = rect.y;
  }

  if (abs (curr_rect_.w - rect.w) > size_threshold_w ||
      abs (curr_rect_.h - rect.h) > size_threshold_h) {
    curr_rect_.w = rect.w;
    curr_rect_.h = rect.h;
  }
  return curr_rect_;
}


AverageFilter::AverageFilter (int32_t size)
    : filter_average_size_(size) {}

void AverageFilter::SetSize (int32_t size) {
  filter_average_size_ = size;
}

float AverageFilter::ApplyFilter (float value, std::vector<float> &filter_list) {
  auto filter_size = filter_list.size ();

  if (filter_size == 0) {
    filter_list.push_back(value);
    return value;
  }

  if (filter_size == filter_average_size_) {
    filter_list.erase (filter_list.begin ());
  }

  filter_list.push_back(value);

  return average_list (filter_list.begin (), filter_list.end ());
}

VideoRectangleF AverageFilter::process (VideoRectangleF rect) {
  VideoRectangleF output;
  output.x = ApplyFilter (rect.x, list_x);
  output.y = ApplyFilter (rect.y, list_y);
  output.w = ApplyFilter (rect.w, list_w);
  output.h = ApplyFilter (rect.h, list_h);
  return output;
}

MedianFilter::MedianFilter (int32_t size)
    : filter_size_(size) {
}

void MedianFilter::SetSize(int32_t size) {
  filter_size_ = size;
}

VideoRectangleF MedianFilter::process (VideoRectangleF rect) {
  VideoRectangleF output;
  output.x = ApplyMedianFilter (rect.x, list_x_, list_short_average_x_);
  output.y = ApplyMedianFilter (rect.y, list_y_, list_short_average_y_);
  output.w = ApplyMedianFilter (rect.w, list_w_, list_short_average_w_);
  output.h = ApplyMedianFilter (rect.h, list_h_, list_short_average_h_);
  return output;
}

float MedianFilter::ApplyMedianFilter (float value,
  std::vector<float> &filter_list, std::vector<float> &short_avrg_list) {
  // Add to the average last frame queue
  if (short_avrg_list.size () == AFR_FILTER_HISTORY_FRAMES_AVERAGE) {
    short_avrg_list.erase (short_avrg_list.begin ());
  }

  short_avrg_list.push_back (value);

  auto avrg_value = average_list (short_avrg_list.begin (), short_avrg_list.end ());

  auto filter_size = filter_list.size ();

  if (filter_size == 0) {
    filter_list.push_back(value);
    return value;
  }

  int32_t median_pos = filter_size / 2;
  auto median_value = filter_list[median_pos];

  if (filter_size == filter_size_) {
    std::vector<float>::iterator value_to_delete;
    if (avrg_value < median_value) {
      value_to_delete = filter_list.end () - 1;
    } else {
      value_to_delete = filter_list.begin ();
    }
    filter_list.erase (value_to_delete);
  }

  filter_list.push_back (value);

  std::sort (filter_list.begin (), filter_list.end ());

  median_pos = filter_size / 2;

  median_value = filter_list[median_pos];

  return median_value;
}
