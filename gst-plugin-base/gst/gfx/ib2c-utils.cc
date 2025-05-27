/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <mutex>
#include <dlfcn.h>

#include "ib2c-utils.h"
#include "ib2c-formats.h"

namespace ib2c {

// Function pointers for the Adreno Utils library.
typedef unsigned int (*get_gpu_pixel_alignment)(void);

// Coeffcients for red, green and blue channels.
typedef std::tuple<float, float, float> ColorCoeffcients;

// Color channel coefficients based on the YUV color space.
static const std::map<uint32_t, ColorCoeffcients> kColorSpaceCoefficients = {
  { ColorMode::kBT601,          { 0.299, 0.587, 0.114 } },
  { ColorMode::kBT601FullRange, { 0.299, 0.587, 0.114 } },
  { ColorMode::kBT709,          { 0.2126, 0.7152, 0.0722 } }
};

// Adreno GPU alignment requirements.
static uint32_t kAlignment = 0;
static std::mutex kAlignmentLock;

static inline uint32_t GetGpuAlignment() {

  std::lock_guard<std::mutex> lk(kAlignmentLock);

  if (kAlignment != 0) {
    // Alignment has already been set, just return its value.
    return kAlignment;
  }

  void *handle = ::dlopen("libadreno_utils.so", RTLD_NOW);

  if (nullptr == handle) {
    throw Exception("Failed to load Adreno utils lib, error: ", dlerror(), "!");
  }

  get_gpu_pixel_alignment GetGpuPixelAlignment =
         (get_gpu_pixel_alignment) dlsym(handle, "get_gpu_pixel_alignment");

  if (nullptr == GetGpuPixelAlignment) {
    dlclose(handle);
    throw Exception("Failed to load Adreno utils symbol, error: ", dlerror(), "!");
  }

  // Fetch the GPU Pixel Alignment.
  kAlignment = GetGpuPixelAlignment();

  // Close the library as it is no longer needed.
  dlclose(handle);

  if (kAlignment == 1) {
    throw Exception("ChipID not present or target not supported!");
  }

  return kAlignment;
}

// Convert RGB color code to YUV color code.
uint32_t RgbToYuv(uint32_t color, uint32_t standard) {

  auto& coeffcients = kColorSpaceCoefficients.at(standard);

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

bool IsAligned(const Surface& surface) {

  uint32_t alignment = GetGpuAlignment();

#if defined(ANDROID)
  return ((surface.buffer->stride % alignment) == 0) ? true : false;
#else // ANDROID
  return ((surface.stride0 % alignment) == 0) ? true : false;
#endif // !ANDROID
}

std::tuple<uint32_t, uint32_t> AlignedDimensions(const Surface& surface) {

#if defined(ANDROID)
  uint32_t width = surface.buffer->width;
  uint32_t stride = surface.buffer->stride;
#else // ANDROID
  uint32_t width = surface.width;
  uint32_t stride = surface.stride0;
#endif // !ANDROID

  uint32_t alignment = GetGpuAlignment();

  uint32_t n_bytes = Format::BytesPerChannel(surface.format);
  // Channels is 4 because output compute texture is 4 channaled (RGBA).
  uint32_t n_channels = 4;

  // Align stride and calculate the width for the compute texture.
  stride = ((stride + (alignment - 1)) & ~(alignment - 1));
  width = stride / (n_channels * n_bytes);

  // Calculate the aligned height value rounded up based on surface size.
  uint32_t height = std::ceil(
      (surface.size / (n_channels * n_bytes)) / static_cast<float>(width));

  return std::make_tuple(width, height);
}

} // namespace ib2c
