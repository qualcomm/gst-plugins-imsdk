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

#include "deeplabengine.h"

#define COLOR_TABLE_SIZE 32
#define DEFAULT_ALPHA 128

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} rgba;

rgba color_table[COLOR_TABLE_SIZE] = {
  { //Black(background 0)
  .red   = 0,
  .green = 0,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Maroon(aeroplane 1)
  .red   = 128,
  .green = 0,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Green(bicycle 2)
  .red   = 0,
  .green = 128,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Olive(bird 3)
  .red   = 128,
  .green = 128,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Navy(boat 4)
  .red   = 0,
  .green = 0,
  .blue  = 128,
  .alpha = DEFAULT_ALPHA
 },
 { //Purple(bottle 5)
  .red   = 128,
  .green = 0,
  .blue  = 128,
  .alpha = DEFAULT_ALPHA
 },
 { //Teal(bus 6)
  .red   = 0,
  .green = 128,
  .blue  = 128,
  .alpha = DEFAULT_ALPHA
 },
 { //Silver(car 7)
  .red   = 192,
  .green = 192,
  .blue  = 192,
  .alpha = DEFAULT_ALPHA
 },
 { //Grey(cat 8)
  .red   = 128,
  .green = 128,
  .blue  = 128,
  .alpha = DEFAULT_ALPHA
 },
 { //Red(chair 9)
  .red   = 255,
  .green = 0,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Lime(cow 10)
  .red   = 0,
  .green = 255,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Yellow(diningtable 11)
  .red   = 255,
  .green = 255,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Blue(dog 12)
  .red   = 0,
  .green = 0,
  .blue  = 255,
  .alpha = DEFAULT_ALPHA
 },
 { //Fuchsia(horse 13)
  .red   = 255,
  .green = 0,
  .blue  = 255,
  .alpha = DEFAULT_ALPHA
 },
 { //Aqua(motorbike 14)
  .red   = 0,
  .green = 255,
  .blue  = 255,
  .alpha = DEFAULT_ALPHA
 },
 { //White(person 15)
  .red   = 255,
  .green = 255,
  .blue  = 255,
  .alpha = DEFAULT_ALPHA
 },
 { //Honeydew2(potted plant 16)
  .red   = 215,
  .green = 255,
  .blue  = 215,
  .alpha = DEFAULT_ALPHA
 },
 { //Salmon1(sheep 17)
  .red   = 255,
  .green = 135,
  .blue  = 95,
  .alpha = DEFAULT_ALPHA
 },
 { //Orange1(sofa 18)
  .red   = 255,
  .green = 175,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Gold1(train 19)
  .red   = 255,
  .green = 215,
  .blue  = 0,
  .alpha = DEFAULT_ALPHA
 },
 { //Thistle1(tv/monitor 20)
  .red   = 255,
  .green = 215,
  .blue  = 255,
  .alpha = DEFAULT_ALPHA
 },
 { //Cornsilk1(unknown 255)
  .red   = 255,
  .green = 255,
  .blue  = 215,
  .alpha = DEFAULT_ALPHA
 }
};

const std::string DeepLabv3Engine::kModelLib = "libdeeplabv3_nn.so";

int32_t DeepLabv3Engine::Init(const NNSourceInfo* source_info)
{
  int32_t out_sizes[kNumOutputs];
  out_sizes[0] = kOutSize0;

  EngineInit(source_info, out_sizes);

  posix_memalign(reinterpret_cast<void**>(&result_buff_), 128,
      (scale_width_ * scale_height_ * kOutBytesPerPixel));

  if (nullptr == result_buff_) {
    NN_LOGE(" Buffer allocation failed");
    return NN_FAIL;
  }

  for (uint32_t i = 0; i < scale_width_ * scale_height_; i++) {
    ((uint32_t*)result_buff_)[i] =
        static_cast<uint32_t>(color_table[0].red)  |
        static_cast<uint32_t>(color_table[0].green) << 8 |
        static_cast<uint32_t>(color_table[0].blue) << 16 |
        static_cast<uint32_t>(color_table[0].alpha) << 24;
  }

  return NN_OK;
}

void DeepLabv3Engine::DeInit()
{
  if (nullptr != result_buff_) {
    free(result_buff_);
    result_buff_ = nullptr;
  }

  EngineDeInit();
}

void * DeepLabv3Engine::GenerateMeta(GstBuffer * gst_buffer)
{
  GstMLSegmentationMeta *img_meta = gst_buffer_add_segmentation_meta (gst_buffer);
  if (!img_meta) {
    NN_LOGE ("Failed to add overlay image meta");
    return nullptr;
  }

  uint32_t image_size = scale_width_ * scale_height_ * kOutBytesPerPixel;

  if (img_meta->img_buffer == nullptr) {
    img_meta->img_buffer = (gpointer) calloc (1, image_size);
    if (!img_meta->img_buffer) {
      NN_LOGE(" Failed to allocate image buffer");
      return nullptr;
    }
  }

  img_meta->img_width  = scale_width_;
  img_meta->img_height = scale_height_;
  img_meta->img_size   = image_size;
  img_meta->img_format = GST_VIDEO_FORMAT_RGBA;
  img_meta->img_stride = scale_width_ * kOutBytesPerPixel;

  return img_meta->img_buffer;
}

void DeepLabv3Engine::GenerateRGBAImage(void* outputs[], void *buffer)
{
  uint32_t* out_buf = static_cast<uint32_t *> (outputs[0]);

  for (uint32_t y = 0; y < scale_height_; y++) {
    for (uint32_t x = 0; x < scale_width_; x++) {
      uint32_t label = out_buf[y * kPadWidth + x];
      ((uint32_t*)buffer)[y * scale_width_ + x] =
          static_cast<uint32_t>(color_table[label].red)  |
          static_cast<uint32_t>(color_table[label].green) << 8 |
          static_cast<uint32_t>(color_table[label].blue) << 16 |
          static_cast<uint32_t>(color_table[label].alpha) << 24;
    }
  }
}

int32_t DeepLabv3Engine::PostProcess(void* outputs[], GstBuffer * gst_buffer)
{
  void * buff;
  if (gst_buffer) {
    buff = GenerateMeta(gst_buffer);
  } else {
    buff = result_buff_;
  }
  if (!buff) {
    NN_LOGE("%s: Output buffer is null", __func__);
    return NN_FAIL;
  }

  std::unique_lock<std::mutex> lock(lock_);
  GenerateRGBAImage(outputs, buff);

  return NN_OK;
}

int32_t DeepLabv3Engine::FillMLMeta(GstBuffer * gst_buffer)
{
  void * buff = GenerateMeta(gst_buffer);
  if (!buff) {
    NN_LOGE("%s: Output buffer is null", __func__);
    return NN_FAIL;
  }

  std::unique_lock<std::mutex> lock(lock_);
  memcpy(buff, result_buff_, scale_width_ * scale_height_ * kOutBytesPerPixel);

  return NN_OK;
}
