/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-process.h"
#include "qti-labels-parser.h"

#include <string>

struct RootPoint {
  uint32_t id;
  float    x;
  float    y;
  float    confidence;

  RootPoint()
      : id(0), x(0), y(0), confidence(0.0) {};

  RootPoint(uint32_t id, float x, float y, float confidence)
      : id(id), x(x), y(y), confidence(confidence) {};
};

struct KeypointLinkIds {
  uint32_t s_kp_id;
  uint32_t d_kp_id;

  KeypointLinkIds()
      : s_kp_id(0), d_kp_id(0) {};

  KeypointLinkIds(uint32_t s_kp_id, uint32_t d_kp_id)
      : s_kp_id(s_kp_id), d_kp_id(d_kp_id) {};
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

  uint64_t ScaleUint64Safe(const uint64_t val, const int32_t num,
                           const int32_t denom);

 private:
  // Logging callback.
  LogCallback logger_;
  // Confidence threshold value.
  double       threshold_;

  // Labels parser.
  LabelsParser labels_parser_;

  std::vector<KeypointLinkIds> links_;
  std::vector<KeypointLinkIds> connections_;
};
