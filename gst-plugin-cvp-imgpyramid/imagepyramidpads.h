/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_CVP_IMGPYRAMID_PADS_H__
#define __GST_QTI_CVP_IMGPYRAMID_PADS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_CVP_IMGPYRAMID_SINKPAD (gst_cvp_imgpyramid_sinkpad_get_type())
#define GST_CVP_IMGPYRAMID_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CVP_IMGPYRAMID_SINKPAD,\
      GstCvpImgPyramidSinkPad))
#define GST_CVP_IMGPYRAMID_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CVP_IMGPYRAMID_SINKPAD,\
      GstCvpImgPyramidSinkPadClass))
#define GST_IS_CVP_IMGPYRAMID_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CVP_IMGPYRAMID_SINKPAD))
#define GST_IS_CVP_IMGPYRAMID_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CVP_IMGPYRAMID_SINKPAD))
#define GST_CVP_IMGPYRAMID_SINKPAD_CAST(obj) ((GstCvpImgPyramidSinkPad *)(obj))

#define GST_TYPE_CVP_IMGPYRAMID_SRCPAD (gst_cvp_imgpyramid_srcpad_get_type())
#define GST_CVP_IMGPYRAMID_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CVP_IMGPYRAMID_SRCPAD,\
      GstCvpImgPyramidSrcPad))
#define GST_CVP_IMGPYRAMID_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CVP_IMGPYRAMID_SRCPAD,\
      GstCvpImgPyramidSrcPadClass))
#define GST_IS_CVP_IMGPYRAMID_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CVP_IMGPYRAMID_SRCPAD))
#define GST_IS_CVP_IMGPYRAMID_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CVP_IMGPYRAMID_SRCPAD))
#define GST_CVP_IMGPYRAMID_SRCPAD_CAST(obj) ((GstCvpImgPyramidSrcPad *)(obj))

typedef struct _GstCvpImgPyramidSinkPad GstCvpImgPyramidSinkPad;
typedef struct _GstCvpImgPyramidSinkPadClass GstCvpImgPyramidSinkPadClass;
typedef struct _GstCvpImgPyramidSrcPad GstCvpImgPyramidSrcPad;
typedef struct _GstCvpImgPyramidSrcPadClass GstCvpImgPyramidSrcPadClass;

struct _GstCvpImgPyramidSinkPad
{
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  /// Video info from caps.
  GstVideoInfo *info;

  /// Buffer requests.
  GstDataQueue *requests;
};

struct _GstCvpImgPyramidSinkPadClass
{
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstCvpImgPyramidSrcPad
{
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  /// Worker queue.
  GstDataQueue *buffers;
};

struct _GstCvpImgPyramidSrcPadClass
{
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_cvp_imgpyramid_srcpad_get_type (void);

GType gst_cvp_imgpyramid_sinkpad_get_type (void);

// Source pad methods
gboolean
gst_cvp_imgpyramid_srcpad_query (GstPad * pad, GstObject * parent,
                               GstQuery * query);

gboolean
gst_cvp_imgpyramid_srcpad_event (GstPad * pad, GstObject * parent,
                               GstEvent * event);

gboolean
gst_cvp_imgpyramid_srcpad_activate_mode (GstPad * pad, GstObject * parent,
                                       GstPadMode mode, gboolean active);

gboolean
gst_cvp_imgpyramid_srcpad_setcaps (GstCvpImgPyramidSrcPad * srcpad);

gboolean
gst_cvp_imgpyramid_srcpad_push_event (GstElement * element, GstPad * pad,
                                    gpointer userdata);

G_END_DECLS

#endif // __GST_QTI_CVP_IMGPYRAMID_PADS_H__
