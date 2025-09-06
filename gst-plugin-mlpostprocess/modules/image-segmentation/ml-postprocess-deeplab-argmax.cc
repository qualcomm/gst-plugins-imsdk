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

#include "ml-postprocess-deeplab-argmax.h"

#include <climits>

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)


/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const char* kModuleCaps = R"(
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

  return std::string(kModuleCaps);
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG(logger_, kError, "Failed to parse labels");
    return false;
  }

  return true;
}

int32_t Module::CompareValues(const float *data,
                              const uint32_t& l_idx, const uint32_t& r_idx) {

  return ((float*)data)[l_idx] > ((float*)data)[r_idx] ? 1 :
      ((float*)data)[l_idx] < ((float*)data)[r_idx] ? -1 : 0;
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

  if (output.type() != typeid(VideoFrame)) {
    LOG(logger_, kError, "Unexpected output type!");
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

  // Retrive the video frame Bytes Per Pixel for later calculations.
  uint32_t bpp = frame.bits *
      frame.n_components / CHAR_BIT;

  const float *indata = static_cast<const float*>(tensors[0].data);
  uint8_t *outdata = frame.planes[0].data;

  // The 4th tensor dimension represents multiple the class scores per pixel.
  uint32_t n_scores = (tensors[0].dimensions.size() != 4) ? 1 :
      tensors[0].dimensions[3];

  // Transform source tensor region dimensions to dimensions in the color mask.
  region.x *= (tensors[0].dimensions[2] / (float)resolution.width);
  region.y *= (tensors[0].dimensions[1] / (float)resolution.height);
  region.width *= (tensors[0].dimensions[2] / (float)resolution.width);
  region.height *= (tensors[0].dimensions[1] / (float)resolution.height);

  for (uint32_t row = 0; row < frame.height; row++) {
    uint32_t outidx = row * frame.planes[0].stride;

    for (uint32_t column = 0; column < frame.width; column++, outidx += bpp) {
      // Calculate the source index. First calculate the row offset.
      uint32_t inidx = tensors[0].dimensions[2] *
          (region.y + ScaleUint64Safe(row, region.height, frame.height));

      // Calculate the source index. Second calculate the column offset.
      inidx += region.x + ScaleUint64Safe(column, region.width, frame.width);

      // Calculate the source index. Lastly multiply by the number of class scores.
      inidx *= n_scores;

      // Initialize the class ID value.
      uint32_t id = inidx;

      // Find the class index with best score if tensor has multiple class scores.
      for (uint32_t num = (inidx + 1); num < (inidx + n_scores); num++)
        id = (CompareValues(indata, num, id) > 0) ? num : id;

      // If there is no 4th dimension the tensor pixel contains the class ID.
      if (n_scores == 1)
        id = indata[id];
      else
        id = (id - inidx);

      uint32_t color = labels_parser_.GetColor(id);

      outdata[outidx] = EXTRACT_RED_COLOR(color);
      outdata[outidx + 1] = EXTRACT_GREEN_COLOR(color);
      outdata[outidx + 2] = EXTRACT_BLUE_COLOR(color);

      if (bpp == 4)
        outdata[outidx + 3] = EXTRACT_ALPHA_COLOR(color);
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
