/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_OFFLINE_CAMERA_H__
#define __GST_OFFLINE_CAMERA_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "offline-camera-context.h"

G_BEGIN_DECLS

// Classic macro defination
#define GST_TYPE_OFFLINE_CAMERA (gst_offline_camera_get_type())
#define GST_OFFLINE_CAMERA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OFFLINE_CAMERA,GstOfflineCamera))
#define GST_OFFLINE_CAMERA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OFFLINE_CAMERA,GstOfflineCameraClass))
#define GST_IS_OFFLINE_CAMERA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OFFLINE_CAMERA))
#define GST_IS_OFFLINE_CAMERA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OFFLINE_CAMERA))

typedef struct _GstOfflineCamera        GstOfflineCamera;
typedef struct _GstOfflineCameraClass   GstOfflineCameraClass;

struct _GstOfflineCamera {
  /// Inherited parent structure.
  GstBaseTransform        base;

  /// BufferPool for output buffer
  GstBufferPool           *pool;

  /// Context of OfflineCamera
  GstOfflineCameraContext *context;
};

struct _GstOfflineCameraClass {
  /// Inherited parent structure.
  GstBaseTransformClass parent;
};

GType gst_offline_camera_get_type (void);

G_END_DECLS

#endif // __GST_OFFLINE_CAMERA_H__
