/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-palmd.h"

#include <cmath>

static const float kNmsIntersectionThreshold = 0.5f;
static const float kDefaultThreshold = 0.7f;

static const std::vector<float> kAnchorSizes = {8, 16, 16, 16};

/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const std::string kModuleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 2016, 18],
        [1, 2016, 1]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(kDefaultThreshold) {

}


std::string Module::Caps() {

  return kModuleCaps;
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
  float width = fmin(l_box.right, r_box.right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= fmax(l_box.left, r_box.left);

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
    if (score <= kNmsIntersectionThreshold)
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

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG(logger_, kError, "Failed to parse labels");
    return false;
  }

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);

    if (!root || root->GetType() != JsonType::Object)
      return false;

    threshold_ = root->GetNumber("confidence") / 100.0;
    LOG(logger_, kLog, "Threshold: %f", threshold_);

    auto lmks = root->GetArray("landmarks");
    for (auto lmk : lmks) {
      if (!lmk || lmk->GetType() != JsonType::Object)
        continue;

      std::map<uint32_t, std::string> lmk_names;

      for (auto name : lmk->GetArray("landmarks_names")) {
        lmk_names.emplace(name->GetNumber("id"), name->GetString("name"));
      }

      landmarks_.emplace(lmk->GetNumber("id"), lmk_names);
    }
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  if (anchors_.empty()) {
    for (size_t i = 0; i < kAnchorSizes.size(); i++) {
      for (uint32_t y = 0; y < resolution.height / kAnchorSizes[i]; y++) {
        for (uint32_t x = 0; x < resolution.width / kAnchorSizes[i]; x++) {
          float cx = (x + 0.5f) * kAnchorSizes[i];
          float cy = (y + 0.5f) * kAnchorSizes[i];
          anchors_.push_back({cx, cy});
        }
      }
    }
  }

  const float* bboxes = reinterpret_cast<const float*>(tensors[0].data);
  const float* scores = reinterpret_cast<const float*>(tensors[1].data);
  uint32_t paxels = tensors[0].dimensions[1];
  uint32_t layers = tensors[0].dimensions[2];
  uint32_t n_landmarks = (layers - 4) / 2;

  for (uint32_t idx = 0; idx < paxels; idx++) {
    ObjectDetection entry;

    float confidence = 1 / (1 + expf(- scores[idx]));

    if (confidence < threshold_)
      continue;

    float cx = bboxes[idx * layers] + anchors_[idx / 2][0];
    float cy = bboxes[(idx * layers) + 1] + anchors_[idx / 2][1];
    float w = bboxes[(idx * layers) + 2];
    float h = bboxes[(idx * layers) + 3];

    entry.top =  cy - h / 2.0f;
    entry.left = cx - w / 2.0f;
    entry.bottom = entry.top + h;
    entry.right = entry.left + w;

    entry.left = std::max(entry.left, (float)region.x);
    entry.top = std::max(entry.top, (float)region.y);
    entry.right = std::min(entry.right, (float) (region.x + region.width));
    entry.bottom = std::min(entry.bottom, (float) (region.y + region.height));

    TransformDimensions(entry, region);

    entry.confidence = confidence * 100;
    entry.name = labels_parser_.GetLabel(0);
    entry.color = labels_parser_.GetColor(0);

    // Non-Max Suppression (NMS) algorithm.
    int32_t nms = NonMaxSuppression(entry, detections);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    for (uint32_t num = 0; num < n_landmarks; num++) {
      const std::map<uint32_t, std::string>& names = landmarks_.at(0);

      Keypoint lmk;
      lmk.name = names.at(num);

      lmk.x = bboxes[(idx * layers) + 4 + (2 * num)] + anchors_[idx / 2][0];
      lmk.y = bboxes[(idx * layers) + 4 + (2 * num) + 1] + anchors_[idx / 2][1];

      lmk.x = (lmk.x - (cx - w / 2.0f)) / w;
      lmk.y = (lmk.y - (cy - h / 2.0f)) / h;

      lmk.x = std::min(std::max(lmk.x, 0.0f), 1.0f);
      lmk.y = std::min(std::max(lmk.y, 0.0f), 1.0f);

      if (!entry.landmarks.has_value())
        entry.landmarks.emplace();

      entry.landmarks->emplace_back(lmk);

      LOG(logger_, kTrace, "Landmark: %d %s [%f %f] ",
          num, lmk.name.c_str(), lmk.x, lmk.y);
    }

    detections.push_back(entry);
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
