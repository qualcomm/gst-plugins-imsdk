/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-process.h"
#include "qti-labels-parser.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <cmath>
#include <algorithm>

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
  // Logging callback.
  LogCallback logger_;
  // Confidence threshold value.
  double       threshold_;
  // Labels parser.
  LabelsParser labels_parser_;
};
