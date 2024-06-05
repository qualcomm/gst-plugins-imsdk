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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <sstream>
#include <string>

#include <gst/gst.h>

#if defined(HAVE_LINUX_DMA_HEAP_H)
#include <linux/dma-heap.h>
#else
#include <linux/ion.h>
#include <linux/msm_ion.h>
#endif // HAVE_LINUX_DMA_HEAP_H

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize category = 0;

  if (g_once_init_enter (&category)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new("qtioverlay", 0,
        "QTI overlay");

    g_once_init_leave (&category, cat_done);
  }

  return (GstDebugCategory *) category;
}

namespace overlay {

/** SyncStart
 *    @fd: ion fd
 *
 * Start CPU Access
 *
 **/
inline void SyncStart (int32_t fd)
{
  GST_LOG ("Enter");
#ifdef HAVE_LINUX_DMA_BUF_H
  struct dma_buf_sync buf_sync;
  buf_sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

  auto result = ioctl (fd, DMA_BUF_IOCTL_SYNC, &buf_sync);
  if (result)
    GST_ERROR ("Failed first DMA_BUF_IOCTL_SYNC start");
#endif // HAVE_LINUX_DMA_BUF_H
  GST_LOG ("Exit");
}

/** SyncEnd
 *    @fd: ion fd
 *
 * End CPU Access
 *
 **/
inline void SyncEnd (int32_t fd)
{
  GST_LOG ("Enter");
#ifdef HAVE_LINUX_DMA_BUF_H
  struct dma_buf_sync buf_sync;
  buf_sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

  auto result = ioctl (fd, DMA_BUF_IOCTL_SYNC, &buf_sync);
  if (result)
    GST_ERROR ("Failed first DMA_BUF_IOCTL_SYNC End");
#endif // HAVE_LINUX_DMA_BUF_H
  GST_LOG ("Exit");
}
}  // namespace overlay
