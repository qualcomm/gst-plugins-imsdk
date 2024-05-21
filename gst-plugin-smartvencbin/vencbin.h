/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_VIDEO_ENCODE_BIN_H__
#define __GST_QTI_VIDEO_ENCODE_BIN_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/pbutils/pbutils.h>
#include <gst/base/gstdataqueue.h>

#include "smart-codec-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_VENC_BIN (gst_venc_bin_get_type())
#define GST_VENC_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VENC_BIN,GstVideoEncBin))
#define GST_VENC_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VENC_BIN,GstVideoEncBinClass))
#define GST_IS_VENC_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VENC_BIN))
#define GST_IS_VENC_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VENC_BIN))
#define GST_VENC_BIN_CAST(obj)  ((GstVideoEncBin *)(obj))

#define GST_VENC_BIN_GET_LOCK(obj) (&GST_VENC_BIN(obj)->lock)
#define GST_VENC_BIN_LOCK(obj) g_mutex_lock(GST_VENC_BIN_GET_LOCK(obj))
#define GST_VENC_BIN_UNLOCK(obj) g_mutex_unlock(GST_VENC_BIN_GET_LOCK(obj))

typedef struct _GstVideoEncBin GstVideoEncBin;
typedef struct _GstVideoEncBinClass GstVideoEncBinClass;
typedef struct _GstCtrlFrameData GstCtrlFrameData;

typedef enum {
  GST_VENC_BIN_C2_ENC,
  GST_VENC_BIN_OMX_ENC,
} GstBinEncoderType;

struct _GstCtrlFrameData
{
  GstBuffer *buffer;
  GstVideoFrame vframe;
};

struct _GstVideoEncBin
{
  // Inherited parent structure.
  GstBin            bin;
  // Global mutex lock.
  GMutex            lock;
  // List of available encoders.
  GList             *encoders;
  // Instance of the chosen encoder.
  GstElement        *encoder;
  // Ghostpad which acts as proxy to the encoder sinkpad.
  GstPad            *sinkpad;
  // Ghostpad which acts as proxy to the encoder srcpad.
  GstPad            *srcpad;
  // Auxilary request pad which will be used for decision making.
  GstPad            *sinkctrlpad;
  // Auxilary request pad which will be used for decision making.
  GstPad            *sinkmlpad;
  // Engine which control the video parameters.
  SmartCodecEngine  *engine;
  // Properties.
  GstBinEncoderType encoder_type;
  guint             buff_cnt_delay;
  guint             target_bitrate;
  GArray            *framerate_thresholds;
  GArray            *bitrate_thresholds;
  GArray            *roi_qualitys;

  // Ctrl pad frames queue
  GstDataQueue      *ctrl_frames;
  // Ctrl stream video info
  GstVideoInfo      video_ctrl_info;

  // Main frames queue
  GstDataQueue      *main_frames;
  /// Worker task.
  GstTask           *worktask;
  /// Worker task mutex.
  GRecMutex         worklock;
  // Indicates whether the worker task is active or not.
  gboolean          active;
  /// Condition for push/pop buffers from the queues.
  GCond             wakeup;
};

struct _GstVideoEncBinClass
{
  // Inherited parent structure.
  GstBinClass parent_class;
};

GType gst_venc_bin_get_type(void);

G_END_DECLS

#endif /* __GST_QTI_VIDEO_ENCODE_BIN_H__ */
