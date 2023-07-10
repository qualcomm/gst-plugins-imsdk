/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_DRM_DECRYPTOR_H__
#define __GST_DRM_DECRYPTOR_H__

#include <gst/gst.h>

#include "drmdecryptor-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_DRM_DECRYPTOR \
  (gst_drm_decryptor_get_type())
#define GST_DRM_DECRYPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DRM_DECRYPTOR, GstDrmDecryptor))
#define GST_DRM_DECRYPTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DRM_DECRYPTOR, GstDrmDecryptorClass))
#define GST_IS_DRM_DECRYPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DRM_DECRYPTOR))
#define GST_IS_DRM_DECRYPTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DRM_DECRYPTOR))
#define GST_DRM_DECRYPTOR_CAST(obj)         ((GstDrmDecryptor *)(obj))

typedef struct _GstDrmDecryptor GstDrmDecryptor;
typedef struct _GstDrmDecryptorClass GstDrmDecryptorClass;

struct _GstDrmDecryptor {
  GstElement              parent;

  GstPad                  *srcpad;
  GstPad                  *sinkpad;

  GstDrmDecryptorEngine   *engine;

  GstBufferPool           *pool;

  /// Properties
  gchar                   *session_id;

  gpointer                cdm_instance;
};

struct _GstDrmDecryptorClass {
  GstElementClass   parent;
};

G_GNUC_INTERNAL GType gst_drm_decryptor_get_type (void);

G_END_DECLS

#endif //__GST_DRM_DECRYPTOR_H__