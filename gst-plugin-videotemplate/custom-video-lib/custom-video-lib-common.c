/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "custom-video-lib-common.h"

#include <gst/utils/common-utils.h>
#include <sys/mman.h>

CustomLib *
custom_lib_create_handle (VideoTemplateCb * callback, void *priv_data)
{
  CustomLib *custom_lib = (CustomLib *) malloc (sizeof (CustomLib));
  if (NULL == custom_lib)
    return NULL;

  custom_lib->cb_ = *callback;
  custom_lib->priv_data = priv_data;

  gst_video_info_init (&custom_lib->ininfo_);
  gst_video_info_init (&custom_lib->outinfo_);

  // for reference functionality
  custom_lib->backend = GST_VCE_BACKEND_GLES;
  custom_lib->converter = NULL;

  return custom_lib;

}

void
custom_lib_set_cfg (CustomLib * customlib,
    GstVideoInfo * ininfo, GstVideoInfo * outinfo)
{
  if (NULL == customlib || NULL == ininfo || NULL == outinfo) {
    return;
  }

  customlib->ininfo_ = *ininfo;
  customlib->outinfo_ = *outinfo;

  // for reference functionality
  if (customlib->converter != NULL) {
    gst_video_converter_engine_free (customlib->converter);
  }

  customlib->converter =
      gst_video_converter_engine_new (customlib->backend, NULL);
}

void
custom_lib_query_possible_srcpad_cfgs (const VideoCfgRanges * sinkpad_cfgs,
    VideoCfgRanges * srcpad_cfgs)
{
  (void) sinkpad_cfgs;
  srcpad_cfgs->min_width = 1;
  srcpad_cfgs->max_width = G_MAXINT;
  srcpad_cfgs->min_height = 1;
  srcpad_cfgs->max_height = G_MAXINT;
  g_strlcpy (srcpad_cfgs->formats, "NV12,YUY2", sizeof (srcpad_cfgs->formats));
}

void
custom_lib_query_possible_sinkpad_cfgs (const VideoCfgRanges * srcpad_cfgs,
    VideoCfgRanges * sinkpad_cfgs)
{
  (void) srcpad_cfgs;
  sinkpad_cfgs->min_width = 1;
  sinkpad_cfgs->max_width = G_MAXINT;
  sinkpad_cfgs->min_height = 1;
  sinkpad_cfgs->max_height = G_MAXINT;
  g_strlcpy (sinkpad_cfgs->formats, "NV12,YUY2",
      sizeof (sinkpad_cfgs->formats));
}

void
custom_lib_query_preferred_src_pad_cfg (CustomLib * custom_lib,
    VideoCfgRanges * sink_pad_possibiities,
    VideoCfgRanges * src_pad_possibilities, VideoCfg * src_pad_config)
{
  if (NULL == src_pad_possibilities || NULL == src_pad_config) {
    GST_ERROR ("null arg");
    return;
  }

  memset (src_pad_config, 0, sizeof (VideoCfg));

  if (src_pad_possibilities->min_width == src_pad_possibilities->max_width) {
    src_pad_config->selected_width = src_pad_possibilities->min_width;
  } else {
    if (sink_pad_possibiities->min_width == sink_pad_possibiities->max_width) {
      src_pad_config->selected_width = sink_pad_possibiities->min_width;
    } else {
      GST_ERROR ("TODO: select width");
    }
  }

  if (src_pad_possibilities->min_height == src_pad_possibilities->max_height) {
    src_pad_config->selected_height = src_pad_possibilities->min_height;
  } else {
    if (sink_pad_possibiities->min_height == sink_pad_possibiities->max_height) {
      src_pad_config->selected_height = sink_pad_possibiities->min_height;
    } else {
      GST_ERROR ("TODO: select height");
    }
  }

  g_strlcpy (src_pad_config->selected_format,
      src_pad_possibilities->formats, sizeof (src_pad_config->selected_format));
}

CustomCmdStatus
custom_lib_process_buffer_inplace (CustomLib * custom_lib, GstBuffer * inbuffer)
{
  GstVideoFrame inframe = { 0, };
  GstMemory *memory = NULL;
  void *bufVaddr = NULL;
  guint fd = 0;

  if (!gst_video_frame_map (&inframe, &custom_lib->ininfo_, inbuffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR ("Failed to map input buffer!");
    return CUSTOM_STATUS_FAIL;
  }

  memory = gst_buffer_peek_memory (inframe.buffer, 0);
  fd = gst_fd_memory_get_fd (memory);

  bufVaddr = mmap (NULL, gst_buffer_get_size (inbuffer), PROT_READ | PROT_WRITE,
      MAP_SHARED, fd, 0);

  if (!bufVaddr) {
    GST_ERROR ("mmap failed!");
    gst_video_frame_unmap (&inframe);
    return CUSTOM_STATUS_FAIL;
  }
  // start custom handling

  // end custom handling

  munmap (bufVaddr, gst_buffer_get_size (inbuffer));
  bufVaddr = NULL;

  gst_video_frame_unmap (&inframe);
  return CUSTOM_STATUS_OK;
}

CustomCmdStatus
custom_lib_process_buffer (CustomLib * custom_lib,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstVideoFrame inframe = { 0, }, outframe = { 0, };
  gboolean success = FALSE;

  if (NULL == custom_lib) {
    GST_ERROR ("NULL lib");
    return CUSTOM_STATUS_FAIL;
  }

  if (!gst_video_frame_map (&inframe, &custom_lib->ininfo_, inbuffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR ("Failed to map input buffer!");
    return CUSTOM_STATUS_FAIL;
  }

  (*custom_lib->cb_.lock_buf_for_writing) (outbuffer);

  if (!gst_video_frame_map (&outframe, &custom_lib->outinfo_, outbuffer,
      GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR ("Failed to map output buffer!");
    gst_video_frame_unmap (&inframe);
    return CUSTOM_STATUS_FAIL;
  }
  // reference functionality: flip the image
  {
    GstVideoBlit blit = GST_VCE_BLIT_INIT;
    GstVideoComposition composition = GST_VCE_COMPOSITION_INIT;
    GstClockTime time = GST_CLOCK_TIME_NONE;
    GstVideoRectangle in_rect, out_rect;

    in_rect.x = 0;
    in_rect.y = 0;
    in_rect.w = GST_VIDEO_INFO_WIDTH (&custom_lib->ininfo_);
    in_rect.h = GST_VIDEO_INFO_HEIGHT (&custom_lib->ininfo_);

    out_rect.x = 0;
    out_rect.y = 0;
    out_rect.w = GST_VIDEO_INFO_WIDTH (&custom_lib->outinfo_);
    out_rect.h = GST_VIDEO_INFO_HEIGHT (&custom_lib->outinfo_);

    time = gst_util_get_timestamp ();

    blit.frame = &inframe;

    blit.source = in_rect;
    blit.destination = out_rect;

    blit.flip = GST_VCE_FLIP_VERTICAL;
    blit.rotate = 0;

    composition.blits = &blit;
    composition.n_blits = 1;

    composition.frame = &outframe;
    composition.flags = 0;

    composition.bgcolor = 0;
    composition.bgfill = FALSE;

    success = gst_video_converter_engine_compose (custom_lib->converter,
        &composition, 1, NULL);

    time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

    GST_LOG ("Conversion took %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
        (GST_TIME_AS_USECONDS (time) % 1000));
  }

  gst_video_frame_unmap (&outframe);
  gst_video_frame_unmap (&inframe);

  (*custom_lib->cb_.unlock_buf_for_writing) (outbuffer);

  if (!success) {
    GST_ERROR ("Failed to process composition!");
    return CUSTOM_STATUS_FAIL;
  }

  return CUSTOM_STATUS_OK;
}

void
custom_lib_delete_handle (CustomLib * custom_lib)
{
  if (custom_lib) {
    if (custom_lib->converter != NULL)
      gst_video_converter_engine_free (custom_lib->converter);

    free (custom_lib);
  }
}
