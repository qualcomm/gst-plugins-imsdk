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

#ifndef NNEGINE_H
#define NNEGINE_H

#include <vector>
#include <string>
#include <future>
#include <thread>
#include <time.h>

#include <utils/Log.h>
#include <ml-meta/ml_meta.h>
#include <fastcv/fastcv.h>

#include "nndriver.h"
#include "engines/common_utils.h"

#define DEEP_LAP_PATH_LEN 512

typedef enum {
  NN_FORMAT_INVALID = 0,
  NN_FORMAT_NV12,
  NN_FORMAT_NV21,
  NN_FORMAT_BGR24,
  NN_FORMAT_RGB24,
} NNImgFormat;

typedef enum {
  NN_OK = 0,
  NN_FAIL
} NNErrors;

typedef struct {
  uint8_t *frame_data[2];
  uint32_t stride;
} NNFrameInfo;

typedef struct {
  char data_folder[DEEP_LAP_PATH_LEN];
  char label_file[DEEP_LAP_PATH_LEN];
  NNImgFormat img_format;
  int32_t img_width;
  int32_t img_height;
} NNSourceInfo;

class Timer {
  std::string str;
  uint64_t begin;

  uint64_t GetMicroSeconds() {
    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    uint64_t microSeconds = (static_cast<uint32_t>(time.tv_sec) * 1000000ULL) +
                            (static_cast<uint32_t>(time.tv_nsec)) / 1000;
    return microSeconds;
  }

public:

  Timer (std::string s) : str(s) {
    begin = GetMicroSeconds();
  }

  ~Timer () {
    uint64_t end = GetMicroSeconds();
    NN_LOGV("%s: %" G_GINT64_FORMAT " us", str.c_str(), end - begin);
  }
};

class NNEngine {
public:

  virtual int32_t Init(const NNSourceInfo* pSourceInfo) = 0;

  int32_t Process(NNFrameInfo* frame_info, GstBuffer* gst_buffer,
                  uint32_t frame_skip_count);

  int32_t Process(NNFrameInfo* frame_info, GstBuffer* gst_buffer,
                  bool online);

  virtual void DeInit() = 0;

protected:

  NNEngine(const std::string &model_lib, uint32_t pad_width, uint32_t pad_height,
    uint32_t num_outputs, NNImgFormat nn_format)
             : model_lib_(model_lib),
               pad_width_(pad_width),
               pad_height_(pad_height),
               num_outputs_(num_outputs),
               nn_format_(nn_format),
               rgb_buf_(nullptr),
               scale_buf_(nullptr),
               nn_input_buf_(nullptr) {
    fcvSetOperationMode(FASTCV_OP_PERFORMANCE);
  }

  int32_t EngineInit(const NNSourceInfo* source_info, int32_t* out_sizes);

  int32_t EngineDeInit();

  int32_t ProcessOnline(NNFrameInfo* pFrameInfo, GstBuffer * gst_buffer);

  int32_t ProcessPerFrame(NNFrameInfo* pFrameInfo, GstBuffer * gst_buffer);

  int32_t PreProcess(NNFrameInfo* pFrameInfo);

  virtual int32_t PostProcess(void* outputs[],
                              GstBuffer * gst_buffer = nullptr) = 0;

  virtual int32_t FillMLMeta(GstBuffer * gst_buffer) = 0;

  bool IsRunning() {
    return future_.valid() &&
       (future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
  }

  void Pad(
      uint8_t*       input_buf,
      const uint32_t input_width,
      const uint32_t input_height,
      const uint32_t pad_width,
      const uint32_t pad_height,
      uint8_t*       output_buf);

  void PreProcessColorConvertRGB(
      uint8_t*       pSrcLuma,
      uint8_t*       pSrcChroma,
      uint8_t*       pDst,
      const uint32_t width,
      const uint32_t height,
      NNImgFormat    format);

  void PreProcessColorConvertBGR(
      uint8_t*       pSrc,
      uint8_t*       pDst,
      const uint32_t width,
      const uint32_t height);

  int32_t PreProcessScale(
      uint8_t*       pSrcLuma,
      uint8_t*       pSrcChroma,
      uint8_t*       pDst,
      const uint32_t srcWidth,
      const uint32_t srcHeight,
      const uint32_t srcStride,
      const uint32_t scaleWidth,
      const uint32_t scaleHeight,
      NNImgFormat    format);

  uint8_t ReadLabelsFiles(const std::string& file_name,
                          std::vector<std::string>& result);

  static const uint8_t     kMaxOut = 4;

  const std::string        model_lib_;
  uint32_t                 pad_width_;
  uint32_t                 pad_height_;
  uint32_t                 num_outputs_;

  NNImgFormat              in_format_;
  NNImgFormat              nn_format_;
  uint32_t                 in_width_;
  uint32_t                 in_height_;
  uint32_t                 scale_width_;
  uint32_t                 scale_height_;
  uint8_t*                 rgb_buf_;
  uint8_t*                 scale_buf_;
  uint8_t*                 nn_input_buf_;
  std::shared_ptr<NNDriver> nn_driver_;
  void *                   outputs_[kMaxOut];

  static const uint32_t     kTimeOut = 1000;

  std::future<void>        future_;
  std::mutex               process_lock_;
  std::mutex               result_lock_;
};

#endif // NNEGINE_H
