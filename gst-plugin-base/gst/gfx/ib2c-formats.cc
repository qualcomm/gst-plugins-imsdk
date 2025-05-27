/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <drm/drm_fourcc.h>
#if !defined(ANDROID) && defined(HAVE_GBM_PRIV_H)
#include <gbm_priv.h>
#else
//TODO: Workaround due to Adreno not exporting the formats.
#define fourcc_mod_code_qti(vendor, val) \
    ((((uint32_t)DRM_FORMAT_MOD_VENDOR_## vendor) << 28) | (val & 0x0fffffffULL))

#define GBM_FORMAT_RGBA16161616F                fourcc_mod_code_qti(QCOM, 54)
#define GBM_FORMAT_RGB161616F                   fourcc_mod_code_qti(QCOM, 55)
#define GBM_FORMAT_RGBA32323232F                fourcc_mod_code_qti(QCOM, 56)
#define GBM_FORMAT_RGB323232F                   fourcc_mod_code_qti(QCOM, 57)
#endif // !defined(ANDROID) && defined(HAVE_GBM_PRIV_H)

#include "ib2c-formats.h"
#include "ib2c-utils.h"

namespace ib2c {

const uint32_t Format::kFormatMask = 0xFF;
const uint32_t Format::kColorSpaceMask = (0b11 << 9);
const uint32_t Format::kPixelTypeMask = (0b11 << 11);

// ColorFormat + PixelType mask and their corresponding tuple of:
// <DRM/GBM Format, Number of Channels, Inverted, Swapped RB>
const std::map<uint32_t, Format::RgbColorTuple> Format::kRgbColorTable = {
  { ColorFormat::kGRAY8,
      { DRM_FORMAT_R8,            1, false, false } },
  { ColorFormat::kRG88,
      { DRM_FORMAT_GR88,          2, false, false } },
  { ColorFormat::kGR88,
      { DRM_FORMAT_GR88,          2, false, true  } },
  { ColorFormat::kRGB565,
      { DRM_FORMAT_RGB565,        3, false, false } },
  { ColorFormat::kBGR565,
      { DRM_FORMAT_RGB565,        3, false, true  } },
  { ColorFormat::kRGB888,
      { DRM_FORMAT_BGR888,        3, false, false } },
  { ColorFormat::kBGR888,
      { DRM_FORMAT_BGR888,        3, false, true  } },
  { ColorFormat::kGRAY8 | ColorMode::kSigned,
      { DRM_FORMAT_R8,            1, false, false } },
  { ColorFormat::kRGB565 | ColorMode::kSigned,
      { DRM_FORMAT_RGB565,        3, false, false } },
  { ColorFormat::kBGR565 | ColorMode::kSigned,
      { DRM_FORMAT_RGB565,        3, false, true  } },
  { ColorFormat::kRGB888 | ColorMode::kSigned,
      { DRM_FORMAT_BGR888,        3, false, false } },
  { ColorFormat::kBGR888 | ColorMode::kSigned,
      { DRM_FORMAT_BGR888,        3, false, true  } },
#ifndef ANDROID
  { ColorFormat::kRGB888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGB161616F,    3, false, false } },
  { ColorFormat::kBGR888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGB161616F,    3, false, true  } },
  { ColorFormat::kRGB888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGB323232F,    3, false, false } },
  { ColorFormat::kBGR888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGB323232F,    3, false, true  } },
#endif // ANDROID
  { ColorFormat::kARGB1555,
      { DRM_FORMAT_ABGR1555,      4, true,  false } },
  { ColorFormat::kABGR1555,
      { DRM_FORMAT_ABGR1555,      4, true,  true  } },
  { ColorFormat::kRGBA5551,
      { DRM_FORMAT_ABGR1555,      4, false, false } },
  { ColorFormat::kBGRA5551,
      { DRM_FORMAT_ABGR1555,      4, false, true  } },
  { ColorFormat::kARGB4444,
      { DRM_FORMAT_ABGR4444,      4, true,  false } },
  { ColorFormat::kABGR4444,
      { DRM_FORMAT_ABGR4444,      4, true,  true  } },
  { ColorFormat::kRGBA4444,
      { DRM_FORMAT_ABGR4444,      4, false, false } },
  { ColorFormat::kBGRA4444,
      { DRM_FORMAT_ABGR4444,      4, false, true  } },
  { ColorFormat::kARGB8888,
      { DRM_FORMAT_ABGR8888,      4, true,  false } },
  { ColorFormat::kABGR8888,
      { DRM_FORMAT_ABGR8888,      4, true,  true  } },
#ifndef ANDROID
  { ColorFormat::kARGB8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, true,  false } },
  { ColorFormat::kABGR8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, true,  true  } },
  { ColorFormat::kARGB8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, true,  false } },
  { ColorFormat::kABGR8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, true,  true  } },
#endif // ANDROID
  { ColorFormat::kRGBA8888,
      { DRM_FORMAT_ABGR8888,      4, false, false } },
  { ColorFormat::kBGRA8888,
      { DRM_FORMAT_ABGR8888,      4, false, true  } },
  { ColorFormat::kRGBA8888 | ColorMode::kSigned,
      { DRM_FORMAT_ABGR8888,      4, false, false } },
  { ColorFormat::kBGRA8888 | ColorMode::kSigned,
      { DRM_FORMAT_ABGR8888,      4, false, true  } },
#ifndef ANDROID
  { ColorFormat::kRGBA8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, false, false } },
  { ColorFormat::kBGRA8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, false, true  } },
  { ColorFormat::kRGBA8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, false, false } },
  { ColorFormat::kBGRA8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, false, true  } },
#endif // ANDROID
  { ColorFormat::kXRGB8888,
      { DRM_FORMAT_ABGR8888,      4, true,  false } },
  { ColorFormat::kXBGR8888,
      { DRM_FORMAT_ABGR8888,      4, true,  true  } },
  { ColorFormat::kXRGB8888 | ColorMode::kSigned,
      { DRM_FORMAT_ABGR8888,      4, true,  false } },
  { ColorFormat::kXBGR8888 | ColorMode::kSigned,
      { DRM_FORMAT_ABGR8888,      4, true,  true  } },
#ifndef ANDROID
  { ColorFormat::kXRGB8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, true,  false } },
  { ColorFormat::kXBGR8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, true,  true  } },
  { ColorFormat::kXRGB8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, true,  false } },
  { ColorFormat::kXBGR8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, true,  true  } },
#endif // ANDROID
  { ColorFormat::kRGBX8888,
      { DRM_FORMAT_ABGR8888,      4, false, false } },
  { ColorFormat::kBGRX8888,
      { DRM_FORMAT_ABGR8888,      4, false, true  } },
  { ColorFormat::kRGBX8888 | ColorMode::kSigned,
      { DRM_FORMAT_ABGR8888,      4, false, false } },
  { ColorFormat::kBGRX8888 | ColorMode::kSigned,
      { DRM_FORMAT_ABGR8888,      4, false, true  } },
#ifndef ANDROID
  { ColorFormat::kRGBX8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, false, false } },
  { ColorFormat::kBGRX8888 | ColorMode::kFloat16,
      { GBM_FORMAT_RGBA16161616F, 4, false, true  } },
  { ColorFormat::kRGBX8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, false, false } },
  { ColorFormat::kBGRX8888 | ColorMode::kFloat32,
      { GBM_FORMAT_RGBA32323232F, 4, false, true  } },
#endif // ANDROID
};

const std::map<uint32_t, uint32_t> Format::kYuvColorTable = {
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

std::tuple<uint32_t, uint64_t> Format::ToInternal(uint32_t format) {

  uint32_t external = format & kFormatMask;
  uint64_t modifier = 0;

  if ((format & ColorMode::kUBWC) != 0)
    modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;

  // First check whether it is YUV format or not.
  if (kYuvColorTable.count(external) != 0)
    return {kYuvColorTable.at(external), modifier};

  external = format & (kFormatMask | kPixelTypeMask);

  // In case it is not YUV format check whether it is RGB(A).
  if (kRgbColorTable.count(external) == 0)
    throw Exception("Unsuppoted format ", format);

  uint32_t internal = std::get<uint32_t>(kRgbColorTable.at(external));

  return {internal, modifier};
}

GLenum Format::ToGL(uint32_t format) {

  if ((kRgbColorTable.count(format & kFormatMask) != 0) &&
      ((format & kPixelTypeMask) == ColorMode::kFloat16))
    return GL_RGBA16F;

  if ((kRgbColorTable.count(format & kFormatMask) != 0) &&
      ((format & kPixelTypeMask) == ColorMode::kFloat32))
    return GL_RGBA32F;

  if ((kRgbColorTable.count(format & kFormatMask) != 0) &&
      ((format & kPixelTypeMask) == ColorMode::kSigned))
    return GL_RGBA8_SNORM;

  return GL_RGBA8;
}

bool Format::IsRgb(uint32_t format) {

  uint32_t mask = kFormatMask | kPixelTypeMask;
  return (kRgbColorTable.count(format & mask) != 0) ? true : false;
}

bool Format::IsYuv(uint32_t format) {

  uint32_t mask = kFormatMask;
  return (kYuvColorTable.count(format & mask) != 0) ? true : false;
}

uint32_t Format::NumChannels(uint32_t format) {

  uint32_t mask = kFormatMask | kPixelTypeMask;

  if (kRgbColorTable.count(format & mask) == 0)
    throw Exception("Unsuppoted format ", format);

  return std::get<uint8_t>(kRgbColorTable.at(format & mask));
}

uint32_t Format::BytesPerChannel(uint32_t format) {

  if (kRgbColorTable.count(format & (kFormatMask | kPixelTypeMask)) == 0)
    throw Exception("Unsuppoted format ", format);

  if ((format & kPixelTypeMask) == ColorMode::kFloat16)
    return 2;

  if ((format & kPixelTypeMask) == ColorMode::kFloat32)
    return 4;

  return 1;
}

bool Format::IsInverted(uint32_t format) {

  uint32_t mask = kFormatMask | kPixelTypeMask;

  if (kRgbColorTable.count(format & mask) == 0)
    return false;

  return std::get<2>(kRgbColorTable.at(format & mask));
}

bool Format::IsSwapped(uint32_t format) {

  uint32_t mask = kFormatMask | kPixelTypeMask;

  if (kRgbColorTable.count(format & mask) == 0)
    return false;

  return std::get<3>(kRgbColorTable.at(format & mask));
}

bool Format::IsSigned(uint32_t format) {

  if (kRgbColorTable.count(format & (kFormatMask | kPixelTypeMask)) == 0)
    return false;

  return ((format & kPixelTypeMask) == ColorMode::kSigned);
}

bool Format::IsFloat(uint32_t format) {

  if (kRgbColorTable.count(format & (kFormatMask | kPixelTypeMask)) == 0)
    return false;

  return ((format & kPixelTypeMask) == ColorMode::kFloat16) ||
      ((format & kPixelTypeMask) == ColorMode::kFloat32);
}

bool Format::IsFloat16(uint32_t format) {

  if (kRgbColorTable.count(format & (kFormatMask | kPixelTypeMask)) == 0)
    return false;

  return ((format & kPixelTypeMask) == ColorMode::kFloat16);
}

bool Format::IsFloat32(uint32_t format) {

  if (kRgbColorTable.count(format & (kFormatMask | kPixelTypeMask)) == 0)
    return false;

  return ((format & kPixelTypeMask) == ColorMode::kFloat32);
}

uint32_t Format::ColorSpace(uint32_t format) {

  uint32_t colorspace = format & kColorSpaceMask;

  // By default use BT601 color space if none was set.
  return (colorspace != 0) ? colorspace : ColorMode::kBT601;
}

} // namespace ib2c
