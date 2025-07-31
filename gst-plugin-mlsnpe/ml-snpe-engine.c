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

#include "ml-snpe-engine.h"

#include <stdbool.h>
#include <dlfcn.h>

#include <gst/ml/gstmlmeta.h>

#include <DlContainer/DlContainer.h>
#include <DlSystem/IUserBuffer.h>
#include <SNPE/SNPE.h>
#include <SNPE/SNPEUtil.h>
#include <SNPE/SNPEBuilder.h>

#define GST_CAT_DEFAULT gst_ml_snpe_engine_debug_category()

struct _GstMLSnpeEngine
{
  GstMLInfo                   *ininfo;
  GstMLInfo                   *outinfo;

  // SNPE container model.
  Snpe_DlContainer_Handle_t   model;
  // SNPE Builder constructed from the container model.
  Snpe_SNPEBuilder_Handle_t   builder;
  // SNPE model interpreter.
  Snpe_SNPE_Handle_t          interpreter;

  // List of output tensor names.
  Snpe_StringList_Handle_t    outnames;
  // Map between SNPE input tensor names and corresponding User Buffer.
  Snpe_UserBufferMap_Handle_t inputs;
  // Map between SNPE output tensor names and corresponding User Buffer.
  Snpe_UserBufferMap_Handle_t outputs;

  // SNPE backend library handle.
  gpointer                    libhandle;

  // SNPE library APIs.
  SNPE_API Snpe_DlContainer_Handle_t (*DlContainerOpen) (const char*);
  SNPE_API Snpe_ErrorCode_t (*DlContainerDelete) (Snpe_DlContainer_Handle_t);

  SNPE_API Snpe_SNPEBuilder_Handle_t (*SNPEBuilderCreate) (
      Snpe_DlContainer_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*SNPEBuilderDelete) (Snpe_SNPEBuilder_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*SNPEBuilderSetOutputLayers) (
      Snpe_SNPEBuilder_Handle_t, Snpe_StringList_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*SNPEBuilderSetOutputTensors) (
      Snpe_SNPEBuilder_Handle_t, Snpe_StringList_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*SNPEBuilderSetRuntimeProcessorOrder) (
      Snpe_SNPEBuilder_Handle_t, Snpe_RuntimeList_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*SNPEBuilderSetUseUserSuppliedBuffers) (
      Snpe_SNPEBuilder_Handle_t, int);
  SNPE_API Snpe_ErrorCode_t (*SnpeSNPEBuilderSetPerformanceProfile) (
      Snpe_SNPEBuilder_Handle_t, Snpe_PerformanceProfile_t);
  SNPE_API Snpe_ErrorCode_t (*SnpeSNPEBuilderSetProfilingLevel) (
      Snpe_SNPEBuilder_Handle_t, Snpe_ProfilingLevel_t);
  SNPE_API Snpe_ErrorCode_t (*SnpeSNPEBuilderSetExecutionPriorityHint) (
      Snpe_SNPEBuilder_Handle_t, Snpe_ExecutionPriorityHint_t);
  SNPE_API Snpe_SNPE_Handle_t (*SNPEBuilderBuild) (Snpe_SNPEBuilder_Handle_t);

  SNPE_API Snpe_ErrorCode_t (*SNPE_Delete) (Snpe_SNPE_Handle_t);
  SNPE_API const char* (*SNPE_GetModelVersion) (Snpe_SNPE_Handle_t);
  SNPE_API Snpe_StringList_Handle_t (*SNPE_GetInputTensorNames) (
      Snpe_SNPE_Handle_t);
  SNPE_API Snpe_StringList_Handle_t (*SNPE_GetOutputTensorNames) (
      Snpe_SNPE_Handle_t);
  SNPE_API Snpe_IBufferAttributes_Handle_t (*SNPE_GetInputOutputBufferAttributes)(
      Snpe_SNPE_Handle_t, const char *);
  SNPE_API Snpe_ErrorCode_t (*SNPE_ExecuteUserBuffers) (
    Snpe_SNPE_Handle_t, Snpe_UserBufferMap_Handle_t, Snpe_UserBufferMap_Handle_t);

  SNPE_API Snpe_RuntimeList_Handle_t (*RuntimeListCreate) ();
  SNPE_API Snpe_ErrorCode_t (*RuntimeListDelete) (Snpe_RuntimeList_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*RuntimeListAdd) (
        Snpe_RuntimeList_Handle_t, Snpe_Runtime_t);

  SNPE_API Snpe_StringList_Handle_t (*StringListCreate) ();
  SNPE_API Snpe_StringList_Handle_t (*StringListCreateCopy) (Snpe_StringList_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*StringListDelete) (Snpe_StringList_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*StringListAppend) (
      Snpe_StringList_Handle_t, const char*);
  SNPE_API size_t (*StringListSize) (Snpe_StringList_Handle_t);
  SNPE_API const char* (*StringListAt) (Snpe_StringList_Handle_t, size_t);

  SNPE_API Snpe_ErrorCode_t (*IBufferAttributesDelete) (
      Snpe_IBufferAttributes_Handle_t);
  SNPE_API Snpe_UserBufferEncoding_ElementType_t (*IBufferAttributesGetEncodingType)(
      Snpe_IBufferAttributes_Handle_t);
  SNPE_API Snpe_TensorShape_Handle_t (*IBufferAttributesGetDims) (
      Snpe_IBufferAttributes_Handle_t);
  SNPE_API Snpe_UserBufferEncoding_Handle_t (*IBufferAttributesGetEncoding) (
      Snpe_IBufferAttributes_Handle_t);

  SNPE_API Snpe_ErrorCode_t (*TensorShapeDelete) (Snpe_TensorShape_Handle_t);
  SNPE_API size_t (*TensorShapeRank) (Snpe_TensorShape_Handle_t);
  SNPE_API Snpe_TensorShape_Handle_t (*TensorShapeCreateDimsSize) (
      const size_t *, size_t);
  SNPE_API const size_t* (*TensorShapeGetDimensions) (Snpe_TensorShape_Handle_t);

  SNPE_API Snpe_UserBufferMap_Handle_t (*UserBufferMapCreate) ();
  SNPE_API Snpe_ErrorCode_t (*UserBufferMapDelete) (Snpe_UserBufferMap_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*UserBufferMapAdd) (
      Snpe_UserBufferMap_Handle_t, const char *, Snpe_IUserBuffer_Handle_t);
  SNPE_API Snpe_ErrorCode_t (*UserBufferMapRemove)(
      Snpe_UserBufferMap_Handle_t, const char *);
  SNPE_API Snpe_IUserBuffer_Handle_t (*UserBufferMapGet) (
      Snpe_UserBufferMap_Handle_t handle , const char *name);

  SNPE_API Snpe_ErrorCode_t (*IUserBufferDelete) (Snpe_IUserBuffer_Handle_t);
  SNPE_API int (*IUserBufferSetBufferAddress) (Snpe_IUserBuffer_Handle_t, void*);

  SNPE_API Snpe_IUserBuffer_Handle_t (*UtilCreateUserBuffer) (
      void *, size_t, Snpe_TensorShape_Handle_t, Snpe_IUserBuffer_Handle_t);

  SNPE_API Snpe_UserBufferEncoding_Handle_t (*UserBufferEncodingFloatCreate) ();
  SNPE_API Snpe_ErrorCode_t (*UserBufferEncodingFloatDelete) (
      Snpe_UserBufferEncoding_Handle_t);
  SNPE_API Snpe_UserBufferEncoding_Handle_t (*UserBufferEncodingUnsigned8BitCreate) ();
  SNPE_API Snpe_ErrorCode_t (*UserBufferEncodingUnsigned8BitDelete) (
      Snpe_UserBufferEncoding_Handle_t);
};

static GstDebugCategory *
gst_ml_snpe_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("ml-snpe-engine", 0,
        "Machine Learning SNPE Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

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

GType
gst_ml_snpe_delegate_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_SNPE_DELEGATE_NONE,
        "No delegate, CPU is used for all operations", "none"
    },
    { GST_ML_SNPE_DELEGATE_DSP,
        "Run the processing on the Hexagon DSP", "dsp"
    },
    { GST_ML_SNPE_DELEGATE_GPU,
        "Run the processing on the Adreno GPU", "gpu"
    },
    { GST_ML_SNPE_DELEGATE_AIP,
        "Run the processing on Snapdragon AIX + HVX", "aip"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
      gtype = g_enum_register_static ("GstMLSnpeDelegate", variants);

  return gtype;
}

GType
gst_ml_snpe_perf_profile_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_SNPE_PERF_PROFILE_DEFAULT,
        "Run in a standard mode",
        "default"
    },
    { GST_ML_SNPE_PERF_PROFILE_BALANCED,
        "Run in a balanced mode",
        "balanced"
    },
    { GST_ML_SNPE_PERF_PROFILE_HIGH_PERFORMANCE,
        "Run in high performance mode",
        "high-performance"
    },
    { GST_ML_SNPE_PERF_PROFILE_POWER_SAVER,
        "Run in a power sensitive mode, at the expense of performance",
        "power-saver"
    },
    { GST_ML_SNPE_PERF_PROFILE_SYSTEM_SETTINGS,
        "Use system settings. no calls to performance APIs",
        "system-settings"
    },
    { GST_ML_SNPE_PERF_PROFILE_SUSTAINED_HIGH_PERFORMANCE,
        "Run in sustained high performance mode",
        "sustained-high-performance"
    },
    { GST_ML_SNPE_PERF_PROFILE_BURST,
        "Run in burst mode",
        "burst"
    },
    { GST_ML_SNPE_PERF_PROFILE_LOW_POWER_SAVER,
        "Run in lower clock than POWER_SAVER with less performance",
        "low-power-saver"
    },
    { GST_ML_SNPE_PERF_PROFILE_HIGH_POWER_SAVER,
        "Higher clock and better performance compared to POWER_SAVER",
        "high-power-saver"
    },
    { GST_ML_SNPE_PERF_PROFILE_LOW_BALANCED,
        "Run in lower balanced mode",
        "low-balanced"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLSnpePerformanceProfile", variants);

  return gtype;
}

GType
gst_ml_snpe_profiling_level_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_SNPE_PROFILING_LEVEL_OFF,
        "No profiling. Collects no runtime stats in the DiagLog", "off"
    },
    { GST_ML_SNPE_PROFILING_LEVEL_BASIC,
        "Basic profiling Collects some runtime stats in the DiagLog", "basic"
    },
    { GST_ML_SNPE_PROFILING_LEVEL_DETAILED,
        "Detailed profiling Collects more runtime stats in the DiagLog", "detailed"
    },
    { GST_ML_SNPE_PROFILING_LEVEL_MODERATE,
        "Moderate profiling Collects more runtime stats in the DiagLog", "moderate"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLSnpeProfilingLevel", variants);

  return gtype;
}

GType
gst_ml_snpe_exec_priority_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_SNPE_EXEC_PRIORITY_NORMAL,
        "Normal priority", "normal"
    },
    { GST_ML_SNPE_EXEC_PRIORITY_HIGH,
        "Higher than normal priority", "high"
    },
    { GST_ML_SNPE_EXEC_PRIORITY_LOW,
        "Lower priority", "low"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLSnpeExecutionPriority", variants);

  return gtype;
}

static GstMLType
snpe_to_ml_type (Snpe_UserBufferEncoding_ElementType_t type)
{
  switch (type) {
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT16:
      return GST_ML_TYPE_FLOAT16;
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT:
      return GST_ML_TYPE_FLOAT32;
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT8:
      return GST_ML_TYPE_INT8;
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNSIGNED8BIT:
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF8:
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT8:
      return GST_ML_TYPE_UINT8;
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT32:
      return GST_ML_TYPE_INT32;
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT32:
      return GST_ML_TYPE_UINT32;
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF16:
    case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT16:
      return GST_ML_TYPE_UINT16;
    default:
      GST_ERROR ("Unsupported SNPE element type 0x%x!", type);
      break;
  }

  return GST_ML_TYPE_UNKNOWN;
}

static gboolean
gst_ml_snpe_engine_setup_input_tensors (GstMLSnpeEngine *engine)
{
  Snpe_StringList_Handle_t names = NULL;
  Snpe_IBufferAttributes_Handle_t attribs = NULL;
  Snpe_TensorShape_Handle_t shape = NULL, strides = NULL;
  Snpe_UserBufferEncoding_Handle_t encoding = NULL;
  Snpe_IUserBuffer_Handle_t usrbuffer = NULL;
  const size_t *dimensions = NULL;
  gint idx = 0, num = 0, n_tensors = 0, rank = 0, size = 0;
  gboolean success = FALSE;

  if ((engine->inputs = engine->UserBufferMapCreate ()) == NULL) {
    GST_ERROR ("Failed to create map for the input user buffers!");
    return FALSE;
  }

  names = engine->SNPE_GetInputTensorNames (engine->interpreter);
  success = (names != NULL) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Failed to retrieve input tensor names!");
    return FALSE;
  }

  n_tensors = engine->StringListSize (names);
  engine->ininfo->n_tensors = n_tensors;

  for (idx = 0; idx < n_tensors; idx++) {
    const char *name = engine->StringListAt (names, idx);
    size_t stride[GST_ML_TENSOR_MAX_DIMS] = { 0, };

    GST_DEBUG ("Input tensor[%u] name: %s", idx, name);

    attribs =
        engine->SNPE_GetInputOutputBufferAttributes (engine->interpreter, name);
    success = (attribs != NULL) ? TRUE : FALSE;

    if (!success) {
      GST_ERROR ("Failed to get attributes for input tensor '%s'!", name);
      engine->IBufferAttributesDelete (attribs);
      goto cleanup;
    }

    GST_ML_INFO_TYPE (engine->ininfo) =
        snpe_to_ml_type (engine->IBufferAttributesGetEncodingType (attribs));

    shape = engine->IBufferAttributesGetDims (attribs);
    dimensions = engine->TensorShapeGetDimensions (shape);
    rank = engine->TensorShapeRank (shape);

    success = (rank <= GST_ML_TENSOR_MAX_DIMS) ? TRUE : FALSE;

    if (!success) {
      GST_ERROR ("Input tensor '%s' rank is not supported!", name);
      engine->TensorShapeDelete (shape);
      engine->IBufferAttributesDelete (attribs);
      goto cleanup;
    }

    GST_ML_INFO_N_DIMENSIONS (engine->ininfo, idx) = rank;

    for (num = 0; num < rank; ++num) {
      GST_ML_INFO_TENSOR_DIM (engine->ininfo, idx, num) = dimensions[num];
      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          GST_ML_INFO_TENSOR_DIM (engine->ininfo, idx, num));
    }

    stride[rank - 1] = gst_ml_type_get_size (GST_ML_INFO_TYPE (engine->ininfo));

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (rank - 1); num > 0; num--)
      stride[num - 1] = dimensions[num] * stride[num];

    // Tensor shape is no longer needed, deallocate.
    engine->TensorShapeDelete (shape);

    strides = engine->TensorShapeCreateDimsSize (stride, rank);
    encoding = engine->IBufferAttributesGetEncoding (attribs);
    size = gst_ml_info_tensor_size (engine->ininfo, idx);

    // Empty User Buffer which will later be set via setBufferAddress API.
    usrbuffer = engine->UtilCreateUserBuffer (NULL, size, strides, encoding);
    success = (usrbuffer != NULL) ? TRUE : FALSE;

    engine->TensorShapeDelete (strides);
    engine->IBufferAttributesDelete (attribs);

    if (!success) {
      GST_ERROR ("Failed to create buffer for tensor %d!", idx);
      goto cleanup;
    }

    engine->UserBufferMapAdd (engine->inputs, name, usrbuffer);
  }

  GST_DEBUG ("Number of input tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->ininfo));
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->ininfo)));

cleanup:
  if (names != NULL)
    engine->StringListDelete (names);

  return success;
}

static gboolean
gst_ml_snpe_engine_setup_output_tensors (GstMLSnpeEngine *engine)
{
  Snpe_IBufferAttributes_Handle_t attribs = NULL;
  Snpe_TensorShape_Handle_t shape = NULL, strides = NULL;
  Snpe_UserBufferEncoding_Handle_t encoding = NULL;
  Snpe_IUserBuffer_Handle_t usrbuffer = NULL;
  const size_t *dimensions = NULL;
  gint idx = 0, num = 0, n_tensors = 0, rank = 0, size = 0;
  gboolean success = FALSE;

  if ((engine->outputs = engine->UserBufferMapCreate ()) == NULL) {
    GST_ERROR ("Failed to create map for the input user buffers!");
    return FALSE;
  }

  n_tensors = engine->StringListSize (engine->outnames);
  engine->outinfo->n_tensors = n_tensors;

  for (idx = 0; idx < n_tensors; idx++) {
    const char *name = engine->StringListAt (engine->outnames, idx);
    size_t stride[GST_ML_TENSOR_MAX_DIMS] = { 0, };

    GST_DEBUG ("Output tensor[%u] name: %s", idx, name);

    attribs =
        engine->SNPE_GetInputOutputBufferAttributes (engine->interpreter, name);
    success = (attribs != NULL) ? TRUE : FALSE;

    if (!success) {
      GST_ERROR ("Failed to get attributes for output tensor '%s'!", name);
      engine->IBufferAttributesDelete (attribs);
      return FALSE;
    }

    GST_ML_INFO_TYPE (engine->outinfo) =
        snpe_to_ml_type (engine->IBufferAttributesGetEncodingType (attribs));

    shape = engine->IBufferAttributesGetDims (attribs);
    dimensions = engine->TensorShapeGetDimensions (shape);
    rank = engine->TensorShapeRank (shape);

    success = (rank <= GST_ML_TENSOR_MAX_DIMS) ? TRUE : FALSE;

    if (!success) {
      GST_ERROR ("Output tensor '%s' rank is not supported!", name);
      engine->TensorShapeDelete (shape);
      engine->IBufferAttributesDelete (attribs);
      return FALSE;
    }

    GST_ML_INFO_N_DIMENSIONS (engine->outinfo, idx) = rank;

    for (num = 0; num < rank; ++num) {
      GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num) = dimensions[num];
      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num));
    }

    stride[rank - 1] = gst_ml_type_get_size (GST_ML_INFO_TYPE (engine->outinfo));

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (rank - 1); num > 0; num--)
      stride[num - 1] = dimensions[num] * stride[num];

    // Tensor shape is no longer needed, deallocate.
    engine->TensorShapeDelete (shape);

    strides = engine->TensorShapeCreateDimsSize (stride, rank);
    encoding = engine->IBufferAttributesGetEncoding (attribs);
    size = gst_ml_info_tensor_size (engine->outinfo, idx);

    // Empty User Buffer which will later be set via setBufferAddress API.
    usrbuffer = engine->UtilCreateUserBuffer (NULL, size, strides, encoding);
    success = (usrbuffer != NULL) ? TRUE : FALSE;

    engine->TensorShapeDelete (strides);
    engine->IBufferAttributesDelete (attribs);

    if (!success) {
      GST_ERROR ("Failed to create buffer for tensor %d!", idx);
      return FALSE;
    }

    engine->UserBufferMapAdd (engine->outputs, name, usrbuffer);
  }

  GST_DEBUG ("Number of output tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->outinfo));
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));

  return success;
}

static gboolean
gst_ml_snpe_engine_setup_backend (GstMLSnpeEngine *engine,
    GstMLSnpeSettings * settings)
{
  Snpe_StringList_Handle_t strlist = NULL;
  Snpe_RuntimeList_Handle_t rtlist = NULL;
  Snpe_ErrorCode_t error = SNPE_SUCCESS;
  GList *ls = NULL;
  const gchar *version = NULL;
  gboolean success = FALSE;

  if ((engine->model = engine->DlContainerOpen (settings->modelfile)) == NULL) {
    GST_ERROR ("Failed to load model file '%s'!", settings->modelfile);
    return FALSE;
  }

  GST_DEBUG ("Loaded model file '%s'!", settings->modelfile);

  if ((engine->builder = engine->SNPEBuilderCreate (engine->model)) == NULL) {
    GST_ERROR ("Failed to create SNPE builder!");
    return FALSE;
  }

  if ((strlist = engine->StringListCreate ()) == NULL) {
    GST_ERROR ("Failed to string list for output layers/tensors!");
    return FALSE;
  }

  switch (settings->perf_profile) {
    case GST_ML_SNPE_PERF_PROFILE_DEFAULT:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_DEFAULT);
      break;
    case GST_ML_SNPE_PERF_PROFILE_BALANCED:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_BALANCED);
      break;
    case GST_ML_SNPE_PERF_PROFILE_HIGH_PERFORMANCE:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_HIGH_PERFORMANCE);
      break;
    case GST_ML_SNPE_PERF_PROFILE_POWER_SAVER:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_POWER_SAVER);
      break;
    case GST_ML_SNPE_PERF_PROFILE_SYSTEM_SETTINGS:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_SYSTEM_SETTINGS);
      break;
    case GST_ML_SNPE_PERF_PROFILE_SUSTAINED_HIGH_PERFORMANCE:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_SUSTAINED_HIGH_PERFORMANCE);
      break;
    case GST_ML_SNPE_PERF_PROFILE_BURST:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_BURST);
      break;
    case GST_ML_SNPE_PERF_PROFILE_LOW_POWER_SAVER:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_LOW_POWER_SAVER);
      break;
    case GST_ML_SNPE_PERF_PROFILE_HIGH_POWER_SAVER:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_HIGH_POWER_SAVER);
      break;
    case GST_ML_SNPE_PERF_PROFILE_LOW_BALANCED:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_LOW_BALANCED);
      break;
    default:
      engine->SnpeSNPEBuilderSetPerformanceProfile (engine->builder,
          SNPE_PERFORMANCE_PROFILE_DEFAULT);
      break;
  }

  switch (settings->profiling_level) {
    case GST_ML_SNPE_PROFILING_LEVEL_OFF:
      engine->SnpeSNPEBuilderSetProfilingLevel (engine->builder,
          SNPE_PROFILING_LEVEL_OFF);
      break;
    case GST_ML_SNPE_PROFILING_LEVEL_BASIC:
      engine->SnpeSNPEBuilderSetProfilingLevel (engine->builder,
          SNPE_PROFILING_LEVEL_BASIC);
      break;
    case GST_ML_SNPE_PROFILING_LEVEL_DETAILED:
      engine->SnpeSNPEBuilderSetProfilingLevel (engine->builder,
          SNPE_PROFILING_LEVEL_DETAILED);
      break;
    case GST_ML_SNPE_PROFILING_LEVEL_MODERATE:
      engine->SnpeSNPEBuilderSetProfilingLevel (engine->builder,
          SNPE_PROFILING_LEVEL_MODERATE);
      break;
    default:
      engine->SnpeSNPEBuilderSetProfilingLevel (engine->builder,
          SNPE_PROFILING_LEVEL_OFF);
      break;
  }

  switch (settings->exec_priority) {
    case GST_ML_SNPE_EXEC_PRIORITY_NORMAL:
      engine->SnpeSNPEBuilderSetExecutionPriorityHint (engine->builder,
          SNPE_EXECUTION_PRIORITY_NORMAL);
      break;
    case GST_ML_SNPE_EXEC_PRIORITY_HIGH:
      engine->SnpeSNPEBuilderSetExecutionPriorityHint (engine->builder,
          SNPE_EXECUTION_PRIORITY_HIGH);
      break;
    case GST_ML_SNPE_EXEC_PRIORITY_LOW:
      engine->SnpeSNPEBuilderSetExecutionPriorityHint (engine->builder,
          SNPE_EXECUTION_PRIORITY_LOW);
      break;
    default:
      engine->SnpeSNPEBuilderSetExecutionPriorityHint (engine->builder,
          SNPE_EXECUTION_PRIORITY_NORMAL);
      break;
  }

  for (ls = settings->outputs; ls != NULL; ls = ls->next)
    engine->StringListAppend (strlist, (const gchar *) ls->data);

  if (settings->is_tensor) {
    error = engine->SNPEBuilderSetOutputTensors (engine->builder, strlist);
    success = (error == SNPE_SUCCESS) ? TRUE : FALSE;
  } else {
    error = engine->SNPEBuilderSetOutputLayers (engine->builder, strlist);
    success = (error == SNPE_SUCCESS) ? TRUE : FALSE;
  }

  if (!success) {
    GST_ERROR ("Failed to set output layers, error: '%d'!", error);
    goto cleanup;
  }

  rtlist = engine->RuntimeListCreate ();
  success = (rtlist != NULL) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Failed to create string list for runtime order!");
    goto cleanup;
  }

  switch (settings->delegate) {
    case GST_ML_SNPE_DELEGATE_DSP:
      engine->RuntimeListAdd (rtlist, SNPE_RUNTIME_DSP);
      GST_INFO ("Delegate preference: DSP > CPU");
      break;
    case GST_ML_SNPE_DELEGATE_GPU:
      engine->RuntimeListAdd (rtlist, SNPE_RUNTIME_GPU);
      GST_INFO ("Delegate preference: GPU > CPU");
      break;
    case GST_ML_SNPE_DELEGATE_AIP:
      engine->RuntimeListAdd (rtlist, SNPE_RUNTIME_AIP_FIXED8_TF);
      GST_INFO ("Delegate preference: AIP > CPU");
      break;
    default:
      GST_INFO ("No delegate preference, CPU will be used");
      break;
  }

  engine->RuntimeListAdd (rtlist, SNPE_RUNTIME_CPU);

  error = engine->SNPEBuilderSetRuntimeProcessorOrder (engine->builder, rtlist);
  success = (error == SNPE_SUCCESS) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Failed to set processor preferences, error: '%d'!", error);
    goto cleanup;
  }

  error = engine->SNPEBuilderSetUseUserSuppliedBuffers (engine->builder, 1);
  success = (error == SNPE_SUCCESS) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Failed to set User Suppplied Buffers mode, error: '%d'!", error);
    goto cleanup;
  }

  engine->interpreter = engine->SNPEBuilderBuild (engine->builder);
  success = (engine->interpreter != NULL) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Failed to create model interpreter!");
    goto cleanup;
  }

  version = engine->SNPE_GetModelVersion (engine->interpreter);
  GST_INFO ("Created Interpreter for model version '%s'", version);

  if (settings->is_tensor) {
    engine->outnames = engine->StringListCreateCopy (strlist);
  } else {
    engine->outnames = engine->SNPE_GetOutputTensorNames (engine->interpreter);
  }

  success = (engine->outnames != NULL) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Failed to get output tensor names!");
    goto cleanup;
  }

cleanup:
  if (rtlist != NULL)
    engine->RuntimeListDelete (rtlist);

  if (strlist != NULL)
    engine->StringListDelete (strlist);

  return success;
}

GstMLSnpeEngine *
gst_ml_snpe_engine_new (GstMLSnpeSettings * settings)
{
  GstMLSnpeEngine *engine = NULL;
  gboolean success = TRUE;

  engine = g_new0 (GstMLSnpeEngine, 1);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->outnames = NULL;
  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->libhandle = dlopen ("libSNPE.so", RTLD_NOW | RTLD_LOCAL);
  if (engine->libhandle == NULL) {
    GST_ERROR ("Failed to open SNPE library, error: %s!", dlerror());
    goto error;
  }

  success &= load_symbol ((gpointer*)&engine->DlContainerOpen,
      engine->libhandle, "Snpe_DlContainer_Open");
  success &= load_symbol ((gpointer*)&engine->DlContainerDelete,
      engine->libhandle, "Snpe_DlContainer_Delete");

  success &= load_symbol ((gpointer*)&engine->SNPEBuilderCreate,
      engine->libhandle, "Snpe_SNPEBuilder_Create");
  success &= load_symbol ((gpointer*)&engine->SNPEBuilderDelete,
      engine->libhandle, "Snpe_SNPEBuilder_Delete");
  success &= load_symbol ((gpointer*)&engine->SNPEBuilderSetOutputLayers,
      engine->libhandle, "Snpe_SNPEBuilder_SetOutputLayers");
  success &= load_symbol ((gpointer*)&engine->SNPEBuilderSetOutputTensors,
      engine->libhandle, "Snpe_SNPEBuilder_SetOutputTensors");
  success &= load_symbol ((gpointer*)&engine->SNPEBuilderSetRuntimeProcessorOrder,
      engine->libhandle, "Snpe_SNPEBuilder_SetRuntimeProcessorOrder");
  success &= load_symbol ((gpointer*)&engine->SNPEBuilderSetUseUserSuppliedBuffers,
      engine->libhandle, "Snpe_SNPEBuilder_SetUseUserSuppliedBuffers");
  success &= load_symbol ((gpointer*)&engine->SnpeSNPEBuilderSetPerformanceProfile,
      engine->libhandle, "Snpe_SNPEBuilder_SetPerformanceProfile");
  success &= load_symbol ((gpointer*)&engine->SnpeSNPEBuilderSetProfilingLevel,
      engine->libhandle, "Snpe_SNPEBuilder_SetProfilingLevel");
  success &= load_symbol ((gpointer*)&engine->SnpeSNPEBuilderSetExecutionPriorityHint,
      engine->libhandle, "Snpe_SNPEBuilder_SetExecutionPriorityHint");
  success &= load_symbol ((gpointer*)&engine->SNPEBuilderBuild,
      engine->libhandle, "Snpe_SNPEBuilder_Build");

  success &= load_symbol ((gpointer*)&engine->SNPE_Delete,
      engine->libhandle, "Snpe_SNPE_Delete");
  success &= load_symbol ((gpointer*)&engine->SNPE_GetModelVersion,
      engine->libhandle, "Snpe_SNPE_GetModelVersion");
  success &= load_symbol ((gpointer*)&engine->SNPE_GetInputTensorNames,
      engine->libhandle, "Snpe_SNPE_GetInputTensorNames");
  success &= load_symbol ((gpointer*)&engine->SNPE_GetOutputTensorNames,
      engine->libhandle, "Snpe_SNPE_GetOutputTensorNames");
  success &= load_symbol ((gpointer*)&engine->SNPE_GetInputOutputBufferAttributes,
      engine->libhandle, "Snpe_SNPE_GetInputOutputBufferAttributes");
  success &= load_symbol ((gpointer*)&engine->SNPE_ExecuteUserBuffers,
      engine->libhandle, "Snpe_SNPE_ExecuteUserBuffers");

  success &= load_symbol ((gpointer*)&engine->RuntimeListCreate,
      engine->libhandle, "Snpe_RuntimeList_Create");
  success &= load_symbol ((gpointer*)&engine->RuntimeListDelete,
      engine->libhandle, "Snpe_RuntimeList_Delete");
  success &= load_symbol ((gpointer*)&engine->RuntimeListAdd,
      engine->libhandle, "Snpe_RuntimeList_Add");

  success &= load_symbol ((gpointer*)&engine->StringListCreate,
      engine->libhandle, "Snpe_StringList_Create");
  success &= load_symbol ((gpointer*)&engine->StringListCreateCopy,
      engine->libhandle, "Snpe_StringList_CreateCopy");
  success &= load_symbol ((gpointer*)&engine->StringListDelete,
      engine->libhandle, "Snpe_StringList_Delete");
  success &= load_symbol ((gpointer*)&engine->StringListAppend,
      engine->libhandle, "Snpe_StringList_Append");
  success &= load_symbol ((gpointer*)&engine->StringListSize,
      engine->libhandle, "Snpe_StringList_Size");
  success &= load_symbol ((gpointer*)&engine->StringListAt,
      engine->libhandle, "Snpe_StringList_At");

  success &= load_symbol ((gpointer*)&engine->IBufferAttributesDelete,
      engine->libhandle, "Snpe_IBufferAttributes_Delete");
  success &= load_symbol ((gpointer*)&engine->IBufferAttributesGetEncodingType,
      engine->libhandle, "Snpe_IBufferAttributes_GetEncodingType");
  success &= load_symbol ((gpointer*)&engine->IBufferAttributesGetDims,
      engine->libhandle, "Snpe_IBufferAttributes_GetDims");
  success &= load_symbol ((gpointer*)&engine->IBufferAttributesGetEncoding,
      engine->libhandle, "Snpe_IBufferAttributes_GetEncoding_Ref");

  success &= load_symbol ((gpointer*)&engine->TensorShapeDelete,
      engine->libhandle, "Snpe_TensorShape_Delete");
  success &= load_symbol ((gpointer*)&engine->TensorShapeRank,
      engine->libhandle, "Snpe_TensorShape_Rank");
  success &= load_symbol ((gpointer*)&engine->TensorShapeCreateDimsSize,
      engine->libhandle, "Snpe_TensorShape_CreateDimsSize");
  success &= load_symbol ((gpointer*)&engine->TensorShapeGetDimensions,
      engine->libhandle, "Snpe_TensorShape_GetDimensions");

  success &= load_symbol ((gpointer*)&engine->UserBufferMapCreate,
      engine->libhandle, "Snpe_UserBufferMap_Create");
  success &= load_symbol ((gpointer*)&engine->UserBufferMapDelete,
      engine->libhandle, "Snpe_UserBufferMap_Delete");
  success &= load_symbol ((gpointer*)&engine->UserBufferMapAdd,
      engine->libhandle, "Snpe_UserBufferMap_Add");
  success &= load_symbol ((gpointer*)&engine->UserBufferMapRemove,
      engine->libhandle, "Snpe_UserBufferMap_Remove");
  success &= load_symbol ((gpointer*)&engine->UserBufferMapGet,
      engine->libhandle, "Snpe_UserBufferMap_GetUserBuffer_Ref");

  success &= load_symbol ((gpointer*)&engine->IUserBufferDelete,
      engine->libhandle, "Snpe_IUserBuffer_Delete");
  success &= load_symbol ((gpointer*)&engine->IUserBufferSetBufferAddress,
      engine->libhandle, "Snpe_IUserBuffer_SetBufferAddress");

  success &= load_symbol ((gpointer*)&engine->UtilCreateUserBuffer,
      engine->libhandle, "Snpe_Util_CreateUserBuffer");

  success &= load_symbol ((gpointer*)&engine->UserBufferEncodingFloatCreate,
      engine->libhandle, "Snpe_UserBufferEncodingFloat_Create");
  success &= load_symbol ((gpointer*)&engine->UserBufferEncodingFloatDelete,
      engine->libhandle, "Snpe_UserBufferEncodingFloat_Delete");
  success &= load_symbol ((gpointer*)&engine->UserBufferEncodingUnsigned8BitCreate,
      engine->libhandle, "Snpe_UserBufferEncodingUnsigned8Bit_Create");
  success &= load_symbol ((gpointer*)&engine->UserBufferEncodingUnsigned8BitDelete,
      engine->libhandle, "Snpe_UserBufferEncodingUnsigned8Bit_Delete");

  // Check whether symbol loading was successful.
  if (!success)
    goto error;

  success = gst_ml_snpe_engine_setup_backend (engine, settings);
  if (!success) {
    GST_ERROR ("Failed to set setup SNPE backend!");
    goto error;
  }

  if (!gst_ml_snpe_engine_setup_input_tensors (engine)) {
    GST_ERROR ("Failed to set setup input tensors!");
    goto error;
  }

  if (!gst_ml_snpe_engine_setup_output_tensors (engine)) {
    GST_ERROR ("Failed to set setup output tensors!");
    goto error;
  }

  GST_INFO ("Created MLE SNPE engine: %p", engine);
  return engine;

error:
  gst_ml_snpe_engine_free (engine);
  return NULL;
}

void
gst_ml_snpe_engine_free (GstMLSnpeEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->outinfo != NULL) {
    gst_ml_info_free (engine->outinfo);
    engine->outinfo = NULL;
  }

  if (engine->ininfo != NULL) {
    gst_ml_info_free (engine->ininfo);
    engine->ininfo = NULL;
  }

  if (engine->outputs != NULL) {
    Snpe_IUserBuffer_Handle_t usrbuffer = NULL;
    guint idx = 0, n_tensors = 0;

    n_tensors = engine->StringListSize (engine->outnames);

    for (idx = 0; idx < n_tensors; idx++) {
      const char *name = engine->StringListAt (engine->outnames, idx);

      usrbuffer = engine->UserBufferMapGet (engine->outputs, name);
      engine->UserBufferMapRemove (engine->outputs, name);

      engine->IUserBufferDelete (usrbuffer);
    }

    engine->UserBufferMapDelete (engine->outputs);
  }

  if (engine->outnames != NULL) {
    engine->StringListDelete (engine->outnames);
  }

  if (engine->inputs != NULL) {
    Snpe_StringList_Handle_t names = NULL;
    Snpe_IUserBuffer_Handle_t usrbuffer = NULL;
    guint idx = 0, n_tensors = 0;

    names = engine->SNPE_GetInputTensorNames (engine->interpreter);
    n_tensors = engine->StringListSize (names);

    for (idx = 0; idx < n_tensors; idx++) {
      const char *name = engine->StringListAt (names, idx);

      usrbuffer = engine->UserBufferMapGet (engine->inputs, name);
      engine->UserBufferMapRemove (engine->inputs, name);

      engine->IUserBufferDelete (usrbuffer);
    }

    engine->StringListDelete (names);
    engine->UserBufferMapDelete (engine->inputs);
  }

  if (engine->interpreter != NULL)
    engine->SNPE_Delete (engine->interpreter);

  if (engine->builder != NULL)
    engine->SNPEBuilderDelete (engine->builder);

  if (engine->model != NULL)
    engine->DlContainerDelete (engine->model);

  if (engine->libhandle != NULL)
    dlclose (engine->libhandle);

  GST_INFO ("Destroyed MLE SNPE engine: %p", engine);
  g_free (engine);
}

GstCaps *
gst_ml_snpe_engine_get_input_caps  (GstMLSnpeEngine * engine)
{
  if (engine == NULL)
    return NULL;

  return gst_ml_info_to_caps (engine->ininfo);
}

GstCaps *
gst_ml_snpe_engine_get_output_caps  (GstMLSnpeEngine * engine)
{
  GstCaps *caps = NULL;
  GValue list = G_VALUE_INIT, value = G_VALUE_INIT;

  if (engine == NULL)
    return NULL;

  caps = gst_ml_info_to_caps (engine->outinfo);

  // If current type is already FLOAT, return immediately.
  if (GST_ML_INFO_TYPE (engine->outinfo) == GST_ML_TYPE_FLOAT32)
    return caps;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&value, G_TYPE_STRING);

  g_value_set_string (&value, gst_ml_type_to_string (GST_ML_TYPE_FLOAT32));
  gst_value_list_append_value (&list, &value);

  g_value_set_string (&value,
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));
  gst_value_list_append_value (&list, &value);

  // Overwrite the type field by adding FLOAT in addition to current type.
  gst_caps_set_value (caps, "type", &list);

  g_value_unset (&value);
  g_value_unset (&list);

  return caps;
}

gboolean
gst_ml_snpe_engine_update_output_caps (GstMLSnpeEngine * engine, GstCaps * caps)
{
  Snpe_IBufferAttributes_Handle_t attribs = NULL;
  Snpe_TensorShape_Handle_t shape = NULL, strides = NULL;
  Snpe_UserBufferEncoding_Handle_t encoding = NULL;
  Snpe_IUserBuffer_Handle_t usrbuffer = NULL;
  const size_t *dimensions = NULL;
  GstMLInfo mlinfo = { 0, };
  guint idx = 0, n_tensors = 0, rank = 0, size = 0;
  gint num = 0;
  gboolean success = FALSE;

  g_return_val_if_fail (engine != NULL, FALSE);

  if (!gst_ml_info_from_caps (&mlinfo, caps)) {
    GST_ERROR ("Failed to extract ML info from caps!");
    return FALSE;
  }

  if (gst_ml_info_is_equal (&mlinfo, engine->outinfo))
    return TRUE;

  n_tensors = engine->StringListSize (engine->outnames);
  success = (GST_ML_INFO_N_TENSORS (&mlinfo) == n_tensors) ? TRUE : FALSE;

  if (!success) {
    GST_ERROR ("Updated info has invalid number of tensors!");
    return FALSE;
  }

  for (idx = 0; idx < n_tensors; idx++) {
    const char *name = engine->StringListAt (engine->outnames, idx);
    size_t stride[GST_ML_TENSOR_MAX_DIMS] = { 0, };

    GST_DEBUG ("Output tensor[%u] name: %s", idx, name);

    attribs =
        engine->SNPE_GetInputOutputBufferAttributes (engine->interpreter, name);
    success = (attribs != NULL) ? TRUE : FALSE;

    if (!success) {
      GST_ERROR ("Failed to get attributes for output tensor '%s'!", name);
      return FALSE;
    }

    shape = engine->IBufferAttributesGetDims (attribs);
    dimensions = engine->TensorShapeGetDimensions (shape);
    rank = engine->TensorShapeRank (shape);

    success = (rank == GST_ML_INFO_N_DIMENSIONS (&mlinfo, idx)) ? TRUE : FALSE;

    if (!success) {
      GST_ERROR ("Output tensor %d has invalid number of dimensions!", idx);
      engine->TensorShapeDelete (shape);
      engine->IBufferAttributesDelete (attribs);
      return FALSE;
    }

    for (num = 0; num < ((gint) rank); ++num) {
      // Update only dimensions with value 0, all others must be the same.
      GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num) = (dimensions[num] == 0) ?
          GST_ML_INFO_TENSOR_DIM (&mlinfo, idx, num) : dimensions[num];

      success =
          GST_ML_INFO_TENSOR_DIM (&mlinfo, idx, num) ==
              GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num);

      if (!success) {
        GST_ERROR ("Updated tensor %d has invalid dimension %d!", idx, num);
        engine->TensorShapeDelete (shape);
        engine->IBufferAttributesDelete (attribs);
        return FALSE;
      }

      GST_DEBUG ("Output tensor[%d] Dimension[%d]: %u", idx, num,
          GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num));
    }

    // Tensor shape is no longer needed, deallocate.
    engine->TensorShapeDelete (shape);

    stride[rank - 1] = gst_ml_type_get_size (GST_ML_INFO_TYPE (&mlinfo));

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (rank - 1); num > 0; num--)
      stride[num - 1] = engine->outinfo->tensors[idx][num] * stride[num];

    strides = engine->TensorShapeCreateDimsSize (stride, rank);
    size = gst_ml_info_tensor_size (&mlinfo, idx);

    GST_DEBUG ("Output tensor[%u] size: %u", idx, size);

    if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_INFO_TYPE (engine->outinfo)) {
      encoding = engine->IBufferAttributesGetEncoding (attribs);
    } else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_FLOAT32) {
      encoding = engine->UserBufferEncodingFloatCreate ();
    } else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_UINT8) {
      encoding = engine->UserBufferEncodingUnsigned8BitCreate ();
    } else {
      engine->IBufferAttributesDelete (attribs);
      engine->TensorShapeDelete (strides);
      success = FALSE;
    }

    if (!success) {
      GST_ERROR ("Unsupported encoding for tensor %d!", idx);
      return FALSE;
    }

    // Remove and deallocate previous buffer for that tensor.
    usrbuffer = engine->UserBufferMapGet (engine->outputs, name);
    engine->UserBufferMapRemove (engine->outputs, name);
    engine->IUserBufferDelete (usrbuffer);

    // Empty User Buffer which will later be set via setBufferAddress API.
    usrbuffer = engine->UtilCreateUserBuffer (NULL, size, strides, encoding);
    success = (usrbuffer != NULL) ? TRUE : FALSE;

    if (GST_ML_INFO_TYPE (&mlinfo) != GST_ML_INFO_TYPE (engine->outinfo)) {
      if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_FLOAT32)
        engine->UserBufferEncodingFloatDelete (encoding);
      else if (GST_ML_INFO_TYPE (&mlinfo) == GST_ML_TYPE_UINT8)
        engine->UserBufferEncodingUnsigned8BitDelete (encoding);
    }

    engine->TensorShapeDelete (strides);
    engine->IBufferAttributesDelete (attribs);

    if (!success) {
      GST_ERROR ("Failed to create buffer for tensor %d!", idx);
      return FALSE;
    }

    engine->UserBufferMapAdd (engine->outputs, name, usrbuffer);
  }

  // Update the tensor type.
  GST_ML_INFO_TYPE (engine->outinfo) = GST_ML_INFO_TYPE (&mlinfo);

  GST_DEBUG ("Number of output tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->outinfo));
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));

  return success;
}

gboolean
gst_ml_snpe_engine_execute (GstMLSnpeEngine * engine,
    GstMLFrame * inframe, GstMLFrame * outframe)
{
  Snpe_StringList_Handle_t names = NULL;
  Snpe_IUserBuffer_Handle_t usrbuffer = NULL;
  Snpe_ErrorCode_t error = SNPE_SUCCESS;
  guint idx = 0;

  g_return_val_if_fail (engine != NULL, FALSE);

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

  names = engine->SNPE_GetInputTensorNames (engine->interpreter);

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    const char *name = engine->StringListAt (names, idx);
    void *vaddress = GST_ML_FRAME_BLOCK_DATA (inframe, idx);

    usrbuffer = engine->UserBufferMapGet (engine->inputs, name);
    engine->IUserBufferSetBufferAddress (usrbuffer, vaddress);
  }

  engine->StringListDelete (names);

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    const char *name = engine->StringListAt (engine->outnames, idx);
    void *vaddress = GST_ML_FRAME_BLOCK_DATA (outframe, idx);

    usrbuffer = engine->UserBufferMapGet (engine->outputs, name);
    engine->IUserBufferSetBufferAddress (usrbuffer, vaddress);
  }

  error = engine->SNPE_ExecuteUserBuffers (engine->interpreter, engine->inputs,
      engine->outputs);

  if (error != SNPE_SUCCESS) {
    GST_ERROR ("Model execution failed, error: %d!", error);
    return FALSE;
  }

  return TRUE;
}
