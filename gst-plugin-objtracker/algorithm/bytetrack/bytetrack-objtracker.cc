/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "objtracker-data.h"
#include "BYTETracker.h"

BYTETracker *tracker;

extern "C" {
  void *TrackerAlgoCreate (std::map<std::string, ParameterType> params);
  std::vector<TrackerAlgoOutputData> TrackerAlgoExecute (void *tracker,
      std::vector<TrackerAlgoInputData> data);
  void TrackerAlgoDelete (void *tracker);
}

void *TrackerAlgoCreate (std::map<std::string, ParameterType> params)
{
  ByteTrackerConfig config;

  if (params.size() == 0) {
    config.frame_rate = 30;
    config.track_buffer = 30;
    config.wh_smooth_factor = 0.9;
    config.track_thresh = 0.5;
    config.high_thresh = 0.6;
  } else {
    std::map<std::string, ParameterType>::iterator it;

    it = params.find("frame-rate");
    if (it != params.end()) {
        config.frame_rate = std::get<int>(it->second);
    } else {
        goto cleanup;
    }
    it = params.find("track-buffer");
    if (it != params.end()) {
        config.track_buffer = std::get<int>(it->second);
    } else {
        goto cleanup;
    }
    it = params.find("wh-smooth-factor");
    if (it != params.end()) {
        config.wh_smooth_factor = std::get<float>(it->second);
    } else {
        goto cleanup;
    }
    it = params.find("track-thresh");
    if (it != params.end()) {
        config.track_thresh = std::get<float>(it->second);
    } else {
        goto cleanup;
    }
    it = params.find("high-thresh");
    if (it != params.end()) {
        config.high_thresh = std::get<float>(it->second);
    } else {
        goto cleanup;
    }
  }

  tracker = new BYTETracker(config);

  return tracker;

cleanup:
  return NULL;
}

std::vector<TrackerAlgoOutputData> TrackerAlgoExecute (void *tracker,
    std::vector<TrackerAlgoInputData> data)
{
  ByteTrackerObject object;
  std::vector<ByteTrackerObject> objects;
  TrackerAlgoOutputData result;
  std::vector<TrackerAlgoOutputData> results;

  for (size_t i = 0; i < data.size(); i++) {
    object.bounding_box[0] = data[i].x;
    object.bounding_box[1] = data[i].y;
    object.bounding_box[2] = data[i].x + data[i].w;
    object.bounding_box[3] = data[i].y + data[i].h;
    object.prob = data[i].prob / 100.0;
    object.label = data[i].detection_id;

    objects.push_back(object);
  }

  std::vector<STrack> stracks = ((BYTETracker *)tracker)->update(objects);

  for (const auto& strack : stracks) {
    if (strack.state == TrackState::Removed)
      continue;

    auto cx = (strack.tlbr[2] + strack.tlbr[0]) / 2;
    auto cy = (strack.tlbr[3] + strack.tlbr[1]) / 2;

    result.x = cx - strack.smoothed_wh[0] / 2;
    result.y = cy - strack.smoothed_wh[1] / 2;
    result.w = strack.smoothed_wh[0];
    result.h = strack.smoothed_wh[1];
    result.matched_detection_id = strack.matched_detection_id;
    result.track_id = strack.track_id;

    results.push_back(result);
  }

  return results;
}

void TrackerAlgoDelete (void *tracker)
{
  if (tracker != NULL)
    delete (BYTETracker *)tracker;
}
