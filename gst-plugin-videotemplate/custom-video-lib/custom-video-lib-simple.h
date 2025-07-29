/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __CUSTOM_VIDEO_LIB_SIMPLE_H_
#define __CUSTOM_VIDEO_LIB_SIMPLE_H_

#include "qtivideotemplate-defs.h"
#include "custom-video-lib-common.h"

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

// For reference functionality
#include <gst/video/video-converter-engine.h>

CustomLib *custom_create_handle (VideoTemplateCb * cb, void *priv_data);

void custom_set_custom_params (CustomLib * customlib, char *custom_params);

void custom_query_possible_srcpad_cfgs (const VideoCfgRanges * sinkpad_cfgs,
    VideoCfgRanges * srcpad_cfgs);

void custom_query_possible_sinkpad_cfgs (const VideoCfgRanges * srcpad_cfgs,
    VideoCfgRanges * sinkpad_cfgs);

void custom_query_preferred_src_pad_cfg (CustomLib * custom_lib,
    VideoCfgRanges * sink_pad_possibiities,
    VideoCfgRanges * src_pad_possibilities, VideoCfg * src_pad_config);

void custom_set_cfg (CustomLib * customlib,
    GstVideoInfo * ininfo, GstVideoInfo * outinfo);

void custom_query_buffer_alloc_mode (CustomLib * customlib,
    BufferAllocMode * usage);

// For BUFFER_ALLOC_MODE_INPLACE
CustomCmdStatus custom_process_buffer_inplace (CustomLib * custom_lib,
    GstBuffer * inbuffer);

// For BUFFER_ALLOC_MODE_ALLOC
CustomCmdStatus custom_process_buffer (CustomLib * custom_lib,
    GstBuffer * inbuffer, GstBuffer * outbuffer);

void custom_delete_handle (CustomLib * custom_lib);

#endif
