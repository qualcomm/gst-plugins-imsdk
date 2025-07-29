/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "custom-video-lib-async.h"
#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

#include <stdint.h>

static void
gst_venc_bin_worker_task (gpointer user_data)
{
  CustomLib *custom_lib = (CustomLib *) user_data;
  GstBuffer *outbuffer = NULL;
  CustomCmdStatus status = CUSTOM_STATUS_FAIL;

  g_mutex_lock (&custom_lib->lock);

  {
    GstBuffer *inbuf = g_queue_pop_head (custom_lib->bufqueue);
    if (inbuf) {
      // reference check for inplace versus alloc. for same video fmt
      // dimensions check may be sufficient
      if (custom_lib->ininfo_.width == custom_lib->outinfo_.width &&
          custom_lib->ininfo_.height == custom_lib->outinfo_.height) {
        outbuffer = inbuf;
        custom_lib_process_buffer_inplace (custom_lib, inbuf);
        status =
            (*custom_lib->cb_.buffer_done) (outbuffer, custom_lib->priv_data);

      } else {
        (*custom_lib->cb_.allocate_outbuffer) (&outbuffer,
            custom_lib->priv_data);
        if (NULL == outbuffer) {
          GST_ERROR ("failed to allocate outbuffer for async");
          gst_buffer_unref(inbuf);
          g_mutex_unlock (&custom_lib->lock);
          return;
        }

        gst_buffer_copy_into (outbuffer, inbuf,
            GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

        custom_lib_process_buffer (custom_lib, inbuf, outbuffer);
        status =
            (*custom_lib->cb_.buffer_done) (outbuffer, custom_lib->priv_data);

        gst_buffer_unref (inbuf);
      }

      if (CUSTOM_STATUS_OK != status) {
        GST_ERROR ("buffer_done failed");
      }
    }
  }

  if (custom_lib->active && CUSTOM_STATUS_OK == status) {
    g_cond_wait (&custom_lib->wakeup, &custom_lib->lock);
  }

  g_mutex_unlock (&custom_lib->lock);
}

CustomLib *
custom_create_handle (VideoTemplateCb * callback, void *priv_data)
{
  CustomLib *custom_lib = custom_lib_create_handle (callback, priv_data);

  g_mutex_init (&custom_lib->lock);
  custom_lib->bufqueue = g_queue_new ();

  custom_lib->active = FALSE;
  g_cond_init (&custom_lib->wakeup);
  g_rec_mutex_init (&custom_lib->worklock);
  custom_lib->worktask =
      gst_task_new (gst_venc_bin_worker_task, custom_lib, NULL);
  gst_task_set_lock (custom_lib->worktask, &custom_lib->worklock);

  custom_lib->active = TRUE;
  gst_task_start (custom_lib->worktask);
  GST_DEBUG ("Started worktask %p", custom_lib->worktask);

  return custom_lib;
}

void
custom_set_custom_params (CustomLib * customlib, char *custom_params)
{

  if (NULL == customlib) {
    return;
  }
  // TODO: custom param initialization
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
  // determine based on sink and src pad buffer requirements limitation
  // and custom_lib preference

  if (NULL == usage) {
    return;
  }

  *usage = BUFFER_ALLOC_MODE_CUSTOM;
}

CustomCmdStatus
custom_process_buffer_custom (CustomLib * custom_lib, GstBuffer * inbuffer)
{

  GstBuffer *inbuf = gst_buffer_ref (inbuffer);
  g_queue_push_tail (custom_lib->bufqueue, inbuf);
  g_cond_signal (&custom_lib->wakeup);

  return CUSTOM_STATUS_OK;
}

void
custom_delete_handle (CustomLib * custom_lib)
{
  if (custom_lib) {
    GST_DEBUG ("stop work task");
    gst_task_stop (custom_lib->worktask);
    custom_lib->active = FALSE;
    g_cond_signal (&custom_lib->wakeup);

    GST_DEBUG ("work task join");
    gst_task_join (custom_lib->worktask);
    GST_DEBUG ("work task join completed");

    g_rec_mutex_clear (&custom_lib->worklock);
    g_cond_clear (&custom_lib->wakeup);

    g_queue_free_full (custom_lib->bufqueue, (GDestroyNotify) gst_buffer_unref);
    g_mutex_clear (&custom_lib->lock);

    custom_lib_delete_handle (custom_lib);
  }
}
