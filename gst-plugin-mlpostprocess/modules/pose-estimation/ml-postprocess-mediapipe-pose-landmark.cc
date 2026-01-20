/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-mediapipe-pose-landmark.h"

#include <cfloat>
#include <utility>
#include <algorithm>

static const float kDefaultThreshold = 0.50f;

const std::string kModuleCaps = R"(
{
  "type": "pose-estimation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1],
        [1, 25, 4]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(kDefaultThreshold) {

}

Module::~Module() {

}

void Module::KeypointTransformCoordinates(Keypoint& keypoint,
                                          const Region& region) {

  keypoint.x = (keypoint.x - region.x) / region.width;
  keypoint.y = (keypoint.y - region.y) / region.height;
}

std::string Module::Caps() {

  return kModuleCaps;
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG (logger_, kError, "Failed to parse labels");
    return false;
  }

  if (json_settings.empty()) {
    LOG(logger_, kError, "No JSON settings provided - connections required for pose estimation");
    return false;
  }

  auto root = JsonValue::Parse(json_settings);
  if (!root) {
    LOG(logger_, kError, "Failed to parse JSON settings");
    return false;
  }

  if (root->GetType() != JsonType::Object) {
    LOG(logger_, kError, "Invalid JSON settings format");
    return false;
  }

  const auto& obj = root->GetObject();

  if (obj.count("confidence") > 0) {
    threshold_ = root->GetNumber("confidence") / 100.0;
    LOG(logger_, kLog, "Threshold: %f", threshold_);
  }

  if (obj.count("connections") > 0) {
    auto nodes = root->GetArray("connections");
    if (!LoadConnections(nodes)) return false;
  } else {
    LOG(logger_, kError, "No connections provided in JSON settings - required for pose estimation");
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

    connections_.push_back({label_id, con_id});
  }
  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(PoseEstimations)) {
    LOG (logger_, kError, "Unexpected predictions type!");
    return false;
  }

  PoseEstimations& estimations =
      std::any_cast<PoseEstimations&>(output);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  const float* scores = reinterpret_cast<const float*>(tensors[0].data);
  const float* landmarks = reinterpret_cast<const float*>(tensors[1].data);

  if (scores[0] < threshold_) {
    LOG (logger_, kTrace, "Pose score %.3f below threshold %.3f",
         scores[0], threshold_);
    return true;
  }

  uint32_t n_keypoints = tensors[1].dimensions[1];
  uint32_t n_dimensions = tensors[1].dimensions[2];

  PoseEstimation entry;

  entry.keypoints.resize(n_keypoints);
  entry.confidence = scores[0] * 100.0f;

  for (uint32_t idx = 0; idx < n_keypoints; idx++) {
    uint32_t num = idx * n_dimensions;

    Keypoint& kp = entry.keypoints[idx];

    kp.x = landmarks[num] * resolution.width;
    kp.y = landmarks[num + 1] * resolution.height;
    kp.confidence = landmarks[num + 3] * 100.0f;

    kp.name = labels_parser_.GetLabel(idx);
    kp.color = labels_parser_.GetColor(idx);

    KeypointTransformCoordinates(kp, region);

    // clamp key-point to avoid point going out of region
    kp.x = std::clamp((double)kp.x, (double)0.0f, (double)1.0f);
    kp.y = std::clamp((double)kp.y, (double)0.0f, (double)1.0f);

    LOG (logger_, kDebug, "Keypoint: %u [%.2f x %.2f], confidence %.2f",
         idx, kp.x, kp.y, kp.confidence);
  }

  entry.links = KeypointLinks{};

  for (uint32_t num = 0; num < connections_.size(); num++) {
    KeypointLinkIds& lk = connections_[num];

    if (lk.s_kp_id < n_keypoints && lk.d_kp_id < n_keypoints) {
      entry.links->push_back(
          KeypointLink(entry.keypoints[lk.s_kp_id],
              entry.keypoints[lk.d_kp_id]));
    }
  }

  estimations.push_back(entry);

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
