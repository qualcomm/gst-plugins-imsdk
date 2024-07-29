/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cstddef>
#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
#include <vector>

#include <dlfcn.h>
#include <stdlib.h>

#include "System/QnnSystemInterface.h"
#include "System/QnnSystemContext.h"

#include "QnnInterface.h"
#include "QnnTypes.h"
#include "QnnBackend.h"

typedef struct QnnFunctionPointers
{
  QNN_INTERFACE_VER_TYPE qnnInterface;
  QNN_SYSTEM_INTERFACE_VER_TYPE qnnSystemInterface;
} QnnFunctionPointers;

using QnnInterfaceGetProvidersFn = decltype (QnnInterface_getProviders);
using QnnSystemInterfaceGetProvidersFn = decltype (QnnSystemInterface_getProviders);

template <class T> static inline T
resolve_symbol (void* lib_handle, const char* sym)
{
  T ptr = (T) dlsym (lib_handle, sym);

  if (nullptr == ptr) {
    std::cerr << "Unable to access symbol [" << sym << "], dlerror(): "
        << dlerror () << std::endl;
  }

  return ptr;
}

int
main(int argc, char* argv[])
{
  std::shared_ptr<void> handle_htp(
      dlopen ("libQnnHtp.so", RTLD_NOW | RTLD_LOCAL), &dlclose);

  if (NULL == handle_htp) {
    std::cerr << "Failed to open libQnnHtp.so backend, error: " << dlerror ()
        << "!" << std::endl;
    return EXIT_FAILURE;
  }

  // Load interface symbol of the backend library.
  // Get QNN Interface
  QnnInterfaceGetProvidersFn* GetProviders =
      resolve_symbol<QnnInterfaceGetProvidersFn *> (handle_htp.get (),
      "QnnInterface_getProviders");

  if (nullptr == GetProviders) {
    return EXIT_FAILURE;
  }

  const QnnInterface_t** providers = nullptr;
  uint32_t n_providers{0};

  if (!GetProviders (&providers, &n_providers)) {
    std::cerr << "Failed to get interface providers." << std::endl;
    return EXIT_FAILURE;
  }

  if (nullptr == providers) {
    std::cerr << "Failed to get interface providers: null interface"
        << " providers received." << std::endl;
    return EXIT_FAILURE;
  }

  if (0 == n_providers) {
    std::cerr << "Failed to get interface providers: 0 interface providers."
        << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<QnnFunctionPointers> qnn_function_pointers
      = std::make_shared<QnnFunctionPointers>();

  std::cout << "===== DEVICE QNN INFO =====" << std::endl;

  for (uint32_t i = 0; i < n_providers; i++) {

    qnn_function_pointers->qnnInterface =
        providers[i]->QNN_INTERFACE_VER_NAME;

    std::cout << "\tinterfaceProviders[" << i << "]->apiVersion.coreApiVersion"
        << providers[i]->apiVersion.coreApiVersion.major
        << "."
        << providers[i]->apiVersion.coreApiVersion.minor
        << "."
        << providers[i]->apiVersion.coreApiVersion.patch << std::endl;

    std::cout << "\tinterfaceProviders[" << i << "]->apiVersion.backendApiVersion"
        << providers[i]->apiVersion.backendApiVersion.major
        << "."
        << providers[i]->apiVersion.backendApiVersion.minor
        << "."
        << providers[i]->apiVersion.backendApiVersion.patch << std::endl;

    const char* ver = nullptr;
    const Qnn_ApiVersion_t* p_api = &providers[i]->apiVersion;
    const QNN_INTERFACE_VER_TYPE* p_iface =
        reinterpret_cast<const QNN_INTERFACE_VER_TYPE *>(p_api + 1);

    p_iface->backendGetBuildId (&ver);

    std::cout << "\tinterfaceProviders[" << i << "]->backendGetBuildId "
        << ver << std::endl;
  }

  if (argc > 1) {
    std::filesystem::path file_path(argv[1]);
    std::string extension(file_path.extension ());

    std::cout << "===== MODEL INFO " << file_path << " =====" << std::endl;

    if (!extension.compare (".so")) {
      std::shared_ptr<void> handle_model(
          dlopen (file_path.c_str (), RTLD_NOW | RTLD_LOCAL), &dlclose);

      if (nullptr == handle_model) {
        std::cerr << "Error: cannot load file " << file_path << " " << dlerror ()
            << " !!!" << std::endl;
        return EXIT_FAILURE;
      }

      char** qnn_sdk_version = resolve_symbol<char **> (handle_model.get (),
          "QNN_SDK_VERSION");

      if (nullptr == qnn_sdk_version) {
        std::cerr << "Error: cannot resolve symbol QNN_SDK_VERSION !!!"
            << std::endl;
        return EXIT_FAILURE;
      }

      std::cout << "\tmodel build id " << *qnn_sdk_version << std::endl;
    } else if (!extension.compare (".bin")) {
      std::ifstream file(file_path.c_str (), std::ios::binary);
      if (!file.is_open ()) {
        std::cerr << "Error: cannot read file " << file_path << " !!!"
            << std::endl;
        return EXIT_FAILURE;
      }

      std::vector<uint8_t> file_contents((std::istreambuf_iterator<char>(file)),
          std::istreambuf_iterator<char>());

      std::shared_ptr<void> handle_system(
          dlopen ("libQnnSystem.so", RTLD_NOW | RTLD_LOCAL), &dlclose);

      if (nullptr == handle_system) {
        std::cerr << "Error: cannot load file " << file_path << " " << dlerror ()
            << " !!!" << std::endl;
        return EXIT_FAILURE;
      }

      QnnSystemInterfaceGetProvidersFn* GetSysIntfProviders = nullptr;
      GetSysIntfProviders = resolve_symbol<QnnSystemInterfaceGetProvidersFn *> (
          handle_system.get (), "QnnSystemInterface_getProviders");

      if (nullptr == GetSysIntfProviders) {
        std::cerr << "Error: cannot resolve symbol"
            << " QnnSystemInterface_getProviders !!!" << std::endl;
        return EXIT_FAILURE;
      }

      const QnnSystemInterface_t** sysintf_providers = nullptr;
      uint32_t providers = 0;

      GetSysIntfProviders (
        (const QnnSystemInterface_t***)(&sysintf_providers), &providers);

      if ((nullptr == sysintf_providers) || (0 == providers)) {
        std::cerr << "Error: cannot GetSysIntfProviders !!!" << std::endl;
        return EXIT_FAILURE;
      }

      QNN_SYSTEM_INTERFACE_VER_TYPE sys_interface =
          sysintf_providers[0]->QNN_SYSTEM_INTERFACE_VER_NAME;

      QnnSystemContext_Handle_t sysctx_handle = nullptr;
      sys_interface.systemContextCreate (&sysctx_handle);

      const QnnSystemContext_BinaryInfo_t* binary_info = nullptr;
      Qnn_ContextBinarySize_t binary_info_size = 0;

      sys_interface.systemContextGetBinaryInfo (sysctx_handle,
          static_cast<void *>(file_contents.data()), file_contents.size(),
          &binary_info, &binary_info_size);

      if (nullptr == binary_info) {
        std::cerr << "Error: cannot systemContextGetBinaryInfo !!!" << std::endl;
        return EXIT_FAILURE;
      }

      std::cout << "\tbinary_info->contextBinaryInfoV1.coreApiVersion "
          << binary_info->contextBinaryInfoV1.coreApiVersion.major
          << "."
          << binary_info->contextBinaryInfoV1.coreApiVersion.minor
          << "."
          << binary_info->contextBinaryInfoV1.coreApiVersion.patch << std::endl;

      std::cout << "\tbinary_info->contextBinaryInfoV1.backendApiVersion "
          << binary_info->contextBinaryInfoV1.backendApiVersion.major
          << "."
          << binary_info->contextBinaryInfoV1.backendApiVersion.minor
          << "."
          << binary_info->contextBinaryInfoV1.backendApiVersion.patch
          << std::endl;

      std::cout << "\tBuildId " << binary_info->contextBinaryInfoV1.buildId
          << std::endl;

      sys_interface.systemContextFree (sysctx_handle);
    } else {
      std::cerr << "Error: unknow file extension : " << extension << " !!!"
          << std::endl;
      return EXIT_FAILURE;
    }
  }

  std::cout << "===== I Am Ready !!! =====" << std::endl;

  return EXIT_SUCCESS;
}
