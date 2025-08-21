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

#include "ml-postprocess-srnet.h"

#include <climits>


/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const char* kModuleCaps = R"(
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
        [1, [32, 4096], [32, 4096], [1, 3]]
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

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(VideoFrame)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  VideoFrame& frame =
      std::any_cast<VideoFrame&>(output);

  // Retrive the video frame Bytes Per Pixel for later calculations.
  uint32_t bpp = frame.bits *
      frame.n_components / CHAR_BIT;

  const float *indata = static_cast<const float*>(tensors[0].data);
  uint8_t *outdata = frame.planes[0].data;

  // TODO: Right now this won't work with any output resolution.
  // TODO: Expolore the possible use of OpenGL or OpenCL
  for (uint32_t row = 0; row < frame.height; row++) {
    uint32_t inidx = row * frame.width * bpp;
    uint32_t outidx = row * frame.planes[0].stride;

    for (uint32_t column = 0; column < frame.width; column++) {

      outdata[outidx] = (uint8_t)(indata[inidx] * 255.0f);
      outdata[outidx + 1] = (uint8_t)(indata[inidx + 1] * 255.0f);
      outdata[outidx + 2] = (uint8_t)(indata[inidx + 2] * 255.0f);

      // If output has an alpha channel set it to opaque.
      if (bpp == 4)
        outdata[outidx + 3] = 0xFF;

      inidx += bpp;
      outidx += bpp;
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
