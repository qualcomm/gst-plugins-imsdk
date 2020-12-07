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

#pragma once

#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <memory>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>

#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/model.h>

#include "ml_engine_intf.h"

namespace mle {

using TfLiteDelegatePtr = tflite::Interpreter::TfLiteDelegatePtr;
using TfLiteDelegatePtrMap = std::map<std::string, TfLiteDelegatePtr>;

struct TFLiteEngineParams {
  std::unique_ptr<tflite::FlatBufferModel> model;
  std::unique_ptr<tflite::Interpreter> interpreter;
  uint32_t num_inputs;
  uint32_t num_outputs;
  uint32_t num_predictions;
};

class TFLBase : public MLEngine {
 public:
  TFLBase(MLConfig &config);
  ~TFLBase() {};

 private:
  int32_t LoadModel(std::string& model_path);
  void TFliteRuntimeRun();
  int32_t InitFramework();
  int32_t ExecuteModel();
  int32_t ValidateModelInfo();
  void* GetInputBuffer();
  void Deinit();
  int32_t PostProcessMultiOutput(GstBuffer* buffer);
  int32_t PostProcess(GstBuffer* buffer);
  TfLiteDelegatePtrMap GetDelegates();
  std::thread tflite_thread_;
  std::atomic<bool> process_frame_;
  std::atomic<bool> frame_ready_;
  std::condition_variable processed_cv_;
  std::mutex processed_mutex_;
  std::condition_variable config_cv_;
  std::mutex config_mutex_;
  std::condition_variable inference_cv_;
  std::mutex inference_mutex_;
  std::atomic<int32_t> status_code_;
  int32_t invoke_fail_count_;

 protected:
  TFLiteEngineParams tflite_params_;
};

}; // namespace mle
