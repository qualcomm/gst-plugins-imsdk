/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "qti-ml-post-proccess.h"
#include "qti-labels-parser.h"

#include <string>
#include <vector>

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
  // Logging callback.
  LogCallback logger_;
  // Labels parser.
  LabelsParser labels_parser_;
  // Confidence threshold value.
  double       threshold_;

  struct GstFaceFeatures_ {
    std::vector<float> half;
    std::vector<float> whole;
  };

  struct GstFaceTemplate_ {
    std::string name;
    std::vector<float> liveliness;
    std::vector<GstFaceFeatures_> features;
  };

  std::vector<GstFaceTemplate_ *> face_database_;
  bool LoadFaceDatabase(const uint32_t idx, const std::string filename);
  void FaceRecognition (int32_t& person_id, float& confidence,
                   const Tensors& tensors, const uint32_t index);

  float CosineSimilarityScore (const float * data,
                         const std::vector<float> database,
                         const uint32_t n_entries);

  bool FaceHasLiveliness (const GstFaceTemplate_ * face,
                     const Tensors& tensors,
                     const uint32_t index);

  float CosineDistanceScore (const float * data,
                             std::vector<float> database, uint32_t n_entries);

  float AccessoryTensorScore (const Tensors& tensors, const uint32_t idx);
};
