/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __AUTO_FRAMING_H__
#define __AUTO_FRAMING_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AFR_DEFAULT_FILTER_SIZE 240
#define AFR_FILTER_AVERAGE_SIZE 48
#define AFR_DEFAULT_POS_THRESHOLD 6
#define AFR_DEFAULT_SIZE_THRESHOLD 6
#define AFR_DEFAULT_POS_MOVING_THRESHOLD_PERCENT 5
#define AFR_DEFAULT_SIZE_MOVING_THRESHOLD_PERCENT 5
#define AFR_DEFAULT_SPEED_MOVEMENT 30
#define AFR_DEFAULT_MAX_MOVE_STEP 20
#define AFR_DEFAULT_MAX_CROP_RATIO 2
#define AFR_DEFAULT_FIRST_RECT_START false

typedef struct _AutoFramingAlgo AutoFramingAlgo;
typedef struct _AutoFramingConfig AutoFramingConfig;
typedef struct _VideoRectangle VideoRectangle;
typedef struct _VideoRectangleF VideoRectangleF;

// Contains tracking camera configuration parameters
struct _AutoFramingConfig
{
  // Output stream dimensions
  int32_t out_width;
  int32_t out_height;

  // Input stream dimensions
  int32_t in_width;
  int32_t in_height;
};

struct _VideoRectangle
{
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
};

struct _VideoRectangleF
{
  float x;
  float y;
  float w;
  float h;
};

// Algorithm process execute
VideoRectangle
auto_framing_algo_process                 (AutoFramingAlgo * inst,
                                           VideoRectangle * rect);

// Algorithm process execute
VideoRectangleF
auto_framing_algo_float_process           (AutoFramingAlgo * inst,
                                           VideoRectangle * rect);

// Set filter size
void
auto_framing_algo_set_filter_size         (AutoFramingAlgo * inst,
                                           uint32_t size);

// Set filter average size
void
auto_framing_algo_set_filter_average_size (AutoFramingAlgo * inst,
                                           uint32_t size);

// Set position static threshold
void
auto_framing_algo_set_position_threshold  (AutoFramingAlgo * inst,
                                           uint32_t threshold);

// Set dimension static threshold
void
auto_framing_algo_set_dims_threshold      (AutoFramingAlgo * inst,
                                           uint32_t threshold);

// Set position moving threshold
void
auto_framing_algo_set_position_moving_threshold(AutoFramingAlgo * inst,
                                           uint32_t percent);

// Set dimension moving threshold
void
auto_framing_algo_set_dims_moving_threshold(AutoFramingAlgo * inst,
                                           uint32_t percent);

// Set the speed of movement to the final crop rectangle
void
auto_framing_algo_set_movement_speed      (AutoFramingAlgo * inst,
                                           uint32_t speed);

// Set the maximum movement step in pixels
void
auto_framing_algo_set_max_move_step       (AutoFramingAlgo * inst,
                                           uint32_t step);

// Set the maximum crop/zoom in
void
auto_framing_algo_set_max_crop            (AutoFramingAlgo * inst,
                                           float ratio);

// Set start form first rect
void
auto_framing_algo_set_first_rect_start    (AutoFramingAlgo * inst,
                                           bool enable);

// Initialization of the algorithm
AutoFramingAlgo *
auto_framing_algo_new                     (AutoFramingConfig config);

// Deinitialization of the algorithm
void
auto_framing_algo_free                    (AutoFramingAlgo * inst);

#ifdef __cplusplus
}
#endif

#endif // __AUTO_FRAMING_H__
