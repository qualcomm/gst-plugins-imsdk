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

  auto& segmentations = std::any_cast<std::vector<Segmentation>&>(output);
  auto& region = std::any_cast<Region&>(mlparams["input-tensor-region"]);
  auto& resolution = std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  auto indata = reinterpret_cast<const float *>(tensors.front().data);

  uint32_t mlwidth = tensors.front().dimensions[2];
  uint32_t mlheight = tensors.front().dimensions[1];
  // The 4th tensor dimension represents multiple the class scores per paxel.
  uint32_t n_scores = (tensors.front().dimensions.size() != 4) ?
      1 : tensors.front().dimensions[3];

  // Scale factors for avoiding using division and instead use multiplication.
  float wscale = static_cast<float>(mlwidth) / resolution.width;
  float hscale = static_cast<float>(mlheight) / resolution.height;

  // Recalculate region dimensions to the tensor resolution being processed.
  uint32_t left = std::lround(region.x * wscale);
  uint32_t top = std::lround(region.y * hscale);
  uint32_t right = std::lround((region.x + region.width) * wscale);
  uint32_t bottom = std::lround((region.y + region.height) * hscale);

  Segmentation segmentation;

  segmentation.n_rows = bottom - top;
  segmentation.n_columns = right - left;

  for (uint32_t row = top; row < bottom; row++) {
    for (uint32_t column = left; column < right; column++) {
      // Calculate the source index.
      uint32_t inidx = (row * mlwidth * n_scores) + column;

      // Initialize the class ID value.
      uint32_t id = inidx;

      // Find the class index with best score if tensor has multiple class scores.
      for (uint32_t num = (inidx + 1); num < (inidx + n_scores); num++)
        id = (indata[num] > indata[id]) ? num : id;

      // If there is no 4th dimension the tensor paxel contains the class ID.
      id = (n_scores == 1) ? indata[id] : (id - inidx);

      segmentation.labels.push_back(labels_parser_.GetLabel(id));
      segmentation.colors.push_back(labels_parser_.GetColor(id));
    }
  }

  segmentations.push_back(std::move(segmentation));
  return true;
}

IModule* NewModule(LogCallback logger) {

  return new Module(logger);
}
