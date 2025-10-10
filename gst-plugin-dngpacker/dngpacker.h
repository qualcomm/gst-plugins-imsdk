/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_DNGPACKER_H__
#define __GST_QTI_DNGPACKER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>
#include <packer-utils.h>

G_BEGIN_DECLS

#define GST_TYPE_DNGPACKER (gst_dngpacker_get_type())

#define GST_DNGPACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DNGPACKER,GstDngPacker))

#define GST_DNGPACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DNGPACKER,GstDngPackerClass))

#define GST_IS_DNGPACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DNGPACKER))

#define GST_IS_DNGPACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DNGPACKER))

#define GST_DNGPACKER_CAST(obj)       ((GstDngPacker *)(obj))

#define GST_DNGPACKER_GET_LOCK(obj)   (&GST_DNGPACKER(obj)->lock)
#define GST_DNGPACKER_LOCK(obj)       g_mutex_lock(GST_DNGPACKER_GET_LOCK(obj))
#define GST_DNGPACKER_UNLOCK(obj)     g_mutex_unlock(GST_DNGPACKER_GET_LOCK(obj))

/// GST Bayer format from qtiqmmfsrc plugin
#define GST_BAYER_FORMAT_OFFSET 0x1000

typedef struct _GstDngPacker GstDngPacker;
typedef struct _GstDngPackerClass GstDngPackerClass;
typedef struct _GstRawImageSettings GstRawImageSettings;

typedef enum {
  GST_BAYER_FORMAT_BGGR = GST_BAYER_FORMAT_OFFSET,
  GST_BAYER_FORMAT_RGGB,
  GST_BAYER_FORMAT_GBRG,
  GST_BAYER_FORMAT_GRBG,
  GST_BAYER_FORMAT_MONO,
} GstBayerFormat;

struct _GstRawImageSettings {
  /// CFA pattern
  DngPackerCFAPattern   cfa;

  /// bits per pixel
  gint                  bpp;

  /// raw image width
  gint                  width;

  /// raw image height
  gint                  height;

  /// raw image stride in bytes
  gint                  stride;
};

struct _GstDngPacker
{
  /// Inherited parent structure.
  GstElement        parent;

  /// Global mutex lock.
  GMutex            lock;

  /// Pads
  GstPad            *raw_sink_pad;
  GstPad            *img_sink_pad;
  GstPad            *dng_src_pad;

  /// Packing task.
  GstTask           *task;

  /// Packing task mutex.
  GRecMutex         task_lock;

  // Indicates whether the worker task is active or not.
  gboolean          task_active;

  // Raw Image Pad properties
  GstRawImageSettings   raw_img_settings;
  //
  /// Buffer numbers waiting for process
  guint             process_buf_num;

  /// Condition for processing buf num free
  GCond             cond_buf_idle;

  /// Queue for managing incoming raw image buffers
  GstDataQueue      *raw_buf_queue;

  /// Queue for managing incoming jpeg image buffers
  GstDataQueue      *image_buf_queue;

  /// Dngpacker handle
  DngPackerUtils    *packer_utils;
};

struct _GstDngPackerClass {
  /// Inherited parent structure.
  GstElementClass   parent;
};

GType gst_dngpacker_get_type (void);

G_END_DECLS

#endif // __GST_QTI_DNGPACKER_H__
