/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_DRM_DECRYPTOR_ENGINE_H__
#define __GST_DRM_DECRYPTOR_ENGINE_H__

#include <gst/allocators/allocators.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <cutils/native_handle.h>

#include <media/hardware/CryptoAPI.h>
#include <media/drm/DrmAPI.h>
#ifdef ENABLE_WIDEVINE
#include <ce_cdm/cdm.h>
#endif

G_BEGIN_DECLS

#define GST_DRM_DECRYPTOR_ENGINE(obj)    ((GstDrmDecryptorEngine*)(obj))

#define PLAYREADY_SYSTEM_ID       "9a04f079-9840-4286-ab92-e65be0885f95"
#define WIDEVINE_SYSTEM_ID        "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"

struct GstDrmDecryptorEngine {
  GstDrmDecryptorEngine () {}
  virtual ~GstDrmDecryptorEngine () {}

  virtual gboolean drm_plugin_init (gpointer session_id, gpointer instance) = 0;
  virtual gint decrypt (gboolean secure, GstMapInfo keyid_map_info,
      GstMapInfo inbuff_map_info, GstMapInfo subsample_map_info,
      guint subsample_count, const guint8 *iv_arr, native_handle_t *nh,
      gboolean is_clear) = 0;
};

GstDrmDecryptorEngine*
gst_drm_decryptor_engine_new (const gchar *sys_id, gpointer session_id,
    gpointer instance);

gint
gst_drm_decryptor_engine_execute (GstDrmDecryptorEngine* engine,
    GstBuffer *in_buffer, GstBuffer *out_buffer);

G_END_DECLS

#endif // __GST_DRM_DECRYPTOR_ENGINE_H__