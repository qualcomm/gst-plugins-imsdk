/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-yolov5.h"

#include <cmath>

// Layer index at which the object score resides.
#define SCORE_IDX              4

// Layer index from which the class labels begin.
#define CLASSES_IDX            5

// Non-maximum Suppression (NMS) threshold (50%), corresponding to 2/3 overlap.
#define NMS_INTERSECTION_THRESHOLD 0.5F

#define DEFAULT_THRESHOLD 0.70

// Bounding box weights for each of the 3 tensors used for normalization.
static const uint32_t weights[3] = { 8, 16, 32 };
// Bounding box anchor values for each of the 3 tensors used for normalization.
static const uint32_t anchors[3][3][2] = {
    { {10,  13}, {16,   30}, {33,   23} },
    { {30,  61}, {62,   45}, {59,  119} },
    { {116, 90}, {156, 198}, {373, 326} },
};

static const char* moduleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [1, 136], [1, 136], [18, 3018]],
        [1, [1, 136], [1, 136], [18, 3018]],
        [1, [1, 136], [1, 136], [18, 3018]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 3, [1, 136], [1, 136], [6, 85]],
        [1, 3, [1, 136], [1, 136], [6, 85]],
        [1, 3, [1, 136], [1, 136], [6, 85]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [21, 72828], [6, 85]]
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

    float score = IntersectionScore (l_box, r_box);

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

int32_t Module::TensorCompareValues(const float *data,
    const uint32_t& l_idx, const uint32_t& r_idx) {

  return ((float*)data)[l_idx] > ((float*)data)[r_idx] ? 1 :
      ((float*)data)[l_idx] < ((float*)data)[r_idx] ? -1 : 0;
}


void Module::ParseMonoblockFrame(const Tensors& tensors, Dictionary& mlparams,
                                 std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG (logger_, kError, "Unexpected output type!");
    return;
  }

  int nms = -1;
  float bbox[4] = { 0, };
  ObjectDetections& detections =
    std::any_cast<ObjectDetections&>(output);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  const float* data = reinterpret_cast<const float*>(tensors[0].data);

  uint32_t n_paxels = tensors[0].dimensions[1];
  uint32_t n_layers = tensors[0].dimensions[2];

  uint32_t idx = 0;
  for (uint32_t num = 0; num < n_paxels; num++, idx += n_layers) {
    ObjectDetection entry;

    float score = data[idx + SCORE_IDX];

    if (score < threshold_)
      continue;

    uint32_t id = idx + CLASSES_IDX;

    for (uint32_t m = (idx + CLASSES_IDX + 1);  m < (idx + n_layers); m++)
      id = (TensorCompareValues(data, m, id) > 0) ? m : id;

    float confidence = data[id];

    confidence *= score;

    if (confidence < threshold_)
      continue;

    bbox[0] = data[idx];
    bbox[1] = data[idx + 1];
    bbox[2] = data[idx + 2];
    bbox[3] = data[idx + 3];

    entry.top = (bbox[1] - (bbox[3] / 2)) * resolution.height;
    entry.left = (bbox[0] - (bbox[2] / 2)) * resolution.width;
    entry.bottom = (bbox[1] + (bbox[3] / 2)) * resolution.height;
    entry.right = (bbox[0] + (bbox[2] / 2)) * resolution.width;

    TransformDimensions(entry, region);

    // Keep dimensions within the region.
    entry.top = std::max(entry.top, (float)region.y);
    entry.left = std::max(entry.left, (float)region.x);
    entry.bottom = std::min(entry.bottom, (float)(region.y + region.height));
    entry.right = std::min(entry.right, (float)(region.x + region.width));

    entry.confidence = confidence * 100.0f;
    entry.name = labels_parser_.GetLabel(id - (idx + CLASSES_IDX));
    entry.color = labels_parser_.GetColor(id - (idx + CLASSES_IDX));

    nms = NonMaxSuppression (entry, detections);

    if (nms == (-2))
      continue;

    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    detections.push_back(entry);
  }
}

void Module::ParseTripleblockFrame(const Tensors& tensors,
                                   Dictionary& mlparams,
                                   std::any& output) {

  uint32_t w_idx = 0;
  uint width = 0, height = 0, n_layers = 0, n_anchors = 0;
  float bbox[4] = { 0, };
  int nms = -1;

  if (output.type() != typeid(ObjectDetections)) {
    LOG (logger_, kError, "Unexpected predictions type!");
    return;
  }

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  for (uint32_t idx = 0; idx < tensors.size(); idx++) {
    const float *data =
        reinterpret_cast<const float *>(tensors[idx].data);

    uint32_t num = 0;

    if (tensors[idx].dimensions.size() == 5) {
      n_anchors = tensors[idx].dimensions[1];
      height = tensors[idx].dimensions[2];
      width = tensors[idx].dimensions[3];
      n_layers = tensors[idx].dimensions[4];
    } else {
      n_anchors = 3;
      height = tensors[idx].dimensions[1];
      width = tensors[idx].dimensions[2];
      n_layers = tensors[idx].dimensions[3] / n_anchors;
    }

    uint32_t n_paxels = width * height;

    uint32_t paxelsize = resolution.width / width;

    for (w_idx = 0; w_idx < 3; w_idx++)
      if (weights[w_idx] == paxelsize) break;

    for (uint32_t pxl_idx = 0; pxl_idx < n_paxels; pxl_idx++) {

      for (uint32_t anchor = 0; anchor < n_anchors; anchor++, num += n_layers) {
        ObjectDetection entry;

        float score = data[num + SCORE_IDX];

        if (score < threshold_)
          continue;

        uint32_t id = num + CLASSES_IDX;

        for (uint32_t m = (num + CLASSES_IDX + 1); m < (num + n_layers); m++)
          id = (TensorCompareValues(data, m, id) > 0 ? m : id);

        uint32_t class_idx = id - (num + CLASSES_IDX);

        float confidence = data[id];

        if (confidence < threshold_)
          continue;

        // Apply a sigmoid function in order to normalize the confidence.
        confidence = 1 / (1 + expf (- confidence));
        // Normalize the end confidence with the object score value.
        confidence *= 1 / (1 + expf (- score));

        // Aquire the bounding box parameters.
        bbox[0] = data[num];
        bbox[1] = data[num + 1];
        bbox[2] = data[num + 2];
        bbox[3] = data[num + 3];

        bbox[0] = 1 / (1 + expf (- bbox[0]));
        bbox[1] = 1 / (1 + expf (- bbox[1]));
        bbox[2] = 1 / (1 + expf (- bbox[2]));
        bbox[3] = 1 / (1 + expf (- bbox[3]));

        uint32_t x = pxl_idx % width;
        uint32_t y = pxl_idx / width;

        // Special calculations for the bounding box parameters.
        bbox[0] = (bbox[0] * 2 - 0.5F + x) * paxelsize;
        bbox[1] = (bbox[1] * 2 - 0.5F + y) * paxelsize;
        bbox[2] = pow ((bbox[2] * 2), 2) * anchors[w_idx][anchor][0];
        bbox[3] = pow ((bbox[3] * 2), 2) * anchors[w_idx][anchor][1];

        entry.top = bbox[1] - (bbox[3] / 2);
        entry.left = bbox[0] - (bbox[2] / 2);
        entry.bottom = bbox[1] + (bbox[3] / 2);
        entry.right = bbox[0] + (bbox[2] / 2);

        LOG (logger_, kTrace, "Class: %u Confidence: %.2f Box[%f, %f, %f, %f]",
            class_idx, confidence, entry.top, entry.left, entry.bottom, entry.right);

        // Keep dimensions within the region.
        entry.top = std::max(entry.top, (float)region.y);
        entry.left = std::max(entry.left, (float)region.x);
        entry.bottom = std::min(entry.bottom, (float)(region.y + region.height));
        entry.right = std::min(entry.right, (float)(region.x + region.width));

        TransformDimensions (entry, region);

        entry.name = labels_parser_.GetLabel(id - (num + CLASSES_IDX));
        entry.color = labels_parser_.GetColor(id - (num + CLASSES_IDX));
        entry.confidence = confidence * 100.0f;

        nms = NonMaxSuppression (entry, detections);

        if (nms == (-2))
          continue;

        LOG (logger_, kTrace, "TRIPLEBLOCK Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
            entry.name.c_str(), entry.confidence, entry.top,
            entry.left, entry.bottom, entry.right);

        if (nms >= 0)
          detections.erase(detections.begin() + nms);

        detections.push_back(entry);
      }
    }
  }
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

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  LOG (logger_, kDebug, "Module Process - %ld", tensors.size());

  if (tensors.size() == 3) {
    ParseTripleblockFrame (tensors, mlparams, output);
  } else if (tensors.size() == 1) {
    ParseMonoblockFrame (tensors, mlparams, output);
  } else {
    LOG (logger_, kError, "Ml frame with unsupported post-processing procedure!");
    return false;
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
