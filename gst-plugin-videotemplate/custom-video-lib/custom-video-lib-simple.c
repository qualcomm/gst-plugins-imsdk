/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "custom-video-lib-simple.h"

#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

#include <stdint.h>

CustomLib *
custom_create_handle (VideoTemplateCb * callback, void *priv_data)
{
  return custom_lib_create_handle (callback, priv_data);
}

void
custom_set_custom_params (CustomLib * customlib, char *custom_params)
{
  if (NULL == customlib)
    return;

  // TODO: Customize functionality
}

void
custom_set_cfg (CustomLib * customlib,
    GstVideoInfo * ininfo, GstVideoInfo * outinfo)
{
  custom_lib_set_cfg (customlib, ininfo, outinfo);
}

void
custom_query_possible_srcpad_cfgs (const VideoCfgRanges * sinkpad_cfgs,
    VideoCfgRanges * srcpad_cfgs)
{
  custom_lib_query_possible_srcpad_cfgs (sinkpad_cfgs, srcpad_cfgs);
}

void
custom_query_possible_sinkpad_cfgs (const VideoCfgRanges * srcpad_cfgs,
    VideoCfgRanges * sinkpad_cfgs)
{
  custom_lib_query_possible_sinkpad_cfgs (srcpad_cfgs, sinkpad_cfgs);
}

void
custom_query_preferred_src_pad_cfg (CustomLib * custom_lib,
    VideoCfgRanges * sink_pad_possibiities,
    VideoCfgRanges * src_pad_possibilities, VideoCfg * src_pad_config)
{

  custom_lib_query_preferred_src_pad_cfg (custom_lib, sink_pad_possibiities,
      src_pad_possibilities, src_pad_config);
}

void
custom_query_buffer_alloc_mode (CustomLib * customlib, BufferAllocMode * usage)
{
  // query the preferred buffer allocation mode
  // for reference implemenation, assuming format is the same, selects in-place if
  // dimensions are same, and buffer allocation mode if not.
  if (NULL == usage) {
    return;
  }

  if (customlib->ininfo_.width == customlib->outinfo_.width &&
      customlib->ininfo_.height == customlib->outinfo_.height) {

    // TODO: additional checks
    *usage = BUFFER_ALLOC_MODE_INPLACE;
    return;
  }

  *usage = BUFFER_ALLOC_MODE_ALLOC;
}

CustomCmdStatus
custom_process_buffer_inplace (CustomLib * custom_lib, GstBuffer * inbuffer)
{
  return custom_lib_process_buffer_inplace (custom_lib, inbuffer);
}

CustomCmdStatus
custom_process_buffer (CustomLib * custom_lib,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{

  return custom_lib_process_buffer (custom_lib, inbuffer, outbuffer);
}

void
custom_delete_handle (CustomLib * custom_lib)
{
  custom_lib_delete_handle (custom_lib);
}
