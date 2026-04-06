/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-midas-v2.h"

#include <climits>
#include <cmath>
#include <algorithm>

static const std::string kModuleCaps = R"(
{
  "type": "depth-estimation",
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

  auto& depthmaps = std::any_cast<std::vector<DepthMap>&>(output);
  auto& region = std::any_cast<Region&>(mlparams["input-tensor-region"]);
  auto& resolution = std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  auto indata = reinterpret_cast<const float *>(tensors.front().data);
  uint32_t mlwidth = tensors.front().dimensions[2];
  uint32_t mlheight = tensors.front().dimensions[1];

  // Scale factors for avoiding using division and instead use multiplication.
  float wscale = static_cast<float>(mlwidth) / resolution.width;
  float hscale = static_cast<float>(mlheight) / resolution.height;

  // Recalculate region dimensions to the tensor resolution being processed.
  uint32_t left = std::lround(region.x * wscale);
  uint32_t top = std::lround(region.y * hscale);
  uint32_t right = std::lround((region.x + region.width) * wscale);
  uint32_t bottom = std::lround((region.y + region.height) * hscale);

  double mindepth = std::numeric_limits<double>::max();
  double maxdepth = std::numeric_limits<double>::min();

  DepthMap depthmap;

  // Find the minimum and maximum depth values in the region mask.
  for (uint32_t row = top; row < bottom; row++) {
    for (uint32_t column = left; column < right; column++) {
      uint32_t idx = row * mlwidth + column;

      if (indata[idx] > maxdepth)
        maxdepth = indata[idx];

      if (indata[idx] < mindepth)
        mindepth = indata[idx];

      depthmap.values.push_back(indata[idx]);
    }
  }

  depthmap.n_rows = bottom - top;
  depthmap.n_columns = right - left;

  std::for_each(depthmap.values.cbegin(), depthmap.values.cend(),
      [&](const double& depth) {
        uint32_t id = std::numeric_limits<uint8_t>::max() *
                      ((depth - mindepth) / (maxdepth - mindepth));
        depthmap.colors.push_back(labels_parser_.GetColor(id));
      });

  depthmaps.push_back(std::move(depthmap));
  return true;
}

IModule* NewModule(LogCallback logger) {

  return new Module(logger);
}
