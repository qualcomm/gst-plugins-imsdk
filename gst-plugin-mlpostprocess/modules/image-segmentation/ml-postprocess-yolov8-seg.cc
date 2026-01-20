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

void Module::MlBoxRelativeTranslation(ObjectDetection * box, const int width,
                                       const int height) {

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

    double score = IntersectionScore(l_box, r_box);

    // If the score is below the threshold, continue with next list entry.
    if (score <= kNMSIntersectionTreshold)
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

std::vector<uint32_t> Module::GenerateMaskFromProtos(const Tensors& tensors,
                                                     std::vector<ObjectDetection>& bboxes,
                                                     std::vector<uint32_t>& mask_matrix_indices,
                                                     uint32_t proto_tensor_idx) {

  uint32_t mlheight = tensors[proto_tensor_idx].dimensions[1];
  uint32_t mlwidth = tensors[proto_tensor_idx].dimensions[2];
  uint32_t n_channels = tensors[proto_tensor_idx].dimensions[3];
  const float* protos = reinterpret_cast<const float*>(tensors[proto_tensor_idx].data);
  uint32_t n_blocks = mlheight * mlwidth;
  const float* masks = reinterpret_cast<const float*>(tensors[2].data);
  std::vector<uint32_t> colormask(n_blocks);

  for (uint32_t idx = 0; idx < bboxes.size(); idx++) {
    ObjectDetection& bbox = bboxes[idx];
    uint32_t m_idx = mask_matrix_indices[idx];
    uint32_t top = bbox.top * mlheight;
    uint32_t left = bbox.left * mlwidth;
    uint32_t bottom = bbox.bottom * mlheight;
    uint32_t right = bbox.right * mlwidth;

    for (uint32_t row = top; row < bottom; row++) {
      for (uint32_t column = left; column < right; column++) {
        uint32_t b_idx = (row * mlwidth + column) * n_channels;
        float confidence = 0.0f;

        for (uint32_t num = 0; num < tensors[2].dimensions[2]; num++) {
          float m_value = masks[m_idx + num];
          float p_value = protos[b_idx + num];
          confidence += m_value * p_value;
        }

        confidence = 1.0f / (1.0f + expf(-confidence));

        uint32_t spatial_idx = row * mlwidth + column;
        colormask[spatial_idx] = (confidence > threshold_) ? bbox.color.value() : 0x00000000;
      }
    }
  }

  return colormask;
}

void Module::ParseBoundingBoxes(const Tensors& tensors,
                                std::vector<ObjectDetection>& bboxes,
                                std::vector<uint32_t>& mask_matrix_indices) {

  uint32_t n_paxels = tensors[0].dimensions[1];
  float confidence = 0.0f;
  bool has_classes = (tensors.size() == 5);

  const float *mlboxes = reinterpret_cast<const float*>(tensors[0].data);
  const float *scores = reinterpret_cast<const float*>(tensors[1].data);
  const float *classes = has_classes ? reinterpret_cast<const float*>(tensors[3].data) : nullptr;

  for (uint32_t idx = 0; idx < n_paxels; idx++) {
    ObjectDetection bbox;

    confidence = scores[idx];
    uint32_t class_idx = has_classes ? static_cast<uint32_t>(classes[idx]) : 0;

    if (confidence < threshold_)
      continue;

    bbox.left = mlboxes[idx * 4];
    bbox.top = mlboxes[idx * 4 + 1];
    bbox.right = mlboxes[idx * 4 + 2];
    bbox.bottom = mlboxes[idx * 4 + 3];

    LOG(logger_, kTrace, "Class: %u Box[%f, %f, %f, %f] Confidence: %f",
      class_idx, bbox.top, bbox.left, bbox.bottom, bbox.right, confidence);

    MlBoxRelativeTranslation(&bbox, source_width_, source_height_);

    bbox.confidence = confidence * 100.0f;

    if (has_classes) {
      bbox.name = labels_parser_.GetLabel(class_idx);
      bbox.color = labels_parser_.GetColor(class_idx);
    } else {
      uint32_t instance_idx = idx % labels_parser_.Size();
      bbox.name = labels_parser_.GetLabel(instance_idx);
      bbox.color = labels_parser_.GetColor(instance_idx);
    }

    int nms = -1;

    nms = NonMaxSuppression(bbox, bboxes);

    if (nms == (-2))
      continue;

    LOG(logger_, kLog, "Label: %s  Box[%f, %f, %f, %f] Confidence: %f",
        bbox.name.c_str(), bbox.top, bbox.left, bbox.bottom,
        bbox.right, bbox.confidence);

    if (nms > 0) {
      bboxes.erase(bboxes.begin() + nms);
      mask_matrix_indices.erase(mask_matrix_indices.begin() + nms);
    }

    bboxes.emplace_back(std::move(bbox));

    uint32_t num = idx * tensors[2].dimensions[2];
    mask_matrix_indices.emplace_back(std::move(num));
  }
}

void Module::ParseSegmentationFrame(const Tensors& tensors, Dictionary& mlparams,
                                     std::any& output, uint32_t proto_tensor_idx) {

  if (output.type() != typeid(VideoFrame)) {
    LOG (logger_, kError, "Unexpected output type!");
    return;
  }

  VideoFrame& frame = std::any_cast<VideoFrame&>(output);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  source_width_ = resolution.width;
  source_height_ = resolution.height;

  uint32_t width = frame.width;
  uint32_t height = frame.height;

  // Retrieve the video frame Bytes Per Pixel for later calculations.
  uint32_t bpp = frame.bits * frame.n_components / CHAR_BIT;

  std::vector<uint32_t> mask_matrix_indices;
  std::vector<ObjectDetection> bboxes;

  ParseBoundingBoxes(tensors, bboxes, mask_matrix_indices);

  if (bboxes.size() == 0)
    return;

  // Get region
  Region& region = std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t mlheight = tensors[proto_tensor_idx].dimensions[1];
  uint32_t mlwidth = tensors[proto_tensor_idx].dimensions[2];

  // Transform source tensor region dimensions to dimensions in the color mask.
  region.x *= (mlwidth / (float)source_width_);
  region.y *= (mlheight / (float)source_height_);
  region.width *= (mlwidth / (float)source_width_);
  region.height *= (mlheight / (float)source_height_);

  std::vector<uint32_t> colormask =
      GenerateMaskFromProtos(tensors, bboxes, mask_matrix_indices, proto_tensor_idx);

  uint8_t *outdata = frame.planes[0].data;

  for (uint32_t row = 0; row < height; row++) {
    uint32_t outidx = row * frame.planes[0].stride;

    for (uint32_t column = 0; column < width; column++, outidx += bpp) {
      uint32_t num = mlwidth * (region.y + (row * region.height) / height);

      num += region.x + (column * region.width) / width;

      outdata[outidx] = EXTRACT_RED_COLOR(colormask[num]);
      outdata[outidx + 1] = EXTRACT_GREEN_COLOR(colormask[num]);
      outdata[outidx + 2] = EXTRACT_BLUE_COLOR(colormask[num]);

      if (bpp == 4)
        outdata[outidx + 3] = EXTRACT_ALPHA_COLOR(colormask[num]);
    }
  }
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
    std::any& output) {

  if (tensors.size() == 5)
    // For 5-tensor model, proto tensor is at index 4
    ParseSegmentationFrame(tensors, mlparams, output, 4);
  else if (tensors.size() == 4)
    // For 4-tensor model, proto tensor is at index 3
    ParseSegmentationFrame(tensors, mlparams, output, 3);
  else {
    LOG(logger_, kError,
        "ML frame with unsupported number of tensors: %zu. "
        "Expected 4 or 5 tensors for segmentation models!",
        tensors.size());
    return false;
  }

  return true;
}

IModule* NewModule(LogCallback logger) {

  return new Module(logger);
}
