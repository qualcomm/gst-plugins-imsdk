/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#include "ml-postprocess-hrnet.h"

#include <cfloat>

#define DEFAULT_THRESHOLD 0.70


static const char* moduleCaps = R"(
{
  "type": "pose-estimation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [1, 256], [1, 256], [1, 17]]
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

void Module::KeypointTransformCoordinates(Keypoint& keypoint,
                                          const Region& region) {

  keypoint.x = (keypoint.x - region.x) / region.width;
  keypoint.y = (keypoint.y - region.y) / region.height;
}

std::string Module::Caps() {

  return std::string(moduleCaps);
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG (logger_, kError, "Failed to parse labels");
    return false;
  }

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);
    if (!root || root->GetType() != JsonType::Object) return false;

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG (logger_, kLog, "Threshold: %f", threshold_);

    auto nodes = root->GetArray("connections");

    if (!LoadConnections(nodes)) return false;
  }

  return true;
}

bool Module::LoadConnections(const std::vector<JsonValue::Ptr>& nodes) {

  for (const auto& node : nodes) {
    if (!node || node->GetType() != JsonType::Object)
      continue;

    const auto& obj = node->GetObject();

    if (obj.count("id") == 0 || obj.count("connection") == 0)
      continue;

    uint32_t label_id = static_cast<uint32_t>(node->GetNumber("id"));
    uint32_t con_id = static_cast<uint32_t>(node->GetNumber("connection"));

    connections_.push_back({label_id, con_id});
  }
  return true;
}

int32_t Module::TensorCompareValues(const void *data,
                                    const uint32_t& l_idx,
                                    const uint32_t& r_idx) {

  return ((float*)data)[l_idx] > ((float*)data)[r_idx] ? 1 :
      ((float*)data)[l_idx] < ((float*)data)[r_idx] ? -1 : 0;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(PoseEstimations)) {
    LOG (logger_, kError, "Unexpected predictions type!");
    return false;
  }

  PoseEstimations& estimations =
      std::any_cast<PoseEstimations&>(output);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t height = tensors[0].dimensions[1];
  uint32_t width = tensors[0].dimensions[2];
  uint32_t n_keypoints = tensors[0].dimensions[3];

  const float* heatmap = reinterpret_cast<const float*>(tensors[0].data);
  uint32_t n_blocks = width * height * n_keypoints;

  PoseEstimation entry;

  entry.keypoints.resize(n_keypoints);

  for (uint32_t idx = 0; idx < n_keypoints; idx++) {
    int dx = 0, dy = 0;

    uint32_t id = idx;

    for (uint32_t num = (idx + n_keypoints); num < n_blocks; num += n_keypoints)
      id = (TensorCompareValues(heatmap, num, id) > 0) ? num : id;

    float confidence = heatmap[id];

    uint32_t x = (id / n_keypoints) % width;
    uint32_t y = (id / n_keypoints) / width;

    LOG (logger_, kDebug, "Keypoint: %u [%u x %u], confidence %.2f",
        idx, x, y, confidence);

    uint32_t lf = (y * (x + 1) * n_keypoints) + idx;
    uint32_t rf = (y * (x - 1) * n_keypoints) + idx;

    if ((x > 1) && (x < (width - 1)) && (y > 0) && (y < height)) {
      dx = TensorCompareValues (heatmap, lf, rf);
    }

    uint32_t ls = ((y + 1) * x * n_keypoints) + idx;
    uint32_t rs = ((y - 1) * x * n_keypoints) + idx;

    if ((y > 1) && (y < (height - 1)) && (x > 0) && (x < width)) {
      dy = TensorCompareValues (heatmap, ls, rs);
    }

    Keypoint& kp = entry.keypoints[idx];

    kp.x = ((x + dx * 0.25) / width) * resolution.width;
    kp.y = ((y + dy * 0.25) / height) * resolution.height;

    kp.confidence = confidence;
    entry.confidence += kp.confidence;

    kp.name = labels_parser_.GetLabel(idx);
    kp.color = labels_parser_.GetColor(idx);

    KeypointTransformCoordinates(kp, region);
  }

  entry.confidence /= n_keypoints;

  if (entry.confidence < threshold_)
    return true;

  KeypointLinks links;
  for (uint32_t num = 0; num < connections_.size(); num++) {
    KeypointLinkIds& lk = connections_[num];

    links.push_back(
        KeypointLink(entry.keypoints[lk.s_kp_id],
            entry.keypoints[lk.d_kp_id]));
  }

  entry.links = std::move(links);

  estimations.push_back(entry);

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
