/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "auto-framing-alg.h"
#include "auto-framing-filters.h"
#include "auto-framing-basic-movement.h"
#include "auto-framing-exponent-movement.h"
#include "auto-framing-logging.h"

#include <cmath>
#include <memory>

#define AFR_MIN_FILTER_HISTORY_FRAMES 11

#define AFR_WIDTH_4K 3840.0
#define AFR_N_AVERAGE_FILTERS 1

// Contains tracking camera process parameters
typedef struct _AutoFramingProcess AutoFramingProcess;
struct _AutoFramingProcess
{
  // Store the last received from filter bounding box
  VideoRectangleF target_rect;

  // Parameter for start procesing from the first rect
  bool first_rect_start;

  // Maximum allowed crop width. Maximum height is calculated by aspec ratio.
  uint32_t max_crop_w;

  // Flag indicate that fisrt rect is received
  bool first_rect_received;

  std::unique_ptr<MedianFilter> median_filter;
  std::unique_ptr<ThresholdFilter> threshold_filter;
  std::unique_ptr<AverageFilter> average_filter;
  std::unique_ptr<AverageFilter> average_filter1;
  std::unique_ptr<Movement> movement;
};

struct _AutoFramingAlgo
{
  AutoFramingConfig config;
  AutoFramingProcess process;
};

void clip_to_full_fov_center_xy (VideoRectangleF &coords,
  const uint32_t fov_width, const uint32_t fov_height)
{
  float aspect_ratio = (float) fov_width / fov_height;

  // Do not allow negative position value
  if (coords.x < coords.w / 2) {
    coords.x = coords.w / 2;
  }
  if (coords.y < coords.h / 2) {
    coords.y = coords.h / 2;
  }

  // Do not go outside of the width of the rectangle
  if (coords.x + coords.w / 2 > fov_width) {
    coords.x -= coords.x + coords.w / 2 - fov_width;
    if (coords.x < coords.w / 2) {
      coords.w -= coords.w / 2 - coords.x;
      coords.x = coords.w / 2;
      coords.h = coords.w / aspect_ratio;
    }
  }

  // Do not go outside of the height of the rectangle
  if (coords.y + coords.h / 2 > fov_height) {
    coords.y -= coords.y + coords.h / 2 - fov_height;
    if (coords.y < coords.h / 2) {
      coords.h -= coords.h / 2 - coords.y;
      coords.y = coords.h / 2;
      coords.w = coords.h * aspect_ratio;
    }
  }
}

void clip_to_full_fov_start_xy (VideoRectangleF &coords,
  const uint32_t fov_width, const uint32_t fov_height)
{
  float aspect_ratio = (float) fov_width / fov_height;

  // Do not allow negative position value
  if (coords.x < 0.0f) {
    coords.x = 0.0f;
  }
  if (coords.y < 0.0f) {
    coords.y = 0.0f;
  }

  // Do not go outside of the width of the rectangle
  if (coords.x + coords.w > fov_width) {
    coords.x -= coords.x + coords.w - fov_width;
    if (coords.x < 0.0f) {
      coords.w -= fabs(coords.x);
      coords.x = 0;
      coords.h = coords.w / aspect_ratio;
    }
  }

  // Do not go outside of the height of the rectangle
  if (coords.y + coords.h > fov_height) {
    coords.y -= coords.y + coords.h - fov_height;
    if (coords.y < 0) {
      coords.h -= fabs(coords.y);
      coords.y = 0;
      coords.w = coords.h * aspect_ratio;
    }
  }
}

VideoRectangle
auto_framing_algo_process (AutoFramingAlgo * inst, VideoRectangle * rect)
{
  VideoRectangle output = {};
  VideoRectangleF output_f = auto_framing_algo_float_process (inst, rect);

  output.x = ceil (output_f.x);
  output.y = ceil (output_f.y);
  output.w = ceil (output_f.w);
  output.h = ceil (output_f.h);

  return output;
}

VideoRectangleF
auto_framing_algo_float_process (AutoFramingAlgo * inst, VideoRectangleF * rect)
{
  VideoRectangleF output = {};

  if (!inst) {
    // Error
    return output;
  }

  float aspect_ratio =
      (float) inst->config.out_width /
      (float) inst->config.out_height;

  // Check if there is received a valid rect data
  // Otherwise move smoothly to the
  // last bounding box received (if not already done)
  if (rect) {
    // Align the crop dimensions based on the input size
    float w_coef =
        (float) inst->config.out_width / (float) inst->config.in_width;
    float h_coef =
        (float) inst->config.out_height / (float) inst->config.in_height;

    rect->x *= w_coef;
    rect->y *= h_coef;
    rect->w *= w_coef;
    rect->h *= h_coef;

    // Clip margin to full FOV
    if (rect->x < 0) {
      rect->x = 0;
    }
    if (rect->w + rect->x > inst->config.out_width) {
      rect->w = inst->config.out_width - rect->x;
    }
    if (rect->h + rect->y > inst->config.out_height) {
      rect->h = inst->config.out_height - rect->y;
    }

    // Calculate the center of the rect before aspect ratio correction
    rect->x += rect->w / 2;
    rect->y += rect->h / 2;

    // Calculate the aspect ratio to follow the original aspect ratio
    if ((rect->w / rect->h) < aspect_ratio) {
      rect->w = rect->h * aspect_ratio;
    } else {
      rect->h = rect->w / aspect_ratio;
    }

    // Use the first rect as a starting point if configured
    // Do not start form full FOV
    if (inst->process.first_rect_start && !inst->process.first_rect_received) {
      inst->process.movement->SetTarget (*rect);
    } else if (!inst->process.first_rect_received) {
      inst->process.movement->SetTarget (inst->process.target_rect);
    }
    inst->process.first_rect_received = true;

    // Apply median filter
    VideoRectangleF coords = inst->process.median_filter->process (*rect);

    // Apply treshold filter
    coords = inst->process.threshold_filter->process (coords);

    // Apply average filter
    coords = inst->process.average_filter->process (coords);

    // Limit maximum crop/zoom
    if (coords.w < inst->process.max_crop_w) {
      coords.w = inst->process.max_crop_w;
      coords.h = inst->process.max_crop_w / aspect_ratio;
    }

    // Output could go out of FOV becuase of margin. We have to clip it
    // before movement estimation to ensure proper destination crop window.
    clip_to_full_fov_center_xy (coords, inst->config.out_width,
        inst->config.out_height);

    inst->process.target_rect.x = coords.x;
    inst->process.target_rect.y = coords.y;
    inst->process.target_rect.w = coords.w;
    inst->process.target_rect.h = coords.h;
  }

  // Movement process
  VideoRectangleF curr_rect = inst->process.movement->process (inst->process.target_rect);

  curr_rect = inst->process.average_filter1->process (curr_rect);

  // Apply same aspect ratio
  curr_rect.h = curr_rect.w / aspect_ratio;

  // Get the crop coordinates from current rectangle
  output.w = curr_rect.w;
  output.h = output.w / aspect_ratio;
  output.x = curr_rect.x - (output.w / 2);
  output.y = curr_rect.y - (output.h / 2);

  // Output could go slightly out of FOV becuase X/Y and size speed migth
  // go out of sync because or rounding.
  clip_to_full_fov_start_xy (output, inst->config.out_width,
      inst->config.out_height);

  AFR_LOG_DEBUG ("%s output: %f,%f,%f,%f\n", __func__,
      output.x, output.y, output.w, output.h);


  return output;
}

VideoRectangleF
auto_framing_algo_float_process (AutoFramingAlgo * inst, VideoRectangle * rect) {
  VideoRectangleF rectf;

  if (!rect)
    return auto_framing_algo_float_process(inst, (VideoRectangleF*)NULL);

  rectf.x = static_cast<float>(rect->x);
  rectf.y = static_cast<float>(rect->y);
  rectf.w = static_cast<float>(rect->w);
  rectf.h = static_cast<float>(rect->h);
  return auto_framing_algo_float_process (inst, &rectf);
}

void
auto_framing_algo_set_filter_size (AutoFramingAlgo * inst, uint32_t size)
{
  if (!inst) {
    // Error
    return;
  }

  int32_t filter_size = (size / 2) * 2 + 1;
  if (filter_size < AFR_MIN_FILTER_HISTORY_FRAMES) {
    filter_size = AFR_MIN_FILTER_HISTORY_FRAMES;
  }
  inst->process.median_filter->SetSize (filter_size);
}

void
auto_framing_algo_set_filter_average_size (AutoFramingAlgo * inst,
    uint32_t size)
{
  if (!inst) {
    // Error
    return;
  }
  inst->process.average_filter->SetSize (size);
}

void
auto_framing_algo_set_position_threshold (AutoFramingAlgo * inst,
    uint32_t threshold)
{
  if (!inst) {
    // Error
    return;
  }
  inst->process.threshold_filter->SetPosThreshold (threshold);
}

void
auto_framing_algo_set_dims_threshold (AutoFramingAlgo * inst, uint32_t threshold)
{
  if (!inst) {
    // Error
    return;
  }
  inst->process.threshold_filter->SetSizeThreshold (threshold);
}

void
auto_framing_algo_set_position_moving_threshold (AutoFramingAlgo * inst,
    uint32_t percent)
{
  if (!inst) {
    // Error
    return;
  }
}

void
auto_framing_algo_set_dims_moving_threshold (AutoFramingAlgo * inst,
    uint32_t percent)
{
  if (!inst) {
    // Error
    return;
  }
}

void
auto_framing_algo_set_movement_speed (AutoFramingAlgo * inst, uint32_t speed)
{
  if (!inst) {
    // Error
    return;
  }
  inst->process.movement->SetSpeed (speed);
}

void
auto_framing_algo_set_max_move_step (AutoFramingAlgo * inst, uint32_t step)
{
  if (!inst) {
    // Error
    return;
  }
  float max_move_step =
      (float)inst->config.out_width * step / AFR_WIDTH_4K;
  inst->process.movement->SetMaxMoveStep (max_move_step);
}

void
auto_framing_algo_set_max_crop (AutoFramingAlgo * inst, float ratio)
{
  if (!inst) {
    // Error
    return;
  }

  inst->process.max_crop_w =
      ((uint32_t)(inst->config.out_width / ratio)) & ~0x1;
}

void
auto_framing_algo_set_first_rect_start (AutoFramingAlgo * inst, bool enable)
{
  if (!inst) {
    // Error
    return;
  }

  inst->process.first_rect_start = enable;
}

AutoFramingAlgo *
auto_framing_algo_new (AutoFramingConfig config)
{
  AutoFramingAlgo *inst = new AutoFramingAlgo();
  if (!inst) {
    // Error
    return NULL;
  }

  inst->config = config;

  float max_move_step =
      (float)inst->config.out_width * AFR_DEFAULT_MAX_MOVE_STEP / AFR_WIDTH_4K;

  inst->process.target_rect.x = inst->config.out_width / 2;
  inst->process.target_rect.y = inst->config.out_height / 2;
  inst->process.target_rect.w = inst->config.out_width;
  inst->process.target_rect.h = inst->config.out_height;

  inst->process.max_crop_w =
      ((uint32_t)(inst->config.out_width / AFR_DEFAULT_MAX_CROP_RATIO)) & ~0x1;


  inst->process.median_filter = std::make_unique<MedianFilter>(
    (AFR_DEFAULT_FILTER_SIZE / 2) * 2 + 1
  );

  inst->process.threshold_filter = std::make_unique<ThresholdFilter>(
    AFR_DEFAULT_POS_THRESHOLD,
    AFR_DEFAULT_SIZE_THRESHOLD
  );

  inst->process.average_filter = std::make_unique<AverageFilter>(
    AFR_FILTER_AVERAGE_SIZE
  );

  inst->process.average_filter1 = std::make_unique<AverageFilter>(20);

  inst->process.movement = std::make_unique<ExponentMovement>();

  inst->process.movement->SetSpeed (AFR_DEFAULT_SPEED_MOVEMENT);
  inst->process.movement->SetMaxMoveStep (
      (float)inst->config.out_width * AFR_DEFAULT_MAX_MOVE_STEP / AFR_WIDTH_4K);
  inst->process.movement->SetOutputResolution (config.out_width, config.out_height);

  return inst;
}

void
auto_framing_algo_free (AutoFramingAlgo * inst)
{
  if (inst) {
    delete inst;
  }
}
