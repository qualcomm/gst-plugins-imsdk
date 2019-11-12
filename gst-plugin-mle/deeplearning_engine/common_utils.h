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

#include <sstream>
#include <string>
#include <memory>
#include <cutils/properties.h>
#include <ion/ion.h>
#include <linux/dma-buf.h>
#include <linux/msm_ion.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <utils/Log.h>
#ifdef QMMF_ALG
#include <qmmf-alg/qmmf_alg_plugin.h>
#include <qmmf-alg/qmmf_alg_utils.h>
#endif


#define VAM_ML_LOGI(...) ALOGI("MLWrapper: " __VA_ARGS__)
#define VAM_ML_LOGE(...) ALOGE("MLWrapper: " __VA_ARGS__)
#define VAM_ML_LOGD(...) ALOGD("MLWrapper: " __VA_ARGS__)

#define MLE_UNUSED(var) ((void)var)

/** SyncStart
 *    @fd: ion fd
 *
 * Start CPU Access
 *
 **/
inline void SyncStart(int32_t fd) {
  VAM_ML_LOGD("%s: Enter", __func__);
#if TARGET_ION_ABI_VERSION >= 2
  struct dma_buf_sync buf_sync;
  buf_sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

  auto result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &buf_sync);
  if (result) {
    VAM_ML_LOGE("%s: Failed first DMA_BUF_IOCTL_SYNC start", __func__);
  }
#else
  MLE_UNUSED(fd);
#endif
  VAM_ML_LOGD("%s: Exit", __func__);
}

/** SyncEnd
 *    @fd: ion fd
 *
 * End CPU Access
 *
 **/
inline void SyncEnd(int32_t fd) {
  VAM_ML_LOGD("%s: Enter", __func__);
#if TARGET_ION_ABI_VERSION >= 2
  struct dma_buf_sync buf_sync;
  buf_sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

  auto result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &buf_sync);
  if (result) {
    VAM_ML_LOGE("%s: Failed first DMA_BUF_IOCTL_SYNC End", __func__);
  }
#else
  MLE_UNUSED(fd);
#endif
  VAM_ML_LOGD("%s: Exit", __func__);
}

class Property {
 public:
  /** Get
   *    @property: property
   *    @default_value: default value
   *
   * Gets requested property value
   *
   * return: property value
   **/
  template <typename TProperty>
  static TProperty Get(std::string property, TProperty default_value) {
    TProperty value = default_value;
    char prop_val[PROPERTY_VALUE_MAX];
    std::stringstream s;
    s << default_value;
    property_get(property.c_str(), prop_val, s.str().c_str());

    std::stringstream output(prop_val);
    output >> value;
    return value;
  }

  /** Set
   *    @property: property
   *    @value: value
   *
   * Sets requested property value
   *
   * return: nothing
   **/
  template <typename TProperty>
  static void Set(std::string property, TProperty value) {
    std::stringstream s;
    s << value;
    std::string value_string = s.str();
    value_string.resize(PROPERTY_VALUE_MAX);
    property_set(property.c_str(), value_string.c_str());
  }
};

#ifdef QMMF_ALG
class MLTools : public qmmf::qmmf_alg_plugin::ITools {
 public:
  ~MLTools(){};

  void SetProperty(std::string property, std::string value) {
    Property::Set(property, value);
  }

  void SetProperty(std::string property, int32_t value) {
    Property::Set(property, value);
  }

  const std::string GetProperty(std::string property,
                                std::string default_value) {
    return Property::Get(property, default_value);
  }

  uint32_t GetProperty(std::string property, int32_t default_value) {
    return Property::Get(property, default_value);
  }

  void LogError(const std::string &s) { ALOGE("%s", s.c_str()); }

  void LogWarning(const std::string &s) { ALOGW("%s", s.c_str()); }

  void LogInfo(const std::string &s) { ALOGI("%s", s.c_str()); }

  void LogDebug(const std::string &s) { ALOGD("%s", s.c_str()); }

  void LogVerbose(const std::string &s) { ALOGV("%s", s.c_str()); }

  /*TODO add proper implementation of below methods
   * Note: Currently, ML unified engine is responsible for buffer management
   */
  std::shared_ptr<qmmf::qmmf_alg_plugin::IBufferHolder> ImportBufferHolder(
      const uint8_t *vaddr, const int32_t fd, const uint32_t size,
      const bool cached) {
    std::shared_ptr<qmmf::qmmf_alg_plugin::IBufferHolder> rv = nullptr;
    return rv;
  }

  std::shared_ptr<qmmf::qmmf_alg_plugin::IBufferHolder> NewBufferHolder(
      const uint32_t size, const bool cached) {
    std::shared_ptr<qmmf::qmmf_alg_plugin::IBufferHolder> rv = nullptr;
    return rv;
  }
};
#endif
