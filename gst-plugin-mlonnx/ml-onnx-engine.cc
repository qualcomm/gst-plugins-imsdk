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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ml-onnx-engine.h"

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <onnxruntime/onnxruntime_c_api.h>
#include <onnx/onnx-ml.pb.h>

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

#define DEFAULT_OPT_THREADS  1
#define DEFAULT_OPT_EXECUTION_PROVIDER GST_ML_ONNX_EXECUTION_PROVIDER_CPU
#define DEFAULT_OPT_OPTIMIZATION_LEVEL \
    GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED

#define GET_OPT_MODEL(s) get_opt_string (s, GST_ML_ONNX_ENGINE_OPT_MODEL)
#define GET_OPT_EXECUTION_PROVIDER(s) get_opt_enum (s, \
    GST_ML_ONNX_ENGINE_OPT_EXECUTION_PROVIDER, \
    GST_TYPE_ML_ONNX_EXECUTION_PROVIDER, DEFAULT_OPT_EXECUTION_PROVIDER)
#define GET_OPT_QNN_BACKEND_PATH(s) get_opt_string (s, \
    GST_ML_ONNX_ENGINE_OPT_QNN_BACKEND_PATH)
#define GET_OPT_OPTIMIZATION_LEVEL(s) get_opt_enum (s, \
    GST_ML_ONNX_ENGINE_OPT_OPTIMIZATION_LEVEL, \
    GST_TYPE_ML_ONNX_OPTIMIZATION_LEVEL, DEFAULT_OPT_OPTIMIZATION_LEVEL)
#define GET_OPT_THREADS(s) get_opt_uint (s, GST_ML_ONNX_ENGINE_OPT_THREADS, \
    DEFAULT_OPT_THREADS)

#define GST_CAT_DEFAULT gst_ml_onnx_engine_debug_category()

static const OrtApi *api = NULL;

struct _GstMLOnnxEngine
{
  GstMLInfo *ininfo;
  GstMLInfo *outinfo;

  GstStructure *settings;

  // ONNX Runtime components
  OrtEnv *env;
  OrtSession *session;
  OrtMemoryInfo *memory_info;
  OrtAllocator *allocator;
  ONNXTensorElementDataType elem_type[GST_ML_MAX_TENSORS];

  // Model information
  size_t n_inputs;
  size_t n_outputs;
  char **input_names;
  char **output_names;

  // Scale and offset values for dequantization
  gdouble    offsets[GST_ML_MAX_TENSORS];
  gdouble    scales[GST_ML_MAX_TENSORS];
};

static GstDebugCategory *
gst_ml_onnx_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("ml-onnx-engine", 0,
        "Machine Learning ONNX Engine");
    g_once_init_leave (&catonce, catdone);
  }

  return (GstDebugCategory *) catonce;
}

GType
gst_ml_onnx_execution_provider_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_ONNX_EXECUTION_PROVIDER_CPU,
        "CPU execution provider", "cpu"
    },
    { GST_ML_ONNX_EXECUTION_PROVIDER_QNN,
        "Qualcomm QNN execution provider", "qnn"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLOnnxExecutionProvider", variants);

  return gtype;
}

GType
gst_ml_onnx_optimization_level_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL,
        "Disable all optimizations", "disable-all"
    },
    { GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
        "Enable basic optimizations", "enable-basic"
    },
    { GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
        "Enable extended optimizations", "enable-extended"
    },
    { GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
        "Enable all optimizations", "enable-all"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLOnnxOptimizationLevel", variants);

  return gtype;
}

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

static GstMLType
onnx_to_ml_type (ONNXTensorElementDataType type)
{
  switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
      return GST_ML_TYPE_INT8;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      return GST_ML_TYPE_UINT8;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:
      return GST_ML_TYPE_INT16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:
      return GST_ML_TYPE_UINT16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
      return GST_ML_TYPE_INT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
      return GST_ML_TYPE_UINT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
      return GST_ML_TYPE_INT64;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
      return GST_ML_TYPE_UINT64;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
      return GST_ML_TYPE_FLOAT16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return GST_ML_TYPE_FLOAT32;
    default:
      GST_ERROR ("Unsupported ONNX tensor type: %d", type);
      return GST_ML_TYPE_UNKNOWN;
  }
}

static const gchar *
onnx_type_to_string (ONNXTensorElementDataType type)
{
  switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      return "UINT8";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
      return "INT8";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:
      return "UINT16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:
      return "INT16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
      return "UINT32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
      return "INT32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
      return "UINT64";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
      return "INT64";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
      return "FLOAT16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return "FLOAT32";
    default:
      return "Unknown type";
  }
}

static void
gst_ml_onnx_extract_qparams (const gchar *filename, gdouble *scales,
    gdouble *offsets, char **output_names, size_t n_outputs)
{
  std::ifstream input(filename, std::ios::binary);
  if (!input) {
    GST_ERROR ("Failed to open ONNX model file: %s", filename);
    return;
  }

  onnx::ModelProto model;
  if (!model.ParseFromIstream(&input)) {
    GST_ERROR ("Failed to parse ONNX model from file: %s", filename);
    return;
  }

  const onnx::GraphProto& graph = model.graph();
  GST_INFO ("Parsing ONNX model graph with %d nodes", graph.node_size());

  std::unordered_map<std::string, int> tensor_names;

  for (size_t i = 0; i < n_outputs; i++) {
    std::string output_name_str(output_names[i]);
    tensor_names[output_name_str] = i;
  }

  // Iterate through all nodes in the graph
  for (int i = 0; i < graph.node_size(); i++) {
    const onnx::NodeProto& node = graph.node(i);

    // Look for QuantizeLinear nodes and name matching output tensor name
    if ((node.op_type() == "QuantizeLinear") &&
        (tensor_names.find(node.output(0)) != tensor_names.end())) {
      GST_DEBUG ("Found QuantizeLinear node: %s output name: %s",
          node.name().c_str(), node.output(0).c_str());

      std::string output_name = node.output(0);
      std::string scale_name = node.input(1);
      std::string zero_point_name = node.input(2);

      float scale_value = 1.0f;
      float zero_point_value = 0.0f;

      for (int j = 0; j < graph.initializer_size(); j++) {
        const onnx::TensorProto& tensor = graph.initializer(j);

        if (tensor.name() == scale_name) {
          const auto bytes =
              reinterpret_cast<const float*>(tensor.raw_data().data());
          scale_value = *bytes;
          GST_DEBUG ("Scale: %f", scale_value);
        }

        if (tensor.name() == zero_point_name) {
          switch (tensor.data_type()) {
            case onnx::TensorProto::UINT8:
            {
              const auto bytes = reinterpret_cast<const std::uint8_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::INT8:
            {
              const auto bytes = reinterpret_cast<const std::int8_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::UINT16:
            {
              const auto bytes = reinterpret_cast<const std::uint16_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::INT16:
            {
              const auto bytes = reinterpret_cast<const std::int16_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::UINT32:
            {
              const auto bytes = reinterpret_cast<const std::uint32_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::INT32:
            {
              const auto bytes = reinterpret_cast<const std::int32_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::UINT64:
            {
              const auto bytes = reinterpret_cast<const std::uint64_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            case onnx::TensorProto::INT64:
            {
              const auto bytes = reinterpret_cast<const std::int64_t*>
                  (tensor.raw_data().data());

              zero_point_value = static_cast<float>(*bytes);
              break;
            }
            default:
              break;
          }

          GST_DEBUG ("Zero-point: %f", zero_point_value);
        }
      }
      scales[tensor_names[node.output(0)]] = scale_value;
      offsets[tensor_names[node.output(0)]] = zero_point_value;
    }
  }
}

static void
gst_ml_onnx_convert_to_float (GstMLFrame *mlframe, guint idx, void *tensor_data,
    ONNXTensorElementDataType type, float scale, float offset)
{
  float *output = NULL;
  size_t n_elements = 0;

  output = reinterpret_cast<float *>(GST_ML_FRAME_BLOCK_DATA (mlframe, idx));
  n_elements = gst_ml_info_tensor_size (&(mlframe->info), idx);
  n_elements /= gst_ml_type_get_size (mlframe->info.type);

  GST_LOG ("Converting tensor from %s to FLOAT32 using scale: %f and offset: %f",
      onnx_type_to_string (type), scale, offset);

  switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
    {
      int8_t *data = reinterpret_cast<int8_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:
    {
      uint16_t *data = reinterpret_cast<uint16_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:
    {
      int16_t *data = reinterpret_cast<int16_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
    {
      uint32_t *data = reinterpret_cast<uint32_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
    {
      int32_t *data = reinterpret_cast<int32_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
    {
      uint64_t *data = reinterpret_cast<uint64_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
    {
      int64_t *data = reinterpret_cast<int64_t *>(tensor_data);

      for (size_t i = 0; i < n_elements; i++)
        output[i] = (static_cast<float>(data[i]) - offset) * scale;

      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
    {
      memcpy (output, tensor_data, sizeof (float) * n_elements);

      break;
    }
    default:
      GST_ERROR ("Data type not supported yet!");
      break;
  }
}

GstMLOnnxEngine *
gst_ml_onnx_engine_new (GstStructure * settings)
{
  GstMLOnnxEngine *engine = NULL;
  const gchar *filename = NULL;
  OrtStatus *status = NULL;
  OrtSessionOptions *session_options = NULL;
  guint n_threads = 1;
  gint execution_provider, optimization_level;

  engine = g_slice_new0 (GstMLOnnxEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  // Initialize dequantization parameters to defaults
  for (size_t i = 0; i < GST_ML_MAX_TENSORS; i++) {
    engine->scales[i] = 1.0;
    engine->offsets[i] = 0.0;
  }

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  // Initialize ONNX Runtime API
  api = OrtGetApiBase ()->GetApi (ORT_API_VERSION);
  if (!api) {
    GST_ERROR ("Failed to get ONNX Runtime API!");
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  filename = GET_OPT_MODEL (engine->settings);
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (filename != NULL, NULL,
      gst_ml_onnx_engine_free (engine), "No model file name!");

  // Create ONNX Runtime environment
  status = api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "GstMLOnnx", &engine->env);
  if (status) {
    GST_ERROR ("Failed to create ONNX environment: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  // Create session options
  status = api->CreateSessionOptions (&session_options);
  if (status) {
    GST_ERROR ("Failed to create session options: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  // Set optimization level
  optimization_level = GET_OPT_OPTIMIZATION_LEVEL (engine->settings);
  GraphOptimizationLevel onnx_optim;
  switch (optimization_level) {
    case GST_ML_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL:
      onnx_optim = ORT_DISABLE_ALL;
      break;
    case GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC:
      onnx_optim = ORT_ENABLE_BASIC;
      break;
    case GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED:
      onnx_optim = ORT_ENABLE_EXTENDED;
      break;
    case GST_ML_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL:
      onnx_optim = ORT_ENABLE_ALL;
      break;
    default:
      onnx_optim = ORT_DISABLE_ALL;
      break;
  }
  status = api->SetSessionGraphOptimizationLevel (session_options, onnx_optim);
  if (status) {
    GST_ERROR ("Failed to set optimization level: %s",
        api->GetErrorMessage (status));
    api->ReleaseSessionOptions (session_options);
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  // Set number of threads
  n_threads = GET_OPT_THREADS (engine->settings);
  status = api->SetIntraOpNumThreads (session_options, n_threads);
  if (status) {
    GST_ERROR ("Failed to set number of threads: %s",
        api->GetErrorMessage (status));
    api->ReleaseSessionOptions (session_options);
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  GST_DEBUG ("Number of threads: %u", n_threads);

  // Set execution provider
  execution_provider = GET_OPT_EXECUTION_PROVIDER (engine->settings);
  switch (execution_provider) {
    case GST_ML_ONNX_EXECUTION_PROVIDER_QNN:
    {
      const gchar *backend_path = GET_OPT_QNN_BACKEND_PATH (engine->settings);

      if (backend_path == NULL || strlen(backend_path) == 0) {
        GST_ERROR ("QNN execution provider requires a valid backend path. "
            "Please set the 'backend-path' property.");
        api->ReleaseSessionOptions (session_options);
        gst_ml_onnx_engine_free (engine);
        return NULL;
      }

      // QNN execution provider configuration
      // Using SessionOptionsAppendExecutionProvider with QNN provider name
      const char* qnn_provider_options_keys[] = {"backend_path"};
      const char* qnn_provider_options_values[] = {backend_path};

      status = api->SessionOptionsAppendExecutionProvider (session_options,
          "QNN", qnn_provider_options_keys, qnn_provider_options_values, 1);
      if (status) {
        GST_WARNING ("Failed to set QNN execution provider: %s",
            api->GetErrorMessage (status));
        api->ReleaseStatus (status);
      } else {
        GST_INFO ("Using QNN execution provider");
      }
      break;
    }
    default:
      GST_INFO ("Using CPU execution provider");
      break;
  }

  // Create session
  status = api->CreateSession (engine->env, filename, session_options,
      &engine->session);
  if (status) {
    GST_ERROR ("Failed to create session: %s", api->GetErrorMessage (status));
    api->ReleaseSessionOptions (session_options);
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  api->ReleaseSessionOptions (session_options);

  GST_DEBUG ("Loaded model file '%s'!", filename);

  // Get allocator
  status = api->GetAllocatorWithDefaultOptions (&engine->allocator);
  if (status) {
    GST_ERROR ("Failed to get allocator: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  // Get input/output counts
  status = api->SessionGetInputCount (engine->session, &engine->n_inputs);
  if (status) {
    GST_ERROR ("Failed to get input count: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  status = api->SessionGetOutputCount (engine->session, &engine->n_outputs);
  if (status) {
    GST_ERROR ("Failed to get output count: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  engine->ininfo->n_tensors = engine->n_inputs;
  engine->outinfo->n_tensors = engine->n_outputs;

  GST_DEBUG ("Number of input tensors: %zu", engine->n_inputs);
  GST_DEBUG ("Number of output tensors: %zu", engine->n_outputs);

  // Get input names and info
  engine->input_names = g_new0 (char *, engine->n_inputs);
  if (!engine->input_names) {
    GST_ERROR ("Failed to allocate memory for input names");
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  for (size_t i = 0; i < engine->n_inputs; i++) {
    OrtTypeInfo *type_info = NULL;
    const OrtTensorTypeAndShapeInfo *tensor_info = NULL;

    status = api->SessionGetInputName (engine->session, i, engine->allocator,
        &engine->input_names[i]);
    if (status) {
      GST_ERROR ("Failed to get input name %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    status = api->SessionGetInputTypeInfo (engine->session, i, &type_info);
    if (status) {
      GST_ERROR ("Failed to get input type info %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    status = api->CastTypeInfoToTensorInfo (type_info, &tensor_info);
    if (status) {
      GST_ERROR ("Failed to cast type info %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    status = api->GetTensorElementType (tensor_info, &(engine->elem_type[i]));
    if (status) {
      GST_ERROR ("Failed to get element type %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    if (i == 0) {
      engine->ininfo->type = onnx_to_ml_type (engine->elem_type[i]);
      if (engine->ininfo->type == GST_ML_TYPE_UNKNOWN) {
        GST_ERROR ("Input ML type unknown!");
        api->ReleaseTypeInfo (type_info);
        gst_ml_onnx_engine_free (engine);
        return NULL;
      }
    }

    size_t num_dims;
    status = api->GetDimensionsCount (tensor_info, &num_dims);
    if (status) {
      GST_ERROR ("Failed to get dimensions count %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    std::vector<int64_t> dims(num_dims);
    status = api->GetDimensions (tensor_info, dims.data(), num_dims);
    if (status) {
      GST_ERROR ("Failed to get dimensions %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    engine->ininfo->n_dimensions[i] = num_dims;
    for (size_t j = 0; j < num_dims; j++) {
      engine->ininfo->tensors[i][j] = dims[j] > 0 ? dims[j] : 0;
      GST_DEBUG ("Input tensor[%zu] Dimension[%zu]: %u", i, j,
          engine->ininfo->tensors[i][j]);
    }

    api->ReleaseTypeInfo (type_info);
  }

  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (engine->ininfo->type));

  // Get output names and info
  engine->output_names = g_new0 (char *, engine->n_outputs);
  for (size_t i = 0; i < engine->n_outputs; i++) {
    OrtTypeInfo *type_info = NULL;
    const OrtTensorTypeAndShapeInfo *tensor_info = NULL;

    status = api->SessionGetOutputName (engine->session, i, engine->allocator,
        &engine->output_names[i]);
    if (status) {
      GST_ERROR ("Failed to get output name %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    status = api->SessionGetOutputTypeInfo (engine->session, i, &type_info);
    if (status) {
      GST_ERROR ("Failed to get output type info %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    status = api->CastTypeInfoToTensorInfo (type_info, &tensor_info);
    if (status) {
      GST_ERROR ("Failed to cast type info %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    ONNXTensorElementDataType elem_type;
    status = api->GetTensorElementType (tensor_info, &elem_type);
    if (status) {
      GST_ERROR ("Failed to get element type %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    if (i == 0) {
      engine->outinfo->type = onnx_to_ml_type (elem_type);
      if (engine->outinfo->type == GST_ML_TYPE_UNKNOWN) {
        GST_ERROR ("Output ML type unknown!");
        api->ReleaseTypeInfo (type_info);
        gst_ml_onnx_engine_free (engine);
        return NULL;
      }
    }

    size_t num_dims;
    status = api->GetDimensionsCount (tensor_info, &num_dims);
    if (status) {
      GST_ERROR ("Failed to get dimensions count %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    std::vector<int64_t> dims(num_dims);
    status = api->GetDimensions (tensor_info, dims.data(), num_dims);
    if (status) {
      GST_ERROR ("Failed to get dimensions %zu: %s",
          i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (type_info);
      api->ReleaseStatus (status);
      gst_ml_onnx_engine_free (engine);
      return NULL;
    }

    engine->outinfo->n_dimensions[i] = num_dims;
    for (size_t j = 0; j < num_dims; j++) {
      engine->outinfo->tensors[i][j] = dims[j] > 0 ? dims[j] : 0;
      GST_DEBUG ("Output tensor[%zu] Dimension[%zu]: %u", i, j,
          engine->outinfo->tensors[i][j]);
    }

    api->ReleaseTypeInfo (type_info);
  }

  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (engine->outinfo->type));

  // Extract quantization parameters from the model graph
  if (engine->outinfo->type != GST_ML_TYPE_FLOAT32) {
    gst_ml_onnx_extract_qparams (filename, engine->scales, 
        engine->offsets, engine->output_names, engine->n_outputs);
  }

  // Create memory info
  status = api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault,
      &engine->memory_info);
  if (status) {
    GST_ERROR ("Failed to create mem info: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    gst_ml_onnx_engine_free (engine);
    return NULL;
  }

  GST_INFO ("Created ML ONNX engine: %p", engine);
  return engine;
}

void
gst_ml_onnx_engine_free (GstMLOnnxEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->input_names) {
    for (size_t i = 0; i < engine->n_inputs; i++) {
      if (engine->input_names[i] && engine->allocator)
        engine->allocator->Free (engine->allocator, engine->input_names[i]);
    }
    g_free (engine->input_names);
  }

  if (engine->output_names) {
    for (size_t i = 0; i < engine->n_outputs; i++) {
      if (engine->output_names[i] && engine->allocator)
        engine->allocator->Free (engine->allocator, engine->output_names[i]);
    }
    g_free (engine->output_names);
  }

  if (engine->memory_info)
    api->ReleaseMemoryInfo (engine->memory_info);

  if (engine->session)
    api->ReleaseSession (engine->session);

  if (engine->env)
    api->ReleaseEnv (engine->env);

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

  GST_INFO ("Destroyed ML ONNX engine: %p", engine);
  g_slice_free (GstMLOnnxEngine, engine);
}

GstCaps *
gst_ml_onnx_engine_get_input_caps (GstMLOnnxEngine * engine)
{
  if (engine == NULL)
    return NULL;

  return gst_ml_info_to_caps (engine->ininfo);
}

GstCaps *
gst_ml_onnx_engine_get_output_caps  (GstMLOnnxEngine * engine)
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
gst_ml_onnx_engine_execute (GstMLOnnxEngine * engine,
    GstMLFrame * inframe, GstMLFrame * outframe)
{
  GstMLTensorMeta *mlmeta = NULL;
  OrtStatus *status = NULL;
  std::vector<OrtValue*> input_tensors;
  std::vector<OrtValue*> output_tensors;
  gboolean success = FALSE;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (inframe != NULL, FALSE);
  g_return_val_if_fail (outframe != NULL, FALSE);

  if (GST_ML_FRAME_N_BLOCKS (inframe) != engine->n_inputs) {
    GST_WARNING ("Input buffer has %u memory blocks but engine requires %zu!",
        GST_ML_FRAME_N_BLOCKS (inframe), engine->n_inputs);
    return FALSE;
  }

  if (GST_ML_FRAME_N_BLOCKS (outframe) != engine->n_outputs) {
    GST_WARNING ("Output buffer has %u memory blocks but engine requires %zu!",
        GST_ML_FRAME_N_BLOCKS (outframe), engine->n_outputs);
    return FALSE;
  }

  // Create input tensors
  input_tensors.resize(engine->n_inputs);
  for (size_t i = 0; i < engine->n_inputs; i++) {
    std::vector<int64_t> shape;
    for (guint j = 0; j < engine->ininfo->n_dimensions[i]; j++) {
      shape.push_back(engine->ininfo->tensors[i][j]);
    }

    void *data = GST_ML_FRAME_BLOCK_DATA (inframe, i);
    size_t data_size = GST_ML_FRAME_BLOCK_SIZE (inframe, i);

    status = api->CreateTensorWithDataAsOrtValue (engine->memory_info, data,
        data_size, shape.data(), shape.size(), engine->elem_type[i],
        &input_tensors[i]);

    if (status) {
      GST_ERROR ("Failed to create input tensor %zu: %s", i,
          api->GetErrorMessage (status));
      api->ReleaseStatus (status);
      goto cleanup;
    }

    mlmeta = gst_buffer_get_ml_tensor_meta_id (inframe->buffer, i);
    mlmeta->name = g_quark_from_string (engine->input_names[i]);
  }

  // Run inference
  output_tensors.resize (engine->n_outputs);
  status = api->Run (engine->session, NULL,
      (const char * const *)engine->input_names,
      (const OrtValue * const *)input_tensors.data(), engine->n_inputs,
      (const char * const *)engine->output_names, engine->n_outputs,
      output_tensors.data());

  if (status) {
    GST_ERROR ("Failed to run inference: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    goto cleanup;
  }

  // Process output tensors
  for (size_t i = 0; i < engine->n_outputs; i++) {
    OrtTensorTypeAndShapeInfo *tensor_info = NULL;
    ONNXTensorElementDataType elem_type;
    void *tensor_data = NULL;

    status = api->GetTensorTypeAndShape (output_tensors[i], &tensor_info);
    if (status) {
      GST_ERROR ("Failed to get tensor info %zu", i);
      api->ReleaseStatus (status);
      goto cleanup;
    }

    status = api->GetTensorElementType (tensor_info, &elem_type);
    if (status) {
      api->ReleaseTensorTypeAndShapeInfo (tensor_info);
      GST_ERROR ("Failed to get element type %zu", i);
      api->ReleaseStatus (status);
      goto cleanup;
    }

    status = api->GetTensorMutableData (output_tensors[i], &tensor_data);
    if (status) {
      api->ReleaseTensorTypeAndShapeInfo (tensor_info);
      GST_ERROR ("Failed to get tensor data %zu", i);
      api->ReleaseStatus (status);
      goto cleanup;
    }

    gst_ml_onnx_convert_to_float (outframe, i, tensor_data, elem_type,
        engine->scales[i], engine->offsets[i]);

    mlmeta = gst_buffer_get_ml_tensor_meta_id (outframe->buffer, i);
    mlmeta->name = g_quark_from_string (engine->output_names[i]);

    api->ReleaseTensorTypeAndShapeInfo (tensor_info);
  }

  success = TRUE;

cleanup:
  // Release tensors
  for (auto tensor : input_tensors) {
    if (tensor)
      api->ReleaseValue (tensor);
  }
  for (auto tensor : output_tensors) {
    if (tensor)
      api->ReleaseValue (tensor);
  }

  return success;
}
