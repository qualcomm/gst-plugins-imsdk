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

#include "ml-postprocess-posenet.h"

#include <cfloat>

#define DEFAULT_THRESHOLD 0.70

// Minimum distance in pixels between keypoints of poses.
#define NMS_THRESHOLD_RADIUS  20.0F
// Radius in which to search for highest root keypoint of given type.
#define LOCAL_MAXIMUM_RADIUS  1
// Number of refinement steps to apply when traversing skeleton links.
#define NUM_REFINEMENT_STEPS  2


/* kModuleCaps
*
* Description of the supported caps and the type of the module.
*/
static const char* kModuleCaps = R"(
{
  "type": "pose-estimation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [5, 251], [5, 251], [1, 17]],
        [1, [5, 251], [5, 251], [2, 34]],
        [1, [5, 251], [5, 251], [4, 64]],
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(DEFAULT_THRESHOLD),
      source_width_(0),
      source_height_(0) {

}

int32_t Module::NonMaxSuppression(PoseEstimation &l_entry,
                                  PoseEstimations &entries) {

  uint32_t n_overlaps = 0;
  uint32_t n_keypoints = l_entry.keypoints.size();

  // The threhold distance between 2 keypoints.
  double threshold = NMS_THRESHOLD_RADIUS * NMS_THRESHOLD_RADIUS;

  for (uint32_t idx = 0; idx < entries.size();  idx++, n_overlaps = 0) {
    PoseEstimation& r_entry = entries[idx];

    // Find out how much overlap is between the keypoints of the predictons.
    for (uint32_t num = 0; num < n_keypoints; num++) {
      Keypoint& l_kp = l_entry.keypoints[num];
      Keypoint& r_kp = r_entry.keypoints[num];

      double distance = pow (l_kp.x - r_kp.x, 2) + pow (l_kp.y - r_kp.y, 2);

      // If the distance is below the threshold, increase the overlap score.
      if (distance <= threshold)
        n_overlaps += 1;
    }

    // If only half of the keypoints overlap then it's probably another pose.
    if (n_overlaps < (n_keypoints / 2))
      continue;

    // If confidence of current prediction is higher, remove the old entry.
    if (l_entry.confidence > r_entry.confidence)
      return idx;

    // If confidence of current prediction is lower, don't add it to the list.
    if (l_entry.confidence <= r_entry.confidence)
      return -2;
  }

  // If this point is reached then add current prediction to the list;
  return -1;
}

static bool MlCompareRootpoints(RootPoint& a, RootPoint& b) {

  if (a.confidence <= b.confidence)
    return false;

  return true;
}

void Module::ExtractRootpoints(const Tensors& tensors,
                              std::vector<RootPoint>& rootpoints) {

  // The 2nd dimension of each tensor represents the matrix height.
  int32_t n_rows = tensors[0].dimensions[1];
  // The 3rd dimension of each tensor represents the matrix width.
  int32_t n_columns = tensors[0].dimensions[2];
  // The 4th dimension of 1st tensor represents the number of parts in the pose.
  uint32_t n_parts = tensors[0].dimensions[3];

  // Convenient pointer to the keypoints heatmap inside the 1st tensor.
  const float *heatmap = static_cast<const float*>(tensors[0].data);
  // Pointer to the keypoints coordinate offsets inside the 2nd tensor.
  const float *offsets = static_cast<const float*>(tensors[1].data);

  // The width (position 0) and height (position 1) of the paxel block.
  uint32_t paxelsize[2] = {0, 0};
  paxelsize[0] = (source_width_ - 1) / (n_columns - 1);
  paxelsize[1] = (source_height_ - 1) / (n_rows - 1);

  // Confidence threshold represented as the exponent of sigmoid.
  float threshold = log(threshold_ / (1 - threshold_));

  // Iterate the heatmap and find the keypoint with highest score for each block.
  for (int32_t row = 0; row < n_rows; row++) {
    for (int32_t column = 0; column < n_columns; column++) {
      for (uint32_t num = 0; num < n_parts; num++) {
        RootPoint rootpoint;
        uint32_t x = 0, y = 0, xmin = 0, xmax = 0, ymin = 0, ymax = 0;
        float score = FLT_MIN;

        uint32_t idx = (((row * n_columns) + column) * n_parts) + num;

        // Extract the keypoint heatmap confidence.
        float confidence = heatmap[idx];

        // Discard results below the minimum confidence threshold.
        if (confidence < threshold)
          continue;

        // Calculate the X and Y range of the local window.
        ymin = std::max(row - LOCAL_MAXIMUM_RADIUS, 0);
        ymax = std::min(row + LOCAL_MAXIMUM_RADIUS + 1, n_rows);

        xmin = std::max(column - LOCAL_MAXIMUM_RADIUS, 0);
        xmax = std::min(column + LOCAL_MAXIMUM_RADIUS + 1, n_columns);

        // Check if this root point is the maximum in the local window.
        for (y = ymin; (confidence >= score) && (y < ymax); y++) {
          for (x = xmin; (confidence >= score) && (x < xmax); x++) {
            idx = (((y * n_columns) + x) * n_parts) + num;

            score = heatmap[idx];
          }
        }

        // Dicard keypoint if it is not the maximum in the local window.
        if (confidence < score)
          continue;

        // Apply a sigmoid function in order to normalize the heatmap confidence.
        confidence = 1.0 / (1.0 + expf(- confidence));

        rootpoint.id = num;
        rootpoint.confidence = confidence * 100.0;

        rootpoint.x = column * paxelsize[0];
        rootpoint.y = row * paxelsize[1];

        idx = (((y * n_columns) + x) * n_parts * 2) + num;

        // Dequantize the keypoint Y axis offset and add it ot the end coordinate.
        rootpoint.y += offsets[idx];

        // Dequantize the keypoint X axis offset and add it ot the end coordinate.
        rootpoint.x += offsets[idx + n_parts];

        LOG(logger_, kTrace, "Root Keypoint %u [%.2f x %.2f], confidence %.2f",
            rootpoint.id, rootpoint.x, rootpoint.y, rootpoint.confidence);

        rootpoints.push_back(rootpoint);
      }
    }
  }

  // Sort the hough keypoint scores map by the their confidences.
  std::sort(rootpoints.begin(), rootpoints.end(),
      MlCompareRootpoints);
}

void Module::TraverseSkeletonLinks(const Tensors& tensors,
                                   PoseEstimation &entry, bool backwards) {

  uint32_t num = 0;

  // The 2nd dimension of each tensor represents the matrix height.
  uint32_t n_rows = tensors[0].dimensions[1];
  // The 3rd dimension of each tensor represents the matrix width.
  uint32_t n_columns = tensors[0].dimensions[2];
  // The 4th dimension of 1st tensor represents the number of keypoints.
  uint32_t n_parts = tensors[0].dimensions[3];

  // The 4th dimension of 3rd tensor represents the number of keypoint links.
  // Division by 4 due to X and Y coordinates and backwards and forward values.
  int32_t n_edges = tensors[2].dimensions[3] / 4;

  // Pointer to the keypoints heatmap inside the 1st tensor.
  const float *heatmap = static_cast<const float*>(tensors[0].data);
  // Pointer to the keypoints coordinate offsets inside the 2nd tensor.
  const float *offsets = static_cast<const float*>(tensors[1].data);
  // Pointer to the displacement data inside the 3rd tensor.
  const float *displacements = static_cast<const float*>(tensors[2].data);

  // The width (position 0) and height (position 1) of the paxel block.
  uint32_t paxelsize[2] = {0, 0};
  paxelsize[0] = (source_width_ - 1) / (n_columns - 1);
  paxelsize[1] = (source_height_ - 1) / (n_rows - 1);

  int32_t base = backwards ? (n_edges - 1) : 0;

  for (int32_t edge = 0; edge < n_edges; edge++, num = 0) {
    uint32_t id = std::abs(base - edge);

    KeypointLinkIds& link = links_[id];

    uint32_t s_kp_id = backwards ? link.d_kp_id : link.s_kp_id;
    uint32_t d_kp_id = backwards ? link.s_kp_id : link.d_kp_id;

    Keypoint& s_kp = entry.keypoints[s_kp_id];
    Keypoint& d_kp = entry.keypoints[d_kp_id];

    // Skip if source is not present or destination is already populated.
    if ((s_kp.confidence == 0.0) || (d_kp.confidence != 0.0))
      continue;

    // Calculate original X & Y axis values in the matrix coordinate system.
    uint32_t row = std::clamp(round (s_kp.y / paxelsize[1]),
        (double)0, (double)(n_rows - 1));
    uint32_t column = std::clamp(round (s_kp.x / paxelsize[0]),
        (double)0, (double)(n_columns - 1));

    // Calculate the position of source keypoint inside the displacements tensor.
    uint32_t idx = (((row * n_columns) + column) * (n_edges * 4)) + id;
    // For reverse iteration an additional offset by half of the edges is needed.
    idx += backwards ? (n_edges * 2) : 0;

    // Calculate the displaced Y axis value in the matrix coordinate system.
    float displacement = displacements[idx];
    d_kp.y = s_kp.y + displacement;

    // Calculate the displaced X axis value in the matrix coordinate system.
    displacement = displacements[idx + n_edges];
    d_kp.x = s_kp.x + displacement;

    // Refine the destination keypoint coordinates.
    do {
      // Calculate original X & Y axis values in the matrix coordinate system.
      row = std::clamp(round(d_kp.y / paxelsize[1]),
          (double)0, (double)(n_rows - 1));
      column = std::clamp(round(d_kp.x / paxelsize[0]),
          (double)0, (double)(n_columns - 1));

      // Calculate the position of target keypoint inside the offsets tensor.
      idx = (((row * n_columns) + column) * n_parts * 2) + d_kp_id;

      // Dequantize the keypoint Y axis offset and add it ot the end coordinate.
      float offset = offsets[idx];
      d_kp.y = row * paxelsize[1] + offset;

      // Dequantize the keypoint X axis offset and add it ot the end coordinate.
      offset = offsets[idx + n_parts];
      d_kp.x = column * paxelsize[0] + offset;
    } while (++num < NUM_REFINEMENT_STEPS);

    // Clamp values outside the range.
    d_kp.y = std::clamp((double)d_kp.y,
        (double)0, (double)(source_height_ - 1));
    d_kp.x = std::clamp((double)d_kp.x,
        (double)0, (double)(source_width_ - 1));

    // Calculate original X & Y axis values in the matrix coordinate system.
    row = std::clamp(round(d_kp.y / paxelsize[1]),
        (double)0, (double)(n_rows - 1));
    column = std::clamp(round(d_kp.x / paxelsize[0]),
        (double)0, (double)(n_columns - 1));

    // Calculate the position of target keypoint inside the heatmap tensor.
    idx = (((row * n_columns) + column) * n_parts) + d_kp_id;

    // Extract the keypoint heatmap confidence.
    float confidence = heatmap[idx];
    // Apply a sigmoid function in order to normalize the heatmap confidence.
    confidence = 1.0 / (1.0 + expf(- confidence));

    d_kp.confidence = confidence * 100;

    // Extract info from labels and populate the coresponding keypoint params.
    d_kp.name = labels_parser_.GetLabel(d_kp_id);
    d_kp.color = labels_parser_.GetColor(d_kp_id);

    LOG(logger_, kTrace, "Link[%d]: '%s' [%f x %f], %.2f <---> '%s' [%f x %f], %.2f", id,
        s_kp.name.c_str(), s_kp.x, s_kp.y, s_kp.confidence,
        d_kp.name.c_str(), d_kp.x, d_kp.y, d_kp.confidence);

    // Increase the overall prediction confidence with the found keypoint.
    entry.confidence += d_kp.confidence / n_parts;
  }
}

bool Module::LoadLinks(const std::vector<JsonValue::Ptr>& nodes, uint32_t idx,
                       std::vector<KeypointLinkIds>& links) {

  if (idx >= nodes.size())
    return false;

  const auto& node = nodes[idx];
  if (!node)
    return false;

  if (node->GetType() != JsonType::Object)
    return true;

  if (node->GetObject().count("id") == 0)
    return false;

  uint32_t s_kp_id = static_cast<uint32_t>(node->GetNumber("id"));

  if (node->GetObject().count("links") == 0)
    return true;

  auto link_arr = node->GetArray("links");
  for (const auto& val : link_arr) {
    if (!val || val->GetType() != JsonType::Number)
      continue;

    uint32_t d_kp_id = static_cast<uint32_t>(val->AsNumber());
    links.push_back({s_kp_id, d_kp_id});

    // Recursively check and load the next link in the chain/tree.
    if (!LoadLinks(nodes, d_kp_id, links))
      return false;
  }

  return true;
}

bool Module::LoadConnections(const std::vector<JsonValue::Ptr>& nodes,
                             std::vector<KeypointLinkIds>& connections) {

  for (const auto& node : nodes) {
    if (!node || node->GetType() != JsonType::Object)
      continue;

    const auto& obj = node->GetObject();

    if (obj.count("id") == 0 || obj.count("connection") == 0)
      continue;

    uint32_t label_id = static_cast<uint32_t>(node->GetNumber("id"));
    uint32_t con_id = static_cast<uint32_t>(node->GetNumber("connection"));

    connections.push_back({label_id, con_id});
  }
  return true;
}

void Module::KeypointTransformCoordinates(Keypoint& keypoint,
                                          Region& region) {

  keypoint.x = (keypoint.x - region.x) / region.width;
  keypoint.y = (keypoint.y - region.y) / region.height;
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

    auto nodes = root->GetArray("posenet");

    if (!LoadLinks(nodes, 0, links_))
      return false;

    if (!LoadConnections(nodes, connections_))
      return false;
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  std::vector<RootPoint> rootpoints;

  if (output.type() != typeid(PoseEstimations)) {
    LOG(logger_, kError, "Unexpected output type!");
    return false;
  }

  PoseEstimations& estimations =
      std::any_cast<PoseEstimations&>(output);

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  source_width_ = resolution.width;
  source_height_ = resolution.height;

  // The 4th dimension of 1st tensor represents the number of parts in the pose.
  uint32_t n_parts = tensors[0].dimensions[3];

  // Find the keypoints with highest score for each block inside the heatmap.
  ExtractRootpoints(tensors, rootpoints);

  // Iterate over the root keypoints and build up pose predictions.
  for (uint32_t idx = 0; idx < rootpoints.size(); idx++) {
    RootPoint& rootpoint = rootpoints[idx];
    PoseEstimation entry;

    // Init all keypoints
    for (uint32_t num = 0; num < n_parts; num++) {
      Keypoint kp;
      entry.keypoints.push_back(kp);
    }

    Keypoint& keypoint = entry.keypoints[rootpoint.id];
    keypoint.x = rootpoint.x;
    keypoint.y = rootpoint.y;
    keypoint.confidence = rootpoint.confidence;

    keypoint.name = labels_parser_.GetLabel(rootpoint.id);
    keypoint.color = labels_parser_.GetColor(rootpoint.id);

    entry.confidence = keypoint.confidence / n_parts;

    LOG(logger_, kTrace, "Seed Keypoint: '%s' [%.2f x %.2f], confidence %.2f",
        keypoint.name.c_str(), keypoint.x, keypoint.y, keypoint.confidence);

    // Iterate backwards over the skeleton links to find the seed keypoint.
    TraverseSkeletonLinks(tensors, entry, true);
    // Iterate forward over the skeleton links to find all other keypoints.
    TraverseSkeletonLinks(tensors, entry, false);

    // Non-Max Suppression (NMS) algorithm.
    // If the NMS result is below 0 don't create new pose prediction.
    int32_t nms = NonMaxSuppression(entry, estimations);

    // If the NMS result is -2 don't add the prediction to the list.
    if (nms == (-2)) {
      continue;
    }

    KeypointLinks links;
    for (uint32_t num = 0; num < connections_.size(); num++) {
      KeypointLinkIds& lk = connections_[num];
      links.push_back(
          KeypointLink(entry.keypoints[lk.s_kp_id],
              entry.keypoints[lk.d_kp_id]));
    }
    entry.links = links;

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0)
      estimations.erase(estimations.begin() + nms);

    estimations.push_back(entry);
  }

  // TODO Optimize?
  // Transform coordinates to relative with extracted source aspect ratio.
  for (uint32_t idx = 0; idx < estimations.size(); idx++) {
    PoseEstimation& entry = estimations[idx];

    for (uint32_t num = 0; num < entry.keypoints.size(); num++) {
      Keypoint& keypoint = entry.keypoints[num];
      KeypointTransformCoordinates(keypoint, region);
    }
  }

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
