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
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ml-tflite-engine.h"

#include <string>
#include <dlfcn.h>

#ifdef HAVE_EXTERNAL_DELEGATE_H
#if defined(HAVE_CORE_C_API_H)
#include <tensorflow/lite/core/c/c_api.h>
#include <tensorflow/lite/core/c/c_api_experimental.h>
#elif defined(HAVE_C_API_H)
#include <tensorflow/lite/c/c_api.h>
#include <tensorflow/lite/c/c_api_experimental.h>
#endif // defined(HAVE_CORE_C_API_H)
#include <tensorflow/lite/delegates/gpu/delegate.h>
#include <tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h>
#include <tensorflow/lite/delegates/external/external_delegate.h>
#ifdef HAVE_NNAPI_H
#include <tensorflow/lite/delegates/nnapi/nnapi_delegate_c_api.h>
#endif // HAVE_NNAPI_H
#if defined(HAVE_HEXAGON_DELEGATE_H)
#include <tensorflow/lite/delegates/hexagon/hexagon_delegate.h>
#elif defined(HAVE_HEXAGON_EXPERIMENTAL_DELEGATE_H)
#include <tensorflow/lite/experimental/delegates/hexagon/hexagon_delegate.h>
#endif // defined(HAVE_HEXAGON_DELEGATE_H)

#if defined(HAVE_HEXAGON_DELEGATE_H) || defined(HAVE_HEXAGON_EXPERIMENTAL_DELEGATE_H)
#define HAVE_HEXAGON_H
#endif // defined(HAVE_HEXAGON_DELEGATE_H) || defined(HAVE_HEXAGON_EXPERIMENTAL_DELEGATE_H)

#else

#include <tensorflow/lite/model.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/delegates/nnapi/nnapi_delegate.h>
#include <tensorflow/lite/delegates/gpu/delegate.h>
#include <tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h>

#ifdef HAVE_HEXAGON_DELEGATE_H
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 3)
#include <tensorflow/lite/delegates/hexagon/hexagon_delegate.h>
#else
#include <tensorflow/lite/experimental/delegates/hexagon/hexagon_delegate.h>
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 3)
#endif // HAVE_HEXAGON_DELEGATE_H
#endif // HAVE_EXTERNAL_DELEGATE_H

#define GST_ML_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define GST_ML_RETURN_IF_FAIL(expression, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return; \
  } \
}

#define GST_ML_RETURN_IF_FAIL_WITH_CLEAN(expression, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return; \
  } \
}

#define DEFAULT_OPT_THREADS  1
#define DEFAULT_OPT_DELEGATE GST_ML_TFLITE_DELEGATE_NONE

#define GET_OPT_MODEL(s) get_opt_string (s, \
    GST_ML_TFLITE_ENGINE_OPT_MODEL)
#define GET_OPT_DELEGATE(s) get_opt_enum (s, \
    GST_ML_TFLITE_ENGINE_OPT_DELEGATE, GST_TYPE_ML_TFLITE_DELEGATE, \
    DEFAULT_OPT_DELEGATE)
#define GET_OPT_STHREADS(s) get_opt_uint (s, \
    GST_ML_TFLITE_ENGINE_OPT_THREADS, DEFAULT_OPT_THREADS)

#define GET_OPT_EXT_DELEGATE_PATH(s) get_opt_string (s, \
    GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_PATH)

#define GET_OPT_EXT_DELEGATE_OPTS(s) get_opt_structure (s, \
    GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_OPTS)

#define GST_CAT_DEFAULT gst_ml_tflite_engine_debug_category()

static GstDebugCategory *
gst_ml_tflite_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("ml-tflite-engine", 0,
        "Machine Learning TFLite Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

#ifdef HAVE_EXTERNAL_DELEGATE_H
using GpuDelegateOptionsV2Default_fn = decltype (
    TfLiteGpuDelegateOptionsV2Default);
using GpuDelegateV2Create_fn = decltype (TfLiteGpuDelegateV2Create);
using GpuDelegateV2Delete_fn = decltype (TfLiteGpuDelegateV2Delete);
using XNNPackDelegateOptionsDefault_fn = decltype (
    TfLiteXNNPackDelegateOptionsDefault);
using XNNPackDelegateCreate_fn = decltype (TfLiteXNNPackDelegateCreate);
using XNNPackDelegateDelete_fn = decltype (TfLiteXNNPackDelegateDelete);

using ExternalDelegateOptionsDefault_fn = decltype (
    TfLiteExternalDelegateOptionsDefault);
using ExternalDelegateCreate_fn = decltype (TfLiteExternalDelegateCreate);
using ExternalDelegateDelete_fn = decltype (TfLiteExternalDelegateDelete);

#ifdef HAVE_NNAPI_H
using NnapiDelegateOptionsDefault_fn = decltype (
    TfLiteNnapiDelegateOptionsDefault);
using NnapiDelegateCreate_fn = decltype (TfLiteNnapiDelegateCreate);
using NnapiDelegateDelete_fn = decltype (TfLiteNnapiDelegateDelete);
#endif // HAVE_NNAPI_H

#ifdef HAVE_HEXAGON_H
using HexagonDelegateOptionsDefault_fn = decltype (TfLiteHexagonDelegateOptionsDefault);
using HexagonDelegateCreate_fn = decltype (TfLiteHexagonDelegateCreate);
using HexagonDelegateDelete_fn = decltype (TfLiteHexagonDelegateDelete);
using HexagonInit_fn = decltype (TfLiteHexagonInit);
using HexagonTearDown_fn = decltype (TfLiteHexagonTearDown);
#endif // HAVE_HEXAGON_H

using ModelCreateFromFile_fn = decltype (TfLiteModelCreateFromFile);
using ModelDelete_fn = decltype (TfLiteModelDelete);
using InterpreterOptionsCreate_fn = decltype (TfLiteInterpreterOptionsCreate);
using InterpreterOptionsDelete_fn = decltype (TfLiteInterpreterOptionsDelete);
using InterpreterCreate_fn = decltype (TfLiteInterpreterCreate);
using InterpreterDelete_fn = decltype (TfLiteInterpreterDelete);
using InterpreterOptionsSetNumThreads_fn = decltype (
    TfLiteInterpreterOptionsSetNumThreads);
using InterpreterOptionsAddDelegate_fn = decltype (
    TfLiteInterpreterOptionsAddDelegate);
using InterpreterAllocateTensors_fn = decltype (
    TfLiteInterpreterAllocateTensors);
using InterpreterGetInputTensorCount_fn = decltype (
    TfLiteInterpreterGetInputTensorCount);
using InterpreterGetInputTensor_fn = decltype (
    TfLiteInterpreterGetInputTensor);
using InterpreterGetOutputTensorCount_fn = decltype (
    TfLiteInterpreterGetOutputTensorCount);
using InterpreterGetOutputTensor_fn = decltype (
    TfLiteInterpreterGetOutputTensor);
using InterpreterModifyGraphWithDelegate_fn = decltype (
    TfLiteInterpreterModifyGraphWithDelegate);
using InterpreterInvoke_fn = decltype (TfLiteInterpreterInvoke);
using TensorType_fn = decltype (TfLiteTensorType);
using TensorNumDims_fn = decltype (TfLiteTensorNumDims);
using TensorDim_fn = decltype (TfLiteTensorDim);
using TensorData_fn = decltype (TfLiteTensorData);
using Version_fn = decltype (TfLiteVersion);
#endif // HAVE_EXTERNAL_DELEGATE_H

struct _GstMLTFLiteEngine
{
  GstMLInfo *ininfo;
  GstMLInfo *outinfo;

  GstStructure *settings;

  // TFLite model delegate.
  TfLiteDelegate *delegate;

#ifndef HAVE_EXTERNAL_DELEGATE_H
  // TFLite flatbuffer model.
  // Raw pointer to c++ unique_ptr because struct is allocated via malloc.
  tflite::FlatBufferModel *model;

  // TFLite model interpreter.
  // Raw pointer to c++ unique_ptr because struct is allocated via malloc.
  tflite::Interpreter *interpreter;

#else
  // TFLite flatbuffer model.
  TfLiteModel* model;

  // TFLite model interpreter.
  TfLiteInterpreter* interpreter;

  // TFLite version variables.
  gint major;
  gint minor;
  gint patch;

  // TFLite backend library handle.
  void* libhandle;

  // TFLite library APIs.
  GpuDelegateOptionsV2Default_fn* GpuDelegateOptionsV2Default;

  GpuDelegateV2Create_fn* GpuDelegateV2Create;
  GpuDelegateV2Delete_fn* GpuDelegateV2Delete;

  XNNPackDelegateOptionsDefault_fn* XNNPackDelegateOptionsDefault;

  XNNPackDelegateCreate_fn* XNNPackDelegateCreate;
  XNNPackDelegateDelete_fn* XNNPackDelegateDelete;

  ExternalDelegateOptionsDefault_fn* ExternalDelegateOptionsDefault;

  ExternalDelegateCreate_fn* ExternalDelegateCreate;
  ExternalDelegateDelete_fn* ExternalDelegateDelete;

#ifdef HAVE_NNAPI_H
  NnapiDelegateOptionsDefault_fn* NnapiDelegateOptionsDefault;
  NnapiDelegateCreate_fn* NnapiDelegateCreate;
  NnapiDelegateDelete_fn* NnapiDelegateDelete;
#endif // HAVE_NNAPI_H

#ifdef HAVE_HEXAGON_H
  HexagonDelegateOptionsDefault_fn* HexagonDelegateOptionsDefault;
  HexagonDelegateCreate_fn* HexagonDelegateCreate;
  HexagonDelegateDelete_fn* HexagonDelegateDelete;
  HexagonInit_fn* HexagonInit;
  HexagonTearDown_fn* HexagonTearDown;
#endif // HAVE_HEXAGON_H

  ModelCreateFromFile_fn* ModelCreateFromFile;
  ModelDelete_fn* ModelDelete;

  InterpreterOptionsCreate_fn* InterpreterOptionsCreate;
  InterpreterOptionsDelete_fn* InterpreterOptionsDelete;

  InterpreterCreate_fn* InterpreterCreate;
  InterpreterDelete_fn* InterpreterDelete;

  InterpreterOptionsSetNumThreads_fn* InterpreterOptionsSetNumThreads;
  InterpreterOptionsAddDelegate_fn* InterpreterOptionsAddDelegate;
  InterpreterAllocateTensors_fn* InterpreterAllocateTensors;
  InterpreterGetInputTensorCount_fn* InterpreterGetInputTensorCount;
  InterpreterGetInputTensor_fn* InterpreterGetInputTensor;
  InterpreterGetOutputTensorCount_fn* InterpreterGetOutputTensorCount;
  InterpreterGetOutputTensor_fn* InterpreterGetOutputTensor;
  InterpreterModifyGraphWithDelegate_fn* InterpreterModifyGraphWithDelegate;
  InterpreterInvoke_fn* InterpreterInvoke;

  TensorType_fn* TensorType;
  TensorNumDims_fn* TensorNumDims;
  TensorDim_fn* TensorDim;
  TensorData_fn* TensorData;

  Version_fn* Version;
#endif // HAVE_EXTERNAL_DELEGATE_H
};


static const gchar *
get_opt_string (GstStructure * settings, const gchar * opt)
{
  return gst_structure_get_string (settings, opt);
}

static guint
get_opt_uint (GstStructure * settings, const gchar * opt, guint dval)
{
  guint result;
  return gst_structure_get_uint (settings, opt, &result) ?
    result : dval;
}

static gint
get_opt_enum (GstStructure * settings, const gchar * opt, GType type, gint dval)
{
  gint result;
  return gst_structure_get_enum (settings, opt, type, &result) ?
    result : dval;
}

static GstStructure *
get_opt_structure (GstStructure * settings, const gchar * opt)
{
  GstStructure *result = NULL;
  gst_structure_get(settings, opt, GST_TYPE_STRUCTURE, &result, NULL);
  return result;
}

const GstMLInfo *
gst_ml_tflite_engine_get_input_info  (GstMLTFLiteEngine * engine)
{
  return (engine == NULL) ? NULL : engine->ininfo;
}

const GstMLInfo *
gst_ml_tflite_engine_get_output_info  (GstMLTFLiteEngine * engine)
{
  return (engine == NULL) ? NULL : engine->outinfo;
}


#ifdef HAVE_EXTERNAL_DELEGATE_H

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR ("Failed to find symbol %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_ml_tflite_initialize_library (GstMLTFLiteEngine * engine)
{
  engine->libhandle = dlopen ("libtensorflowlite_c.so", RTLD_NOW | RTLD_LOCAL);
  if (engine->libhandle == NULL) {
    GST_ERROR ("Failed to open TFLite library, error: %s!", dlerror());
    return FALSE;
  }

  auto success = load_symbol ((gpointer*)&engine->GpuDelegateOptionsV2Default,
      engine->libhandle, "TfLiteGpuDelegateOptionsV2Default");
  success &= load_symbol ((gpointer*)&engine->XNNPackDelegateOptionsDefault,
      engine->libhandle, "TfLiteXNNPackDelegateOptionsDefault");

  success &= load_symbol ((gpointer*)&engine->GpuDelegateV2Create,
      engine->libhandle, "TfLiteGpuDelegateV2Create");
  success &= load_symbol ((gpointer*)&engine->GpuDelegateV2Delete,
      engine->libhandle, "TfLiteGpuDelegateV2Delete");

  success &= load_symbol ((gpointer*)&engine->XNNPackDelegateCreate,
      engine->libhandle, "TfLiteXNNPackDelegateCreate");
  success &= load_symbol ((gpointer*)&engine->XNNPackDelegateDelete,
      engine->libhandle, "TfLiteXNNPackDelegateDelete");

  success &= load_symbol ((gpointer*)&engine->ExternalDelegateOptionsDefault,
      engine->libhandle, "TfLiteExternalDelegateOptionsDefault");

  success &= load_symbol ((gpointer*)&engine->ExternalDelegateCreate,
      engine->libhandle, "TfLiteExternalDelegateCreate");
  success &= load_symbol ((gpointer*)&engine->ExternalDelegateDelete,
      engine->libhandle, "TfLiteExternalDelegateDelete");

#ifdef HAVE_NNAPI_H
  success &= load_symbol ((gpointer*)&engine->NnapiDelegateOptionsDefault,
      engine->libhandle, "TfLiteNnapiDelegateOptionsDefault");
  success &= load_symbol ((gpointer*)&engine->NnapiDelegateCreate,
      engine->libhandle, "TfLiteNnapiDelegateCreate");
  success &= load_symbol ((gpointer*)&engine->NnapiDelegateDelete,
      engine->libhandle, "TfLiteNnapiDelegateDelete");
#endif // HAVE_NNAPI_H

#ifdef HAVE_HEXAGON_H
  success &= load_symbol ((gpointer*)&engine->HexagonDelegateOptionsDefault,
      engine->libhandle, "TfLiteHexagonDelegateOptionsDefault");
  success &= load_symbol ((gpointer*)&engine->HexagonDelegateCreate,
      engine->libhandle, "TfLiteHexagonDelegateCreate");
  success &= load_symbol ((gpointer*)&engine->HexagonDelegateDelete,
      engine->libhandle, "TfLiteHexagonDelegateDelete");
  success &= load_symbol ((gpointer*)&engine->HexagonInit,
      engine->libhandle, "TfLiteHexagonInit");
  success &= load_symbol ((gpointer*)&engine->HexagonTearDown,
      engine->libhandle, "TfLiteHexagonTearDown");
#endif // HAVE_HEXAGON_H

  success &= load_symbol ((gpointer*)&engine->ModelCreateFromFile,
      engine->libhandle, "TfLiteModelCreateFromFile");
  success &= load_symbol ((gpointer*)&engine->ModelDelete,
      engine->libhandle, "TfLiteModelDelete");

  success &= load_symbol ((gpointer*)&engine->InterpreterOptionsCreate,
      engine->libhandle, "TfLiteInterpreterOptionsCreate");
  success &= load_symbol ((gpointer*)&engine->InterpreterOptionsDelete,
      engine->libhandle, "TfLiteInterpreterOptionsDelete");

  success &= load_symbol ((gpointer*)&engine->InterpreterCreate,
      engine->libhandle, "TfLiteInterpreterCreate");
  success &= load_symbol ((gpointer*)&engine->InterpreterDelete,
      engine->libhandle, "TfLiteInterpreterDelete");

  success &= load_symbol ((gpointer*)&engine->InterpreterOptionsSetNumThreads,
      engine->libhandle, "TfLiteInterpreterOptionsSetNumThreads");
  success &= load_symbol ((gpointer*)&engine->InterpreterOptionsAddDelegate,
      engine->libhandle, "TfLiteInterpreterOptionsAddDelegate");
  success &= load_symbol ((gpointer*)&engine->InterpreterAllocateTensors,
      engine->libhandle, "TfLiteInterpreterAllocateTensors");
  success &= load_symbol ((gpointer*)&engine->InterpreterGetInputTensorCount,
      engine->libhandle, "TfLiteInterpreterGetInputTensorCount");
  success &= load_symbol ((gpointer*)&engine->InterpreterGetInputTensor,
      engine->libhandle, "TfLiteInterpreterGetInputTensor");
  success &= load_symbol ((gpointer*)&engine->InterpreterGetOutputTensorCount,
      engine->libhandle, "TfLiteInterpreterGetOutputTensorCount");
  success &= load_symbol ((gpointer*)&engine->InterpreterGetOutputTensor,
      engine->libhandle, "TfLiteInterpreterGetOutputTensor");
  success &= load_symbol ((gpointer*)&engine->InterpreterModifyGraphWithDelegate,
      engine->libhandle, "TfLiteInterpreterModifyGraphWithDelegate");
  success &= load_symbol ((gpointer*)&engine->InterpreterInvoke,
      engine->libhandle, "TfLiteInterpreterInvoke");

  success &= load_symbol ((gpointer*)&engine->TensorType,
      engine->libhandle, "TfLiteTensorType");
  success &= load_symbol ((gpointer*)&engine->TensorNumDims,
      engine->libhandle, "TfLiteTensorNumDims");
  success &= load_symbol ((gpointer*)&engine->TensorDim,
      engine->libhandle, "TfLiteTensorDim");
  success &= load_symbol ((gpointer*)&engine->TensorData,
      engine->libhandle, "TfLiteTensorData");

  success &= load_symbol ((gpointer*)&engine->Version,
      engine->libhandle, "TfLiteVersion");

  std::string version_str = engine->Version ();

  engine->major = std::stoi (version_str);

  version_str.erase (
    version_str.begin (),
    version_str.begin () + version_str.find (".") + 1
  );

  engine->minor = std::stoi (version_str);

  version_str.erase (
    version_str.begin (),
    version_str.begin () + version_str.find (".") + 1
  );

  engine->patch = std::stoi (version_str);

  return success;
}

static TfLiteDelegate *
gst_ml_tflite_engine_delegate_new (GstMLTFLiteEngine * engine,
    GstStructure * settings)
{
  TfLiteDelegate *delegate = NULL;
  gint type = GET_OPT_DELEGATE (settings);

  switch (type) {
#ifdef HAVE_NNAPI_H
    case GST_ML_TFLITE_DELEGATE_NNAPI_DSP:
    {
      auto options = engine->NnapiDelegateOptionsDefault ();

      options.accelerator_name       = "libunifiedhal-driver.so2";
      // Save power and maintain high accuracy of inference
      options.execution_preference =
          TfLiteNnapiDelegateOptions::ExecutionPreference::kSustainedSpeed;

      if ((delegate = engine->NnapiDelegateCreate (&options)) == NULL) {
        GST_WARNING ("Failed to create NN Framework DSP delegate!");
        break;
      }

      GST_INFO ("Using NN Framework DSP delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_NNAPI_GPU:
    {
      auto options = engine->NnapiDelegateOptionsDefault ();

      options.accelerator_name       = "libunifiedhal-driver.so1";
      // Save power and maintain high accuracy of inference
      options.execution_preference =
          TfLiteNnapiDelegateOptions::ExecutionPreference::kSustainedSpeed;

      if ((engine->major < 2) || (engine->major == 2 && engine->minor < 5))
        // Allow quant types to be converted to fp16 instead of fp32
        options.allow_fp16             = true;

      if ((delegate = engine->NnapiDelegateCreate (&options)) == NULL) {
        GST_WARNING ("Failed to create NN Framework DSP delegate!");
        break;
      }

      GST_INFO ("Using NN Framework GPU delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_NNAPI_NPU:
    {
      auto options = engine->NnapiDelegateOptionsDefault ();

      options.accelerator_name       = "libunifiedhal-driver.so0";
      // Save power and maintain high accuracy of inference
      options.execution_preference =
          TfLiteNnapiDelegateOptions::ExecutionPreference::kSustainedSpeed;

      if ((delegate = engine->NnapiDelegateCreate (&options)) == NULL) {
        GST_WARNING ("Failed to create NN Framework NPU delegate!");
        break;
      }

      GST_INFO ("Using NN Framework NPU delegate");
      return delegate;
    }
#endif // HAVE_NNAPI_H
#ifdef HAVE_HEXAGON_H
    case GST_ML_TFLITE_DELEGATE_HEXAGON:
    {
      auto options = engine->HexagonDelegateOptionsDefault ();

      // Initialize the Hexagon unit.
      engine->HexagonInit ();

      options.debug_level = 0;
      options.powersave_level = 0;
      options.print_graph_profile = false;
      options.print_graph_debug = false;

      if ((delegate = engine->HexagonDelegateCreate (&options)) == NULL) {
        GST_WARNING ("Failed to create Hexagon delegate!");
        break;
      }

      GST_INFO ("Using Hexagon delegate");
      return delegate;
    }
#endif // HAVE_HEXAGON_H
    case GST_ML_TFLITE_DELEGATE_GPU:
    {
      auto options = engine->GpuDelegateOptionsV2Default ();

      // Prefer minimum latency and memory usage with precision lower than fp32
      options.inference_priority1 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
      options.inference_priority2 =
          TFLITE_GPU_INFERENCE_PRIORITY_MIN_MEMORY_USAGE;
      options.inference_priority3 =
          TFLITE_GPU_INFERENCE_PRIORITY_MAX_PRECISION;
      options.inference_preference =
          TFLITE_GPU_INFERENCE_PREFERENCE_SUSTAINED_SPEED;

      if ((delegate = engine->GpuDelegateV2Create (&options)) == NULL) {
        GST_WARNING ("Failed to create GPU delegate!");
        break;
      }

      GST_INFO ("Using GPU delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_XNNPACK:
    {
      auto options = engine->XNNPackDelegateOptionsDefault ();

      if ((delegate = engine->XNNPackDelegateCreate (&options)) == NULL) {
        GST_WARNING ("Failed to create XNNPACK delegate!");
        break;
      }

      GST_INFO ("Using XNNPACK delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_EXTERNAL:
    {
      if ((engine->major < 2) || (engine->major == 2 && engine->minor < 10)) {
        GST_WARNING("External delegate is not supported !");
        break;
      }

      const gchar * path = GET_OPT_EXT_DELEGATE_PATH (settings);
      GstStructure * opts = GET_OPT_EXT_DELEGATE_OPTS (settings);

      if (path == NULL || opts == NULL) {
        GST_WARNING ("External delegate path/options not provided! "
            "Failed to create external delegate.");
        break;
      }

      auto options = engine->ExternalDelegateOptionsDefault (path);
      auto n_opts = gst_structure_n_fields(opts);

      for (auto idx = 0; idx < n_opts; idx++) {
        const gchar *name = gst_structure_nth_field_name(opts, idx);
        const gchar *value = gst_structure_get_string(opts, name);

        GST_INFO("External delegate option '%s' with value '%s'", name, value);
        options.insert(&options, name, value);
      }

      if ((delegate = engine->ExternalDelegateCreate (&options)) == NULL) {
        GST_WARNING("Failed to create external delegate");
        break;
      }

      GST_INFO("Using external delegate");
      return delegate;
    }
    default:
      GST_INFO ("No delegate will be used");
      break;
  }

  return NULL;
}

static void
gst_ml_tflite_engine_delegate_free (GstMLTFLiteEngine * engine,
    TfLiteDelegate * delegate, gint type)
{
  if (NULL == delegate)
    return;

  switch (type) {
#ifdef HAVE_NNAPI_H
    case GST_ML_TFLITE_DELEGATE_NNAPI_DSP:
    case GST_ML_TFLITE_DELEGATE_NNAPI_GPU:
    case GST_ML_TFLITE_DELEGATE_NNAPI_NPU:
      engine->NnapiDelegateDelete (delegate);
      break;
#endif // HAVE_NNAPI_H
#ifdef HAVE_HEXAGON_H
    case GST_ML_TFLITE_DELEGATE_HEXAGON:
      engine->HexagonDelegateDelete (delegate);
      engine->HexagonTearDown ();
      break;
#endif // HAVE_HEXAGON_H
    case GST_ML_TFLITE_DELEGATE_GPU:
      engine->GpuDelegateV2Delete (delegate);
      break;
    case GST_ML_TFLITE_DELEGATE_XNNPACK:
      engine->XNNPackDelegateDelete (delegate);
      break;
    case GST_ML_TFLITE_DELEGATE_EXTERNAL:
      engine->ExternalDelegateDelete (delegate);
      break;
    default:
      break;
  }

  return;
}

GstMLTFLiteEngine *
gst_ml_tflite_engine_new (GstStructure * settings)
{
  GstMLTFLiteEngine *engine = NULL;
  const gchar *filename = NULL;
  gint idx = 0, num = 0, n_threads = 1;

  engine = g_slice_new0 (GstMLTFLiteEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  auto success = gst_ml_tflite_initialize_library (engine);

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, NULL,
      gst_ml_tflite_engine_free (engine),
      "Failed to initialize tflite library!");

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  filename = GET_OPT_MODEL (engine->settings);
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (filename != NULL, NULL,
      gst_ml_tflite_engine_free (engine), "No model file name!");

  engine->model = engine->ModelCreateFromFile (filename);

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->model, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to load model file '%s'!",
      filename);

  GST_DEBUG ("Loaded model file '%s'!", filename);

  TfLiteInterpreterOptions* options = engine->InterpreterOptionsCreate ();

  engine->interpreter = engine->InterpreterCreate (engine->model, options);

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->interpreter, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to construct interpreter!");

  n_threads = GET_OPT_STHREADS (engine->settings);

  engine->InterpreterOptionsSetNumThreads (options, n_threads);
  GST_DEBUG ("Number of interpreter threads: %u", n_threads);

  engine->delegate = gst_ml_tflite_engine_delegate_new (engine,
      engine->settings);

  if (engine->delegate != NULL)
    engine->InterpreterOptionsAddDelegate (options, engine->delegate);

  engine->InterpreterOptionsDelete (options);

  if (engine->delegate != NULL) {
    TfLiteStatus status =
        engine->InterpreterModifyGraphWithDelegate(engine->interpreter,
            engine->delegate);

    if (status != TfLiteStatus::kTfLiteOk) {
      GST_WARNING ("Failed to modify graph with delegate!");
      gst_ml_tflite_engine_free (engine);
      return NULL;
    }
  }

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->InterpreterAllocateTensors (
      engine->interpreter) == kTfLiteOk, NULL, gst_ml_tflite_engine_free (
          engine), "Failed to allocate tensors!");

  engine->ininfo->n_tensors = engine->InterpreterGetInputTensorCount (
      engine->interpreter);
  engine->outinfo->n_tensors = engine->InterpreterGetOutputTensorCount (
      engine->interpreter);

  TfLiteTensor* input_tensor = engine->InterpreterGetInputTensor (
      engine->interpreter, 0);

  TfLiteType input_tensor_type = engine->TensorType (input_tensor);

  switch (input_tensor_type) {
    case kTfLiteFloat16:
      engine->ininfo->type = GST_ML_TYPE_FLOAT16;
      break;
    case kTfLiteFloat32:
      engine->ininfo->type = GST_ML_TYPE_FLOAT32;
      break;
    case kTfLiteInt32:
      engine->ininfo->type = GST_ML_TYPE_INT32;
      break;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteUInt32:
      engine->ininfo->type = GST_ML_TYPE_UINT32;
      break;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteInt8:
      engine->ininfo->type = GST_ML_TYPE_INT8;
      break;
    case kTfLiteUInt8:
      engine->ininfo->type = GST_ML_TYPE_UINT8;
      break;
    default:
      GST_ERROR ("Unsupported input tensors format!");
      gst_ml_tflite_engine_free (engine);
      return NULL;
  }

  const TfLiteTensor* output_tensor = engine->InterpreterGetOutputTensor (
      engine->interpreter, 0);

  TfLiteType output_tensor_type = engine->TensorType (output_tensor);

  switch (output_tensor_type) {
    case kTfLiteFloat16:
      engine->outinfo->type = GST_ML_TYPE_FLOAT16;
      break;
    case kTfLiteFloat32:
      engine->outinfo->type = GST_ML_TYPE_FLOAT32;
      break;
    case kTfLiteInt32:
      engine->outinfo->type = GST_ML_TYPE_INT32;
      break;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteUInt32:
      engine->outinfo->type = GST_ML_TYPE_UINT32;
      break;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteInt8:
      engine->outinfo->type = GST_ML_TYPE_INT8;
      break;
    case kTfLiteUInt8:
      engine->outinfo->type = GST_ML_TYPE_UINT8;
      break;
    default:
      GST_ERROR ("Unsupported output tensors format!");
      gst_ml_tflite_engine_free (engine);
      return NULL;
  }

  GST_DEBUG ("Number of input tensors: %u", engine->ininfo->n_tensors);
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (engine->ininfo->type));

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    TfLiteTensor* tensor = engine->InterpreterGetInputTensor (
        engine->interpreter, idx);

    auto dimensions_size = engine->TensorNumDims (tensor);

    engine->ininfo->n_dimensions[idx] = dimensions_size;

    for (num = 0; num < dimensions_size; ++num) {
      engine->ininfo->tensors[idx][num] = engine->TensorDim (tensor, num);
      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          engine->ininfo->tensors[idx][num]);
    }
  }

  GST_DEBUG ("Number of output tensors: %u", engine->outinfo->n_tensors);
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (engine->outinfo->type));

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    const TfLiteTensor* tensor =
        engine->InterpreterGetOutputTensor (engine->interpreter, idx);

    auto dimensions_size = engine->TensorNumDims (tensor);

    engine->outinfo->n_dimensions[idx] = dimensions_size;

    for (num = 0; num < dimensions_size; ++num) {
      engine->outinfo->tensors[idx][num] = engine->TensorDim (tensor, num);
      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          engine->outinfo->tensors[idx][num]);
    }
  }

  GST_INFO ("Created MLE TFLite engine: %p", engine);
  return engine;
}

void
gst_ml_tflite_engine_free (GstMLTFLiteEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->interpreter != NULL)
    engine->InterpreterDelete (engine->interpreter);

  if (engine->model != NULL)
    engine->ModelDelete (engine->model);

  gst_ml_tflite_engine_delegate_free (engine, engine->delegate,
      GET_OPT_DELEGATE (engine->settings));

  if (engine->libhandle != NULL)
    dlclose(engine->libhandle);

  if (engine->outinfo != NULL) {
    gst_ml_info_free (engine->outinfo);
    engine->outinfo = NULL;
  }

  if (engine->ininfo != NULL) {
    gst_ml_info_free (engine->ininfo);
    engine->ininfo = NULL;
  }

  if (engine->settings != NULL) {
    gst_structure_free (engine->settings);
    engine->settings = NULL;
  }

  GST_INFO ("Destroyed MLE TFLite engine: %p", engine);
  g_slice_free (GstMLTFLiteEngine, engine);
}

gboolean
gst_ml_tflite_engine_execute (GstMLTFLiteEngine * engine,
    GstMLFrame * inframe, GstMLFrame * outframe)
{
  gboolean success = FALSE;
  guint idx = 0;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (inframe != NULL, FALSE);
  g_return_val_if_fail (outframe != NULL, FALSE);

  if (GST_ML_FRAME_N_BLOCKS (inframe) != engine->ininfo->n_tensors) {
    GST_WARNING ("Input buffer has %u memory blocks but engine requires %u!",
        GST_ML_FRAME_N_BLOCKS (inframe), engine->ininfo->n_tensors);
    return FALSE;
  }

  if (GST_ML_FRAME_N_BLOCKS (outframe) != engine->outinfo->n_tensors) {
    GST_WARNING ("Output buffer has %u memory blocks but engine requires %u!",
        GST_ML_FRAME_N_BLOCKS (outframe), engine->outinfo->n_tensors);
    return FALSE;
  }

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    TfLiteTensor* tensor = engine->InterpreterGetInputTensor (
        engine->interpreter, idx);

    memcpy (engine->TensorData (tensor), GST_ML_FRAME_BLOCK_DATA (inframe, idx),
        GST_ML_FRAME_BLOCK_SIZE (inframe, idx));
  }

  success = (0 == engine->InterpreterInvoke (
      engine->interpreter));

  if (!success) {
    GST_ERROR ("Model execution failed!");
    return FALSE;
  }

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    const TfLiteTensor* tensor = engine->InterpreterGetOutputTensor (
        engine->interpreter, idx);

    memcpy (GST_ML_FRAME_BLOCK_DATA (outframe, idx),
        engine->TensorData (tensor), GST_ML_FRAME_BLOCK_SIZE (outframe, idx));
  }

  return success;
}

#else

static TfLiteDelegate *
gst_ml_tflite_engine_delegate_new (GstStructure * settings)
{
  TfLiteDelegate *delegate = NULL;
  gint type = GET_OPT_DELEGATE (settings);

  switch (type) {
    case GST_ML_TFLITE_DELEGATE_NNAPI_DSP:
    {
      tflite::StatefulNnApiDelegate::Options options;

      options.accelerator_name       = "libunifiedhal-driver.so2";
      // Save power and maintain high accuracy of inference
      options.execution_preference   =
          tflite::StatefulNnApiDelegate::Options::kSustainedSpeed;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
      // Burst computation as same delegate is used for all inputs in pipeline
      options.use_burst_computation  = true;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
      if ((delegate = new tflite::StatefulNnApiDelegate (options)) == NULL) {
        GST_WARNING ("Failed to create NN Framework DSP delegate!");
        break;
      }

      GST_INFO ("Using NN Framework DSP delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_NNAPI_GPU:
    {
      tflite::StatefulNnApiDelegate::Options options;

      options.accelerator_name       = "libunifiedhal-driver.so1";
      // Save power and maintain high accuracy of inference
      options.execution_preference   =
          tflite::StatefulNnApiDelegate::Options::kSustainedSpeed;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
      // Burst computation as same delegate is used for all inputs in pipeline
      options.use_burst_computation  = true;
      // Allow quant types to be converted to fp16 instead of fp32
      options.allow_fp16             = true;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
      if ((delegate = new tflite::StatefulNnApiDelegate (options)) == NULL) {
        GST_WARNING ("Failed to create NN Framework DSP delegate!");
        break;
      }

      GST_INFO ("Using NN Framework GPU delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_NNAPI_NPU:
    {
      tflite::StatefulNnApiDelegate::Options options;

      options.accelerator_name       = "libunifiedhal-driver.so0";
      // Save power and maintain high accuracy of inference
      options.execution_preference   =
          tflite::StatefulNnApiDelegate::Options::kSustainedSpeed;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
      // Burst computation as same delegate is used for all inputs in pipeline
      options.use_burst_computation  = true;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
      if ((delegate = new tflite::StatefulNnApiDelegate (options)) == NULL) {
        GST_WARNING ("Failed to create NN Framework NPU delegate!");
        break;
      }

      GST_INFO ("Using NN Framework NPU delegate");
      return delegate;
    }
#ifdef HAVE_HEXAGON_DELEGATE_H
    case GST_ML_TFLITE_DELEGATE_HEXAGON:
    {
      TfLiteHexagonDelegateOptions options = {};

      // Initialize the Hexagon unit.
      TfLiteHexagonInit();

      options.debug_level = 0;
      options.powersave_level = 0;
      options.print_graph_profile = false;
      options.print_graph_debug = false;

      if ((delegate = TfLiteHexagonDelegateCreate (&options)) == NULL) {
        GST_WARNING ("Failed to create Hexagon delegate!");
        break;
      }

      GST_INFO ("Using Hexagon delegate");
      return delegate;
    }
#endif // HAVE_HEXAGON_DELEGATE_H
    case GST_ML_TFLITE_DELEGATE_GPU:
    {
      TfLiteGpuDelegateOptionsV2 options = TfLiteGpuDelegateOptionsV2Default();

      // Prefer minimum latency and memory usage with precision lower than fp32
      options.inference_priority1 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
      options.inference_priority2 =
          TFLITE_GPU_INFERENCE_PRIORITY_MIN_MEMORY_USAGE;
      options.inference_priority3 =
          TFLITE_GPU_INFERENCE_PRIORITY_MAX_PRECISION;
      options.inference_preference =
          TFLITE_GPU_INFERENCE_PREFERENCE_SUSTAINED_SPEED;

      if ((delegate = TfLiteGpuDelegateV2Create (&options)) == NULL) {
        GST_WARNING ("Failed to create GPU delegate!");
        break;
      }

      GST_INFO ("Using GPU delegate");
      return delegate;
    }
    case GST_ML_TFLITE_DELEGATE_XNNPACK:
    {
      TfLiteXNNPackDelegateOptions options = TfLiteXNNPackDelegateOptionsDefault();

      if ((delegate = TfLiteXNNPackDelegateCreate(&options)) == NULL) {
        GST_WARNING ("Failed to create XNNPACK delegate!");
        break;
      }

      GST_INFO ("Using XNNPACK delegate");
      return delegate;
    }
#ifdef HAVE_EXTERNAL_DELEGATE_H
    case GST_ML_TFLITE_DELEGATE_EXTERNAL:
    {
      const gchar * path = GET_OPT_EXT_DELEGATE_PATH (settings);
      GstStructure * opts = GET_OPT_EXT_DELEGATE_OPTS (settings);

      if (path == NULL || opts == NULL) {
        GST_WARNING ("External delegate path/options not provided! "
            "Failed to create external delegate.");
        break;
      }

      TfLiteExternalDelegateOptions options =
          TfLiteExternalDelegateOptionsDefault(path);
      auto n_opts = gst_structure_n_fields(opts);

      for (auto idx = 0; idx < n_opts; idx++) {
        const gchar *name = gst_structure_nth_field_name(opts, idx);
        const gchar *value = gst_structure_get_string(opts, name);

        GST_INFO("External delegate option '%s' with value '%s'", name, value);
        options.insert(&options, name, value);
      }

      if ((delegate = TfLiteExternalDelegateCreate(&options)) == NULL) {
        GST_WARNING("Failed to create external delegate");
        break;
      }

      GST_INFO("Using external delegate");
      return delegate;
    }
#endif // HAVE_EXTERNAL_DELEGATE_H
    default:
      GST_INFO ("No delegate will be used");
      break;
  }

  return NULL;
}

static void
gst_ml_tflite_engine_delegate_free (TfLiteDelegate * delegate, gint type)
{
  if (NULL == delegate)
    return;

  switch (type) {
    case GST_ML_TFLITE_DELEGATE_NNAPI_DSP:
    case GST_ML_TFLITE_DELEGATE_NNAPI_GPU:
    case GST_ML_TFLITE_DELEGATE_NNAPI_NPU:
      delete reinterpret_cast<tflite::StatefulNnApiDelegate*>(delegate);
      break;
#ifdef HAVE_HEXAGON_DELEGATE_H
    case GST_ML_TFLITE_DELEGATE_HEXAGON:
      TfLiteHexagonDelegateDelete (delegate);
      TfLiteHexagonTearDown ();
      break;
#endif // HAVE_HEXAGON_DELEGATE_H
    case GST_ML_TFLITE_DELEGATE_GPU:
      TfLiteGpuDelegateV2Delete (delegate);
      break;
    case GST_ML_TFLITE_DELEGATE_XNNPACK:
      TfLiteXNNPackDelegateDelete (delegate);
      break;
#ifdef HAVE_EXTERNAL_DELEGATE_H
    case GST_ML_TFLITE_DELEGATE_EXTERNAL:
      TfLiteExternalDelegateDelete(delegate);
      break;
#endif // HAVE_EXTERNAL_DELEGATE_H
    default:
      break;
  }

  return;
}

GstMLTFLiteEngine *
gst_ml_tflite_engine_new (GstStructure * settings)
{
  GstMLTFLiteEngine *engine = NULL;
  const gchar *filename = NULL;
  gint idx = 0, num = 0, n_threads = 1;

  tflite::ops::builtin::BuiltinOpResolver resolver;

  engine = g_slice_new0 (GstMLTFLiteEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  filename = GET_OPT_MODEL (engine->settings);
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (filename != NULL, NULL,
      gst_ml_tflite_engine_free (engine), "No model file name!");

  engine->model = tflite::FlatBufferModel::BuildFromFile (filename).release();
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->model, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to load model file '%s'!",
      filename);

  GST_DEBUG ("Loaded model file '%s'!", filename);

  std::unique_ptr<tflite::Interpreter> interpreter;
  tflite::InterpreterBuilder builder (engine->model->GetModel(), resolver);

  builder (&interpreter);
  engine->interpreter = interpreter.release();

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->interpreter, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to construct interpreter!");

  n_threads = GET_OPT_STHREADS (engine->settings);

  engine->interpreter->SetNumThreads(n_threads);
  GST_DEBUG ("Number of interpreter threads: %u", n_threads);

  engine->delegate = gst_ml_tflite_engine_delegate_new(engine->settings);

  if (engine->delegate != NULL) {
    TfLiteStatus status =
        engine->interpreter->ModifyGraphWithDelegate(engine->delegate);

    if (status != TfLiteStatus::kTfLiteOk)
      GST_WARNING ("Failed to modify graph with delegate!");
  }

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (
      engine->interpreter->AllocateTensors() == kTfLiteOk, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to allocate tensors!");

  engine->ininfo->n_tensors = engine->interpreter->inputs().size();
  engine->outinfo->n_tensors = engine->interpreter->outputs().size();

  idx = engine->interpreter->inputs()[0];

  switch (engine->interpreter->tensor(idx)->type) {
    case kTfLiteFloat16:
      engine->ininfo->type = GST_ML_TYPE_FLOAT16;
      break;
    case kTfLiteFloat32:
      engine->ininfo->type = GST_ML_TYPE_FLOAT32;
      break;
    case kTfLiteInt32:
      engine->ininfo->type = GST_ML_TYPE_INT32;
      break;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteUInt32:
      engine->ininfo->type = GST_ML_TYPE_UINT32;
      break;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteInt8:
      engine->ininfo->type = GST_ML_TYPE_INT8;
      break;
    case kTfLiteUInt8:
      engine->ininfo->type = GST_ML_TYPE_UINT8;
      break;
    default:
      GST_ERROR ("Unsupported input tensors format!");
      gst_ml_tflite_engine_free (engine);
      return NULL;
  }

  idx = engine->interpreter->outputs()[0];

  switch (engine->interpreter->tensor(idx)->type) {
    case kTfLiteFloat16:
      engine->outinfo->type = GST_ML_TYPE_FLOAT16;
      break;
    case kTfLiteFloat32:
      engine->outinfo->type = GST_ML_TYPE_FLOAT32;
      break;
    case kTfLiteInt32:
      engine->outinfo->type = GST_ML_TYPE_INT32;
      break;
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteUInt32:
      engine->outinfo->type = GST_ML_TYPE_UINT32;
      break;
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 5)
    case kTfLiteInt8:
      engine->outinfo->type = GST_ML_TYPE_INT8;
      break;
    case kTfLiteUInt8:
      engine->outinfo->type = GST_ML_TYPE_UINT8;
      break;
    default:
      GST_ERROR ("Unsupported output tensors format!");
      gst_ml_tflite_engine_free (engine);
      return NULL;
  }

  GST_DEBUG ("Number of input tensors: %u", engine->ininfo->n_tensors);
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (engine->ininfo->type));

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    gint input = engine->interpreter->inputs()[idx];
    TfLiteIntArray* dimensions = engine->interpreter->tensor(input)->dims;

    engine->ininfo->n_dimensions[idx] = dimensions->size;

    for (num = 0; num < dimensions->size; ++num) {
      engine->ininfo->tensors[idx][num] = dimensions->data[num];
      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          engine->ininfo->tensors[idx][num]);
    }
  }

  GST_DEBUG ("Number of output tensors: %u", engine->outinfo->n_tensors);
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (engine->outinfo->type));

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    gint output = engine->interpreter->outputs()[idx];
    TfLiteIntArray* dimensions = engine->interpreter->tensor(output)->dims;

    engine->outinfo->n_dimensions[idx] = dimensions->size;

    for (num = 0; num < dimensions->size; ++num) {
      engine->outinfo->tensors[idx][num] = dimensions->data[num];
      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          engine->outinfo->tensors[idx][num]);
    }
  }

  GST_INFO ("Created MLE TFLite engine: %p", engine);
  return engine;
}

void
gst_ml_tflite_engine_free (GstMLTFLiteEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->interpreter != NULL)
    delete engine->interpreter;

  if (engine->model != NULL)
    delete engine->model;

  gst_ml_tflite_engine_delegate_free (engine->delegate,
      GET_OPT_DELEGATE (engine->settings));

  if (engine->outinfo != NULL) {
    gst_ml_info_free (engine->outinfo);
    engine->outinfo = NULL;
  }

  if (engine->ininfo != NULL) {
    gst_ml_info_free (engine->ininfo);
    engine->ininfo = NULL;
  }

  if (engine->settings != NULL) {
    gst_structure_free (engine->settings);
    engine->settings = NULL;
  }

  GST_INFO ("Destroyed MLE TFLite engine: %p", engine);
  g_slice_free (GstMLTFLiteEngine, engine);
}

gboolean
gst_ml_tflite_engine_execute (GstMLTFLiteEngine * engine,
    GstMLFrame * inframe, GstMLFrame * outframe)
{
  gboolean success = FALSE;
  guint idx = 0;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (inframe != NULL, FALSE);
  g_return_val_if_fail (outframe != NULL, FALSE);

  if (GST_ML_FRAME_N_BLOCKS (inframe) != engine->ininfo->n_tensors) {
    GST_WARNING ("Input buffer has %u memory blocks but engine requires %u!",
        GST_ML_FRAME_N_BLOCKS (inframe), engine->ininfo->n_tensors);
    return FALSE;
  }

  if (GST_ML_FRAME_N_BLOCKS (outframe) != engine->outinfo->n_tensors) {
    GST_WARNING ("Output buffer has %u memory blocks but engine requires %u!",
        GST_ML_FRAME_N_BLOCKS (outframe), engine->outinfo->n_tensors);
    return FALSE;
  }

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    gint input = engine->interpreter->inputs()[idx];
    TfLiteTensor *tensor = engine->interpreter->tensor(input);

    memcpy (tensor->data.raw, GST_ML_FRAME_BLOCK_DATA (inframe, idx),
        GST_ML_FRAME_BLOCK_SIZE (inframe, idx));
  }

  if (!(success = (engine->interpreter->Invoke() == 0)))
    GST_ERROR ("Model execution failed!");

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    gint output = engine->interpreter->outputs()[idx];
    TfLiteTensor *tensor = engine->interpreter->tensor(output);

    memcpy (GST_ML_FRAME_BLOCK_DATA (outframe, idx), tensor->data.raw,
        GST_ML_FRAME_BLOCK_SIZE (outframe, idx));
  }

  return success;
}

#endif // HAVE_EXTERNAL_DELEGATE_H

GType
gst_ml_tflite_delegate_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_TFLITE_DELEGATE_NONE,
        "No delegate, CPU is used for all operations", "none"
    },
#ifdef HAVE_NNAPI_H
    { GST_ML_TFLITE_DELEGATE_NNAPI_DSP,
        "Run the processing on the DSP through NN API. "
        "Unsupported operations will fallback on NPU, GPU or CPU",
        "nnapi-dsp"
    },
    { GST_ML_TFLITE_DELEGATE_NNAPI_GPU,
        "Run the processing on the GPU through NN API. "
        "Unsupported operations will fallback on DSP, NPU or CPU",
        "nnapi-gpu"
    },
    { GST_ML_TFLITE_DELEGATE_NNAPI_NPU,
        "Run the processing on the NPU through NN API. "
        "Unsupported operations will fallback on DSP, GPU or CPU",
        "nnapi-npu"
    },
#endif // HAVE_NNAPI_H
#ifdef HAVE_HEXAGON_DELEGATE_H
    { GST_ML_TFLITE_DELEGATE_HEXAGON,
        "Run the processing directly on the Hexagon DSP", "hexagon"
    },
#endif // HAVE_HEXAGON_DELEGATE_H
    { GST_ML_TFLITE_DELEGATE_GPU,
        "Run the processing directly on the GPU", "gpu"
    },
    {
      GST_ML_TFLITE_DELEGATE_XNNPACK,
        "Run inferences using xnnpack cpu runtime", "xnnpack"
    },
#ifdef HAVE_EXTERNAL_DELEGATE_H
    {
      GST_ML_TFLITE_DELEGATE_EXTERNAL,
        "Run the processing on external delegate. It uses two plugin properties"
        " external-delegate-path and external-delegate-options.", "external"
    },
#endif // HAVE_EXTERNAL_DELEGATE_H
    {0, NULL, NULL},
  };

  if (!gtype)
      gtype = g_enum_register_static ("GstMLTFLiteDelegate", variants);

  return gtype;
}
