/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Changes from Qualcomm Technologies, Inc. are provided under the following license:
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "ml-postprocess-ssd-mobilenet.h"

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
        [1, 10, 4], [1, 10], [1, 10], [1]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 10], [1, 10, 4], [1, 10], [1], [1, 10]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 100], [1], [1, 100, 4], [1, 100]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 25, 4], [1, 25], [1, 25], [1]
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

  const float *bboxes = nullptr, *classes = nullptr;
  const float *scores = nullptr, *n_boxes = nullptr;

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

  if (tensors.size() == 4) {
    if (tensors[3].dimensions.size() == 1) {
      bboxes = reinterpret_cast<const float *>(tensors[0].data);
      classes = reinterpret_cast<const float *>(tensors[1].data);
      scores = reinterpret_cast<const float *>(tensors[2].data);
      n_boxes = reinterpret_cast<const float *>(tensors[3].data);
    }

    if (tensors[3].dimensions.size() == 2) {
      bboxes = reinterpret_cast<const float *>(tensors[2].data);
      classes = reinterpret_cast<const float *>(tensors[0].data);
      scores = reinterpret_cast<const float *>(tensors[3].data);
      n_boxes = reinterpret_cast<const float *>(tensors[1].data);
    }
  } else if (tensors.size() == 5) {
    bboxes = reinterpret_cast<const float *>(tensors[1].data);
    classes = reinterpret_cast<const float *>(tensors[4].data);
    scores = reinterpret_cast<const float *>(tensors[0].data);
    n_boxes = reinterpret_cast<const float *>(tensors[3].data);
  }

  uint32_t n_entries = n_boxes[0];

  for (uint32_t idx = 0; idx < n_entries; idx++) {
    ObjectDetection entry;

    // Discard results with confidence below the set threshold.
    if (scores[idx] < threshold_)
      continue;

    entry.top = bboxes[(idx * 4)] * resolution.height;
    entry.left = bboxes[(idx * 4) + 1] * resolution.width;
    entry.bottom = bboxes[(idx * 4) + 2] * resolution.height;
    entry.right = bboxes[(idx * 4) + 3] * resolution.width;

    // Adjust bounding box dimensions with extracted source tensor region.
    TransformDimensions (entry, region);

    if ((entry.top > 1.0) || (entry.left > 1.0) ||
        (entry.bottom > 1.0) || (entry.right > 1.0))
      continue;

    entry.confidence = scores[idx] * 100;
    entry.name = labels_parser_.GetLabel(classes[idx]);
    entry.color = labels_parser_.GetColor(classes[idx]);

    // Non-Max Suppression (NMS) algorithm.
    int32_t nms = NonMaxSuppression (entry, detections);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2))
      continue;

    // If the NMS result is above -1 remove the entry with the nms index.
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
