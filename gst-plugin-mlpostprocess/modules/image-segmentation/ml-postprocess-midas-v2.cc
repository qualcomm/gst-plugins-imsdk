/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-midas-v2.h"

#include <climits>
#include <cmath>
#include <algorithm>

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

static const std::string kModuleCaps = R"(
{
  "type": "image-segmentation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [128, 2048], [128, 2048], 1]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [128, 2048], [128, 2048]]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb) {

}

Module::~Module() {

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

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  auto& frame = std::any_cast<VideoFrame&>(output);
  auto& region = std::any_cast<Region&>(mlparams["input-tensor-region"]);
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

  auto indata = reinterpret_cast<const float *>(tensors.front().data);
  uint8_t *outdata = frame.planes[0].data;

  uint32_t mlwidth = tensors.front().dimensions[2];
  uint32_t mlheight = tensors.front().dimensions[1];

  // Scale factors for avoiding using division and instead use multiplication.
  float wscale = static_cast<float>(mlwidth) / resolution.width;
  float hscale = static_cast<float>(mlheight) / resolution.height;

  double mindepth = std::numeric_limits<double>::max();
  double maxdepth = std::numeric_limits<double>::min();

  // Recalculate region dimensions to the tensor resolution being processed.
  uint32_t left = std::lround(region.x * wscale);
  uint32_t top = std::lround(region.y * hscale);
  uint32_t right = std::lround((region.x + region.width) * wscale);
  uint32_t bottom = std::lround((region.y + region.height) * hscale);

  // Find the minimum and maximum depth values in the region mask.
  for (uint32_t row = top; row < bottom; row++) {
    for (uint32_t column = left; column < right; column++) {
      uint32_t inidx = row * mlwidth + column;

      if (indata[inidx] > maxdepth)
        maxdepth = indata[inidx];

      if (indata[inidx] < mindepth)
        mindepth = indata[inidx];
    }
  }

  left = region.x;
  top = region.y;
  right = region.x + region.width;
  bottom = region.y + region.height;

  for (uint32_t row = top; row < bottom; row++) {
    uint32_t outidx = (row * frame.planes[0].stride) + (left * 4);

    for (uint32_t column = left; column < right; column++, outidx += 4) {
      // Calculate the source index.
      uint32_t inidx = (std::lround(row * hscale) * mlwidth) + (column * wscale);

      uint32_t id = std::numeric_limits<uint8_t>::max() *
          (indata[inidx] - mindepth) / (maxdepth - mindepth);
      uint32_t color = labels_parser_.GetColor(id);

      outdata[outidx] = EXTRACT_RED_COLOR(color);
      outdata[outidx + 1] = EXTRACT_GREEN_COLOR(color);
      outdata[outidx + 2] = EXTRACT_BLUE_COLOR(color);
      outdata[outidx + 3] = EXTRACT_ALPHA_COLOR(color);
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {

  return new Module(logger);
}
