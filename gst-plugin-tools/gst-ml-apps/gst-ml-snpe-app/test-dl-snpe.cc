/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 *
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <iostream>

#include <dlfcn.h>

#include <DlContainer/DlContainer.h>
#include <DlSystem/IUserBuffer.h>
#include <SNPE/SNPE.h>
#include <SNPE/SNPEUtil.h>
#include <SNPE/SNPEBuilder.h>

using Snpe_DlContainerOpen_fnp = decltype(Snpe_DlContainer_Open);
using Snpe_DlContainerDelete_fnp = decltype(Snpe_DlContainer_Delete);
using Snpe_SNPEBuilderCreate_fnp = decltype(Snpe_SNPEBuilder_Create);
using Snpe_SNPEBuilderDelete_fnp = decltype(Snpe_SNPEBuilder_Delete);
using Snpe_SNPEBuilderBuild_fnp = decltype(Snpe_SNPEBuilder_Build);
using Snpe_SNPE_Delete_fnp = decltype(Snpe_SNPE_Delete);
using Snpe_SNPEBuilder_SetRuntimeProcessorOrder_fnp = decltype(
    Snpe_SNPEBuilder_SetRuntimeProcessorOrder);
using Snpe_RuntimeList_Create_fnp = decltype(Snpe_RuntimeList_Create);
using Snpe_RuntimeList_Delete_fnp = decltype(Snpe_RuntimeList_Delete);
using Snpe_RuntimeList_Add_fnp = decltype(Snpe_RuntimeList_Add);
using Snpe_Util_GetLibraryVersion_fnp = decltype(Snpe_Util_GetLibraryVersion);
using Snpe_DlVersion_ToString_fnp = decltype(Snpe_DlVersion_ToString);
using Snpe_DlVersion_Delete_fnp = decltype(Snpe_DlVersion_Delete);

struct SNPEFunctionPointers
{
  Snpe_DlContainerOpen_fnp* DlContainer_Open;
  Snpe_DlContainerDelete_fnp* DlContainer_Delete;
  Snpe_SNPEBuilderCreate_fnp* SNPEBuilder_Create;
  Snpe_SNPEBuilderDelete_fnp* SNPEBuilder_Delete;
  Snpe_SNPEBuilderBuild_fnp* SNPEBuilder_Build;
  Snpe_SNPE_Delete_fnp* SNPE_Delete;
  Snpe_SNPEBuilder_SetRuntimeProcessorOrder_fnp* SNPEBuilder_SetRuntimeProcessorOrder;
  Snpe_RuntimeList_Create_fnp* RuntimeList_Create;
  Snpe_RuntimeList_Delete_fnp* RuntimeList_Delete;
  Snpe_RuntimeList_Add_fnp* RuntimeList_Add;
  Snpe_Util_GetLibraryVersion_fnp* Util_GetLibraryVersion;
  Snpe_DlVersion_ToString_fnp* DlVersion_ToString;
  Snpe_DlVersion_Delete_fnp* DlVersion_Delete;
};

int
main(int argc, char *argv[])
{
  std::cout << std::endl << "===== Dynamic loading test for SNPE ====="
      << std::endl << std::endl;

  void* lib_handle;

  lib_handle = dlopen ("libSNPE.so", RTLD_NOW | RTLD_LOCAL);

  if (lib_handle == NULL) {
    std::cerr << "Cannot open lib: " << dlerror () << std::endl;
    return -1;
  }

  SNPEFunctionPointers snpe;

  snpe.DlContainer_Open = (Snpe_DlContainerOpen_fnp*) dlsym (lib_handle,
      "Snpe_DlContainer_Open");
  snpe.DlContainer_Delete = (Snpe_DlContainerDelete_fnp*) dlsym (lib_handle,
      "Snpe_DlContainer_Delete");
  snpe.SNPEBuilder_Create = (Snpe_SNPEBuilderCreate_fnp*) dlsym (lib_handle,
      "Snpe_SNPEBuilder_Create");
  snpe.SNPEBuilder_Delete = (Snpe_SNPEBuilderDelete_fnp*) dlsym (lib_handle,
      "Snpe_SNPEBuilder_Delete");
  snpe.SNPEBuilder_Build = (Snpe_SNPEBuilderBuild_fnp*) dlsym (lib_handle,
      "Snpe_SNPEBuilder_Build");
  snpe.SNPE_Delete = (Snpe_SNPE_Delete_fnp*) dlsym (lib_handle,
      "Snpe_SNPE_Delete");
  snpe.SNPEBuilder_SetRuntimeProcessorOrder =
      (Snpe_SNPEBuilder_SetRuntimeProcessorOrder_fnp*) dlsym (lib_handle,
      "Snpe_SNPEBuilder_SetRuntimeProcessorOrder");
  snpe.RuntimeList_Create = (Snpe_RuntimeList_Create_fnp*) dlsym (lib_handle,
      "Snpe_RuntimeList_Create");
  snpe.RuntimeList_Delete = (Snpe_RuntimeList_Delete_fnp*) dlsym (lib_handle,
      "Snpe_RuntimeList_Delete");
  snpe.RuntimeList_Add = (Snpe_RuntimeList_Add_fnp*) dlsym (lib_handle,
      "Snpe_RuntimeList_Add");
  snpe.Util_GetLibraryVersion = (Snpe_Util_GetLibraryVersion_fnp*) dlsym (
      lib_handle, "Snpe_Util_GetLibraryVersion");
  snpe.DlVersion_ToString = (Snpe_DlVersion_ToString_fnp*) dlsym (lib_handle,
      "Snpe_DlVersion_ToString");
  snpe.DlVersion_Delete = (Snpe_DlVersion_Delete_fnp*) dlsym (lib_handle,
      "Snpe_DlVersion_Delete");

  if (!snpe.DlContainer_Open || !snpe.DlContainer_Delete || !snpe.SNPEBuilder_Create ||
      !snpe.SNPEBuilder_Delete || !snpe.SNPEBuilder_Build || !snpe.SNPE_Delete ||
      !snpe.SNPEBuilder_SetRuntimeProcessorOrder ||
      !snpe.RuntimeList_Create || !snpe.RuntimeList_Delete || !snpe.RuntimeList_Add) {
      std::cerr << "Cannot load symbols: " << dlerror() << std::endl;
      dlclose (lib_handle);
      return -1;
  }

  auto lib_version_handle = snpe.Util_GetLibraryVersion ();
  std::cout << "SNPE v" << snpe.DlVersion_ToString (lib_version_handle) << "\n";
  snpe.DlVersion_Delete (lib_version_handle);

  auto model = snpe.DlContainer_Open (argv[1]);
  if (!model) {
      std::cerr << "model is null !!!" << std::endl;
      dlclose (lib_handle);
      return -2;
  }

  auto builder = snpe.SNPEBuilder_Create (model);
  if (!builder) {
      std::cerr << "builder is null !!!" << std::endl;
      dlclose (lib_handle);
      return -3;
  }

  auto rtlist = snpe.RuntimeList_Create ();
  if (!rtlist) {
      std::cerr << "rtlist is null !!!" << std::endl;
      dlclose (lib_handle);
      return -3;
  }

  snpe.RuntimeList_Add (rtlist, SNPE_RUNTIME_DSP);
  snpe.RuntimeList_Add (rtlist, SNPE_RUNTIME_CPU);
  snpe.SNPEBuilder_SetRuntimeProcessorOrder (builder, rtlist);

  auto interpreter = snpe.SNPEBuilder_Build (builder);
  if (!interpreter) {
      std::cerr << "interpreter is null !!!" << std::endl;
      dlclose (lib_handle);
      return -4;
  }

  snpe.SNPE_Delete (interpreter);
  snpe.SNPEBuilder_Delete (builder);
  snpe.RuntimeList_Delete (rtlist);
  snpe.DlContainer_Delete (model);

  dlclose (lib_handle);

  std::cout << "===== I am ready !!! =====" << std::endl;
  return 0;
}
