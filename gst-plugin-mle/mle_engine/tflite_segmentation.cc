/*
* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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

#include "tflite_segmentation.h"
#include "common_utils.h"

namespace mle {

static const uint32_t       kOutBytesPerPixel = 4;

TFLSegmentation::TFLSegmentation(MLConfig &config) : TFLBase(config) {
  need_labels_ = false;
}
TFLSegmentation::~TFLSegmentation() {}

int32_t TFLSegmentation::PostProcess(GstBuffer* buffer) {
  MLE_LOGI("%s Enter", __func__);
  GstMLSegmentationMeta *img_meta = gst_buffer_add_segmentation_meta (buffer);
  if (!img_meta) {
    MLE_LOGE ("Failed to add overlay image meta");
    return MLE_FAIL;
  }

  int output = tflite_params_.interpreter->outputs()[0];
  TfLiteIntArray* output_dims = tflite_params_.interpreter->tensor(output)->dims;
  int batch_size = output_dims->data[0];
  if (batch_size != 1) {
    MLE_LOGE("%s: No support for %d batch size", __func__, batch_size);
    return MLE_FAIL;
  }
  uint32_t output_tensor_width = output_dims->data[2];
  uint32_t output_tensor_height = output_dims->data[1];
  uint32_t rgb_width = 0;
  uint32_t rgb_height = 0;

  if ((engine_input_params_.width != output_tensor_width) ||
      (engine_input_params_.height != output_tensor_height)) {
    float ratio = (output_tensor_width & ~0x1) * 1.0 /
                  fmax(scale_width_, scale_width_);
    rgb_width = (uint32_t)(scale_width_ * ratio) & ~0x1;
    rgb_height = (uint32_t)(scale_height_ * ratio) & ~0x1;
  } else {
    rgb_width = scale_width_;
    rgb_height = scale_height_;
  }

  uint32_t image_size = rgb_width * rgb_height * kOutBytesPerPixel;
  if (img_meta->img_buffer == nullptr) {
    img_meta->img_buffer = (gpointer) calloc (1, image_size);
    if (!img_meta->img_buffer) {
      MLE_LOGE(" Failed to allocate image buffer");
      return MLE_FAIL;
    }
  }

  img_meta->img_width  = rgb_width;
  img_meta->img_height = rgb_height;
  img_meta->img_size   = image_size;
  img_meta->img_format = GST_VIDEO_FORMAT_RGBA;
  img_meta->img_stride = rgb_width * kOutBytesPerPixel;

  float *float_temp_output =
      tflite_params_.interpreter->typed_output_tensor<float>(0);
  if (float_temp_output) {
    for (uint32_t y = 0; y < rgb_height; y++) {
      for (uint32_t x = 0; x < rgb_width; x++) {
        uint32_t offset = (y * output_tensor_width + x) * 21;
        int pos = 0;
        float *array = &float_temp_output[offset];
        float max = array[0];
        for (int i = 1; i < 21; i++) {
            if (max < array[i]) {
                pos = i;
                max = array[i];
            }
        }
        ((uint32_t*)img_meta->img_buffer)[y * rgb_width + x] =
            static_cast<uint32_t>(color_table[pos].red)  |
            static_cast<uint32_t>(color_table[pos].green) << 8 |
            static_cast<uint32_t>(color_table[pos].blue) << 16 |
            static_cast<uint32_t>(color_table[pos].alpha) << 24;
      }
    }
  }

  int32_t* int32_temp_output =
      tflite_params_.interpreter->typed_output_tensor<int32_t>(0);
  if (int32_temp_output) {
    for (uint32_t y = 0; y < rgb_height; y++) {
      for (uint32_t x = 0; x < rgb_width; x++) {
        uint32_t label = int32_temp_output[y * output_tensor_width + x];
        ((uint32_t*)img_meta->img_buffer)[y * rgb_width + x] =
            static_cast<uint32_t>(color_table[label].red)  |
            static_cast<uint32_t>(color_table[label].green) << 8 |
            static_cast<uint32_t>(color_table[label].blue) << 16 |
            static_cast<uint32_t>(color_table[label].alpha) << 24;
      }
    }
  }
  MLE_LOGI("%s Exit", __func__);
  return MLE_OK;
}
}; // namespace mle
