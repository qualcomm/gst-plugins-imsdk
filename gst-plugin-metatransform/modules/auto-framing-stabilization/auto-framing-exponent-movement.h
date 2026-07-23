/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "auto-framing-movement-interface.h"

class ExponentMovement : public Movement {
 public:
  ExponentMovement ();
  void SetSpeed (float speed) override;
  void SetTarget (VideoRectangleF target) override;
  VideoRectangleF process (VideoRectangleF rect) override;

  void SetMaxMoveStep (float max_move_step) override;
  void SetOutputResolution (int32_t width, int32_t height) override;

 private:
  VideoRectangleF curr_rect_;
  VideoRectangleF target_rect_;
  bool first_target_ = false;
  float speed_;
  int64_t frame_index_;
  int64_t initial_xy_index_;
  int64_t end_xy_index_;
  int64_t initial_w_index_;
  int64_t end_w_index_;
  float coef_x_;
  float coef_y_;
  float coef_w_;
  float ext_x_;
  float ext_y_;
  float ext_w_;
  float extra_distance_x_;
  float extra_distance_y_;
  float extra_distance_w_;
};
