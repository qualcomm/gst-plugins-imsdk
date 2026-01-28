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

#include "ml-postprocess-srnet.h"

#include <climits>
#include <cmath>
#include <algorithm>

/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const std::string kModuleCaps = R"(
{
  "type": "super-resolution",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [32, 4096], [32, 4096]]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [32, 4096], [32, 4096], 3]
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

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  auto& frame = std::any_cast<VideoFrame&>(output);
  auto& region = std::any_cast<Region&>(mlparams["input-tensor-region"]);
  auto& resolution = std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  uint32_t mlwidth = tensors.front().dimensions[2];
  uint32_t mlheight = tensors.front().dimensions[1];

  if ((frame.width != mlwidth) && (frame.height != mlheight)) {
    LOG(logger_, kError,
        "Mismatch between video frame resolution and tensor resolution!");
    return false;
  } else if ((frame.format != VideoFormat::kRGBA8888) &&
             (frame.format != VideoFormat::kRGBX8888)) {
    LOG(logger_, kError, "Unsupported video format!");
    return false;
  }

  auto indata = static_cast<const float*>(tensors.front().data);
  uint8_t *outdata = frame.planes[0].data;

  // Scale factors for avoiding using division and instead use multiplication.
  float wscale = static_cast<float>(mlwidth) / resolution.width;
  float hscale = static_cast<float>(mlheight) / resolution.height;

  // Recalculate region dimensions to the tensor resolution being processed.
  uint32_t left = std::lround(region.x * wscale);
  uint32_t top = std::lround(region.y * hscale);
  uint32_t right = std::lround((region.x + region.width) * wscale);
  uint32_t bottom = std::lround((region.y + region.height) * hscale);

  for (uint32_t row = top; row < bottom; row++) {
    uint32_t inidx = (row * mlwidth * 3) + (left * 3);
    uint32_t outidx = (row * frame.planes[0].stride) + (left * 4);

    for (uint32_t col = left; col < right; col++, inidx += 3, outidx += 4) {
      outdata[outidx] = std::clamp(indata[inidx], 0.0f, 1.0f) * 255.0f;
      outdata[outidx + 1] = std::clamp(indata[inidx + 1], 0.0f, 1.0f) * 255.0f;
      outdata[outidx + 2] = std::clamp(indata[inidx + 2], 0.0f, 1.0f) * 255.0f;
      outdata[outidx + 3] = 0xFF;
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {

  return new Module(logger);
}
