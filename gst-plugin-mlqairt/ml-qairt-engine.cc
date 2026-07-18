/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ml-qairt-engine.h"

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

#include <gst/ml/gstmlmeta.h>

#include <QairtApi.hpp>
#include <QairtBackend.hpp>
#include <QairtContext.hpp>
#include <QairtGraph.hpp>
#include <QairtInfo.hpp>
#include <QairtTensor.hpp>
#include <QairtLog.hpp>
#include <System/QairtSystemContext.hpp>
#include <System/QairtSystemDlc.hpp>

#define GST_CAT_DEFAULT gst_ml_qairt_engine_debug_category()

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

/// QAIRT system library, loaded at runtime via the loader search path.
#define GST_ML_QAIRT_SYSTEM_LIB "libQairtSystem.so"

/// Per-output tensor descriptor holding the dequantization parameters and the
/// QAIRT tensor used to bind the output buffer at execution time.
struct GstMLQairtOutput {
  std::shared_ptr<qairt::Tensor> tensor;
  qairt::DataType                type;
  gboolean                       quantized;
  gfloat                         scale;
  gint32                         offset;
  std::string                    name;
};

struct _GstMLQairtEngine
{
  GstMLInfo *ininfo;
  GstMLInfo *outinfo;

  // QAIRT low-level runtime objects. Declared in destruction-safe order; the
  // struct is released with delete so members tear down bottom-to-top.
  std::shared_ptr<qairt::Api>                api;
  std::shared_ptr<qairt::SystemApi>          sysapi;
  std::shared_ptr<qairt::Log>                log;
  std::shared_ptr<qairt::Backend>            backend;
  std::shared_ptr<qairt::Context>            context;

  // Model containers. Kept alive for the lifetime of the engine as the graph
  // metadata references memory owned by them.
  std::shared_ptr<qairt::SystemDlc>          dlc;
  std::shared_ptr<qairt::SystemContext>      syscontext;
  std::shared_ptr<qairt::SystemContextGraphInfoSet> graphinfoset;
  std::vector<qairt::SystemContextGraphInfo> graphinfos;

  // The single graph being executed (graph 0).
  std::shared_ptr<qairt::Graph>              graph;

  // Pre-built input tensors and output descriptors, in negotiated order.
  std::vector<std::shared_ptr<qairt::Tensor>> inputs;
  std::vector<GstMLQairtOutput>              outputs;
};

static GstDebugCategory *
gst_ml_qairt_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("ml-qairt-engine", 0,
        "Machine Learning QAIRT Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

GType
gst_ml_qairt_delegate_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_QAIRT_DELEGATE_CPU,
        "Run the processing on the CPU", "cpu"
    },
    { GST_ML_QAIRT_DELEGATE_GPU,
        "Run the processing on the GPU", "gpu"
    },
    { GST_ML_QAIRT_DELEGATE_HTP,
        "Run the processing on the Hexagon Tensor Processor", "htp"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLQairtDelegate", variants);

  return gtype;
}

GType
gst_ml_qairt_exec_priority_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_QAIRT_EXEC_PRIORITY_NORMAL,
        "Normal priority", "normal"
    },
    { GST_ML_QAIRT_EXEC_PRIORITY_LOW,
        "Lower priority", "low"
    },
    { GST_ML_QAIRT_EXEC_PRIORITY_NORMAL_HIGH,
        "Between normal and high priority", "normal-high"
    },
    { GST_ML_QAIRT_EXEC_PRIORITY_HIGH,
        "Higher than normal priority", "high"
    },
    { GST_ML_QAIRT_EXEC_PRIORITY_CRITICAL,
        "Critical priority", "critical"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLQairtExecPriority", variants);

  return gtype;
}

static GstMLType
qairt_to_ml_type (qairt::DataType type)
{
  switch (type) {
    case qairt::DataType::Int8:
    case qairt::DataType::SFixedPoint8:
      return GST_ML_TYPE_INT8;
    case qairt::DataType::UInt8:
    case qairt::DataType::UFixedPoint8:
    case qairt::DataType::Bool8:
      return GST_ML_TYPE_UINT8;
    case qairt::DataType::Int16:
    case qairt::DataType::SFixedPoint16:
      return GST_ML_TYPE_INT16;
    case qairt::DataType::UInt16:
    case qairt::DataType::UFixedPoint16:
      return GST_ML_TYPE_UINT16;
    case qairt::DataType::Int32:
    case qairt::DataType::SFixedPoint32:
      return GST_ML_TYPE_INT32;
    case qairt::DataType::UInt32:
    case qairt::DataType::UFixedPoint32:
      return GST_ML_TYPE_UINT32;
    case qairt::DataType::Int64:
      return GST_ML_TYPE_INT64;
    case qairt::DataType::UInt64:
      return GST_ML_TYPE_UINT64;
    case qairt::DataType::Float16:
      return GST_ML_TYPE_FLOAT16;
    case qairt::DataType::Float32:
      return GST_ML_TYPE_FLOAT32;
    default:
      GST_ERROR ("Unsupported QAIRT data type %d!", static_cast<int> (type));
      break;
  }

  return GST_ML_TYPE_UNKNOWN;
}

// Map the delegate enum to the QAIRT backend shared library name. QAIRT selects
// the backend by which library the Api loads (there is no runtime enum).
static const gchar *
delegate_to_backend_lib (GstMLQairtDelegate delegate)
{
  switch (delegate) {
    case GST_ML_QAIRT_DELEGATE_GPU:
      return "libQairtGpu.so";
    case GST_ML_QAIRT_DELEGATE_HTP:
      return "libQairtHtp.so";
    case GST_ML_QAIRT_DELEGATE_CPU:
    default:
      return "libQairtCpu.so";
  }
}

static qairt::Priority
to_priority (GstMLQairtExecPriority priority)
{
  switch (priority) {
    case GST_ML_QAIRT_EXEC_PRIORITY_LOW:
      return qairt::Priority::Low;
    case GST_ML_QAIRT_EXEC_PRIORITY_NORMAL_HIGH:
      return qairt::Priority::NormalHigh;
    case GST_ML_QAIRT_EXEC_PRIORITY_HIGH:
      return qairt::Priority::High;
    case GST_ML_QAIRT_EXEC_PRIORITY_CRITICAL:
      return qairt::Priority::Critical;
    case GST_ML_QAIRT_EXEC_PRIORITY_NORMAL:
    default:
      return qairt::Priority::Normal;
  }
}

// Fill a GstMLInfo tensor entry from a QAIRT tensor info. Returns FALSE if the
// tensor rank exceeds what GstMLInfo can represent.
static gboolean
fill_tensor_dims (GstMLInfo * info, guint idx, guint rank,
    const uint32_t * dims)
{
  if (rank > GST_ML_TENSOR_MAX_DIMS) {
    GST_ERROR ("Tensor rank %u exceeds maximum %u!", rank,
        GST_ML_TENSOR_MAX_DIMS);
    return FALSE;
  }

  GST_ML_INFO_N_DIMENSIONS (info, idx) = rank;

  for (guint num = 0; num < rank; num++) {
    GST_ML_INFO_TENSOR_DIM (info, idx, num) = dims[num];
    GST_DEBUG ("Tensor[%u] Dimension[%u]: %u", idx, num, dims[num]);
  }

  return TRUE;
}

// Build a QAIRT Tensor from context tensor metadata, matching the reference
// QairtSampleApp::prepareTensor(). The client buffer is bound per-inference.
static std::shared_ptr<qairt::Tensor>
make_tensor (GstMLQairtEngine * engine, qairt::SystemContextTensorInfo & info,
    gboolean is_input)
{
  std::shared_ptr<qairt::Tensor> tensor =
      engine->api->makeShared<qairt::Tensor> ();

  qairt::TensorProperties prop = engine->api->make<qairt::TensorProperties> ();
  prop.setIsInput (is_input);
  prop.setIsOutput (!is_input);

  tensor->setId (info.getId ());
  tensor->setName (info.getName ());
  tensor->setTensorProperties (prop);
  tensor->setDataFormat (0);  // Dense format.
  tensor->setDataType (info.getDataType ());
  tensor->setDimensions (std::vector<uint32_t> (info.getDimensions (),
      info.getDimensions () + info.getRank ()));
  tensor->setIsDynamicDimensions (std::vector<bool> (info.getRank (), false));

  return tensor;
}

// Populate a per-output descriptor and its bindable tensor from the metadata.
static gboolean
fill_output (GstMLQairtEngine * engine, GstMLQairtOutput * output,
    qairt::SystemContextTensorInfo * info)
{
  output->tensor = make_tensor (engine, *info, FALSE);
  output->type = info->getDataType ();
  output->name = info->getName ();
  output->quantized = FALSE;
  output->scale = 1.0f;
  output->offset = 0;

  const qairt::SystemContextQuantizationInfo &qinfo = info->getQuantInfo ();

  if (qinfo.getQuantizationType () ==
      qairt::SystemContextQuantInfoType::QAIRT_QUANTIZATION_INFO_SCALE_OFFSET) {
    const qairt::SystemContextScaleOffset &so = qinfo.getScaleOffset ();
    output->quantized = TRUE;
    output->scale = so.getScale ();
    output->offset = so.getOffset ();
    GST_DEBUG ("Output '%s' quantized: scale %f offset %d",
        output->name.c_str (), output->scale, output->offset);
  }

  return TRUE;
}

// Read a model file into a caller-owned buffer. Returns NULL on failure.
static gchar *
read_model_file (const gchar * filename, gsize * length)
{
  gchar *contents = NULL;
  GError *error = NULL;

  if (!g_file_get_contents (filename, &contents, length, &error)) {
    GST_ERROR ("Failed to read model file '%s': %s!", filename,
        GST_STR_NULL (error ? error->message : NULL));
    g_clear_error (&error);
    return NULL;
  }

  return contents;
}

// Compose the graphs contained in a '.dlc' model onto the backend context.
static gboolean
setup_dlc_graphs (GstMLQairtEngine * engine, const gchar * filename)
{
  std::vector<qairt::SystemDlcGraphConfigInfo> configs;

  engine->dlc =
      engine->sysapi->makeShared<qairt::SystemDlc> (std::string (filename));

  engine->graphinfoset = std::make_shared<qairt::SystemContextGraphInfoSet> (
      engine->dlc->composeGraphs (configs, *engine->backend, *engine->context,
          *engine->api, *engine->sysapi));

  engine->graphinfos = std::move (engine->graphinfoset->getGraphInfos ());

  if (engine->graphinfos.empty ()) {
    GST_ERROR ("Model contains no graphs!");
    return FALSE;
  }

  engine->graph =
      engine->context->retrieveGraph (engine->graphinfos[0].getGraphName ());
  engine->graph->finalize ();

  return TRUE;
}

// Create the context from a cached context '.bin' binary and retrieve its
// graphs. The binary bytes must remain valid until the context is created.
static gboolean
setup_binary_graphs (GstMLQairtEngine * engine, const gchar * filename)
{
  gsize length = 0;
  gchar *contents = read_model_file (filename, &length);

  if (contents == NULL)
    return FALSE;

  if (length == 0) {
    GST_ERROR ("Model file '%s' is empty!", filename);
    g_free (contents);
    return FALSE;
  }

  qairt::ContextBinaryBuffer binbuffer =
      engine->api->make<qairt::ContextBinaryBuffer> ();
  binbuffer.setData (reinterpret_cast<void *> (contents));
  binbuffer.setSize (static_cast<uint64_t> (length));

  // The system context must outlive access to the extracted graph metadata.
  engine->syscontext = engine->sysapi->makeShared<qairt::SystemContext> ();
  qairt::SystemContextBinaryInfo bininfo =
      engine->syscontext->getBinaryInfo (binbuffer.getData (),
          binbuffer.getSize ());

  engine->context = std::make_shared<qairt::Context> (
      engine->backend->createContextFromBinary (std::nullopt, std::nullopt,
          binbuffer));

  std::vector<qairt::SystemContextGraphInfo> &graphs = bininfo.getGraphs ();
  engine->graphinfos.clear ();
  engine->graphinfos.reserve (graphs.size ());

  for (qairt::SystemContextGraphInfo &graph : graphs)
    engine->graphinfos.emplace_back (std::move (graph));

  g_free (contents);

  if (engine->graphinfos.empty ()) {
    GST_ERROR ("Model contains no graphs!");
    return FALSE;
  }

  engine->graph =
      engine->context->retrieveGraph (engine->graphinfos[0].getGraphName ());

  return TRUE;
}

GstMLQairtEngine *
gst_ml_qairt_engine_new (GstMLQairtSettings * settings)
{
  GstMLQairtEngine *engine = NULL;
  gboolean success = FALSE;
  gboolean failed = FALSE;

  g_return_val_if_fail (settings != NULL, NULL);
  GST_ML_RETURN_VAL_IF_FAIL (settings->modelfile != NULL, NULL,
      "No model file name!");

  engine = new GstMLQairtEngine;
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  try {
    const gchar *backendlib = delegate_to_backend_lib (settings->delegate);

    // Load the backend and system libraries.
    GST_DEBUG ("Loading QAIRT backend '%s'", backendlib);
    engine->api = std::make_shared<qairt::Api> (backendlib);
    engine->sysapi =
        std::make_shared<qairt::SystemApi> (GST_ML_QAIRT_SYSTEM_LIB);

    GST_DEBUG ("Loaded QAIRT backend '%s'!", backendlib);

    // Set up logging routed to the GStreamer debug system category.
    engine->log = engine->api->makeShared<qairt::Log> (
        [](const char *fmt, QairtLog_Level_t level, uint64_t timestamp,
            void *userdata, va_list argp) {
          (void) level; (void) timestamp; (void) userdata;
          if (G_LIKELY (GST_LEVEL_LOG > _gst_debug_min))
            return;
          gst_debug_log_valist (gst_ml_qairt_engine_debug_category (),
              GST_LEVEL_LOG, __FILE__, GST_FUNCTION, __LINE__, NULL, fmt, argp);
        },
        static_cast<QairtLog_Level_t> (qairt::LogLevel::Warn));

    GST_DEBUG ("Creating QAIRT backend handle");
    engine->backend = engine->api->makeShared<qairt::Backend> (*engine->log);

    // '.dlc' containers are composed into a fresh context; anything else is
    // treated as a cached context binary.
    std::filesystem::path modelpath (settings->modelfile);

    if (modelpath.extension () == ".dlc") {
      // Create the context, applying the priority configuration. Some backends
      // (e.g. CPU) do not support the priority context-config option and reject
      // createContext() with QNN_COMMON_ERROR_NOT_SUPPORTED; fall back to a
      // default context in that case rather than failing engine creation.
      try {
        qairt::ContextConfiguration config =
            engine->api->make<qairt::ContextConfiguration> ();
        config.setPriority (to_priority (settings->priority));

        GST_DEBUG ("Creating QAIRT context with priority configuration");
        engine->context = std::make_shared<qairt::Context> (
            engine->backend->createContext (config));
      } catch (const std::exception &e) {
        GST_WARNING ("Context creation with priority config failed (%s); "
            "retrying with default configuration", e.what ());
        engine->context = std::make_shared<qairt::Context> (
            engine->backend->createContext ());
      }

      GST_DEBUG ("Composing graphs from DLC");
      success = setup_dlc_graphs (engine, settings->modelfile);
    } else {
      GST_DEBUG ("Creating context from cached binary");
      success = setup_binary_graphs (engine, settings->modelfile);
    }

    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, NULL,
        gst_ml_qairt_engine_free (engine), "Failed to load model '%s'!",
        settings->modelfile);

    GST_DEBUG ("Loaded model file '%s'!", settings->modelfile);

    if (engine->graphinfos.size () > 1)
      GST_WARNING ("Multiple graphs detected! Only the first will be executed.");

    qairt::SystemContextGraphInfo &ginfo = engine->graphinfos[0];
    std::vector<qairt::SystemContextTensorInfo> &intensors =
        ginfo.getGraphInputs ();
    std::vector<qairt::SystemContextTensorInfo> &outtensors =
        ginfo.getGraphOutputs ();

    // Fill input ML info and build the input tensors in graph order.
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (
        intensors.size () <= GST_ML_MAX_TENSORS, NULL,
        gst_ml_qairt_engine_free (engine),
        "Model has %zu inputs, maximum is %u!", intensors.size (),
        GST_ML_MAX_TENSORS);

    for (guint idx = 0; idx < intensors.size (); idx++) {
      qairt::SystemContextTensorInfo &tensor = intensors[idx];

      GST_DEBUG ("Input tensor[%u] name: %s", idx, tensor.getName ().c_str ());

      if (idx == 0)
        GST_ML_INFO_TYPE (engine->ininfo) =
            qairt_to_ml_type (tensor.getDataType ());

      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (
          fill_tensor_dims (engine->ininfo, idx, tensor.getRank (),
              tensor.getDimensions ()), NULL,
          gst_ml_qairt_engine_free (engine), "Invalid input tensor %u!", idx);

      engine->ininfo->n_tensors++;
      engine->inputs.push_back (make_tensor (engine, tensor, TRUE));
    }

    GST_DEBUG ("Number of input tensors: %u",
        GST_ML_INFO_N_TENSORS (engine->ininfo));
    GST_DEBUG ("Input tensors type: %s",
        gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->ininfo)));

    // Build the ordered output list, honoring the optional user-specified names.
    if (settings->outputs != NULL) {
      guint length = g_list_length (settings->outputs);

      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (length <= GST_ML_MAX_TENSORS, NULL,
          gst_ml_qairt_engine_free (engine),
          "Requested %u outputs, maximum is %u!", length, GST_ML_MAX_TENSORS);

      for (guint idx = 0; idx < length; idx++) {
        const gchar *name = (const gchar *) g_list_nth_data (settings->outputs,
            idx);
        gboolean found = FALSE;

        for (guint num = 0; num < outtensors.size (); num++) {
          if (outtensors[num].getName () == name) {
            GstMLQairtOutput output;
            fill_output (engine, &output, &outtensors[num]);
            engine->outputs.push_back (std::move (output));
            found = TRUE;
            break;
          }
        }

        GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (found, NULL,
            gst_ml_qairt_engine_free (engine),
            "Output tensor name '%s' not found in graph!", name);
      }
    } else {
      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (
          outtensors.size () <= GST_ML_MAX_TENSORS, NULL,
          gst_ml_qairt_engine_free (engine),
          "Model has %zu outputs, maximum is %u!", outtensors.size (),
          GST_ML_MAX_TENSORS);

      for (guint idx = 0; idx < outtensors.size (); idx++) {
        GstMLQairtOutput output;
        fill_output (engine, &output, &outtensors[idx]);
        engine->outputs.push_back (std::move (output));
      }
    }

    // Output tensors are always dequantized to float for downstream elements.
    GST_ML_INFO_TYPE (engine->outinfo) = GST_ML_TYPE_FLOAT32;

    for (guint idx = 0; idx < engine->outputs.size (); idx++) {
      const std::vector<uint32_t> &dims =
          engine->outputs[idx].tensor->getDimensions ();

      GST_DEBUG ("Output tensor[%u] name: %s", idx,
          engine->outputs[idx].name.c_str ());

      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (
          fill_tensor_dims (engine->outinfo, idx, dims.size (), dims.data ()),
          NULL, gst_ml_qairt_engine_free (engine),
          "Invalid output tensor %u!", idx);

      engine->outinfo->n_tensors++;
    }

    GST_DEBUG ("Number of output tensors: %u",
        GST_ML_INFO_N_TENSORS (engine->outinfo));
    GST_DEBUG ("Output tensors type: %s",
        gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));
  } catch (const std::exception &e) {
    GST_ERROR ("Failed to create QAIRT engine: %s", e.what ());
    // Defer teardown until the exception object has been fully destroyed.
    // Freeing here destroys the qairt::Api, which dlclose()s the backend
    // library; but the in-flight exception was allocated by that library, so
    // unloading it now leaves the exception's cleanup code unmapped and the
    // handler crashes on exit. Fall through and free below instead.
    failed = TRUE;
  }

  if (failed) {
    gst_ml_qairt_engine_free (engine);
    return NULL;
  }

  GST_INFO ("Created MLE QAIRT engine: %p", engine);
  return engine;
}

void
gst_ml_qairt_engine_free (GstMLQairtEngine * engine)
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

  GST_INFO ("Destroyed MLE QAIRT engine: %p", engine);
  delete engine;
}

const GstMLInfo *
gst_ml_qairt_engine_get_input_info (GstMLQairtEngine * engine)
{
  return (engine == NULL) ? NULL : engine->ininfo;
}

const GstMLInfo *
gst_ml_qairt_engine_get_output_info (GstMLQairtEngine * engine)
{
  return (engine == NULL) ? NULL : engine->outinfo;
}

// Dequantize/convert a single native output tensor into the float output block.
// The native data is read from and the float result written to the same buffer;
// iterating from the last element backwards keeps the in-place expansion safe as
// sizeof(float) is greater than or equal to the size of the native element.
static void
convert_to_float (GstMLFrame * outframe, guint idx,
    const GstMLQairtOutput * output)
{
  gfloat *dst = reinterpret_cast<gfloat *> (GST_ML_FRAME_BLOCK_DATA (outframe,
      idx));
  gsize n_elements = gst_ml_info_tensor_size (&(outframe->info), idx) /
      gst_ml_type_get_size (GST_ML_FRAME_TYPE (outframe));
  gfloat scale = output->quantized ? output->scale : 1.0f;
  gint32 offset = output->quantized ? output->offset : 0;

  switch (output->type) {
    case qairt::DataType::Float32:
      // Native type already matches, no conversion needed.
      break;
    case qairt::DataType::UFixedPoint8:
    case qairt::DataType::UInt8:
    case qairt::DataType::Bool8:
    {
      uint8_t *src = reinterpret_cast<uint8_t *> (dst);
      for (gint i = (gint) n_elements - 1; i >= 0; i--)
        dst[i] = ((gfloat) src[i] + offset) * scale;
      break;
    }
    case qairt::DataType::SFixedPoint8:
    case qairt::DataType::Int8:
    {
      int8_t *src = reinterpret_cast<int8_t *> (dst);
      for (gint i = (gint) n_elements - 1; i >= 0; i--)
        dst[i] = ((gfloat) src[i] + offset) * scale;
      break;
    }
    case qairt::DataType::UFixedPoint16:
    case qairt::DataType::UInt16:
    {
      uint16_t *src = reinterpret_cast<uint16_t *> (dst);
      for (gint i = (gint) n_elements - 1; i >= 0; i--)
        dst[i] = ((gfloat) src[i] + offset) * scale;
      break;
    }
    case qairt::DataType::SFixedPoint16:
    case qairt::DataType::Int16:
    {
      int16_t *src = reinterpret_cast<int16_t *> (dst);
      for (gint i = (gint) n_elements - 1; i >= 0; i--)
        dst[i] = ((gfloat) src[i] + offset) * scale;
      break;
    }
    case qairt::DataType::UFixedPoint32:
    case qairt::DataType::UInt32:
    {
      uint32_t *src = reinterpret_cast<uint32_t *> (dst);
      for (gint i = (gint) n_elements - 1; i >= 0; i--)
        dst[i] = ((gfloat) src[i] + offset) * scale;
      break;
    }
    case qairt::DataType::SFixedPoint32:
    case qairt::DataType::Int32:
    {
      int32_t *src = reinterpret_cast<int32_t *> (dst);
      for (gint i = (gint) n_elements - 1; i >= 0; i--)
        dst[i] = ((gfloat) src[i] + offset) * scale;
      break;
    }
    default:
      GST_ERROR ("Output data type %d not supported for conversion!",
          static_cast<int> (output->type));
      break;
  }
}

gboolean
gst_ml_qairt_engine_execute (GstMLQairtEngine * engine, GstMLFrame * inframe,
    GstMLFrame * outframe)
{
  GstMLTensorMeta *mlmeta = NULL;

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

  try {
    // Bind the mapped GStreamer memory blocks to the QAIRT tensors' client
    // buffers, then execute the graph.
    for (guint idx = 0; idx < engine->inputs.size (); idx++) {
      qairt::ClientBuffer &cb = engine->inputs[idx]->getClientBuffer ();
      cb.setData (GST_ML_FRAME_BLOCK_DATA (inframe, idx));
      cb.setDataSize (static_cast<uint32_t> (
          gst_ml_info_tensor_size (&(inframe->info), idx)));
    }

    for (guint idx = 0; idx < engine->outputs.size (); idx++) {
      qairt::ClientBuffer &cb = engine->outputs[idx].tensor->getClientBuffer ();
      cb.setData (GST_ML_FRAME_BLOCK_DATA (outframe, idx));
      cb.setDataSize (static_cast<uint32_t> (
          gst_ml_info_tensor_size (&(outframe->info), idx)));
    }

    std::vector<std::shared_ptr<qairt::Tensor>> outtensors;
    outtensors.reserve (engine->outputs.size ());
    for (GstMLQairtOutput &output : engine->outputs)
      outtensors.push_back (output.tensor);

    engine->graph->execute (engine->inputs, outtensors);

    // Convert the native output tensors to float and stamp the tensor names.
    for (guint idx = 0; idx < engine->outputs.size (); idx++) {
      convert_to_float (outframe, idx, &engine->outputs[idx]);

      mlmeta = gst_buffer_get_ml_tensor_meta_id (outframe->buffer, idx);
      if (mlmeta != NULL)
        mlmeta->name = g_quark_from_string (engine->outputs[idx].name.c_str ());
    }
  } catch (const std::exception &e) {
    GST_ERROR ("Model execution failed: %s", e.what ());
    return FALSE;
  }

  return TRUE;
}
