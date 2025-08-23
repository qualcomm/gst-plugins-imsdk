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

#include "ml-postprocess-mobilenet-softmax.h"

#define DEFAULT_THRESHOLD 0.70


/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const char* kModuleCaps = R"(
{
  "type": "image-classification",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [400, 1001]]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(DEFAULT_THRESHOLD) {

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

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);

    if (!root || root->GetType() != JsonType::Object)
      return false;

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG(logger_, kLog, "Threshold: %f", threshold_);
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  double confidence = 0.0, sum = 0.0;

  if (output.type() != typeid(ImageClassifications)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  ImageClassifications& classifications =
      std::any_cast<ImageClassifications&>(output);

  uint32_t n_inferences = tensors[0].dimensions[1];

  const float *data = static_cast<const float*>(tensors[0].data);

  // Calculate the sum of the exponents for softmax function.
  for (uint32_t idx = 0; idx < n_inferences; ++idx) {
    confidence = data[idx];
    sum += exp(confidence);
  }

  // Fill the prediction table.
  for (uint32_t idx = 0; idx < n_inferences; ++idx) {
    confidence = data[idx];
    confidence = (exp(confidence) / sum) * 100;

    // Discard results with confidence below the set threshold.
    if (confidence < threshold_)
      continue;

    ImageClassification entry;
    entry.confidence = confidence;
    entry.name = labels_parser_.GetLabel(idx);
    entry.color = labels_parser_.GetColor(idx);

    classifications.push_back(entry);
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
