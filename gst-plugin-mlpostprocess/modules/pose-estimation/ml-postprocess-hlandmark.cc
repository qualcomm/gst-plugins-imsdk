/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-hlandmark.h"

#include <cfloat>

#define DEFAULT_THRESHOLD 0.70

static const char* moduleCaps = R"(
{
  "type": "pose-estimation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 63],
        [1, 1],
        [1, 1],
        [1, 63]
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

void Module::KeypointTransformCoordinates(Keypoint& keypoint,
                                          const Region& region) {

  keypoint.x = (keypoint.x - region.x) / region.width;
  keypoint.y = (keypoint.y - region.y) / region.height;
}

std::string Module::Caps() {

  return std::string(moduleCaps);
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG (logger_, kError, "Failed to parse labels");
    return false;
  }

  if (json_settings.empty()) {
    LOG (logger_, kWarning,
        "Failed to load connections! No JSON Settings provided!");
    return true;
  }

  auto root = JsonValue::Parse(json_settings);
  if (!root || root->GetType() != JsonType::Object) {
    LOG (logger_, kError, "Failed extract type from settings!");
    return false;
  }

  threshold_ = root->GetNumber("confidence");
  threshold_ /= 100.0;
  LOG (logger_, kLog, "Threshold: %f", threshold_);

  auto nodes = root->GetArray("connections");

  if (!LoadConnections(nodes)) {
    LOG (logger_, kError, "Failed load connections from settings!");
    return false;
  }

  return true;
}

bool Module::LoadConnections(const std::vector<JsonValue::Ptr>& nodes) {

  for (const auto& node : nodes) {
    if (!node || node->GetType() != JsonType::Object)
      continue;

    const auto& obj = node->GetObject();

    if (obj.count("id") == 0 || obj.count("connection") == 0)
      continue;

    uint32_t label_id = static_cast<uint32_t>(node->GetNumber("id"));
    uint32_t con_id = static_cast<uint32_t>(node->GetNumber("connection"));

    connections_.emplace_back(label_id, con_id);
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(PoseEstimations)) {
    LOG (logger_, kError, "Unexpected predictions type!");
    return false;
  }

  if (tensors[0].dimensions[1] != tensors[3].dimensions[1]) {
    LOG (logger_, kError, "Second dimension of first and third tensor must be "
        "equal: %u != %u", tensors[0].dimensions[1], tensors[3].dimensions[1]);
    return false;
  }

  PoseEstimations& estimations = std::any_cast<PoseEstimations&>(output);
  Region& region = std::any_cast<Region&>(mlparams["input-tensor-region"]);

  const float* coordinates = reinterpret_cast<const float*>(tensors[0].data);
  const float* scores = reinterpret_cast<const float*>(tensors[1].data);

  // There are 3 coordinates per point - x, y, z
  uint32_t n_keypoints = tensors[0].dimensions[1] / 3;
  float confidence = scores[0];

  if (confidence < threshold_)
    return true;

  PoseEstimation entry;
  entry.confidence = confidence;

  entry.keypoints.resize(n_keypoints);

  for (uint32_t idx = 0; idx < n_keypoints; idx++) {
    Keypoint& kp = entry.keypoints[idx];

    kp.x = coordinates[3 * idx];
    kp.y = coordinates[3 * idx + 1];
    kp.name = labels_parser_.GetLabel(idx);
    kp.color = labels_parser_.GetColor(idx);
    kp.confidence = confidence * 100;

    KeypointTransformCoordinates(kp, region);

    kp.x = std::min (std::max(kp.x, (float)0), (float)1);
    kp.y = std::min (std::max(kp.y, (float)0), (float)1);
  }

  KeypointLinks links;
  for (const auto &lk : connections_) {
    links.emplace_back (entry.keypoints[lk.s_kp_id], entry.keypoints[lk.d_kp_id]);
  }

  entry.links = std::move(links);

  estimations.emplace_back(entry);

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
