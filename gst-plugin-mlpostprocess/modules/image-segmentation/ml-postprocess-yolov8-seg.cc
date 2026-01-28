/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-yolov8-seg.h"

#include <climits>
#include <cmath>
#include <algorithm>

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

static const float kNMSIntersectionTreshold = 0.5;
static const float kDefaultThreshold        = 0.70;

static const std::string kModuleCaps = R"(
{
  "type": "image-segmentation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [21, 42840], 4],
        [1, [21, 42840]],
        [1, [21, 42840], [1, 32]],
        [1, [21, 42840]],
        [1, [1, 32], [32, 2048], [32, 2048]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [21, 42840], 4],
        [1, [21, 42840]],
        [1, [21, 42840], [1, 32]],
        [1, [21, 42840]],
        [1, [32, 2048], [32, 2048], [1, 32]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [21, 42840], 4],
        [1, [21, 42840]],
        [1, [21, 42840], [1, 32]],
        [1, [32, 2048], [32, 2048], [1, 32]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [21, 42840], 4],
        [1, [21, 42840]],
        [1, [21, 42840], [1, 32]],
        [1, [1, 32], [32, 2048], [32, 2048]]
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

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG(logger_, kError, "Failed to parse labels");
    return false;
  }

  // No settings provided, nothing to do.
  if (json_settings.empty()) return true;

  auto root = JsonValue::Parse(json_settings);

  if (!root || root->GetType() != JsonType::Object) {
    LOG(logger_, kError, "Failed to parse settings!");
    return false;
  }

  threshold_ = root->GetNumber("confidence") / 100.0;
  LOG(logger_, kLog, "Threshold: %f", threshold_);

  return true;
}

float Module::IntersectionScore(const ObjectDetection &l_box,
                                const ObjectDetection &r_box) {

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  float width = std::min(l_box.right, r_box.right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= std::max(l_box.left, r_box.left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  float height = std::min(l_box.bottom, r_box.bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= std::max(l_box.top, r_box.top);

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

int32_t Module::NonMaxSuppression(ObjectDetections &objects,
                                  const ObjectDetection &l_object) {

  for (uint32_t idx = 0; idx < objects.size(); idx++) {
    ObjectDetection& r_object = objects[idx];

    // If labels do not match, continue with next list entry.
    if (l_object.name != r_object.name) continue;

    float score = IntersectionScore(l_object, r_object);

    // If the score is below the threshold, continue with next list entry.
    if (score <= kNMSIntersectionTreshold) continue;

    // If confidence of current box is higher, replace the old entry.
    if (l_object.confidence > r_object.confidence) {
      r_object = std::move(l_object);
      return idx;
    }

    // If confidence of current box is lower, don't add it to the list.
    if (l_object.confidence <= r_object.confidence) return -2;
  }

  // If this point is reached then add current box to the list;
  objects.emplace_back(std::move(l_object));

  return -1;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  auto& frame = std::any_cast<VideoFrame&>(output);
  auto& resolution = std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  if ((frame.width != resolution.width) && (frame.height != resolution.height)) {
    LOG(logger_, kError,
        "Mismatch between the model input tensor and video frame resolution!");
    return false;
  } else if ((frame.format != VideoFormat::kRGBA8888) &&
             (frame.format != VideoFormat::kRGBX8888)) {
    LOG(logger_, kError, "Unsupported video format!");
    return false;
  }

  uint32_t n_channels = tensors[2].dimensions[2];
  uint32_t n_paxels = tensors[2].dimensions[1];

  auto mlboxes = reinterpret_cast<const float*>(tensors[0].data);
  auto scores = reinterpret_cast<const float*>(tensors[1].data);
  // Tensor with predicted mask coefficients.
  auto masks = reinterpret_cast<const float*>(tensors[2].data);
  // Only the setup with 5 tensors has a tensor with class index.
  auto classes = (tensors.size() == 5) ?
      reinterpret_cast<const float*>(tensors[3].data) : nullptr;
  // Tensor with prototype masks.
  auto protos = reinterpret_cast<const float*>(tensors.back().data);

  std::vector<uint32_t> mask_matrix_indices;
  std::vector<ObjectDetection> objects;

  for (uint32_t idx = 0; idx < n_paxels; idx++) {
    // Discard results below the minimum confidence threshold.
    if (scores[idx] < threshold_) continue;

    ObjectDetection object(mlboxes[idx * 4], mlboxes[idx * 4 + 1],
                           mlboxes[idx * 4 + 2], mlboxes[idx * 4 + 3]);

    uint32_t class_idx = (classes != nullptr) ?
        static_cast<uint32_t>(classes[idx]) : (idx % labels_parser_.Size());

    object.confidence = scores[idx] * 100.0f;
    object.name = labels_parser_.GetLabel(class_idx);
    object.color = labels_parser_.GetColor(class_idx);

    LOG(logger_, kTrace, "Class: %s Confidence: %f Box[%f, %f, %f, %f]",
        object.name.c_str(), object.confidence, object.top, object.left,
        object.bottom, object.right);

    int32_t nms = NonMaxSuppression(objects, object);

    // Positive NMS result, remove the old entry.
    if (nms >= 0)
      mask_matrix_indices[nms] = idx * n_channels;

    // NMS result is -1 or positive, add the new entry to the results.
    if (nms == -1)
      mask_matrix_indices.emplace_back(idx * n_channels);
  }

  // When there are no objects detected then there is not point to continue.
  if (objects.empty()) return true;

  // Determine whether the layout of the protos tensor is NHWC or NCHW.
  auto minimum = std::min_element(tensors.back().dimensions.begin() + 1,
                                  tensors.back().dimensions.end());
  bool is_nchw = std::distance(tensors.back().dimensions.begin(), minimum) == 1;

  uint32_t mlwidth = tensors.back().dimensions[is_nchw ? 3 : 2];
  uint32_t mlheight = tensors.back().dimensions[is_nchw ? 2 : 1];

  // Number blocks with witch to do iteration over 5th tensor confidence scores.
  uint32_t n_blocks = is_nchw ? (mlwidth * mlheight) : 1;
  // Factor with which to multiply the index calculation below for 5th tensor.
  uint32_t p_idx_factor = is_nchw ? 1 : n_channels;

  // Scale factors for avoiding using division and instead use multiplication.
  float wscale = static_cast<float>(mlwidth) / resolution.width;
  float hscale = static_cast<float>(mlheight) / resolution.height;

  uint8_t *outdata = frame.planes[0].data;

  for (uint32_t idx = 0; idx < objects.size(); idx++) {
    ObjectDetection& object = objects[idx];
    uint32_t m_idx = mask_matrix_indices[idx];

    uint32_t left = std::lround(object.left);
    uint32_t top = std::lround(object.top);
    uint32_t right = std::lround(object.right);
    uint32_t bottom = std::lround(object.bottom);

    for (uint32_t row = top; row < bottom; row++) {
      for (uint32_t column = left; column < right; column++) {
        // Index of the current paxel in the 5th (protos) tensor.
        uint32_t p_idx = p_idx_factor *
            ((std::lround(row * hscale) * mlwidth) + std::lround(column * wscale));
        float confidence = 0.0f;

        // Perform matrix multiplication of confidence scores for current paxel.
        for (uint32_t num = 0; num < n_channels; num++) {
          confidence += masks[m_idx + num] * protos[p_idx + (num * n_blocks)];
        }

        // Apply a sigmoid function in order to normalize the confidence.
        confidence = 1.0f / (1.0f + expf(-confidence));
        // Discard results below the minimum confidence threshold.
        if (confidence < threshold_) continue;

        uint32_t outidx = (row * frame.planes[0].stride) + (column * 4);

        outdata[outidx] = EXTRACT_RED_COLOR(object.color.value());
        outdata[outidx + 1] = EXTRACT_GREEN_COLOR(object.color.value());
        outdata[outidx + 2] = EXTRACT_BLUE_COLOR(object.color.value());
        outdata[outidx + 3] = EXTRACT_ALPHA_COLOR(object.color.value());
      }
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {

  return new Module(logger);
}
