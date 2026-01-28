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
*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "ml-postprocess-deeplab-argmax.h"

#include <climits>
#include <cmath>
#include <algorithm>

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const std::string kModuleCaps = R"(
{
  "type": "image-segmentation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [32, 2048], [32, 2048]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [32, 2048], [32, 2048], [1, 150]]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb) {

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
  // The 4th tensor dimension represents multiple the class scores per paxel.
  uint32_t n_scores =
      (tensors.front().dimensions.size() != 4) ? 1 : tensors.front().dimensions[3];

  uint32_t left = region.x;
  uint32_t top = region.y;
  uint32_t right = region.x + region.width;
  uint32_t bottom = region.y + region.height;

  // Scale factors for avoiding using division and instead use multiplication.
  float wscale = static_cast<float>(mlwidth) / resolution.width;
  float hscale = static_cast<float>(mlheight) / resolution.height;

  for (uint32_t row = top; row < bottom; row++) {
    uint32_t outidx = (row * frame.planes[0].stride) + (left * 4);

    for (uint32_t column = left; column < right; column++, outidx += 4) {
      // Calculate the source index.
      uint32_t inidx = n_scores *
          (std::lround(row * hscale) * mlwidth) + std::lround(column * wscale);

      // Initialize the class ID value.
      uint32_t id = inidx;

      // Find the class index with best score if tensor has multiple class scores.
      for (uint32_t num = (inidx + 1); num < (inidx + n_scores); num++)
        id = (indata[num] > indata[id]) ? num : id;

      // If there is no 4th dimension the tensor paxel contains the class ID.
      id = (n_scores == 1) ? indata[id] : (id - inidx);

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
