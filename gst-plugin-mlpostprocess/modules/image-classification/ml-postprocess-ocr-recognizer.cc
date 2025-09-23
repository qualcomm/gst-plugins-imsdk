/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-ocr-recognizer.h"
#include "qti-json-parser.h"

#include <limits>
#include <cmath>

static const std::vector<std::string> alphabet = {
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "!", "\\", "\"", "#", "$",
  "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/", ":", ";", "<", "=",
  ">", "?", "@", "[", "\\", "]", "^", "_", "`", "{", "|", "}", "~", " ", "A", "B",
  "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
  "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "a", "b", "c", "d", "e", "f",
  "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u",
  "v", "w", "x", "y", "z"
};

#define DEFAULT_THRESHOLD 0.90

static const char* moduleCaps = R"(
{
  "type": "image-classification",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [250, 1, 97]
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [26, 250], 97]
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

  if (output.type() != typeid(ImageClassifications)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  if (tensors.empty()) {
    LOG(logger_, kError, "No tensors provided!");
    return false;
  }

  if (tensors[0].dimensions.size() < 3) {
    LOG(logger_, kError, "Tensor has incorrect dimensions!");
    return false;
  }

  ImageClassifications& classifications =
      std::any_cast<ImageClassifications&>(output);

  const uint32_t blank = 0;
  std::string result;
  uint32_t prev = std::numeric_limits<uint32_t>::max();
  std::vector<float> emittedProbs;

  uint32_t n_rows = tensors[0].dimensions[0];
  uint32_t n_characters = tensors[0].dimensions[2];

  if (n_rows == 1)
    n_rows = tensors[0].dimensions[1];

  float * data = (float *) tensors[0].data;

  LOG(logger_, kTrace, "n_rows: %d, n_characters: %d", n_rows, n_characters);

  result.reserve(n_rows);
  emittedProbs.reserve(n_rows);

  std::vector<float> timestepMaxProb;
  timestepMaxProb.reserve(n_rows);

  const uint32_t alphaSize = static_cast<uint32_t>(alphabet.size());
  const size_t stride = static_cast<size_t>(n_characters);

  for (uint32_t tstep = 0; tstep < n_rows; ++tstep) {
    const float* logits = data + tstep * stride;

    float maxLogit = logits[0];
    uint32_t k = 0;
    for (uint32_t c = 1; c < n_characters; ++c) {
      const float v = logits[c];
      if (v > maxLogit) {
        maxLogit = v;
        k = c;
      }
    }

    double sumExp = 0.0;
    std::vector<double> expValues(n_characters);

    for (uint32_t c = 0; c < n_characters; ++c) {
      expValues[c] = std::exp(double(logits[c] - maxLogit));
      sumExp += expValues[c];
    }

    float p = static_cast<float>(expValues[k] / sumExp);
    timestepMaxProb.push_back(p);

    if (k != blank && k != prev && k < alphaSize && k > 0) {
      LOG(logger_, kTrace, "k: %d, p: %f",k,p);
      const std::string& ch = alphabet[k - 1];
      if ((std::isalnum(ch[0]) || ch[0] == ' ') && p > threshold_) {
        result += ch;
        emittedProbs.push_back(p);
      } else {
        LOG(logger_, kTrace, "Ignoring character \'%s\' for k=%d.", ch.c_str(), k);
      }
    }
    prev = k;
  }

  float confidence = 0.f;

  if (!emittedProbs.empty()) {
    double sum = 0.0;
    for (float p : emittedProbs) sum += p;
    confidence = static_cast<float>(sum / emittedProbs.size());
  } else {
    double sum = 0.0;
    for (float p : timestepMaxProb) sum += p;
    confidence = static_cast<float>(sum / (timestepMaxProb.empty() ? 1 : timestepMaxProb.size()));
  }

  if (result.empty()) {
    LOG(logger_, kError, "Result is empty!");
    return true;
  }

  LOG(logger_, kInfo, "Result is %s", result.c_str());

  ImageClassification entry;
  entry.confidence = confidence;
  entry.name = "label_" + result;
  entry.color = 0x00FF00FF;

  classifications.push_back(entry);
  return true;
}


IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
