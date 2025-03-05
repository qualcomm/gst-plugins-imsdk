/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_OBJTRACKER_DATA_H__
#define __GST_OBJTRACKER_DATA_H__

#include <vector>
#include <variant>
#include <map>
#include <string>

using ParameterType = std::variant<int, float>;
using ParameterTypeMap = std::map<std::string, ParameterType>;

typedef struct _TrackerAlgoInputData TrackerAlgoInputData;
typedef struct _TrackerAlgoOutputData TrackerAlgoOutputData;

struct _TrackerAlgoInputData {
  float                     x;
  float                     y;
  float                     w;
  float                     h;
  int                       detection_id;
  float                     prob;
};

struct _TrackerAlgoOutputData {
  float                     x;
  float                     y;
  float                     w;
  float                     h;
  int                       matched_detection_id;
  int                       track_id;
};

#endif // __GST_OBJTRACKER_ALGO_H__
