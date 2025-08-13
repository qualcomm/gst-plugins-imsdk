/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-proccess.h"
#include "qti-labels-parser.h"

#include <string>

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

private:
  void KeypointTransformCoordinates (Keypoint& keypoint,
                                     const Region& region);
  bool LoadConnections(const std::vector<JsonValue::Ptr>& nodes);

  // Logging callback.
  LogCallback logger_;
  // Confidence threshold value.
  double       threshold_;
  LabelsParser labels_parser_;

  std::vector<KeypointLinkIds> connections_;
};
