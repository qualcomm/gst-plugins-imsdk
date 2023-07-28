/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_CV_IMGPYRAMID_PADS_H__
#define __GST_QTI_CV_IMGPYRAMID_PADS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_CV_IMGPYRAMID_SINKPAD (gst_cv_imgpyramid_sinkpad_get_type())
#define GST_CV_IMGPYRAMID_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CV_IMGPYRAMID_SINKPAD,\
      GstCvImgPyramidSinkPad))
#define GST_CV_IMGPYRAMID_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CV_IMGPYRAMID_SINKPAD,\
      GstCvImgPyramidSinkPadClass))
#define GST_IS_CV_IMGPYRAMID_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CV_IMGPYRAMID_SINKPAD))
#define GST_IS_CV_IMGPYRAMID_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CV_IMGPYRAMID_SINKPAD))
#define GST_CV_IMGPYRAMID_SINKPAD_CAST(obj) ((GstCvImgPyramidSinkPad *)(obj))

#define GST_TYPE_CV_IMGPYRAMID_SRCPAD (gst_cv_imgpyramid_srcpad_get_type())
#define GST_CV_IMGPYRAMID_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CV_IMGPYRAMID_SRCPAD,\
      GstCvImgPyramidSrcPad))
#define GST_CV_IMGPYRAMID_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CV_IMGPYRAMID_SRCPAD,\
      GstCvImgPyramidSrcPadClass))
#define GST_IS_CV_IMGPYRAMID_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CV_IMGPYRAMID_SRCPAD))
#define GST_IS_CV_IMGPYRAMID_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CV_IMGPYRAMID_SRCPAD))
#define GST_CV_IMGPYRAMID_SRCPAD_CAST(obj) ((GstCvImgPyramidSrcPad *)(obj))

typedef struct _GstCvImgPyramidSinkPad GstCvImgPyramidSinkPad;
typedef struct _GstCvImgPyramidSinkPadClass GstCvImgPyramidSinkPadClass;
typedef struct _GstCvImgPyramidSrcPad GstCvImgPyramidSrcPad;
typedef struct _GstCvImgPyramidSrcPadClass GstCvImgPyramidSrcPadClass;

struct _GstCvImgPyramidSinkPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  /// Video info from caps.
  GstVideoInfo *info;

  /// Buffer requests.
  GstDataQueue *requests;
};

struct _GstCvImgPyramidSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstCvImgPyramidSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  gboolean     is_ubwc;

  /// Worker queue.
  GstDataQueue *buffers;
};

struct _GstCvImgPyramidSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_cv_imgpyramid_srcpad_get_type (void);

GType gst_cv_imgpyramid_sinkpad_get_type (void);

// Source pad methods
gboolean
gst_cv_imgpyramid_srcpad_query (GstPad * pad, GstObject * parent,
                               GstQuery * query);

gboolean
gst_cv_imgpyramid_srcpad_event (GstPad * pad, GstObject * parent,
                               GstEvent * event);

gboolean
gst_cv_imgpyramid_srcpad_activate_mode (GstPad * pad, GstObject * parent,
                                       GstPadMode mode, gboolean active);

gboolean
gst_cv_imgpyramid_srcpad_setcaps (GstCvImgPyramidSrcPad * srcpad,
                                  gboolean is_ubwc);

gboolean
gst_cv_imgpyramid_srcpad_push_event (GstElement * element, GstPad * pad,
                                    gpointer userdata);

G_END_DECLS

#endif // __GST_QTI_CV_IMGPYRAMID_PADS_H__
