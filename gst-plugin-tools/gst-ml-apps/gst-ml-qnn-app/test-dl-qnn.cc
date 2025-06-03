/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <map>
#include <numeric>
#include <vector>
#include <filesystem>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#include <dlfcn.h>

#include <QnnInterface.h>
#include <System/QnnSystemInterface.h>
#include <System/QnnSystemContext.h>

#if defined(QNN_TENSOR_V2_INIT)

#define QNN_GET_TENSOR(tensor) ((tensor)->v2)
#define QNN_TENSOR_VERSION_SUPPORTED(tensor) \
    (((tensor)->version == QNN_TENSOR_VERSION_1) || \
        ((tensor)->version == QNN_TENSOR_VERSION_2))

#elif defined(QNN_TENSOR_V1_INIT)

#define QNN_GET_TENSOR(tensor) ((tensor)->v1)
#define QNN_TENSOR_VERSION_SUPPORTED(tensor) \
    ((tensor)->version == QNN_TENSOR_VERSION_1)

#else

#error "Not supprted QNN tensor version !!!"

#endif

#if defined(QNN_SYSTEM_CONTEXT_GRAPH_INFO_V3_INIT)

#define QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO(graphInfo) \
    ((graphInfo)->graphInfoV3)
#define QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_SUPPORTED(graphInfo) \
    (((graphInfo)->version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1) || \
        ((graphInfo)->version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_2)|| \
        ((graphInfo)->version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_3))

#elif defined(QNN_SYSTEM_CONTEXT_GRAPH_INFO_V2_INIT)

#define QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO(graphInfo) \
    ((graphInfo)->graphInfoV2)
#define QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_SUPPORTED(graphInfo) \
    (((graphInfo)->version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1) || \
        ((graphInfo)->version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_2))

#elif defined(QNN_SYSTEM_CONTEXT_GRAPH_INFO_V1_INIT)

#define QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO(graphInfo) \
    ((graphInfo)->graphInfoV1)
#define QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_SUPPORTED(graphInfo) \
    ((graphInfo)->version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1)

#else

#error "Not supprted QNN system context graph info version !!!"

#endif

#if defined(QNN_SYSTEM_CONTEXT_BINARY_INFO_V3_INIT)

#define QNN_GET_SYSTEM_CONTEXT_BINARY_INFO(binary_info) \
    ((binary_info)->contextBinaryInfoV3)
#define QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_SUPPORTED(binary_info) \
    (((binary_info)->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1) || \
        ((binary_info)->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2) || \
        ((binary_info)->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_3))

#elif defined(QNN_SYSTEM_CONTEXT_BINARY_INFO_V2_INIT)

#define QNN_GET_SYSTEM_CONTEXT_BINARY_INFO(binary_info) \
    ((binary_info)->contextBinaryInfoV2)
#define QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_SUPPORTED(binary_info) \
    (((binary_info)->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1) || \
        ((binary_info)->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2))

#elif defined(QNN_SYSTEM_CONTEXT_BINARY_INFO_V1_INIT)

#define QNN_GET_SYSTEM_CONTEXT_BINARY_INFO(binary_info) \
    ((binary_info)->contextBinaryInfoV1)
#define QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_SUPPORTED(binary_info) \
    ((binary_info)->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1)

#else

#error "Not supprted QNN system context binary info version !!!"

#endif

#define QNN_TENSOR_DATA_TYPE(tensor) \
    (QNN_GET_TENSOR (tensor).dataType)

#define QNN_TENSOR_DIMENSION(tensor, idx) \
    (QNN_GET_TENSOR (tensor).dimensions[(idx)])

#define QNN_TENSOR_NAME(tensor) \
    (QNN_GET_TENSOR (tensor).name)

#define QNN_TENSOR_RANK(tensor) \
    (QNN_GET_TENSOR (tensor).rank)

#define QNN_TENSOR_CLIENTBUF(tensor) \
    (QNN_GET_TENSOR (tensor).clientBuf)

#define QNN_TENSOR_QUANTIZE_PARAMS(tensor) \
    (QNN_GET_TENSOR (tensor).quantizeParams)

// TODO: Workaround! Need to be exported by the QNN SDK.
typedef struct {
  Qnn_GraphHandle_t graph;
  const char        *graphName;
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

using QnnInterfaceGetProvidersFn = decltype(QnnInterface_getProviders);
using QnnSystemInterfaceGetProvidersFn = decltype(QnnSystemInterface_getProviders);

typedef Qnn_ErrorHandle_t (ComposeGraphsFn)(Qnn_BackendHandle_t,
    QNN_INTERFACE_VER_TYPE, Qnn_ContextHandle_t, const GraphConfigInfo_t **,
    const uint32_t, GraphInfo_t ***, uint32_t *, bool, QnnLog_Callback_t,
    QnnLog_Level_t);

typedef Qnn_ErrorHandle_t (FreeGraphFn) (GraphInfo_t ***,
    uint32_t);

typedef struct QnnEngine {
  QNN_INTERFACE_VER_TYPE         interface;
  QNN_SYSTEM_INTERFACE_VER_TYPE  sys_interface;

  // QNN backend library handle.
  std::shared_ptr<void>          libhandle;

  // QNN model library handle.
  std::shared_ptr<void>          model;

  // QNN system library handle.
  std::shared_ptr<void>          syslibhandle;

  // QNN log handle.
  Qnn_LogHandle_t                logger;
  // QNN profiling handle.
  Qnn_ProfileHandle_t            profiler;
  // QNN device handle.
  Qnn_DeviceHandle_t             device;
  // QNN graph context handle.
  Qnn_ContextHandle_t            context;
  // QNN graph systemcontext handle.
  QnnSystemContext_Handle_t      sysctx_handle;
  Qnn_BackendHandle_t            backend;

  // QNN model graphs.
  GraphInfo_t                    **graph_infos;
  uint32_t                       n_graphs;
  bool                           is_cached;

  std::stringstream              versions;

  FreeGraphFn*                   FreeGraph;

  ~QnnEngine () {
    if (sys_interface.systemContextFree && sysctx_handle) {
      sys_interface.systemContextFree (sysctx_handle);
      sysctx_handle = nullptr;
    }

    if (graph_infos) {
      if (is_cached) {
        delete[] *(graph_infos);
        delete[] graph_infos;
      } else {
        FreeGraph (&(graph_infos), n_graphs);
      }

      graph_infos = nullptr;
      n_graphs = 0;
    }

    if (interface.contextFree && context)
      interface.contextFree (context, nullptr);

    if (interface.deviceFree && device)
      interface.deviceFree (device);

    if (interface.profileFree && profiler)
      interface.profileFree (profiler);

    if (interface.backendFree && backend)
      interface.backendFree (backend);
  }

} QnnEngine;

template <class T> static inline T ResolveSymbol (void* lib_handle,
                                                  const char* sym) {
  T ptr = (T) dlsym (lib_handle, sym);

  if (nullptr == ptr) {
    std::cerr << "Unable to access symbol [" << sym
        << "], dlerror(): " << dlerror () << std::endl;
  }

  return ptr;
}

std::string QnnDataTypeToString(Qnn_DataType_t dtype) {
  switch (dtype) {
    case QNN_DATATYPE_UINT_8:
    case QNN_DATATYPE_UFIXED_POINT_8:
      return "UINT_8";
    case QNN_DATATYPE_UINT_16:
    case QNN_DATATYPE_UFIXED_POINT_16:
      return "UINT_16";
    case QNN_DATATYPE_UINT_32:
    case QNN_DATATYPE_UFIXED_POINT_32:
      return "UINT_32";
    case QNN_DATATYPE_UINT_64:
      return "UINT_64";
    case QNN_DATATYPE_INT_8:
    case QNN_DATATYPE_SFIXED_POINT_8:
      return "INT_8";
    case QNN_DATATYPE_INT_16:
    case QNN_DATATYPE_SFIXED_POINT_16:
      return "INT_16";
    case QNN_DATATYPE_INT_32:
    case QNN_DATATYPE_SFIXED_POINT_32:
      return "INT_32";
    case QNN_DATATYPE_INT_64:
      return "INT_64";
    case QNN_DATATYPE_FLOAT_16:
      return "FLOAT_16";
    case QNN_DATATYPE_FLOAT_32:
      return "FLOAT_32";
    default:
      return "UNKNOWN_DATATYPE";
  }
}

void GetScaleOffset (Qnn_Tensor_t * tensor, int32_t& offset, float& scale) {
  switch (QNN_TENSOR_QUANTIZE_PARAMS (tensor).quantizationEncoding) {
    case QNN_QUANTIZATION_ENCODING_SCALE_OFFSET:
    {
      offset = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).scaleOffsetEncoding.offset;
      scale = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).scaleOffsetEncoding.scale;

      break;
    }
    case QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET:
    {
      offset = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).axisScaleOffsetEncoding.scaleOffset->offset;
      scale = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).axisScaleOffsetEncoding.scaleOffset->scale;

      break;
    }
    case QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET:
    {
      offset = QNN_TENSOR_QUANTIZE_PARAMS (
        tensor).bwScaleOffsetEncoding.offset;
      scale = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).bwScaleOffsetEncoding.scale;

      break;
    }
    case QNN_QUANTIZATION_ENCODING_BLOCK:
    {
      offset = QNN_TENSOR_QUANTIZE_PARAMS (
        tensor).blockEncoding.scaleOffset->offset;
      scale = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).blockEncoding.scaleOffset->scale;
      break;
    }
    case QNN_QUANTIZATION_ENCODING_BLOCKWISE_EXPANSION:
    {
      offset = QNN_TENSOR_QUANTIZE_PARAMS (
        tensor).blockwiseExpansion->scaleOffsets->offset;
      scale = QNN_TENSOR_QUANTIZE_PARAMS (
          tensor).blockwiseExpansion->scaleOffsets->scale;

      break;
    }
    default:
      std::cerr << "Datatype not supported yet!" << std::endl;
      break;
  }
}

int32_t InitializeQnnEngine (std::shared_ptr<void>& handle,
                             std::shared_ptr<QnnEngine>& engine) {
  // Load interface symbol of the backend library.
  // Get QNN Interface
  QnnInterfaceGetProvidersFn* GetProviders =
      ResolveSymbol<QnnInterfaceGetProvidersFn *> (handle.get (),
      "QnnInterface_getProviders");

  if (nullptr == GetProviders) {
    return EXIT_FAILURE;
  }

  const QnnInterface_t** providers = nullptr;
  uint32_t n_providers{0};

  if (QNN_SUCCESS != GetProviders (&providers, &n_providers)) {
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

  engine.reset (new QnnEngine);

  engine->FreeGraph = nullptr;
  engine->logger = nullptr;
  engine->profiler = nullptr;
  engine->context = nullptr;
  engine->backend = nullptr;
  engine->sysctx_handle = nullptr;

  engine->libhandle = handle;

  handle.reset ();

  for (uint32_t i = 0; i < n_providers; i++) {
    engine->interface =
        providers[i]->QNN_INTERFACE_VER_NAME;

    uint32_t core_major = providers[i]->apiVersion.coreApiVersion.major;
    uint32_t core_minor = providers[i]->apiVersion.coreApiVersion.minor;
    uint32_t core_patch = providers[i]->apiVersion.coreApiVersion.patch;

    uint32_t backend_major = providers[i]->apiVersion.backendApiVersion.major;
    uint32_t backend_minor = providers[i]->apiVersion.backendApiVersion.minor;
    uint32_t backend_patch = providers[i]->apiVersion.backendApiVersion.patch;

    engine->versions << "\tinterfaceProviders[" << i
        << "]->apiVersion.coreApiVersion " << core_major << "." << core_minor
        << "." << core_patch << std::endl;

    engine->versions << "\tinterfaceProviders[" << i
        << "]->apiVersion.backendApiVersion " << backend_major << "."
        << backend_minor << "." << backend_patch << std::endl;

    const char* ver = nullptr;
    const Qnn_ApiVersion_t* p_api = &providers[i]->apiVersion;
    const QNN_INTERFACE_VER_TYPE* p_iface =
        reinterpret_cast<const QNN_INTERFACE_VER_TYPE *>(p_api + 1);

    p_iface->backendGetBuildId (&ver);

    engine->versions << "\tinterfaceProviders[" << i << "]->backendGetBuildId "
        << ver << std::endl;
  }

  // Set up any necessary backend configurations.
  const QnnBackend_Config_t **bknd_configs = nullptr;
  auto status = engine->interface.backendCreate (engine->logger,
      bknd_configs, &(engine->backend));

  if (QNN_SUCCESS != status) {
    std::cerr << "Could not initialize backend!" << std::endl;
    return EXIT_FAILURE;
  }

  // Enable basic profiling.
  status = engine->interface.profileCreate (engine->backend,
                                            QNN_PROFILE_LEVEL_BASIC,
                                            &(engine->profiler));

  if (QNN_SUCCESS != status) {
    std::cerr << "Unable to create profile handle in the backend!" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int32_t QnnSetupUncachedGraphsInner (std::shared_ptr<QnnEngine>& engine,
                                     std::filesystem::path& file_path) {
  std::shared_ptr<void> handle_model (
    dlopen (file_path.c_str (), RTLD_NOW | RTLD_LOCAL),
    [&](void* ptr) {
      if (ptr)
        dlclose (ptr);
    });

  if (nullptr == handle_model) {
    std::cerr << "Error: cannot load file "
        << file_path << " " << dlerror () << " !!!" << std::endl;

    return EXIT_FAILURE;
  }

  engine->model = handle_model;

  handle_model.reset ();

  // Load symbols for setup of model graph.
  ComposeGraphsFn* ComposeGraphs = ResolveSymbol<ComposeGraphsFn *> (
      engine->model.get (), "QnnModel_composeGraphs");

  if (nullptr == ComposeGraphs) {
    return EXIT_FAILURE;
  }

  FreeGraphFn* FreeGraph = ResolveSymbol<FreeGraphFn *> (
      engine->model.get (), "QnnModel_freeGraphsInfo");

  if (nullptr == FreeGraph) {
    return EXIT_FAILURE;
  }

  engine->FreeGraph = FreeGraph;

  // Set up any context configs that are necessary.
  const QnnContext_Config_t **ctx_configs = nullptr;
  auto status = engine->interface.contextCreate (engine->backend, engine->device,
                                                 ctx_configs, &(engine->context));

  if (QNN_SUCCESS != status) {
    std::cerr << "Could not create context!" << std::endl;
    return EXIT_FAILURE;
  }

  const GraphConfigInfo_t **configs = nullptr;
  uint32_t n_configs = 0;

  status = ComposeGraphs (engine->backend, engine->interface, engine->context,
                          configs, n_configs, &(engine->graph_infos),
                          &(engine->n_graphs), false, nullptr, QNN_LOG_LEVEL_INFO);

  if (QNN_SUCCESS != status) {
    std::cerr << "Graph composition failed!" << std::endl;
    return EXIT_FAILURE;
  }

  for (uint32_t idx = 0; idx < engine->n_graphs; idx++) {
    status = engine->interface.graphFinalize ((*(engine->graph_infos))[idx].graph,
                                              engine->profiler, nullptr);

    if (QNN_SUCCESS != status) {
      std::cerr << "Finalize for graph " << idx << " failed!" << std::endl;
      return EXIT_FAILURE;
    }
  }

  engine->is_cached = false;

  return EXIT_SUCCESS;
}

int32_t QnnSetupUncachedGraphs (std::shared_ptr<QnnEngine>& engine,
                                std::filesystem::path& file_path) {
  if (EXIT_SUCCESS == QnnSetupUncachedGraphsInner (engine, file_path)) {
    return EXIT_SUCCESS;
  }

  std::shared_ptr<void> handle_gpu (
      dlopen ("libQnnGpu.so", RTLD_NOW | RTLD_LOCAL),
      [&](void* ptr) {
        if (ptr)
          dlclose (ptr);
      });

  if (nullptr == handle_gpu) {
    std::cerr << "Failed to open libQnnGpu.so backend, error: "
        << dlerror () << "!" << std::endl;

    return EXIT_FAILURE;
  }

  if (EXIT_SUCCESS != InitializeQnnEngine (handle_gpu, engine)) {
    std::cerr << "Failed to initialize qnn engine !" << std::endl;
    return EXIT_FAILURE;
  }

  return QnnSetupUncachedGraphsInner (engine, file_path);
}

int32_t QnnSetupCachedGraphs (std::shared_ptr<QnnEngine>& engine,
                              std::filesystem::path& file_path) {
  engine->is_cached = true;

  std::ifstream file (file_path.c_str (), std::ios::binary);

  if (!file.is_open ()) {
    std::cerr << "Error: cannot read file " << file_path << " !!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<uint8_t> file_contents ((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());

  std::shared_ptr<void> handle_system(
      dlopen ("libQnnSystem.so", RTLD_NOW | RTLD_LOCAL),
      [&](void* ptr) {
        if (ptr)
          dlclose (ptr);
      });

  if (nullptr == handle_system) {
    std::cerr << "Error: cannot load file " << file_path << " "
        << dlerror () << " !!!" << std::endl;
    return EXIT_FAILURE;
  }

  engine->syslibhandle = handle_system;

  handle_system.reset ();

  QnnSystemInterfaceGetProvidersFn* GetSysIntfProviders = nullptr;

  GetSysIntfProviders = ResolveSymbol<QnnSystemInterfaceGetProvidersFn *> (
      engine->syslibhandle.get (), "QnnSystemInterface_getProviders");

  if (nullptr == GetSysIntfProviders) {
    std::cerr << "Error: cannot resolve symbol"
        << " QnnSystemInterface_getProviders !!!" << std::endl;
    return EXIT_FAILURE;
  }

  const QnnSystemInterface_t** sysintf_providers = nullptr;
  uint32_t providers = 0;

  auto status = GetSysIntfProviders (
      (const QnnSystemInterface_t***)(&sysintf_providers), &providers);

  if (QNN_SUCCESS != status) {
    std::cerr << "Failed to get system interface providers!" << std::endl;
    return EXIT_FAILURE;
  }

  if ((nullptr == sysintf_providers) || (0 == providers)) {
    std::cerr << "Error: cannot GetSysIntfProviders !!!" << std::endl;
    return EXIT_FAILURE;
  }

  engine->sys_interface =
      sysintf_providers[0]->QNN_SYSTEM_INTERFACE_VER_NAME;

  status = engine->sys_interface.systemContextCreate (&engine->sysctx_handle);

  const QnnSystemContext_BinaryInfo_t* binary_info = nullptr;
  Qnn_ContextBinarySize_t binary_info_size = 0;

  engine->sys_interface.systemContextGetBinaryInfo (
      engine->sysctx_handle, static_cast<void *> (file_contents.data ()),
      file_contents.size (), &binary_info, &binary_info_size);

  if (nullptr == binary_info) {
    std::cerr << "Error: cannot systemContextGetBinaryInfo !!!" << std::endl;
    return EXIT_FAILURE;
  }

  engine->versions << "\tbinary_info->contextBinaryInfoV1.coreApiVersion "
      << binary_info->contextBinaryInfoV1.coreApiVersion.major
      << "."
      << binary_info->contextBinaryInfoV1.coreApiVersion.minor
      << "."
      << binary_info->contextBinaryInfoV1.coreApiVersion.patch << std::endl;

  engine->versions << "\tbinary_info->contextBinaryInfoV1.backendApiVersion "
      << binary_info->contextBinaryInfoV1.backendApiVersion.major
      << "."
      << binary_info->contextBinaryInfoV1.backendApiVersion.minor
      << "."
      << binary_info->contextBinaryInfoV1.backendApiVersion.patch
      << std::endl;

  engine->versions << "\tBuildId " << binary_info->contextBinaryInfoV1.buildId
      << std::endl;

  if (!QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_SUPPORTED (binary_info)) {
    std::cerr << "Not supprted QNN system context binary info version !" << std::endl;
    return EXIT_FAILURE;
  }

  uint32_t n_graphs = QNN_GET_SYSTEM_CONTEXT_BINARY_INFO (binary_info).numGraphs;

  QnnSystemContext_GraphInfo_t *graphs =
      QNN_GET_SYSTEM_CONTEXT_BINARY_INFO (binary_info).graphs;

  if (nullptr == graphs) {
    std::cerr << "Qnn system context graph info is nullptr !" << std::endl;
    return EXIT_FAILURE;
  }

  engine->graph_infos = new GraphInfo_t *[n_graphs];
  GraphInfo_t * graph_info_arr = new GraphInfo_t[n_graphs];

  for (size_t idx = 0; idx < n_graphs; idx++) {

    if (!QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_SUPPORTED (&graphs[idx])) {
      std::cerr << "Not supprted QNN system context graph info version !" << std::endl;
      return EXIT_FAILURE;
    }

    graph_info_arr[idx].graphName =
        QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO (&graphs[idx]).graphName;
    graph_info_arr[idx].numInputTensors =
        QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO (&graphs[idx]).numGraphInputs;
    graph_info_arr[idx].inputTensors =
        QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO (&graphs[idx]).graphInputs;
    graph_info_arr[idx].numOutputTensors =
        QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO (&graphs[idx]).numGraphOutputs;
    graph_info_arr[idx].outputTensors =
        QNN_GET_SYSTEM_CONTEXT_GRAPH_INFO (&graphs[idx]).graphOutputs;

    engine->graph_infos[idx] = graph_info_arr + idx;
  }

  if (nullptr == engine->interface.contextCreateFromBinary) {
    std::cerr << "contextCreateFromBinaryFnHandle is nullptr." << std::endl;
    return false;
  }

  // Set up any context configs that are necessary.
  const QnnContext_Config_t **ctx_configs = nullptr;

  auto err_code = engine->interface.contextCreateFromBinary (
      engine->backend, engine->device, (const QnnContext_Config_t**)&ctx_configs,
      static_cast<void*> (file_contents.data ()), file_contents.size (),
      &(engine->context), engine->profiler);


  if (err_code != QNN_SUCCESS) {
    std::cerr << "Could not create context from binary." << std::endl;
    return EXIT_FAILURE;
  }

  for (size_t idx = 0; idx < engine->n_graphs; idx++) {
    if (nullptr == engine->interface.graphRetrieve) {
      std::cerr << "graphRetrieveFnHandle is nullptr." << std::endl;
      return EXIT_FAILURE;
    }

    status = engine->interface.graphRetrieve (
        engine->context,
        (*engine->graph_infos)[idx].graphName,
        &((*engine->graph_infos)[idx].graph));

    if (QNN_SUCCESS != status) {
      std::cerr << "Unable to retrieve graph handle for graph Idx: "
          << idx << std::endl;
      return EXIT_FAILURE;
    }
  }

  std::cout << "Setup graph using context binary exit." << std::endl;

  return EXIT_SUCCESS;
}

void QnnPrintVersions (std::shared_ptr<QnnEngine>& engine) {
  std::cout << engine->versions.str ();
}

int32_t PrintTensors (Qnn_Tensor_t * tensor, uint32_t n_tensors,
                      std::string specific) {
  Qnn_Tensor_t *current_tensor = nullptr;

  current_tensor = &(tensor[0]);

  std::cout << "Number of " << specific << " tensors: "
      << n_tensors << std::endl;

  std::cout << specific << " tensors type: "
      << QnnDataTypeToString (QNN_TENSOR_DATA_TYPE (current_tensor))
      << std::endl;

  for (uint32_t idx = 0; idx < n_tensors; ++idx) {
    current_tensor = &(tensor[idx]);

    if (!QNN_TENSOR_VERSION_SUPPORTED (current_tensor)) {
      std::cerr << "Not supported tensor version!" << std::endl;
      return EXIT_FAILURE;
    }

    std::cout << specific << " tensor[" << idx << "] name: "
        << QNN_TENSOR_NAME (current_tensor) << std::endl;

    int32_t offset = 0;
    float scale = 0.0f;

    GetScaleOffset (current_tensor, offset, scale);

    std::cout << specific << " tensor[" << idx << "] offset: "
        << offset << std::endl;
    std::cout << specific << " tensor[" << idx << "] scale: "
        << scale << std::endl;

    auto rank = QNN_TENSOR_RANK (current_tensor);

    for (uint32_t num = 0; num < rank; ++num) {
      std::cout << specific << " tensor[" << idx << "] Dimension[" << num
          << "]: " << QNN_TENSOR_DIMENSION (current_tensor, num) << std::endl;
    }
  }

  std::cout << "=================================================" << std::endl;

  return EXIT_SUCCESS;
}

int main (int argc, char* argv[]) {
  std::shared_ptr<void> handle_htp (
      dlopen ("libQnnHtp.so", RTLD_NOW | RTLD_LOCAL),
      [&](void* ptr) {
        if (ptr)
          dlclose (ptr);
      });

  if (nullptr == handle_htp) {
    std::cerr << "Failed to open libQnnHtp.so backend, error: "
        << dlerror () << "!" << std::endl;

    return EXIT_FAILURE;
  }

  std::shared_ptr<QnnEngine> engine = nullptr;

  if (EXIT_SUCCESS != InitializeQnnEngine (handle_htp, engine)) {
    std::cerr << "Failed to initialize qnn engine !" << std::endl;
    return EXIT_FAILURE;
  }

  if (argc > 1) {
    std::filesystem::path file_path(argv[1]);

    std::string extension(file_path.extension ());

    if (!extension.compare (".so")) { // LibModel.so

      if (EXIT_SUCCESS != QnnSetupUncachedGraphs (engine, file_path)) {
        std::cerr << "Failed to setup qnn engine uncached graphs !" << std::endl;
        return EXIT_FAILURE;
      }

    } else if (!extension.compare (".bin")) { // ContextBin
      if (EXIT_SUCCESS != QnnSetupCachedGraphs (engine, file_path)) {
        std::cerr << "Failed to setup qnn engine cached graphs !" << std::endl;
        return EXIT_FAILURE;
      }
    } else {
      std::cerr << "Error: unknow file extension : " << extension << " !!!"
          << std::endl;
      return EXIT_FAILURE;
    }
  } else {
    std::cerr << "No input file was given !!!" << std::endl;
    return EXIT_FAILURE;
  }

  GraphInfo_t *graph_info = engine->graph_infos[0];

  // Input Tensors

  if (EXIT_SUCCESS != PrintTensors (graph_info->inputTensors,
                                    graph_info->numInputTensors, "input")) {
    std::cerr << "Failed to print input tensors" << std::endl;
    return EXIT_FAILURE;
  }

  // Output Tensors

  if (EXIT_SUCCESS != PrintTensors (graph_info->outputTensors,
                                    graph_info->numOutputTensors, "output")) {
    std::cerr << "Failed to print output tensors" << std::endl;
    return EXIT_FAILURE;
  }

  QnnPrintVersions (engine);

  std::cout << "I am Ready !" << std::endl;

  return EXIT_SUCCESS;
}
