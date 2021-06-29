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
#include <gst/video/gstvideometa.h> // to get buffer frame meta
#include <gst/allocators/allocators.h>

#include "opticalflow.h"

namespace cvp {

OFEngine::OFEngine (CVPConfig &config) : cvpConfig(config) {
  frameid     = 0;
  buffersize  = 0;
  pInitHandle = NULL;
  pSessH      = NULL;
  savebuffer  = NULL;
  g_mutex_init (&lock);
}

OFEngine::~OFEngine () {
  g_mutex_clear (&lock);
}

int32_t OFEngine::Init () {
  int res = CVP_OK;
  CVP_LOGI ("%s: Enter", __func__);

  // init CVP session
  pSessH = cvpCreateSession (NULL, NULL);
  if (pSessH == nullptr) {
    CVP_LOGE ("%s: Create session failed, NULL session handle returned",
             __func__);
    return CVP_FAIL;
  }
  CVP_LOGI ("%s: CVP Session %p", __func__, pSessH);

  // Init call config Parameters
  config.nActualFps         = cvpConfig.source_info.fps;
  config.nOperationalFps    = cvpConfig.source_info.fps;
  config.sImageInfo.nWidth  = cvpConfig.source_info.width;
  config.sImageInfo.nHeight = cvpConfig.source_info.height;
  config.eMode              = (cvpOpticalFlowMode)CVP_OPTICALFLOW_SEVEN_PASS;
  config.bStatsEnable       = cvpConfig.stats_enable;
  // TODO: When CVP supports NV12 this logic will be used for format setting
  switch (cvpConfig.source_info.format) {
    case CVPImageFormat::cvp_format_nv12:
      config.sImageInfo.eFormat = CVP_COLORFORMAT_NV12;
      break;
    case CVPImageFormat::cvp_format_nv21:
    case CVPImageFormat::cvp_format_gray8bit:
      config.sImageInfo.eFormat = CVP_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      CVP_LOGE ("%s: ERROR, invalid video format", __func__);
      return CVP_FAIL;
  }

  CVP_LOGI ("%s: Query image info", __func__);
  cvpImageInfo pQueryImageInfo;
  cvpStatus query = cvpQueryImageInfo (config.sImageInfo.eFormat,
                                       config.sImageInfo.nWidth,
                                       config.sImageInfo.nHeight,
                                       &pQueryImageInfo);
  if (query != CVP_SUCCESS) {
    CVP_LOGE ("%s: Error! Query image info failed", __func__);
    return CVP_FAIL;
  }

  CVP_LOGI ("%s:      Width      %d", __func__, pQueryImageInfo.nWidth);
  CVP_LOGI ("%s:      Height     %d", __func__, pQueryImageInfo.nHeight);
  CVP_LOGI ("%s:      Format     %d", __func__, pQueryImageInfo.eFormat);

  config.sImageInfo.nPlane = pQueryImageInfo.nPlane;
  gint heightAlligned = (cvpConfig.source_info.height + 64-1) & ~(64-1);
  if (config.sImageInfo.nPlane == 1) {
    gsize size = cvpConfig.source_info.stride * heightAlligned;
    config.sImageInfo.nTotalSize = size;
    config.sImageInfo.nWidthStride[0] = cvpConfig.source_info.stride;
    config.sImageInfo.nAlignedSize[0] = size;
    buffersize = config.sImageInfo.nAlignedSize[0];
    CVP_LOGI ("%s:      Plane width stride %d, aligned size %d",
              __func__,
              config.sImageInfo.nWidthStride[0],
              config.sImageInfo.nAlignedSize[0]);
  } else {
    config.sImageInfo.nTotalSize = pQueryImageInfo.nTotalSize;
    for (unsigned int i = 0; i < config.sImageInfo.nPlane; i++) {
      config.sImageInfo.nWidthStride[i] = pQueryImageInfo.nWidthStride[i];
      config.sImageInfo.nAlignedSize[i] = pQueryImageInfo.nAlignedSize[i];
      buffersize += config.sImageInfo.nAlignedSize[i];
      CVP_LOGI ("%s:      Plane %d width stride %d, aligned size %d",
                __func__, i,
                config.sImageInfo.nWidthStride[i],
                config.sImageInfo.nAlignedSize[i]);
    }
  }

  CVP_LOGI ("%s:      Plane      %d", __func__, config.sImageInfo.nPlane);
  CVP_LOGI ("%s:      Total size %d", __func__, config.sImageInfo.nTotalSize);

  //Init Call
  pInitHandle = cvpInitOpticalFlow (pSessH, &config, &advConfig, &outMemReq,
      NULL, NULL);
  if (pInitHandle == NULL) {
    CVP_LOGE ("%s: ERROR cvpInitOpticalFlow call failed. pInitHandle is NULL",
        __func__);
    return CVP_FAIL;
  }

  CVP_LOGI ("%s: CVP optical flow initialized", __func__);
  CVP_LOGI ("%s: CVP Output MV bytes:    %d",
      __func__, outMemReq.nMotionVectorBytes);
  if (config.bStatsEnable) {
    CVP_LOGI ("%s: CVP Output Stats bytes: %d",
        __func__, outMemReq.nStatsBytes);
  }

  CVP_LOGI ("%s: Optical flow Config", __func__);
  CVP_LOGI ("%s:  actual fps:      %d", __func__, config.nActualFps);
  CVP_LOGI ("%s:  operational fps: %d", __func__, config.nOperationalFps);
  CVP_LOGI ("%s:  format:          %d", __func__, config.sImageInfo.eFormat);
  CVP_LOGI ("%s:  width:           %d", __func__, config.sImageInfo.nWidth);
  CVP_LOGI ("%s:  height:          %d", __func__, config.sImageInfo.nHeight);
  CVP_LOGI ("%s:  total size:      %d", __func__, config.sImageInfo.nTotalSize);
  CVP_LOGI ("%sl  plane:           %d", __func__, config.sImageInfo.nPlane);
  CVP_LOGI ("%s:  pass type:       %d", __func__, config.eMode);
  CVP_LOGI ("%s:  stats enable:    %d", __func__, config.bStatsEnable);

  CVP_LOGI ("%s: Optical flow Adv Config", __func__);
  CVP_LOGI ("%s:  MV Dist:                 %d", __func__, advConfig.nMvDist);
  CVP_LOGI ("%s:  median filter type:      %d", __func__,
                                               advConfig.nMedianFiltType);
  CVP_LOGI ("%s:  threshold median filter: %d", __func__,
                                               advConfig.nThresholdMedFilt);
  CVP_LOGI ("%s:  smoothness penalty:      %d",
           __func__, advConfig.nSmoothnessPenaltyThresh);
  CVP_LOGI ("%s:  search range X:          %d", __func__,
      advConfig.nSearchRangeX);
  CVP_LOGI ("%s:  search range Y:          %d", __func__,
      advConfig.nSearchRangeY);
  CVP_LOGI ("%s:  enable EIC:              %d", __func__, advConfig.bEnableEic);

  // calculate image buffer size
  refImage.sImageInfo = config.sImageInfo;
  curImage.sImageInfo = config.sImageInfo;

  // CVP Start Session
  res = cvpStartSession (pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGE ("%s: CVP session fail to start", __func__);
    return CVP_FAIL;
  }
  CVP_LOGI ("%s: CVP session start", __func__);

  CVP_LOGI ("%s: Exit", __func__);
  return res;
}

int32_t OFEngine::Deinit () {
  int res = CVP_OK;
  CVP_LOGI ("%s: Enter", __func__);

  // CVP Stop Session
  res = cvpStopSession (pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGI ("%s: Stop CVP session Failed", __func__);
    return res;
  }
  CVP_LOGI ("%s: CVP session stopped", __func__);

  // OF De-Initialization
  res = cvpDeInitOpticalFlow (pInitHandle);
  if (res != CVP_SUCCESS) {
    CVP_LOGI ("%s: DeInit Optical flow Failed", __func__);
    return res;
  }
  else {
    CVP_LOGI ("%s: Optical flow deinit", __func__);
  }

  // CVP Delete session
  res = cvpDeleteSession (pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGI ("%s: cvpDeleteSession Failed", __func__);
    return res;
  }
  else {
    CVP_LOGI ("%s: CVP session deleted", __func__);
  }

  CVP_LOGI ("%s: Exit", __func__);
  return res;
}

void OFEngine::Flush () {
  g_mutex_lock (&lock);
  CVP_LOGI ("%s: Enter", __func__);

  // Unref the saved buffer
  if (savebuffer && GST_MINI_OBJECT_REFCOUNT_VALUE (savebuffer) > 0)
    gst_buffer_unref (savebuffer);

  CVP_LOGI ("%s: Exit", __func__);
  g_mutex_unlock (&lock);
  return;
}

/* Process
 * Check input frame from GST
 * Assign image to cur image buffer
 *
 * If ref buffer does not exist
 * Skip to next iteration
 *
 * If ref buffer exist
 * Register both image buffer to optical flow
 * Run optical flow sync
 * Process output
 *
 * Swap ref and cur image buffer
 * Clean old ref image buffer
 */
int32_t OFEngine::Process (GstBuffer * inbuffer, GstBuffer * outbuffer) {
  CVP_LOGI ("%s: Enter", __func__);
  int res = CVP_OK;

  g_mutex_lock (&lock);

  if (!inbuffer || !outbuffer) {
    CVP_LOGE ("%s Null pointer!", __func__);
    g_mutex_unlock (&lock);
    return CVP_FAIL;
  }

  if (savebuffer) {
    Timer t("Process time + MAP");
    CVP_LOGI("%s: Last saved buffer address %p", __func__, savebuffer);

    GstMapInfo cur_info;
    gint curMemFd =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (inbuffer, 0));
    if (!gst_buffer_map (inbuffer, &cur_info, GST_MAP_READ)) {
      CVP_LOGE ("%s Failed to map the inbuffer!!", __func__);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }

    void *cur_plane0 = cur_info.data;
    CVP_LOGI ("%s: This frame data address %p", __func__, cur_plane0);
    cvpMem curMem;
    curMem.nSize = buffersize;
    curMem.eType = CVP_MEM_NON_SECURE;
    curMem.pAddress = cur_plane0;
    curMem.nFD = curMemFd;
    if (curMem.nFD == -1) {
      CVP_LOGE ("%s Failed to get cur FD!!", __func__);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }
    curImage.pBuffer = &curMem;
    if (cvpMemRegister(pSessH, curImage.pBuffer) != CVP_SUCCESS ||
        cvpRegisterOpticalFlowImageBuf (pInitHandle, &curImage) != CVP_SUCCESS) {
      CVP_LOGI("%s: Failed to register cur image", __func__);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }
    CVP_LOGI ("%s: Register cur image success", __func__);
    CVP_LOGI ("%s: Register image buffers to optical flow", __func__);
    CVP_LOGI ("%s: Cur Image size in imageinfo: %d",
        __func__, curImage.sImageInfo.nTotalSize);
    CVP_LOGI ("%s:              size in buffer: %d",
        __func__, curImage.pBuffer->nSize);
    CVP_LOGI ("%s:              buffer address: %p",
        __func__, curImage.pBuffer->pAddress);

    // access old plane info
    GstMapInfo ref_info;
    gint refMemFd =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (savebuffer, 0));
    if (!gst_buffer_map (savebuffer, &ref_info, GST_MAP_READ)) {
      CVP_LOGE ("%s Failed to map the savebuffer buffer!!", __func__);
      FreeCurImageBuffer ();
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }

    void *ref_plane0 = cur_info.data;
    cvpMem refMem;
    refMem.nSize = buffersize;
    refMem.eType = CVP_MEM_NON_SECURE;
    refMem.pAddress = ref_plane0;
    refMem.nFD = refMemFd;
    if (refMem.nFD == -1) {
      CVP_LOGE ("%s Failed to get ref FD!!", __func__);
      FreeCurImageBuffer ();
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }
    refImage.pBuffer = &refMem;
    if (cvpMemRegister(pSessH, &refMem) != CVP_SUCCESS ||
        cvpRegisterOpticalFlowImageBuf (pInitHandle, &refImage) != CVP_SUCCESS) {
      CVP_LOGI("%s: Register Ref image failed", __func__);
      FreeCurImageBuffer ();
      gst_buffer_unmap (savebuffer, &ref_info);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }
    CVP_LOGI ("%s: Register ref image success", __func__);
    CVP_LOGI ("%s: Ref Image size in imageinfo: %d",
        __func__, refImage.sImageInfo.nTotalSize);
    CVP_LOGI ("%s:           size in buffer: %d",
        __func__, refImage.pBuffer->nSize);
    CVP_LOGI ("%s:           buffer address: %p",
        __func__, refImage.pBuffer->pAddress);

    GstMapInfo out_info0;
    GstMapInfo out_info1;
    gint outMemFd0 =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));
    gint outMemFd1 =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 1));

    if (!gst_buffer_map_range (outbuffer, 0, 1, &out_info0, GST_MAP_READWRITE)) {
      CVP_LOGE ("%s Failed to map the mv buffer!!", __func__);
      FreeCurImageBuffer ();
      FreeRefImageBuffer ();
      gst_buffer_unmap (savebuffer, &ref_info);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }

    if (config.bStatsEnable &&
        !gst_buffer_map_range (outbuffer, 1, 1, &out_info1, GST_MAP_READWRITE)) {
      CVP_LOGE ("%s Failed to map the mv buffer!!", __func__);
      FreeCurImageBuffer ();
      FreeRefImageBuffer ();
      gst_buffer_unmap (savebuffer, &ref_info);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }

    cvpMem outMemVect;
    outMemVect.nSize = out_info0.size;
    outMemVect.eType = CVP_MEM_NON_SECURE;
    outMemVect.pAddress = out_info0.data;
    outMemVect.nFD = outMemFd0;
    if (outMemVect.nFD == -1) {
      CVP_LOGE ("%s Failed to get MV FD!!", __func__);
      FreeCurImageBuffer ();
      FreeRefImageBuffer ();
      gst_buffer_unmap (savebuffer, &ref_info);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }
    outputBuf[0].pMotionVector = &outMemVect;

    if (cvpMemRegister(pSessH, outputBuf[0].pMotionVector) != CVP_SUCCESS) {
      CVP_LOGI("%s: Register pMotionVector failed", __func__);
      FreeCurImageBuffer ();
      FreeRefImageBuffer ();
      gst_buffer_unmap (outbuffer, &out_info0);
      gst_buffer_unmap (savebuffer, &ref_info);
      gst_buffer_unmap (inbuffer, &cur_info);
      gst_buffer_unref (savebuffer);
      savebuffer = NULL;
      g_mutex_unlock (&lock);
      return CVP_FAIL;
    }

    if (config.bStatsEnable) {
      cvpMem outMemStats;
      outMemStats.nSize = out_info1.size;
      outMemStats.eType = CVP_MEM_NON_SECURE;
      outMemStats.pAddress = out_info1.data;
      outMemStats.nFD = outMemFd1;
      if (outMemStats.nFD == -1) {
        CVP_LOGE ("%s Failed to get stats FD!!", __func__);
        FreeCurImageBuffer ();
        FreeRefImageBuffer ();
        FreeMVBuffer ();
        gst_buffer_unmap (outbuffer, &out_info0);
        gst_buffer_unmap (savebuffer, &ref_info);
        gst_buffer_unmap (inbuffer, &cur_info);
        gst_buffer_unref (savebuffer);
        savebuffer = NULL;
        g_mutex_unlock (&lock);
        return CVP_FAIL;
      }
      outputBuf[0].pStats = &outMemStats;

      if (cvpMemRegister(pSessH, outputBuf[0].pStats) != CVP_SUCCESS) {
        CVP_LOGI("%s: Register pMotionVector failed", __func__);
        FreeCurImageBuffer ();
        FreeRefImageBuffer ();
        FreeMVBuffer ();
        gst_buffer_unmap (outbuffer, &out_info1);
        gst_buffer_unmap (outbuffer, &out_info0);
        gst_buffer_unmap (savebuffer, &ref_info);
        gst_buffer_unmap (inbuffer, &cur_info);
        gst_buffer_unref (savebuffer);
        savebuffer = NULL;
        g_mutex_unlock (&lock);
        return CVP_FAIL;
      }
    }

    CVP_LOGI ("%s: This frame buffer %p has ref count %d",
        __func__, inbuffer, GST_MINI_OBJECT_REFCOUNT_VALUE(inbuffer));
    CVP_LOGI ("%s: Last frame buffer %p has ref count %d",
        __func__, savebuffer, GST_MINI_OBJECT_REFCOUNT_VALUE(savebuffer));

    // optical flow sync
    {
      Timer t ("Process time");
      CVP_LOGI ("%s: CPU OF_Sync Call", __func__);
      res = cvpOpticalFlow_Sync (pInitHandle, &refImage, &curImage,
          newRef, newCur, outputBuf);// CVP Sync call
      if (res != CVP_SUCCESS) {
        CVP_LOGE ("%s: ERROR! cvpOpticalFlow_Sync Failed", __func__);
        if (res == CVP_EFAIL)
          CVP_LOGE ("%s: General failure", __func__);
        else if (res == CVP_EUNALIGNPARAM)
          CVP_LOGE ("%s: Unaligned pointer parameter", __func__);
        else if (res == CVP_EBADPARAM)
          CVP_LOGE ("%s: Bad parameters", __func__);
        else if (res == CVP_ENORES)
          CVP_LOGE ("%s: Insufficient resources, memory, etc", __func__);
        else if (res == CVP_EFATAL)
          CVP_LOGE ("%s: Fatal error", __func__);
      }
      else {
        CVP_LOGI ("%s: Optical flow sync successful", __func__);
      }
    }

    if (FreeCurImageBuffer () != CVP_SUCCESS ||
        FreeRefImageBuffer () != CVP_SUCCESS ||
        FreeMVBuffer () != CVP_SUCCESS)
      res = CVP_FAIL;

    if (config.bStatsEnable && FreeStatsBuffer () != CVP_SUCCESS)
      res = CVP_FAIL;

    CVP_LOGI ("%s: Memory deregister successful", __func__);

    // previous buffer cleanup
    CVP_LOGI ("%s: Last frame buffer %p has ref count %d",
        __func__, savebuffer,  GST_MINI_OBJECT_REFCOUNT_VALUE(savebuffer));
    gst_buffer_unmap (inbuffer, &cur_info);
    gst_buffer_unmap (savebuffer, &ref_info);
    if (config.bStatsEnable)
      gst_buffer_unmap (outbuffer, &out_info1);
    gst_buffer_unmap (outbuffer, &out_info0);
    gst_buffer_unref (savebuffer);
    savebuffer = NULL;
  }

  // increase ref count
  if (res == CVP_OK) {
    gst_buffer_ref (inbuffer);
    savebuffer = inbuffer;
    CVP_LOGI ("%s: New save buffer address %p has ref count %d",
        __func__, savebuffer, GST_MINI_OBJECT_REFCOUNT_VALUE (savebuffer));
  }

  CVP_LOGI ("%s: Exit", __func__);
  g_mutex_unlock (&lock);
  return res;
}

int32_t OFEngine::FreeCurImageBuffer () {
  CVP_LOGI ("%s: Enter", __func__);
  CVP_LOGI ("%s: Deregister cur image from optical flow", __func__);
  cvpDeregisterOpticalFlowImageBuf (pInitHandle, &curImage);
  if (cvpMemDeregister (pSessH, curImage.pBuffer) != CVP_SUCCESS) {
    CVP_LOGE("%s: Ref, Cur image buffer and output buffer deregister fail",
        __func__);
    return CVP_FAIL;
  }
  CVP_LOGI ("%s: Exit", __func__);
  return CVP_SUCCESS;
}

int32_t OFEngine::FreeRefImageBuffer () {
  CVP_LOGI ("%s: Enter", __func__);
  CVP_LOGI ("%s: Deregister ref image from optical flow", __func__);
  cvpDeregisterOpticalFlowImageBuf (pInitHandle, &refImage);
  if (cvpMemDeregister (pSessH, refImage.pBuffer) != CVP_SUCCESS) {
    CVP_LOGE("%s: Ref, Cur image buffer and output buffer deregister fail",
        __func__);
    return CVP_FAIL;
  }
  CVP_LOGI ("%s: Exit", __func__);
  return CVP_SUCCESS;
}

int32_t OFEngine::FreeMVBuffer () {
  CVP_LOGI ("%s: Enter", __func__);
  if (cvpMemDeregister (pSessH, outputBuf[0].pMotionVector) != CVP_SUCCESS) {
    CVP_LOGE("%s: Ref, Cur image buffer and output buffer deregister fail",
        __func__);
    return CVP_FAIL;
  }
  CVP_LOGI ("%s: Exit", __func__);
  return CVP_SUCCESS;
}

int32_t OFEngine::FreeStatsBuffer () {
  CVP_LOGI ("%s: Enter", __func__);
  if (cvpMemDeregister (pSessH, outputBuf[0].pStats) != CVP_SUCCESS) {
    CVP_LOGE("%s: Ref, Cur image buffer and output buffer deregister fail",
        __func__);
    return CVP_FAIL;
  }
  CVP_LOGI ("%s: Exit", __func__);
  return CVP_SUCCESS;
}
}; // namespace cvp
