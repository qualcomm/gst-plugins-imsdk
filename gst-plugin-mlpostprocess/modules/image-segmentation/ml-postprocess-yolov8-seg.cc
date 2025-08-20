/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-yolov8-seg.h"

#include <climits>
#include <cmath>

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

#define NMS_INTERSECTION_THRESHOLD 0.5F

#define DEFAULT_THRESHOLD 0.5

static const char* moduleCaps = R"(
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
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(DEFAULT_THRESHOLD) {

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

  return true;
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

void
Module::MlBoxRelativeTranslation (ObjectDetection * box,
    const int width, const int height) {

  box->top /= height;
  box->bottom /= height;
  box->left /= width;
  box->right /= width;
}

int32_t Module::NonMaxSuppression(const ObjectDetection &l_box,
                                  ObjectDetections &boxes) {

  for (uint32_t idx = 0; idx < boxes.size();  idx++) {
    ObjectDetection &r_box = boxes[idx];

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

std::vector<uint32_t> Module::ParseMonoblockTensor(
    const Tensors& tensors,
    std::vector<ObjectDetection>& bboxes,
    std::vector<uint32_t>& mask_matrix_indices) {

  uint32_t n_blocks = tensors[4].dimensions[3] * tensors[4].dimensions[2];
  const float* masks = reinterpret_cast<const float*>(tensors[2].data);
  const float* protos = reinterpret_cast<const float*>(tensors[4].data);

  std::vector<uint32_t> colormask(n_blocks);

  for (uint32_t idx = 0; idx < bboxes.size(); idx++) {
    ObjectDetection& bbox = bboxes[idx];
    uint32_t m_idx = mask_matrix_indices[idx];

    uint32_t top = bbox.top * tensors[4].dimensions[2];
    uint32_t left = bbox.left * tensors[4].dimensions[3];
    uint32_t bottom = bbox.bottom * tensors[4].dimensions[2];
    uint32_t right = bbox.right * tensors[4].dimensions[3];

    for (uint32_t row = top; row < bottom; row++) {
      for (uint32_t column = left; column < right; column++) {
        uint32_t b_idx = column + (row * tensors[4].dimensions[3]);

        float confidence = 0.0f;

        for (uint32_t num = 0; num < tensors[2].dimensions[2]; num++) {
          float m_value = masks[m_idx + num];
          float p_value = protos[b_idx + num * n_blocks];
          confidence += m_value * p_value;
        }

        confidence = 1.0f / (1.0f + expf(-confidence));

        colormask[b_idx] = (confidence > threshold_) ? bbox.color.value() : 0x00000000;
      }
    }
  }

  return colormask;
}

void Module::ParseTripleblockTensors (const Tensors& tensors,
                                      std::vector<ObjectDetection>& bboxes,
                                      std::vector<uint32_t>& mask_matrix_indices) {

  uint32_t n_paxels = tensors[0].dimensions[1];
  float confidence = 0.0f;

  const float *mlboxes = reinterpret_cast<const float*>(tensors[0].data);
  const float *scores = reinterpret_cast<const float*>(tensors[1].data);
  const float *classes = reinterpret_cast<const float*>(tensors[3].data);

  for (uint32_t idx = 0; idx < n_paxels; idx++) {
    ObjectDetection bbox;

    confidence = scores[idx];
    uint32_t class_idx = classes[idx];

    if (confidence < threshold_)
      continue;

    bbox.left = mlboxes[idx * 4];
    bbox.top = mlboxes[idx * 4 + 1];
    bbox.right = mlboxes[idx * 4 + 2];
    bbox.bottom = mlboxes[idx * 4 + 3];

    LOG (logger_, kTrace, "Class: %u Box[%f, %f, %f, %f] Confidence: %f",
      class_idx, bbox.top, bbox.left, bbox.bottom, bbox.right, confidence);

    MlBoxRelativeTranslation(&bbox, source_width_, source_height_);

    bbox.confidence = confidence * 100.0f;
    bbox.name = labels_parser_.GetLabel(class_idx);
    bbox.color = labels_parser_.GetColor(class_idx);

    int nms = -1;

    nms = NonMaxSuppression(bbox, bboxes);

    if (nms == (-2))
      continue;

    LOG (logger_, kLog, "Label: %s  Box[%f, %f, %f, %f] Confidence: %f",
        bbox.name.c_str(), bbox.top, bbox.left, bbox.bottom,
        bbox.right, bbox.confidence);

    if (nms > 0) {
      bboxes.erase(bboxes.begin() + nms);
      mask_matrix_indices.erase(mask_matrix_indices.begin() + nms);
    }

    bboxes.push_back(bbox);

    uint32_t num = idx * tensors[2].dimensions[2];
    mask_matrix_indices.push_back(num);
  }
}

uint64_t Module::ScaleUint64Safe(const uint64_t val,
                                 const int32_t num, const int32_t denom) {

  if (denom == 0)
    return UINT64_MAX;

  // If multiplication won't overflow, perform it directly
  if (val < (std::numeric_limits<uint32_t>::max() / num))
    return (val * num) / denom;
  else
    // Use division first to avoid overflow
    return (val / denom) * num + ((val % denom) * num) / denom;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
    std::any& output) {

  if (output.type() != typeid(VideoFrame)) {
    LOG (logger_, kError, "Unexpected output type!");
    return false;
  }

  VideoFrame& frame =
      std::any_cast<VideoFrame&>(output);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  source_width_ = resolution.width;
  source_height_ = resolution.height;

  uint32_t width = frame.width;
  uint32_t height = frame.height;

  // Retrive the video frame Bytes Per Pixel for later calculations.
  uint32_t bpp = frame.bits *
      frame.n_components / CHAR_BIT;

  // Calculate the row padding in bytes.
  uint32_t padding = frame.planes[0].stride - (width * bpp);

  std::vector<uint32_t> mask_matrix_indices;
  std::vector<ObjectDetection> bboxes;

  ParseTripleblockTensors(tensors, bboxes, mask_matrix_indices);

  if (bboxes.size() == 0)
    return true;

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  // Transform source tensor region dimensions to dimensions in the color mask.
  region.x *= (tensors[4].dimensions[3] / (float)source_width_);
  region.y *= (tensors[4].dimensions[2] / (float)source_height_);
  region.width *= (tensors[4].dimensions[3] / (float)source_width_);
  region.height *= (tensors[4].dimensions[2] / (float)source_height_);

  std::vector<uint32_t> colormask =
     ParseMonoblockTensor(tensors, bboxes, mask_matrix_indices);

  uint8_t *outdata = frame.planes[0].data;

  for (uint32_t row = 0; row < height; row++) {
    for (uint32_t column = 0; column < width; column++) {
      uint32_t num = tensors[4].dimensions[3] *
          (region.y + ScaleUint64Safe (row, region.height, height));

      num += region.x + ScaleUint64Safe (column, region.width, width);

      uint32_t idx = (((row * width) + column) * bpp) + (row * padding);

      outdata[idx] = EXTRACT_RED_COLOR (colormask[num]);
      outdata[idx + 1] = EXTRACT_GREEN_COLOR (colormask[num]);
      outdata[idx + 2] = EXTRACT_BLUE_COLOR (colormask[num]);

      if (bpp == 4)
        outdata[idx + 3] = EXTRACT_ALPHA_COLOR (colormask[num]);
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
