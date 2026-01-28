/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-process.h"
#include "qti-labels-parser.h"

class Module : public IModule {
 public:
  Module(LogCallback cb);
  ~Module() = default;

  std::string Caps() override;

  bool Configure(const std::string& labels_file,
                 const std::string& json_settings) override;

  bool Process(const Tensors& tensors, Dictionary& mlparams,
               std::any& output) override;

 private:
  int32_t NonMaxSuppression(ObjectDetections &objects,
                            const ObjectDetection &l_object);

  float IntersectionScore(const ObjectDetection &l_box,
                          const ObjectDetection &r_box);

  // Logging callback.
  LogCallback  logger_;
  // Labels parser.
  LabelsParser labels_parser_;
  // Confidence threshold value.
  float        threshold_;
};
