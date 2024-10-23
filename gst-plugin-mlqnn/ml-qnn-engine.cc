/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <map>
#include <numeric>
#include <vector>
#include <filesystem>

#include <dlfcn.h>

#include <QnnInterface.h>
#include <System/QnnSystemInterface.h>
#include <System/QnnSystemContext.h>

#include "ml-qnn-engine.h"

#define GST_CAT_DEFAULT gst_ml_qnn_engine_debug_category()
#define GST_CAT_QNN_SDK gst_ml_qnn_sdk_debug_category()

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

#if defined(QNN_SYSTEM_CONTEXT_GRAPH_INFO_V2_INIT)

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

#if defined(QNN_SYSTEM_CONTEXT_BINARY_INFO_V2_INIT)

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

typedef Qnn_ErrorHandle_t (*ComposeGraphsFn)(Qnn_BackendHandle_t,
    QNN_INTERFACE_VER_TYPE, Qnn_ContextHandle_t, const GraphConfigInfo_t **,
    const uint32_t, GraphInfo_t ***, uint32_t *, bool, QnnLog_Callback_t,
    QnnLog_Level_t);

typedef Qnn_ErrorHandle_t (*FreeGraphFn) (GraphInfo_t ***,
    uint32_t);

struct _GstMLQnnEngine
{
  GstMLInfo                      *ininfo;
  GstMLInfo                      *outinfo;

  GstStructure                   *settings;

  // QNN backend library handle.
  gpointer                       libhandle;
  // QNN model library handle.
  gpointer                       model;
  // QNN system library handle.
  gpointer                       syslibhandle;

  // QNN versioned interface.
  QNN_INTERFACE_VER_TYPE         interface;
  // QNN versioned system interface.
  QNN_SYSTEM_INTERFACE_VER_TYPE  sysinterface;
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
  gboolean                       iscached;
  // QNNF library APIs
  FreeGraphFn                    FreeGraph;

  // Device Platform Information.
  const QnnDevice_PlatformInfo_t *device_platform;
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

  switch (QNN_TENSOR_DATA_TYPE (tensor)) {
    case QNN_DATATYPE_UFIXED_POINT_8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);
      int32_t offset = QNN_TENSOR_QUANTIZE_PARAMS (tensor).scaleOffsetEncoding.offset;
      float scale = QNN_TENSOR_QUANTIZE_PARAMS (tensor).scaleOffsetEncoding.scale;

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = (float)(data[idx] + offset) * scale;

      break;
    }
    case QNN_DATATYPE_UFIXED_POINT_16:
    {
      uint16_t *data = reinterpret_cast<uint16_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);
      int32_t offset = QNN_TENSOR_QUANTIZE_PARAMS (tensor).scaleOffsetEncoding.offset;
      float scale = QNN_TENSOR_QUANTIZE_PARAMS (tensor).scaleOffsetEncoding.scale;

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = (float)(data[idx] + offset) * scale;

      break;
    }
    case QNN_DATATYPE_UINT_8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_UINT_16:
    {
      uint16_t *data = reinterpret_cast<uint16_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_UINT_32:
    {
      uint32_t *data = reinterpret_cast<uint32_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_INT_8:
    {
      int8_t *data = reinterpret_cast<int8_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_INT_16:
    {
      int16_t *data = reinterpret_cast<int16_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_INT_32:
    {
      int32_t *data = reinterpret_cast<int32_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

      for (auto idx = 0; idx < n_elements; idx++)
        output[idx] = static_cast<float>(data[idx]);

      break;
    }
    case QNN_DATATYPE_BOOL_8:
    {
      uint8_t *data = reinterpret_cast<uint8_t *>(QNN_TENSOR_CLIENTBUF (tensor).data);

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
gst_ml_qnn_graph_info_from_binary_info (
    const QnnSystemContext_BinaryInfo_t* binary_info,
    GraphInfo_t**& graph_infos, uint32_t& n_graphs)
{
  if (nullptr == binary_info) {
    GST_ERROR ("binary_info is nullptr.");
    return FALSE;
  }

  if (!QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_SUPPORTED (binary_info)) {
    GST_ERROR ("Not supprted QNN system context binary info version !");
    return FALSE;
  }

  n_graphs = QNN_GET_SYSTEM_CONTEXT_BINARY_INFO (binary_info).numGraphs;
  QnnSystemContext_GraphInfo_t *graphs =
      QNN_GET_SYSTEM_CONTEXT_BINARY_INFO (binary_info).graphs;

  graph_infos = g_new0 (GraphInfo_t*, n_graphs);
  GraphInfo_t* graph_info_arr = g_new0 (GraphInfo_t, n_graphs);

  for (size_t idx = 0; idx < n_graphs; idx++) {
    GST_INFO ("Extracting graph_infos for graph Idx: %lu", idx);

    GST_INFO ("Info is V%d Idx: %lu", graphs[idx].version, idx);

    if (!QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_SUPPORTED (&graphs[idx])) {
      GST_ERROR ("Not supprted QNN system context graph info version !");
      return FALSE;
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

    graph_infos[idx] = graph_info_arr + idx;
  }
  return TRUE;
}

static gboolean
gst_ml_qnn_create_device_config (GstMLQnnEngine *engine,
    QnnDevice_Config_t**& dev_configs)
{
  QnnDevice_PlatformInfo_t* device_platform_info = nullptr;
  QnnDevice_HardwareDeviceInfo_t* p_hw_device_info = nullptr;

  Qnn_ErrorHandle_t error = engine->interface.deviceGetPlatformInfo(nullptr,
      &(engine->device_platform));

  if (QNN_DEVICE_ERROR_UNSUPPORTED_FEATURE == error) {
    GST_WARNING ("Device feature is not supported!");
    return TRUE;
  }

  if (QNN_SUCCESS != error) {
    GST_ERROR ("Failed to get platform info. Error %ld",
        QNN_GET_ERROR_CODE (error));
    return FALSE;
  }

  guint backend_device_id = 0;
  QnnDevice_HardwareDeviceInfo_t* hw_device_info =
      engine->device_platform->v1.hwDevices;
  QnnDevice_HardwareDeviceInfo_t* hw_device_info_chosen = nullptr;

  gst_structure_get_uint (engine->settings,
      GST_ML_QNN_ENGINE_OPT_BACKEND_DEVICE_ID, &(backend_device_id));

  for (uint32_t idx = 0; idx < engine->device_platform->v1.numHwDevices; ++idx) {
    if (hw_device_info[idx].v1.deviceId == backend_device_id) {
      hw_device_info_chosen = &hw_device_info[idx];
      GST_INFO ("HW device found!, id = %u", backend_device_id);
      break;
    }
  }

  if (nullptr == hw_device_info_chosen) {
    GST_ERROR ("Failed to get device with id = %u.", backend_device_id);
    return FALSE;
  }

  device_platform_info = g_new0 (QnnDevice_PlatformInfo_t, 1);
  device_platform_info->version = QNN_DEVICE_PLATFORM_INFO_VERSION_1;
  // We only choose 1 device here.
  device_platform_info->v1.numHwDevices = 1;
  device_platform_info->v1.hwDevices = hw_device_info_chosen;

  dev_configs = g_new0 (QnnDevice_Config_t*, 2);
  QnnDevice_Config_t* dev_config_arr = g_new0 (QnnDevice_Config_t, 1);

  dev_config_arr[0].option = QNN_DEVICE_CONFIG_OPTION_PLATFORM_INFO;
  dev_config_arr[0].hardwareInfo = device_platform_info;

  dev_configs[0] = dev_config_arr;

  // Null-terminate the array.
  dev_configs[1] = NULL;

  return TRUE;
}

static gboolean
gst_ml_qnn_engine_setup_backend (GstMLQnnEngine *engine)
{
  gboolean success = TRUE;
  const gchar *filename = NULL;
  guint idx = 0;

  filename = GET_OPT_BACKEND (engine->settings);

  engine->libhandle = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
  if (engine->libhandle == NULL) {
    GST_ERROR ("Failed to open %s backend, error: %s!", filename, dlerror());
    return FALSE;
  }

  GST_DEBUG ("Loaded backend '%s'!", filename);

  // Load interface symbol of the backend library.
  QnnInterfaceGetProvidersFn* GetProviders;
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

  engine->interface = providers[0]->QNN_INTERFACE_VER_NAME;

  GST_DEBUG ("Interface Provider core api version : %d.%d.%d",
      providers[0]->apiVersion.coreApiVersion.major,
      providers[0]->apiVersion.coreApiVersion.minor,
      providers[0]->apiVersion.coreApiVersion.patch);
  GST_DEBUG ("Interface Provider backend api version : %d.%d.%d",
      providers[0]->apiVersion.backendApiVersion.major,
      providers[0]->apiVersion.backendApiVersion.minor,
      providers[0]->apiVersion.backendApiVersion.patch);

  const char* ver = nullptr;
  Qnn_ApiVersion_t *p_api = const_cast<Qnn_ApiVersion_t *>(&(providers[0]->apiVersion));
  QNN_INTERFACE_VER_TYPE *p_iface = reinterpret_cast<QNN_INTERFACE_VER_TYPE *>(p_api + 1);
  p_iface->backendGetBuildId(&ver);
  GST_DEBUG ("Interface Provider build id : %s", ver);

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

  QnnDevice_Config_t **dev_configs = nullptr;

  success = gst_ml_qnn_create_device_config (engine, dev_configs);

  if (!success)
    return FALSE;

  status = engine->interface.deviceCreate(engine->logger,
      const_cast<const QnnDevice_Config_t **>(dev_configs), &(engine->device));

  while ((dev_configs != NULL) && (dev_configs[idx] != NULL)) {
    if (QNN_DEVICE_CONFIG_OPTION_PLATFORM_INFO == dev_configs[idx]->option)
      g_free (dev_configs[idx]->hardwareInfo);

    g_free (dev_configs[idx]);
    idx++;
  }

  g_free (dev_configs);

  if (QNN_SUCCESS == status) {
    GST_DEBUG ("Device created");
  } else if (QNN_DEVICE_ERROR_UNSUPPORTED_FEATURE != status) {
    GST_ERROR ("Could not create device!");
    return FALSE;
  }

  if (engine->iscached) {
    if ((filename = GET_OPT_SYSLIB (engine->settings)) == NULL) {
      GST_ERROR ("No system library file name!");
      return FALSE;
    }

    engine->syslibhandle = dlopen (filename, RTLD_NOW | RTLD_LOCAL);
    if (engine->syslibhandle == NULL) {
      GST_ERROR ("Failed to open %s sys library, error: %s!", filename,
          dlerror());
      return FALSE;
    }

    // Load sys interface symbol of the sys library.
    QnnSystemInterfaceGetProvidersFn* GetSysIntfProviders;
    success &= load_symbol ((gpointer*)&GetSysIntfProviders,
        engine->syslibhandle, "QnnSystemInterface_getProviders");

    // Check whether symbol loading was successful.
    if (!success) {
      GST_ERROR ("Failed to load symbol.");
      return FALSE;
    }

    const QnnSystemInterface_t** sysintf_providers = nullptr;
    n_providers = 0;

    // Query for all available sys interfaces.
    status = GetSysIntfProviders (
      (const QnnSystemInterface_t***)(&sysintf_providers), &n_providers);

    if (QNN_SUCCESS != status) {
      GST_ERROR ("Failed to get system interface providers!");
      return FALSE;
    }

    // Check for validity of returned interfaces,
    if ((nullptr == sysintf_providers) || (0 == n_providers)) {
      GST_ERROR ("Received Null system interface providers!");
      return FALSE;
    }

    engine->sysinterface =
        sysintf_providers[0]->QNN_SYSTEM_INTERFACE_VER_NAME;
  }

  return TRUE;
}

static gboolean
gst_ml_qnn_engine_setup_cached_graphs (GstMLQnnEngine *engine)
{
  gboolean res = TRUE;
  const gchar *filename = NULL;

  if ((filename = GET_OPT_MODEL (engine->settings)) == NULL) {
    GST_ERROR ("No context bin file name!");
    return FALSE;
  }

  if ((nullptr == engine->sysinterface.systemContextCreate) ||
      (nullptr == engine->sysinterface.systemContextGetBinaryInfo) ||
          (nullptr == engine->sysinterface.systemContextFree)) {
    GST_ERROR ("QNN System function pointers are not populated.");
    return FALSE;
  }

  if (!g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
    GST_ERROR ("File %s does not exist", filename);
    return FALSE;
  }

  GError *error = NULL;
  uint64_t buffer_size = 0;
  char *buffer = nullptr;

  // read serialized binary into a byte buffer
  res = g_file_get_contents (filename, &buffer, &buffer_size, &error);
  if (!res) {
    GST_ERROR ("Failed to get serialized binary content, error: %s!",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return FALSE;
  }

  // inspect binary info
  auto status =
      engine->sysinterface.systemContextCreate(&(engine->sysctx_handle));
  if (QNN_SUCCESS != status) {
    GST_ERROR ("Could not create system context.");
    return FALSE;
  }
  GST_DEBUG ("System context created");

  const QnnSystemContext_BinaryInfo_t* binary_info = nullptr;
  Qnn_ContextBinarySize_t binary_info_size = 0;

  status = engine->sysinterface.systemContextGetBinaryInfo(
      (engine->sysctx_handle), static_cast<void*>(buffer), buffer_size,
          &(binary_info), &binary_info_size);
  if (QNN_SUCCESS != status) {
    GST_ERROR ("Failed to get context binary info");
    return FALSE;
  }
  GST_DEBUG ("Read binary info from bin file");

  GST_DEBUG ("Binary info core api version : %d.%d.%d",
      binary_info->contextBinaryInfoV1.coreApiVersion.major,
      binary_info->contextBinaryInfoV1.coreApiVersion.minor,
      binary_info->contextBinaryInfoV1.coreApiVersion.patch);
  GST_DEBUG ("Binary info backend api version : %d.%d.%d",
      binary_info->contextBinaryInfoV1.backendApiVersion.major,
      binary_info->contextBinaryInfoV1.backendApiVersion.minor,
      binary_info->contextBinaryInfoV1.backendApiVersion.patch);
  GST_DEBUG ("Binary info build id : %s",
      binary_info->contextBinaryInfoV1.buildId);

  // populate GraphInfo_t based on binary info
  res = gst_ml_qnn_graph_info_from_binary_info (binary_info,
      engine->graph_infos, engine->n_graphs);
  if (!res) {
    GST_ERROR("Failed to populate Graph Info.");
    return FALSE;
  }
  GST_DEBUG ("Populated Graph Info from Binary Info");

  // Set up any context configs that are necessary.
  const QnnContext_Config_t **ctx_configs = nullptr;

  if (nullptr == engine->interface.contextCreateFromBinary) {
    GST_ERROR ("contextCreateFromBinaryFnHandle is nullptr.");
    return FALSE;
  }
  if (engine->interface.contextCreateFromBinary(engine->backend,
      engine->device, (const QnnContext_Config_t**)&ctx_configs,
          static_cast<void*>(buffer), buffer_size, &(engine->context),
              engine->profiler)) {
    GST_ERROR ("Could not create context from binary.");
    res = FALSE;
  } else {
    GST_DEBUG ("Context created from cached binary");
    res = TRUE;
  }

  if (res) {
    for (size_t idx = 0; idx < engine->n_graphs; idx++) {
      if (nullptr == engine->interface.graphRetrieve) {
        GST_ERROR ("graphRetrieveFnHandle is nullptr.");
        res = FALSE;
        break;
      }
      status = engine->interface.graphRetrieve(engine->context,
          (*engine->graph_infos)[idx].graphName,
              &((*engine->graph_infos)[idx].graph));
      if (QNN_SUCCESS != status) {
        GST_ERROR ("Unable to retrieve graph handle for graph Idx: %ld",
            idx);
        res = FALSE;
        break;
      }
    }
  }
  if (!res) {
    GST_ERROR ("ERROR: Need to clean up the graph info structures");
  }
  GST_INFO ("Setup graph using context binary exit.");
  return res;
}

static gboolean
gst_ml_qnn_engine_setup_uncached_graphs (GstMLQnnEngine *engine)
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

  char** qnn_sdk_version;
  success = load_symbol ((gpointer*)&qnn_sdk_version, engine->model,
      "QNN_SDK_VERSION");
  if (nullptr != qnn_sdk_version) {
    GST_DEBUG ("Model build id : %s", *qnn_sdk_version);
  }

  // Set up any context configs that are necessary.
  const QnnContext_Config_t **ctx_configs = nullptr;
  auto status = engine->interface.contextCreate(engine->backend,
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
      &(engine->n_graphs), false, gst_ml_qnn_log_callback,
      QNN_LOG_LEVEL_INFO);

  if (QNN_SUCCESS != status) {
    GST_ERROR ("Graph composition failed!");
    return FALSE;
  }
  GST_DEBUG ("Graph composition success");

  for (uint32_t idx = 0; idx < engine->n_graphs; idx++) {
    status = engine->interface.graphFinalize(
        (*(engine->graph_infos))[idx].graph, engine->profiler, nullptr);

    if (QNN_SUCCESS != status) {
      GST_ERROR ("Finalize for graph %u failed!", idx);
      engine->FreeGraph (&(engine->graph_infos), engine->n_graphs);
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
  Qnn_Tensor_t *input_tensor = NULL;
  Qnn_Tensor_t *output_tensor = NULL;
  gboolean success = TRUE;

  GST_DEBUG ("Creating engine");

  engine = g_new0 (GstMLQnnEngine, 1);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  std::filesystem::path modelpath (GET_OPT_MODEL (engine->settings));

  engine->iscached = (modelpath.extension() == ".bin") ? TRUE : FALSE;

  // Initialize backend.
  if (!gst_ml_qnn_engine_setup_backend (engine)) {
    GST_ERROR ("Failed to setup backend!");
    goto cleanup;
  }

  // Initialize model graphs.
  if (engine->iscached) {
    success = gst_ml_qnn_engine_setup_cached_graphs (engine);
  } else {
    success = gst_ml_qnn_engine_setup_uncached_graphs (engine);
  }

  if (!success) {
    GST_ERROR ("Failed to setup graph!");
    goto cleanup;
  }

  if (engine->n_graphs > 1) {
    GST_WARNING ("Multiple Graphs Detected!!\n"
        "Support is available for single graph. The first graph will be executed.");
  }

  graph_info = engine->graph_infos[0];
  input_tensor = &(graph_info->inputTensors[0]);

  if (!QNN_TENSOR_VERSION_SUPPORTED (input_tensor)) {
    GST_ERROR ("Not supported tensor version!");
    goto cleanup;
  }

  // Translate information about input tensors to GstMLInfo.
  engine->ininfo->n_tensors = graph_info->numInputTensors;
  engine->ininfo->type = qnn_to_ml_type (
      QNN_TENSOR_DATA_TYPE (input_tensor));

  GST_DEBUG ("Number of input tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->ininfo));
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->ininfo)));

  for (auto idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    input_tensor = &(graph_info->inputTensors[idx]);

    if (!QNN_TENSOR_VERSION_SUPPORTED (input_tensor)) {
      GST_ERROR ("Not supported tensor version!");
      goto cleanup;
    }

    engine->ininfo->n_dimensions[idx] =
        QNN_TENSOR_RANK (input_tensor);

    for (auto num = 0; num < engine->ininfo->n_dimensions[idx]; ++num) {
      engine->ininfo->tensors[idx][num] =
          QNN_TENSOR_DIMENSION (input_tensor, num);

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
    output_tensor = &(graph_info->outputTensors[idx]);

    if (!QNN_TENSOR_VERSION_SUPPORTED (output_tensor)) {
      GST_ERROR ("Not supported tensor version!");
      goto cleanup;
    }

    engine->outinfo->n_dimensions[idx] = QNN_TENSOR_RANK (output_tensor);

    for (auto num = 0; num < engine->outinfo->n_dimensions[idx]; ++num) {
      engine->outinfo->tensors[idx][num] = QNN_TENSOR_DIMENSION (output_tensor, num);

      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          engine->outinfo->tensors[idx][num]);
    }

    // TODO: Workaround! Need to handle tensors of different data type to avoid
    // buffer allocation and buffer copy
    gsize size = gst_ml_info_tensor_size (engine->outinfo, idx);

    // Use the tensor type from the graph instead of that from MLInfo
    size /= gst_ml_type_get_size (engine->outinfo->type);
    size *= kDataTypeToSize.find(QNN_TENSOR_DATA_TYPE (output_tensor))->second;

    QNN_TENSOR_CLIENTBUF (output_tensor).data = g_malloc (size);
    QNN_TENSOR_CLIENTBUF (output_tensor).dataSize = size;
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
    Qnn_Tensor_t *tensor;

    for (auto idx = 0; idx < graph_info->numInputTensors; idx++) {
      tensor = &(graph_info->inputTensors[idx]);
      QNN_TENSOR_CLIENTBUF (tensor).data = NULL;
      QNN_TENSOR_CLIENTBUF (tensor).dataSize = 0;
    }

    for (auto idx = 0; idx < graph_info->numOutputTensors; ++idx) {
      tensor = &(graph_info->outputTensors[idx]);
      g_free (QNN_TENSOR_CLIENTBUF (tensor).data);
      QNN_TENSOR_CLIENTBUF (tensor).data = NULL;
      QNN_TENSOR_CLIENTBUF (tensor).dataSize = 0;
    }

    if (engine->iscached) {
      if (engine->sysinterface.systemContextFree && engine->sysctx_handle) {
        engine->sysinterface.systemContextFree (engine->sysctx_handle);
        engine->sysctx_handle = nullptr;
      }
      g_free (*(engine->graph_infos));
      g_free (engine->graph_infos);
    } else {
      engine->FreeGraph (&(engine->graph_infos), engine->n_graphs);
    }
    engine->graph_infos = NULL;
    engine->n_graphs = 0;
  }

  if (engine->interface.deviceFreePlatformInfo && engine->device_platform)
    engine->interface.deviceFreePlatformInfo (nullptr, engine->device_platform);

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

  if (engine->syslibhandle != NULL)
    dlclose (engine->syslibhandle);

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

    QNN_TENSOR_CLIENTBUF (tensor).data = GST_ML_FRAME_BLOCK_DATA (inframe, idx);
    QNN_TENSOR_CLIENTBUF (tensor).dataSize = GST_ML_FRAME_BLOCK_SIZE (inframe, idx);
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
    Qnn_Tensor_t *tensor = &(graph_info->outputTensors[idx]);

    // TODO: Workaround! Need to handle tensors of different data type to avoid
    // buffer allocation and buffer copy
    if (QNN_TENSOR_DATA_TYPE (tensor) == QNN_DATATYPE_FLOAT_32) {
      memcpy (reinterpret_cast<float *>(GST_ML_FRAME_BLOCK_DATA(outframe, idx)),
          reinterpret_cast<float *> (QNN_TENSOR_CLIENTBUF (tensor).data),
          GST_ML_FRAME_BLOCK_SIZE (outframe, idx));
    } else {
      GST_DEBUG ("Converting Native tensor type to Float");
      gst_ml_qnn_convert_to_float (outframe, idx, tensor);
    }
  }

  return TRUE;
}
