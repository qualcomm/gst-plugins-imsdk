/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CAMERA_IMAGE_REPROCESS_H__
#define __GST_CAMERA_IMAGE_REPROCESS_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "camera-image-reprocess-pad.h"
#include "camera-image-reprocess-context.h"

G_BEGIN_DECLS

#define GST_TYPE_CAMERA_IMAGE_REPROC \
  (gst_camera_image_reproc_get_type())

#define GST_CAMERA_IMAGE_REPROC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  GST_TYPE_CAMERA_IMAGE_REPROC,GstCameraImageReproc))

#define GST_CAMERA_IMAGE_REPROC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  GST_TYPE_CAMERA_IMAGE_REPROC,GstCameraImageReprocClass))

#define GST_IS_CAMERA_IMAGE_REPROC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERA_IMAGE_REPROC))

#define GST_IS_CAMERA_IMAGE_REPROC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERA_IMAGE_REPROC))

#define GST_CAMERA_IMAGE_REPROC_CAST(obj) \
  ((GstCameraImageReproc *)(obj))

#define GST_CAMERA_IMAGE_REPROC_GET_LOCK(obj) \
  (&GST_CAMERA_IMAGE_REPROC(obj)->lock)

#define GST_CAMERA_IMAGE_REPROC_LOCK(obj) \
  g_mutex_lock(GST_CAMERA_IMAGE_REPROC_GET_LOCK(obj))

#define GST_CAMERA_IMAGE_REPROC_UNLOCK(obj) \
  g_mutex_unlock(GST_CAMERA_IMAGE_REPROC_GET_LOCK(obj))

typedef struct _GstCameraImageReproc        GstCameraImageReproc;
typedef struct _GstCameraImageReprocClass   GstCameraImageReprocClass;

struct _GstCameraImageReproc
{
  /// Inherited parent structure.
  GstElement                      parent;

  /// Global mutex lock.
  GMutex                          lock;

  /// Next available index for the sink pads.
  guint                           nextidx;

  /// Convenient local reference to dynamic sink pads.
  GList                           *dynsinkpads;
  /// Convenient local reference to source pad.
  GstCameraReprocSrcPad           *srcpad;

  /// Worker task.
  GstTask                         *worktask;
  /// Worker task mutex.
  GRecMutex                       worklock;
  // Indicates whether the worker task is active or not.
  gboolean                        active;
  /// Output buffer pool.
  GstBufferPool                   *outpool;

  /// Context of OfflineCamera
  GstCameraImageReprocContext     *context;

  /// Properties.
  guint                           queue_size;
};

struct _GstCameraImageReprocClass {
  /// Inherited parent structure.
  GstElementClass                 parent;
};

GType gst_camera_image_reproc_get_type (void);

G_END_DECLS

#endif // __GST_QTI_CAMERA_IMAGE_REPROCESS_H__

