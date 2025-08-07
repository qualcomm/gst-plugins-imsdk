/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <mutex>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <regex>
#include <dlfcn.h>

#include "ib2c-utils.h"

namespace ib2c {

// Function pointers for the Adreno Utils library.
typedef unsigned int (*get_gpu_pixel_alignment)(void);

static const std::unordered_map<std::string, uint32_t> kGpuAlignment = {
  { "qcom,adreno-635.0", 64 }, { "qcom,adreno-gpu-a643", 64 },
};

// Adreno GPU alignment requirements.
static int32_t kAlignment = -1;
static std::mutex kAlignmentLock;

int32_t QueryAlignment() {

  std::lock_guard<std::mutex> lk(kAlignmentLock);

  if (kAlignment != -1)
    return kAlignment;

  try {
    std::string rootpath = "/sys/devices/platform/soc@0/";
    auto iter = std::filesystem::directory_iterator(rootpath);

    auto found = std::find_if(iter, std::filesystem::end(iter),
        [&](const auto& entry) {
            std::string dirname = entry.path().filename().string();
            return (dirname.find(".qcom,kgsl-3d0") != std::string::npos) ||
                (dirname.find(".gpu") != std::string::npos);
        });

    if (found == std::filesystem::end(iter))
      throw Exception("Failed to find GPU in filesystem !");

    std::string filepath = (*found).path().string() + "/of_node/compatible";
    std::ifstream file(filepath);

    std::stringstream sstream;
    sstream << file.rdbuf();
    std::string contents = sstream.str();

    const std::regex pattern("qcom,adreno([-a-z]+)([0-9|.]+)");
    std::smatch match;

    if (!std::regex_search(contents, match, pattern))
      throw Exception("Failed to find GPU model in file !");

    std::string model = match[0].str();

    if (kGpuAlignment.count(model) == 0)
      throw Exception("Unknown GPU model ", model, " !");

    kAlignment = kGpuAlignment.at(model);
  } catch (std::exception& e) {
    try {
      void *handle = dlopen("libadreno_utils.so.1", RTLD_NOW);

      if (nullptr == handle) {
        throw Exception(e.what(), "Fallback to Adreno utils. Failed to load "
                        "library, error: ", dlerror());
      }

      get_gpu_pixel_alignment GetGpuPixelAlignment =
            (get_gpu_pixel_alignment) dlsym(handle, "get_gpu_pixel_alignment");

      if (nullptr == GetGpuPixelAlignment) {
        dlclose(handle);
        throw Exception(e.what(), "Fallback to Adreno utils. Failed to load "
                        "symbol, error: ", dlerror());
      }

      // Fetch the GPU Pixel Alignment.
      kAlignment = GetGpuPixelAlignment();

      // Close the library as it is no longer needed.
      dlclose(handle);
    } catch (std::exception& excp) {
      Log("CRITICAL: '", excp.what(), "' Using default alignment of 128!");
      kAlignment = 128;
    }
  }

  return kAlignment;
}

} // namespace ib2c
