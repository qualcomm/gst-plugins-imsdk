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
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#define DEFAULT_OPT_DELEGATE GST_ML_SNPE_DELEGATE_NONE

#define GET_OPT_MODEL(s) get_opt_string (s, \
    GST_ML_SNPE_ENGINE_OPT_MODEL)
#define GET_OPT_DELEGATE(s) get_opt_enum (s, \
    GST_ML_SNPE_ENGINE_OPT_DELEGATE, GST_TYPE_ML_SNPE_DELEGATE, \
    DEFAULT_OPT_DELEGATE)
#define GET_OPT_LAYERS(s) get_opt_list (s, \
    GST_ML_SNPE_ENGINE_OPT_LAYERS)

#define GST_CAT_DEFAULT gst_ml_snpe_engine_debug_category()

struct _GstMLSnpeEngine
{
  GstMLInfo *ininfo;
  GstMLInfo *outinfo;

  GstStructure *settings;

  // SNPE container model.
  std::unique_ptr<zdl::DlContainer::IDlContainer> model;

  // SNPE model interpreter.
  std::unique_ptr<zdl::SNPE::SNPE> interpreter;

  // List with SNPE User Buffers.
  std::vector<std::unique_ptr<zdl::DlSystem::IUserBuffer>> usrbuffers;

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

static const gchar *
get_opt_string (GstStructure * settings, const gchar * opt)
{
  return gst_structure_get_string (settings, opt);
}

static gint
get_opt_enum (GstStructure * settings, const gchar * opt, GType type, gint dval)
{
  gint result;
  return gst_structure_get_enum (settings, opt, type, &result) ?
    result : dval;
}

static GList *
get_opt_list (GstStructure * settings, const gchar * opt)
{
  GList *list = NULL;
  const GValue *value = NULL;
  guint idx = 0;

  if ((value = gst_structure_get_value (settings, opt)) == NULL)
    return NULL;

  for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
    const gchar *layer = g_value_get_string (
        gst_value_array_get_value (value, idx));
    list = g_list_append (list, g_strdup (layer));
  }

  return list;
}

static GstMLType
snpe_to_ml_type (zdl::DlSystem::UserBufferEncoding::ElementType_t type)
{
  switch (type) {
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::FLOAT:
      return GST_ML_TYPE_FLOAT32;
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::UNSIGNED8BIT:
    case zdl::DlSystem::UserBufferEncoding::ElementType_t::TF8:
      return GST_ML_TYPE_UINT8;
    default:
      GST_ERROR ("Unsupported format %x!", static_cast<uint32_t>(type));
      break;
  }

  return GST_ML_TYPE_UNKNOWN;
}

GstMLSnpeEngine *
gst_ml_snpe_engine_new (GstStructure * settings)
{
  GstMLSnpeEngine *engine = NULL;
  GList *list = NULL;
  const gchar *filename = NULL;
  gint idx = 0, num = 0;

  engine = new GstMLSnpeEngine;
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  filename = GET_OPT_MODEL (engine->settings);
  GST_ML_RETURN_VAL_IF_FAIL (filename != NULL, NULL, "No model file name!");

  engine->model = zdl::DlContainer::IDlContainer::open(std::string(filename));
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->model, NULL,
      gst_ml_snpe_engine_free (engine), "Failed to load model file '%s'!",
      filename);

  GST_DEBUG ("Loaded model file '%s'!", filename);

  zdl::DlSystem::RuntimeList rtlist;

  switch (GET_OPT_DELEGATE (engine->settings)) {
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
  zdl::DlSystem::StringList layers;

  list = GET_OPT_LAYERS (engine->settings);

  for (idx = 0; idx < g_list_length (list); idx++)
    layers.append ((const gchar *) g_list_nth_data (list, idx));

  g_list_free_full (list, (GDestroyNotify) g_free);

  builder.setOutputLayers(layers).setRuntimeProcessorOrder(rtlist);
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

    engine->ininfo->type =
        snpe_to_ml_type ((*optattributes)->getEncodingType());

    const zdl::DlSystem::TensorShape& shape = (*optattributes)->getDims();
    const zdl::DlSystem::Dimension *dimensions = shape.getDimensions();

    engine->ininfo->n_dimensions[idx] = shape.rank();

    for (num = 0; num < shape.rank(); ++num) {
      engine->ininfo->tensors[idx][num] = dimensions[num];
      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          engine->ininfo->tensors[idx][num]);
    }

    engine->ininfo->n_tensors++;

    std::vector<zdl::DlSystem::Dimension> strides(shape.rank());
    strides[shape.rank() - 1] = gst_ml_type_get_size (engine->ininfo->type);

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (shape.rank() - 1); num > 0; num--)
      strides[num - 1] = dimensions[num] * strides[num];

    zdl::DlSystem::UserBufferEncoding *encoding = (*optattributes)->getEncoding();
    size_t size = gst_ml_info_tensor_size (engine->ininfo, idx);

    // Empty User Buffer which will later be set via setBufferAddress API.
    std::unique_ptr<zdl::DlSystem::IUserBuffer> usrbuffer =
        factory.createUserBuffer(NULL, size, strides, encoding);

    engine->usrbuffers.push_back(std::move (usrbuffer));
    engine->inputs.add(name, engine->usrbuffers.back().get());
  }

  GST_DEBUG ("Number of input tensors: %u", engine->ininfo->n_tensors);
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (engine->ininfo->type));

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

    engine->outinfo->type =
        snpe_to_ml_type ((*optattributes)->getEncodingType());

    const zdl::DlSystem::TensorShape& shape = (*optattributes)->getDims();
    const zdl::DlSystem::Dimension *dimensions = shape.getDimensions();

    engine->outinfo->n_dimensions[idx] = shape.rank();

    for (num = 0; num < shape.rank(); ++num) {
      engine->outinfo->tensors[idx][num] = dimensions[num];
      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          engine->outinfo->tensors[idx][num]);
    }

    engine->outinfo->n_tensors++;

    std::vector<zdl::DlSystem::Dimension> strides(shape.rank());
    strides[shape.rank() - 1] = gst_ml_type_get_size (engine->outinfo->type);

    // Total number of bytes between elements in each dimension.
    // Float tensor with dimensions [4, 3, 2] would have strides of [24, 8, 4].
    for (num = (shape.rank() - 1); num > 0; num--)
      strides[num - 1] = dimensions[num] * strides[num];

    zdl::DlSystem::UserBufferEncoding *encoding = (*optattributes)->getEncoding();
    size_t size = gst_ml_info_tensor_size (engine->outinfo, idx);

    // Empty User Buffer which will later be set via setBufferAddress API.
    std::unique_ptr<zdl::DlSystem::IUserBuffer> usrbuffer =
        factory.createUserBuffer(NULL, size, strides, encoding);

    engine->usrbuffers.push_back(std::move (usrbuffer));
    engine->outputs.add(name, engine->usrbuffers.back().get());
  }

  GST_DEBUG ("Number of output tensors: %u", engine->outinfo->n_tensors);
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (engine->outinfo->type));

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

  if (engine->settings != NULL) {
    gst_structure_free (engine->settings);
    engine->settings = NULL;
  }

  GST_INFO ("Destroyed MLE SNPE engine: %p", engine);
  delete engine;
}

const GstMLInfo *
gst_ml_snpe_engine_get_input_info  (GstMLSnpeEngine * engine)
{
  return (engine == NULL) ? NULL : engine->ininfo;
}

const GstMLInfo *
gst_ml_snpe_engine_get_output_info  (GstMLSnpeEngine * engine)
{
  return (engine == NULL) ? NULL : engine->outinfo;
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

  zdl::DlSystem::Optional <zdl::DlSystem::StringList> optnames =
      engine->interpreter->getInputTensorNames();

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    zdl::DlSystem::IUserBuffer *usrbuffer =
        engine->inputs.getUserBuffer((*optnames).at(idx));
    usrbuffer->setBufferAddress(GST_ML_FRAME_BLOCK_DATA (inframe, idx));
  }

  optnames = engine->interpreter->getOutputTensorNames();

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    zdl::DlSystem::IUserBuffer *usrbuffer =
        engine->outputs.getUserBuffer((*optnames).at(idx));
    usrbuffer->setBufferAddress(GST_ML_FRAME_BLOCK_DATA (outframe, idx));
  }

  if (!(success = engine->interpreter->execute(engine->inputs, engine->outputs)))
    GST_ERROR ("Model execution failed!");

  return success;
}
