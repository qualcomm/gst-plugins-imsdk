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

#ifndef GST_DISABLE_GST_DEBUG
#include <gst/gst.h>

#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize category = 0;

  if (g_once_init_enter (&category)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new("qtihexagonnn", 0,
        "QTI HexagonNN plugin");

    g_once_init_leave (&category, cat_done);
  }

  return (GstDebugCategory *) category;
}

#define NN_LOGE(...) GST_ERROR(__VA_ARGS__)
#define NN_LOGI(...) GST_INFO(__VA_ARGS__)
#define NN_LOGD(...) GST_DEBUG(__VA_ARGS__)
#define NN_LOGV(...) GST_LOG(__VA_ARGS__)
#else
#include <utils/Log.h>
#define NN_LOGE(...) ALOGE("NN: " __VA_ARGS__)
#define NN_LOGI(...) ALOGI("NN: " __VA_ARGS__)
#define NN_LOGD(...) ALOGD("NN: " __VA_ARGS__)
#define NN_LOGV(...) ALOGD("NN: " __VA_ARGS__)
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */
