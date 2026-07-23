/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "auto-framing-basic-movement.h"
#include "auto-framing-logging.h"

#include <cmath>
#include <vector>

#define AFR_DEFAULT_NIN_STEP 800.0 // 0.8 * 100
#define AFR_WIDTH_4K 3840.0

BasicMovement::BasicMovement ()
    : curr_rect_({}),
      target_rect_({}),
      first_target_(false),
      speed_movement_(0.0f),
      max_move_step_(0.0f),
      out_width_(0),
      out_height_(0),
      extra_distance_x_(0.0f),
      extra_distance_y_(0.0f),
      extra_distance_w_(0.0f),
      priv_rect_({}),
      prv_step_x_(0),
      prv_step_y_(0),
      prv_step_w_(0) {
  AFR_LOG_DEBUG ("%s", __func__);
}

void BasicMovement::SetSpeed (float speed) {
  speed_movement_ = speed;
}

void BasicMovement::SetMaxMoveStep (float value) {
  max_move_step_ = value;
}

void BasicMovement::SetOutputResolution (int32_t width, int32_t height) {
  out_width_ = width;
  out_height_ = height;
}

void BasicMovement::SetTarget (VideoRectangleF target) {
  if (!first_target_) {
    curr_rect_ = target;
    first_target_ = true;
  }

  if (target.x == target_rect_.x &&
      target.y == target_rect_.y &&
      target.w == target_rect_.w) {
    return;
  }
  target_rect_ = target;
}

void BasicMovement::CalcNextRect (VideoRectangleF &curr_rect) {
  // Calculated common max speed correction for X and Y to ensure
  // directional movement.
  float speed_ratio_correction = 1.0;

  // Calculate the next X movement of the crop based on
  // the speed movement parameter
  float reminding_distance = extra_distance_x_ +
      abs (target_rect_.x - curr_rect.x);
  float move_val_x = reminding_distance * speed_movement_ / 1000.0;
  if (move_val_x > max_move_step_) {
    float ratio = (float)move_val_x / max_move_step_;
    speed_ratio_correction = std::max(speed_ratio_correction, ratio);
  }

  // Calculate the next Y movement of the crop based on
  // the speed movement parameter
  reminding_distance = extra_distance_y_ +
      abs (target_rect_.y - curr_rect.y);
  float move_val_y = reminding_distance * speed_movement_ / 1000.0;
  if (move_val_y > max_move_step_) {
    float ratio = (float)move_val_y / max_move_step_;
    speed_ratio_correction = std::max(speed_ratio_correction, ratio);
  }

  // Calculate the next WIDTH movement of the crop based on
  // the speed movement parameter
  reminding_distance = extra_distance_w_ +
      abs (target_rect_.w - curr_rect.w);
  float move_val = reminding_distance * speed_movement_ / 1000.0;
  if (move_val > max_move_step_ * 2.0) {
    float ratio = (float)move_val / (max_move_step_ * 2.0);
    speed_ratio_correction = std::max(speed_ratio_correction, ratio);
  }

  if (target_rect_.x > curr_rect.x) {
    curr_rect.x += move_val_x / speed_ratio_correction;
  } else if (target_rect_.x < curr_rect.x) {
    curr_rect.x -= move_val_x / speed_ratio_correction;
  }

  if (target_rect_.y > curr_rect.y) {
    curr_rect.y += move_val_y / speed_ratio_correction;
  } else if (target_rect_.y < curr_rect.y) {
    curr_rect.y -= move_val_y / speed_ratio_correction;
  }

  if (target_rect_.w > curr_rect.w) {
    curr_rect.w += move_val / speed_ratio_correction;
  } else if (target_rect_.w < curr_rect.w) {
    curr_rect.w -= move_val / speed_ratio_correction;
  }
}

void BasicMovement::StabilizeOutput ()
{
  VideoRectangleF curr_rect;
  int32_t cur_step;
  int32_t tmp_step;
  int32_t prv_position;

  //////////////////////////// X Stabilization ////////////////////////////

  curr_rect = curr_rect_;
  cur_step = abs (ceil (priv_rect_.x) - ceil (curr_rect_.x));
  tmp_step = cur_step;
  prv_position = ceil (curr_rect.x);

  // check if next step is bigger then previous because of rounding
  if (prv_step_x_ < cur_step && cur_step - prv_step_x_ == 1) {
    if (target_rect_.x > curr_rect_.x) {
      curr_rect_.x -= cur_step - prv_step_x_;
      cur_step = prv_step_x_;
    } else {
      curr_rect.x += cur_step - prv_step_x_;
      cur_step = prv_step_x_;
    }
  }

  while (prv_step_x_ > cur_step &&
          abs (prv_step_x_ - tmp_step) <= 3 &&
          abs (target_rect_.x - curr_rect.x) > 1.5) {
    CalcNextRect (curr_rect);

    tmp_step = abs ((int32_t) ceil (curr_rect.x) - prv_position);
    prv_position = ceil (curr_rect.x);

    if (prv_step_x_ == tmp_step) {
      // same step is found leater in sequece
      if (target_rect_.x > curr_rect_.x) {
        curr_rect_.x += abs (cur_step - tmp_step);
      } else {
        curr_rect_.x -= abs (cur_step - tmp_step);
      }
      cur_step = tmp_step;
      break;
    }

    if (abs (prv_step_x_ - tmp_step) <
        abs (prv_step_x_ - cur_step)) {
      // closer step in found later in sequece
      if (target_rect_.x > curr_rect_.x) {
        curr_rect_.x += abs (cur_step - tmp_step);
      } else {
        curr_rect_.x -= abs (cur_step - tmp_step);
      }
      cur_step = tmp_step;
    }
  }
  prv_step_x_ = cur_step;

  //////////////////////////// Y Stabilization ////////////////////////////

  curr_rect = curr_rect_;
  cur_step = abs (ceil (priv_rect_.y) - ceil (curr_rect_.y));
  tmp_step = cur_step;
  prv_position = ceil (curr_rect.y);

  // check if next step is bigger then previous because of rounding
  if (prv_step_y_ < cur_step && cur_step - prv_step_y_ == 1) {
    if (target_rect_.y > curr_rect_.y) {
      curr_rect_.y -= cur_step - prv_step_y_;
      cur_step = prv_step_y_;
    } else {
      curr_rect_.y += cur_step - prv_step_y_;
      cur_step = prv_step_y_;
    }
  }

  while (prv_step_y_ > cur_step &&
          abs (prv_step_y_ - tmp_step) <= 3 &&
          abs (target_rect_.y - curr_rect.y) > 1.5) {
    CalcNextRect (curr_rect);

    tmp_step = abs ((int32_t) ceil (curr_rect.y) - prv_position);
    prv_position = ceil (curr_rect.y);

    if (prv_step_y_ == tmp_step) {
      // same step is found leater in sequece
      if (target_rect_.y > curr_rect_.y) {
        curr_rect_.y += abs (cur_step - tmp_step);
      } else {
        curr_rect_.y -= abs (cur_step - tmp_step);
      }
      cur_step = tmp_step;
      break;
    }

    if (abs (prv_step_y_ - tmp_step) <
        abs (prv_step_y_ - cur_step)) {
      // closer step in found later in sequece
      if (target_rect_.y > curr_rect_.y) {
        curr_rect_.y += abs (cur_step - tmp_step);
      } else {
        curr_rect_.y -= abs (cur_step - tmp_step);
      }
      cur_step = tmp_step;
    }
  }
  prv_step_y_ = cur_step;

  //////////////////////////// W Stabilization ////////////////////////////

  curr_rect = curr_rect_;
  cur_step = abs (ceil (priv_rect_.w) - ceil (curr_rect_.w));
  tmp_step = cur_step;
  prv_position = (int32_t) ceil (curr_rect.w);

  // check if next step is bigger then previous because of rounding
  if (prv_step_w_ < cur_step && cur_step - prv_step_w_ == 1) {
    if (target_rect_.w > curr_rect_.w) {
      curr_rect_.w -= cur_step - prv_step_w_;
      cur_step = prv_step_w_;
    } else {
      curr_rect.w += cur_step - prv_step_w_;
      cur_step = prv_step_w_;
    }
  }

  while (prv_step_w_ > cur_step &&
          abs (prv_step_w_ - tmp_step) <= 2 &&
          abs (target_rect_.w - curr_rect.w) > 1.5) {
    CalcNextRect (curr_rect);

    tmp_step = abs ((int32_t) ceil (curr_rect.w) - prv_position);
    prv_position = (int32_t) ceil (curr_rect.w);

    if (prv_step_w_ == tmp_step) {
      // same step is found leater in sequece
      if (target_rect_.w > curr_rect_.w) {
        curr_rect_.w += abs (cur_step - tmp_step);
      } else {
        curr_rect_.w -= abs (cur_step - tmp_step);
      }
      cur_step = tmp_step;
      break;
    }

    if (abs (prv_step_w_ - tmp_step) <
        abs (prv_step_w_ - cur_step)) {
      // closer step in found later in sequece
      if (target_rect_.w > curr_rect_.w) {
        curr_rect_.w += abs (cur_step - tmp_step);
      } else {
        curr_rect_.w -= abs (cur_step - tmp_step);
      }
      cur_step = tmp_step;
    }
  }
  prv_step_w_ = cur_step;

  priv_rect_ = curr_rect_;
}

VideoRectangleF BasicMovement::process (VideoRectangleF target) {
  SetTarget (target);

  // Calculate extra distance to avoid the slowest movement at the end.
  // Maximum extra value is calculated in such way to avoid steps less then
  // half pixel. But extra value must be proportional to distsatence to
  // target in each direction to ensure directional movement.

  // Calcutlate max extra distance based on speed
  float extra_path = AFR_DEFAULT_NIN_STEP / speed_movement_;

  // Ajust to resolution
  extra_path *= (float)out_width_ / AFR_WIDTH_4K;

  // Calculated distance to target in each direction
  float dist_x = abs (target_rect_.x - curr_rect_.x);
  float dist_y = abs (target_rect_.y - curr_rect_.y);
  float dist_w = abs (target_rect_.w - curr_rect_.w);

  // Apply proportional extra distance
  if (dist_x >= dist_y && dist_x >= dist_w) {
    extra_distance_x_ = extra_path;
    extra_distance_y_ = extra_path * dist_y / dist_x;
    extra_distance_w_ = extra_path * dist_w / dist_x;
  } else if (dist_y >= dist_x && dist_y >= dist_w) {
    extra_distance_y_ = extra_path;
    extra_distance_x_ = extra_path * dist_x / dist_y;
    extra_distance_w_ = extra_path * dist_w / dist_y;
  } else {
    extra_distance_w_ = extra_path;
    extra_distance_y_ = extra_path * dist_y / dist_w;
    extra_distance_x_ = extra_path * dist_x / dist_w;
  }

  // Calculated crop for the next frame. It updates curr_rect.
  CalcNextRect (curr_rect_);

  // Reduce zig-zag movement
  StabilizeOutput ();

  // If distance to target is less then 1.5 pixel jump immediatly to target.
  // Calculate this at the end because otherwise it impact current step
  // instaed of next step.
  if (abs (target_rect_.x - curr_rect_.x) < 1.5) {
    curr_rect_.x = target_rect_.x;
    extra_distance_x_ = 0;
  }
  if (abs (target_rect_.y - curr_rect_.y) < 1.5) {
    curr_rect_.y = target_rect_.y;
    extra_distance_y_ = 0;
  }
  if (abs (target_rect_.w - curr_rect_.w) < 1.5) {
    curr_rect_.w = target_rect_.w;
    extra_distance_w_ = 0;
  }

  AFR_LOG_DEBUG ("Movement rect: %f,%f,%f\n",
      curr_rect_.x, curr_rect_.y, curr_rect_.w);

  return curr_rect_;
}
