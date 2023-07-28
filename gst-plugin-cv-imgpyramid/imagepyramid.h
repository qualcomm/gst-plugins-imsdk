/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CV_IMGPYRAMID_H__
#define __GST_CV_IMGPYRAMID_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdio.h>

#include "imgpyramid-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_CV_IMGPYRAMID   (gst_cv_imgpyramid_get_type())
#define GST_CV_IMGPYRAMID(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CV_IMGPYRAMID,GstCvImgPyramid))
#define GST_CV_IMGPYRAMID_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CV_IMGPYRAMID,GstCvImgPyramidClass))
#define GST_IS_CV_IMGPYRAMID(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CV_IMGPYRAMID))
#define GST_IS_CV_IMGPYRAMID_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CV_IMGPYRAMID))

#define GST_CV_IMGPYRAMID_GET_LOCK(obj) (&GST_CV_IMGPYRAMID(obj)->lock)
#define GST_CV_IMGPYRAMID_LOCK(obj) \
  g_mutex_lock(GST_CV_IMGPYRAMID_GET_LOCK(obj))
#define GST_CV_IMGPYRAMID_UNLOCK(obj) \
  g_mutex_unlock(GST_CV_IMGPYRAMID_GET_LOCK(obj))

typedef struct _GstCvImgPyramid GstCvImgPyramid;
typedef struct _GstCvImgPyramidClass GstCvImgPyramidClass;

struct _GstCvImgPyramid {
  GstElement             parent;

  /// Global mutex lock.
  GMutex                 lock;

  /// Convenient local reference to source pads.
  GHashTable             *srcpads;

  /// Convenient local reference to buffer pools.
  GHashTable             *bufferpools;

  /// Convenient local reference to sink pad.
  GstPad                 *sinkpad;

  /// Worker task.
  GstTask                *worktask;
  /// Worker task mutex.
  GRecMutex              worklock;

  // Number of output levels
  guint                  n_levels;

  /// Supported converters.
  GstImgPyramidEngine    *engine;

  /// Properties.
  guint                  n_octaves;
  guint                  n_scales;
  GArray                 *octave_sharpness;
};

struct _GstCvImgPyramidClass {
  GstElementClass parent;
};

G_GNUC_INTERNAL GType gst_cv_imgpyramid_get_type (void);

G_END_DECLS

#endif // __GST_CV_IMGPYRAMID_H__