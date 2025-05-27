/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

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

uint32_t GetAlignment() {

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

uint32_t ToYuvColorCode(uint32_t color, uint32_t standard) {

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

} // namespace ib2c
