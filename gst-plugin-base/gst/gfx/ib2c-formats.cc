/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <drm/drm_fourcc.h>

//TODO: Workaround due to Adreno not using the proper DRM formats.
#define fourcc_mod_code_qti(vendor, val) \
    ((((uint32_t)DRM_FORMAT_MOD_VENDOR_## vendor) << 28) | (val & 0x0fffffffULL))

#if defined(DRM_FORMAT_ABGR16161616F) && !defined(DRM_FORMAT_BGR161616F)
#undef DRM_FORMAT_ABGR16161616F
#endif // DRM_FORMAT_ABGR16161616F && !(DRM_FORMAT_BGR161616F)

#if !defined(DRM_FORMAT_ABGR16161616F)
#define DRM_FORMAT_ABGR16161616F  fourcc_mod_code_qti(QCOM, 54)
#endif // DRM_FORMAT_ABGR16161616F

#if !defined(DRM_FORMAT_BGR161616F)
#define DRM_FORMAT_BGR161616F     fourcc_mod_code_qti(QCOM, 55)
#endif // DRM_FORMAT_BGR161616F

#if !defined(DRM_FORMAT_ABGR32323232F)
#define DRM_FORMAT_ABGR32323232F  fourcc_mod_code_qti(QCOM, 56)
#endif // DRM_FORMAT_ABGR32323232F

#if !defined(DRM_FORMAT_BGR323232F)
#define DRM_FORMAT_BGR323232F     fourcc_mod_code_qti(QCOM, 57)
#endif // DRM_FORMAT_BGR323232F

#include "ib2c-formats.h"
#include "ib2c-utils.h"

namespace ib2c {

const uint32_t Format::kFormatMask = 0xFF;
const uint32_t Format::kColorSpaceMask = (0b11 << 9);

// ColorFormat + PixelType mask and their corresponding tuple of:
// <DRM/GBM Format, Pixel Type, Number of Channels, Bit depth per Channel, Inverted, Swapped RB>
const std::map<uint32_t, Format::RgbFormatTuple> Format::kRgbFormatTable = {
    { ColorFormat::kGRAY8,
        { DRM_FORMAT_R8,
            { PixelType::kUnsigned, 1,   8, false, false } } },
    { ColorFormat::kGRAY8I,
        { DRM_FORMAT_R8,
            { PixelType::kSigned,   1,   8, false, false } } },
    { ColorFormat::kRG88,
        { DRM_FORMAT_GR88,
            { PixelType::kUnsigned, 2,   8, false, false } } },
    { ColorFormat::kGR88,
        { DRM_FORMAT_GR88,
            { PixelType::kUnsigned, 2,   8, false, true  } } },
    { ColorFormat::kRGB888,
        { DRM_FORMAT_BGR888,
            { PixelType::kUnsigned, 3,   8, false, false } } },
    { ColorFormat::kRGB888I,
        { DRM_FORMAT_BGR888,
            { PixelType::kSigned,   3,   8, false, false } } },
    { ColorFormat::kRGB161616F,
        { DRM_FORMAT_BGR161616F,
            { PixelType::kFloat,    3,  16, false, false } } },
    { ColorFormat::kRGB323232F,
        { DRM_FORMAT_BGR323232F,
            { PixelType::kFloat,    3,  32, false, false } } },
    { ColorFormat::kBGR888,
        { DRM_FORMAT_BGR888,
            { PixelType::kUnsigned, 3,   8, false, true  } } },
    { ColorFormat::kBGR888I,
        { DRM_FORMAT_BGR888,
            { PixelType::kSigned,   3,   8, false, true  } } },
    { ColorFormat::kBGR161616F,
        { DRM_FORMAT_BGR161616F,
            { PixelType::kFloat,    3,  16, false, true  } } },
    { ColorFormat::kBGR323232F,
        { DRM_FORMAT_BGR323232F,
            { PixelType::kFloat,    3,  32, false, true  } } },
    { ColorFormat::kARGB8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, true,  false } } },
    { ColorFormat::kARGB8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, true,  false } } },
    { ColorFormat::kARGB16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, true,  false } } },
    { ColorFormat::kARGB32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, true,  false } } },
    { ColorFormat::kABGR8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, true,  true  } } },
    { ColorFormat::kABGR8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, true,  true  } } },
    { ColorFormat::kABGR16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, true,  true  } } },
    { ColorFormat::kABGR32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, true,  true  } } },
    { ColorFormat::kRGBA8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, false, false } } },
    { ColorFormat::kRGBA8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, false, false } } },
    { ColorFormat::kRGBA16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, false, false } } },
    { ColorFormat::kRGBA32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, false, false } } },
    { ColorFormat::kBGRA8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, false, true  } } },
    { ColorFormat::kBGRA8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, false, true  } } },
    { ColorFormat::kBGRA16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, false, true  } } },
    { ColorFormat::kBGRA32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, false, true  } } },
    { ColorFormat::kXRGB8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, true,  false } } },
    { ColorFormat::kXRGB8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, true,  false } } },
    { ColorFormat::kXRGB16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, true,  false } } },
    { ColorFormat::kXRGB32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, true,  false } } },
    { ColorFormat::kXBGR8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, true,  true  } } },
    { ColorFormat::kXBGR8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, true,  true  } } },
    { ColorFormat::kXBGR16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, true,  true  } } },
    { ColorFormat::kXBGR32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, true,  true  } } },
    { ColorFormat::kRGBX8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, false, false } } },
    { ColorFormat::kRGBX8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, false, false } } },
    { ColorFormat::kRGBX16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, false, false } } },
    { ColorFormat::kRGBX32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, false, false } } },
    { ColorFormat::kBGRX8888,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kUnsigned, 4,   8, false, true  } } },
    { ColorFormat::kBGRX8888I,
        { DRM_FORMAT_ABGR8888,
            { PixelType::kSigned,   4,   8, false, true  } } },
    { ColorFormat::kBGRX16161616F,
        { DRM_FORMAT_ABGR16161616F,
            { PixelType::kFloat,    4,  16, false, true  } } },
    { ColorFormat::kBGRX32323232F,
        { DRM_FORMAT_ABGR32323232F,
            { PixelType::kFloat,    4,  32, false, true  } } },
};

const std::map<uint32_t, uint32_t> Format::kYuvFormatTable = {
  { ColorFormat::kYUYV,   DRM_FORMAT_YUYV   },
  { ColorFormat::kYVYU,   DRM_FORMAT_YVYU   },
  { ColorFormat::kUYVY,   DRM_FORMAT_UYVY   },
  { ColorFormat::kVYUY,   DRM_FORMAT_VYUY   },
  { ColorFormat::kNV12,   DRM_FORMAT_NV12   },
  { ColorFormat::kNV21,   DRM_FORMAT_NV21   },
  { ColorFormat::kNV16,   DRM_FORMAT_NV16   },
  { ColorFormat::kNV61,   DRM_FORMAT_NV61   },
  { ColorFormat::kNV24,   DRM_FORMAT_NV24   },
  { ColorFormat::kNV42,   DRM_FORMAT_NV42   },
  { ColorFormat::kYUV410, DRM_FORMAT_YUV410 },
  { ColorFormat::kYVU410, DRM_FORMAT_YVU410 },
  { ColorFormat::kYUV411, DRM_FORMAT_YUV411 },
  { ColorFormat::kYVU411, DRM_FORMAT_YVU411 },
  { ColorFormat::kYUV420, DRM_FORMAT_YUV420 },
  { ColorFormat::kYVU420, DRM_FORMAT_YVU420 },
  { ColorFormat::kYUV422, DRM_FORMAT_YUV422 },
  { ColorFormat::kYVU422, DRM_FORMAT_YVU422 },
  { ColorFormat::kYUV444, DRM_FORMAT_YUV444 },
  { ColorFormat::kYVU444, DRM_FORMAT_YVU444 },
};

// Color channel coefficients based on the YUV color space.
const std::map<uint32_t, Format::ColorCoeffcients> Format::kColorSpaceCoefficients = {
  { ColorMode::kBT601,          { 0.299, 0.587, 0.114 } },
  { ColorMode::kBT601FullRange, { 0.299, 0.587, 0.114 } },
  { ColorMode::kBT709,          { 0.2126, 0.7152, 0.0722 } }
};

std::tuple<uint32_t, uint64_t> Format::ToInternal(uint32_t format) {

  uint32_t external = format & kFormatMask;
  uint64_t modifier = 0;

  if ((format & ColorMode::kUBWC) != 0)
    modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;

  // First check whether it is YUV format or not.
  if (kYuvFormatTable.count(external) != 0)
    return {kYuvFormatTable.at(external), modifier};

  external = format & kFormatMask;

  // In case it is not YUV format check whether it is RGB(A).
  if (kRgbFormatTable.count(external) == 0)
    throw Exception("Unsuppoted format ", format);

  uint32_t internal = std::get<uint32_t>(kRgbFormatTable.at(external));

  return {internal, modifier};
}

GLenum Format::ToGL(uint32_t format) {

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    return GL_RGBA8;

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));

  if ((info.pixtype == PixelType::kFloat) && (info.bitdepth == 32))
    return GL_RGBA32F;
  else if ((info.pixtype == PixelType::kFloat) && (info.bitdepth == 16))
    return GL_RGBA16F;
  else if ((info.pixtype == PixelType::kSigned) && (info.bitdepth == 8))
    return GL_RGBA8_SNORM;

  return GL_RGBA8;
}

bool Format::IsRgb(uint32_t format) {

  return (kRgbFormatTable.count(format & kFormatMask) != 0) ? true : false;
}

bool Format::IsYuv(uint32_t format) {

  return (kYuvFormatTable.count(format & kFormatMask) != 0) ? true : false;
}

uint32_t Format::NumComponents(uint32_t format) {

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    throw Exception("Unsuppoted format ", format);

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));
  return info.n_components;
}

uint32_t Format::BitDepth(uint32_t format) {

  // First check whether it is YUV format, which have 8 bit depth.
  if (kYuvFormatTable.count(format & kFormatMask) != 0)
    return 8;

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    throw Exception("Unsuppoted format ", format);

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));
  return info.bitdepth;
}

bool Format::IsInverted(uint32_t format) {

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    return false;

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));
  return info.inverted;
}

bool Format::IsSwapped(uint32_t format) {

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    return false;

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));
  return info.swapped;
}

bool Format::IsSigned(uint32_t format) {

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    return false;

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));
  return (info.pixtype == PixelType::kSigned);
}

bool Format::IsFloat(uint32_t format) {

  if (kRgbFormatTable.count(format & kFormatMask) == 0)
    return false;

  auto& info = std::get<RgbInfo>(kRgbFormatTable.at(format & kFormatMask));
  return (info.pixtype == PixelType::kFloat);
}

uint32_t Format::ColorSpace(uint32_t format) {

  uint32_t colorspace = format & kColorSpaceMask;

  // By default use BT601 color space if none was set.
  return (colorspace != 0) ? colorspace : ColorMode::kBT601;
}

uint32_t Format::ToYuvColor(uint32_t color, uint32_t colorspace) {

  auto& coeffcients = kColorSpaceCoefficients.at(colorspace);

  float kr = std::get<0>(coeffcients);
  float kg = std::get<1>(coeffcients);
  float kb = std::get<2>(coeffcients);

  uint8_t red = (color >> 24) & 0xFF;
  uint8_t green = (color >> 16) & 0xFF;
  uint8_t blue = (color >> 8) & 0xFF;
  uint8_t alpha = color & 0xFF;

  uint32_t y = (red * kr) + (green * kg) + (blue * kb);
  uint32_t u = 128 + (red * (-(kr / (1.0 - kb)) / 2)) +
      (green * (-(kg / (1.0 - kb)) / 2)) + (blue * 0.5);
  uint32_t v = 128 + (red * 0.5) + (green * (-(kg / (1.0 - kr)) / 2)) +
      (blue * (-(kb / (1.0 - kr)) / 2));

  return (y << 24) + (u << 16) + (v << 8) + alpha;
}

} // namespace ib2c
