/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CAMERA_REPROCESS_H__
#define __GST_CAMERA_REPROCESS_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "camera-reprocess-context.h"

G_BEGIN_DECLS

// Classic macro defination
#define GST_TYPE_CAMERA_REPROCESS (gst_camera_reprocess_get_type())
#define GST_CAMERA_REPROCESS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
  GST_TYPE_CAMERA_REPROCESS,GstCameraReprocess))
#define GST_CAMERA_REPROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),\
  GST_TYPE_CAMERA_REPROCESS,GstCameraReprocessClass))
#define GST_IS_CAMERA_REPROCESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERA_REPROCESS))
#define GST_IS_CAMERA_REPROCESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERA_REPROCESS))

typedef struct _GstCameraReprocess        GstCameraReprocess;
typedef struct _GstCameraReprocessClass   GstCameraReprocessClass;

struct _GstCameraReprocess {
  /// Inherited parent structure.
  GstBaseTransform          base;

  /// BufferPool for output buffer
  GstBufferPool             *pool;

  /// Context of OfflineCamera
  GstCameraReprocessContext *context;
};

struct _GstCameraReprocessClass {
  /// Inherited parent structure.
  GstBaseTransformClass parent;
};

GType gst_camera_reprocess_get_type (void);

G_END_DECLS

#endif // __GST_CAMERA_REPROCESS_H__
