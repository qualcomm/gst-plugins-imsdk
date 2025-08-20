/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-midas-v2.h"

#include <cfloat>
#include <climits>
#include <cmath>

#define DEFAULT_THRESHOLD 0.70

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

static const char* moduleCaps = R"(
{
  "type": "image-segmentation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [ 1, 256, 256, 1 ]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [ 1, 256, 256 ]
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

uint64_t Module::ScaleUint64Safe(const uint64_t val, const int32_t num,
                                 const int32_t denom) {

  if (denom == 0)
    return UINT64_MAX;

  // If multiplication won't overflow, perform it directly
  if (val < (std::numeric_limits<uint64_t>::max() / num))
    return (val * num) / denom;
  else
    // Use division first to avoid overflow
    return (val / denom) * num + ((val % denom) * num) / denom;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  double mindepth = std::numeric_limits<double>::max();
  double maxdepth = std::numeric_limits<double>::min();

  if (output.type() != typeid(VideoFrame)) {
    LOG (logger_, kError, "Unexpected output type!");
    return false;
  }

  VideoFrame& frame =
      std::any_cast<VideoFrame&>(output);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  uint32_t source_width = resolution.width;
  uint32_t source_height = resolution.height;

  uint32_t width = frame.width;
  uint32_t height = frame.height;

  uint32_t bpp = frame.bits *
      frame.n_components / CHAR_BIT;
  uint32_t padding = frame.planes[0].stride - (width * bpp);

  const float *indata = reinterpret_cast<const float *>(tensors[0].data);
  uint8_t* outdata = frame.planes[0].data;

  uint32_t mlwidth = tensors[0].dimensions[2];
  uint32_t mlheight = tensors[0].dimensions[1];

  region.x *= mlwidth / source_width;
  region.y *= mlheight / source_height;
  region.width *= mlwidth / source_width;
  region.height *= mlheight / source_height;

  for (uint32_t row = 0; row < region.height; row++) {
    for (uint32_t column = 0; column < region.width; column++) {
      uint32_t idx = row * mlwidth + column;

      double value = indata[idx];

      if (value > maxdepth)
        maxdepth = value;

      if (value < mindepth)
        mindepth = value;
    }
  }

  for (uint32_t row = 0; row < height; row++) {
    for (uint32_t column = 0; column < width; column++) {
      uint32_t id = std::numeric_limits<uint8_t>::max();

      uint32_t idx = mlwidth * (region.y +
         ScaleUint64Safe(row, region.height, height));

      idx += region.x + ScaleUint64Safe(column, region.width, width);

      float value = indata[idx];

      id *= (value - mindepth) / (maxdepth - mindepth);

      uint32_t color = labels_parser_.GetColor(id);

      idx = (((row * width) + column) * bpp) + (row * padding);


      outdata[idx] = EXTRACT_RED_COLOR (color);
      outdata[idx + 1] = EXTRACT_GREEN_COLOR (color);
      outdata[idx + 2] = EXTRACT_BLUE_COLOR (color);

      if (bpp == 4) {
        outdata[idx + 3] = EXTRACT_ALPHA_COLOR (color);
      }
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
