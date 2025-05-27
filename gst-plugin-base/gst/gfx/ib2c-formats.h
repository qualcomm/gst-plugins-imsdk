/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdint>
#include <map>
#include <tuple>

#include <GLES3/gl32.h>

#include "ib2c.h"

namespace ib2c {

class Format {
 public:
  static std::tuple<uint32_t, uint64_t> ToInternal(uint32_t format, bool aligned);
  static GLenum ToGL(uint32_t format);

  static bool IsRgb(uint32_t format);
  static bool IsYuv(uint32_t format);

  static uint32_t NumChannels(uint32_t format);
  static uint32_t BytesPerChannel(uint32_t format);

  static bool IsInverted(uint32_t format);
  static bool IsSwapped(uint32_t format);
  static bool IsFloat(uint32_t format);
  static bool IsSigned(uint32_t format);

  static uint32_t ColorSpace(uint32_t format);

 private:
  // Tuple of <DRM/GBM Format, Number of Channels, Inverted, Swapped RB>
  typedef std::tuple<uint32_t, uint8_t, bool, bool> RgbColorTuple;

  static const uint32_t kFormatMask;
  static const uint32_t kColorSpaceMask;
  static const uint32_t kPixelTypeMask;

  static const std::map<uint32_t, uint32_t> kYuvColorTable;
  static const std::map<uint32_t, RgbColorTuple> kRgbColorTable;
};

} // namespace ib2c
