/*
 * Copyright (c) 2018, 2019-2020, The Linux Foundation. All rights reserved.
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

#include <cutils/properties.h>
#include <linux/msm_ion.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <utils/Log.h>

#if TARGET_ION_ABI_VERSION >= 2
#include <ion/ion.h>
#include <linux/dma-buf.h>
#else
#include <fcntl.h>
#endif

namespace qmmf {

/** SyncStart
 *    @fd: ion fd
 *
 * Start CPU Access
 *
 **/
inline void SyncStart (int32_t fd)
{
  ALOGV ("%s: Enter", __func__);
#if TARGET_ION_ABI_VERSION >= 2
  struct dma_buf_sync buf_sync;
  buf_sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

  auto result = ioctl (fd, DMA_BUF_IOCTL_SYNC, &buf_sync);
  if (result)
    ALOGE ("%s: Failed first DMA_BUF_IOCTL_SYNC start", __func__);
#endif
  ALOGV ("%s: Exit", __func__);
}

/** SyncEnd
 *    @fd: ion fd
 *
 * End CPU Access
 *
 **/
inline void SyncEnd (int32_t fd)
{
  ALOGV ("%s: Enter", __func__);
#if TARGET_ION_ABI_VERSION >= 2
  struct dma_buf_sync buf_sync;
  buf_sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

  auto result = ioctl (fd, DMA_BUF_IOCTL_SYNC, &buf_sync);
  if (result)
    ALOGE ("%s: Failed first DMA_BUF_IOCTL_SYNC End", __func__);
#endif
  ALOGV ("%s: Exit", __func__);
}

}  // namespace qmmf
