/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef _CUSTOMLIB_COMMON_H_
#define _CUSTOMLIB_COMMON_H_

#include "qtivideotemplate-defs.h"

#include <gst/video/video.h>

// For reference functionality
#include <gst/video/video-converter-engine.h>

typedef struct
{
  GstVideoInfo ininfo_;
  GstVideoInfo outinfo_;

  VideoTemplateCb cb_;
  void *priv_data;

  gboolean active;
  GMutex lock;
  GRecMutex worklock;
  GCond wakeup;
  GQueue *bufqueue;
  GstTask *worktask;

  // for reference functionality
  GstVideoConvBackend backend;
  GstVideoConvEngine *converter;
} CustomLib;

CustomLib *custom_lib_create_handle (VideoTemplateCb * callback,
    void *priv_data);

void custom_lib_set_cfg (CustomLib * customlib,
    GstVideoInfo * ininfo, GstVideoInfo * outinfo);

void custom_lib_query_possible_srcpad_cfgs (const VideoCfgRanges * sinkpad_cfgs,
    VideoCfgRanges * srcpad_cfgs);

void custom_lib_query_possible_sinkpad_cfgs (const VideoCfgRanges * srcpad_cfgs,
    VideoCfgRanges * sinkpad_cfgs);

void custom_lib_query_preferred_src_pad_cfg (CustomLib * custom_lib,
    VideoCfgRanges * sink_pad_possibiities,
    VideoCfgRanges * src_pad_possibilities, VideoCfg * src_pad_config);

CustomCmdStatus custom_lib_process_buffer_inplace (CustomLib * custom_lib,
    GstBuffer * inbuffer);

CustomCmdStatus custom_lib_process_buffer (CustomLib * custom_lib,
    GstBuffer * inbuffer, GstBuffer * outbuffer);

void custom_lib_delete_handle (CustomLib * custom_lib);

#endif
