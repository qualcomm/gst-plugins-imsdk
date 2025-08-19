/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-yamnet.h"

#define DEFAULT_THRESHOLD 0.70


/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const char* kModuleCaps = R"(
{
  "type": "audio-classification",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 521]
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

  double confidence = 0.0;

  if (output.type() != typeid(AudioClassifications)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  AudioClassifications& classifications =
      std::any_cast<AudioClassifications&>(output);

  uint32_t n_inferences = tensors[0].dimensions[1];

  const float *data = static_cast<const float*>(tensors[0].data);

  // Fill the prediction table.
  for (uint32_t idx = 0; idx < n_inferences; ++idx) {
    confidence = data[idx];
    confidence *= 100;

    // Discard results with confidence below the set threshold.
    if (confidence < threshold_)
      continue;

    AudioClassification entry;
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
