/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
#include <string>

#include <utils/Log.h>
#include <ml-meta/ml_meta.h>

#include <cvp/v2.0/cvpOpticalFlow.h>
#include <cvp/v2.0/cvpMem.h>
#include <cvp/v2.0/cvpUtils.h>
#include <cvp/v2.0/cvpSession.h>
#include <cvp/v2.0/cvpTypes.h>

#define CVP_LOGI(...) ALOGI("CVP: " __VA_ARGS__)
#define CVP_LOGE(...) ALOGE("CVP: " __VA_ARGS__)
#define CVP_LOGD(...) ALOGD("CVP: " __VA_ARGS__)

#define CVP_UNUSED(var) ((void)var)

namespace cvp {

/* CVP return status
 * Might need to modify to make it CVP format
 * or take out this section and use CVP directly
 */
enum CVPErrors {
  CVP_OK = 0,
  CVP_FAIL,
  CVP_NULLPTR,
  CVP_IMG_FORMAT_NOT_SUPPORTED,
  CVP_FATALERROR
};

/* Image format
 * Only accept NV12, NV21, and gray 8 bit
 * Only accept format that has Y plane
 */
enum CVPImageFormat {
  cvp_format_invalid = 0,
  cvp_format_nv12,
  cvp_format_nv21,
  cvp_format_gray8bit,
};

/* Image input parameters
 * Width:          image width
 * Height:         image height
 * Stride:         image stride
 * FPS:            Frame per second
 * ininfo:         Video info from Gstreamer
 * CVPImageFormat: NV12 or NV21
 */
struct CVPInputParams {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t fps;
  GstVideoInfo *ininfo;
  CVPImageFormat format;
};

struct CVPConfig {
  CVPInputParams source_info;

  // Optical flow specific
  bool stats_enable;

  char* output_location;
};

class OFEngine {
public:
  OFEngine (CVPConfig &config);
  ~OFEngine ();
  int32_t Init ();
  int32_t Deinit ();
  void Flush ();
  int32_t Process (GstBuffer * inbuffer, GstBuffer * outbuffer);

private:
  // process
  uint32_t frameid;
  GMutex lock;

  int32_t FreeCurImageBuffer ();
  int32_t FreeRefImageBuffer ();
  int32_t FreeMVBuffer ();
  int32_t FreeStatsBuffer ();

protected:

  CVPConfig cvpConfig;

  // gst specific
  GstBuffer* savebuffer;
  int32_t buffersize;

  // optical flow process specific
  cvpConfigOpticalFlow config;
  cvpAdvConfigOpticalFlow advConfig;
  cvpOpticalFlowOutBuffReq outMemReq;
  cvpOpticalFlowOutput outputBuf[2];
  cvpHandle pInitHandle;
  cvpSession pSessH;
  cvpImage  refImage, curImage;
  bool newRef = true, newCur = true;
}; // OFEngine

class Timer {
  std::string str;
  uint64_t begin;

public:

  Timer (std::string s) : str(s) {
    begin = GetMicroSeconds ();
  }

  ~Timer () {
    uint64_t end = GetMicroSeconds ();
    CVP_LOGD("%s: %llu us", str.c_str(),
        static_cast<long long unsigned>(end - begin));
  }

  uint64_t GetMicroSeconds() {
    timespec time;

    clock_gettime (CLOCK_MONOTONIC, &time);

    uint64_t microSeconds = (static_cast<uint32_t>(time.tv_sec) * 1000000ULL) +
                            (static_cast<uint32_t>(time.tv_nsec)) / 1000;

    return microSeconds;
  }
};
};
