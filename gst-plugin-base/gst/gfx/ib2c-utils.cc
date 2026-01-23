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
  { "qcom,adreno-635.0", 64 },
  { "qcom,adreno-gpu-a643", 64 },
  { "qcom,adreno-663.0", 64 },
};

// Adreno GPU alignment requirements.
static int32_t kAlignment = -1;
static std::mutex kAlignmentLock;

int32_t QueryAlignment() {

  std::lock_guard<std::mutex> lk(kAlignmentLock);

  if (kAlignment != -1)
    return kAlignment;

  try {
    std::filesystem::path rootpath = "/sys/devices/platform/soc@0/";
    std::string gpumodel;

    for (const auto& entry : std::filesystem::directory_iterator(rootpath)) {
      std::string dirname = entry.path().filename().string();
      if ((dirname.find(".qcom,kgsl-3d0") != std::string::npos) ||
          (dirname.find(".gpu") != std::string::npos)) {
        std::filesystem::path filepath = entry.path() / "of_node" / "compatible";
        std::ifstream file(filepath);
        if (!file.is_open())
            continue;

        std::stringstream sstream;
        sstream << file.rdbuf();
        std::string contents = sstream.str();

        const std::regex pattern(R"(qcom,adreno([-a-z]+)([0-9|.]+))");
        std::smatch match;

        if (std::regex_search(contents, match, pattern)) {
          gpumodel = match[0].str();
          break;
        }
      }
    }

    if (gpumodel.empty())
      throw Exception("Failed to find GPU in filesystem !");

    if (kGpuAlignment.count(gpumodel) == 0)
      throw Exception("Unknown GPU model ", gpumodel, " !");

    kAlignment = kGpuAlignment.at(gpumodel);

    Log("INFO: GPU alignment: ", kAlignment);
  } catch (std::exception& e) {
    try {
      // TEMP: This is a temporary solution until the GPU team provides a more
      // generic way to retrieve the GPU pixel alignment.
      // Relying on kernel device-tree nodes is not a robust long-term approach,
      // as they are not guaranteed to reflect the actual userspace driver in use.
      if (false == std::filesystem::exists("/dev/kgsl-3d0"))
        throw Exception(e.what(), "Adreno Utils is not supported on this platform!");

      void *handle = dlopen("libadreno_utils.so.1", RTLD_NOW);

      if (nullptr == handle)
        throw Exception(e.what(), "Fallback to Adreno utils. Failed to load "
                        "library, error: ", dlerror());

      get_gpu_pixel_alignment GetGpuPixelAlignment =
            (get_gpu_pixel_alignment) dlsym(handle, "get_gpu_pixel_alignment");

      if (nullptr == GetGpuPixelAlignment) {
        dlclose(handle);
        throw Exception(e.what(), "Fallback to Adreno utils. Failed to load "
                        "symbol, error: ", dlerror());
      }

      // Fetch the GPU Pixel Alignment.
      kAlignment = GetGpuPixelAlignment();

      Log("INFO: Adreno GPU alignment: ", kAlignment);

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
