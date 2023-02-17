/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <map>
#include <numeric>
#include <vector>

#include <dlfcn.h>

#include <QnnInterface.h>
#include <System/QnnSystemInterface.h>

#include "ml-qnn-engine.h"

#define GST_CAT_DEFAULT gst_ml_qnn_engine_debug_category()
#define GST_CAT_QNN_SDK gst_ml_qnn_sdk_debug_category()

#define QNN_TENSOR_DATA_TYPE(tensor)      \
    ((tensor.version == QNN_TENSOR_VERSION_1) ?      \
        tensor.v1.dataType : QNN_DATATYPE_UNDEFINED)

#define QNN_TENSOR_DIMENSION(tensor, idx) \
    ((tensor.version == QNN_TENSOR_VERSION_1) ? tensor.v1.dimensions[idx] : 0)

#define QNN_TENSOR_RANK(tensor) \
    ((tensor.version == QNN_TENSOR_VERSION_1) ? tensor.v1.rank : 0u)

// TODO: Workaround! Need to be exported by the QNN SDK.
typedef struct {
  Qnn_GraphHandle_t graph;
  char              *graphName;
  Qnn_Tensor_t      *inputTensors;
  uint32_t          numInputTensors;
  Qnn_Tensor_t      *outputTensors;
  uint32_t          numOutputTensors;
} GraphInfo_t;

// TODO: Workaround! Need to be exported by the QNN SDK.
typedef struct {
  char                    *graphName;
  const QnnGraph_Config_t **graphConfigs;
} GraphConfigInfo_t;

typedef Qnn_ErrorHandle_t (*QnnInterfaceGetProvidersFn)(
    const QnnInterface_t ***providerList, uint32_t *numProviders);

typedef Qnn_ErrorHandle_t (*ComposeGraphsFn)(Qnn_BackendHandle_t,
    QNN_INTERFACE_VER_TYPE, Qnn_ContextHandle_t, const GraphConfigInfo_t **,
    const uint32_t, GraphInfo_t ***, uint32_t *, bool, QnnLog_Callback_t,
    QnnLog_Level_t);

typedef Qnn_ErrorHandle_t (*FreeGraphFn) (GraphInfo_t ***,
    uint32_t);

struct _GstMLQnnEngine
{
  GstMLInfo               *ininfo;
  GstMLInfo               *outinfo;

  GstStructure            *settings;

  // QNN backend library handle.
  gpointer                libhandle;
  // QNN model library handle.
  gpointer                model;

  // QNN versioned interface.
  QNN_INTERFACE_VER_TYPE  interface;
  // QNN log handle.
  Qnn_LogHandle_t         logger;
  // QNN profiling handle.
  Qnn_ProfileHandle_t     profiler;
  // QNN device handle.
  Qnn_DeviceHandle_t      device;
  // QNN graph context handle.
  Qnn_ContextHandle_t     context;
  Qnn_BackendHandle_t     backend;

  // QNN model graphs.
  GraphInfo_t             **graph_infos;
  uint32_t                n_graph_infos;

  // QNNF library APIs
  FreeGraphFn           FreeGraph;
};

static const std::map<Qnn_DataType_t, size_t> kDataTypeToSize = {
    {QNN_DATATYPE_INT_8, 1},
    {QNN_DATATYPE_INT_16, 2},
    {QNN_DATATYPE_INT_32, 4},
    {QNN_DATATYPE_INT_64, 8},
    {QNN_DATATYPE_UINT_8, 1},
    {QNN_DATATYPE_UINT_16, 2},
    {QNN_DATATYPE_UINT_32, 4},
    {QNN_DATATYPE_UINT_64, 8},
    {QNN_DATATYPE_FLOAT_16, 2},
    {QNN_DATATYPE_FLOAT_32, 4},
    {QNN_DATATYPE_SFIXED_POINT_8, 1},
    {QNN_DATATYPE_SFIXED_POINT_16, 2},
    {QNN_DATATYPE_SFIXED_POINT_32, 4},
    {QNN_DATATYPE_UFIXED_POINT_8, 1},
    {QNN_DATATYPE_UFIXED_POINT_16, 2},
    {QNN_DATATYPE_UFIXED_POINT_32, 4},
    {QNN_DATATYPE_BOOL_8, 1},
};

static const size_t kBitsPerByte = 8;

static GstDebugCategory *
gst_ml_qnn_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("ml-qnn-engine", 0,
        "Machine Learning QNN Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static GstDebugCategory *
gst_ml_qnn_sdk_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("ml-qnn-sdk", 0,
        "Machine Learning QNN SDK");
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

static GstMLType
qnn_to_ml_type (Qnn_DataType_t type)
{
  GST_DEBUG ("Qnn tensor type: 0x%04x", static_cast<uint32_t> (type));

  switch (type) {
    case QNN_DATATYPE_UINT_8:
    case QNN_DATATYPE_UFIXED_POINT_8:
      return GST_ML_TYPE_UINT8;
    case QNN_DATATYPE_INT_8:
    case QNN_DATATYPE_SFIXED_POINT_8:
      return GST_ML_TYPE_INT8;
    case QNN_DATATYPE_UINT_32:
    case QNN_DATATYPE_UFIXED_POINT_32:
      return GST_ML_TYPE_UINT32;
    case QNN_DATATYPE_INT_32:
    case QNN_DATATYPE_SFIXED_POINT_32:
      return GST_ML_TYPE_INT32;
    case QNN_DATATYPE_FLOAT_16:
      return GST_ML_TYPE_FLOAT16;
    case QNN_DATATYPE_FLOAT_32:
      return GST_ML_TYPE_FLOAT32;
    default:
      GST_ERROR ("Unsupported format %x!", static_cast<uint32_t>(type));
      break;
  }

  return GST_ML_TYPE_UNKNOWN;
}

static void
gst_ml_qnn_convert_to_float (GstMLFrame *mlframe, guint idx,
    Qnn_Tensor_t *tensor)
{
  float *output = NULL;
  size_t n_elements = 0;

  output = reinterpret_cast<float *>(GST_ML_FRAME_BLOCK_DATA (mlframe, idx));
  n_elements = gst_ml_info_tensor_size (&(mlframe->info), idx);
  n_elements /= gst_ml_type_get_size (mlframe->info.type);

  switch (tensor->v1.dataType) {
    case QNN_DATATYPE_UFIXED_POINT_8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(tensor->v1.clientBuf.data);
      int32_t offset = tensor->v1.quantizeParams.scaleOffsetEncoding.offset;
      float scale = tensor->v1.quantizeParams.scaleOffsetEncoding.scale;

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = (float)(data[idx] + offset) * scale;

      break;
    }
    case QNN_DATATYPE_UFIXED_POINT_16:
    {
      uint16_t *data = reinterpret_cast<uint16_t *>(tensor->v1.clientBuf.data);
      int32_t offset = tensor->v1.quantizeParams.scaleOffsetEncoding.offset;
      float scale = tensor->v1.quantizeParams.scaleOffsetEncoding.scale;

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = (float)(data[idx] + offset) * scale;

      break;
    }
    case QNN_DATATYPE_UINT_8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_UINT_16:
    {
      uint16_t *data = reinterpret_cast<uint16_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_UINT_32:
    {
      uint32_t *data = reinterpret_cast<uint32_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_INT_8:
    {
      int8_t *data = reinterpret_cast<int8_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_INT_16:
    {
      int16_t *data = reinterpret_cast<int16_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_INT_32:
    {
      int32_t *data = reinterpret_cast<int32_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_BOOL_8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(tensor->v1.clientBuf.data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    default:
      GST_ERROR ("Datatype not supported yet!");
      break;
  }

  return;
}

static void
gst_ml_qnn_log_callback (const char* format, QnnLog_Level_t loglvl,
    uint64_t timestamp, va_list varargs)
{
  GstDebugLevel level = GST_LEVEL_NONE;

  switch (loglvl) {
    case QNN_LOG_LEVEL_ERROR:
      level = GST_LEVEL_ERROR;
      break;
    case QNN_LOG_LEVEL_WARN:
      level = GST_LEVEL_WARNING;
      break;
    case QNN_LOG_LEVEL_INFO:
      level = GST_LEVEL_INFO;
      break;
    case QNN_LOG_LEVEL_DEBUG:
      level = GST_LEVEL_DEBUG;
      break;
    case QNN_LOG_LEVEL_VERBOSE:
      level = GST_LEVEL_LOG;
      break;
    default:
      break;
  }

  GST_CAT_LEVEL_LOG (GST_CAT_QNN_SDK, level, NULL, format, varargs);
}

static gboolean
gst_ml_qnn_engine_setup_backend (GstMLQnnEngine *engine)
{
  gboolean success = TRUE, found = FALSE;
  const gchar *filename = NULL;

  filename = GET_OPT_BACKEND (engine->settings);

  engine->libhandle = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
  if (engine->libhandle == NULL) {
    GST_ERROR ("Failed to open %s backend, error: %s!", filename, dlerror());
    return FALSE;
  }

  GST_DEBUG ("Loaded backend '%s'!", filename);

  // Load interface symbol of the backend library.
  QnnInterfaceGetProvidersFn GetProviders;
  success &= load_symbol ((gpointer*)&GetProviders, engine->libhandle,
      "QnnInterface_getProviders");

  // Check whether symbol loading was successful.
  if (!success)
    return FALSE;

  const QnnInterface_t** providers = nullptr;
  uint32_t n_providers = 0;

  // Query for all available interfaces.
  if (GetProviders (&providers, &n_providers) != QNN_SUCCESS) {
    GST_ERROR ("Failed to get interface providers!");
    return FALSE;
  }

  // Check for validity of returned interfaces,
  if ((nullptr == providers) || (0 == n_providers)) {
    GST_ERROR ("Received Null interface providers!");
    return FALSE;
  }

  // Find a interface provider that suits the current API version.
  for (uint32_t idx = 0; (idx < n_providers) && !found; idx++) {
    auto& major = providers[idx]->apiVersion.coreApiVersion.major;
    auto& minor = providers[idx]->apiVersion.coreApiVersion.minor;

    if ((QNN_API_VERSION_MAJOR != major) || (QNN_API_VERSION_MINOR != minor))
      continue;

    engine->interface = providers[idx]->QNN_INTERFACE_VER_NAME;
    found = TRUE;
  }

  if (!found) {
    GST_ERROR ("Unable to find a suitable interface provider!");
    return FALSE;
  }

  // Register callback for various log messages.
  auto status = engine->interface.logCreate(gst_ml_qnn_log_callback,
      QNN_LOG_LEVEL_VERBOSE, &(engine->logger));

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Unable to initialize logging in the backend!");
    return FALSE;
  }

  // Set up any necessary backend configurations.
  const QnnBackend_Config_t **bknd_configs = nullptr;
  status = engine->interface.backendCreate(engine->logger, bknd_configs,
      &(engine->backend));

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Could not initialize backend!");
    return FALSE;
  }

  // Enable basic profiling.
  status = engine->interface.profileCreate(engine->backend,
      QNN_PROFILE_LEVEL_BASIC, &(engine->profiler));

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Unable to create profile handle in the backend!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ml_qnn_engine_setup_graphs (GstMLQnnEngine *engine)
{
  gboolean success = TRUE;
  const gchar *filename = NULL;

  if ((filename = GET_OPT_MODEL (engine->settings)) == NULL) {
    GST_ERROR ("No model file name!");
    return FALSE;
  }

  engine->model = dlopen (filename, RTLD_NOW | RTLD_LOCAL);
  if (engine->model == NULL) {
    GST_ERROR ("Failed to open %s model, error: %s!", filename, dlerror());
    return FALSE;
  }
  // Load symbols for setup of model graph.
  ComposeGraphsFn ComposeGraphs;
  success &= load_symbol ((gpointer*)&ComposeGraphs, engine->model,
      "QnnModel_composeGraphs");

  success &= load_symbol ((gpointer*)&engine->FreeGraph, engine->model,
      "QnnModel_freeGraphsInfo");

  // Check whether symbol loading was successful.
  if (!success) {
    GST_ERROR ("Could not load symbols to compose graph!");
    return FALSE;
  }

  const QnnDevice_Config_t **dev_configs = nullptr;
  auto status = engine->interface.deviceCreate(engine->logger, dev_configs,
      &(engine->device));

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Could not create device!");
    return FALSE;
  }
  GST_DEBUG ("Device created");

  // Set up any context configs that are necessary.
  const QnnContext_Config_t **ctx_configs = nullptr;
  status = engine->interface.contextCreate(engine->backend,
      engine->device, ctx_configs, &(engine->context));

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Could not create context!");
    return FALSE;
  }
  GST_DEBUG ("Context created");

  const GraphConfigInfo_t **configs = nullptr;
  uint32_t n_configs = 0;

  status = ComposeGraphs (engine->backend, engine->interface,
      engine->context, configs, n_configs, &(engine->graph_infos),
      &(engine->n_graph_infos), false, gst_ml_qnn_log_callback,
      QNN_LOG_LEVEL_INFO);

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Graph composition failed!");
    return FALSE;
  }
  GST_DEBUG ("Graph composition success");

  for (uint32_t idx = 0; idx < engine->n_graph_infos; idx++) {
    status = engine->interface.graphFinalize(
        (*(engine->graph_infos))[idx].graph, engine->profiler, nullptr);

    if (QNN_SUCCESS != status) {
      GST_ERROR ("Finalize for graph %u failed!", idx);
      engine->FreeGraph (&(engine->graph_infos), engine->n_graph_infos);
      return FALSE;
    }
  }
  GST_DEBUG ("Graph finalize success");

  return TRUE;
}

GstMLQnnEngine *
gst_ml_qnn_engine_new (GstStructure *settings)
{
  GstMLQnnEngine *engine = NULL;
  const GraphInfo_t *graph_info = NULL;

  GST_DEBUG ("Creating engine");

  engine = g_new0 (GstMLQnnEngine, 1);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  // Initialize backend.
  if (!gst_ml_qnn_engine_setup_backend (engine)) {
    GST_ERROR ("Failed to setup backend!");
    goto cleanup;
  }

  // Initialize model graphs.
  if (!gst_ml_qnn_engine_setup_graphs (engine)) {
    GST_ERROR ("Failed to setup graph!");
    goto cleanup;
  }

  graph_info = engine->graph_infos[0];

  // Translate information about input tensors to GstMLInfo.
  engine->ininfo->n_tensors = graph_info->numInputTensors;
  engine->ininfo->type = qnn_to_ml_type (
      QNN_TENSOR_DATA_TYPE (graph_info->inputTensors[0]));

  GST_DEBUG ("Number of input tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->ininfo));
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->ininfo)));

  for (auto idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    engine->ininfo->n_dimensions[idx] =
        QNN_TENSOR_RANK (graph_info->inputTensors[idx]);

    for (auto num = 0; num < engine->ininfo->n_dimensions[idx]; ++num) {
      engine->ininfo->tensors[idx][num] =
          QNN_TENSOR_DIMENSION (graph_info->inputTensors[idx], num);

      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          engine->ininfo->tensors[idx][num]);
    }
  }

  // Translate information about output tensors to GstMLInfo.
  engine->outinfo->n_tensors = graph_info->numOutputTensors;

  // TODO: Workaround! Need to handle the tensors of different type. For now,
  // negotiate with float32 and convert from tensor type to float.
  engine->outinfo->type = GST_ML_TYPE_FLOAT32;

  GST_DEBUG ("Number of output tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->outinfo));
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));

  for (auto idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    Qnn_Tensor_t *tensor = &(graph_info->outputTensors[idx]);

    if (tensor->version != QNN_TENSOR_VERSION_1)
      continue;

    engine->outinfo->n_dimensions[idx] = tensor->v1.rank;

    for (auto num = 0; num < engine->outinfo->n_dimensions[idx]; ++num) {
      engine->outinfo->tensors[idx][num] = tensor->v1.dimensions[num];

      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          engine->outinfo->tensors[idx][num]);
    }

    // TODO: Workaround! Need to handle tensors of different data type to avoid
    // buffer allocation and buffer copy
    gsize size = gst_ml_info_tensor_size (engine->outinfo, idx);

    // Use the tensor type from the graph instead of that from MLInfo
    size /= gst_ml_type_get_size (engine->outinfo->type);
    size *= kDataTypeToSize.find(tensor->v1.dataType)->second;

    tensor->v1.clientBuf.data = g_malloc (size);
    tensor->v1.clientBuf.dataSize = size;
  }

  GST_INFO ("Created MLE QNN engine: %p", engine);
  return engine;

cleanup:
  gst_ml_qnn_engine_free (engine);
  return NULL;
}

void
gst_ml_qnn_engine_free (GstMLQnnEngine * engine)
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

  if (engine->settings != NULL) {
    gst_structure_free (engine->settings);
    engine->settings = NULL;
  }

  if (engine->graph_infos) {
    const GraphInfo_t *graph_info = engine->graph_infos[0];
    for (auto idx = 0; idx < graph_info->numOutputTensors; ++idx) {
      Qnn_Tensor_t *tensor = &(graph_info->outputTensors[idx]);
      g_free (tensor->v1.clientBuf.data);
    }
    engine->FreeGraph (&(engine->graph_infos), engine->n_graph_infos);
    engine->graph_infos = NULL;
    engine->n_graph_infos = 0;
  }

  if (engine->interface.contextFree && engine->context)
    engine->interface.contextFree (engine->context, nullptr);

  if (engine->interface.deviceFree && engine->device)
    engine->interface.deviceFree (engine->device);

  if (engine->interface.profileFree && engine->profiler)
    engine->interface.profileFree (engine->profiler);

  if (engine->interface.backendFree && engine->backend)
    engine->interface.backendFree (engine->backend);

  if (engine->interface.logFree && engine->logger)
    engine->interface.logFree (engine->logger);

  if (engine->model != NULL)
    dlclose (engine->model);

  if (engine->libhandle != NULL)
    dlclose (engine->libhandle);

  GST_INFO ("Destroyed MLE QNN engine: %p", engine);
  g_free (engine);
}

const GstMLInfo *
gst_ml_qnn_engine_get_input_info  (GstMLQnnEngine * engine)
{
  return (engine == NULL) ? NULL : engine->ininfo;
}

const GstMLInfo *
gst_ml_qnn_engine_get_output_info  (GstMLQnnEngine * engine)
{
  return (engine == NULL) ? NULL : engine->outinfo;
}

gboolean
gst_ml_qnn_engine_execute (GstMLQnnEngine *engine, GstMLFrame *inframe,
    GstMLFrame *outframe)
{
  const GraphInfo_t *graph_info = engine->graph_infos[0];

  if (GST_ML_FRAME_N_BLOCKS (inframe) != engine->ininfo->n_tensors) {
    GST_WARNING ("Input buffer has %u memory blocks but engine requires %u!",
        GST_ML_FRAME_N_BLOCKS(inframe), engine->ininfo->n_tensors);
    return FALSE;
  }

  if (GST_ML_FRAME_N_BLOCKS (outframe) != engine->outinfo->n_tensors) {
    GST_WARNING ("Output buffer has %u memory blocks but engine requires %u!",
        GST_ML_FRAME_N_BLOCKS (outframe), engine->outinfo->n_tensors);
    return FALSE;
  }

  // populate input tensor data
  for (size_t idx = 0; idx < graph_info->numInputTensors; idx++) {
    Qnn_Tensor_t *tensor = &(graph_info->inputTensors[idx]);

    if (tensor->version != QNN_TENSOR_VERSION_1)
      continue;

    tensor->v1.clientBuf.data = GST_ML_FRAME_BLOCK_DATA (inframe, idx);
    tensor->v1.clientBuf.dataSize = GST_ML_FRAME_BLOCK_SIZE (inframe, idx);
  }

  // Execute Graph
  auto status = engine->interface.graphExecute(graph_info->graph,
      graph_info->inputTensors, graph_info->numInputTensors,
      graph_info->outputTensors, graph_info->numOutputTensors, engine->profiler,
      nullptr);

  if (QNN_GRAPH_NO_ERROR != status) {
    GST_ERROR ("Graph execution failed!");
    return FALSE;
  }

  for (size_t idx = 0; idx < graph_info->numOutputTensors; idx++) {
    Qnn_Tensor_t tensor = graph_info->outputTensors[idx];

    if (tensor.version != QNN_TENSOR_VERSION_1)
      continue;

    // TODO: Workaround! Need to handle tensors of different data type to avoid
    // buffer allocation and buffer copy
    if (tensor.v1.dataType == QNN_DATATYPE_FLOAT_32) {
      memcpy (reinterpret_cast<float *>(GST_ML_FRAME_BLOCK_DATA(outframe, idx)),
          reinterpret_cast<float *> (tensor.v1.clientBuf.data),
          GST_ML_FRAME_BLOCK_SIZE (outframe, idx));
    } else {
      GST_DEBUG ("Converting Native tensor type to Float");
      gst_ml_qnn_convert_to_float (outframe, idx, &tensor);
    }
  }

  return TRUE;
}
