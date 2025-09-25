/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-proccess.h"

#include <string>

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
  uint32_t GetTensorTypeSize (const TensorType type);
  bool ValidateTensorSize (const Tensor& l_tensor, const Tensor& r_tensor);

  // Logging callback.
  LogCallback  logger_;
};
