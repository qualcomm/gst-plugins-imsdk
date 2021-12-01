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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ml-tflite-engine.h"

#include <tensorflow/lite/model.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/tools/evaluation/utils.h>
#include <tensorflow/lite/experimental/delegates/hexagon/hexagon_delegate.h>

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

#define GST_CAT_DEFAULT gst_ml_tflite_engine_debug_category()

struct _GstMLTFLiteEngine
{
  GstMLInfo *ininfo;
  GstMLInfo *outinfo;

  GstStructure *settings;

  // TFLite flatbuffer model.
  // Raw pointer to c++ unique_ptr because struct is allocated via malloc.
  std::unique_ptr<tflite::FlatBufferModel> model;

  // TFLite model interpreter.
  // Raw pointer to c++ unique_ptr because struct is allocated via malloc.
  std::unique_ptr<tflite::Interpreter> interpreter;
};

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

GType
gst_ml_tflite_delegate_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_ML_TFLITE_DELEGATE_NONE,
        "No delegate, CPU is used for all operations", "none"
    },
    { GST_ML_TFLITE_DELEGATE_DSP,
        "Run the processing on the Hexagon DSP through the Android NN API",
        "nnapi-dsp"
    },
    { GST_ML_TFLITE_DELEGATE_NPU,
        "Run the processing on the NPU through the Android NN API", "nnapi-npu"
    },
    { GST_ML_TFLITE_DELEGATE_HEXAGON,
        "Run the processing directly on the Hexagon DSP", "hexagon"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
      gtype = g_enum_register_static ("GstMLTFLiteDelegate", variants);

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

GstMLTFLiteEngine *
gst_ml_tflite_engine_new (GstStructure * settings)
{
  GstMLTFLiteEngine *engine = NULL;
  const gchar *filename = NULL;
  gint idx = 0, num = 0, n_threads = 1;

  tflite::ops::builtin::BuiltinOpResolver resolver;

  engine = new GstMLTFLiteEngine;
  g_return_val_if_fail (engine != NULL, NULL);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  filename = GET_OPT_MODEL (engine->settings);
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (filename != NULL, NULL,
      gst_ml_tflite_engine_free (engine), "No model file name!");

  engine->model = tflite::FlatBufferModel::BuildFromFile (filename);
  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->model, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to load model file '%s'!",
      filename);

  GST_DEBUG ("Loaded model file '%s'!", filename);

  tflite::InterpreterBuilder builder (engine->model->GetModel(), resolver);
  builder (&(engine)->interpreter);

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->interpreter, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to construct interpreter!");

  switch (GET_OPT_DELEGATE (engine->settings)) {
    case GST_ML_TFLITE_DELEGATE_DSP:
    {
      tflite::StatefulNnApiDelegate::Options options;
      TfLiteStatus status = TfLiteStatus::kTfLiteOk;

      auto delegate = tflite::Interpreter::TfLiteDelegatePtr (
        new tflite::StatefulNnApiDelegate(options), [](TfLiteDelegate* dlg) {
          delete reinterpret_cast<tflite::StatefulNnApiDelegate*>(dlg);
        }
      );

      if (!delegate) {
        GST_WARNING ("Failed to create NN Framework delegate!");
        break;
      }

      status = engine->interpreter->ModifyGraphWithDelegate(delegate.get());
      if (status != TfLiteStatus::kTfLiteOk)
        GST_WARNING ("Failed to modify graph with NN Framework delegate!");

      break;
    }
    case GST_ML_TFLITE_DELEGATE_HEXAGON:
    {
      TfLiteHexagonDelegateOptions options = {};
      TfLiteStatus status = TfLiteStatus::kTfLiteOk;

      // Initialize the Hexagon unit.
      TfLiteHexagonInit();

      options.debug_level = 0;
      options.powersave_level = 0;
      options.print_graph_profile = false;
      options.print_graph_debug = false;

      auto delegate = tflite::Interpreter::TfLiteDelegatePtr (
        TfLiteHexagonDelegateCreate(&options), [](TfLiteDelegate* dlg) {
          TfLiteHexagonDelegateDelete(dlg);
          TfLiteHexagonTearDown();
        }
      );

      if (!delegate) {
        GST_WARNING ("Failed to create NN Framework delegate!");
        break;
      }

      status = engine->interpreter->ModifyGraphWithDelegate(delegate.get());
      if (status != TfLiteStatus::kTfLiteOk)
        GST_WARNING ("Failed to modify graph with HEXAGON delegate!");

      break;
    }
    case GST_ML_TFLITE_DELEGATE_NPU:
    {
      tflite::StatefulNnApiDelegate::Options options;
      TfLiteStatus status = TfLiteStatus::kTfLiteOk;

      // Set the higher ExecutionPreference bits so that the NPU is chosen.
      options.execution_preference = static_cast<
          tflite::StatefulNnApiDelegate::Options::ExecutionPreference>(0x00300000);

      auto delegate = tflite::Interpreter::TfLiteDelegatePtr (
        new tflite::StatefulNnApiDelegate(options), [](TfLiteDelegate* dlg) {
          delete reinterpret_cast<tflite::StatefulNnApiDelegate*>(dlg);
        }
      );

      if (!delegate) {
        GST_WARNING ("Failed to create NN Framework delegate!");
        break;
      }

      status = engine->interpreter->ModifyGraphWithDelegate(delegate.get());
      if (status != TfLiteStatus::kTfLiteOk)
        GST_WARNING ("Failed to modify graph with NPU delegate!");

      break;
    }
    default:
      GST_INFO ("No delegate will be used");
      break;
  }

  n_threads = GET_OPT_STHREADS (engine->settings);

  engine->interpreter->SetNumThreads(n_threads);
  GST_DEBUG ("Number of interpreter threads: %u", n_threads);

  GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (
      engine->interpreter->AllocateTensors() == kTfLiteOk, NULL,
      gst_ml_tflite_engine_free (engine), "Failed to allocate tensors!");

  engine->ininfo->n_tensors = engine->interpreter->inputs().size();
  engine->outinfo->n_tensors = engine->interpreter->outputs().size();

  idx = engine->interpreter->inputs()[0];

  switch (engine->interpreter->tensor(idx)->type) {
    case kTfLiteFloat32:
      engine->ininfo->type = GST_ML_TYPE_FLOAT32;
      break;
    case kTfLiteInt32:
      engine->ininfo->type = GST_ML_TYPE_INT32;
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
    case kTfLiteFloat32:
      engine->outinfo->type = GST_ML_TYPE_FLOAT32;
      break;
    case kTfLiteInt32:
      engine->outinfo->type = GST_ML_TYPE_INT32;
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
  delete engine;
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

gboolean
gst_ml_tflite_engine_execute (GstMLTFLiteEngine * engine,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMapInfo *inmap = NULL, *outmap = NULL;
  gboolean success = FALSE;
  guint idx = 0, num = 0;

  g_return_val_if_fail (engine != NULL, FALSE);

  if (gst_buffer_n_memory (inbuffer) != engine->ininfo->n_tensors) {
    GST_WARNING ("Input buffer has %u memory blocks but engine requires %u!",
        gst_buffer_n_memory (inbuffer), engine->ininfo->n_tensors);
    return FALSE;
  }

  if (gst_buffer_n_memory (outbuffer) != engine->outinfo->n_tensors) {
    GST_WARNING ("Output buffer has %u memory blocks but engine requires %u!",
        gst_buffer_n_memory (outbuffer), engine->outinfo->n_tensors);
    return FALSE;
  }

  inmap = g_new0 (GstMapInfo, engine->ininfo->n_tensors);
  outmap = g_new0 (GstMapInfo, engine->outinfo->n_tensors);

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx) {
    // Get input tensor information.
    gint input = engine->interpreter->inputs()[idx];
    TfLiteTensor *tensor = engine->interpreter->tensor(input);

    // Map input buffer memory blocks.
    success = gst_buffer_map_range (inbuffer, idx, 1, &inmap[idx],
        GST_MAP_READ);

    if (!success) {
      GST_ERROR ("Failed to map input memory block at idx %u!", idx);

      for (num = 0; num < idx; num++)
        gst_buffer_unmap (inbuffer, &inmap[num]);

      g_free (outmap);
      g_free (inmap);

      return FALSE;
    }

    tensor->data.raw = reinterpret_cast<char*>(inmap[idx].data);
  }

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx) {
    // Get output tensor information.
    gint output = engine->interpreter->outputs()[idx];
    TfLiteTensor *tensor = engine->interpreter->tensor(output);

    // Map output buffer memory blocks.
    success = gst_buffer_map_range (outbuffer, idx, 1, &outmap[idx],
        GST_MAP_READWRITE);

    if (!success) {
      GST_ERROR ("Failed to map input memory block at idx %u!", idx);

      for (num = 0; num < idx; num++)
        gst_buffer_unmap (outbuffer, &outmap[num]);

      for (num = 0; num < engine->ininfo->n_tensors; num++)
        gst_buffer_unmap (inbuffer, &inmap[num]);

      g_free (outmap);
      g_free (inmap);

      return FALSE;
    }

    tensor->data.raw = reinterpret_cast<char*>(outmap[idx].data);
  }

  if (!(success = (engine->interpreter->Invoke() == 0)))
    GST_ERROR ("Model execution failed!");

  for (idx = 0; idx < engine->ininfo->n_tensors; ++idx)
    gst_buffer_unmap (inbuffer, &inmap[idx]);

  for (idx = 0; idx < engine->outinfo->n_tensors; ++idx)
    gst_buffer_unmap (outbuffer, &outmap[idx]);

  g_free (inmap);
  g_free (outmap);

  return success;
}
