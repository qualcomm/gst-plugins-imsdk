/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "c2-component.h"

#include <dlfcn.h>
#include <sstream>

#include <C2PlatformSupport.h>
#include <C2AllocatorGBM.h>

#define ALIGN(num, to) (((num) + (to - 1)) & (~(to - 1)))

std::shared_ptr<C2Factory::QC2ComponentStoreFactory> C2Factory::factory_ = nullptr;
std::mutex C2Factory::lock_;

template<typename ...Args> std::runtime_error Exception(Args&&... args) {

  std::stringstream s;
  ((s << std::forward<Args>(args)), ...);
  return std::runtime_error(s.str());
}

std::shared_ptr<C2GraphicBlock> C2GraphicMemory::Fetch(uint32_t width,
                                                       uint32_t height,
                                                       C2PixelFormat format) {

  if (width == 0 || height == 0) {
    throw Exception("One or more dimensions are 0 !");
  }

  uint32_t fmt = 0;
  C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
  std::shared_ptr<C2GraphicBlock> block;

  switch (format) {
    case C2PixelFormat::kNV12:
      fmt = GBM_FORMAT_NV12;
      break;
    case C2PixelFormat::kNV12UBWC:
      fmt = GBM_FORMAT_NV12;
      usage.expected |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
      break;
    case C2PixelFormat::kP010:
      fmt = GBM_FORMAT_YCbCr_420_P010_VENUS;
      break;
    case C2PixelFormat::kTP10UBWC:
      fmt = GBM_FORMAT_YCbCr_420_TP10_UBWC;
      usage.expected |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
      break;
    default:
      throw Exception("Failed to create C2Buffer! Unsupported format!");
  }

  auto status = pool_->fetchGraphicBlock(width, height, fmt, usage, &block);
  if (status != C2_OK) {
    throw Exception("Unable to create graphic block, error: ", status, " !");
  }

  return block;
}

std::shared_ptr<C2LinearBlock> C2LinearMemory::Fetch(uint32_t size) {

  if (size == 0) {
    throw Exception("Size is 0 !");
  }

  uint32_t capacity = ALIGN(size, 4096);
  C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
  std::shared_ptr<C2LinearBlock> block;

  auto status = pool_->fetchLinearBlock(capacity, usage, &block);
  if (status != C2_OK) {
    throw Exception("Unable to create linear block, error: ", status, " !");
  }

  return block;
}

C2Module::C2Module (std::shared_ptr<C2Component>& component)
    : component_(component),
      state_(State::kCreated),
      notifier_(nullptr) {

  // Get local pointer to the underlying component interface.
  interface_ = std::shared_ptr<C2ComponentInterface>(component_->intf());
}

C2Module::~C2Module () { }

c2_status_t C2Module::Initialize(std::shared_ptr<IC2Notifier>& notifier) {

  std::lock_guard<std::mutex> lk(lock_);
  auto listener = std::make_shared<C2Listener> (this);

  auto status = component_->setListener_vb (listener, C2_MAY_BLOCK);
  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Failed to set events listener, error ", status, "!");
  }

  if (!notifier) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Invalid notifier argument!");
  }

  notifier_ = notifier;
  state_ = State::kIdle;

#if defined(CODEC2_CONFIG_VERSION_2_0)
  bool decoding = (interface_->getName().find("decoder") != std::string::npos);

  if (!decoding) {
    // Output buffer pool for the encoder is not properly supported.
    return C2_OK;
  }

  // Create output graphic/linear pool for buffer circulation.
  ::android::C2PlatformAllocatorStore::id_t type =
      C2AllocatorStore::GRAPHIC_NON_CONTIGUOUS;

  std::shared_ptr<C2BlockPool> pool;
  status = ::android::CreateCodec2BlockPool(type, component_, &pool);

  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Unable to create output block pool, error: ", status, "!");
  }

  C2BlockPool::local_id_t id;

  graphic_mem_ = std::make_shared<C2GraphicMemory>(pool);
  id = graphic_mem_->GetLocalId();

  // Register the buffer pool ID so that it is used by the component.
  auto pools = C2PortBlockPoolsTuning::output::AllocUnique({id});
  auto param = C2Param::Copy(*pools);

  std::vector<std::unique_ptr<C2SettingResult>> failures;
  status = interface_->config_vb ({param.get()}, C2_MAY_BLOCK, &failures);

  if ((status != C2_OK) || !failures.empty()) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Failed to set parameter, error ", status, "!");
  }
#endif // CODEC2_CONFIG_VERSION_2_0

  return C2_OK;
}

std::shared_ptr<C2GraphicMemory> C2Module::GetGraphicMemory() {

  std::lock_guard<std::mutex> lk(lock_);

  if (!graphic_mem_) {
    std::shared_ptr<C2BlockPool> pool;

    auto status = ::android::GetCodec2BlockPool(
        C2AllocatorStore::DEFAULT_GRAPHIC, component_, &pool);

    if (status != C2_OK) {
      throw Exception("Component[", interface_->getName().c_str(), "]: "
          "Unable to get graphic block pool, error: ", status, "!");
    }

    graphic_mem_ = std::make_shared<C2GraphicMemory>(pool);
  }

  return graphic_mem_;
}

std::shared_ptr<C2LinearMemory> C2Module::GetLinearMemory() {

  std::lock_guard<std::mutex> lk(lock_);

  if (!linear_mem_) {
    std::shared_ptr<C2BlockPool> pool;

    auto status = ::android::GetCodec2BlockPool(
        C2AllocatorStore::DEFAULT_LINEAR, component_, &pool);

    if (status != C2_OK) {
      throw Exception("Component[", interface_->getName().c_str(), "]: "
          "Unable to get linear block pool, error: ", status, "!");
    }

    linear_mem_ = std::make_shared<C2LinearMemory>(pool);
  }

  return linear_mem_;
}

std::unique_ptr<C2Param> C2Module::QueryParam(C2Param::Index index) {

  std::vector<std::unique_ptr<C2Param>> params;
  auto status = interface_->query_vb({}, {index}, C2_MAY_BLOCK, &params);

  if ((status != C2_OK) || params.empty()) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Failed to query parameter, error ", status, "!");
  }

  return std::move(params.at(0));
}

c2_status_t C2Module::SetParam(std::unique_ptr<C2Param>& param) {

  std::vector<std::unique_ptr<C2SettingResult>> failures;
  auto status = interface_->config_vb ({param.get()}, C2_MAY_BLOCK, &failures);

  if ((status != C2_OK) || !failures.empty()) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Failed to set parameter, error ", status, "!");
  }

  return C2_OK;
}

c2_status_t C2Module::Start() {

  std::lock_guard<std::mutex> lk(lock_);

  if (state_ == State::kCreated) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Start failed! Not initialized!");
  } else if (state_ == State::kRunning) {
    return C2_OK;
  }

  auto status = component_->start();
  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Start failed, error ", status, "!");
  }

  state_ = State::kRunning;
  return C2_OK;
}

c2_status_t C2Module::Stop() {

  std::lock_guard<std::mutex> lk(lock_);

  if (state_ == State::kCreated) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Stop failed! Not initialized!");
  } else if (state_ == State::kIdle) {
    return C2_OK;
  }

  auto status = component_->stop();
  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Stop failed, error ", status, "!");
  }

  state_ = State::kIdle;
  return C2_OK;
}

c2_status_t C2Module::Flush(C2Component::flush_mode_t mode) {

  std::lock_guard<std::mutex> lk(lock_);

  if (state_ == State::kCreated) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Stop failed! Not initialized!");
  } else if (state_ == State::kIdle) {
    return C2_OK;
  }

  std::list<std::unique_ptr<C2Work>> witems;
  auto status = component_->flush_sm(mode, &witems);

  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Flush failed, error ", status, "!");
  }

  HandleWorkDone(std::move(witems));
  return C2_OK;
}

c2_status_t C2Module::Drain(C2Component::drain_mode_t mode) {

  std::lock_guard<std::mutex> lk(lock_);

  if (state_ == State::kCreated) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Drain failed! Not initialized!");
  } else if (state_ == State::kIdle) {
    return C2_OK;
  }

  auto status = component_->drain_nb(mode);
  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Drain failed, error ", status, "!");
  }

  return C2_OK;
}

c2_status_t C2Module::Queue(std::shared_ptr<C2Buffer>& buffer,
                                  std::list<std::unique_ptr<C2Param>>& settings,
                                  uint64_t index, uint64_t timestamp,
                                  uint32_t flags) {

  std::lock_guard<std::mutex> lk(lock_);

  if (state_ == State::kCreated) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Queue failed! Not initialized!");
  } else if (state_ != State::kRunning) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Queue failed! Not in running state!");
  }

  std::unique_ptr<C2Work> work = std::make_unique<C2Work> ();

  work->input.ordinal.frameIndex = index;
  work->input.ordinal.timestamp = timestamp;
  work->input.flags = static_cast<C2FrameData::flags_t>(flags);
  work->input.buffers.emplace_back(buffer);

  auto worklet = std::make_unique<C2Worklet>();

  for (; !settings.empty(); settings.pop_front()) {
    std::unique_ptr<C2Param>& param = settings.front();
    worklet->tunings.push_back(
        std::unique_ptr<C2Tuning>(reinterpret_cast<C2Tuning*>(param.release())));
  }

  work->worklets.emplace_back(std::move(worklet));

  std::list<std::unique_ptr<C2Work>> witems;
  witems.push_back(std::move(work));

  auto status = component_->queue_nb (&witems);
  if (status != C2_OK) {
    throw Exception("Component[", interface_->getName().c_str(), "]: "
        "Failed to queue work items, error ", status, "!");
  }

  return C2_OK;
}

void C2Module::HandleWorkDone(std::list<std::unique_ptr<C2Work>> witems) {

  while (!witems.empty()) {
    std::unique_ptr<C2Work> work = std::move(witems.front());
    witems.pop_front();

    if (!work || work->worklets.empty()) {
      // No work item or empty worklets, skip.
      continue;
    }

    const std::unique_ptr<C2Worklet>& worklet = work->worklets.front();
    C2FrameData::flags_t flags = worklet->output.flags;

    if (flags & C2FrameData::FLAG_END_OF_STREAM) {
      notifier_->EventHandler(C2EventType::kEOS, nullptr);
      continue;
    }

    if (flags & C2FrameData::FLAG_DROP_FRAME) {
      uint64_t index = worklet->output.ordinal.frameIndex.peeku();
      notifier_->EventHandler(C2EventType::kDrop, &index);
      continue;
    }

    // Process the worklets.
    if (work->workletsProcessed > 0 && !worklet->output.buffers.empty()) {
      auto buffer = worklet->output.buffers[0];
      uint64_t index = worklet->output.ordinal.frameIndex.peeku();
      uint64_t timestamp = worklet->output.ordinal.timestamp.peeku();

      notifier_->FrameAvailable(buffer, index, timestamp, flags);
    }
  }
}

void C2Module::HandleTripped(
    std::vector<std::shared_ptr<C2SettingResult>> results) {

  // Not implemented.
}

void C2Module::HandleError(uint32_t error) {

  notifier_->EventHandler(C2EventType::kError, &error);
}

C2Module* C2Factory::GetModule(std::string name) {

  std::lock_guard<std::mutex> lk(C2Factory::lock_);

  // Initialize Codec2 Store Factory.
  if (!factory_) {
    void* handle = dlopen("libqcodec2_core.so", RTLD_NOW);
    if (!handle || dlerror()) {
      throw std::runtime_error(dlerror() ? dlerror() : "dlopen failed!");
    }

    const char* method = "QC2ComponentStoreFactoryGetter";
    auto FactoryGetter = (QC2ComponentStoreFactoryGetter_t)dlsym(handle, method);

    if ((FactoryGetter == nullptr) || dlerror()) {
      dlclose(handle);
      throw std::runtime_error(dlerror() ? dlerror() : "dlsym failed!");
    }

    // Get version 1.0 of the Codec2 Store Factory.
    QC2ComponentStoreFactory* sfactory = (*FactoryGetter) (1, 0);
    if (sfactory == nullptr) {
      dlclose(handle);
      throw std::runtime_error("Unable to fetch Codec2 Store Factory!");
    }

    factory_ = std::shared_ptr<QC2ComponentStoreFactory>(sfactory,
        [handle](QC2ComponentStoreFactory* factory) {
          delete factory;
          dlclose(handle);
        }
    );
  }

  // Fetch an instance of the Codec2 store.
  std::shared_ptr<C2ComponentStore> store = factory_->getInstance();
  if (!store) {
    throw std::runtime_error("Unable to get Codec2 Store!");
  }

  // Create Codec2 component with the given name.
  std::shared_ptr<C2Component> component;
  auto status = store->createComponent(name, &component);

  if (status != C2_OK) {
    throw Exception("Unable to create Codec2 component '", name,
        "', error: ", status, " !");
  }

  return new C2Module(component);
}
