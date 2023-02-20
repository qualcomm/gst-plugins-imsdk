/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GST_QTI_VIDEO_SPLIT_PADS_H__
#define __GST_QTI_VIDEO_SPLIT_PADS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_SPLIT_SINKPAD (gst_video_split_sinkpad_get_type())
#define GST_VIDEO_SPLIT_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_SPLIT_SINKPAD,\
      GstVideoSplitSinkPad))
#define GST_VIDEO_SPLIT_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_SPLIT_SINKPAD,\
      GstVideoSplitSinkPadClass))
#define GST_IS_VIDEO_SPLIT_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_SPLIT_SINKPAD))
#define GST_IS_VIDEO_SPLIT_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_SPLIT_SINKPAD))
#define GST_VIDEO_SPLIT_SINKPAD_CAST(obj) ((GstVideoSplitSinkPad *)(obj))

#define GST_TYPE_VIDEO_SPLIT_SRCPAD (gst_video_split_srcpad_get_type())
#define GST_VIDEO_SPLIT_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_SPLIT_SRCPAD,\
      GstVideoSplitSrcPad))
#define GST_VIDEO_SPLIT_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_SPLIT_SRCPAD,\
      GstVideoSplitSrcPadClass))
#define GST_IS_VIDEO_SPLIT_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_SPLIT_SRCPAD))
#define GST_IS_VIDEO_SPLIT_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_SPLIT_SRCPAD))
#define GST_VIDEO_SPLIT_SRCPAD_CAST(obj) ((GstVideoSplitSrcPad *)(obj))

typedef struct _GstVideoSplitSinkPad GstVideoSplitSinkPad;
typedef struct _GstVideoSplitSinkPadClass GstVideoSplitSinkPadClass;
typedef struct _GstVideoSplitSrcPad GstVideoSplitSrcPad;
typedef struct _GstVideoSplitSrcPadClass GstVideoSplitSrcPadClass;

typedef enum {
  GST_VSPLIT_MODE_NONE,
  GST_VSPLIT_MODE_FORCE_TRANSFORM,
  GST_VSPLIT_MODE_ROI_SINGLE,
  GST_VSPLIT_MODE_ROI_BATCH,
} GstVideoSplitMode;

struct _GstVideoSplitSinkPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  /// Video info from caps.
  GstVideoInfo *info;
  /// Whether input buffers have Universal Bandwidth Compression.
  gboolean     isubwc;

  /// Buffer requests.
  GstDataQueue *requests;
};

struct _GstVideoSplitSinkPadClass {
  /// Inherited parent structure.
  GstPadClass  parent;
};

struct _GstVideoSplitSrcPad {
  /// Inherited parent structure.
  GstPad            parent;

  /// Segment.
  GstSegment        segment;

  /// Video info from caps.
  GstVideoInfo      *info;
  /// Whether output buffers have Universal Bandwidth Compression.
  gboolean          isubwc;
  /// Passthrough when mode is 'none' and sink to source caps match.
  gboolean          passthrough;

  /// Buffer pool.
  GstBufferPool     *pool;
  /// Worker queue.
  GstDataQueue      *buffers;

  /// Properties.
  GstVideoSplitMode mode;
};

struct _GstVideoSplitSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_video_split_sinkpad_get_type (void);

GType gst_video_split_srcpad_get_type (void);

gboolean
gst_video_split_srcpad_setcaps (GstVideoSplitSrcPad * srcpad, GstCaps * incaps);

GstBufferPool *
gst_video_split_create_pool (GstPad * pad, GstCaps * caps);

G_END_DECLS

#endif // __GST_QTI_VIDEO_SPLIT_PADS_H__
