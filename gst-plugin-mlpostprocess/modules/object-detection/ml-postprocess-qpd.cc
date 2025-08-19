/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-qpd.h"

#include <cmath>

#define NMS_INTERSECTION_THRESHOLD 0.5F

#define BBOX_SIZE_THRESHOLD         400 // 20 x 20 pixels

#define DEFAULT_THRESHOLD 0.70

static const char* moduleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 120, 160, 3],
        [1, 120, 160, 12],
        [1, 120, 160, 34],
        [1, 120, 160, 17]
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
    LOG (logger_, kError, "Failed to parse labels");
    return false;
  }

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);
    if (!root || root->GetType() != JsonType::Object) return false;

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG (logger_, kLog, "Threshold: %f", threshold_);

    auto lmks = root->GetArray("landmarks");
    for (auto lmk : lmks) {
      std::map<uint32_t, std::string> landmarks_names;
      if (!lmk || lmk->GetType() != JsonType::Object) continue;
      auto lmks_names = lmk->GetArray("landmarks_names");

      for (auto lmk_name : lmks_names) {
        landmarks_names.insert({
              lmk_name->GetNumber("id"),
              lmk_name->GetString("name")});
      }
      landmarks_.insert({lmk->GetNumber("id"), landmarks_names});
    }
  }

  return true;
}

void Module::TransformDimensions(ObjectDetection &box,
                                 const Region& region) {

  box.top = (box.top - region.y) / region.height;
  box.bottom = (box.bottom - region.y) / region.height;
  box.left = (box.left - region.x) / region.width;
  box.right = (box.right - region.x) / region.width;
}

float Module::IntersectionScore(const ObjectDetection &l_box,
                                const ObjectDetection &r_box) {

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  float width = fmin (l_box.right, r_box.right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= fmax (l_box.left, r_box.left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  float height = fmin (l_box.bottom, r_box.bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= fmax (l_box.top, r_box.top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  float intersection = width * height;

  // Calculate the area of the 2 objects.
  float l_area = (l_box.right - l_box.left) * (l_box.bottom - l_box.top);
  float r_area = (r_box.right - r_box.left) * (r_box.bottom - r_box.top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

int32_t Module::NonMaxSuppression(const ObjectDetection &l_box,
                                  const ObjectDetections &boxes) {

  for (uint32_t idx = 0; idx < boxes.size();  idx++) {
    ObjectDetection r_box = boxes[idx];

    // If labels do not match, continue with next list entry.
    if (l_box.name != r_box.name)
      continue;

    double score = IntersectionScore (l_box, r_box);

    // If the score is below the threshold, continue with next list entry.
    if (score <= NMS_INTERSECTION_THRESHOLD)
      continue;

    // If confidence of current box is higher, remove the old entry.
    if (l_box.confidence > r_box.confidence)
      return idx;

    // If confidence of current box is lower, don't add it to the list.
    if (l_box.confidence <= r_box.confidence)
      return -2;
  }

  // If this point is reached then add current box to the list;
  return -1;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG (logger_, kError, "Unexpected predictions type!");
    return false;
  }

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  // Copy info
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  const float* scores = reinterpret_cast<const float *>(tensors[0].data);
  const float* bboxes = reinterpret_cast<const float *>(tensors[1].data);
  const float* landmarks = reinterpret_cast<const float *>(tensors[2].data);
  const float* lmkscores = reinterpret_cast<const float *>(tensors[3].data);

  uint32_t n_classes = tensors[0].dimensions[3];
  uint32_t n_landmarks = tensors[2].dimensions[3] / 2;
  uint32_t n_paxels = tensors[0].dimensions[1] * tensors[0].dimensions[2];
  uint32_t paxelsize = resolution.width / tensors[2].dimensions[2];

  for (uint32_t idx = 0; idx < (n_paxels * n_classes); idx++) {
    float bbox[4] = { 0, }, x = 0.0, y = 0.0;

    float confidence = scores[idx];

    if (confidence < threshold_)
      continue;

    uint32_t class_idx = idx % n_classes;

    if (labels_parser_.GetLabel(class_idx) == "unknown") {
      LOG (logger_, kDebug, "Unknown label, skipping this entry.");
      continue;
    }

    ObjectDetection entry;

    int32_t cx = (idx / n_classes) % tensors[2].dimensions[2];
    int32_t cy = (idx / n_classes) / tensors[2].dimensions[2];

    bbox[0] = bboxes[idx * 4];
    bbox[1] = bboxes[idx * 4 + 1];
    bbox[2] = bboxes[idx * 4 + 2];
    bbox[3] = bboxes[idx * 4 + 3];

    entry.left = (cx - bbox[0]) * paxelsize;
    entry.top = (cy - bbox[1]) * paxelsize;
    entry.right = (cx + bbox[2]) * paxelsize;
    entry.bottom = (cy + bbox[3]) * paxelsize;

    uint32_t size = (entry.right - entry.left) * (entry.bottom - entry.top);

    if (size < BBOX_SIZE_THRESHOLD)
      continue;

    entry.left = std::max(entry.left, static_cast<float>(region.x));
    entry.top = std::max(entry.top, static_cast<float>(region.y));
    entry.right =
        std::min(entry.right, static_cast<float>(region.x + region.width));
    entry.bottom =
        std::min(entry.bottom, static_cast<float>(region.y + region.height));

    LOG (logger_, kTrace, "Class: %u Confidence: %.2f Box[%f, %f, %f, %f]",
        class_idx, confidence, entry.top, entry.left, entry.bottom, entry.right);

    TransformDimensions (entry, region);

    entry.confidence = confidence * 100;
    entry.name = labels_parser_.GetLabel(class_idx);
    entry.color = labels_parser_.GetColor(class_idx);

    int32_t nms = NonMaxSuppression (entry, detections);

    if (nms == -2)
      continue;

    LOG (logger_, kLog, "Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        entry.name.c_str(), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    for (uint32_t num = 0; num < n_landmarks; ++num) {
      uint32_t id = (idx / n_classes) * n_landmarks + num;
      confidence = lmkscores[id];

      if (confidence < threshold_)
        continue;

      auto it = landmarks_.find(class_idx);
      const std::map<uint32_t, std::string>& names = it->second;

      if (names.size() == 0)
        continue;

      Keypoint lmk;
      lmk.name = names.find(num)->second;

      if (lmk.name.empty())
        continue;

      id = (idx / n_classes) * (n_landmarks * 2) + num;

      x = landmarks[id];
      y = landmarks[id + n_landmarks];

      lmk.x = (cx + x) * paxelsize;
      lmk.y = (cx + y) * paxelsize;

      lmk.x -= region.x + (entry.left * region.width);
      lmk.y -= region.y + (entry.top * region.height);

      lmk.x /= (entry.right - entry.left) * region.width;
      lmk.y /= (entry.bottom - entry.top) * region.height;

      lmk.x = std::min (std::max (lmk.x, 0.0f), 1.0f);
      lmk.y = std::min (std::max (lmk.y, 0.0f), 1.0f);

      if (!entry.landmarks.has_value()) {
        entry.landmarks.emplace();
      }

      entry.landmarks->emplace_back(lmk);

      LOG (logger_, kTrace, "Landmark: %d %s [%f %f] ",
          n_landmarks, lmk.name.c_str(), lmk.x, lmk.y);
    }

    detections.push_back(entry);
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
