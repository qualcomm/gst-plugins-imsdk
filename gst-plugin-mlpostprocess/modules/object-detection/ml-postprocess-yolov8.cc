/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-yolov8.h"

#include <cmath>

// Non-maximum Suppression (NMS) threshold (50%), corresponding to 2/3 overlap.
#define NMS_INTERSECTION_THRESHOLD 0.5F

#define DEFAULT_THRESHOLD 0.70


/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const char* kModuleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [21, 42840], 4],
        [1, [21, 42840]],
        [1, [21, 42840]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 4, [21, 42840]],
        [1, [1, 1001], [21, 42840]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [5, 1005], [21, 42840]]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(DEFAULT_THRESHOLD) {

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

int32_t Module::TensorCompareValues(const float *data,
                                    const uint32_t& l_idx,
                                    const uint32_t& r_idx) {

  return ((float*)data)[l_idx] > ((float*)data)[r_idx] ? 1 :
      ((float*)data)[l_idx] < ((float*)data)[r_idx] ? -1 : 0;
}

void Module::ParseMonoblockFrame (const Tensors& tensors, Dictionary& mlparams,
                                  std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG(logger_, kError, "Unexpected output type!");
    return;
  }

  ObjectDetections& detections =
    std::any_cast<ObjectDetections&>(output);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t n_paxels = tensors[0].dimensions[2];

  // We subtract 4 because the last 4 are the bbox coordinates.
  uint32_t n_classes = tensors[0].dimensions[1] - 4;

  const float* bboxes = reinterpret_cast<const float*>(tensors[0].data);
  const float* scores = &bboxes[4 * n_paxels];

  for (uint32_t idx = 0; idx < n_paxels; idx++) {
    ObjectDetection entry;

    uint32_t id = idx;

    for (uint32_t num = (idx + n_paxels);
        num < (n_classes * n_paxels); num += n_paxels)
      id = (TensorCompareValues(scores, num, id) > 0) ? num : id;

    uint32_t class_idx = id / n_paxels;

    double confidence = scores[id];

    if (confidence < threshold_)
      continue;

    double cx = bboxes[idx];
    double cy = bboxes[idx + n_paxels];
    double w  = bboxes[idx + 2 * n_paxels];
    double h  = bboxes[idx + 3 * n_paxels];

    LOG(logger_, kLog, "Class: %u Confidence: %.2f CX x CY[%f, %f] W x H: [%f, %f]",
        class_idx, confidence, cx, cy, w, h);

    entry.top =  cy - h / 2.0f;
    entry.left = cx - w / 2.0f;
    entry.bottom = entry.top + h;
    entry.right = entry.left + w;

    entry.left = std::max(entry.left, (float)region.x);
    entry.top = std::max(entry.top, (float)region.y);
    entry.right = std::min(entry.right, (float)(region.x + region.width));
    entry.bottom = std::min(entry.bottom, (float)(region.y + region.height));

    LOG(logger_, kLog, "Class: %u Confidence: %.2f Box[%f, %f, %f, %f]", class_idx,
        confidence, entry.top, entry.left, entry.bottom, entry.right);

    TransformDimensions(entry, region);

    entry.confidence = confidence * 100.0f;
    entry.name = labels_parser_.GetLabel(class_idx);
    entry.color = labels_parser_.GetColor(class_idx);

    int32_t nms = NonMaxSuppression (entry, detections);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    LOG(logger_, kTrace, "Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        entry.name.c_str(), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    detections.push_back(entry);
  }
}

void Module::ParseDualblockFrame (const Tensors& tensors, Dictionary& mlparams,
                                  std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG(logger_, kError, "Unexpected output type!");
    return;
  }

  ObjectDetections& detections =
    std::any_cast<ObjectDetections&>(output);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t n_paxels = tensors[0].dimensions[2];
  uint32_t n_classes = tensors[1].dimensions[1];

  const float* bboxes = reinterpret_cast<const float *>(tensors[0].data);
  const float* scores = reinterpret_cast<const float *>(tensors[1].data);

  for (uint32_t idx = 0; idx < n_paxels; idx++) {
    ObjectDetection entry;

    uint32_t id = idx;

    for (uint32_t num = (idx + n_paxels); num < (n_classes * n_paxels); num += n_paxels)
      id = (TensorCompareValues(scores, num, id) > 0) ? num : id;

    uint32_t class_idx = idx / n_paxels;

    double confidence = scores[id];

    if (confidence < threshold_)
      continue;

    double cx = bboxes[id + 0 * n_paxels];
    double cy = bboxes[id + 1 * n_paxels];
    double w  = bboxes[id + 2 * n_paxels];
    double h  = bboxes[id + 3 * n_paxels];

    LOG(logger_, kLog, "Class: %u Confidence: %.2f CX x CY[%f, %f] W x H: [%f, %f]",
        class_idx, confidence, cx, cy, w, h);

    entry.top =  cy - h / 2.0f;
    entry.left = cx - w / 2.0f;
    entry.bottom = entry.top + h;
    entry.right = entry.left + w;

    entry.left = std::max(entry.left, (float)region.x);
    entry.top = std::max(entry.top, (float)region.y);
    entry.right = std::min(entry.right, (float)(region.x + region.width));
    entry.bottom = std::min(entry.bottom, (float)(region.y + region.height));

    LOG(logger_, kLog, "Class: %u Confidence: %.2f Box[%f, %f, %f, %f]", class_idx,
        confidence, entry.top, entry.left, entry.bottom,
        entry.right);

    TransformDimensions(entry, region);

    entry.confidence = confidence * 100.0f;
    entry.name = labels_parser_.GetLabel(class_idx);
    entry.color = labels_parser_.GetColor(class_idx);

    int32_t nms = NonMaxSuppression (entry, detections);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    LOG(logger_, kTrace, "Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        entry.name.c_str(), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    detections.push_back(entry);
  }
}

void Module::ParseTripleblockFrame(const Tensors& tensors,
                                   Dictionary& mlparams,
                                   std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG(logger_, kError, "Unexpected output type!");
    return;
  }

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t n_paxels = tensors[0].dimensions[1];

  const float *bboxes = static_cast<const float*>(tensors[0].data);
  const float *scores = static_cast<const float*>(tensors[1].data);
  const float *classes = static_cast<const float*>(tensors[2].data);

  for (uint32_t idx = 0; idx < n_paxels; idx++) {
    double confidence = scores[idx];
    uint32_t class_idx = classes[idx];

    // Discard results below the minimum score threshold.
    if (confidence < threshold_)
      continue;

    ObjectDetection entry;
    entry.left = bboxes[idx * 4];
    entry.top = bboxes[idx * 4 + 1];
    entry.right = bboxes[idx * 4 + 2];
    entry.bottom = bboxes[idx * 4 + 3];

    LOG(logger_, kLog, "Class: %u Confidence: %.2f Box[%f, %f, %f, %f]", class_idx,
        confidence, entry.top, entry.left, entry.bottom, entry.right);

    // Adjust bounding box dimensions with extracted source tensor region.
    TransformDimensions (entry, region);

    // Discard results with out of region coordinates.
    if ((entry.top > 1.0)   || (entry.left > 1.0) ||
        (entry.bottom > 1.0) || (entry.right > 1.0) ||
        (entry.top < 0.0)    || (entry.left < 0.0) ||
        (entry.bottom < 0.0) || (entry.right < 0.0))
      continue;

    entry.confidence = confidence * 100.0F;
    entry.name = labels_parser_.GetLabel(class_idx);
    entry.color = labels_parser_.GetColor(class_idx);

    // Non-Max Suppression (NMS) algorithm.
    int32_t nms = NonMaxSuppression (entry, detections);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    LOG(logger_, kTrace, "Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        entry.name.c_str(), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    detections.push_back(entry);
  }
}

std::string Module::Caps() {

  return std::string(kModuleCaps);
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

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG(logger_, kLog, "Threshold: %f", threshold_);
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (tensors.size() == 3) {
    ParseTripleblockFrame (tensors, mlparams, output);
  } else if (tensors.size() == 2) {
    ParseDualblockFrame (tensors, mlparams, output);
  } else if (tensors.size() == 1) {
    ParseMonoblockFrame (tensors, mlparams, output);
  } else {
    LOG(logger_, kError, "ML frame with unsupported post-processing procedure!");
    return false;
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
