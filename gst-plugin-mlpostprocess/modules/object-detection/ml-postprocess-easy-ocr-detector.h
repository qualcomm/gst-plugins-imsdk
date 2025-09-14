/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-proccess.h"
#include "qti-labels-parser.h"

#include <string>
#include <algorithm>
#include <opencv2/opencv.hpp>

using Poly = std::vector<cv::Point2f>;

struct DetectorArgs {
    float text_threshold = 0.70f;
    float link_threshold = 0.40f;
    float low_text       = 0.40f;
};

class Module : public IModule {
 public:
  Module(LogCallback cb);
  ~Module();

  std::string Caps() override;

  bool Configure(const std::string& labels_file,
                 const std::string& json_settings) override;

  bool Process(const Tensors& tensors, Dictionary& mlparams,
               std::any& output) override;

 private:
  void TransformDimensions(ObjectDetection &box, const Region& region);
  float IntersectionScore(ObjectDetection &l_box, ObjectDetection &r_box);
  void MergeOverlappingBoxes(ObjectDetection &l_box, ObjectDetections &boxes);
  void GetDetBoxes(const std::vector<float>& text_score_map,
                   const std::vector<float>& link_score_map, uint32_t width,
                   uint32_t height, const float text_threshold,
                   const float link_threshold, const float low_text,
                   std::vector<Poly>& boxes);
  std::vector<ObjectDetection> PolygonsToBoxes(const std::vector<Poly>& boxes);

  // Logging callback.
  LogCallback logger_;
  // Confidence threshold value.
  double       threshold_;
  DetectorArgs detector_args_;
  // Labels parser.
  LabelsParser labels_parser_;
};
