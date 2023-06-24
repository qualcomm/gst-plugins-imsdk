/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_DRM_DECRYPTOR_ENGINE_H__
#define __GST_DRM_DECRYPTOR_ENGINE_H__

#include <gst/allocators/allocators.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_DRM_DECRYPTOR_ENGINE(obj)    ((GstDrmDecryptorEngine*)(obj))

typedef struct _GstDrmDecryptorEngine GstDrmDecryptorEngine;

GST_API GstDrmDecryptorEngine *
gst_drm_decryptor_engine_new (const gchar *session_id);

GST_API void
gst_drm_decryptor_engine_free (GstDrmDecryptorEngine *engine);

GST_API gboolean
gst_drm_decryptor_engine_execute (GstDrmDecryptorEngine *engine,
    GstBuffer *in_buffer, GstBuffer *out_buffer);

G_END_DECLS

#endif // __GST_DRM_DECRYPTOR_ENGINE_H__