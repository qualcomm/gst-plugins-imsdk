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

#include <vector>
#include <cmath>
#include "snpe_complex.h"

namespace mle {

SNPEComplex::SNPEComplex(MLConfig &config) : SNPEBase(config) {}
SNPEComplex::~SNPEComplex() {}

int32_t SNPEComplex::EnginePostProcess(GstBuffer* buffer) {
  std::vector<float> score_buf;
  std::vector<float> box_buf;
  std::vector<float> class_buf;

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
        if (config_.io_type == NetworkIO::kUserBuffer) {
          if ((config_.input_format == InputFormat::kBgr) ||
              (config_.input_format == InputFormat::kRgb)) {
            if (0 == std::strcmp(name, config_.result_layers[0].c_str())) {
              IONBuffer b = snpe_params_.out_heap_map.at(name);
              for (size_t i = 0; i < b.size / sizeof(uint8_t); i++) {
                score_buf.push_back(b.addr[i]);
              }
            } else if (0 == std::strcmp(name, config_.result_layers[1].c_str())) {
              IONBuffer b = snpe_params_.out_heap_map.at(name);
              for (size_t i = 0; i < b.size / sizeof(uint8_t); i++) {
                box_buf.push_back(b.addr[i]);
              }
            } else if (0 == std::strcmp(name, config_.result_layers[2].c_str())) {
              IONBuffer b = snpe_params_.out_heap_map.at(name);
              for (size_t i = 0; i < b.size / sizeof(uint8_t); i++) {
                class_buf.push_back(b.addr[i]);
              }
            }
          } else {
            if (0 == std::strcmp(name, config_.result_layers[0].c_str())) {
              IONBuffer b = snpe_params_.out_heap_map.at(name);
              for (size_t i = 0; i < b.size / sizeof(float); i++) {
                score_buf.push_back(b.addr_f[i]);
              }
            } else if (0 == std::strcmp(name, config_.result_layers[1].c_str())) {
              IONBuffer b = snpe_params_.out_heap_map.at(name);
              for (size_t i = 0; i < b.size / sizeof(float); i++) {
                box_buf.push_back(b.addr_f[i]);
              }
            } else if (0 == std::strcmp(name, config_.result_layers[2].c_str())) {
              IONBuffer b = snpe_params_.out_heap_map.at(name);
              for (size_t i = 0; i < b.size / sizeof(float); i++) {
                class_buf.push_back(b.addr_f[i]);
              }
            }
          }
        } else if (config_.io_type == NetworkIO::kITensor) {
          if (0 == std::strcmp(name, config_.result_layers[0].c_str())) {
            auto t = snpe_params_.output_tensor_map.getTensor(name);
            for (auto it = t->begin(); it != t->end(); it++) {
              score_buf.push_back(*it);
            }
          } else if (0 == std::strcmp(name, config_.result_layers[1].c_str())) {
            auto t = snpe_params_.output_tensor_map.getTensor(name);
            for (auto it = t->begin(); it != t->end(); it++) {
              box_buf.push_back(*it);
            }
          } else if (0 == std::strcmp(name, config_.result_layers[2].c_str())) {
            auto t = snpe_params_.output_tensor_map.getTensor(name);
            for (auto it = t->begin(); it != t->end(); it++) {
              class_buf.push_back(*it);
            }
          }
        }
      });

  uint32_t width = init_params_.width;
  uint32_t height = init_params_.height;

  if (config_.preprocess_mode == PreprocessingMode::kKeepAR) {
    width = po_.width;
    height = po_.height;
  }

  if (score_buf.size() && box_buf.size() && class_buf.size()) {
    uint32_t num_obj = 0;
    for (size_t i = 0; i < score_buf.size(); i++) {
      if (score_buf[i] < init_params_.conf_threshold) {
        continue;
      }
      GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(buffer);
      if (!meta) {
        VAM_ML_LOGE("Failed to create metadata");
        return MLE_NULLPTR;
      }

      GstMLClassificationResult *box_info = (GstMLClassificationResult*)malloc(
          sizeof(GstMLClassificationResult));

      uint32_t label_size = labels_.at(
          static_cast<uint32_t>(class_buf[i] + 0.5)).size() + 1;
      box_info->name = (gchar *)malloc(label_size);
      snprintf(box_info->name, label_size, "%s",
               labels_.at(static_cast<uint32_t>(class_buf[i] + 0.5)).c_str());
      box_info->confidence = score_buf[i];
      meta->box_info = g_slist_append (meta->box_info, box_info);

      meta->bounding_box.x = std::lround(box_buf[i * 4 + 1] * width) +
          po_.x_offset;
      meta->bounding_box.y = std::lround(box_buf[i * 4] * height) +
          po_.y_offset;
      meta->bounding_box.width = (std::lround(box_buf[i * 4 + 3] * width) + po_.x_offset) -
                                             meta->bounding_box.x;
      meta->bounding_box.height = (std::lround(box_buf[i * 4 + 2] * height) + po_.y_offset) -
                                              meta->bounding_box.y;

      if (config_.preprocess_mode == PreprocessingMode::kKeepFOV) {
#ifdef QMMF_ALG
        meta->bounding_box.x = (std::lround(box_buf[i * 4 + 1] * width) -
            po_.x_offset) * (scale_width_ / po_.width);
        meta->bounding_box.y = (std::lround(box_buf[i * 4] * height) -
            po_.y_offset) * (scale_height_ / po_.height);
      }
#else
     VAM_ML_LOGI("KeepFOV mode is supported only in QMMF_ALG pre-processing");
    }
#endif

    num_obj++;

    VAM_ML_LOGD("object info: name: %s , score %f, box x %d y %d w %d h %d",
                box_info->name, box_info->confidence, meta->bounding_box.x, meta->bounding_box.y,
                meta->bounding_box.width, meta->bounding_box.height);
    }
    VAM_ML_LOGI("Inference engine detected %d objects, highest score: %f",
                  num_obj, score_buf[0]);
  }
  return MLE_OK;
}

int32_t SNPEComplex::Process(struct SourceFrame* frame_info,
                             GstBuffer* buffer) {
  int32_t result = MLE_OK;

  result = PreProcessBuffer(frame_info);
  if (MLE_OK != result) {
    VAM_ML_LOGE("PreProcessBuffer failed");
    return result;
  }

  result = ExecuteSNPE();
  if (MLE_OK != result) {
    VAM_ML_LOGE("SNPE execution failed");
    return result;
  }

  result = EnginePostProcess(buffer);
  if (MLE_OK != result) {
    VAM_ML_LOGE("EnginePostProcess failed");
  }
  return result;
}

}; // namespace mle
