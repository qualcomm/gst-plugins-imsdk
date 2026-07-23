/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "auto-framing-alg.h"

#include <vector>

class FilterBase {
 public:
  virtual ~FilterBase() {};
  virtual VideoRectangleF process(VideoRectangleF rect) = 0;
};

class ThresholdFilter : public FilterBase {
 public:
  ThresholdFilter (float pos_threshold, float size_threshold);

  VideoRectangleF process(VideoRectangleF rect) override;

  void SetPosThreshold(float pos_threshold);
  void SetSizeThreshold(float size_threshold);

 private:
  VideoRectangleF curr_rect_;

  bool first_sample_ = true;

  // Parameter for the position threshold of the crop when AFR is converged
  float pos_threshold_;
  // Parameter for the dimensions threshold of the crop when AFR is converged
  float size_threshold_;
};

class AverageFilter : public FilterBase {
 public:
  AverageFilter (int32_t size);

  void SetSize(int32_t size);

  VideoRectangleF process(VideoRectangleF rect) override;

 private:

  float ApplyFilter (float value, std::vector<float> &filter_list);

  int32_t filter_average_size_;

  std::vector<float> list_x;
  std::vector<float> list_y;
  std::vector<float> list_w;
  std::vector<float> list_h;
};

class MedianFilter : public FilterBase {
 public:
  MedianFilter (int32_t size);

  void SetSize(int32_t size);

  VideoRectangleF process(VideoRectangleF rect) override;

 private:
  float ApplyMedianFilter (float value,
                           std::vector<float> &filter_list,
                           std::vector<float> &short_avrg_list);

  int32_t filter_size_;

  // Used for applying a median filter
  std::vector<float> list_x_;
  std::vector<float> list_y_;
  std::vector<float> list_w_;
  std::vector<float> list_h_;

  std::vector<float> list_short_average_x_;
  std::vector<float> list_short_average_y_;
  std::vector<float> list_short_average_w_;
  std::vector<float> list_short_average_h_;
};
