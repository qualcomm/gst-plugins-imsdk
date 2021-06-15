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

OFEngine::OFEngine (CVPConfig &config) : config_(config) {
  is_start    = true;
  frameid     = 0;
  buffersize  = 0;
  Init_Handle = NULL;
  pSessH      = NULL;
  savebuffer  = NULL;
  g_mutex_init (&lock_);
}

OFEngine::~OFEngine () {
  g_mutex_clear (&lock_);
}

int32_t OFEngine::Init() {
  int res = CVP_OK;
  CVP_LOGI("%s: Enter", __func__);

  // init CVP session
  pSessH = cvpCreateSession (NULL, NULL);
  if (pSessH == nullptr) {
    CVP_LOGE("%s: Create session failed, NULL session handle returned",
             __func__);
  }
  CVP_LOGI("%s: CVP Session %p", __func__, pSessH);

  // Init call config Parameters
  pConfig.nActualFps         = config_.fps;
  pConfig.nOperationalFps    = config_.fps;
  pConfig.sImageInfo.nWidth  = config_.source_info.width;
  pConfig.sImageInfo.nHeight = config_.source_info.height;
  pConfig.eMode              = (cvpOpticalFlowMode)1; // seven-pass
  pConfig.bStatsEnable       = config_.stats_enable;
  switch (config_.source_info.format) {
    case CVPImageFormat::cvp_format_nv12:
      pConfig.sImageInfo.eFormat = CVP_COLORFORMAT_NV12;
      break;
    case CVPImageFormat::cvp_format_nv21:
    case CVPImageFormat::cvp_format_gray8bit:
      pConfig.sImageInfo.eFormat = CVP_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      CVP_LOGE("%s: ERROR, invalid video format", __func__);
      return CVP_FAIL;
  }

  CVP_LOGI("%s: Query image info", __func__);
  cvpImageInfo pQueryImageInfo;
  cvpStatus query = cvpQueryImageInfo(pConfig.sImageInfo.eFormat,
                                      pConfig.sImageInfo.nWidth,
                                      pConfig.sImageInfo.nHeight,
                                      &pQueryImageInfo);
  if (query != CVP_SUCCESS) {
    CVP_LOGE("%s: Error! Query image info failed", __func__);
    return query;
  }

  CVP_LOGI("%s:      Width      %d", __func__, pQueryImageInfo.nWidth);
  CVP_LOGI("%s:      Height     %d", __func__, pQueryImageInfo.nHeight);
  CVP_LOGI("%s:      Format     %d", __func__, pQueryImageInfo.eFormat);

  pConfig.sImageInfo.nPlane = pQueryImageInfo.nPlane;
  gint heightAlligned = (config_.source_info.height + 64-1) & ~(64-1);
  if (pConfig.sImageInfo.nPlane == 1) {
    gsize size = config_.source_info.stride * heightAlligned;
    pConfig.sImageInfo.nTotalSize = size;
    pConfig.sImageInfo.nWidthStride[0] = config_.source_info.stride;
    pConfig.sImageInfo.nAlignedSize[0] = size;
    CVP_LOGI("%s:      Plane width stride %d, aligned size %d",
              __func__,
              pConfig.sImageInfo.nWidthStride[0],
              pConfig.sImageInfo.nAlignedSize[0]);
  } else {
    pConfig.sImageInfo.nTotalSize = pQueryImageInfo.nTotalSize;
    for (unsigned int i = 0; i < pConfig.sImageInfo.nPlane; i++) {
      pConfig.sImageInfo.nWidthStride[i] = pQueryImageInfo.nWidthStride[i];
      pConfig.sImageInfo.nAlignedSize[i] = pQueryImageInfo.nAlignedSize[i];
      CVP_LOGI("%s:      Plane %d width stride %d, aligned size %d",
                __func__, i,
                pConfig.sImageInfo.nWidthStride[i],
                pConfig.sImageInfo.nAlignedSize[i]);
    }
  }

  CVP_LOGI("%s:      Plane      %d", __func__, pConfig.sImageInfo.nPlane);
  CVP_LOGI("%s:      Total size %d", __func__, pConfig.sImageInfo.nTotalSize);

  CVP_LOGI("%s: Optical flow Config", __func__);
  CVP_LOGI("%s:  actual fps:      %d", __func__, pConfig.nActualFps);
  CVP_LOGI("%s:  operational fps: %d", __func__, pConfig.nOperationalFps);
  CVP_LOGI("%s:  format:          %d", __func__, pConfig.sImageInfo.eFormat);
  CVP_LOGI("%s:  width:           %d", __func__, pConfig.sImageInfo.nWidth);
  CVP_LOGI("%s:  height:          %d", __func__, pConfig.sImageInfo.nHeight);
  CVP_LOGI("%s:  total size:      %d", __func__, pConfig.sImageInfo.nTotalSize);
  CVP_LOGI("%sl  plane:           %d", __func__, pConfig.sImageInfo.nPlane);
  CVP_LOGI("%s:  pass type:       %d", __func__, pConfig.eMode);
  CVP_LOGI("%s:  stats enable:    %d", __func__, pConfig.bStatsEnable);

  CVP_LOGI("%s: Optical flow Adv Config", __func__);
  CVP_LOGI("%s:  MV Dist:                 %d", __func__, pAdvConfig.nMvDist);
  CVP_LOGI("%s:  median filter type:      %d", __func__,
                                               pAdvConfig.nMedianFiltType);
  CVP_LOGI("%s:  threshold median filter: %d", __func__,
                                               pAdvConfig.nThresholdMedFilt);
  CVP_LOGI("%s:  smoothness penalty:      %d",
           __func__, pAdvConfig.nSmoothnessPenaltyThresh);
  CVP_LOGI("%s:  search range X:          %d", __func__,
                                               pAdvConfig.nSearchRangeX);
  CVP_LOGI("%s:  search range Y:          %d", __func__,
                                               pAdvConfig.nSearchRangeY);
  CVP_LOGI("%s:  enable EIC:              %d", __func__, pAdvConfig.bEnableEic);

  //Init Call
  Init_Handle = cvpInitOpticalFlow(pSessH, &pConfig, &pAdvConfig, &pOutMemReq,
                                   NULL, NULL);
  if (Init_Handle == NULL) {
    CVP_LOGE("%s: ERROR cvpInitOpticalFlow call failed. Init_Handle is NULL",
             __func__);
    return CVP_FAIL;
  }

  CVP_LOGI("%s: CVP optical flow initialized", __func__);
  CVP_LOGI("%s: CVP Output MV bytes:    %d", __func__,
                                             pOutMemReq.nMotionVectorBytes);
  if (pConfig.bStatsEnable) {
    CVP_LOGI("%s: CVP Output Stats bytes: %d", __func__,
                                               pOutMemReq.nStatsBytes);
  }

  // allocate image and output buffer
  res = AllocateBuffer();
  if (res != CVP_SUCCESS) {
    CVP_LOGE("%s: Buffer allocation failed", __func__);
  }

  // CVP Start Session
  res = cvpStartSession (pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGE("%s: CVP session fail to start", __func__);
    return CVP_FAIL;
  }
  CVP_LOGI("%s: CVP session start", __func__);

  CVP_LOGI("%s: Exit", __func__);
  return res;
}

void OFEngine::Deinit() {
  int res = CVP_OK;
  CVP_LOGI("%s: Enter", __func__);

  // CVP Stop Session
  res = cvpStopSession(pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGI("%s: Stop CVP session Failed", __func__);
  }
  CVP_LOGI("%s: CVP session stopped", __func__);

  // Free image and output buffer
  FreeBuffer();

  // OF De-Initialization
  res = cvpDeInitOpticalFlow(Init_Handle);
  if (res != CVP_SUCCESS) {
    CVP_LOGI("%s: DeInit Optical flow Failed", __func__);
    return;
  }
  else {
    CVP_LOGI("%s: Optical flow deinit", __func__);
  }

  // CVP Delete session
  res = cvpDeleteSession(pSessH);
  if (res != CVP_SUCCESS) {
    CVP_LOGI("%s: cvpDeleteSession Failed", __func__);
    return;
  }
  else {
    CVP_LOGI("%s: CVP session deleted", __func__);
  }

  CVP_LOGI("%s: Exit", __func__);
  return;
}

void OFEngine::Flush () {
  g_mutex_lock (&lock_);
  CVP_LOGI("%s: Enter", __func__);

  // Unref the saved buffer
  if (savebuffer && GST_MINI_OBJECT_REFCOUNT_VALUE(savebuffer) > 0)
    gst_buffer_unref (savebuffer);

  CVP_LOGI("%s: Exit", __func__);
  g_mutex_unlock (&lock_);
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
int32_t OFEngine::Process(GstBuffer * inbuffer, GstBuffer * outbuffer) {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  g_mutex_lock (&lock_);

  if (!inbuffer || !outbuffer) {
    CVP_LOGE ("%s Null pointer!", __func__);
    g_mutex_unlock (&lock_);
    return CVP_FAIL;
  }

  if (!is_start) {
    Timer t("Process time + MAP");
    CVP_LOGI("%s: Last saved buffer address %p", __func__, savebuffer);

    GstMapInfo cur_info;
    gint curMemFd =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (inbuffer, 0));
    if (!gst_buffer_map (inbuffer, &cur_info, GST_MAP_READ)) {
      CVP_LOGE ("%s Failed to map the inbuffer!!", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }

    void *cur_plane0 = cur_info.data;
    CVP_LOGI("%s: This frame data address %p", __func__, cur_plane0);
    cvpMem curMem;
    curMem.nSize = buffersize;
    curMem.eType = CVP_MEM_NON_SECURE;
    curMem.pAddress = cur_plane0;
    curMem.nFD = curMemFd;
    if (curMem.nFD == -1) {
      CVP_LOGE ("%s Failed to get cur FD!!", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }
    pCurImage.pBuffer = &curMem;
    if (cvpMemRegister(pSessH, pCurImage.pBuffer) != CVP_SUCCESS) {
      CVP_LOGI("%s: Register Cur image failed", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }
    CVP_LOGI("%s: Register cur image success", __func__);
    CVP_LOGI("%s: Cur Image size in imageinfo: %d", __func__, pCurImage.sImageInfo.nTotalSize);

    CVP_LOGI("%s:              size in buffer: %d", __func__, pCurImage.pBuffer->nSize);
    CVP_LOGI("%s:              buffer address: %p", __func__, pCurImage.pBuffer->pAddress);

    // access old plane info
    GstMapInfo ref_info;
    gint refMemFd =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (savebuffer, 0));
    if (!gst_buffer_map (savebuffer, &ref_info, GST_MAP_READ)) {
      CVP_LOGE ("%s Failed to map the savebuffer buffer!!", __func__);
      g_mutex_unlock (&lock_);
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
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }
    if (cvpMemRegister(pSessH, &refMem) != CVP_SUCCESS) {
      CVP_LOGI("%s: Register Ref image failed", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }
    CVP_LOGI("%s: Register ref image success", __func__);
    pRefImage.pBuffer = &refMem;
    CVP_LOGI("%s: Ref Image size in imageinfo: %d", __func__, pRefImage.sImageInfo.nTotalSize);
    CVP_LOGI("%s:           size in buffer: %d", __func__, pRefImage.pBuffer->nSize);
    CVP_LOGI("%s:           buffer address: %p", __func__, pRefImage.pBuffer->pAddress);

    GstMapInfo out_info0;
    GstMapInfo out_info1;
    gint outMemFd0 =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));
    gint outMemFd1 =
        gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 1));

    if (!gst_buffer_map_range (outbuffer, 0, 1, &out_info0, GST_MAP_READWRITE)) {
      CVP_LOGE ("%s Failed to map the mv buffer!!", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }

    if (pConfig.bStatsEnable &&
        !gst_buffer_map_range (outbuffer, 1, 1, &out_info1, GST_MAP_READWRITE)) {
      CVP_LOGE ("%s Failed to map the mv buffer!!", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }

    cvpMem outMemVect;
    outMemVect.nSize = out_info0.size;
    outMemVect.eType = CVP_MEM_NON_SECURE;
    outMemVect.pAddress = out_info0.data;
    outMemVect.nFD = outMemFd0;
    if (outMemVect.nFD == -1) {
      CVP_LOGE ("%s Failed to get MV FD!!", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }
    pOutput[0].pMotionVector = &outMemVect;

    if (cvpMemRegister(pSessH, pOutput[0].pMotionVector) != CVP_SUCCESS) {
      CVP_LOGI("%s: Register pMotionVector failed", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }

    if (pConfig.bStatsEnable) {
      cvpMem outMemStats;
      outMemStats.nSize = out_info1.size;
      outMemStats.eType = CVP_MEM_NON_SECURE;
      outMemStats.pAddress = out_info1.data;
      outMemStats.nFD = outMemFd1;
      if (outMemStats.nFD == -1) {
        CVP_LOGE ("%s Failed to get stats FD!!", __func__);
        g_mutex_unlock (&lock_);
        return CVP_FAIL;
      }
      pOutput[0].pStats = &outMemStats;

      if (cvpMemRegister(pSessH, pOutput[0].pStats) != CVP_SUCCESS) {
        CVP_LOGI("%s: Register pMotionVector failed", __func__);
        g_mutex_unlock (&lock_);
        return CVP_FAIL;
      }
    }

    // register current image
    res = cvpRegisterOpticalFlowImageBuf(Init_Handle, &pRefImage);
    if (res != CVP_SUCCESS) {
      CVP_LOGE("%s: Unable to register ref image buffer", __func__);
      g_mutex_unlock (&lock_);
      return res;
    }
    res = cvpRegisterOpticalFlowImageBuf(Init_Handle, &pCurImage);
    if (res != CVP_SUCCESS) {
      CVP_LOGE("%s: Unable to register cur image buffer", __func__);
      g_mutex_unlock (&lock_);
      return res;
    }
    CVP_LOGI("%s: Register image buffers to optical flow", __func__);

    CVP_LOGI("%s: This frame buffer %p has ref count %d", __func__,
             inbuffer,
             GST_MINI_OBJECT_REFCOUNT_VALUE(inbuffer));
    CVP_LOGI("%s: Last frame buffer %p has ref count %d", __func__,
             savebuffer,
             GST_MINI_OBJECT_REFCOUNT_VALUE(savebuffer));

    // optical flow sync
    {
      Timer t("Process time");
      CVP_LOGI("%s: CPU OF_Sync Call", __func__);
      res = cvpOpticalFlow_Sync (Init_Handle, &pRefImage, &pCurImage,
                                 pNewRef, pNewCur, pOutput);// CVP Sync call
      if (res != CVP_SUCCESS) {
        CVP_LOGE("%s: ERROR! cvpOpticalFlow_Sync Failed", __func__);
        if (res == CVP_EFAIL) {
          CVP_LOGE("%s: General failure", __func__);
        }
        else if (res == CVP_EUNALIGNPARAM) {
          CVP_LOGE("%s: Unaligned pointer parameter", __func__);
        }
        else if (res == CVP_EBADPARAM) {
          CVP_LOGE("%s: Bad parameters", __func__);
        }
        else if (res == CVP_ENORES) {
          CVP_LOGE("%s: Insufficient resources, memory, etc", __func__);
        }
        else if (res == CVP_EFATAL) {
          CVP_LOGE("%s: Fatal error", __func__);
        }
        g_mutex_unlock (&lock_);
        return res;
      }
      else {
        CVP_LOGI("%s: Optical flow sync successful", __func__);
      }
    }

    // DeRegister Reference Image
    CVP_LOGI("%s: Deregister image from optical flow", __func__);
    cvpDeregisterOpticalFlowImageBuf(Init_Handle, &pRefImage);
    cvpDeregisterOpticalFlowImageBuf(Init_Handle, &pCurImage);

    if (cvpMemDeregister (pSessH, pRefImage.pBuffer) != CVP_SUCCESS ||
        cvpMemDeregister (pSessH, pCurImage.pBuffer) != CVP_SUCCESS ||
        cvpMemDeregister (pSessH, pOutput[0].pMotionVector) != CVP_SUCCESS ||
        cvpMemDeregister (pSessH, pOutput[0].pStats) != CVP_SUCCESS) {
      CVP_LOGE("%s: Memory deregister fail", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }

    if (pConfig.bStatsEnable &&
        cvpMemDeregister (pSessH, pOutput[0].pStats) != CVP_SUCCESS) {
      CVP_LOGE("%s: Memory deregister fail", __func__);
      g_mutex_unlock (&lock_);
      return CVP_FAIL;
    }

    CVP_LOGI("%s: Memory deregister successful", __func__);

    // previous buffer cleanup
    CVP_LOGI("%s: Last frame buffer %p has ref count %d", __func__,
                 savebuffer,
                 GST_MINI_OBJECT_REFCOUNT_VALUE(savebuffer));
    gst_buffer_unmap (inbuffer, &cur_info);
    gst_buffer_unmap (savebuffer, &ref_info);
    if (pConfig.bStatsEnable)
      gst_buffer_unmap (outbuffer, &out_info1);
    gst_buffer_unmap (outbuffer, &out_info0);
    gst_buffer_unref (savebuffer);

    CVP_LOGI("%s: This frame buffer %p has ref count %d", __func__,
             inbuffer,
             GST_MINI_OBJECT_REFCOUNT_VALUE(inbuffer));
    CVP_LOGI("%s: Last frame buffer %p has ref count %d", __func__,
             savebuffer,
             GST_MINI_OBJECT_REFCOUNT_VALUE(savebuffer));
    savebuffer = NULL;
  }
  is_start = false;

  // increase ref count
  gst_buffer_ref (inbuffer);
  savebuffer = inbuffer;
  CVP_LOGI ("%s: New save buffer address %p has ref count %d",
             __func__, savebuffer, GST_MINI_OBJECT_REFCOUNT_VALUE (savebuffer));

  CVP_LOGI("%s: Exit", __func__);
  g_mutex_unlock (&lock_);
  return res;
}

/* Allocate buffer
 * 1. Output buffer and stats buffer
 * 2. Image buffer - only allocate ref is there is not already one
 */
int32_t OFEngine::AllocateBuffer() {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  if (pSessH == nullptr) {
    CVP_LOGE("%s: Session handle", __func__);
  }

  // allocate image buffer
  int32_t numbytes = 0;

  //Ref Image
  pRefImage.sImageInfo.nWidth = pConfig.sImageInfo.nWidth;
  pRefImage.sImageInfo.nHeight = pConfig.sImageInfo.nHeight;
  pRefImage.sImageInfo.eFormat = pConfig.sImageInfo.eFormat;
  pRefImage.sImageInfo.nPlane = pConfig.sImageInfo.nPlane;
  pRefImage.sImageInfo.nTotalSize = pConfig.sImageInfo.nTotalSize;
  for (unsigned int i = 0; i < pRefImage.sImageInfo.nPlane; i++) {
    pRefImage.sImageInfo.nWidthStride[i] = pConfig.sImageInfo.nWidthStride[i];
    pRefImage.sImageInfo.nAlignedSize[i] = pConfig.sImageInfo.nAlignedSize[i];
    numbytes += pRefImage.sImageInfo.nAlignedSize[i];
  }
  buffersize = numbytes;

  //Cur Image
  numbytes = 0;
  pCurImage.sImageInfo.nWidth = pConfig.sImageInfo.nWidth;
  pCurImage.sImageInfo.nHeight = pConfig.sImageInfo.nHeight;
  pCurImage.sImageInfo.eFormat = pConfig.sImageInfo.eFormat;
  pCurImage.sImageInfo.nPlane = pConfig.sImageInfo.nPlane;
  pCurImage.sImageInfo.nTotalSize = pConfig.sImageInfo.nTotalSize;
  for (unsigned int i = 0; i < pCurImage.sImageInfo.nPlane; i++) {
    pCurImage.sImageInfo.nWidthStride[i] = pConfig.sImageInfo.nWidthStride[i];
    pCurImage.sImageInfo.nAlignedSize[i] = pConfig.sImageInfo.nAlignedSize[i];
    numbytes += pCurImage.sImageInfo.nAlignedSize[i];
  }
  if (numbytes != buffersize) {
    CVP_LOGE("%s: RefImage buffer size %d is different from CurImage buffer size %d",
        __func__, buffersize, numbytes);
  }
  if (numbytes != buffersize) {
    CVP_LOGE("%s: Ref buffer %d and cur buffer %d mismatch", __func__,
             buffersize, numbytes);
    return CVP_FAIL;
  }

  return res;
}

/* Free buffer
 * 1. Free output and stats buffer
 * 2. Free image buffer
 *      if keep_ref is set, keep a ref image buffer
 *      set cur image buffer to ref
 *      free original ref image buffer
 *      else, free both
 */
int32_t OFEngine::FreeBuffer() {
  CVP_LOGI("%s: Enter", __func__);
  int res = CVP_OK;

  // free image buffer
  if (cvpMemDeregister (pSessH, pCurImage.pBuffer) != CVP_SUCCESS) {
    CVP_LOGE("%s: Memory deregister fail", __func__);
  }
  CVP_LOGI("%s: CurImage memory deregister successful", __func__);

  CVP_LOGI("%s: Exit", __func__);
  return res;
}

}; // namespace cvp
