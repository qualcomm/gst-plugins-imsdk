/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#include <fstream>
#include <vector>
#include <string>
#include <fastcv/fastcv.h>
#include <tensorflow/lite/delegates/nnapi/nnapi_delegate.h>
#include <tensorflow/lite/examples/label_image/get_top_n.h>
#include <tensorflow/lite/examples/label_image/get_top_n_impl.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/tools/evaluation/utils.h>
#include "tflite_base.h"

namespace mle {

static const uint32_t delegate_preferences = 00300000;

// Takes a file name, and loads a list of labels from it, one per line, and
// returns a vector of the strings. It pads with empty strings so the length
// of the result is a multiple of 16, because our model expects that.
TfLiteStatus TFLBase::ReadLabelsFile(const std::string& file_name,
                            std::vector<std::string>& result,
                            size_t& found_label_count) {
  std::ifstream file(file_name);
  if (!file) {
    VAM_ML_LOGE("%s: Labels file %s not found!", __func__, file_name.c_str());
    return kTfLiteError;
  }
  result.clear();
  std::string line;
  while (std::getline(file, line)) {
    result.push_back(line);
  }
  found_label_count = result.size();
  const int padding = 16;
  while (result.size() % padding) {
    result.emplace_back();
  }
  return kTfLiteOk;
}

TFLBase::TFLBase(MLConfig &config) {
  config_.conf_threshold = config.conf_threshold;
  config_.model_file = config.model_file;
  config_.labels_file = config.labels_file;
  config_.number_of_threads = config.number_of_threads;
  config_.use_nnapi = config.use_nnapi;
  input_params_.scale_buf = nullptr;
}

TfLiteDelegatePtrMap TFLBase::GetDelegates() {
  TfLiteDelegatePtrMap delegates;

  if (config_.use_nnapi) {
    tflite::StatefulNnApiDelegate::Options options;
    options.execution_preference =
        static_cast<tflite::StatefulNnApiDelegate::Options::ExecutionPreference>(
        delegate_preferences);

    auto delegate = tflite::evaluation::CreateNNAPIDelegate(options);
    if (!delegate) {
      VAM_ML_LOGI("NNAPI acceleration is unsupported on this platform.");
    } else {
      delegates.emplace("NNAPI", std::move(delegate));
    }
  }

  return delegates;
}

int32_t TFLBase::Init(const struct MLEInputParams* source_info) {

  // Load tflite model from file
  std::string folder("/data/misc/camera");
  std::string model_file = folder + "/" + config_.model_file;
  engine_params_.model = tflite::FlatBufferModel::BuildFromFile(model_file.c_str());
  if (!engine_params_.model) {
    VAM_ML_LOGE("%s: Failed to load model from %s", __func__, model_file.c_str());
    return MLE_FAIL;
  }
  VAM_ML_LOGI("%s: Loaded model from %s", __func__, model_file.c_str());

  // Load labels from file
  std::string labels_file_name = folder + "/" + config_.labels_file;
  if (ReadLabelsFile(labels_file_name, engine_params_.labels,
                     engine_params_.label_count) != kTfLiteOk) {
    VAM_ML_LOGE("%s: Failed to read labeles file %s", __func__,
                     labels_file_name.c_str());
    return MLE_FAIL;
  }
  VAM_ML_LOGI("%s: Loaded %d labels from %s", __func__,
                   engine_params_.label_count, labels_file_name.c_str());

  // Gather input configuration parameters
  input_params_.width  = source_info->width;
  input_params_.height = source_info->height;
  input_params_.format = source_info->format;

  VAM_ML_LOGI("%s: Input data: format %d, height %d, width %d", __func__,
                   1, input_params_.height, input_params_.width);

  // Create the interpreter
  tflite::ops::builtin::BuiltinOpResolver resolver;
  tflite::InterpreterBuilder(*engine_params_.model, resolver)(&engine_params_.interpreter);
  if (!engine_params_.interpreter) {
    VAM_ML_LOGE("%s: Failed to construct interpreter", __func__);
    return MLE_FAIL;
  }

  // Set the interpreter configurations
  engine_params_.interpreter->SetNumThreads(config_.number_of_threads);
  VAM_ML_LOGI("%s: USE_NNAPI %d No: of threads %d", __func__,
                   config_.use_nnapi, config_.number_of_threads);

  // Validate & process model information
  if (ValidateModelInfo() != MLE_OK) {
    VAM_ML_LOGE("%s: Provided model is not supported", __func__);
    return MLE_FAIL;
  }

  // Allocate buffer for appending input frame's data
  // Since fast-cv based color conversion is used, no need to
  // append frame_l_data[0], frame_l_data[1] & frame_l_data[2]

  // Check if rescaling is required or not
  if ((input_params_.width != engine_params_.width) ||
      (input_params_.height != engine_params_.height)) {
    engine_params_.do_rescale = true;

    // Allocate output buffer for pre-processing
    posix_memalign(reinterpret_cast<void**>(&input_params_.scale_buf),
                                    128,
                                    ((engine_params_.width *
                                          engine_params_.height * 3) / 2));
    if (nullptr == input_params_.scale_buf) {
      VAM_ML_LOGE("%s: Buffer allocation failed", __func__);
      return MLE_FAIL;
    }
  } else {
    engine_params_.do_rescale = false;
  }

  auto delegates = GetDelegates();
  for (const auto& delegate : delegates) {
    if (engine_params_.interpreter->ModifyGraphWithDelegate(delegate.second.get()) !=
        kTfLiteOk) {
      VAM_ML_LOGE("Failed to apply delegate.");
    }
  }

  // Allocate output buffer for re-scaling
  // Input tensor would be used for holding scaled output data

  // Allocate the tensors
  if (engine_params_.interpreter->AllocateTensors() != kTfLiteOk) {
    VAM_ML_LOGE("%s: Failed to allocate tensors!", __func__);
    if (nullptr != input_params_.scale_buf) {
      free(input_params_.scale_buf);
      input_params_.scale_buf = nullptr;
    }
    return MLE_FAIL;
  }

  // Make a note of the model's input buffer
  int input = engine_params_.interpreter->inputs()[0];
  TfLiteType input_type = engine_params_.interpreter->tensor(input)->type;
  switch (input_type) {
    case kTfLiteUInt8:
      engine_params_.input_buffer = (engine_params_.interpreter->tensor(input)->data).uint8;
      break;
    case kTfLiteFloat32:
      engine_params_.input_buffer_f = (engine_params_.interpreter->tensor(input)->data).f;
      break;
    default:
      VAM_ML_LOGE("%s: No support for %d input type", __func__, input_type);
      return MLE_FAIL;
  }

  VAM_ML_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

void TFLBase::Deinit() {
  VAM_ML_LOGI("%s: Enter", __func__);
  if (nullptr != input_params_.scale_buf) {
    free(input_params_.scale_buf);
    input_params_.scale_buf = nullptr;
  }
  VAM_ML_LOGI("%s: Exit", __func__);
}

int32_t TFLBase::Process(struct SourceFrame* frame_info, GstBuffer* buffer) {
  VAM_ML_LOGI("%s: Enter", __func__);

  // pre-processing input frame
  if (PreProcessInput(frame_info) != MLE_OK) {
    VAM_ML_LOGE("%s: PreProcessInput Failed!!!", __func__);
    return MLE_FAIL;
  }

  // Execute the network
  if (engine_params_.interpreter->Invoke() != kTfLiteOk) {
    VAM_ML_LOGE("%s: Failed to invoke!", __func__);
    return MLE_FAIL;
  }
  VAM_ML_LOGI("%s: Execution completed!", __func__);

  // post-processing the output results
  if (PostProcessOutput(buffer) != MLE_OK) {
    VAM_ML_LOGE("%s: PostProcessOutput Failed!!!", __func__);
    return MLE_FAIL;
  }

  VAM_ML_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

int32_t TFLBase::ValidateModelInfo() {
  // Validate for supported models
  // Mobilenet model
  //   - input tensor (1, uint8_t, 1 X height X width X 3)
  //   - output tensor (1, uint8_t or float, 1 X number of labels)
  //   - order of predictions corresponds to order of labels, as listed in labels file
  VAM_ML_LOGI("%s: Support tensor types - kTfLiteFloat32 %d - kTfLiteUInt8 %d",
                  __func__, kTfLiteFloat32, kTfLiteUInt8);
  // Validate input nodes
  const std::vector<int> inputs = engine_params_.interpreter->inputs();
  engine_params_.num_inputs = inputs.size();
  if (engine_params_.num_inputs != 1) {
    VAM_ML_LOGE("%s: No support for %d input nodes", __func__,
                    engine_params_.num_inputs);
    return MLE_FAIL;
  }

  int input = engine_params_.interpreter->inputs()[0];

  // Check for input tensor type
  TfLiteType input_type = engine_params_.interpreter->tensor(input)->type;
  switch (input_type) {
    case kTfLiteUInt8:
    case kTfLiteFloat32:
      break;
    default:
      VAM_ML_LOGE("%s: No support for %d input type", __func__, input_type);
      return MLE_FAIL;
  }

  TfLiteIntArray* dims = engine_params_.interpreter->tensor(input)->dims;
  int batch_size = dims->data[0];
  if (batch_size != 1) {
    VAM_ML_LOGE("%s: No support for %d input batch size", __func__, batch_size);
    return MLE_FAIL;
  }

  // Initialize engine configuration parameters
  engine_params_.height = dims->data[1];
  engine_params_.width = dims->data[2];
  engine_params_.channels = dims->data[3];

  VAM_ML_LOGI("%s: Input tensor: type %d, batch size %d", __func__,
              input_type, batch_size);
  VAM_ML_LOGI("%s: Input tensor: height %d, width %d, channels %d", __func__,
              engine_params_.height, engine_params_.width,
              engine_params_.channels);
  VAM_ML_LOGI("%s: Input tensor: quantization scale %f, zero_point %d",
              __func__, engine_params_.interpreter->tensor(input)->params.scale,
              engine_params_.interpreter->tensor(input)->params.zero_point);

  // Validate output nodes
  const std::vector<int> outputs = engine_params_.interpreter->outputs();
  engine_params_.num_outputs = outputs.size();
  if (engine_params_.num_inputs != 1 && engine_params_.num_inputs != 4) {
    VAM_ML_LOGE("%s: No support for %d output nodes", __func__,
                engine_params_.num_outputs);
    return MLE_FAIL;
  }
  if (engine_params_.num_outputs == 1) {
    int output = engine_params_.interpreter->outputs()[0];

    // Check for output tensor type
    TfLiteType output_type = engine_params_.interpreter->tensor(output)->type;
    switch (output_type) {
      case kTfLiteUInt8:
      case kTfLiteFloat32:
        break;
      default:
        VAM_ML_LOGE("%s: No support for %d output type", __func__, output_type);
        return MLE_FAIL;
        break;
    }

    // Check for output tensor dimensions
    TfLiteIntArray* output_dims = engine_params_.interpreter->tensor(output)->dims;
    engine_params_.num_predictions = output_dims->data[output_dims->size - 1];
    if (engine_params_.label_count != engine_params_.num_predictions) {
      VAM_ML_LOGE("%s: No: of labels %d, DO NOT match no: of predictions %d",
                       __func__, engine_params_.label_count,
                       engine_params_.num_predictions);
      return MLE_FAIL;
    }

    VAM_ML_LOGI("%s: Output tensor: type %d, no: of predictions %d", __func__,
                     output_type, engine_params_.num_predictions);
    VAM_ML_LOGI("%s: Output tensor: quantization scale %f, zero_point %d",
                     __func__,
                     engine_params_.interpreter->tensor(output)->params.scale,
                     engine_params_.interpreter->tensor(output)->params.zero_point);
  } else if (engine_params_.num_outputs == 4) {
    for (uint32_t i = 0; i < engine_params_.num_outputs; ++i) {
      int output = engine_params_.interpreter->outputs()[i];

      // Check for output tensor type
      TfLiteType output_type = engine_params_.interpreter->tensor(output)->type;
      switch (output_type) {
        case kTfLiteFloat32:
          break;
        default:
          VAM_ML_LOGE("%s: For output node %d, no support for %d output type",
                          __func__, i, output_type);
          return MLE_FAIL;
          break;
      }
      VAM_ML_LOGI("%s: Output tensor: type %d", __func__, output_type);

      // Output node quantization values
      VAM_ML_LOGI("%s: Output tensor: quantization scale %f, zero_point %d",
                  __func__,
                  engine_params_.interpreter->tensor(output)->params.scale,
                  engine_params_.interpreter->tensor(output)->params.zero_point);
    }
  }

  return MLE_OK;
}

void TFLBase::PreProcessScale(
  uint8_t*       pSrcLuma,
  uint8_t*       pSrcChroma,
  uint8_t*       pDst,
  const uint32_t srcWidth,
  const uint32_t srcHeight,
  const uint32_t scaleWidth,
  const uint32_t scaleHeight,
  MLEImageFormat format)
{

  if ((format == mle_format_nv12) || (format == mle_format_nv21)) {
    fcvScaleDownMNu8(pSrcLuma,
                     srcWidth,
                     srcHeight,
                     0,
                     pDst,
                     scaleWidth,
                     scaleHeight,
                     0);
    fcvScaleDownMNu8(pSrcChroma,
                     srcWidth,
                     srcHeight/2,
                     0,
                     pDst + (scaleWidth*scaleHeight),
                     scaleWidth,
                     scaleHeight/2,
                     0);
  }
}

void TFLBase::PreProcessColorConvertRGB(
    uint8_t*       pSrcLuma,
    uint8_t*       pSrcChroma,
    uint8_t*       pDst,
    const uint32_t width,
    const uint32_t height,
    MLEImageFormat format)
{
  if ((format == mle_format_nv12) || (format == mle_format_nv21)) {
    fcvColorYCbCr420PseudoPlanarToRGB888u8(pSrcLuma,
                                           pSrcChroma,
                                           width,
                                           height,
                                           0,
                                           0,
                                           pDst,
                                           0);
  }
}

int32_t TFLBase::PreProcessInput(SourceFrame* frame_info) {
  VAM_ML_LOGI("%s: Enter", __func__);
  if (engine_params_.do_rescale) {
    PreProcessScale(frame_info->frame_data[0],
                    frame_info->frame_data[1],
                    input_params_.scale_buf,
                    input_params_.width,
                    input_params_.height,
                    engine_params_.width,
                    engine_params_.height,
                    input_params_.format);

    PreProcessColorConvertRGB(input_params_.scale_buf,
                              input_params_.scale_buf + (engine_params_.width * engine_params_.height),
                              engine_params_.input_buffer,
                              engine_params_.width,
                              engine_params_.height,
                              input_params_.format);
  } else {
    //Color conversion
    fcvColorYCbCr420PseudoPlanarToRGB888u8(frame_info->frame_data[0],
                                           frame_info->frame_data[1],
                                           input_params_.width,
                                           input_params_.height, 0, 0,
                                           engine_params_.input_buffer, 0);
  }
  VAM_ML_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

int32_t TFLBase::PostProcessMultiOutput(GstBuffer* buffer) {
  VAM_ML_LOGI("%s: Enter", __func__);

  float *detected_boxes = engine_params_.interpreter->typed_output_tensor<float>(0);
  float *detected_classes = engine_params_.interpreter->typed_output_tensor<float>(1);
  float *detected_scores = engine_params_.interpreter->typed_output_tensor<float>(2);
  float *num_boxes = engine_params_.interpreter->typed_output_tensor<float>(3);

  float num_box = num_boxes[0];
  VAM_ML_LOGI("%s: Found %f boxes", __func__, num_box);
  for (int i = 0; i < num_box; i++) {
    if (detected_scores[i] < config_.conf_threshold) continue;

    uint32_t width = input_params_.width;
    uint32_t height = input_params_.height;

    GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(buffer);
    if (!meta) {
      VAM_ML_LOGE("Failed to create metadata");
      return MLE_NULLPTR;
    }

    GstMLClassificationResult *box_info = (GstMLClassificationResult*)malloc(
        sizeof(GstMLClassificationResult));

    uint32_t label_size =
        engine_params_.labels[detected_classes[i] + 1].size() + 1;
    box_info->name = (gchar *)malloc(label_size);
    snprintf(
        box_info->name,
        label_size,
        "%s",
        engine_params_.labels[detected_classes[i] + 1].c_str());
    box_info->confidence = detected_scores[i];
    meta->box_info = g_slist_append (meta->box_info, box_info);

    meta->bounding_box.x = static_cast<uint32_t>(
        detected_boxes[i * 4 + 1] * width);
    meta->bounding_box.y = static_cast<uint32_t>(
        detected_boxes[i * 4] * height);
    meta->bounding_box.width = static_cast<uint32_t>(
        detected_boxes[i * 4 + 3] * width) - meta->bounding_box.x;
    meta->bounding_box.height = static_cast<uint32_t>(
        detected_boxes[i * 4 + 2] * height) - meta->bounding_box.y;
  }
  VAM_ML_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

int32_t TFLBase::PostProcessOutput(GstBuffer* buffer) {
  VAM_ML_LOGI("%s: Enter", __func__);

  if (engine_params_.num_outputs == 4) {
    // post-processing the output results from 4 nodes
    if (PostProcessMultiOutput(buffer) != MLE_OK) {
      VAM_ML_LOGE("%s: PostProcessMultiOutput Failed!!!", __func__);
      return MLE_FAIL;
    }
    VAM_ML_LOGI("%s: Exit", __func__);
    return MLE_OK;
  }

  // For MobileNet model, return only top most confident object info
  int output = engine_params_.interpreter->outputs()[0];
  auto top_results_count = 1;
  std::vector<std::pair<float, int>> top_results;

  // Sort output predictions in descending order of confidence
  // and then, return the top most confidence result
  bool verbose = false;
  switch (engine_params_.interpreter->tensor(output)->type) {
    case kTfLiteFloat32:
      if (verbose) {
        float* temp_output = engine_params_.interpreter->typed_output_tensor<float>(0);
        for (uint32_t i = 0; i < engine_params_.num_predictions; ++i) {
          VAM_ML_LOGI("%s: i - %d :: conf - %f", __func__, i, temp_output[i]);
        }
      }
      tflite::label_image::get_top_n<float>(
                       engine_params_.interpreter->typed_output_tensor<float>(0),
                       engine_params_.num_predictions, top_results_count,
                       config_.conf_threshold, &top_results, true);
      break;
    case kTfLiteUInt8:
      if (verbose) {
        uint8_t* temp_output = engine_params_.interpreter->typed_output_tensor<uint8_t>(0);
        for (uint32_t i = 0; i < engine_params_.num_predictions; ++i) {
          VAM_ML_LOGI("%s: i - %d :: conf - %d", __func__, i, temp_output[i]);
        }
      }
      tflite::label_image::get_top_n<uint8_t>(
                       engine_params_.interpreter->typed_output_tensor<uint8_t>(0),
                       engine_params_.num_predictions, top_results_count,
                       config_.conf_threshold, &top_results, false);
      break;
    default:
      VAM_ML_LOGE("%s: Invalid output tensor type %d", __func__,
                      engine_params_.interpreter->tensor(output)->type);
      return MLE_FAIL;
      break;
  }
  VAM_ML_LOGI("%s: Found %d objects", __func__, top_results.size());

  // If found, return the label with most confidence level
  if (top_results.size() > 0) {
    const auto& result = top_results.front();
    const float confidence = result.first;
    const int index = result.second;
    if (confidence > config_.conf_threshold) {
      VAM_ML_LOGI("%s: confidence %f, index %d, label %s", __func__,
                      confidence, index, engine_params_.labels[index].c_str());

      GstMLClassificationMeta *meta =
          gst_buffer_add_classification_meta(buffer);
      if (!meta) {
        VAM_ML_LOGE("Failed to create metadata");
        return MLE_NULLPTR;
      }

      meta->result.confidence = confidence;
      uint32_t label_size = engine_params_.labels[index].size() + 1;
      meta->result.name = (gchar *)malloc(label_size);
      snprintf(meta->result.name, label_size, "%s",
               engine_params_.labels[index].c_str());
    }
  }

  VAM_ML_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

}; // namespace mle
