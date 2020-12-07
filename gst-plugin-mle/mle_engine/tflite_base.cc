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

#include <vector>
#include <string>

#ifdef DELEGATE_SUPPORT
#include <tensorflow/lite/delegates/nnapi/nnapi_delegate.h>
#include <tensorflow/lite/experimental/delegates/hexagon/hexagon_delegate.h>
#include <tensorflow/lite/delegates/gpu/delegate.h>
#include <tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h>
#endif

#include <tensorflow/lite/examples/label_image/get_top_n.h>
#include <tensorflow/lite/examples/label_image/get_top_n_impl.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/core/public/version.h>
#include "tflite_base.h"
#include "common_utils.h"

namespace mle {

TFLBase::TFLBase(MLConfig &config) : MLEngine(config) {
  config_.number_of_threads = config.number_of_threads;
  config_.delegate = config.delegate;
  need_labels_ = true;
  process_frame_ = false;
  frame_ready_ = false;
  status_code_ = MLE_OK;
}

void TFLBase::Deinit() {
  MLEngine::FreeInternalBuffers();
  process_frame_ = false;
  frame_ready_ = true;
  std::unique_lock<std::mutex> lk(inference_mutex_);
  inference_cv_.notify_one();
  lk.unlock();
  if (tflite_thread_.joinable()) {
    tflite_thread_.join();
  }
}

TfLiteDelegatePtrMap TFLBase::GetDelegates() {
  TfLiteDelegatePtrMap delegates;

#ifdef DELEGATE_SUPPORT
  switch (config_.delegate) {
    case DelegateType::nnapi:
    {
      MLE_LOGI("%s: Using NNAPI delegate", __func__);
      uint32_t delegate_preferences_dsp = 00100000;

      tflite::StatefulNnApiDelegate::Options options;

      options.execution_preference =
          static_cast<tflite::StatefulNnApiDelegate::Options::ExecutionPreference>(
            delegate_preferences_dsp);

      auto delegate = TfLiteDelegatePtr(
        new tflite::StatefulNnApiDelegate(options), [](TfLiteDelegate* delegate) {
          delete reinterpret_cast<tflite::StatefulNnApiDelegate*>(delegate);
        });

      if (!delegate) {
        MLE_LOGE("NNAPI acceleration is unsupported on this platform.");
      } else {
        delegates.emplace("NNAPI", std::move(delegate));
      }
      break;
    }
    case DelegateType::nnapi_npu:
    {
      MLE_LOGI("%s: Using NNAPI-NPU delegate", __func__);
      uint32_t delegate_preferences_npu = 00300000;

      tflite::StatefulNnApiDelegate::Options options;

      options.execution_preference =
          static_cast<tflite::StatefulNnApiDelegate::Options::ExecutionPreference>(
            delegate_preferences_npu);

      auto delegate = TfLiteDelegatePtr(
        new tflite::StatefulNnApiDelegate(options), [](TfLiteDelegate* delegate) {
          delete reinterpret_cast<tflite::StatefulNnApiDelegate*>(delegate);
        });

      if (!delegate) {
        MLE_LOGE("NNAPI-NPU acceleration is unsupported on this platform.");
      } else {
        delegates.emplace("NNAPI-NPU", std::move(delegate));
      }
      break;
    }
    case DelegateType::hexagon_nn:
    {
      MLE_LOGI("%s: Using hexagon delegate", __func__);
      TfLiteHexagonInit();
      const TfLiteHexagonDelegateOptions options = {
        /*debug_level=*/0, /*powersave_level=*/0, /*profiling*/false,
        /*print_graph_debug=*/false};

      TfLiteDelegate* hexagon_delegate = TfLiteHexagonDelegateCreate(&options);
      if (!hexagon_delegate) {
        MLE_LOGI("Hexagon delegate is not supported");
        return delegates;
      }

      auto delegate = TfLiteDelegatePtr(
        hexagon_delegate, [](TfLiteDelegate* delegate) {
        TfLiteHexagonDelegateDelete(delegate);
        TfLiteHexagonTearDown();
      });

      if (!delegate) {
        MLE_LOGE("Hexagon delegate is not supported");
      } else {
        delegates.emplace("HEXAGON", std::move(delegate));
      }
      break;
    }
    case DelegateType::gpu:
    {
      MLE_LOGI("%s: Using GPU delegate", __func__);

      TfLiteGpuDelegateOptionsV2 options = TfLiteGpuDelegateOptionsV2Default();

      auto delegate = TfLiteDelegatePtr(TfLiteGpuDelegateV2Create(&options),
                      &TfLiteGpuDelegateV2Delete);

      if (!delegate) {
        MLE_LOGE("GPU delegate is not supported");
      } else {
          delegates.emplace("GPU", std::move(delegate));
      }
      break;
    }
    case DelegateType::xnnpack:
    {
      MLE_LOGI("%s: Using xnnpack delegate", __func__);

      TfLiteXNNPackDelegateOptions options = TfLiteXNNPackDelegateOptionsDefault();

      auto delegate = TfLiteDelegatePtr(TfLiteXNNPackDelegateCreate(&options),
                      &TfLiteXNNPackDelegateDelete);

      if (!delegate) {
        MLE_LOGE("XNNPACK delegate is not supported");
      } else {
        delegates.emplace("XNNPACK", std::move(delegate));
      }
      break;
    }
    case DelegateType::cpu:
    default:
    {
      MLE_LOGD("%s: Fallback to CPU runtime", __func__);
    }
  }
#else
  MLE_LOGE("%s: This platform do not support delegates.", __func__);
#endif

  return delegates;
}

int32_t TFLBase::LoadModel(std::string& model_path) {
  int32_t res = MLE_OK;
  tflite_params_.model =
      tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
  if (!tflite_params_.model) {
    MLE_LOGE("%s: Failed to load model from %s", __func__, model_path.c_str());
    res = MLE_FAIL;
  }
  return res;
}

void TFLBase::TFliteRuntimeRun() {

  // Set the interpreter configurations
  tflite_params_.interpreter->SetNumThreads(config_.number_of_threads);

  auto delegates = GetDelegates();
  for (const auto& delegate : delegates) {
    if (tflite_params_.interpreter->ModifyGraphWithDelegate(
          delegate.second.get()) != kTfLiteOk) {
      MLE_LOGE("Failed to apply delegate.");
    } else {
      MLE_LOGI("%s delegate applied successfully!", delegate.first.c_str());
    }
  }

  // Allocate the tensors
  if (tflite_params_.interpreter->AllocateTensors() != kTfLiteOk) {
    MLE_LOGE("%s: Failed to allocate tensors!", __func__);
    process_frame_ = false;
    status_code_ = MLE_FAIL;
    std::unique_lock<std::mutex> lk_config(config_mutex_);
    config_cv_.notify_one();
    return;
  }

  {
    process_frame_ = true;
    std::unique_lock<std::mutex> lk_config(config_mutex_);
    config_cv_.notify_one();
  }


  while (1) {
    std::unique_lock<std::mutex> lk(inference_mutex_);
    inference_cv_.wait(lk, [this] {return frame_ready_ == true;});
    if (!process_frame_) {
      status_code_ = MLE_FAIL;
      frame_ready_ = false;
      std::unique_lock<std::mutex> lk_process(processed_mutex_);
      processed_cv_.notify_one();
      return;
    }

    MLE_LOGD("%s: time to invoke Kernel!", __func__);

    // Execute the network
    if (tflite_params_.interpreter->Invoke() != kTfLiteOk) {
      invoke_fail_count_++;
      MLE_LOGE("%s: Invoke fail occured %d", __func__, invoke_fail_count_);
      status_code_ = MLE_FAIL;
    }
    if (frame_ready_) {
      frame_ready_ = false;
    } else {  // Execute Model thread timed out and exited already
      continue;  // Therefore no need to notify and wait for new frame
    }
    std::unique_lock<std::mutex> lk_process(processed_mutex_);
    processed_cv_.notify_one();
    lk_process.unlock();
  }
   return ;
}

int32_t TFLBase::InitFramework() {
  MLE_LOGI("%s Enter", __func__);
  MLE_LOGI("TFLite version: %s", TF_VERSION_STRING);

  // Create the interpreter
  tflite::ops::builtin::BuiltinOpResolver resolver;
  tflite::InterpreterBuilder(*tflite_params_.model, resolver)
        (&tflite_params_.interpreter);
  if (!tflite_params_.interpreter) {
    MLE_LOGE("%s: Failed to construct interpreter", __func__);
    return MLE_FAIL;
  }

  // Validate & process model information
  if (ValidateModelInfo() != MLE_OK) {
    MLE_LOGE("%s: Provided model is not supported", __func__);
    return MLE_FAIL;
  }

  tflite_thread_ = std::thread(&TFLBase::TFliteRuntimeRun, this);

  MLE_LOGI("%s Exit", __func__);
  return MLE_OK;
}

void* TFLBase::GetInputBuffer() {

  while (!process_frame_) {
    std::unique_lock<std::mutex> lk(config_mutex_);
    config_cv_.wait_for(lk, std::chrono::seconds(1),
                        [this] { return process_frame_ == true; });
    lk.unlock();
    if (status_code_ == MLE_FAIL || !process_frame_) {
      MLE_LOGE("%s TFLite Runtime not configured", __func__);
      return nullptr;
    }
  }

  void* buf = nullptr;
  size_t buf_size =
      engine_input_params_.width * engine_input_params_.height * 3;
  int input = tflite_params_.interpreter->inputs()[0];
  TfLiteType input_type = tflite_params_.interpreter->tensor(input)->type;
  switch (input_type) {
    case kTfLiteUInt8:
      buf_size *= sizeof(uint8_t);
      buf = (void*)(tflite_params_.interpreter->tensor(input)->data).uint8;
      break;
    case kTfLiteFloat32:
      buf_size *= sizeof(float);
      buf = (void*)(tflite_params_.interpreter->tensor(input)->data).f;
      break;
    default:
      MLE_LOGE("%s: No support for %d input type", __func__, input_type);
      break;
  }
  // memset buffer now to avoid it while padding/mean subtract
  memset(buf, 0, buf_size);
  return buf;
}

int32_t TFLBase::ExecuteModel() {
  frame_ready_ = true;
  std::unique_lock<std::mutex> lk_infer(inference_mutex_);
  inference_cv_.notify_one();
  lk_infer.unlock();
  MLE_LOGD("%s: Notify to resume inference!", __func__);
  std::unique_lock<std::mutex> lk(processed_mutex_);
  while (frame_ready_) {
    if (processed_cv_.wait_for(lk, std::chrono::seconds(5)) ==
        std::cv_status::no_timeout) {
      MLE_LOGI("%s: Execution completed!", __func__);
      if (status_code_ != kTfLiteOk) {
        return MLE_FAIL;
      }
    } else {
      MLE_LOGE("%s: Execution Timed Out!", __func__);
      frame_ready_ = false;
      return MLE_FAIL;
    }
  }
  return MLE_OK;
}

int32_t TFLBase::ValidateModelInfo() {
  // Validate input nodes
  const std::vector<int> inputs = tflite_params_.interpreter->inputs();
  tflite_params_.num_inputs = inputs.size();
  if (tflite_params_.num_inputs != 1) {
    MLE_LOGE("%s: No support for %d input nodes", __func__,
                    tflite_params_.num_inputs);
    return MLE_FAIL;
  }

  int input = tflite_params_.interpreter->inputs()[0];

  // Check for input tensor type
  TfLiteType input_type = tflite_params_.interpreter->tensor(input)->type;
  switch (input_type) {
    case kTfLiteUInt8:
    case kTfLiteFloat32:
      break;
    default:
      MLE_LOGE("%s: No support for %d input type", __func__, input_type);
      return MLE_FAIL;
  }

  TfLiteIntArray* dims = tflite_params_.interpreter->tensor(input)->dims;
  int batch_size = dims->data[0];
  if (batch_size != 1) {
    MLE_LOGE("%s: No support for %d input batch size", __func__, batch_size);
    return MLE_FAIL;
  }

  // Initialize engine configuration parameters
  engine_input_params_.height = dims->data[1];
  engine_input_params_.width = dims->data[2];

  MLE_LOGI("%s: Input tensor: type %d, batch size %d", __func__,
              input_type, batch_size);
  MLE_LOGI("%s: Input tensor: height %d, width %d", __func__,
              engine_input_params_.height, engine_input_params_.width);
  MLE_LOGI("%s: Input tensor: quantization scale %f, zero_point %d",
            __func__, tflite_params_.interpreter->tensor(input)->params.scale,
            tflite_params_.interpreter->tensor(input)->params.zero_point);

  // Validate output nodes
  const std::vector<int> outputs = tflite_params_.interpreter->outputs();
  tflite_params_.num_outputs = outputs.size();
  if (tflite_params_.num_outputs != 1 && tflite_params_.num_outputs != 3 &&
      tflite_params_.num_outputs != 4) {
    MLE_LOGE("%s: No support for %d output nodes", __func__,
                tflite_params_.num_outputs);
    return MLE_FAIL;
  }
  if (tflite_params_.num_outputs == 1) {
    int output = tflite_params_.interpreter->outputs()[0];

    // Check for output tensor type
    TfLiteType output_type = tflite_params_.interpreter->tensor(output)->type;
    switch (output_type) {
      case kTfLiteUInt8:
      case kTfLiteFloat32:
      case kTfLiteInt32:
        break;
      default:
        MLE_LOGE("%s: No support for %d output type", __func__, output_type);
        return MLE_FAIL;
        break;
    }

    // Check for output tensor dimensions
    TfLiteIntArray* output_dims =
        tflite_params_.interpreter->tensor(output)->dims;
    tflite_params_.num_predictions = output_dims->data[output_dims->size - 1];
    if (need_labels_ && label_count_ != tflite_params_.num_predictions) {
      MLE_LOGE("%s: No: of labels %zu, DO NOT match no: of predictions %d",
                       __func__, label_count_,
                       tflite_params_.num_predictions);
      return MLE_FAIL;
    }

    MLE_LOGI("%s: Output tensor: type %d, no: of predictions %d", __func__,
                     output_type, tflite_params_.num_predictions);
    MLE_LOGI("%s: Output tensor: quantization scale %f, zero_point %d",
              __func__,
              tflite_params_.interpreter->tensor(output)->params.scale,
              tflite_params_.interpreter->tensor(output)->params.zero_point);
  } else if (tflite_params_.num_outputs == 3 ||
             tflite_params_.num_outputs == 4) {
    for (uint32_t i = 0; i < tflite_params_.num_outputs; ++i) {
      int output = tflite_params_.interpreter->outputs()[i];

      // Check for output tensor type
      TfLiteType output_type = tflite_params_.interpreter->tensor(output)->type;
      switch (output_type) {
        case kTfLiteInt32:
        case kTfLiteFloat32:
        case kTfLiteUInt8:
          break;
        default:
          MLE_LOGE("%s: For output node %d, no support for %d output type",
                          __func__, i, output_type);
          return MLE_FAIL;
          break;
      }
      MLE_LOGI("%s: Output tensor: type %d", __func__, output_type);

      // Output node quantization values
      MLE_LOGI("%s: Output tensor: quantization scale %f, zero_point %d",
                  __func__,
                  tflite_params_.interpreter->tensor(output)->params.scale,
                  tflite_params_.interpreter->tensor(output)->params.zero_point);
    }
  }

  return MLE_OK;
}

int32_t TFLBase::PostProcessMultiOutput(GstBuffer* buffer) {
  MLE_LOGI("%s: Enter", __func__);

  float *detected_boxes =
      tflite_params_.interpreter->typed_output_tensor<float>(0);
  float *detected_classes =
      tflite_params_.interpreter->typed_output_tensor<float>(1);
  float *detected_scores =
      tflite_params_.interpreter->typed_output_tensor<float>(2);
  float *num_boxes = tflite_params_.interpreter->typed_output_tensor<float>(3);

  float num_box = num_boxes[0];
  MLE_LOGI("%s: Found %f boxes", __func__, num_box);
  for (int i = 0; i < num_box; i++) {
    if (detected_scores[i] < config_.conf_threshold) continue;

    uint32_t width = source_params_.width;
    uint32_t height = source_params_.height;

    float scale_ratio_x = (float)engine_input_params_.width / scale_width_;
    float scale_ratio_y = (float)engine_input_params_.height / scale_height_;

    if (config_.preprocess_mode == PreprocessingMode::kKeepARCrop) {
      width = po_.width;
      height = po_.height;
    }

    GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(buffer);
    if (!meta) {
      MLE_LOGE("Failed to create metadata");
      return MLE_NULLPTR;
    }

    GstMLClassificationResult *box_info = (GstMLClassificationResult*)malloc(
        sizeof(GstMLClassificationResult));

    uint32_t label_size =
        labels_[detected_classes[i] + 1].size() + 1;
    box_info->name = (gchar *)malloc(label_size);
    snprintf(
        box_info->name,
        label_size,
        "%s",
        labels_[detected_classes[i] + 1].c_str());
    box_info->confidence = detected_scores[i];
    meta->box_info = g_slist_append (meta->box_info, box_info);

    meta->bounding_box.x = static_cast<uint32_t>(
        detected_boxes[i * 4 + 1] * width * scale_ratio_x) + po_.x_offset;
    meta->bounding_box.y = static_cast<uint32_t>(
        detected_boxes[i * 4] * height * scale_ratio_y) + po_.y_offset;
    meta->bounding_box.width = static_cast<uint32_t>(
        detected_boxes[i * 4 + 3] * width * scale_ratio_x) + po_.x_offset -
            meta->bounding_box.x;
    meta->bounding_box.height = static_cast<uint32_t>(
        detected_boxes[i * 4 + 2] * height * scale_ratio_y) + po_.y_offset -
            meta->bounding_box.y;
  }
  MLE_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

int32_t TFLBase::PostProcess(GstBuffer* buffer) {
  MLE_LOGI("%s: Enter", __func__);

  if (tflite_params_.num_outputs == 4) {
    // post-processing the output results from 4 nodes
    if (PostProcessMultiOutput(buffer) != MLE_OK) {
      MLE_LOGE("%s: PostProcessMultiOutput Failed!!!", __func__);
      return MLE_FAIL;
    }
    MLE_LOGI("%s: Exit", __func__);
    return MLE_OK;
  }

  // For MobileNet model, return only top most confident object info
  int output = tflite_params_.interpreter->outputs()[0];
  auto top_results_count = 1;
  std::vector<std::pair<float, int>> top_results;

  // Sort output predictions in descending order of confidence
  // and then, return the top most confidence result
  bool verbose = false;
  switch (tflite_params_.interpreter->tensor(output)->type) {
    case kTfLiteFloat32:
      if (verbose) {
        float* temp_output =
            tflite_params_.interpreter->typed_output_tensor<float>(0);
        for (uint32_t i = 0; i < tflite_params_.num_predictions; ++i) {
          MLE_LOGI("%s: i - %d :: conf - %f", __func__, i, temp_output[i]);
        }
      }
      tflite::label_image::get_top_n<float>(
                  tflite_params_.interpreter->typed_output_tensor<float>(0),
                  tflite_params_.num_predictions, top_results_count,
                  config_.conf_threshold, &top_results, true);
      break;
    case kTfLiteUInt8:
      if (verbose) {
        uint8_t* temp_output =
            tflite_params_.interpreter->typed_output_tensor<uint8_t>(0);
        for (uint32_t i = 0; i < tflite_params_.num_predictions; ++i) {
          MLE_LOGI("%s: i - %d :: conf - %d", __func__, i, temp_output[i]);
        }
      }
      tflite::label_image::get_top_n<uint8_t>(
                  tflite_params_.interpreter->typed_output_tensor<uint8_t>(0),
                  tflite_params_.num_predictions, top_results_count,
                  config_.conf_threshold, &top_results, false);
      break;
    default:
      MLE_LOGE("%s: Invalid output tensor type %d", __func__,
                      tflite_params_.interpreter->tensor(output)->type);
      return MLE_FAIL;
      break;
  }
  MLE_LOGI("%s: Found %zu objects", __func__, top_results.size());

  // If found, return the label with most confidence level
  if (top_results.size() > 0) {
    const auto& result = top_results.front();
    const float confidence = result.first;
    const int index = result.second;
    if (confidence > config_.conf_threshold) {
      MLE_LOGI("%s: confidence %f, index %d, label %s", __func__,
                      confidence, index, labels_[index].c_str());

      GstMLClassificationMeta *meta =
          gst_buffer_add_classification_meta(buffer);
      if (!meta) {
        MLE_LOGE("Failed to create metadata");
        return MLE_NULLPTR;
      }

      meta->result.confidence = confidence;
      uint32_t label_size = labels_[index].size() + 1;
      meta->result.name = (gchar *)malloc(label_size);
      snprintf(meta->result.name, label_size, "%s",
               labels_[index].c_str());
    }
  }

  MLE_LOGI("%s: Exit", __func__);
  return MLE_OK;
}

}; // namespace mle
