/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-proccess.h"
#include "qti-labels-parser.h"

#include <string>

class Module : public IModule {
 public:
  Module(LogCallback cb);
  ~Module() {};

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
  int32_t TensorCompareValues(const float *data,
                              const uint32_t& l_idx,const uint32_t& r_idx);
  void ParseMonoblockFrame (const Tensors& tensors, Dictionary& mlparams,
                            std::any& output);
  void ParseDualblockFrame (const Tensors& tensors, Dictionary& mlparams,
                            std::any& output);
  void ParseTripleblockFrame(const Tensors& tensors, Dictionary& mlparams,
                             std::any& output);

  // Logging callback.
  LogCallback logger_;
  // Confidence threshold value.
  double       threshold_;
  // Labels parser.
  LabelsParser labels_parser_;
};
