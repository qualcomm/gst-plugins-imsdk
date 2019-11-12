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
#include <ion/ion.h>
#include <linux/dma-buf.h>
#include <linux/msm_ion.h>

#include <vector>
#include <memory>
#include <mutex>
#include <sstream>
#include <chrono>
#include <json/json.h>

#include "snpe_base.h"

#ifdef QMMF_ALG
using namespace qmmf::qmmf_alg_plugin;
#else
#include "fastcv/fastcv.h"
#include <string>
#include <media/msm_media_info.h>
#endif
namespace mle {
#ifdef QMMF_ALG
static const auto kOneFrameProcessTimeout = std::chrono::milliseconds(10);
#endif

static const int kBufferAlign                 = 4096;

SNPEBase::SNPEBase(MLConfig &config) : MLEngine() {
  fcvSetOperationMode(FASTCV_OP_PERFORMANCE);
  ConfigureRuntime(config);
  config_.io_type = config.io_type;
  config_.input_format = config.input_format;
  config_.preprocess_mode = config.preprocess_mode;
  config_.blue_mean = config.blue_mean;
  config_.blue_sigma = config.blue_sigma;
  config_.green_mean = config.green_mean;
  config_.green_sigma = config.green_sigma;
  config_.red_mean = config.red_mean;
  config_.red_sigma = config.red_sigma;
  config_.use_norm = config.use_norm;
  config_.model_file = config.model_file;
  config_.labels_file = config.labels_file;
  config_.output_layers = config.output_layers;
  config_.result_layers = config.result_layers;

  init_params_.conf_threshold = config.conf_threshold;
}

int32_t SNPEBase::ConfigureRuntime(MLConfig &config) {
  version_ = zdl::SNPE::SNPEFactory::getLibraryVersion();

  switch (config.runtime) {
    case RuntimeType::DSP: {
      if (zdl::SNPE::SNPEFactory::isRuntimeAvailable(
          zdl::DlSystem::Runtime_t::DSP)) {
        runtime_ = zdl::DlSystem::Runtime_t::DSP;
        VAM_ML_LOGI("DSP runtime selected");
      } else {
        runtime_ = zdl::DlSystem::Runtime_t::CPU;
        VAM_ML_LOGI("CPU runtime selected, but DSP was configured");
      }
      break;
    }
    case RuntimeType::CPU: {
      runtime_ = zdl::DlSystem::Runtime_t::CPU;
      VAM_ML_LOGI("CPU runtime selected");
      break;
    }
  }
  return MLE_OK;
}

int32_t SNPEBase::ConfigureDimensions() {
  zdl::DlSystem::Optional <zdl::DlSystem::StringList> names_opt;
  names_opt = snpe_params_.snpe->getInputTensorNames();
  const zdl::DlSystem::StringList& names = *names_opt;
  const char * name = names.at(0);
  auto uba_opt = snpe_params_.snpe->getInputOutputBufferAttributes(name);
  const zdl::DlSystem::TensorShape& buffer_shape = (*uba_opt)->getDims();
  const zdl::DlSystem::Dimension* dims = buffer_shape.getDimensions();

  scale_height_ = dims[1];
  scale_width_ = dims[2];
  config_.input_layer = static_cast<std::string>(names.at(0));

  scale_stride_ = scale_width_ * 3 * 4;

  if ((config_.input_format == InputFormat::kBgr) ||
      (config_.input_format == InputFormat::kRgb)) {
    scale_stride_ = scale_width_ * 3;
  }

  return MLE_OK;
}

std::unique_ptr<zdl::SNPE::SNPE> SNPEBase::SetBuilderOptions() {
  VAM_ML_LOGI("%s: Enter", __func__);
  std::unique_ptr <zdl::SNPE::SNPE> snpe;
  zdl::SNPE::SNPEBuilder snpeBuilder(snpe_params_.container.get());
  zdl::DlSystem::StringList output_layers;

  for (size_t i = 0; i < config_.output_layers.size(); i++) {
    output_layers.append(config_.output_layers[i].c_str());
  }

  if (config_.io_type == NetworkIO::kUserBuffer) {
    snpe =
        snpeBuilder.setOutputLayers(output_layers).setRuntimeProcessor(runtime_)
            .setUseUserSuppliedBuffers(true).setCPUFallbackMode(true).build();
  } else if (config_.io_type == NetworkIO::kITensor) {
    snpe =
        snpeBuilder.setOutputLayers(output_layers).setRuntimeProcessor(runtime_)
            .setUseUserSuppliedBuffers(false).setCPUFallbackMode(true).build();
  } else {
    VAM_ML_LOGE("%s: Invalid Network IO value", __func__);
    throw std::runtime_error("Invalid Network IO value");
  }

  VAM_ML_LOGI("%s: Exit", __func__);
  return snpe;
}

std::unique_ptr<zdl::DlContainer::IDlContainer> SNPEBase::LoadContainerFromFile(
    std::string container_path) {
  std::unique_ptr<zdl::DlContainer::IDlContainer> container;
  container = zdl::DlContainer::IDlContainer::open(container_path);
  if (nullptr == container) {
    VAM_ML_LOGE("%s: Container loading failed", __func__);
    return nullptr;
  }

  return container;
}

int32_t SNPEBase::PopulateMap(BufferType type) {
  int32_t result = MLE_OK;
  zdl::DlSystem::Optional <zdl::DlSystem::StringList> names_opt;

  switch (type) {
    case BufferType::kInput:
      names_opt = snpe_params_.snpe->getInputTensorNames();
      break;
    case BufferType::kOutput:
      names_opt = snpe_params_.snpe->getOutputTensorNames();
      break;
    default:
      VAM_ML_LOGE("Error obtaining tensor names");
      throw std::runtime_error("Error obtaining tensor names");
  }

  const zdl::DlSystem::StringList& names = *names_opt;
  for (const char *name : names) {
    if (config_.io_type == NetworkIO::kUserBuffer) {
      result = CreateUserBuffer(type, name);
    } else if (config_.io_type == NetworkIO::kITensor) {
      result = CreateTensor(type, name);
    } else {
      VAM_ML_LOGE("Invalid Network IO value %d", static_cast<int32_t>(config_.io_type));
      result = MLE_FAIL;
    }

    if (MLE_OK != result) {
      break;
    }
  }
  return result;
}

int32_t SNPEBase::CreateUserBuffer(BufferType type, const char * name) {
  zdl::DlSystem::IUserBufferFactory& ub_factory =
      zdl::SNPE::SNPEFactory::getUserBufferFactory();
  zdl::DlSystem::UserBufferEncodingFloat ub_encoding_float;
  zdl::DlSystem::UserBufferEncodingTf8 *ub_encoding_uint8 =
      new zdl::DlSystem::UserBufferEncodingTf8(0, 1 / 128);

  auto uba_opt = snpe_params_.snpe->getInputOutputBufferAttributes(name);
  if (!uba_opt) {
    throw std::runtime_error(
        std::string("Error obtaining attributes for tensor ") + name);
  }
  const zdl::DlSystem::TensorShape& buffer_shape = (*uba_opt)->getDims();

  size_t elem_size = sizeof(uint8_t);
  if ((config_.input_format == InputFormat::kBgrFloat) ||
      (config_.input_format == InputFormat::kRgbFloat)) {
    elem_size = sizeof(float);
  }

  size_t buf_size = CalculateSizeFromDims(buffer_shape.rank(),
                                          buffer_shape.getDimensions(),
                                          elem_size);

  auto *heap_map = &snpe_params_.in_heap_map;
  auto *ub_map = &snpe_params_.input_ub_map;
  if (type == BufferType::kOutput) {
    heap_map = &snpe_params_.out_heap_map;
    ub_map = &snpe_params_.output_ub_map;
  }

  IONBuffer ion_buf = AllocateBuffer(buf_size, config_.input_format);
  if (nullptr == ion_buf.addr && nullptr == ion_buf.addr_f) {
    VAM_ML_LOGE(" Buffer allocation failed");
    ReleaseBuffer(ion_buf);
    return MLE_FAIL;
  }

  heap_map->emplace(name, ion_buf);

  if ((config_.input_format == InputFormat::kBgr) ||
      (config_.input_format == InputFormat::kRgb)) {
    snpe_params_.ub_list.push_back(ub_factory.createUserBuffer(
        ion_buf.addr, buf_size,
        GetStrides((*uba_opt)->getDims(), elem_size), ub_encoding_uint8));
    ub_map->add(name, snpe_params_.ub_list.back().get());
    PrintErrorStringAndExit();
  } else {
    snpe_params_.ub_list.push_back(ub_factory.createUserBuffer(
        ion_buf.addr_f, buf_size,
        GetStrides((*uba_opt)->getDims(), elem_size), &ub_encoding_float));
    ub_map->add(name, snpe_params_.ub_list.back().get());
    PrintErrorStringAndExit();
  }

  return MLE_OK;
}

int32_t SNPEBase::CreateTensor(BufferType type, const char* name) {
  zdl::DlSystem::ITensorFactory& tensor_factory =
      zdl::SNPE::SNPEFactory::getTensorFactory();

  auto tensor_opt = snpe_params_.snpe->getInputOutputBufferAttributes(name);
  if (!tensor_opt) {
    throw std::runtime_error(
        std::string("Error obtaining attributes for tensor ") + name);
  }
  const zdl::DlSystem::TensorShape& tensor_shape = (*tensor_opt)->getDims();
  size_t buf_size = CalculateSizeFromDims(tensor_shape.rank(),
                                          tensor_shape.getDimensions(),
                                          sizeof(float));

  auto *heap_map = &snpe_params_.in_heap_map;
  auto *tensor_map = &snpe_params_.input_tensor_map;
  if (type == BufferType::kOutput) {
    heap_map = &snpe_params_.out_heap_map;
    tensor_map = &snpe_params_.output_tensor_map;
  }

  IONBuffer ion_buf = AllocateBuffer(buf_size, config_.input_format);
  if (nullptr == ion_buf.addr && nullptr == ion_buf.addr_f) {
    VAM_ML_LOGE(" Buffer allocation failed");
    ReleaseBuffer(ion_buf);
    return MLE_FAIL;
  }

  heap_map->emplace(name, ion_buf);
  snpe_params_.tensor_list.push_back(tensor_factory.createTensor(tensor_shape));
  tensor_map->add(name, snpe_params_.tensor_list.back().get());

  return MLE_OK;
}

size_t SNPEBase::CalculateSizeFromDims(const size_t rank,
                                       const zdl::DlSystem::Dimension* dims,
                                       const size_t& element_size) {
  if (0 == rank) {
    return 0;
  }
  size_t size = element_size;
  for (size_t i = 0; i < rank; i++) {
    size *= dims[i];
  }
  return size;
}

std::vector<size_t> SNPEBase::GetStrides(zdl::DlSystem::TensorShape dims,
                                         const size_t& element_size) {
  std::vector<size_t> strides(dims.rank());
  strides[strides.size() - 1] = element_size;
  size_t stride = strides[strides.size() - 1];

  for (size_t i = dims.rank() - 1; i > 0; i--) {
    stride *= dims[i];
    strides[i - 1] = stride;
  }

  return strides;
}

int32_t SNPEBase::PreProcessBuffer(const SourceFrame* frame_info) {
#ifdef QMMF_ALG
  IONBuffer *ion_buf = &snpe_params_.in_heap_map[config_.input_layer.c_str()];

  if (!ion_buf) {
    VAM_ML_LOGE("%s: SNPE buffer is null", __func__);
    return VAM_NULLPTR;
  }

  std::vector<AlgBuffer> inb;
  if (alg_input_buffers_map_.find(frame_info->fd) ==
        alg_input_buffers_map_.end()) {
    AlgBuffer in_buf;
    BufferPlane plane0(init_params_.width,
                       init_params_.height,
                       init_params_.stride,
                       0,
                       init_params_.stride * init_params_.scanline);

    BufferPlane plane1(init_params_.width,
                       init_params_.height / 2,
                       init_params_.stride,
                       plane0.length_,
                       init_params_.stride * init_params_.scanline / 2);

    in_buf.plane_.push_back(plane0);
    in_buf.plane_.push_back(plane1);
    in_buf.cached_ = false;
    in_buf.pix_fmt_ = PixelFormat::kNv12;
    in_buf.size_ = plane0.length_ + plane1.length_;
    in_buf.vaddr_ = frame_info->frame_data[0];
    in_buf.fd_ = frame_info->fd;

    alg_input_buffers_map_[frame_info->fd] = in_buf;
    inb.push_back(in_buf);
    algo_->RegisterInputBuffers(inb);
  } else {
    inb.push_back(alg_input_buffers_map_[frame_info->fd]);
  }

  if (alg_output_buffers_.size() == 0) {
    AlgBuffer algBuf;
    ion_buf->GetAlgBuffer(&algBuf, config_.input_format);
    alg_output_buffers_.push_back(algBuf);
    algo_->RegisterOutputBuffers(alg_output_buffers_);
  }

  {
    std::unique_lock<std::mutex> l(lock_);
    algo_process_done_ = false;
  }
  try {
    algo_->Process(inb, alg_output_buffers_);
  }  catch (const std::exception &e) {
    VAM_ML_LOGE("Process failed: %s", e.what());
    return MLE_FAIL;
  }

  {
    std::unique_lock<std::mutex> l(lock_);
    auto rc = signal_.wait_for(l, kOneFrameProcessTimeout, [&] {
      return algo_process_done_;
    });
    if (!rc) {
      VAM_ML_LOGE("algorithm process timed out");
      return MLE_FAIL;
    }
  }

  std::string conf_data;
  algo_->GetConfig(conf_data);

  int32_t ret = GetOffsets(conf_data);
  if (MLE_OK != ret) {
    VAM_ML_LOGE("Invalid configuration data.");
    return MLE_FAIL;
  }

  if (config_.io_type == NetworkIO::kITensor) {
    auto *tensor =
        snpe_params_.input_tensor_map.getTensor(config_.input_layer.c_str());
    if ((config_.input_format == InputFormat::kBgr) ||
        (config_.input_format == InputFormat::kRgb)) {
      if (ion_buf->addr != nullptr) {
        uint8_t *t = ion_buf->addr;
        for (auto it = tensor->begin(); it != tensor->end(); it++, t++) {
          *it = *t;
        }
      }
    } else {
      if (ion_buf->addr_f != nullptr) {
        float *t = ion_buf->addr_f;
        for (auto it = tensor->begin(); it != tensor->end(); it++, t++) {
          *it = *t;
        }
      }
    }
  }
#else
  float *snpe_buf = snpe_params_.in_heap_map[config_.input_layer.c_str()].addr_f;
  if (!snpe_buf) {
    VAM_ML_LOGE("%s: SNPE buffer is null", __func__);
    return MLE_NULLPTR;
  }

  if ((scale_width_ != init_params_.width) ||
      (scale_height_ != init_params_.height)) {
    ScaleImage(frame_info->frame_data[0], scale_ion_buffer_.addr,
               init_params_.width, init_params_.height, scale_width_,
               scale_height_);
    ColorConvert();
    MeanSubtract(init_params_.bgr_buf, scale_width_, scale_height_, snpe_buf);
  } else {
    // No scale down.
    // TODO: color convert without scale needs more refactor.
    ColorConvert();
    MeanSubtract(init_params_.bgr_buf, scale_width_, scale_height_, snpe_buf);
  }

  if (config_.io_type == NetworkIO::kITensor) {
  //   auto *input_vec = &snpe_params_.in_heap_map[config_.input_layers.c_str()];
  //   auto tensor_size = snpe_params_.input_tensor_map.getTensor(
  //       config_.input_layers.c_str())->getSize();
  //   if (input_vec->size() == tensor_size) {
  //     std::copy(
  //         input_vec->begin(),
  //         input_vec->end(),
  //         snpe_params_.input_tensor_map.getTensor(config_.input_layers.c_str())->begin());
  //   } else {
  //     VAM_ML_LOGE("%s: Invalid tensor size %d", __func__, tensor_size);

  //   //temporary disable Itensor support
  //     return MLE_FAIL;
  //   }
  }
#endif
  return MLE_OK;
}

#ifndef QMMF_ALG
int32_t SNPEBase::ColorConvert() {
  if ((init_params_.format != mle_format_nv12) ||
    (init_params_.format != mle_format_nv12)) {
    VAM_ML_LOGE("%s: Color convert not supported!", __func__);
    return MLE_FAIL;
  }

  uint8_t *src_buffer_y       = scale_ion_buffer_.addr;
  size_t src_plane_y_stride   = VENUS_Y_STRIDE(COLOR_FMT_NV12,
                                    scale_ion_buffer_.width);
  size_t src_plane_uv_stride  = VENUS_UV_STRIDE(COLOR_FMT_NV12,
                                    scale_ion_buffer_.width);
  size_t src_plane_y_scanline = VENUS_Y_SCANLINES(COLOR_FMT_NV12,
                                    scale_ion_buffer_.height);
  size_t src_plane_y_len      = src_plane_y_stride * src_plane_y_scanline;
  uint8_t *src_buffer_uv      = src_buffer_y + src_plane_y_len;
  fcvColorYCbCr420PseudoPlanarToRGB888u8(src_buffer_y, src_buffer_uv,
                                         scale_ion_buffer_.width,
                                         scale_ion_buffer_.height,
                                         src_plane_y_stride,
                                         src_plane_uv_stride,
                                         init_params_.bgr_buf, 0);
  if ((config_.input_format == InputFormat::kBgr) ||
      (config_.input_format == InputFormat::kBgrFloat)) {
    uint8_t *bgr_buf = init_params_.bgr_buf;
    fcvColorRGB888ToBGR888u8(init_params_.bgr_buf, init_params_.width,
                             init_params_.height, 0, bgr_buf, 0);
  }
  return MLE_OK;
}

int32_t SNPEBase::ScaleImage(uint8_t* input_buf, uint8_t* scaled_buf,
                            const uint32_t src_width, const uint32_t src_height,
                            const uint32_t scaled_width,
                            const uint32_t scaled_height) {
  uint8_t *src_buffer_y, *src_buffer_uv;
  size_t src_stride_y, src_plane_y_len;

  src_buffer_y    = input_buf;
  src_stride_y    = init_params_.stride;
  src_plane_y_len = src_stride_y * init_params_.scanline;
  src_buffer_uv   = src_buffer_y + src_plane_y_len;

  uint8_t *dst_buffer_y, *dst_buffer_uv;
  size_t dst_stride_y, dst_scanlines_y, dst_plane_y_len;

  dst_buffer_y    = scaled_buf;
  dst_stride_y    = VENUS_Y_STRIDE(COLOR_FMT_NV12, scale_width_);
  dst_scanlines_y = VENUS_Y_SCANLINES(COLOR_FMT_NV12, scale_height_);
  dst_plane_y_len = dst_stride_y * dst_scanlines_y;
  dst_buffer_uv   = dst_buffer_y + dst_plane_y_len;

  // Preserve aspect ratio.
  uint32_t x = 0, y = 0;
  uint32_t width = src_width;
  uint32_t height = src_height;
  uint32_t src_y_offset  = 0;
  uint32_t src_uv_offset = 0;

  if (config_.preprocess_mode == PreprocessingMode::kKeepAR) {
    double in_ar = 0, out_ar = 0;
    in_ar  = static_cast<double>(width) / height;
    out_ar = static_cast<double>(scaled_width) / scaled_height;
    if (in_ar < 1 || out_ar < 1) {
      VAM_ML_LOGE("%s: Invalid Y offset!!", __func__);
      return MLE_FAIL;
    }
    if (in_ar > out_ar) {
      width = out_ar * height;
      x = (src_width - width) / 2;
    } else if (in_ar < out_ar) {
      height = width / out_ar;
      y = (src_height - height) / 2;
    }

    //Adjust the Y pointer.
    src_y_offset = y * src_stride_y + x;

    if (src_y_offset > src_plane_y_len) {
      VAM_ML_LOGE("%s: Invalid Y offset src_y_offset:%d !!", __func__,
          src_y_offset);
      return MLE_FAIL;
    }

    //Adjust the UV pointer.
    src_uv_offset = (y/2) * src_stride_y + x;
  }

  po_.width = width;
  po_.height = height;
  po_.x_offset = x;
  po_.y_offset = y;

  src_buffer_y = reinterpret_cast<unsigned char *>
                      ((intptr_t)src_buffer_y + src_y_offset);

  // Jump the Y plane.
  src_uv_offset += src_plane_y_len;

  if (src_uv_offset > src_plane_y_len * 1.5) {
    VAM_ML_LOGE("%s: Failed: Iinvalid chroma offset %d!", __func__,
        src_uv_offset);
    return MLE_FAIL;
  }

  src_buffer_uv = reinterpret_cast<unsigned char *>
                      ((intptr_t)input_buf + src_uv_offset);

   // Copy both Y and UV plane.
  fcvScaleu8_v2(src_buffer_y, width, height, src_stride_y,
      dst_buffer_y, scaled_width, scaled_height, dst_stride_y,
      FASTCV_INTERPOLATION_TYPE_NEAREST_NEIGHBOR,
      FASTCV_BORDER_REPLICATE, 0);

  fcvScaleDownMNInterleaveu8(src_buffer_uv, width >> 1, height >> 1,
      src_stride_y, dst_buffer_uv, scaled_width >> 1, scaled_height >> 1,
      dst_stride_y);

  return MLE_OK;
}

void SNPEBase::MeanSubtract(uint8_t* input_buf, const uint32_t width,
                              const uint32_t height, float* processed_buf) {

  uint8_t* src = input_buf;
  float* dest = processed_buf;

  float divisor = config_.use_norm ? config_.blue_sigma : 1;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t index = y * width + x;
      dest[index] = (static_cast<float>(src[index]) -
          config_.blue_mean) / divisor;
    }
  }
  src += height * width;
  dest += height * width;
  divisor = config_.use_norm ? config_.green_sigma : 1;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t index = y * width + x;
      dest[index] = (static_cast<float>(src[index]) -
          config_.green_mean) / divisor;
    }
  }
  src += height * width;
  dest += height * width;
  divisor = config_.use_norm ? config_.red_sigma : 1;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t index = y * width + x;
      dest[index] = (static_cast<float>(src[index]) -
          config_.red_mean) / divisor;
    }
  }
}

#endif // !QMMF_ALG

int32_t SNPEBase::ExecuteSNPE() {
  if (config_.io_type == NetworkIO::kUserBuffer) {
    if (!snpe_params_.snpe->execute(snpe_params_.input_ub_map,
                                    snpe_params_.output_ub_map)) {
      PrintErrorStringAndExit();
      return MLE_FAIL;
    }
  } else if (config_.io_type == NetworkIO::kITensor) {
    snpe_params_.output_tensor_map.clear();
    if (!snpe_params_.snpe->execute(snpe_params_.input_tensor_map,
                                    snpe_params_.output_tensor_map)) {
      PrintErrorStringAndExit();
      return MLE_FAIL;
    }
  } else {
    VAM_ML_LOGE("%s: Invalid Network IO value", __func__);
    return MLE_FAIL;
  }

  return MLE_OK;
}

int32_t SNPEBase::EnginePostProcess(GstBuffer* buffer) {
  std::vector<float> score_buf;
  const zdl::DlSystem::StringList &output_buf_names =
      snpe_params_.output_ub_map.getUserBufferNames();
  const zdl::DlSystem::StringList &output_tensor_names =
      snpe_params_.output_tensor_map.getTensorNames();
  const zdl::DlSystem::StringList *output_names = &output_buf_names;
  if (config_.io_type == NetworkIO::kITensor) {
    output_names = &output_tensor_names;
  }
  std::for_each(
      output_names->begin(),
      output_names->end(),
      [&](const char* name)
      {
        if (0 == std::strcmp(name, config_.result_layers[0].c_str())) {
          if (config_.io_type == NetworkIO::kUserBuffer) {
            IONBuffer b = snpe_params_.out_heap_map.at(name);
            if ((config_.input_format == InputFormat::kBgr) ||
               (config_.input_format == InputFormat::kRgb)) {
              for (size_t i = 0; i < b.size / sizeof(uint8_t); i++) {
                score_buf.push_back(b.addr[i]);
              }
            } else {
              for (size_t i = 0; i < b.size / sizeof(float); i++) {
                score_buf.push_back(b.addr_f[i]);
              }
            }
          } else if (config_.io_type == NetworkIO::kITensor) {
            auto t = snpe_params_.output_tensor_map.getTensor(name);
            for (auto it = t->begin(); it != t->end(); it++) {
              score_buf.push_back(*it);
            }
          }
        }
      });

  uint32_t top_score_idx = 0;
  float top_score = 0.0;

  for (size_t i = 0; i < score_buf.size(); i++) {
    if (score_buf[i] > top_score) {
      top_score = score_buf[i];
      top_score_idx = i;
    }
  }
  if (top_score_idx < labels_.size() &&
      top_score > init_params_.conf_threshold) {

    GstMLClassificationMeta *meta =
        gst_buffer_add_classification_meta(buffer);
    if (!meta) {
      VAM_ML_LOGE("Failed to create metadata");
      return MLE_NULLPTR;
    }

    meta->result.confidence = top_score * 100;
    uint32_t label_size = labels_.at(top_score_idx).size() + 1;
    meta->result.name = (gchar *)malloc(label_size);
    snprintf(meta->result.name, label_size, "%s", labels_.at(top_score_idx).c_str());
  }

  return MLE_OK;
}

int32_t SNPEBase::Process(struct SourceFrame* frame_info, GstBuffer* buffer) {

  if (!frame_info || !buffer) {
    VAM_ML_LOGE("%s Null pointer!", __func__);
    return MLE_NULLPTR;
  }
  int32_t result = MLE_OK;
//  out_params_.num_objects = 0;

  result = PreProcessBuffer(frame_info);
  if (MLE_OK != result) {
    VAM_ML_LOGE(" PreProcessBuffer failed");
    return result;
  }

  result = ExecuteSNPE();
  if (MLE_OK != result) {
    VAM_ML_LOGE(" SNPE execution failed");
    return result;
  }

  result = EnginePostProcess(buffer);
  if (MLE_OK != result) {
    VAM_ML_LOGE(" EnginePostProcess failed");
  }

  return result;
}

void SNPEBase::PrintErrorStringAndExit() {
  const char* const err = zdl::DlSystem::getLastErrorString();
  VAM_ML_LOGE(" %s", err);
}

int32_t SNPEBase::Init(const MLEInputParams* source_info) {
  VAM_ML_LOGE("%s: Enter", __func__);
  int32_t res = MLE_OK;
  ion_device_ = ion_open();
  if (ion_device_ < 0) {
    VAM_ML_LOGE("Open ion device failed");
    return MLE_FAIL;
  }

  init_params_.width = source_info->width;
  init_params_.height = source_info->height;
  init_params_.stride = source_info->stride;
  init_params_.scanline = source_info->scanline;
  init_params_.format = source_info->format;

  res = InitSNPE();
  if (MLE_OK != res) {
    VAM_ML_LOGE("InitSNPE failed");
    return res;
  }

#ifdef QMMF_ALG
  std::string configuration_data(AlgoConfiguration());

  try {
    InitAlgo();
    algo_->SetCallbacks(this);
    algo_->Configure(configuration_data);
  } catch (const std::exception &e) {
    DeinitAlgo();
    VAM_ML_LOGE("Algo failed: %s", e.what());
    return MLE_FAIL;
  }
#else
  posix_memalign(reinterpret_cast<void**>(&init_params_.bgr_buf), 128,
                 (init_params_.width * init_params_.height * 3));

  if (nullptr == init_params_.bgr_buf) {
    VAM_ML_LOGE(" Bgr Buffer allocation failed");
    free(init_params_.bgr_buf);
    return MLE_FAIL;
  }

  // TODO: use gralloc to allocate buffer, no need to deal with stride
  // and scanelines.
  uint32_t buffer_size   = VENUS_BUFFER_SIZE(COLOR_FMT_NV12,
                                scale_width_, scale_height_);
  VAM_ML_LOGD("%s: buffer_size: %d", __func__, buffer_size);

  IONBuffer ion_buffer = AllocateBuffer(buffer_size, InputFormat::kRgb);
  if (nullptr == ion_buffer.addr) {
    VAM_ML_LOGE("%s: Scale buffer allocation failed!", __func__);
    ReleaseBuffer(ion_buffer);
    return MLE_FAIL;
  }
  scale_ion_buffer_ = ion_buffer;
  VAM_ML_LOGD("%s: Scale buffer fd: %d", __func__, scale_ion_buffer_.fd);
#endif // QMMF_ALG

  VAM_ML_LOGD("%s: Exit", __func__);
  return res;
}

int32_t SNPEBase::InitSNPE() {
  VAM_ML_LOGI("%s Enter", __func__);
  int32_t res = MLE_OK;

  std::string folder("/data/misc/camera");
  std::string dlc = folder + "/" + config_.model_file;
  std::ifstream dlc_file(dlc);
  if (!dlc_file.is_open()) {
    VAM_ML_LOGE(" dlc files not valid");
    return MLE_FAIL;
  }

  snpe_params_.container = LoadContainerFromFile(dlc);
  if (nullptr == snpe_params_.container) {
    PrintErrorStringAndExit();
    res = MLE_FAIL;
  } else {
    snpe_params_.snpe = SetBuilderOptions();
    if (nullptr == snpe_params_.snpe) {
      PrintErrorStringAndExit();
      res = MLE_FAIL;
    }
  }

  ConfigureDimensions();

  if (MLE_OK == res) {
    std::string labels_filename = folder + "/" + config_.labels_file;
    std::ifstream labels_file(labels_filename);

    if (!labels_file.is_open()) {
      VAM_ML_LOGE(" Labels files not valid");
      res = MLE_FAIL;
    } else {
      for (std::string a; std::getline(labels_file, a);) {
        labels_.push_back(a);
      }
    }
  }

  if (MLE_OK == res) {
    res = PopulateMap(BufferType::kInput);
  }
  if (MLE_OK == res) {
    res = PopulateMap(BufferType::kOutput);
  }

  VAM_ML_LOGI("%s Exit", __func__);
  return res;
}

void SNPEBase::Deinit() {

#ifdef QMMF_ALG
  std::vector<AlgBuffer> inb;
  for (auto &it : alg_input_buffers_map_) {
    inb.push_back(it.second);
  }
  algo_->UnregisterInputBuffers(inb);
  algo_->UnregisterOutputBuffers(alg_output_buffers_);
  alg_input_buffers_map_.clear();
  alg_output_buffers_.clear();
#else
  if (nullptr != init_params_.bgr_buf) {
    free(init_params_.bgr_buf);
    init_params_.bgr_buf = nullptr;
  }
  ReleaseBuffer(scale_ion_buffer_);
#endif

  for (auto it : snpe_params_.in_heap_map) {
    ReleaseBuffer(it.second);
  }
  for (auto it : snpe_params_.out_heap_map) {
    ReleaseBuffer(it.second);
  }
  close(ion_device_);
#ifdef QMMF_ALG
  DeinitAlgo();
#endif
}

#ifdef QMMF_ALG
void IONBuffer::GetAlgBuffer(AlgBuffer* buf, const InputFormat& format) {
  BufferPlane plane(this->width,
                    this->height,
                    this->stride,
                    0,
                    this->size);
  buf->cached_ = false;
  buf->fd_ = this->fd;
  buf->size_ = this->size;
  buf->plane_.push_back(plane);
  switch (format) {
    case InputFormat::kBgrFloat:
      buf->pix_fmt_ = PixelFormat::kBgrFloat;
      break;
    case InputFormat::kRgbFloat:
      buf->pix_fmt_ = PixelFormat::kRgbFloat;
      break;
    case InputFormat::kBgr:
      buf->pix_fmt_ = PixelFormat::kBgr24;
      break;
    case InputFormat::kRgb:
      buf->pix_fmt_ = PixelFormat::kRgb24;
      break;
    default:
      throw std::runtime_error("Format is not supported");
  }

  if ((format == InputFormat::kBgr) ||
      (format == InputFormat::kRgb)) {
    buf->vaddr_ = this->addr;
  } else {
    buf->vaddr_ = reinterpret_cast<uint8_t*>(this->addr_f);
  }

  VAM_ML_LOGE(
      "Buffer info: fd %d, vaddr %p, size %d w %d h %d, stride %d, size %d",
      buf->fd_, buf->vaddr_, buf->size_, this->width, this->height,
      this->stride, this->size);
}
#endif

IONBuffer SNPEBase::AllocateBuffer(const uint32_t& size,
                                   const InputFormat& input_format) {
  IONBuffer buf;
  int32_t ret = 0;
  struct ion_allocation_data alloc {};
  alloc.len = (size + kBufferAlign-1) & ~(kBufferAlign-1);
  alloc.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc.flags = ION_FLAG_CACHED;
  int32_t ion_handle = -1;

 if (!ion_is_legacy(ion_device_)) {
    ret = ion_alloc_fd(ion_device_, alloc.len, 0, alloc.heap_id_mask, alloc.flags, &buf.fd);
    if (ret) {
      VAM_ML_LOGE("ION alloc failed");
      throw std::runtime_error("ION alloc failed");
    }
  } else {
    ret = ion_alloc(ion_device_, alloc.len, 0, alloc.heap_id_mask, alloc.flags, &ion_handle);
    if (ret) {
      VAM_ML_LOGE("ION alloc failed");
      throw std::runtime_error("ION alloc failed");
    }
    ret = ion_share(ion_device_, ion_handle, &buf.fd);
    if (ret) {
      VAM_ML_LOGE("ION alloc failed");
      throw std::runtime_error("ION alloc failed");
    }
 }

  if ((input_format == InputFormat::kBgr) ||
      (input_format == InputFormat::kRgb)) {
    buf.addr = static_cast<uint8_t*>(
      mmap(NULL, alloc.len, PROT_READ | PROT_WRITE, MAP_SHARED, buf.fd, 0));
    if (buf.addr == MAP_FAILED ) {
      VAM_ML_LOGE("mmap call failed");
      throw std::runtime_error("mmap call failed");
    }
  } else {
    buf.addr_f = static_cast<float*>(
      mmap(NULL, alloc.len, PROT_READ | PROT_WRITE, MAP_SHARED, buf.fd, 0));
    if (buf.addr_f == MAP_FAILED ) {
      VAM_ML_LOGE("mmap call failed");
      throw std::runtime_error("mmap call failed");
    }
  }
  SyncStart(buf.fd);
  buf.size = alloc.len;
  buf.width = scale_width_;
  buf.height = scale_height_;
  buf.stride = scale_stride_;
  buf.handle = ion_handle;

  VAM_ML_LOGD("%s: Fd:%d, handle:%d, size:%d, width:%d, height:%d Allocated!",
              __func__, buf.fd, buf.handle, buf.size, buf.width, buf.height);

  return buf;
}

void SNPEBase::ReleaseBuffer(const IONBuffer& buf) {
  VAM_ML_LOGD("%s: Enter", __func__);
  if (config_.input_format == InputFormat::kBgr ||
      config_.input_format == InputFormat::kRgb) {
    if (MAP_FAILED != buf.addr) {
      SyncEnd(buf.fd);
      munmap(buf.addr, buf.size);
    }
  } else {
    if (MAP_FAILED != buf.addr_f) {
      SyncEnd(buf.fd);
      munmap(buf.addr_f, buf.size);
    }
  }

  if (-1 != buf.handle) {
    if (ion_is_legacy(ion_device_)) {
      ion_free(ion_device_, buf.handle);
    }
  }

  if (-1 != buf.fd) {
    close(buf.fd);
  }

  VAM_ML_LOGD("%s: Exit", __func__);
}

#ifdef QMMF_ALG
void SNPEBase::InitAlgo() {
  try {
    auto alg_lib_folder = Utils::GetAlgLibFolder();
    Utils::LoadLib(alg_lib_folder + "libqmmf_alg_res_conv_sub.so",
                   algo_lib_handle_);

    QmmfAlgLoadPlugin LoadPluginFunc;
    Utils::LoadLibHandler(algo_lib_handle_, "QmmfAlgoNew", LoadPluginFunc);

    std::vector<uint8_t> calibration_data;
    algo_ = LoadPluginFunc(calibration_data, *this);
  } catch (const std::exception &e) {
    DeinitAlgo();
    VAM_ML_LOGE("Algo failed: %s", e.what());
    throw std::runtime_error("Init algo failed");
  }
}

void SNPEBase::DeinitAlgo() {
  if (nullptr != algo_) {
    delete algo_;
    algo_ = nullptr;
  }

  Utils::UnloadLib(algo_lib_handle_);
}

int32_t SNPEBase::GetOffsets(const std::string& conf_data) {
  Json::Reader reader;
  Json::Value val;
  if (!reader.parse(conf_data, val)) {
    VAM_ML_LOGE("%s: Failed to get algo configuration data", __func__);
    return MLE_FAIL;
  } else {

    switch (config_.preprocess_mode) {
      case PreprocessingMode::kKeepAR:
        po_.width = val.get("WidthKeepRatio", 0).asInt();
        po_.height = val.get("HeightKeepRatio", 0).asInt();
        po_.x_offset = val.get("XOffsetKeepRatio", 0).asInt();
        po_.y_offset = val.get("YOffsetKeepRatio", 0).asInt();
        break;
      case PreprocessingMode::kKeepFOV:
        po_.width = val.get("WidthKeepFOV", 0).asInt();
        po_.height = val.get("HeightKeepFOV", 0).asInt();
        po_.x_offset = val.get("XOffsetKeepFOV", 0).asInt();
        po_.y_offset = val.get("YOffsetKeepFOV", 0).asInt();
        break;
      case PreprocessingMode::kDirectDownscale:
        po_.width = init_params_.width;
        po_.height = init_params_.height;
        break;
      case PreprocessingMode::kMax:
      default:
        VAM_ML_LOGE("%s: Invalid preprocess mode %d", __func__,
                    static_cast<int>(config_.preprocess_mode));
        return MLE_FAIL;
    }
    if (!po_.width || !po_.height) {
      VAM_ML_LOGE("%s: Dimensions should not be 0", __func__);
      return MLE_FAIL;
    }
    if (po_.x_offset > init_params_.width ||
        po_.y_offset > init_params_.height) {
      VAM_ML_LOGE("%s: Invalid offsets", __func__);
      return MLE_FAIL;
    }

    VAM_ML_LOGI("%s %d  x %d y %d po_width %d po_height %d", __func__,
                static_cast<int>(config_.preprocess_mode), po_.x_offset,
                po_.y_offset, po_.width, po_.height);
  }

  return MLE_OK;
}

std::string SNPEBase::AlgoConfiguration() const {
  Json::StyledWriter w;
  Json::Value v;
  Json::Value color;

  v["BlueChannelMean"] = config_.blue_mean;
  v["GreenChannelMean"] = config_.green_mean;
  v["RedChannelMean"] = config_.red_mean;
  v["UseNorm"] = config_.use_norm;
  v["BlueChannelDivisor"] = config_.blue_sigma;
  v["GreenChannelDivisor"] = config_.green_sigma;
  v["RedChannelDivisor"] = config_.red_sigma;

  switch (config_.preprocess_mode) {
    case PreprocessingMode::kDirectDownscale:
      //In this case algorithm will not preserve the aspect ratio
      //and will shrink the image.
      break;
    case PreprocessingMode::kKeepFOV:
      v["KeepFieldOfView"] = 1;
      break;
    case PreprocessingMode::kKeepAR:
    default:
      v["KeepAspectRatio"] = 1;
      break;
  }

  color["valid"] = 1;
  color["y"] = 0;
  color["u"] = 128;
  color["v"] = 128;
  v["color"] = color;

  v["InputWidth"] = init_params_.width;
  v["InputHeight"] = init_params_.height;

  return w.write(v);
}

void SNPEBase::OnFrameReady(const AlgBuffer &output_buffer) {
  std::unique_lock<std::mutex> l(lock_);
  algo_process_done_ = true;
  signal_.notify_one();
}
#endif
}; // namespace mle
