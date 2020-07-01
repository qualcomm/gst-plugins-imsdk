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
#include <dlfcn.h>
#include <math.h>

#include "nnengine.h"

int32_t NNEngine::EngineInit(const NNSourceInfo* source_info,
  int32_t* out_sizes)
{
  in_width_  = source_info->img_width;
  in_height_ = source_info->img_height;
  in_format_ = source_info->img_format;
  float ratio = (pad_width_ & ~0x1) * 1.0 / fmax(in_width_, in_height_);
  scale_width_ = (uint32_t)(in_width_ * ratio);
  scale_height_ = (uint32_t)(in_height_ * ratio);

  ALOGD("%s:%d: in: %dx%d scaled: %dx%d nn: %dx%d ", __func__, __LINE__,
    in_width_, in_height_, scale_width_, scale_height_,
    pad_width_, pad_height_);

  posix_memalign(reinterpret_cast<void**>(&scale_buf_), 128,
      ((scale_width_ * scale_height_ * 3) / 2));
  posix_memalign(reinterpret_cast<void**>(&rgb_buf_), 128,
      (scale_width_ * scale_height_ * 3));

  if ((nullptr == rgb_buf_) ||
      (nullptr == scale_buf_)) {
    ALOGE(" Buffer allocation failed");
    return NN_FAIL;
  }

  std::string lib = std::string(NNENGINE_LIB_DIR) + "/" + model_lib_;
  void *libptr = dlopen(lib.c_str(), RTLD_LAZY);
  if (!libptr) {
    ALOGE("%s: Error loading library : %s %s\n", __func__, lib.c_str(),
        dlerror());
    return -1;
  }

  InitGraph init_graph;
  *(void **)&(init_graph) = dlsym(libptr, "init_graph");

  int ret = nn_driver_.Init(&nn_input_buf_, pad_width_, pad_height_,
      num_outputs_, out_sizes, init_graph);

  dlclose(libptr);

  if (ret) {
    ALOGE(" NNDriver Init failed");
    return NN_FAIL;
  }

  // input buffer is RGB pad_width x pad_height
  // memset input buffer now to avoid it while padding
  memset(nn_input_buf_, 128, pad_width_ * pad_height_ * 3);

  return NN_OK;
}

int32_t NNEngine::EngineDeInit()
{
  if (future_.valid()) {
    std::future_status status;
    do {
      status = future_.wait_for(std::chrono::milliseconds(kTimeOut));
      if (status == std::future_status::deferred) {
          ALOGE("%s: Future wait deferred", __func__);
      } else if (status == std::future_status::timeout) {
          ALOGE("%s: Future wait timeout %d ms", __func__, kTimeOut);
      }
    } while (status != std::future_status::ready);
  }

  if (nullptr != rgb_buf_) {
    free(rgb_buf_);
    rgb_buf_ = nullptr;
  }

  if (nullptr != scale_buf_) {
    free(scale_buf_);
    scale_buf_ = nullptr;
  }

  nn_driver_.DeInit();

  return NN_OK;
}

int32_t NNEngine::PreProcess(NNFrameInfo* pFrameInfo)
{
  if (NN_FORMAT_NV12 != in_format_ &&
      NN_FORMAT_NV21 != in_format_) {
    ALOGE("%s: Format %d not supported", __func__, in_format_);
    return NN_FAIL;
  }

  // Final image should be stored in nn_input_buf_
  uint8_t* rgb_buf;
  if (scale_width_ != pad_width_ ||
      scale_height_ != pad_height_) {
    // with pading
    rgb_buf = rgb_buf_;
  } else {
    // no pading
    rgb_buf = nn_input_buf_;
  }

  if (scale_width_ != in_width_ ||
      scale_height_ != in_height_) {
    int32_t res = NN_OK;
    res = PreProcessScale(pFrameInfo->frame_data[0],
                          pFrameInfo->frame_data[1],
                          scale_buf_,
                          in_width_,
                          in_height_,
                          pFrameInfo->stride,
                          scale_width_,
                          scale_height_,
                          in_format_);
    if (NN_OK != res) {
      ALOGE("PreProcessScale failed due to unsupported image format");
      return res;
    }

    PreProcessColorConvertRGB(scale_buf_,
                              scale_buf_ + (scale_width_ * scale_height_),
                              rgb_buf,
                              scale_width_,
                              scale_height_,
                              in_format_);

  } else {
    PreProcessColorConvertRGB(pFrameInfo->frame_data[0],
                              pFrameInfo->frame_data[1],
                              rgb_buf,
                              scale_width_,
                              scale_height_,
                              in_format_);
  }

  if (nn_format_ == NN_FORMAT_BGR24) {
    PreProcessColorConvertBGR(rgb_buf,
                              rgb_buf,
                              scale_width_,
                              scale_height_);
  }

  if (scale_width_ != pad_width_ ||
      scale_height_ != pad_height_) {
    Pad(rgb_buf,
        scale_width_,
        scale_height_,
        pad_width_,
        pad_height_,
        nn_input_buf_);
  }

  return NN_OK;
}

int32_t NNEngine::Process(
    NNFrameInfo* frame_info, GstBuffer* gst_buffer, uint32_t frame_skip_count)
{
  static uint32_t count = 0;

  std::unique_lock<std::mutex> lock(process_lock_);

  if (count++ % frame_skip_count == 0) {
    // Pre-process
    {
      Timer t("Pre-process time");
      int ret = PreProcess(frame_info);
      if (ret) {
        ALOGE(" Pre-process failed");
        return NN_FAIL;
      }
    }

    // Inference
    {
      Timer t("Inference time");
      int ret = nn_driver_.Process(nn_input_buf_, outputs_);
      if (ret) {
        ALOGE(" Inference failed");
        return NN_FAIL;
      }
    }

    // Post-process
    {
      Timer t("Post-process time");
      int ret = PostProcess(outputs_);
      if (ret) {
        ALOGE(" Postprocess failed");
        return NN_FAIL;
      }
    }
  }

  // Fill ML Metadata
  {
    Timer t("Fill ML Metadata time");
    int ret = FillMLMeta(gst_buffer);
    if (ret) {
      ALOGE("Fill ML Metadata failed");
      return NN_FAIL;
    }
  }

  return NN_OK;
}

int32_t NNEngine::Process(
    NNFrameInfo* frame_info, GstBuffer* gst_buffer, bool online)
{
  int32_t ret;

  if (online) {
    ret = ProcessOnline(frame_info, gst_buffer);
  } else {
    ret = ProcessPerFrame(frame_info, gst_buffer);
  }

  return ret;
}

int32_t NNEngine::ProcessOnline(
    NNFrameInfo* frame_info, GstBuffer * gst_buffer)
{
  std::unique_lock<std::mutex> lock(process_lock_);

  // Pre-process
  {
    Timer t("Pre-process time");
    int ret = PreProcess(frame_info);
    if (ret) {
      ALOGE(" Pre-process failed");
      return NN_FAIL;
    }
  }

  // Inference
  {
    Timer t("Inference time");
    int ret = nn_driver_.Process(nn_input_buf_, outputs_);
    if (ret) {
      ALOGE(" Inference failed");
      return NN_FAIL;
    }
  }

  // Post-process
  {
    Timer t("Post-process time");
    int ret = PostProcess(outputs_, gst_buffer);
    if (ret) {
      ALOGE(" Postprocess failed");
      return NN_FAIL;
    }
  }

  return NN_OK;
}


int32_t NNEngine::ProcessPerFrame(
    NNFrameInfo* frame_info, GstBuffer * gst_buffer)
{
  std::unique_lock<std::mutex> lock(process_lock_);

  if (!IsRunning()) {

    // Pre-process
    {
      Timer t("Pre-process time");
      int ret = PreProcess(frame_info);
      if (ret) {
        ALOGE(" Pre-process failed");
        return NN_FAIL;
      }
    }

    future_ = std::async(std::launch::async, [&] {

      // Inference
      {
        Timer t("Inference time");
        int ret = nn_driver_.Process(nn_input_buf_, outputs_);
        if (ret) {
          ALOGE(" Inference failed");
        }
      }

      // Post-process
      {
        std::unique_lock<std::mutex> lock(result_lock_);
        Timer t("Post-process time");
        int ret = PostProcess(outputs_);
        if (ret) {
          ALOGE(" Postprocess failed");
        }
      }
    });
  }

  // Fill ML Metadata
  {
    std::unique_lock<std::mutex> lock(result_lock_);
    Timer t("Fill ML Metadata time");
    int ret = FillMLMeta(gst_buffer);
    if (ret) {
      ALOGE("Fill ML Metadata failed");
      return NN_FAIL;
    }
  }

  return NN_OK;
}

void NNEngine::Pad(
    uint8_t*       input_buf,
    const uint32_t input_width,
    const uint32_t input_height,
    const uint32_t pad_width,
    const uint32_t pad_height,
    uint8_t*       output_buf)
{
  // This API assume that buffer is already fill up with
  // pad value and only active area is copied.
  // This optimization reduce time ~10 times.
  for (int y = 0; y < input_height; y++) {
    for (int x = 0; x < 3 * input_width; x++) {
      uint32_t index_src = y * 3 * input_width + x;
      uint32_t index_dst = y * 3 * pad_width + x;
      output_buf[index_dst] = input_buf[index_src];
    }
  }
}

int32_t NNEngine::PreProcessScale(
  uint8_t*       pSrcLuma,
  uint8_t*       pSrcChroma,
  uint8_t*       pDst,
  const uint32_t srcWidth,
  const uint32_t srcHeight,
  const uint32_t srcStride,
  const uint32_t scaleWidth,
  const uint32_t scaleHeight,
  NNImgFormat    format)
{
  int32_t rc = NN_OK;
  if ((format == NN_FORMAT_NV12) || (format == NN_FORMAT_NV21)) {
    fcvScaleDownMNu8(pSrcLuma,
                     srcWidth,
                     srcHeight,
                     srcStride,
                     pDst,
                     scaleWidth,
                     scaleHeight,
                     0);
    fcvScaleDownMNu8(pSrcChroma,
                     srcWidth,
                     srcHeight/2,
                     srcStride,
                     pDst + (scaleWidth*scaleHeight),
                     scaleWidth,
                     scaleHeight/2,
                     0);
  } else {
    ALOGE("Unsupported format %d", (int)format);
    rc = NN_FAIL;
  }
  return rc;
}

void NNEngine::PreProcessColorConvertRGB(
    uint8_t*       pSrcLuma,
    uint8_t*       pSrcChroma,
    uint8_t*       pDst,
    const uint32_t width,
    const uint32_t height,
    NNImgFormat    format)
{
  if ((format == NN_FORMAT_NV12) || (format == NN_FORMAT_NV21)) {
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

void NNEngine::PreProcessColorConvertBGR(
    uint8_t*       pSrc,
    uint8_t*       pDst,
    const uint32_t width,
    const uint32_t height)
{
  fcvColorRGB888ToBGR888u8(pSrc,
                           width,
                           height,
                           0,
                           pDst,
                           0);
}

uint8_t NNEngine::ReadLabelsFiles(const std::string& file_name,
                                  std::vector<std::string>& result) {
  std::ifstream file(file_name);
  if (!file) {
    ALOGE("%s: Labels file %s not found!", __func__, file_name.c_str());
    return NN_FAIL;
  }
  result.clear();
  std::string line;
  while (std::getline(file, line)) {
    result.push_back(line);
  }
  const int padding = 16;
  while (result.size() % padding) {
    result.emplace_back();
  }
  return NN_OK;
}

