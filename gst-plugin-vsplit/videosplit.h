/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_VIDEO_SPLIT_H__
#define __GST_QTI_VIDEO_SPLIT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-converter-engine.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_SPLIT (gst_video_split_get_type())
#define GST_VIDEO_SPLIT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_SPLIT,GstVideoSplit))
#define GST_VIDEO_SPLIT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_SPLIT,GstVideoSplitClass))
#define GST_IS_VIDEO_SPLIT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_SPLIT))
#define GST_IS_VIDEO_SPLIT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_SPLIT))

#define GST_VIDEO_SPLIT_GET_LOCK(obj) (&GST_VIDEO_SPLIT(obj)->lock)
#define GST_VIDEO_SPLIT_LOCK(obj) \
  g_mutex_lock(GST_VIDEO_SPLIT_GET_LOCK(obj))
#define GST_VIDEO_SPLIT_UNLOCK(obj) \
  g_mutex_unlock(GST_VIDEO_SPLIT_GET_LOCK(obj))

typedef struct _GstVideoSplit GstVideoSplit;
typedef struct _GstVideoSplitClass GstVideoSplitClass;

struct _GstVideoSplit
{
  /// Inherited parent structure.
  GstElement           parent;

  /// Global mutex lock.
  GMutex               lock;

  /// Next available index for the source pads.
  guint                nextidx;

  /// Convenient local reference to sink pad.
  GstPad               *sinkpad;
  /// Convenient local reference to source pads.
  GList                *srcpads;

  /// Worker task.
  GstTask              *worktask;
  /// Worker task mutex.
  GRecMutex            worklock;

  /// Video converter engine.
  GstVideoConvEngine   *converter;

  /// Properties.
  GstVideoConvBackend  backend;
};

struct _GstVideoSplitClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_video_split_get_type (void);

G_END_DECLS

#endif // __GST_QTI_VIDEO_SPLIT_H__
