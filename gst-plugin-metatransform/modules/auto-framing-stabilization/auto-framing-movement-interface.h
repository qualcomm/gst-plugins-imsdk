/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "auto-framing-alg.h"

class Movement {
 public:
  virtual ~Movement() {};
  virtual void SetSpeed (float speed) = 0;
  virtual void SetTarget (VideoRectangleF target) = 0;
  virtual void SetMaxMoveStep (float max_move_step) = 0;
  virtual void SetOutputResolution (int32_t width, int32_t height) = 0;

  virtual VideoRectangleF process (VideoRectangleF rect) = 0;

  void SetTarget (VideoRectangle target) {
    VideoRectangleF rect;
    rect.x = static_cast<float>(target.x);
    rect.y = static_cast<float>(target.y);
    rect.w = static_cast<float>(target.w);
    rect.h = static_cast<float>(target.h);
    SetTarget(rect);
  }
};
