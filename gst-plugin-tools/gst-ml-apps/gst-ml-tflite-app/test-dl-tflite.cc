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

#include <memory>
#include <iostream>

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#if defined(HAVE_CORE_SHIMS_C_API_H)
#include <tensorflow/lite/core/shims/c/c_api.h>
#elif defined(HAVE_CORE_C_API_H)
#include <tensorflow/lite/core/c/c_api.h>
#elif defined(HAVE_C_API_H)
#include <tensorflow/lite/c/c_api.h>
#endif // defined(HAVE_CORE_C_API_H)

typedef struct TfLiteModel TfLiteModel;
typedef struct TfLiteInterpreterOptions TfLiteInterpreterOptions;
typedef struct TfLiteInterpreter TfLiteInterpreter;
typedef struct TfLiteTensor TfLiteTensor;

// Function pointer typedefs
using TfLiteModelCreateFromFileFn = decltype (TfLiteModelCreateFromFile);
using TfLiteModelDeleteFn = decltype (TfLiteModelDelete);
using TfLiteInterpreterOptionsCreateFn = decltype (TfLiteInterpreterOptionsCreate);
using TfLiteInterpreterOptionsDeleteFn = decltype (TfLiteInterpreterOptionsDelete);
using TfLiteInterpreterCreateFn = decltype (TfLiteInterpreterCreate);
using TfLiteInterpreterDeleteFn = decltype (TfLiteInterpreterDelete);
using TfLiteInterpreterAllocateTensorsFn = decltype (
    TfLiteInterpreterAllocateTensors);
using TfLiteInterpreterGetInputTensorCountFn = decltype (
    TfLiteInterpreterGetInputTensorCount);
using TfLiteInterpreterGetOutputTensorCountFn = decltype (
    TfLiteInterpreterGetOutputTensorCount);
using TfLiteInterpreterGetInputTensorFn = decltype (
    TfLiteInterpreterGetInputTensor);
using TfLiteInterpreterGetOutputTensorFn = decltype (
    TfLiteInterpreterGetOutputTensor);
using TfLiteTensorNameFn = decltype (TfLiteTensorName);
using TfLiteTensorTypeFn = decltype (TfLiteTensorType);
using TfLiteTensorNumDimsFn = decltype (TfLiteTensorNumDims);
using TfLiteTensorDimFn = decltype (TfLiteTensorDim);
using TfLiteTensorQuantizationParamsFn = decltype (TfLiteTensorQuantizationParams);

#define LOAD_SYMBOL(handle, name)                                     \
    do {                                                              \
      name = (typeof(name)) dlsym(handle, #name);                     \
      if (!name) {                                                    \
        std::cerr << "Failed to load symbol: " << #name << std::endl; \
        return EXIT_FAILURE;                                          \
      }                                                               \
    } while (0)

const char* TfLiteTypeToString (TfLiteType type) {
  switch (type) {
    case kTfLiteFloat32:
      return "FLOAT32";
    case kTfLiteInt32:
      return "INT32";
    case kTfLiteUInt8:
      return "UINT8";
    case kTfLiteInt64:
      return "INT64";
    case kTfLiteString:
      return "STRING";
    case kTfLiteBool:
      return "BOOL";
    case kTfLiteInt16:
      return "INT16";
    case kTfLiteInt8:
      return "INT8";
    default:
      return "UNKNOWN";
  }
}

void PrintTensorDetails (const TfLiteTensor* tensor, int index, const char* prefix,
                         TfLiteTensorNameFn name_fn, TfLiteTensorTypeFn type_fn,
                         TfLiteTensorNumDimsFn num_dims_fn, TfLiteTensorDimFn dim_fn,
                         TfLiteTensorQuantizationParamsFn quant_fn) {
  const char* name = name_fn (tensor);
  TfLiteQuantizationParams q = quant_fn (tensor);
  int dims = num_dims_fn (tensor);

  std::cout << prefix << " tensor[" << index << "] name: "
      << name << std::endl;
  std::cout << prefix << " tensor[" << index << "] offset: "
      << q.zero_point << std::endl;
  std::cout << prefix << " tensor[" << index << "] scale: "
      << q.scale << std::endl;

  for (int d = 0; d < dims; ++d) {
    std::cout << prefix << " tensor[" << index
        << "] Dimension[" << d <<"]: " << dim_fn (tensor, d) << std::endl;
  }
}

int InitializeHandle (std::shared_ptr<void>& handle, const std::string& lib_name) {
  handle = std::shared_ptr<void> (
      dlopen (lib_name.c_str (), RTLD_NOW | RTLD_LOCAL),
      [&](void* ptr) {
        if (ptr)
          dlclose (ptr);
      });

  if (nullptr == handle) {
    std::cerr << "Failed to load " << dlerror () << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int main (int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <model.tflite>" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<void> handle = nullptr;
  std::string lib_name = "libtensorflowlite_c.so";

  bool success = (InitializeHandle (handle, lib_name) == EXIT_SUCCESS);

  if (!success) {
    lib_name = "libtensorflowlite_c.so." + std::string (TFLITE_VERSION);
    success = (InitializeHandle (handle, lib_name) == EXIT_SUCCESS);
  }

  if (!success) {
    std::cerr << "Failed to initialize handle" << std::endl;
    return EXIT_FAILURE;
  }

  // Load functions
  TfLiteModelCreateFromFileFn* TfLiteModelCreateFromFile;
  TfLiteModelDeleteFn* TfLiteModelDelete;
  TfLiteInterpreterOptionsCreateFn* TfLiteInterpreterOptionsCreate;
  TfLiteInterpreterOptionsDeleteFn* TfLiteInterpreterOptionsDelete;
  TfLiteInterpreterCreateFn* TfLiteInterpreterCreate;
  TfLiteInterpreterDeleteFn* TfLiteInterpreterDelete;
  TfLiteInterpreterAllocateTensorsFn* TfLiteInterpreterAllocateTensors;
  TfLiteInterpreterGetInputTensorCountFn* TfLiteInterpreterGetInputTensorCount;
  TfLiteInterpreterGetOutputTensorCountFn* TfLiteInterpreterGetOutputTensorCount;
  TfLiteInterpreterGetInputTensorFn* TfLiteInterpreterGetInputTensor;
  TfLiteInterpreterGetOutputTensorFn* TfLiteInterpreterGetOutputTensor;
  TfLiteTensorNameFn* TfLiteTensorName;
  TfLiteTensorTypeFn* TfLiteTensorType;
  TfLiteTensorNumDimsFn* TfLiteTensorNumDims;
  TfLiteTensorDimFn* TfLiteTensorDim;
  TfLiteTensorQuantizationParamsFn* TfLiteTensorQuantizationParams;

  LOAD_SYMBOL (handle.get (), TfLiteModelCreateFromFile);
  LOAD_SYMBOL (handle.get (), TfLiteModelDelete);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterOptionsCreate);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterOptionsDelete);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterCreate);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterDelete);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterAllocateTensors);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterGetInputTensorCount);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterGetOutputTensorCount);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterGetInputTensor);
  LOAD_SYMBOL (handle.get (), TfLiteInterpreterGetOutputTensor);
  LOAD_SYMBOL (handle.get (), TfLiteTensorName);
  LOAD_SYMBOL (handle.get (), TfLiteTensorType);
  LOAD_SYMBOL (handle.get (), TfLiteTensorNumDims);
  LOAD_SYMBOL (handle.get (), TfLiteTensorDim);
  LOAD_SYMBOL (handle.get (), TfLiteTensorQuantizationParams);

  // Load model
  std::shared_ptr<TfLiteModel> model (
      TfLiteModelCreateFromFile(argv[1]),
      [&](TfLiteModel* model) {
        if (model)
          TfLiteModelDelete (model);
      });

  if (nullptr == model) {
    std::cerr << "Failed to create TfLite model from file: "
        << argv[1] << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<TfLiteInterpreterOptions> options (
      TfLiteInterpreterOptionsCreate (),
      [&](TfLiteInterpreterOptions* options) {
        if (options)
          TfLiteInterpreterOptionsDelete (options);
      });

  if (nullptr == options) {
    std::cerr << "Failed to create TfLite options" << std::endl;
    return EXIT_FAILURE;
  }

  std::shared_ptr<TfLiteInterpreter> interpreter (
      TfLiteInterpreterCreate (model.get (), options.get ()),
      [&](TfLiteInterpreter* interpreter) {
        if (interpreter)
          TfLiteInterpreterDelete (interpreter);
      });

  if (nullptr == interpreter) {
    std::cerr << "Failed to create TfLite interpreter" << std::endl;
    return EXIT_FAILURE;
  }

  if (kTfLiteOk != TfLiteInterpreterAllocateTensors (interpreter.get ())) {
    std::cerr << "Failed to allocate tensors" << std::endl;
    return EXIT_FAILURE;
  }

  // Input tensors
  int input_count =
      TfLiteInterpreterGetInputTensorCount (interpreter.get ());

  std::cout << "Number of input tensors: " << input_count << std::endl;

  if (input_count > 0) {
    const TfLiteTensor* tensor =
        TfLiteInterpreterGetInputTensor (interpreter.get (), 0);

    std::cout << "Input tensors type: "
        << TfLiteTypeToString (TfLiteTensorType (tensor)) << std::endl;
  }

  for (int i = 0; i < input_count; ++i) {
    const TfLiteTensor* tensor =
        TfLiteInterpreterGetInputTensor (interpreter.get (), i);

    PrintTensorDetails (tensor, i, "Input", TfLiteTensorName, TfLiteTensorType,
                        TfLiteTensorNumDims, TfLiteTensorDim,
                        TfLiteTensorQuantizationParams);
  }

  std::cout << "=================================================" << std::endl;

  // Output tensors
  int output_count =
      TfLiteInterpreterGetOutputTensorCount (interpreter.get ());

  std::cout << "Number of output tensors: " << output_count << std::endl;

  if (output_count > 0) {
    const TfLiteTensor* tensor =
        TfLiteInterpreterGetOutputTensor (interpreter.get (), 0);

    std::cout << "Output tensors type: "
        << TfLiteTypeToString (TfLiteTensorType (tensor)) << std::endl;
  }

  for (int i = 0; i < output_count; ++i) {
    const TfLiteTensor* tensor =
        TfLiteInterpreterGetOutputTensor (interpreter.get (), i);

    PrintTensorDetails (tensor, i, "Output", TfLiteTensorName, TfLiteTensorType,
                        TfLiteTensorNumDims, TfLiteTensorDim,
                        TfLiteTensorQuantizationParams);
  }

  std::cout << "=================================================" << std::endl;

  std::cout << "I am Ready !\n" << std::endl;

  return EXIT_SUCCESS;
}
