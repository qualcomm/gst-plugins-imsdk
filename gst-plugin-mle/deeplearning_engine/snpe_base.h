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

#include <cstdlib>
#include <vector>
#include <string>
#include <iterator>
#include <stdlib.h>
#include <thread>
#include <memory>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <fcntl.h>
#include <condition_variable>
#include <mutex>
#include <linux/msm_ion.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "DlContainer/IDlContainer.hpp"
#include "SNPE/SNPE.hpp"
#include "SNPE/SNPEFactory.hpp"
#include "SNPE/SNPEBuilder.hpp"
#include "DlSystem/DlError.hpp"
#include "DlSystem/ITensorFactory.hpp"
#include "DlSystem/TensorMap.hpp"
#include "DlSystem/TensorShape.hpp"
#include "DlSystem/StringList.hpp"
#include "DlSystem/DlError.hpp"
#include "DlSystem/IUserBuffer.hpp"
#include "DlSystem/IUserBufferFactory.hpp"
#include "DlSystem/UserBufferMap.hpp"
#include "DlSystem/IBufferAttributes.hpp"
#include "common_utils.h"
#include "ml_engine_intf.h"

namespace mle {

struct IONBuffer {
  uint8_t* addr = nullptr;
  float* addr_f = nullptr;
  uint32_t size;
  int32_t fd;
  int32_t handle;
  bool cached;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
#ifdef QMMF_ALG
  void GetAlgBuffer(qmmf::qmmf_alg_plugin::AlgBuffer* buf,
                    const InputFormat& format);
#endif
};

struct InitParams {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t scanline;
  MLEImageFormat format;
  float conf_threshold;
#ifndef QMMF_ALG
  uint8_t* bgr_buf;
#endif // !QMMF_ALG
};

struct SNPEParams {
  std::unique_ptr<zdl::DlContainer::IDlContainer> container;
  std::unique_ptr<zdl::SNPE::SNPE> snpe;

  std::vector<std::unique_ptr<zdl::DlSystem::IUserBuffer>> ub_list;
  zdl::DlSystem::UserBufferMap output_ub_map;
  zdl::DlSystem::UserBufferMap input_ub_map;

  std::vector<std::unique_ptr<zdl::DlSystem::ITensor>> tensor_list;
  zdl::DlSystem::TensorMap output_tensor_map;
  zdl::DlSystem::TensorMap input_tensor_map;

  std::unordered_map<std::string, IONBuffer> in_heap_map;
  std::unordered_map<std::string, IONBuffer> out_heap_map;
};

#ifdef QMMF_ALG
class SNPEBase : public qmmf::qmmf_alg_plugin::IEventListener, public MLTools,
                 public MLEngine {
#else
class SNPEBase : public MLEngine {
#endif
 public:
  SNPEBase(MLConfig &config);
  virtual ~SNPEBase(){};
  int32_t Init(const struct MLEInputParams* source_info);
  void Deinit();
  virtual int32_t Process(struct SourceFrame* frame_info, GstBuffer* buffer);

 protected:
  int32_t PreProcessBuffer(const struct SourceFrame* frame_info);
  void PrintErrorStringAndExit();

  IONBuffer AllocateBuffer(const uint32_t& size,
                           const InputFormat& input_format);
  void ReleaseBuffer(const IONBuffer& buf);

  int32_t ExecuteSNPE();
  virtual int32_t EnginePostProcess(GstBuffer* buffer);

  std::vector<std::string> labels_;
  int32_t ion_device_;

  uint32_t scale_stride_;

  uint32_t scale_width_;
  uint32_t scale_height_;
  InitParams init_params_;
  SNPEParams snpe_params_;
  zdl::DlSystem::Runtime_t runtime_;
  zdl::DlSystem::Version_t version_;

 private:
  int32_t ConfigureRuntime(MLConfig &config);
  int32_t ConfigureDimensions();
  std::unique_ptr<zdl::DlContainer::IDlContainer> LoadContainerFromFile(
      std::string container_path);
  std::unique_ptr<zdl::SNPE::SNPE> SetBuilderOptions();
  virtual size_t CalculateSizeFromDims(const size_t rank,
                                       const zdl::DlSystem::Dimension* dims,
                                       const size_t& element_size);
  virtual std::vector<size_t> GetStrides(zdl::DlSystem::TensorShape dims,
                                         const size_t& element_size);

  int32_t PopulateMap(BufferType type);
  int32_t CreateUserBuffer(BufferType type, const char* name);
  int32_t CreateTensor(BufferType type, const char* name);
  int32_t InitSNPE();

#ifdef QMMF_ALG
  void InitAlgo();
  void DeinitAlgo();
  void *algo_lib_handle_;
  qmmf::qmmf_alg_plugin::IAlgPlugin *algo_;
  std::map<int32_t, qmmf::qmmf_alg_plugin::AlgBuffer> alg_input_buffers_map_;
  std::vector<qmmf::qmmf_alg_plugin::AlgBuffer> alg_output_buffers_;
  std::condition_variable signal_;
  std::mutex lock_;
  bool algo_process_done_;
  std::string AlgoConfiguration() const;
  int32_t GetOffsets(const std::string& conf_data);

  // qmmf::qmmf_alg_plugin::IEventListener methods
  void OnFrameReady(const qmmf::qmmf_alg_plugin::AlgBuffer &output_buffer);
  void OnFrameProcessed(const qmmf::qmmf_alg_plugin::AlgBuffer &input_buffer) {};
  void OnError(qmmf::qmmf_alg_plugin::RuntimeError err) {};
#else
  int32_t ColorConvert();
  int32_t ScaleImage(uint8_t* input_buf, uint8_t* scaled_buf, const uint32_t width,
                  const uint32_t height, const uint32_t scaled_width,
                  const uint32_t scaled_height);
  void MeanSubtract(uint8_t* input_buf, const uint32_t width,
                    const uint32_t height, float* processed_buf);
  IONBuffer scale_ion_buffer_;
#endif
};

}; // namespace mle
