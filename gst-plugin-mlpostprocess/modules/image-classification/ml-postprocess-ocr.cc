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

#include "ml-postprocess-ocr.h"
#include "qti-json-parser.h"

static const char alphabet[] = "_0123456789abcdefghijklmnopqrstuvwxyz";

#define DEFAULT_THRESHOLD 0.70

static const char* moduleCaps = R"(
{
  "type": "image-classification",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [26, 1, 37]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [26, 48], 37]
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

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);
    if (!root || root->GetType() != JsonType::Object) return false;

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG(logger_, kLog, "Threshold: %f", threshold_);
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  std::string result;

  if (output.type() != typeid(ImageClassPrediction)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }
  ImageClassifications& classifications =
      std::any_cast<ImageClassifications&>(output);


  uint32_t n_characters = tensors[0].dimensions[2];
  uint32_t n_rows = tensors[0].dimensions[0];
  float * data = (float *) tensors[0].data;

  if (n_rows == 1)
    n_rows = tensors[0].dimensions[1];

  LOG(logger_, kTrace, "n_rows: %d, n_characters: %d", n_rows, n_characters);

  uint32_t idx = 0;
  for (idx = 0; idx < n_rows; ++idx) {
    uint32_t num = 0, c_id = 0;

    float* pclass = data + (n_characters * idx);

     // Find the character ID with the highest confidence.
    for (num = 1; num < n_characters; ++num)
      c_id = (pclass[num] > pclass[c_id]) ? num : c_id;

    if (!c_id)
      continue;

    result = result + alphabet[c_id];
  }

  if (result.empty())
    return true;

  ImageClassification entry;

  entry.confidence = data[idx];
  entry.name = result;
  entry.color = 0x00FF00FF;

  classifications.push_back(entry);

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
