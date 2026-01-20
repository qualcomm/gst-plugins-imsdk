/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-process.h"
#include "qti-labels-parser.h"

#include <string>

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
  uint64_t ScaleUint64Safe(const uint64_t val,
                           const int32_t num, const int32_t denom);
  void ParseSegmentationFrame(const Tensors& tensors, Dictionary& mlparams,
                              std::any& output, uint32_t proto_tensor_idx);
  std::vector<uint32_t> GenerateMaskFromProtos(const Tensors& tensors,
                                               std::vector<ObjectDetection>& bboxes,
                                               std::vector<uint32_t>& mask_matrix_indices,
                                               uint32_t proto_tensor_idx);
  void ParseBoundingBoxes(const Tensors& tensors,
                          std::vector<ObjectDetection>& bboxes,
                          std::vector<uint32_t>& mask_matrix_indices);
  int32_t NonMaxSuppression(const ObjectDetection &l_box,
                            ObjectDetections &boxes);

  float IntersectionScore(const ObjectDetection &l_box,
                          const ObjectDetection &r_box);

  void  MlBoxRelativeTranslation(ObjectDetection * box,
                                  const int width, const int height);

  // Logging callback.
  LogCallback  logger_;
  // Labels parser.
  LabelsParser labels_parser_;
  double       threshold_;
  uint32_t     source_width_;
  uint32_t     source_height_;
};
