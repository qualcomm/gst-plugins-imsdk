/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-mediapipe-pose.h"

#include <cmath>

static const float kDefaultThreshold = 0.75f;
static const float kNMSIntersectionThreshold = 0.3f;
static const float kDetectBoxScale = 1.5f;

static const std::vector<uint32_t> kAnchorSizes = {8, 16};
static const std::vector<uint32_t> kAnchorsPerCell = {2, 6};

const std::string kModuleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 896, 12],
        [1, 896, 1]
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

std::string Module::Caps() {
  return kModuleCaps;
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG (logger_, kError, "Failed to parse labels file");
    return false;
  }

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);
    if (!root || root->GetType() != JsonType::Object)
      return false;

    threshold_ = root->GetNumber("confidence") / 100.0;
    LOG (logger_, kLog, "Threshold: %f", threshold_);
  }

  return true;
}

void Module::TransformDimensions(ObjectDetection &box,
                                 const Region& region) {

  box.top = (box.top - region.y) / region.height;
  box.bottom = (box.bottom - region.y) / region.height;
  box.left = (box.left - region.x) / region.width;
  box.right = (box.right - region.x) / region.width;

  if (!box.landmarks)
    return;

  for (auto& kp : *box.landmarks) {
    kp.x = (kp.x - region.x) / region.width;
    kp.y = (kp.y - region.y) / region.height;
  }
}

float Module::IntersectionScore(const ObjectDetection &l_box,
                                const ObjectDetection &r_box) {

  float width = std::min(l_box.right, r_box.right);
  width -= std::max(l_box.left, r_box.left);

  if (width <= 0.0F)
    return 0.0F;

  float height = std::min(l_box.bottom, r_box.bottom);
  height -= std::max(l_box.top, r_box.top);

  if (height <= 0.0F)
    return 0.0F;

  float intersection = width * height;

  float l_area = (l_box.right - l_box.left) * (l_box.bottom - l_box.top);
  float r_area = (r_box.right - r_box.left) * (r_box.bottom - r_box.top);

  return intersection / (l_area + r_area - intersection);
}

int32_t Module::NonMaxSuppression(const ObjectDetection &l_box,
                                  const ObjectDetections &boxes) {

  for (uint32_t idx = 0; idx < boxes.size();  idx++) {
    ObjectDetection r_box = boxes[idx];

    if (l_box.name != r_box.name)
      continue;

    float score = IntersectionScore (l_box, r_box);

    if (score <= kNMSIntersectionThreshold)
      continue;

    if (l_box.confidence > r_box.confidence)
      return idx;

    if (l_box.confidence <= r_box.confidence)
      return -2;
  }

  return -1;
}


bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG (logger_, kError, "Unexpected output type!");
    return false;
  }

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  if (tensors.size() != 2) {
    LOG (logger_, kError, "Expected 2 tensors for MediaPipe pose detection, got %zu",
        tensors.size());
    return false;
  }

  const float* bboxes = reinterpret_cast<const float*>(tensors[0].data);
  const float* scores = reinterpret_cast<const float*>(tensors[1].data);

  uint32_t n_layers = tensors[0].dimensions[2];

  // Landmarks is number of layers minus 4(bbox) and divided by 2 for X/Y.
  uint32_t n_landmarks = (n_layers - 4) / 2;

  if (anchors_.empty()) {
    for (size_t i = 0; i < kAnchorSizes.size(); i++) {
      for (uint32_t y = 0; y < resolution.height / kAnchorSizes[i]; y++) {
        for (uint32_t x = 0; x < resolution.width / kAnchorSizes[i]; x++) {
          float cx = (x + 0.5f) * kAnchorSizes[i];
          float cy = (y + 0.5f) * kAnchorSizes[i];

          for (uint32_t a = 0; a < kAnchorsPerCell[i]; ++a) {
            anchors_.push_back({cx, cy});
          }
        }
      }
    }
  }

  for (uint32_t idx = 0; idx < anchors_.size(); idx++) {
    float confidence = 1.0f / (1.0f + std::exp(-scores[idx]));

    if (confidence < threshold_)
      continue;

    // Extract bbox and keypoints from tensor data
    const uint32_t num = idx * n_layers;

    std::vector<float> keypoints_x(n_landmarks);
    std::vector<float> keypoints_y(n_landmarks);

    for (uint32_t kp_idx = 0; kp_idx < n_landmarks; ++kp_idx) {
      keypoints_x[kp_idx] = bboxes[num + 4 + (kp_idx * 2)];
      keypoints_y[kp_idx] = bboxes[num + 4 + (kp_idx * 2) + 1];
    }

    // Decode keypoints using absolute anchor coordinates
    for (size_t kp_idx = 0; kp_idx < keypoints_x.size(); kp_idx++) {
      keypoints_x[kp_idx] = anchors_[idx].cx + keypoints_x[kp_idx];
      keypoints_y[kp_idx] = anchors_[idx].cy + keypoints_y[kp_idx];
    }

    // Enlarge bounding box based on keypoints
    float center_x = anchors_[idx].cx + bboxes[num];
    float center_y = anchors_[idx].cy + bboxes[num + 1];
    float width = bboxes[num + 2];
    float height = bboxes[num + 3];

    if (!keypoints_x.empty() && !keypoints_y.empty()) {
      auto min_x_it = std::min_element(keypoints_x.begin(), keypoints_x.end());
      auto max_x_it = std::max_element(keypoints_x.begin(), keypoints_x.end());
      auto min_y_it = std::min_element(keypoints_y.begin(), keypoints_y.end());
      auto max_y_it = std::max_element(keypoints_y.begin(), keypoints_y.end());

      width = std::max(width, *max_x_it - *min_x_it);
      height = std::max(height, *max_y_it - *min_y_it);
      center_x = (*min_x_it + *max_x_it) * 0.5f;
      center_y = (*min_y_it + *max_y_it) * 0.5f;
    } else {
      LOG(logger_, kWarning, "Empty keypoints vector, using org bbox dimensions");
    }

    float size = std::max(width, height) * kDetectBoxScale;

    ObjectDetection entry;
    entry.left = center_x - size / 2;
    entry.top = center_y - size / 2;
    entry.right = center_x + size / 2;
    entry.bottom = center_y + size / 2;
    entry.confidence = confidence * 100.0f;
    entry.name = labels_parser_.GetLabel(0);
    entry.color = labels_parser_.GetColor(0);

    // Create landmarks
    std::vector<Keypoint> landmarks(n_landmarks);

    for (uint32_t lm_idx = 0; lm_idx < n_landmarks; lm_idx++) {
      landmarks[lm_idx].x = keypoints_x[lm_idx];
      landmarks[lm_idx].y = keypoints_y[lm_idx];
      landmarks[lm_idx].name = "keypoint_" + std::to_string(lm_idx);
    }

    entry.landmarks = landmarks;

    TransformDimensions(entry, region);

    int32_t nms = NonMaxSuppression(entry, detections);

    if (nms == (-2))
      continue;

    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    detections.push_back(entry);
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
