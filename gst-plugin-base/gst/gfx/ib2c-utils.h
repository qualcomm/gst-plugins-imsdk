/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <utility>
#include <sstream>
#include <exception>
#include <chrono>
#include <iomanip>

#include <GLES3/gl32.h>

#include "ib2c.h"

namespace ib2c {

#define EXTRACT_RED_COLOR(color)   (((color >> 24) & 0xFF) / 255.0)
#define EXTRACT_GREEN_COLOR(color) (((color >> 16) & 0xFF) / 255.0)
#define EXTRACT_BLUE_COLOR(color)  (((color >> 8) & 0xFF) / 255.0)
#define EXTRACT_ALPHA_COLOR(color) (((color) & 0xFF) / 255.0)

#define EXCEPTION_IF_GL_ERROR(...)                                   \
do {                                                                 \
  GLenum error = glGetError();                                       \
                                                                     \
  if (error != GL_NO_ERROR)                                          \
    throw Exception(__VA_ARGS__, ", error: ", std::hex, error, "!"); \
} while (false)

#define RETURN_IF_GL_ERROR(...)                                   \
do {                                                              \
  GLenum error = glGetError();                                    \
                                                                  \
  if (error != GL_NO_ERROR)                                       \
    return Error(__VA_ARGS__, ", error: ", std::hex, error, "!"); \
} while (false)

template<typename ...Args> void Log(Args&&... args) {

  std::stringstream s;

  const auto timestamp = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(timestamp);
  const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
      timestamp.time_since_epoch()) % 1000000;

  s << std::put_time(std::localtime(&time), "%a %b %d %Y %T") << '.'
    << std::setfill('0') << std::setw(6) << microseconds.count() << ": ";
  ((s << std::forward<Args>(args)), ...) << std::endl;

  std::cout << s.str();
}

template<typename ...Args> std::runtime_error Exception(Args&&... args) {

  std::stringstream s;
  ((s << std::forward<Args>(args)), ...);
  return std::runtime_error(s.str());
}

template<typename ...Args> std::string Error(Args&&... args) {

  std::stringstream s;
  ((s << std::forward<Args>(args)), ...);
  return s.str();
}

// Return the stride alignment requirement in bytes.
uint32_t GetAlignment();

// Convert RGB color code to YUV color code.
uint32_t ToYuvColorCode(uint32_t color, uint32_t standard);

} // namespace ib2c
