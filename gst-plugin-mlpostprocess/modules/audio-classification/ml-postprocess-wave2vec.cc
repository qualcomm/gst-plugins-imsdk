/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-wave2vec.h"

#include <climits>

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
        [1, 124, 32],
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

  uint32_t res_num = 0;
  uint32_t last_class = UINT32_MAX;
  double confidence = 0.0;

  if (output.type() != typeid(AudioClassifications)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  AudioClassifications& classifications =
      std::any_cast<AudioClassifications&>(output);

  uint32_t sequence_length = tensors[0].dimensions[1];
  uint32_t num_classes = tensors[0].dimensions[2];

  const float *data = static_cast<const float*>(tensors[0].data);

  AudioClassification entry;
  entry.color = 0x00FF00FF;

  // Fill the prediction table.
  for (uint32_t idx = 0; idx < sequence_length; ++idx) {

    // Find highest confidence class
    uint32_t max_class = 0;
    float max_conf = 0;

    for (uint32_t c = 0; c < num_classes; ++c) {
      float val = data[idx * num_classes + c];
      if (val > max_conf) {
        max_conf = val;
        max_class = c;
      }
    }

    if (max_class == 0 || max_class == last_class)
      continue;

    confidence += max_conf;
    res_num++;

    last_class = max_class;

    entry.name += labels_parser_.GetLabel(max_class);
  }

  if (res_num > 0)
    confidence /= res_num;

  if (confidence >= threshold_) {
    entry.confidence = confidence;
    classifications.push_back(entry);
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
