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

#include <vector>
#include <string>
#include <ml-meta/ml_meta.h>
#include "common_utils.h"

namespace mle {

enum MLEImageFormat {
  mle_format_invalid = 0,
  mle_format_yv12,
  mle_format_nv12,
  mle_format_nv21,
  mle_format_YUVJ420P,
  mle_format_YUVJ422P,
  mle_format_YUVJ444P,
  mle_format_GRAY8,
  mle_format_RGB24,
  mle_format_JPEG,
};

enum MLEErrors {
  MLE_OK = 0,
  MLE_FAIL,
  MLE_NULLPTR,
  MLE_IMG_FORMAT_NOT_SUPPORTED
};

enum class FrameworkType {
  kSNPE = 0,
  kTFLite
};

enum class RuntimeType {
  CPU = 0,
  DSP,
};

enum class InputFormat {
  kRgb = 0,
  kBgr,
  kRgbFloat,
  kBgrFloat
};

enum class BufferType {
  kOutput = 0,
  kInput
};

enum class NetworkIO {
  kUserBuffer = 0,
  kITensor
};

enum class EngineOutput {
  kSingle = 0,
  kMulti,
  kSqueezenet,
  kSingleSSD
};

enum class PreprocessingMode {
  kKeepAR = 0,
  kKeepFOV,
  kDirectDownscale,
  kMax
};

struct PreprocessingOffsets {
  PreprocessingOffsets(): x_offset(0),
                          y_offset(0),
                          width(0),
                          height(0) {};
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t width;
  uint32_t height;
};

struct MLEInputParams {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t scanline;
  MLEImageFormat format;
};

struct SourceFrame {
  uint8_t *frame_data[2];
};

struct MLConfig {

  //applicable to SNPE
  EngineOutput engine_output;
  NetworkIO io_type;

  //Input image format for the desired network
  InputFormat input_format;

  //Aspect ratio maintenance
  PreprocessingMode preprocess_mode;

  // normalization
  float blue_mean;
  float blue_sigma;
  float green_mean;
  float green_sigma;
  float red_mean;
  float red_sigma;
  uint32_t use_norm;
  // end normalization

  float conf_threshold;
  std::string model_file;
  std::string labels_file;

  //runtime
  RuntimeType runtime;

  //tflite specific
  uint32_t number_of_threads;
  uint32_t use_nnapi;

  //snpe layers
  std::string input_layer;
  std::vector<std::string> output_layers;
  std::vector<std::string> result_layers;
};

class MLEngine {
 public:
  MLEngine(){};
  virtual ~MLEngine(){};
  virtual int32_t Init(const MLEInputParams* source_info) = 0;
  virtual void Deinit() = 0;
  virtual int32_t Process(struct SourceFrame* frame_info,
                          GstBuffer* buffer) = 0;
 protected:
  void DumpFrame(const uint8_t* buffer, const uint32_t& width,
      const uint32_t& height, const uint32_t& size, const std::string& suffix) {

    std::string file_path("/data/misc/camera/ml_engine_");
    size_t written_len = 0;
    file_path += std::to_string(width);
    file_path += "x";
    file_path += std::to_string(height);
    file_path += suffix;
    FILE *file = fopen(file_path.c_str(), "w+");
    if (!file) {
      VAM_ML_LOGE("%s: Unable to open file(%s)", __func__,
          file_path.c_str());
      goto FAIL;
    }
    written_len = fwrite(buffer, sizeof(uint8_t), size, file);
    VAM_ML_LOGD("%s: written_len: %d", __func__, written_len);
    if (size != written_len) {
      VAM_ML_LOGE("%s: Bad Write error (%d):(%s)", __func__, errno,
          strerror(errno));
      goto FAIL;
    }
    VAM_ML_LOGD("%s: Buffer Size:%u Stored:%s", __func__, written_len,
      file_path.c_str());

  FAIL:
    if (file != nullptr) {
      fclose(file);
    }
  }

  MLConfig config_;
  PreprocessingOffsets po_;
};

}; // namespace mle
