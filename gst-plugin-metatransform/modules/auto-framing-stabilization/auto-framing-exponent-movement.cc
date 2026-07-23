/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "auto-framing-exponent-movement.h"
#include "auto-framing-logging.h"

#include <cmath>
#include <vector>

ExponentMovement::ExponentMovement ()
    : curr_rect_({}),
      target_rect_({}),
      first_target_(false),
      speed_((float) AFR_DEFAULT_SPEED_MOVEMENT / 1000),
      frame_index_(0),
      initial_xy_index_(0),
      end_xy_index_(0),
      initial_w_index_(0),
      end_w_index_(0),
      coef_x_(0.0f),
      coef_y_(0.0f),
      coef_w_(0.0f),
      ext_x_(0.0f),
      ext_y_(0.0f),
      ext_w_(0.0f),
      extra_distance_x_(50.0f),
      extra_distance_y_(50.0f),
      extra_distance_w_(50.0f) {
  AFR_LOG_DEBUG ("%s", __func__);
}

void ExponentMovement::SetSpeed (float speed) {
  speed_ = speed / 1000;
}

void ExponentMovement::SetMaxMoveStep (float value) {}

void ExponentMovement::SetOutputResolution (int32_t width, int32_t height) {}

void ExponentMovement::SetTarget (VideoRectangleF target) {
  if (!first_target_) {
    curr_rect_ = target;
    first_target_ = true;
  }

  if (target.x == target_rect_.x &&
      target.y == target_rect_.y &&
      target.w == target_rect_.w) {
    return;
  }

  AFR_LOG_DEBUG ("%s: %f %f %f %f\n", __func__, target.x, target.y,
      target.w, target.h);

  target_rect_ = target;

  double distance_x = abs(target_rect_.x - curr_rect_.x);
  double distance_y = abs(target_rect_.y - curr_rect_.y);

  initial_xy_index_ = frame_index_ - 1;
  end_xy_index_ = initial_xy_index_;
  ext_x_ = 0.0f;
  ext_y_ = 0.0f;

  if (distance_x > distance_y) {
    ext_x_ = extra_distance_x_ * (target_rect_.x - curr_rect_.x) /
        abs(target_rect_.x - curr_rect_.x);
    double target_index = log(((double)target_rect_.x - curr_rect_.x + ext_x_) /
        ext_x_) / speed_;
    end_xy_index_ = (int64_t) ceil(target_index) + initial_xy_index_;

    if (distance_y > 0.0f) {
      ext_y_ = (curr_rect_.y - target_rect_.y) * exp(-speed_ * target_index) /
          (exp(-speed_ * target_index) - 1);
    }
  } else if (distance_x < distance_y) {
    ext_y_ = extra_distance_y_ * (target_rect_.y - curr_rect_.y) /
        abs(target_rect_.y - curr_rect_.y);
    double target_index = log(((double)target_rect_.y - curr_rect_.y + ext_y_) /
        ext_y_) / speed_;
    end_xy_index_ = (int64_t) ceil(target_index) + initial_xy_index_;
    if (distance_y > 0.0f) {
      ext_x_ = (curr_rect_.x - target_rect_.x) * exp(-speed_ * target_index) /
          (exp(-speed_ * target_index) - 1);
    }
  } else {
    if (distance_x > 0.0f) {
      ext_y_ = extra_distance_y_ * (target_rect_.y - curr_rect_.y) /
          abs(target_rect_.y - curr_rect_.y);
      double target_index = log(((double)target_rect_.y - curr_rect_.y + ext_y_) /
          ext_y_) / speed_;
      end_xy_index_ = (int64_t) ceil(target_index) + initial_xy_index_;
      ext_x_ = ext_y_;
    }
  }

  coef_x_ = curr_rect_.x - target_rect_.x - ext_x_;
  coef_y_ = curr_rect_.y - target_rect_.y - ext_y_;

  double distance_w = abs(target_rect_.w - curr_rect_.w);

  initial_w_index_ = frame_index_ - 1;
  end_w_index_ = initial_w_index_;
  ext_w_ = 0.0f;

  if (distance_w > 0.0f) {
    ext_w_ = extra_distance_w_ * (target_rect_.w - curr_rect_.w) /
        abs(target_rect_.w - curr_rect_.w);
    double target_index = log(((double)target_rect_.w - curr_rect_.w + ext_w_) /
        ext_w_) / speed_;
    end_w_index_ = (int64_t) ceil(target_index) + initial_w_index_;
  }
  coef_w_ = curr_rect_.w - target_rect_.w - ext_w_;

  AFR_LOG_DEBUG ("%s coef_x_: %f coef_y_: %f coef_w_: %f speed_: %f " \
                 "ext_x_: %f ext_y_: %f ext_w_: %f " \
                 "initial_xy_index_: %d end_xy_index_: %d " \
                 "initial_w_index_: %d end_w_index_: %d",
      __func__,
      coef_x_, coef_y_, coef_w_, speed_, ext_x_, ext_y_, ext_w_,
      initial_xy_index_, end_xy_index_, initial_w_index_, end_w_index_);
}

VideoRectangleF ExponentMovement::process (VideoRectangleF target) {
  SetTarget (target);
  VideoRectangleF rect{};

  if (frame_index_ < end_xy_index_) {
    rect.x = coef_x_ * exp(-speed_ * (frame_index_ - initial_xy_index_)) +
        target_rect_.x + ext_x_;
  } else {
    rect.x = target_rect_.x;
  }

  if (frame_index_ < end_xy_index_) {
    rect.y = coef_y_ * exp(-speed_ * (frame_index_ - initial_xy_index_)) +
        target_rect_.y + ext_y_;
  } else {
    rect.y = target_rect_.y;
  }

  if (frame_index_ < end_w_index_) {
    rect.w = coef_w_ * exp(-speed_ * (frame_index_ - initial_w_index_)) +
        target_rect_.w + ext_w_;
  } else {
    rect.w = target_rect_.w;
  }

  curr_rect_ = rect;

  frame_index_++;

  AFR_LOG_DEBUG ("Movement rect: %f,%f,%f frame_index_ = %d\n",
      rect.x, rect.y, rect.w, frame_index_);

  return rect;
}
