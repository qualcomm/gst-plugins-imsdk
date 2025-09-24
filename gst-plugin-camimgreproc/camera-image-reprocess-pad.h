/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CAMERA_IMAGE_REPROCESS_PAD_H__
#define __GST_CAMERA_IMAGE_REPROCESS_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include "camera-image-reprocess-context.h"

G_BEGIN_DECLS

#define CAMERA_IMAGE_REPROC_COMMON_VIDEO_CAPS \
    "width = (int) [ 1, 32767 ], "       \
    "height = (int) [ 1, 32767 ], "      \
    "framerate = (fraction) [ 0, 255 ]"

#define CAMERA_IMAGE_REPROC_VIDEO_JPEG_CAPS \
    "image/jpeg, "              \
    CAMERA_IMAGE_REPROC_COMMON_VIDEO_CAPS

#define CAMERA_IMAGE_REPROC_VIDEO_RAW_CAPS(formats) \
    "video/x-raw, "                     \
    "format = (string) " formats ", "   \
    CAMERA_IMAGE_REPROC_COMMON_VIDEO_CAPS

#define CAMERA_IMAGE_REPROC_VIDEO_BAYER_CAPS(formats, bpps) \
    "video/x-bayer, "                           \
    "format = (string) " formats ", "           \
    "bpp = (string) " bpps ", "                 \
    CAMERA_IMAGE_REPROC_COMMON_VIDEO_CAPS

#define GST_TYPE_CAMERA_REPROC_SINK_PAD \
  (gst_camera_reproc_sink_pad_get_type())

#define GST_CAMERA_REPROC_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERA_REPROC_SINK_PAD,\
      GstCameraReprocSinkPad))

#define GST_CAMERA_REPROC_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERA_REPROC_SINK_PAD,\
      GstCameraReprocSinkPadClass))

#define GST_IS_CAMERA_REPROC_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERA_REPROC_SINK_PAD))

#define GST_IS_CAMERA_REPROC_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERA_REPROC_SINK_PAD))

#define GST_CAMERA_REPROC_SINK_PAD_CAST(obj) \
  ((GstCameraReprocSinkPad *)(obj))

#define GST_TYPE_CAMERA_REPROC_SRC_PAD \
  (gst_camera_reproc_src_pad_get_type())

#define GST_CAMERA_REPROC_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERA_REPROC_SRC_PAD,\
      GstCameraReprocSrcPad))

#define GST_CAMERA_REPROC_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERA_REPROC_SRC_PAD,\
      GstCameraReprocSrcPadClass))

#define GST_IS_CAMERA_REPROC_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERA_REPROC_SRC_PAD))

#define GST_IS_CAMERA_REPROC_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERA_REPROC_SRC_PAD))

#define GST_CAMERA_REPROC_SRC_PAD_CAST(obj) \
  ((GstCameraReprocSrcPad *)(obj))

#define GST_CAMERA_REPROC_SRC_GET_LOCK(obj) \
  (&GST_CAMERA_REPROC_SRC_PAD(obj)->lock)

#define GST_CAMERA_REPROC_SRC_LOCK(obj) \
  g_mutex_lock(GST_CAMERA_REPROC_SRC_GET_LOCK(obj))

#define GST_CAMERA_REPROC_SRC_UNLOCK(obj) \
  g_mutex_unlock(GST_CAMERA_REPROC_SRC_GET_LOCK(obj))

#define GST_CAMERA_REPROC_SINK_GET_LOCK(obj) \
  (&GST_CAMERA_REPROC_SINK_PAD(obj)->lock)

#define GST_CAMERA_REPROC_SINK_LOCK(obj) \
  g_mutex_lock(GST_CAMERA_REPROC_SINK_GET_LOCK(obj))

#define GST_CAMERA_REPROC_SINK_UNLOCK(obj) \
  g_mutex_unlock(GST_CAMERA_REPROC_SINK_GET_LOCK(obj))

#define GST_CAMERA_IMAGE_REPROC_PAD_SIGNAL_IDLE(pad, idle) \
{\
  g_mutex_lock (&(pad->lock));                                          \
                                                                        \
  if (pad->is_idle != idle) {                                           \
    pad->is_idle = idle;                                                \
    GST_TRACE_OBJECT (pad, "State %s", idle ? "Idle" : "Running");      \
    g_cond_signal (&(pad->drained));                                    \
  }                                                                     \
                                                                        \
  g_mutex_unlock (&(pad->lock));                                        \
}

#define GST_CAMERA_IMAGE_REPROC_PAD_WAIT_IDLE(pad) \
{\
  g_mutex_lock (&(pad->lock));                                          \
  GST_TRACE_OBJECT (pad, "Waiting until idle");                         \
                                                                        \
  while (!pad->is_idle) {                                               \
    gint64 endtime = g_get_monotonic_time () + 1 * G_TIME_SPAN_SECOND;  \
                                                                        \
    if (!g_cond_wait_until (&(pad->drained), &(pad->lock), endtime))    \
      GST_WARNING_OBJECT (pad, "Timeout while waiting for idle!");      \
  }                                                                     \
                                                                        \
  GST_TRACE_OBJECT (pad, "Received idle");                              \
  g_mutex_unlock (&(pad->lock));                                        \
}

typedef struct _GstCameraReprocSinkPad GstCameraReprocSinkPad;
typedef struct _GstCameraReprocSinkPadClass GstCameraReprocSinkPadClass;

typedef struct _GstCameraReprocSrcPad GstCameraReprocSrcPad;
typedef struct _GstCameraReprocSrcPadClass GstCameraReprocSrcPadClass;

struct _GstCameraReprocSinkPad {
  /// Inherited parent structure.
  GstPad                        parent;

  /// Global mutex lock.
  GMutex                        lock;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond                         drained;
  /// Flag indicating that there is no more work for processing.
  gboolean                      is_idle;

  /// Queue for managing incoming video buffers.
  GstDataQueue                  *buffers;

  /// Segment.
  GstSegment                    segment;

  /// The count of buffers the queue can hold.
  guint                         buffers_limit;

  /// Properties.
  /// Camera id to process
  guint                         camera_id;

  /// Request metadata path
  gchar                         *req_meta_path;
  /// Request metadata step
  guint                         req_meta_step;

  /// Electronic Image Stabilization
  GstCameraImageReprocEis       eis;
};

struct _GstCameraReprocSinkPadClass {
  /// Inherited parent structure.
  GstPadClass                   parent;
};

struct _GstCameraReprocSrcPad {
  /// Inherited parent structure.
  GstPad                        parent;

  /// Global mutex lock.
  GMutex                        lock;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond                         drained;
  /// Flag indicating that there is no more work for processing.
  gboolean                      is_idle;

  /// Segment.
  GstSegment                    segment;

  /// Queue for output buffers.
  GstDataQueue                  *buffers;

  /// The count of buffers the queue can hold.
  guint                         buffers_limit;
};

struct _GstCameraReprocSrcPadClass {
  /// Inherited parent structure.
  GstPadClass                   parent;
};

GType gst_camera_reproc_sink_pad_get_type (void);
GType gst_camera_reproc_src_pad_get_type (void);

gboolean gst_camera_reproc_src_pad_query (GstPad * pad, GstObject * parent,
                                          GstQuery * query);

gboolean gst_camera_reproc_src_pad_activate_mode (GstPad * pad,
                                                  GstObject * parent,
                                                  GstPadMode mode,
                                                  gboolean active);

G_END_DECLS

#endif // __GST_CAMERA_IMAGE_REPROCESS_PAD_H__
