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
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <iostream>
#include <memory>
#include <filesystem>

#include <dlfcn.h>

#include <DlContainer/DlContainer.h>
#include <DlSystem/IUserBuffer.h>
#include <SNPE/SNPE.h>
#include <SNPE/SNPEUtil.h>
#include <SNPE/SNPEBuilder.h>

using Snpe_DlContainerOpen_fnp = decltype (Snpe_DlContainer_Open);
using Snpe_DlContainerDelete_fnp = decltype (Snpe_DlContainer_Delete);
using Snpe_SNPEBuilderCreate_fnp = decltype (Snpe_SNPEBuilder_Create);
using Snpe_SNPEBuilderDelete_fnp = decltype (Snpe_SNPEBuilder_Delete);
using Snpe_SNPEBuilderBuild_fnp = decltype (Snpe_SNPEBuilder_Build);
using Snpe_SNPE_Delete_fnp = decltype (Snpe_SNPE_Delete);
using Snpe_SNPEBuilder_SetRuntimeProcessorOrder_fnp = decltype (
    Snpe_SNPEBuilder_SetRuntimeProcessorOrder);
using Snpe_RuntimeList_Create_fnp = decltype (Snpe_RuntimeList_Create);
using Snpe_RuntimeList_Delete_fnp = decltype (Snpe_RuntimeList_Delete);
using Snpe_RuntimeList_Add_fnp = decltype (Snpe_RuntimeList_Add);
using Snpe_Util_GetLibraryVersion_fnp = decltype (Snpe_Util_GetLibraryVersion);
using Snpe_DlVersion_ToString_fnp = decltype (Snpe_DlVersion_ToString);
using Snpe_DlVersion_Delete_fnp = decltype (Snpe_DlVersion_Delete);
using Snpe_UserBufferMap_Create_fnp = decltype (Snpe_UserBufferMap_Create);
using Snpe_UserBufferMap_Delete_fnp = decltype (Snpe_UserBufferMap_Delete);
using Snpe_StringList_Create_fnp = decltype (Snpe_StringList_Create);
using Snpe_StringList_Delete_fnp = decltype (Snpe_StringList_Delete);
using Snpe_SNPE_GetInputTensorNames_fnp = decltype (
    Snpe_SNPE_GetInputTensorNames);
using Snpe_SNPE_GetOutputTensorNames_fnp = decltype (
    Snpe_SNPE_GetOutputTensorNames);
using Snpe_StringList_Size_fnp = decltype (Snpe_StringList_Size);
using Snpe_StringList_At_fnp = decltype (Snpe_StringList_At);
using Snpe_SNPE_GetInputOutputBufferAttributes_fnp = decltype (
    Snpe_SNPE_GetInputOutputBufferAttributes);
using Snpe_IBufferAttributes_GetDims_fnp = decltype (
    Snpe_IBufferAttributes_GetDims);
using Snpe_TensorShape_Rank_fnp = decltype (Snpe_TensorShape_Rank);
using Snpe_TensorShape_At_fnp = decltype (Snpe_TensorShape_At);
using Snpe_TensorShape_CreateDimsSize_fnp = decltype (
    Snpe_TensorShape_CreateDimsSize);
using Snpe_IBufferAttributes_GetEncoding_Ref_fnp = decltype (
    Snpe_IBufferAttributes_GetEncoding_Ref);
using Snpe_IBufferAttributes_Delete_fnp = decltype (
    Snpe_IBufferAttributes_Delete);
using Snpe_IBufferAttributes_GetEncodingType_fnp = decltype (
    Snpe_IBufferAttributes_GetEncodingType);
using Snpe_TensorShape_GetDimensions_fnp = decltype (
    Snpe_TensorShape_GetDimensions);
using Snpe_TensorShape_Delete_fnp = decltype (Snpe_TensorShape_Delete);
using Snpe_Util_CreateITensor_fnp = decltype (Snpe_Util_CreateITensor);
using Snpe_ITensor_Delete_fnp = decltype (Snpe_ITensor_Delete);
using Snpe_ITensor_GetSize_fnp = decltype (Snpe_ITensor_GetSize);
using Snpe_ITensor_IsQuantized_fnp = decltype (Snpe_ITensor_IsQuantized);
using Snpe_ITensor_GetOffset_fnp = decltype (Snpe_ITensor_GetOffset);
using Snpe_ITensor_GetDelta_fnp = decltype (Snpe_ITensor_GetDelta);

typedef struct SnpeEngine {
  Snpe_DlContainerOpen_fnp                      *DlContainer_Open;
  Snpe_DlContainerDelete_fnp                    *DlContainer_Delete;
  Snpe_SNPEBuilderCreate_fnp                    *SNPEBuilder_Create;
  Snpe_SNPEBuilderDelete_fnp                    *SNPEBuilder_Delete;
  Snpe_SNPEBuilderBuild_fnp                     *SNPEBuilder_Build;
  Snpe_SNPE_Delete_fnp                          *SNPE_Delete;
  Snpe_SNPEBuilder_SetRuntimeProcessorOrder_fnp *SNPEBuilder_SetRuntimeProcessorOrder;
  Snpe_RuntimeList_Create_fnp                   *RuntimeList_Create;
  Snpe_RuntimeList_Delete_fnp                   *RuntimeList_Delete;
  Snpe_RuntimeList_Add_fnp                      *RuntimeList_Add;
  Snpe_Util_GetLibraryVersion_fnp               *Util_GetLibraryVersion;
  Snpe_DlVersion_ToString_fnp                   *DlVersion_ToString;
  Snpe_DlVersion_Delete_fnp                     *DlVersion_Delete;
  Snpe_UserBufferMap_Create_fnp                 *UserBufferMap_Create;
  Snpe_UserBufferMap_Delete_fnp                 *UserBufferMap_Delete;
  Snpe_StringList_Create_fnp                    *StringList_Create;
  Snpe_StringList_Delete_fnp                    *StringList_Delete;
  Snpe_SNPE_GetInputTensorNames_fnp             *SNPE_GetInputTensorNames;
  Snpe_SNPE_GetOutputTensorNames_fnp            *SNPE_GetOutputTensorNames;
  Snpe_StringList_Size_fnp                      *StringList_Size;
  Snpe_StringList_At_fnp                        *StringList_At;
  Snpe_SNPE_GetInputOutputBufferAttributes_fnp  *SNPE_GetInputOutputBufferAttributes;
  Snpe_IBufferAttributes_GetDims_fnp            *IBufferAttributes_GetDims;
  Snpe_TensorShape_Rank_fnp                     *TensorShape_Rank;
  Snpe_TensorShape_At_fnp                       *TensorShape_At;
  Snpe_TensorShape_CreateDimsSize_fnp           *TensorShape_CreateDimsSize;
  Snpe_IBufferAttributes_GetEncoding_Ref_fnp    *IBufferAttributes_GetEncoding_Ref;
  Snpe_IBufferAttributes_Delete_fnp             *IBufferAttributes_Delete;
  Snpe_IBufferAttributes_GetEncodingType_fnp    *IBufferAttributes_GetEncodingType;
  Snpe_TensorShape_GetDimensions_fnp            *TensorShape_GetDimensions;
  Snpe_TensorShape_Delete_fnp                   *TensorShape_Delete;
  Snpe_Util_CreateITensor_fnp                   *Util_CreateITensor;
  Snpe_ITensor_Delete_fnp                       *ITensor_Delete;
  Snpe_ITensor_GetSize_fnp                      *ITensor_GetSize;
  Snpe_ITensor_IsQuantized_fnp                  *ITensor_IsQuantized;
  Snpe_ITensor_GetOffset_fnp                    *ITensor_GetOffset;
  Snpe_ITensor_GetDelta_fnp                     *ITensor_GetDelta;

  // SNPE container model.
  std::shared_ptr<void>                         model;
  // SNPE Builder constructed from the container model.
  std::shared_ptr<void>                         builder;
  // SNPE model interpreter.
  std::shared_ptr<void>                         interpreter;
  // SNPE runtime list.
  std::shared_ptr<void>                         rtlist;
  // SNPE lib version
  std::shared_ptr<void>                         version;

  // Map between SNPE input tensor names and corresponding User Buffer.
  std::shared_ptr<void>                         inputs;
  // Map between SNPE output tensor names and corresponding User Buffer.
  std::shared_ptr<void>                         outputs;

  // SNPE backend library handle.
  std::shared_ptr<void>                         libhandle;
} SnpeEngine;

std::string ElementTypeToString(Snpe_UserBufferEncoding_ElementType_t type) {
  switch (type) {
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT:
      return "FLOAT32";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT16:
      return "FLOAT16";
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNSIGNED8BIT:
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF8:
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT8:
      return "UINT8";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT8:
      return "INT8";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT16:
      return "UINT16";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT16:
      return "INT16";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT32:
      return "UINT32";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT32:
      return "INT32";
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNKNOWN:
      return "UNKNOWN";
    default:
      return "UNRECOGNIZED_ELEMENT_TYPE";
  }
}

int SnpeLoadSymbols (std::shared_ptr<SnpeEngine>& engine) {
  engine->DlContainer_Open = (Snpe_DlContainerOpen_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_DlContainer_Open");
  engine->DlContainer_Delete = (Snpe_DlContainerDelete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_DlContainer_Delete");
  engine->SNPEBuilder_Create = (Snpe_SNPEBuilderCreate_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPEBuilder_Create");
  engine->SNPEBuilder_Delete = (Snpe_SNPEBuilderDelete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPEBuilder_Delete");
  engine->SNPEBuilder_Build = (Snpe_SNPEBuilderBuild_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPEBuilder_Build");
  engine->SNPE_Delete = (Snpe_SNPE_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPE_Delete");
  engine->SNPEBuilder_SetRuntimeProcessorOrder =
      (Snpe_SNPEBuilder_SetRuntimeProcessorOrder_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPEBuilder_SetRuntimeProcessorOrder");
  engine->RuntimeList_Create = (Snpe_RuntimeList_Create_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_RuntimeList_Create");
  engine->RuntimeList_Delete = (Snpe_RuntimeList_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_RuntimeList_Delete");
  engine->RuntimeList_Add = (Snpe_RuntimeList_Add_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_RuntimeList_Add");
  engine->Util_GetLibraryVersion = (Snpe_Util_GetLibraryVersion_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_Util_GetLibraryVersion");
  engine->DlVersion_ToString = (Snpe_DlVersion_ToString_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_DlVersion_ToString");
  engine->DlVersion_Delete = (Snpe_DlVersion_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_DlVersion_Delete");
  engine->UserBufferMap_Create = (Snpe_UserBufferMap_Create_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_UserBufferMap_Create");
  engine->UserBufferMap_Delete = (Snpe_UserBufferMap_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_UserBufferMap_Delete");
  engine->StringList_Create = (Snpe_StringList_Create_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_StringList_Create");
  engine->StringList_Delete = (Snpe_StringList_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_StringList_Delete");
  engine->SNPE_GetInputTensorNames = (Snpe_SNPE_GetInputTensorNames_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPE_GetInputTensorNames");
  engine->SNPE_GetOutputTensorNames = (Snpe_SNPE_GetOutputTensorNames_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPE_GetOutputTensorNames");
  engine->StringList_Size = (Snpe_StringList_Size_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_StringList_Size");
  engine->StringList_At = (Snpe_StringList_At_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_StringList_At");
  engine->SNPE_GetInputOutputBufferAttributes = (Snpe_SNPE_GetInputOutputBufferAttributes_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_SNPE_GetInputOutputBufferAttributes");
  engine->IBufferAttributes_GetDims = (Snpe_IBufferAttributes_GetDims_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_IBufferAttributes_GetDims");
  engine->TensorShape_Rank = (Snpe_TensorShape_Rank_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_TensorShape_Rank");
  engine->TensorShape_At = (Snpe_TensorShape_At_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_TensorShape_At");
  engine->TensorShape_CreateDimsSize = (Snpe_TensorShape_CreateDimsSize_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_TensorShape_CreateDimsSize");
  engine->IBufferAttributes_GetEncoding_Ref = (Snpe_IBufferAttributes_GetEncoding_Ref_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_IBufferAttributes_GetEncoding_Ref");
  engine->IBufferAttributes_Delete = (Snpe_IBufferAttributes_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_IBufferAttributes_Delete");
  engine->IBufferAttributes_GetEncodingType = (Snpe_IBufferAttributes_GetEncodingType_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_IBufferAttributes_GetEncodingType");
  engine->TensorShape_GetDimensions = (Snpe_TensorShape_GetDimensions_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_TensorShape_GetDimensions");
  engine->TensorShape_Delete = (Snpe_TensorShape_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_TensorShape_Delete");
  engine->Util_CreateITensor = (Snpe_Util_CreateITensor_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_Util_CreateITensor");
  engine->ITensor_Delete = (Snpe_ITensor_Delete_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_ITensor_Delete");
  engine->ITensor_GetSize = (Snpe_ITensor_GetSize_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_ITensor_GetSize");
  engine->ITensor_IsQuantized = (Snpe_ITensor_IsQuantized_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_ITensor_IsQuantized");
  engine->ITensor_GetOffset = (Snpe_ITensor_GetOffset_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_ITensor_GetOffset");
  engine->ITensor_GetDelta = (Snpe_ITensor_GetDelta_fnp*) dlsym (
      engine->libhandle.get (), "Snpe_ITensor_GetDelta");

  if (!engine->DlContainer_Open         || !engine->DlContainer_Delete        ||
      !engine->SNPEBuilder_Create       || !engine->SNPEBuilder_Delete        ||
      !engine->SNPEBuilder_Build        || !engine->SNPE_Delete               ||
      !engine->SNPEBuilder_SetRuntimeProcessorOrder                           ||
      !engine->RuntimeList_Create       || !engine->RuntimeList_Delete        ||
      !engine->RuntimeList_Add          || !engine->Util_GetLibraryVersion    ||
      !engine->DlVersion_ToString       || !engine->DlVersion_Delete          ||
      !engine->UserBufferMap_Create     || !engine->UserBufferMap_Delete      ||
      !engine->StringList_Create        || !engine->StringList_Delete         ||
      !engine->SNPE_GetInputTensorNames || !engine->SNPE_GetOutputTensorNames ||
      !engine->SNPE_GetInputOutputBufferAttributes                            ||
      !engine->IBufferAttributes_GetDims                                      ||
      !engine->TensorShape_Rank         || !engine->TensorShape_At            ||
      !engine->TensorShape_CreateDimsSize                                     ||
      !engine->IBufferAttributes_GetEncoding_Ref                              ||
      !engine->IBufferAttributes_Delete || !engine->TensorShape_GetDimensions ||
      !engine->IBufferAttributes_GetEncodingType                              ||
      !engine->TensorShape_Delete       || !engine->Util_CreateITensor        ||
      !engine->ITensor_Delete           || !engine->ITensor_GetSize           ||
      !engine->ITensor_IsQuantized      || !engine->ITensor_GetOffset         ||
      !engine->ITensor_GetDelta) {
    std::cerr << "Cannot load symbols: " << dlerror () << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int SnpeInitializeEngine (std::shared_ptr<SnpeEngine>& engine,
                          std::filesystem::path& file_path) {
  std::shared_ptr<void> lib_handle (
      dlopen ("libSNPE.so", RTLD_NOW | RTLD_LOCAL),
      [&] (void* ptr) {
        if (ptr)
          dlclose (ptr);
      });

  if (nullptr == lib_handle) {
    std::cerr << "Cannot open lib: " << dlerror () << std::endl;
    return EXIT_FAILURE;
  }

  engine.reset(new SnpeEngine);

  engine->libhandle = lib_handle;

  if (EXIT_SUCCESS != SnpeLoadSymbols (engine)) {
    std::cerr << "snpe failed to load symbols !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> version (
      engine->Util_GetLibraryVersion (),
      [&] (Snpe_DlVersion_Handle_t snpe_version_handle) {
        if (snpe_version_handle)
          engine->DlVersion_Delete (snpe_version_handle);
      });

  if (nullptr == version) {
    std::cerr << "snpe version handler is null !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> model (
      engine->DlContainer_Open (file_path.c_str ()),
      [&] (Snpe_DlContainer_Handle_t model) {
        if (model)
          engine->DlContainer_Delete (model);
      });

  if (nullptr == model) {
    std::cerr << "model is null !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> builder (
      engine->SNPEBuilder_Create (model.get ()),
      [&] (Snpe_SNPEBuilder_Handle_t builder) {
        if (builder)
          engine->SNPEBuilder_Delete (builder);
      });

  if (nullptr == builder) {
    std::cerr << "builder is null !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> rtlist (
      engine->RuntimeList_Create (),
      [&] (Snpe_RuntimeList_Handle_t rtlist) {
        if (rtlist)
          engine->RuntimeList_Delete (rtlist);
      });

  if (nullptr == rtlist) {
    std::cerr << "rtlist is null !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> interpreter (
      engine->SNPEBuilder_Build (builder.get ()),
      [&] (Snpe_SNPE_Handle_t interpreter) {
        if (interpreter)
          engine->SNPE_Delete (interpreter);
      });

  if (nullptr == interpreter) {
    std::cerr << "interpreter is null !!!" << std::endl;
    return EXIT_FAILURE;
  }

  engine->model = model;
  engine->builder = builder;
  engine->interpreter = interpreter;
  engine->rtlist = rtlist;
  engine->version = version;

  return EXIT_SUCCESS;
}

int SnpeSetupTensors (std::shared_ptr<SnpeEngine>& engine,
                      Snpe_StringList_Handle_t (*GetTensorNames) (Snpe_SNPE_Handle_t),
                      std::string specific) {
  Snpe_UserBufferEncoding_ElementType_t type =
      SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNKNOWN;

  const size_t *dimensions = nullptr;
  int idx = 0, num = 0, n_tensors = 0, rank = 0;
  bool success = false;

  std::shared_ptr<void> usr_buffer_map (
      engine->UserBufferMap_Create (),
      [&] (Snpe_UserBufferMap_Handle_t usr_buffer_map) {
        if (usr_buffer_map)
          engine->UserBufferMap_Delete (usr_buffer_map);
      });

  if (nullptr == usr_buffer_map) {
    std::cerr << "Failed to create map for the " << specific
        << " user buffers!" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> names (
      GetTensorNames (engine->interpreter.get ()),
      [&] (Snpe_StringList_Handle_t names) {
        if (names)
          engine->StringList_Delete (names);
      });

  success = (names != nullptr) ? true : false;

  if (!success) {
    std::cerr << "Failed to retrieve " << specific
        << " tensor names!" << std::endl;
    return EXIT_FAILURE;
  }

  n_tensors = engine->StringList_Size (names.get ());

  for (idx = 0; idx < n_tensors; idx++) {
    const char *name = engine->StringList_At (names.get (), idx);

    std::cout << specific << " tensor[" << idx << "] name: " << name << std::endl;

    std::shared_ptr<void> attribs (engine->SNPE_GetInputOutputBufferAttributes (
        engine->interpreter.get (), name),
        [&] (Snpe_IBufferAttributes_Handle_t attribs) {
          if (attribs)
            engine->IBufferAttributes_Delete (attribs);
        });

    success = (attribs != nullptr) ? true : false;

    if (!success) {
      std::cerr << "Failed to get attributes for " << specific
          << " tensor " << name << "!" << std::endl;
      return EXIT_FAILURE;
    }

    type = engine->IBufferAttributes_GetEncodingType (attribs.get ());

    std::shared_ptr<void> shape (
        engine->IBufferAttributes_GetDims (attribs.get ()),
        [&] (Snpe_TensorShape_Handle_t shape) {
          if (shape)
            engine->TensorShape_Delete (shape);
        });

    std::shared_ptr<void> itensor (
        engine->Util_CreateITensor (shape.get ()),
        [&] (Snpe_ITensor_Handle_t itensor) {
          if (itensor)
            engine->ITensor_Delete (itensor);
        });

    std::cout << specific << " tensor[" << idx << "] offset: "
        << engine->ITensor_GetOffset (itensor.get ()) << std::endl;

    std::string is_quantized = "FALSE";

    if (engine->ITensor_IsQuantized (itensor.get ()))
      is_quantized = "TRUE";

    std::cout << specific << " tensor[" << idx << "] is quantized: "
        << is_quantized << std::endl;

    std::cout << specific << " tensor[" << idx << "] size: "
        << engine->ITensor_GetSize (itensor.get ()) << std::endl;

    std::cout << specific << " tensor[" << idx << "] delta: "
        << engine->ITensor_GetDelta (itensor.get ()) << std::endl;

    dimensions = engine->TensorShape_GetDimensions (shape.get ());
    rank = engine->TensorShape_Rank (shape.get ());

    for (num = 0; num < rank; ++num) {
      std::cout << specific
          << " tensor[" << idx << "] Dimension[" << num << "]: "
          << dimensions[num] << std::endl;
    }
  }

  std::cout << "Number of " << specific << " tensors: "
      << n_tensors << std::endl;

  std::cout << specific << " tensors type: "
      << ElementTypeToString (type) << std::endl;

  std::cout << "=================================================" << std::endl;

  return EXIT_SUCCESS;
}

int main (int argc, char *argv[]) {
  std::shared_ptr<SnpeEngine> engine = nullptr;

  if (argc <= 1) {
    std::cerr << "No input file was given !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::filesystem::path file_path (argv[1]);

  std::string extension (file_path.extension ());

  if (extension.compare (".dlc")) {
    std::cerr << "Error: unknow file extension : "
        << extension << " !!!" << std::endl;
    return EXIT_FAILURE;
  }

  if (EXIT_SUCCESS != SnpeInitializeEngine (engine, file_path)) {
    std::cerr << "snpe initialize engine failed" << std::endl;
    return EXIT_FAILURE;
  }

  if (EXIT_SUCCESS != SnpeSetupTensors (engine, engine->SNPE_GetInputTensorNames,
                                        "input")) {
    std::cerr << "snpe setup input tensors failed" << std::endl;
    return EXIT_FAILURE;
  }

  if (EXIT_SUCCESS != SnpeSetupTensors (engine, engine->SNPE_GetOutputTensorNames,
                                        "output")) {
    std::cerr << "snpe setup output tensors failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "SNPE v"
      << engine->DlVersion_ToString (engine->version.get ()) << std::endl;

  engine->RuntimeList_Add (engine->rtlist.get (), SNPE_RUNTIME_DSP);
  engine->RuntimeList_Add (engine->rtlist.get (), SNPE_RUNTIME_CPU);
  engine->SNPEBuilder_SetRuntimeProcessorOrder (
      engine->builder.get (), engine->rtlist.get ());

  std::cout << "===== I am ready !!! =====" << std::endl;

  return EXIT_SUCCESS;
}
