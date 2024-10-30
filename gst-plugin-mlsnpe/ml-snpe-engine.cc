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
 * Changes from Qualcomm Innovation Center are provided under the following license:
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

#include <algorithm>
#include <map>
#include <gst/ml/gstmlmeta.h>

#include <DlContainer/IDlContainer.hpp>
#include <SNPE/SNPEFactory.hpp>
#include <SNPE/SNPEBuilder.hpp>
#include <DlSystem/IUserBufferFactory.hpp>

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

#define GST_CAT_DEFAULT gst_ml_snpe_engine_debug_category()

struct _GstMLSnpeEngine
{
  GstMLInfo *ininfo;
  GstMLInfo *outinfo;

  // SNPE container model.
  std::unique_ptr<zdl::DlContainer::IDlContainer> model;

  // SNPE model interpreter.
  std::unique_ptr<zdl::SNPE::SNPE> interpreter;

  // List with SNPE User Buffers.
  std::map<std::string, std::unique_ptr<zdl::DlSystem::IUserBuffer>> usrbuffers;

  // Map between SNPE input tensor names and corresponding User Buffer.
  zdl::DlSystem::UserBufferMap inputs;
  // Map between SNPE output tensor names and corresponding User Buffer.
  zdl::DlSystem::UserBufferMap outputs;
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
snpe_to_ml_type (zdl::DlSystem::UserBufferEncoding::ElementType_t type)
{
  switch (type) {
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::FLOAT16:
      return GST_ML_TYPE_FLOAT16;
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::FLOAT:
      return GST_ML_TYPE_FLOAT32;
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::INT8:
      return GST_ML_TYPE_INT8;
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::UNSIGNED8BIT:
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::TF8:
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::UINT8:
      return GST_ML_TYPE_UINT8;
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::INT32:
      return GST_ML_TYPE_INT32;
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::UINT32:
      return GST_ML_TYPE_UINT32;
    default:
      GST_ERROR ("Unsupported format %x!", static_cast<uint32_t>(type));
      break;
  }

  return GST_ML_TYPE_UNKNOWN;
}

GstMLSnpeEngine *
gst_ml_snpe_engine_new (GstMLSnpeSettings * settings)
{
  GstMLSnpeEngine *engine = NULL;
  gint idx = 0, num = 0;

  engine = new GstMLSnpeEngine;
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  GST_ML_RETURN_VAL_IF_FAIL (settings->modelfile != NULL, NULL,
      "No model file name!");

  engine->model =
      zdl::DlContainer::IDlContainer::open(std::string(settings->modelfile));
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->model, NULL,
      gst_ml_snpe_engine_free (engine), "Failed to load model file '%s'!",
      settings->modelfile);

  GST_DEBUG ("Loaded model file '%s'!", settings->modelfile);

  zdl::DlSystem::RuntimeList rtlist;

  switch (settings->delegate) {
    case GST_ML_SNPE_DELEGATE_DSP:
      rtlist.add(zdl::DlSystem::Runtime_t::DSP);
      break;
    case GST_ML_SNPE_DELEGATE_GPU:
      rtlist.add(zdl::DlSystem::Runtime_t::GPU);
      break;
    case GST_ML_SNPE_DELEGATE_AIP:
      rtlist.add(zdl::DlSystem::Runtime_t::AIP_FIXED8_TF);
      break;
    default:
      // Only CPU will be used to run processing.
      break;
  }

  rtlist.add(zdl::DlSystem::Runtime_t::CPU);
  zdl::DlSystem::StringList names = rtlist.getRuntimeListNames();

  GST_INFO ("Runtime delegates in order of precedence: %s %s",
      names.at(0), (names.size() > 1) ? names.at(1) : "");

  zdl::SNPE::SNPEBuilder builder(engine->model.get());
  zdl::DlSystem::StringList outputs;

  switch (settings->perf_profile) {
    case GST_ML_SNPE_PERF_PROFILE_DEFAULT:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::DEFAULT);
      break;
    case GST_ML_SNPE_PERF_PROFILE_BALANCED:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::BALANCED);
      break;
    case GST_ML_SNPE_PERF_PROFILE_HIGH_PERFORMANCE:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE);
      break;
    case GST_ML_SNPE_PERF_PROFILE_POWER_SAVER:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::POWER_SAVER);
      break;
    case GST_ML_SNPE_PERF_PROFILE_SYSTEM_SETTINGS:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::SYSTEM_SETTINGS);
      break;
    case GST_ML_SNPE_PERF_PROFILE_SUSTAINED_HIGH_PERFORMANCE:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::SUSTAINED_HIGH_PERFORMANCE);
      break;
    case GST_ML_SNPE_PERF_PROFILE_BURST:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::BURST);
      break;
    case GST_ML_SNPE_PERF_PROFILE_LOW_POWER_SAVER:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::LOW_POWER_SAVER);
      break;
    case GST_ML_SNPE_PERF_PROFILE_HIGH_POWER_SAVER:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::HIGH_POWER_SAVER);
      break;
    case GST_ML_SNPE_PERF_PROFILE_LOW_BALANCED:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::LOW_BALANCED);
      break;
    default:
      builder.setPerformanceProfile (
          zdl::DlSystem::PerformanceProfile_t::DEFAULT);
      break;
  }

  switch (settings->profiling_level) {
    case GST_ML_SNPE_PROFILING_LEVEL_OFF:
      builder.setProfilingLevel(zdl::DlSystem::ProfilingLevel_t::OFF);
      break;
    case GST_ML_SNPE_PROFILING_LEVEL_BASIC:
      builder.setProfilingLevel(zdl::DlSystem::ProfilingLevel_t::BASIC);
      break;
    case GST_ML_SNPE_PROFILING_LEVEL_DETAILED:
      builder.setProfilingLevel(zdl::DlSystem::ProfilingLevel_t::DETAILED);
      break;
    case GST_ML_SNPE_PROFILING_LEVEL_MODERATE:
      builder.setProfilingLevel(zdl::DlSystem::ProfilingLevel_t::MODERATE);
      break;
    default:
      builder.setProfilingLevel(zdl::DlSystem::ProfilingLevel_t::OFF);
      break;
  }

  switch (settings->exec_priority) {
    case GST_ML_SNPE_EXEC_PRIORITY_NORMAL:
      builder.setExecutionPriorityHint(
          zdl::DlSystem::ExecutionPriorityHint_t::NORMAL);
      break;
    case GST_ML_SNPE_EXEC_PRIORITY_HIGH:
      builder.setExecutionPriorityHint(
          zdl::DlSystem::ExecutionPriorityHint_t::HIGH);
      break;
    case GST_ML_SNPE_EXEC_PRIORITY_LOW:
      builder.setExecutionPriorityHint(
          zdl::DlSystem::ExecutionPriorityHint_t::LOW);
      break;
    default:
      builder.setExecutionPriorityHint(
          zdl::DlSystem::ExecutionPriorityHint_t::NORMAL);
      break;
  }

  for (idx = 0; idx < g_list_length (settings->outputs); idx++)
    outputs.append (
        (const gchar *) g_list_nth_data (settings->outputs, idx));

  if (settings->is_tensor)
    builder.setOutputTensors(outputs).setRuntimeProcessorOrder(rtlist);
  else
    builder.setOutputLayers(outputs).setRuntimeProcessorOrder(rtlist);
  builder.setUseUserSuppliedBuffers(true);

  engine->interpreter = builder.build();
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->interpreter, NULL,
      gst_ml_snpe_engine_free (engine), "Failed to construct interpreter!");

  // Retrive reference to the User Buffer factory to create buffer placeholders.
  zdl::DlSystem::IUserBufferFactory& factory =
      zdl::SNPE::SNPEFactory::getUserBufferFactory();

  // Fill input ML info.
  zdl::DlSystem::Optional <zdl::DlSystem::StringList> optnames =
      engine->interpreter->getInputTensorNames();
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (optnames, NULL,
      gst_ml_snpe_engine_free (engine), "Failed to retrieve input tensors!");

  for (idx = 0; idx < (*optnames).size(); idx++) {
    zdl::DlSystem::Optional<zdl::DlSystem::IBufferAttributes*> optattributes;
    const char *name = (*optnames).at(idx);
    GST_DEBUG ("Input tensor[%u] name: %s", idx, name);

    optattributes = engine->interpreter->getInputOutputBufferAttributes(name);
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (optattributes, NULL,
        gst_ml_snpe_engine_free (engine), "Failed to get trensor attributes!");

    GST_ML_INFO_TYPE (engine->ininfo) =
        snpe_to_ml_type ((*optattributes)->getEncodingType());

    const zdl::DlSystem::TensorShape& shape = (*optattributes)->getDims();
    const zdl::DlSystem::Dimension *dimensions = shape.getDimensions();

    GST_ML_INFO_N_DIMENSIONS (engine->ininfo, idx) = shape.rank();

    for (num = 0; num < shape.rank(); ++num) {
      GST_ML_INFO_TENSOR_DIM (engine->ininfo, idx, num) = dimensions[num];
      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          GST_ML_INFO_TENSOR_DIM (engine->ininfo, idx, num));
    }

    engine->ininfo->n_tensors++;

    std::vector<zdl::DlSystem::Dimension> strides(shape.rank());
    strides[shape.rank() - 1] =
        gst_ml_type_get_size (GST_ML_INFO_TYPE (engine->ininfo));

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (shape.rank() - 1); num > 0; num--)
      strides[num - 1] = dimensions[num] * strides[num];

    zdl::DlSystem::UserBufferEncoding *encoding = (*optattributes)->getEncoding();
    size_t size = gst_ml_info_tensor_size (engine->ininfo, idx);

    // Empty User Buffer which will later be set via setBufferAddress API.
    std::unique_ptr<zdl::DlSystem::IUserBuffer> usrbuffer =
        factory.createUserBuffer(NULL, size, strides, encoding);

    engine->usrbuffers.emplace(name, std::move (usrbuffer));
    engine->inputs.add(name, engine->usrbuffers[name].get());
  }

  GST_DEBUG ("Number of input tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->ininfo));
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->ininfo)));

  // Fill output ML info.
  optnames = engine->interpreter->getOutputTensorNames();
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (optnames, NULL,
      gst_ml_snpe_engine_free (engine), "Failed to retrieve output tensors!");

  for (idx = 0; idx < (*optnames).size(); idx++) {
    zdl::DlSystem::Optional<zdl::DlSystem::IBufferAttributes*> optattributes;
    const char *name = (*optnames).at(idx);
    GST_DEBUG ("Output tensor[%u] name: %s", idx, name);

    optattributes = engine->interpreter->getInputOutputBufferAttributes(name);
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (optattributes, NULL,
        gst_ml_snpe_engine_free (engine), "Failed to get trensor attributes!");

    GST_ML_INFO_TYPE (engine->outinfo) =
        snpe_to_ml_type ((*optattributes)->getEncodingType());

    const zdl::DlSystem::TensorShape& shape = (*optattributes)->getDims();
    const zdl::DlSystem::Dimension *dimensions = shape.getDimensions();

    GST_ML_INFO_N_DIMENSIONS (engine->outinfo, idx) = shape.rank();

    for (num = 0; num < shape.rank(); ++num) {
      GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num) = dimensions[num];
      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num));
    }

    engine->outinfo->n_tensors++;

    std::vector<zdl::DlSystem::Dimension> strides(shape.rank());
    strides[shape.rank() - 1] =
        gst_ml_type_get_size (GST_ML_INFO_TYPE (engine->outinfo));

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (shape.rank() - 1); num > 0; num--)
      strides[num - 1] = dimensions[num] * strides[num];

    zdl::DlSystem::UserBufferEncoding *encoding = (*optattributes)->getEncoding();
    size_t size = gst_ml_info_tensor_size (engine->outinfo, idx);

    GST_DEBUG ("Output tensor[%u] size: %u", idx, size);
  }

  GST_DEBUG ("Number of output tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->outinfo));
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));

  GST_INFO ("Created MLE SNPE engine: %p", engine);
  return engine;
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

  GST_INFO ("Destroyed MLE SNPE engine: %p", engine);
  delete engine;
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
  guint idx = 0, num = 0;

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
  GstMLInfo info;
  gint idx = 0, num = 0;

  g_return_val_if_fail (engine != NULL, FALSE);

  GST_ML_RETURN_VAL_IF_FAIL (gst_ml_info_from_caps (&info, caps), FALSE,
      "Failed to extract ML info from caps!");

  if (gst_ml_info_is_equal (&info, engine->outinfo))
    return TRUE;

  // Retrive reference to the User Buffer factory to create buffer placeholders.
  zdl::DlSystem::IUserBufferFactory& factory =
      zdl::SNPE::SNPEFactory::getUserBufferFactory();

  zdl::DlSystem::Optional <zdl::DlSystem::StringList> optnames =
      engine->interpreter->getOutputTensorNames();
  GST_ML_RETURN_VAL_IF_FAIL (optnames, FALSE,
      "Failed to retrieve output tensor names!");

  // Updated number of tensors must be the same.
  GST_ML_RETURN_VAL_IF_FAIL ((*optnames).size() == GST_ML_INFO_N_TENSORS (&info),
      FALSE, "Updated info has invalid number of tensors!");

  for (idx = 0; idx < (*optnames).size(); idx++) {
    zdl::DlSystem::Optional<zdl::DlSystem::IBufferAttributes*> optattributes;
    const char *name = (*optnames).at(idx);
    GST_DEBUG ("Output tensor[%u] name: %s", idx, name);

    optattributes = engine->interpreter->getInputOutputBufferAttributes(name);
    GST_ML_RETURN_VAL_IF_FAIL (optattributes, FALSE,
        "Failed to get trensor attributes!");

    const zdl::DlSystem::TensorShape& shape = (*optattributes)->getDims();
    const zdl::DlSystem::Dimension *dimensions = shape.getDimensions();

    // The updated number of tensor dimensions must be the same.
    GST_ML_RETURN_VAL_IF_FAIL (GST_ML_INFO_N_DIMENSIONS (&info, idx) == shape.rank(),
        FALSE, "Updated tensor %d has invalid number of dimensions!", idx);

    for (num = 0; num < shape.rank(); ++num) {
      // Update only dimensions with value 0, all others must be the same.
      GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num) = (dimensions[num] == 0) ?
          GST_ML_INFO_TENSOR_DIM (&info, idx, num) : dimensions[num];

      GST_ML_RETURN_VAL_IF_FAIL (GST_ML_INFO_TENSOR_DIM (&info, idx, num) ==
          GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num), FALSE,
          "Updated tensor %d has invalid number of dimension %d!", idx, num);

      GST_DEBUG ("Output tensor[%d] Dimension[%d]: %u", idx, num,
          GST_ML_INFO_TENSOR_DIM (engine->outinfo, idx, num));
    }

    std::vector<zdl::DlSystem::Dimension> strides(shape.rank());

    // Use the updated tensor type for teh stride calculations.
    strides[shape.rank() - 1] = gst_ml_type_get_size (GST_ML_INFO_TYPE (&info));

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (shape.rank() - 1); num > 0; num--)
      strides[num - 1] = engine->outinfo->tensors[idx][num] * strides[num];

    zdl::DlSystem::UserBufferEncoding *encoding = NULL;

    if (GST_ML_INFO_TYPE (&info) == GST_ML_INFO_TYPE (engine->outinfo))
      encoding = (*optattributes)->getEncoding();
    else if (GST_ML_INFO_TYPE (&info) == GST_ML_TYPE_FLOAT32)
      encoding = new zdl::DlSystem::UserBufferEncodingFloat();
    else if (GST_ML_INFO_TYPE (&info) == GST_ML_TYPE_UINT8)
      encoding = new zdl::DlSystem::UserBufferEncodingUnsigned8Bit();

    GST_ML_RETURN_VAL_IF_FAIL (encoding != NULL, FALSE,
        "Unsupported encoding for tensor %d!", idx);

    size_t size = gst_ml_info_tensor_size (&info, idx);

    GST_DEBUG ("Output tensor[%u] size: %u", idx, size);

    // Empty User Buffer which will later be set via setBufferAddress API.
    std::unique_ptr<zdl::DlSystem::IUserBuffer> usrbuffer =
        factory.createUserBuffer(NULL, size, strides, encoding);

    // Replace previous UserBuffer for this tensor.
    engine->usrbuffers[name] = std::move(usrbuffer);
    engine->outputs.add(name, engine->usrbuffers[name].get());
  }

  // Update the tensor type.
  GST_ML_INFO_TYPE (engine->outinfo) = GST_ML_INFO_TYPE (&info);

  GST_DEBUG ("Number of output tensors: %u",
      GST_ML_INFO_N_TENSORS (engine->outinfo));
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (GST_ML_INFO_TYPE (engine->outinfo)));

  return TRUE;
}

gboolean
gst_ml_snpe_engine_execute (GstMLSnpeEngine * engine,
    GstMLFrame * inframe, GstMLFrame * outframe)
{
  gboolean success = FALSE;
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

  const zdl::DlSystem::Optional <zdl::DlSystem::StringList> inoptnames =
      engine->interpreter->getInputTensorNames();

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    zdl::DlSystem::IUserBuffer *usrbuffer =
        engine->usrbuffers[(*inoptnames).at(idx)].get();
    usrbuffer->setBufferAddress(GST_ML_FRAME_BLOCK_DATA (inframe, idx));
  }

  const zdl::DlSystem::Optional <zdl::DlSystem::StringList> outoptnames =
      engine->interpreter->getOutputTensorNames();

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    zdl::DlSystem::IUserBuffer *usrbuffer =
        engine->usrbuffers[(*outoptnames).at(idx)].get();
    usrbuffer->setBufferAddress(GST_ML_FRAME_BLOCK_DATA (outframe, idx));
  }

  if (!(success = engine->interpreter->execute(engine->inputs, engine->outputs)))
    GST_ERROR ("Model execution failed!");

  return success;
}
