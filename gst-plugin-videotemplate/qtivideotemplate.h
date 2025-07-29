/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QTI_VIDEO_TEMPLATE_H__
#define __GST_QTI_VIDEO_TEMPLATE_H__

#include "include/qtivideotemplate-defs.h"

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_VIDEO_TEMPLATE \
  (gst_video_template_get_type())
#define GST_VIDEO_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_TEMPLATE,GstVideoTemplate))
#define GST_VIDEO_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_TEMPLATE,GstVideoTemplateClass))
#define GST_IS_VIDEO_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_TEMPLATE))
#define GST_IS_VIDEO_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_TEMPLATE))
#define GST_VIDEO_TEMPLATE_CAST(obj)       ((GstVideoTemplate *)(obj))
typedef struct _GstVideoTemplate GstVideoTemplate;
typedef struct _GstVideoTemplateClass GstVideoTemplateClass;

#define MAX_CUSTOM_LIBNAME_SIZE 512
#define MAX_CUSTOM_PARAMS_SIZE 512

struct _GstVideoTemplate
{
  GstBaseTransform parent;

  // Output buffer pool
  GstBufferPool *outpool;

  void *custom_lib;
  BufferAllocMode buffer_alloc_mode;

  char customlib_name[MAX_CUSTOM_LIBNAME_SIZE];
  char custom_params[MAX_CUSTOM_PARAMS_SIZE];

  void *custom_lib_handle;

  // Create handle for custom library
  void *(*customlib_create_handle) (VideoTemplateCb *, void *);

  // Initialize custom library with custom params
  void (*customlib_set_custom_params) (void *, char *);

  // Query supported srcpad configuration based on supported sinkpad
  // configuration
  void (*customlib_query_possible_srcpad_cfgs) (const VideoCfgRanges *
      sinkpad_cfgs, VideoCfgRanges * srcpad_cfgs);

  // Query supported sinkpad configuration based on supported srcpad
  // configuration
  void (*customlib_query_possible_sinkpad_cfgs) (const VideoCfgRanges *
      srcpad_cfgs, VideoCfgRanges * sinkpad_cfgs);

  // Select src_pad configuration from possible srcpad configurations
  void (*customlib_select_src_pad_cfg) (void *custom_lib,
      VideoCfgRanges * sink_pad_possibiities,
      VideoCfgRanges * src_pad_possibilities, VideoCfg * src_pad_config);

  // Set the framework selected configurations on custom library
  void (*customlib_set_cfg) (void *custom_lib, GstVideoInfo * ininfo,
      GstVideoInfo * outinfo);

  // Query buffer allocation mode from custom library
  void (*customlib_query_buffer_alloc_mode) (void *, BufferAllocMode *);

  // Custom library must define this if BUFFER_ALLOC_MODE_INPLACE selected
    CustomCmdStatus (*customlib_process_buffer_inplace) (void *custom_lib,
      GstBuffer * inbuffer);

  // Custom library must define this if BUFFER_ALLOC_MODE_ALLOC selected
    CustomCmdStatus (*customlib_process_buffer) (void *, GstBuffer *,
      GstBuffer *);

  // Custom library must define this if BUFFER_ALLOC_MODE_CUSTOM selected
    CustomCmdStatus (*customlib_process_buffer_custom) (void *custom_lib,
      GstBuffer * inbuffer);

  // Delete the handle associated with custom library
  void (*customlib_delete_handle) (void *);
};

struct _GstVideoTemplateClass
{
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_video_template_get_type (void);

G_END_DECLS
#endif // __GST_QTI_VIDEO_TEMPLATE_H__
