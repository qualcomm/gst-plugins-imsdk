/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-process.h"
#include "qti-labels-parser.h"

#include <string>

typedef std::map<uint32_t, std::map<uint32_t, std::string>> LandmarksMap;

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
  float IntersectionScore(const ObjectDetection &l_box,
                          const ObjectDetection &r_box);
  int32_t NonMaxSuppression(const ObjectDetection &l_box,
                            const ObjectDetections &boxes);

  // Logging callback.
  LogCallback logger_;
  // Confidence threshold value.
  double       threshold_;
  // Landmarks map.
  LandmarksMap landmarks_;
  // Labels parser.
  LabelsParser labels_parser_;
};
