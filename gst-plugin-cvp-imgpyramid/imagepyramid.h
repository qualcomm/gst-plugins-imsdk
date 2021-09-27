/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CVP_IMGPYRAMID_H__
#define __GST_CVP_IMGPYRAMID_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdio.h>

#include "cvp-imgpyramid-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_CVP_IMGPYRAMID   (gst_cvp_imgpyramid_get_type())
#define GST_CVP_IMGPYRAMID(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CVP_IMGPYRAMID,GstCvpImgPyramid))
#define GST_CVP_IMGPYRAMID_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CVP_IMGPYRAMID,GstCvpImgPyramidClass))
#define GST_IS_CVP_IMGPYRAMID(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CVP_IMGPYRAMID))
#define GST_IS_CVP_IMGPYRAMID_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CVP_IMGPYRAMID))

#define GST_CVP_IMGPYRAMID_GET_LOCK(obj) (&GST_CVP_IMGPYRAMID(obj)->lock)
#define GST_CVP_IMGPYRAMID_LOCK(obj) \
  g_mutex_lock(GST_CVP_IMGPYRAMID_GET_LOCK(obj))
#define GST_CVP_IMGPYRAMID_UNLOCK(obj) \
  g_mutex_unlock(GST_CVP_IMGPYRAMID_GET_LOCK(obj))

typedef struct _GstCvpImgPyramid GstCvpImgPyramid;
typedef struct _GstCvpImgPyramidClass GstCvpImgPyramidClass;

struct _GstCvpImgPyramid
{
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
  GstCvpImgPyramidEngine *engine;

  /// Properties.
  guint                  n_octaves;
  guint                  n_scales;
  GArray                 *octave_sharpness;

};

struct _GstCvpImgPyramidClass
{
  GstElementClass parent;
};

G_GNUC_INTERNAL GType gst_cvp_imgpyramid_get_type (void);

G_END_DECLS

#endif // __GST_CVP_IMGPYRAMID_H__