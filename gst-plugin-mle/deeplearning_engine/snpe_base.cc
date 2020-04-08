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
#include <memory>
#include <sstream>
#include <chrono>
#include <json/json.h>
#include <string>
#include <media/msm_media_info.h>
#include <fastcv/fastcv.h>

#include "snpe_base.h"

namespace mle {

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

  pad_height_ = dims[1];
  pad_width_ = dims[2];

  config_.input_layer = static_cast<std::string>(names.at(0));

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

  auto uba_opt = snpe_params_.snpe->getInputOutputBufferAttributes(name);
  if (!uba_opt) {
    throw std::runtime_error(
        std::string("Error obtaining attributes for tensor ") + name);
  }

  auto m_encoding = (*uba_opt)->getEncoding();
  auto enc_type = (*uba_opt)->getEncodingType();
  VAM_ML_LOGI("Encoding type is %d", (int)enc_type);
  const zdl::DlSystem::TensorShape& buffer_shape = (*uba_opt)->getDims();

  size_t elem_size = (*uba_opt)->getElementSize();
  VAM_ML_LOGI("Bufer type %d elements size in bytes: %d", (int)type, elem_size);

  size_t buf_size = CalculateSizeFromDims(buffer_shape.rank(),
                                          buffer_shape.getDimensions(),
                                          elem_size);

  auto *heap_map = &snpe_params_.in_heap_map;
  auto *ub_map = &snpe_params_.input_ub_map;
  if (type == BufferType::kOutput) {
    heap_map = &snpe_params_.out_heap_map;
    ub_map = &snpe_params_.output_ub_map;
  }

  heap_map->emplace(name, std::vector<float>(buf_size / elem_size));

  snpe_params_.ub_list.push_back(ub_factory.createUserBuffer(
      heap_map->at(name).data(), buf_size,
      GetStrides((*uba_opt)->getDims(), elem_size), m_encoding));
  ub_map->add(name, snpe_params_.ub_list.back().get());

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

  size_t elem_size = (*tensor_opt)->getElementSize();
  VAM_ML_LOGI("Bufer type %d elements size in bytes: %d", (int)type, elem_size);

  size_t buf_size = CalculateSizeFromDims(tensor_shape.rank(),
                                          tensor_shape.getDimensions(),
                                          elem_size);
  auto *heap_map = &snpe_params_.in_heap_map;
  auto *tensor_map = &snpe_params_.input_tensor_map;
  if (type == BufferType::kOutput) {
    heap_map = &snpe_params_.out_heap_map;
    tensor_map = &snpe_params_.output_tensor_map;
  }

  heap_map->emplace(name, std::vector<float>(buf_size / elem_size));
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

void SNPEBase::Pad(
    uint8_t*       input_buf,
    const uint32_t input_width,
    const uint32_t input_height,
    const uint32_t pad_width,
    const uint32_t pad_height,
    uint8_t*       output_buf)
{
  MLE_UNUSED(pad_height);
  // This API assume that buffer is already fill up with
  // pad value and only active area is copied.
  // This optimization reduce time ~10 times.
  for (uint32_t y = 0; y < input_height; y++) {
    for (uint32_t x = 0; x < 3 * input_width; x++) {
      uint32_t index_src = y * 3 * input_width + x;
      uint32_t index_dst = y * 3 * pad_width + x;
      output_buf[index_dst] = input_buf[index_src];
    }
  }
}

void SNPEBase::PreProcessScale(
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

void SNPEBase::PreProcessColorConvertRGB(
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

void SNPEBase::PreProcessColorConvertBGR(
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

int32_t SNPEBase::PreProcessBuffer(const SourceFrame* frame_info) {

  float* snpe_buf = snpe_params_.in_heap_map[config_.input_layer.c_str()].data();

  if (!snpe_buf) {
    VAM_ML_LOGE("%s: SNPE buffer is null", __func__);
    return MLE_NULLPTR;
  }

  if ((scale_width_ != init_params_.width) ||
      (scale_height_ != init_params_.height)) {
    PreProcessScale(frame_info->frame_data[0],
                    frame_info->frame_data[1],
                    scale_buffer_,
                    init_params_.width,
                    init_params_.height,
                    scale_width_,
                    scale_height_,
                    init_params_.format);

    PreProcessColorConvertRGB(scale_buffer_,
                              scale_buffer_ + scale_width_ * scale_height_,
                              init_params_.bgr_buf,
                              scale_width_,
                              scale_height_,
                              init_params_.format);
  } else {
    // No scale down.
    PreProcessColorConvertRGB(frame_info->frame_data[0],
                              frame_info->frame_data[1],
                              init_params_.bgr_buf,
                              scale_width_,
                              scale_height_,
                              init_params_.format);
  }

   if (scale_width_ != pad_width_ ||
      scale_height_ != pad_height_) {
    Pad(init_params_.bgr_buf,
        scale_width_,
        scale_height_,
        pad_width_,
        pad_height_,
        pad_buffer_);
  }

  MeanSubtract(pad_buffer_, pad_width_, pad_height_, snpe_buf);

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
            score_buf = snpe_params_.out_heap_map.at(name);
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

  init_params_.width = source_info->width;
  init_params_.height = source_info->height;
  init_params_.format = source_info->format;

  res = InitSNPE();
  if (MLE_OK != res) {
    VAM_ML_LOGE("InitSNPE failed");
    return res;
  }

  // Calculate downscale params
  float ratio = (pad_width_ & ~0x1) * 1.0 / fmax(init_params_.width, init_params_.height);
  scale_width_ = (uint32_t)(init_params_.width * ratio);
  scale_height_ = (uint32_t)(init_params_.height * ratio);

  VAM_ML_LOGE("%s:%d: in: %dx%d scaled: %dx%d nn: %dx%d ", __func__, __LINE__,
              init_params_.width, init_params_.height, scale_width_, scale_height_,
              pad_width_, pad_height_);

  posix_memalign(reinterpret_cast<void**>(&init_params_.bgr_buf), 128,
                 (scale_width_ * scale_height_ * 3));
  posix_memalign(reinterpret_cast<void**>(&pad_buffer_), 128,
                 (pad_width_ * pad_height_ * 3));
  posix_memalign(reinterpret_cast<void**>(&scale_buffer_), 128,
                 ((scale_width_ * scale_height_ * 3) / 2));

  if ((nullptr == init_params_.bgr_buf) ||
      (nullptr == scale_buffer_) ||
      (nullptr == pad_buffer_)) {
    VAM_ML_LOGE(" Buffer allocation failed");
    return MLE_FAIL;
  }

  memset(pad_buffer_, 128, pad_width_ * pad_height_ * 3);

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

  if (MLE_OK == res) {
    ConfigureDimensions();
  }

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

  if (nullptr != init_params_.bgr_buf) {
    free(init_params_.bgr_buf);
    init_params_.bgr_buf = nullptr;
  }
    if (nullptr != scale_buffer_) {
    free(scale_buffer_);
    scale_buffer_ = nullptr;
  }
    if (nullptr != pad_buffer_) {
    free(pad_buffer_);
    pad_buffer_ = nullptr;
  }

  for (auto it : snpe_params_.in_heap_map) {
    (it.second).clear();
  }
  for (auto it : snpe_params_.out_heap_map) {
    (it.second).clear();
  }
}
}; // namespace mle
