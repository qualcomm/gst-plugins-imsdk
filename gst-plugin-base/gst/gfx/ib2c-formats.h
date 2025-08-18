/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl32.h>

#include "ib2c.h"

namespace ib2c {

enum class PixelType : uint32_t {
  /// The pixels are represented by unsigned integer.
  kUnsigned,
  /// The pixels are represented by signed integer.
  kSigned,
  /// The pixels are represented by float.
  kFloat,
};

class Format {
 public:
  static std::tuple<uint32_t, uint64_t> ToInternal(uint32_t format);
  static GLenum ToGL(uint32_t format);

  static bool IsRgb(uint32_t format);
  static bool IsYuv(uint32_t format);

  static uint32_t NumComponents(uint32_t format);
  static uint32_t BitDepth(uint32_t format);

  static bool IsPlanar(uint32_t format);
  static bool IsInverted(uint32_t format);
  static bool IsSwapped(uint32_t format);

  static bool IsSigned(uint32_t format);
  static bool IsUnsigned(uint32_t format);
  static bool IsFloat(uint32_t format);

  static uint32_t ColorSpace(uint32_t format);
  static uint32_t ToYuvColor(uint32_t color, uint32_t colorspace);

 private:
  struct RgbInfo {
    /// How pixel bits are represented in memory.
    PixelType pixtype;
    /// Number of components per pixel.
    uint8_t   n_components;
    /// Bit depth per channel.
    uint8_t   bitdepth;
    /// Whether alpha channel is first in the pixel arrangement (e.g. ARGB).
    bool      inverted;
    /// Whether R abd B channel have swapped positions (e.g. BGR).
    bool      swapped;
    /// Whether is planar format
    bool      planar;
  };

  // Tuple of <DRM/GBM Format, Information for the RGB format>
  typedef std::tuple<uint32_t, RgbInfo> RgbFormatTuple;
  // Coeffcients for red, green and blue channels.
  typedef std::tuple<float, float, float> ColorCoeffcients;

  static const uint32_t kFormatMask;
  static const uint32_t kColorSpaceMask;

  static const std::map<uint32_t, uint32_t> kYuvFormatTable;
  static const std::map<uint32_t, RgbFormatTuple> kRgbFormatTable;

  static const std::map<uint32_t, ColorCoeffcients> kColorSpaceCoefficients;
};

} // namespace ib2c
