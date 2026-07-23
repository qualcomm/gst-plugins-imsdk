/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "auto-framing-movement-interface.h"

class BasicMovement : public Movement {
 public:
  BasicMovement ();
  void SetSpeed (float speed) override;
  void SetTarget (VideoRectangleF target) override;
  VideoRectangleF process (VideoRectangleF rect) override;

  void SetMaxMoveStep (float max_move_step) override;
  void SetOutputResolution (int32_t width, int32_t height) override;

 private:
  void CalcNextRect (VideoRectangleF &curr_rect);
  void StabilizeOutput ();

  VideoRectangleF curr_rect_;
  VideoRectangleF target_rect_;
  bool first_target_ = false;

  // Parameter for the speed of movement to the final crop rectangle
  float speed_movement_;

  // Parameter for max posible step in pixels while movement
  float max_move_step_;

  int32_t out_width_;
  int32_t out_height_;

  float extra_distance_x_;
  float extra_distance_y_;
  float extra_distance_w_;

  // Store paramters from previous frame. Needed for zig-zag stabilzation.
  VideoRectangleF priv_rect_;

  int32_t prv_step_x_;
  int32_t prv_step_y_;
  int32_t prv_step_w_;
};
