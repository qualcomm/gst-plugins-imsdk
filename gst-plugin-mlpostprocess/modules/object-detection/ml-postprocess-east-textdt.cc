/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-east-textdt.h"

#include <cmath>

#define NMS_INTERSECTION_THRESHOLD 0.5F

#define DEFAULT_THRESHOLD 0.70

static const char* moduleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [8, 480], [8, 480], [1, 5]],
        [1, [8, 480], [8, 480], [1, 5]]
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

  uint32_t num = 0, idx = 0;
  int nms = -1;

  if (output.type() != typeid(ObjectDetections)) {
    LOG (logger_, kError, "Unexpected output type!");
    return false;
  }

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  // Copy info
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t n_rows = tensors[0].dimensions[1];
  uint32_t n_cols = tensors[0].dimensions[2];

  const float *scores = nullptr;
  const float *geometry = nullptr;

  if (tensors[0].dimensions[3] == 1) {
    scores = reinterpret_cast<const float *>(tensors[0].data);
    geometry = reinterpret_cast<const float *>(tensors[1].data);
  } else {
    scores = reinterpret_cast<const float *>(tensors[1].data);
    geometry = reinterpret_cast<const float *>(tensors[0].data);
  }

  for (uint32_t y = 0; y < n_rows; y++) {
    for (uint32_t x = 0; x < n_cols; x++, num++, idx +=5) {
      ObjectDetection entry;

      // Discard results below the minimum score threshold.
      float confidence = scores[idx];

      if (confidence < threshold_)
        continue;

      // Extracting the derive rotated boxes surround text
      float x0 = geometry[idx];
      float x1 = geometry[idx + 1];
      float x2 = geometry[idx + 2];
      float x3 = geometry[idx + 3];

      // Extracting the rotation angle then computing the sine and cosine
      float angle = geometry[idx + 4];

      float cos_angle = cos (angle);
      float sin_angle = sin (angle);

      // Using the geo volume to get the width and height bounding box
      float h = x0 + x2;
      float w = x1 + x3;

      // Compute coordinates of text prediction bounding box
      entry.right = (x * 4 + (cos_angle * x1) + (sin_angle * x2));
      entry.bottom = (y * 4 - (sin_angle * x1) + (cos_angle * x2));
      entry.left = (entry.right - w);
      entry.top = (entry.bottom - h);

      TransformDimensions (entry, region);

      // Keep dimensions within the region.
      entry.top = std::max(entry.top, (float)region.y);
      entry.left = std::max(entry.left, (float)region.x);
      entry.bottom = std::min(entry.bottom, (float)(region.y + region.height));
      entry.right = std::min(entry.right, (float)(region.x + region.width));

      entry.confidence = scores[num] * 100;
      entry.name = labels_parser_.GetLabel(0);
      entry.color = labels_parser_.GetColor(0);

      nms = NonMaxSuppression (entry, detections);

      // If the NMS result is -2 don't add the prediction to the list.
      if (nms == (-2))
        continue;

      LOG (logger_, kTrace, "Label: %s. Confidence: %.2f Box[%.2f, %.2f, %.2f, %.2f]",
          entry.name.c_str(), entry.confidence, entry.top,
          entry.left, entry.bottom, entry.right);

      if (nms >= 0)
        detections.erase(detections.begin() + nms);

      detections.push_back(entry);
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
