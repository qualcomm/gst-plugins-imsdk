/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-qfr-softmax.h"

#include <cmath>
#include <cxxabi.h>

#define DEFAULT_THRESHOLD       0.70
#define FACE_PID_SIZE           20

static const char* moduleCaps = R"(
{
  "type": "image-classification",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 512],
        [1, 32],
        [1, 2],
        [1, 2],
        [1, 2],
        [1, 2]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(DEFAULT_THRESHOLD) {

}
Module::~Module() {

}

std::string Module::Caps() {

  return std::string(moduleCaps);
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG(logger_, kError, "Failed to parse labels");
    return false;
  }

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);
    if (!root || root->GetType() != JsonType::Object) return false;

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG(logger_, kLog, "Threshold: %f", threshold_);

    int idx = 0;
    auto databases = root->GetArray("databases");
    for (const auto& database : databases) {
      if (!database || database->GetType() != JsonType::Object) continue;

      std::string filename = database->GetString("database");

      LoadFaceDatabase(idx, filename);
      idx++;
    }
  }

  return true;
}

bool Module::LoadFaceDatabase(const uint32_t idx,
                              const std::string filename) {

  std::ifstream file(filename, std::ios::binary);
  if (!file) {
      LOG(logger_, kError, "Failed to open file: %s", filename.c_str());
      return false;
  }

  uint32_t version = 0, n_features = 0, n_lvns_features = 0;
  uint32_t n_feature_templates = 0;

  file.read(reinterpret_cast<char*>(&version), sizeof(version));
  file.read(reinterpret_cast<char*>(&n_features), sizeof(n_features));
  file.read(reinterpret_cast<char*>(&n_lvns_features), sizeof(n_lvns_features));

  if (!file || n_features != 512 || n_lvns_features != 32) {
    LOG (logger_, kError, "Invalid header or feature dimensions!");
    return false;
  }

  face_database_.push_back(new GstFaceTemplate_());
  GstFaceTemplate_* face = face_database_[idx];

  char name_buffer[FACE_PID_SIZE] = {};
  file.read(name_buffer, FACE_PID_SIZE);
  face->name.assign(name_buffer, strnlen(name_buffer, FACE_PID_SIZE));

  face->liveliness.resize(n_lvns_features);

  file.read(reinterpret_cast<char*>(face->liveliness.data()), sizeof(float) * n_lvns_features);

  file.read(reinterpret_cast<char*>(&n_feature_templates), sizeof(n_feature_templates));

  if (face->name != labels_parser_.GetLabel(idx)) {
    LOG(logger_, kError, "Face name and label name do not match!");
    return false;
  }

  LOG (logger_, kTrace, "Face %u [%s] has %u feature templates",
      idx, face->name.c_str(), n_feature_templates);


  face->features.resize(n_feature_templates);

  for (uint32_t i = 0; i < n_feature_templates; ++i) {
    GstFaceFeatures_& features = face->features[i];
    features.half.resize(n_features);
    features.whole.resize(n_features);

    file.read(reinterpret_cast<char*>(features.half.data()), sizeof(float) * n_features);
    file.read(reinterpret_cast<char*>(features.whole.data()), sizeof(float) * n_features);

    if (!file) {
      LOG (logger_, kError, "Failed to read features for template %u", i);
      return false;
    }
  }

  return true;
}

float
Module::CosineSimilarityScore (const float * data,
                               const std::vector<float> database,
                               const uint32_t n_entries) {

  double v1_pow2_sum = 0.0, v2_pow2_sum = 0.0, product = 0.0;

  for (uint32_t idx = 0; idx < n_entries; ++idx) {
    double value = data[idx];

    v1_pow2_sum += value * value;
    v2_pow2_sum += database[idx] * database[idx];
    product += value * database[idx];
  }

  if ((v1_pow2_sum < 0.1) || (v2_pow2_sum < 0.1))
    return 0.0;

  return product / sqrt (v1_pow2_sum) / sqrt (v2_pow2_sum);
}

void
Module::FaceRecognition (int32_t& person_id, float& confidence,
                         const Tensors& tensors, const uint32_t index) {

  float maxscore = 0.0, maxconfidence = 0.0;
  int pid = -1;

  const float *data = reinterpret_cast<const float *>(tensors[index].data);
  uint32_t n_features = tensors[index].dimensions[1];

  for (uint32_t id = 0; id < face_database_.size(); id++, maxscore = 0.0) {
    Module::GstFaceTemplate_* face = face_database_[id];

    for (uint32_t num = 0; num < face->features.size(); num++) {
      Module::GstFaceFeatures_* features = &(face->features[num]);

      float score = CosineSimilarityScore(data, features->whole, n_features);

      if (score <= maxscore)
        continue;

      maxscore = score;
    }

    LOG (logger_, kTrace, "Face %u [%s] in database scored %f",
        id, face->name.c_str(), maxscore);

    if (maxscore < maxconfidence)
      continue;

    maxconfidence = maxscore;
    pid = id;
  }

  person_id = pid;
  confidence = maxconfidence;
}

float
Module::CosineDistanceScore (const float * data,
                             const std::vector<float> database,
                             const uint32_t n_entries) {

  float value = 0.0, v1_pow2_sum = 0.0, v2_pow2_sum = 0.0, product = 0.0;

  for (uint32_t idx = 0; idx < n_entries; ++idx) {
    value = data[idx];

    v1_pow2_sum += value * value;
    v2_pow2_sum += database[idx] * database[idx];
    product += value * database[idx];
  }

  if ((v1_pow2_sum < 0.1) || (v2_pow2_sum < 0.1))
    return 0.0;

  value = product / (sqrt (v1_pow2_sum) * sqrt (v2_pow2_sum));
  return sqrtf (2 * (1 - value));
}

bool
Module::FaceHasLiveliness (const GstFaceTemplate_ * face,
                           const Tensors& tensors,
                           const uint32_t idx) {

  const float * data = reinterpret_cast<const float*>(tensors[idx].data);
  uint32_t n_features = tensors[idx].dimensions[1];

  double score = CosineDistanceScore(data, face->liveliness, n_features);

  LOG (logger_, kTrace, "Face %s has liveliness score %f",
      face->name.c_str(), score);

  return (score >= threshold_) ? true : false;
}

float
Module::AccessoryTensorScore (const Tensors& tensors, const uint32_t idx) {

  const float * data = reinterpret_cast<const float *>(tensors[idx].data);
  uint32_t n_values = tensors[idx].dimensions[1];

  if (n_values != 2)
    return 0.0;

  float value = 0.0f;
  float sum = 0.0f;

  for (uint32_t idx = 0; idx < n_values; ++idx) {
    value = data[idx];
    sum += exp(value);
  }

  float score = data[1];
  score = (exp (score) / sum);

  return score;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(ImageClassifications)) {
    LOG (logger_, kError, "Unexpected predictions type!");
    return false;
  }

  ImageClassifications& classifications =
      std::any_cast<ImageClassifications&>(output);

  classifications.emplace_back();

  ImageClassification& entry = classifications[0];


  entry.name = "UNKNOWN";
  entry.color = 0xFF0000FF;

  int32_t pid = -1;
  float confidence = 0.0f;

  FaceRecognition (pid, confidence, tensors, 0);

  entry.confidence = (pid != -1) ? confidence : (100.0 - confidence);
  entry.confidence *= 100.0;

  if ((pid == -1) || (confidence < threshold_))
    return true;

  GstFaceTemplate_* face = face_database_[pid];

  entry.name = labels_parser_.GetLabel(pid);
  entry.color = labels_parser_.GetColor(pid);

  LOG (logger_, kTrace, "Recognized face %d [%s] in the database", pid, face->name.c_str());

  float score = AccessoryTensorScore(tensors, 2);
  bool has_open_eyes = (score >=  threshold_) ? true : false;

  LOG (logger_, kTrace, "Face %s has open eyes score %f", face->name.c_str(), score);

  score = AccessoryTensorScore(tensors, 3);
  bool has_glasses = (score >= threshold_) ? true : false;

  LOG (logger_, kTrace, "Face %s has glasses score %f", face->name.c_str(), score);

  score = AccessoryTensorScore(tensors, 4);
  bool has_mask = (score >= threshold_) ? true : false;

  LOG (logger_, kTrace, "Face %s has mask score %f", face->name.c_str(), score);

  score = AccessoryTensorScore(tensors, 5);
  bool has_sunglasses = (score >= threshold_) ? true : false;

  LOG (logger_, kTrace, "Face %s has sunglasses score %f", face->name.c_str(), score);

  bool has_lvns = false;
  if (!has_mask)
    has_lvns = FaceHasLiveliness (face, tensors, 1);

  LOG (logger_, kTrace, "Face %s, Lively: %s, Open Eyes: %s, Mask: %s, Glasses: %s, "
      "Sunglasses: %s", entry.name.c_str(), has_lvns ? "YES" : "NO",
      has_open_eyes ? "YES" : "NO", has_mask ? "YES" : "NO",
      has_glasses ? "YES" : "NO", has_sunglasses ? "YES" : "NO");

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
