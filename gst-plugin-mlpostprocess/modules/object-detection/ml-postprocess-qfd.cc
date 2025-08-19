/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-qfd.h"

#include <cmath>

#define NMS_INTERSECTION_THRESHOLD  0.5F

#define BBOX_SIZE_THRESHOLD         400 // 20 x 20 pixels

#define DEFAULT_THRESHOLD           0.70

#define MAX_FLOAT std::numeric_limits<float>::max()

static const char* moduleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["UINT8", "FLOAT32"],
      "dimensions": [
        [1, 60, 80, 1],
        [1, 60, 80, 1],
        [1, 60, 80, 10],
        [1, 60, 80, 4]
      ]
    },
    {
      "format": ["UINT8", "FLOAT32"],
      "dimensions": [
        [1, 120, 160, 1],
        [1, 120, 160, 10],
        [1, 120, 160, 4]
      ]
    },
    {
      "format": ["UINT8", "FLOAT32"],
      "dimensions": [
        [1, 60, 80, 4],
        [1, 60, 80, 10],
        [1, 60, 80, 1]
      ]
    },
    {
      "format": ["UINT8", "FLOAT32"],
      "dimensions": [
        [1, 60, 80, 1],
        [1, 60, 80, 4],
        [1, 60, 80, 10]
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

  uint32_t scores_idx = 0, landmarks_idx = 0, bboxes_idx = 0, class_idx = 0;
  float confidence = 0.0f;
  int32_t nms = -1;

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

  const float *hm_pool = nullptr;

  if (tensors.size() == 4) {
    scores_idx = 0;
    hm_pool = reinterpret_cast<const float*>(tensors[1].data);
    landmarks_idx = 2;
    bboxes_idx = 3;
  } else if (tensors.size() == 3) {
    if (tensors[0].dimensions[3] == 4) {
      scores_idx = 2;
      bboxes_idx = 0;
      landmarks_idx = 1;
    } else if (tensors[1].dimensions[3] == 4) {
      scores_idx = 0;
      bboxes_idx = 1;
      landmarks_idx = 2;
    } else {
      scores_idx = 0;
      bboxes_idx = 2;
      landmarks_idx = 1;
    }
  }

  const float* scores =
      reinterpret_cast<const float *>(tensors[scores_idx].data);
  const float* bboxes =
      reinterpret_cast<const float *>(tensors[bboxes_idx].data);
  const float* landmarks =
     reinterpret_cast<const float *>(tensors[landmarks_idx].data);

  uint32_t n_classes = tensors[scores_idx].dimensions[3];
  uint32_t n_landmarks = tensors[landmarks_idx].dimensions[3] / 2;

  uint32_t n_paxels = tensors[0].dimensions[1] * tensors[0].dimensions[2];
  uint32_t paxelsize = resolution.width / tensors[0].dimensions[2];

  for (uint32_t idx = 0; idx < n_paxels; idx += n_classes) {
    ObjectDetection entry;
    float left = MAX_FLOAT , right = 0.0, top = MAX_FLOAT , bottom = 0.0;
    float x = 0.0, y = 0.0, tx = 0.0, ty = 0.0, width = 0.0, height = 0.0;
    float bbox_x = 0.0, bbox_y = 0.0, bbox_w = 0.0, bbox_h = 0.0;

    confidence = scores[idx];

    if (hm_pool != NULL) {
      float score = hm_pool[idx];

      if (confidence != score)
        continue;
    }

    if (confidence < threshold_)
      continue;

    class_idx = idx % n_classes;

    int32_t cx = (idx / n_classes) % tensors[0].dimensions[2];
    int32_t cy = (idx / n_classes) / tensors[0].dimensions[2];

    bbox_x = bboxes[idx * 4];
    bbox_y = bboxes[idx * 4] + 1;
    bbox_w = bboxes[idx * 4] + 2;
    bbox_h = bboxes[idx * 4] + 3;

    entry.left = (cx - bbox_x) * paxelsize;
    entry.top = (cy - bbox_y) * paxelsize;
    entry.right = (cx + bbox_w) * paxelsize;
    entry.bottom = (cy + bbox_h) * paxelsize;

    uint32_t size = (entry.right - entry.left) * (entry.bottom - entry.top);

    if (size < BBOX_SIZE_THRESHOLD)
      continue;

    for (uint32_t num = 0; num < n_landmarks; ++num) {
      float ld_x = 0, ld_y = 0;

      uint32_t id = (idx / n_classes) * (n_landmarks * 2) + num;

      ld_x = landmarks[id];
      ld_y = landmarks[id + n_landmarks];

      x = (cx + ld_x) * paxelsize;
      y = (cy + ld_y) * paxelsize;

      x -= region.x + entry.left;
      y -= region.y + entry.top;

      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x);
      bottom = std::max(bottom, y);
    }

    tx = left + ((right - left) / 2) - ((entry.right - entry.left) / 2);
    ty = top + ((bottom - top) / 2) - ((entry.bottom - entry.top) / 2);

    entry.left += tx;
    entry.top += ty;
    entry.right += tx;
    entry.bottom += ty;

    LOG (logger_, kTrace, "Class: %u Confidence: %.2f Box[%f, %f, %f, %f]",
        class_idx, confidence, entry.top, entry.left, entry.bottom, entry.right);

    // Adjust bounding box dimensions in order to make it a square with margins.
    width = entry.right - entry.left;
    height = entry.bottom - entry.top;

    if (width > height) {
      entry.top -= ((width - height) / 2);
      entry.bottom = entry.top + width;
    } else if (width < height) {
      entry.left -= ((height - width) / 2);
      entry.right = entry.left + height;
    }

    LOG (logger_, kTrace, "Class: %u Confidence: %.2f Adjusted Box[%f, %f, %f, %f]",
        class_idx, confidence, entry.top, entry.left, entry.bottom, entry.right);

    TransformDimensions (entry, region);

    entry.confidence = confidence * 100;
    entry.name = labels_parser_.GetLabel(class_idx);
    entry.color = labels_parser_.GetColor(class_idx);

    nms = NonMaxSuppression (entry, detections);

    if (nms == (-2))
      continue;

    LOG (logger_, kLog, "Label: %s Confidence: %.2f Box[%f, %f, %f, %f]",
        entry.name.c_str(), entry.confidence, entry.top, entry.left,
        entry.bottom, entry.right);

    if (nms >= 0)
      detections.erase(detections.begin() + nms);

    detections.push_back(entry);
  }
  // sort entries
  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
