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

#pragma once

#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <memory>

#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/model.h>

#include "ml_engine_intf.h"
#include "common_utils.h"

namespace mle {

struct TFLiteEngineInputParams {
  uint32_t width;
  uint32_t height;
  MLEImageFormat format;
  uint8_t* rgb_buf;
};

struct TFLiteEngineParams {
  uint32_t width;
  uint32_t height;
  uint32_t channels;
  MLEImageFormat format;
  bool do_rescale;
  std::unique_ptr<tflite::FlatBufferModel> model;
  std::unique_ptr<tflite::Interpreter> interpreter;
  uint32_t num_inputs;
  uint32_t num_outputs;
  uint32_t num_predictions;
  uint8_t* input_buffer;
  float* input_buffer_f;
  std::vector<std::string> labels;
  size_t label_count;
};

class TFLBase : public MLEngine {
 public:
  TFLBase(MLConfig &config);
  ~TFLBase() {};
  int32_t Init(const struct MLEInputParams* source_info);
  void Deinit();
  int32_t Process(struct SourceFrame* frame_info, GstBuffer* buffer);

 private:
  int32_t ValidateModelInfo();
  int32_t PreProcessInput(SourceFrame* frame_info);
  int32_t PostProcessMultiOutput(GstBuffer* buffer);
  int32_t PostProcessOutput(GstBuffer* buffer);
  TfLiteStatus ReadLabelsFile(const std::string& file_name,
                              std::vector<std::string>& result,
                              size_t& found_label_count);

 protected:
  TFLiteEngineInputParams input_params_;
  TFLiteEngineParams engine_params_;
};

}; // namespace mle
