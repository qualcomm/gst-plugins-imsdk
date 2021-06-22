/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_VIDEOCROP_PAD_PROCESS_H__
#define __GST_VIDEOCROP_PAD_PROCESS_H__

#include <gst/gst.h>
#include "videocrop-video-pad.h"
#include <gst/video/gstimagepool.h>
#include <gst/video/c2d-video-converter.h>
#include <fastcv/fastcv.h>

#define OUTPUT_BUFFERS_COUNT 3

typedef enum {
  GST_VIDEO_CROP_TYPE_C2D,
  GST_VIDEO_CROP_TYPE_FASTCV,
} GstVideoCropType;

struct PreprocessingBuffers {
  GstBufferPool *nv12_pool;
  GstBufferPool *rgb_pool;
};

class VideoCropPadProcess {
 public:
  VideoCropPadProcess (GstPad * pad, gint index, GstVideoCropType crop_type) :
    pad_ (pad),
    index_ (index),
    crop_type_ (crop_type),
    do_scale_down_ (TRUE),
    do_color_convert_ (FALSE),
    proc_buffers_ {},
    c2dconvert_ (NULL),
    crop_ {},
    next_process_ (NULL) {};
  ~VideoCropPadProcess () {};

  gboolean Init (GstVideoInfo video_info);
  void Deinit ();
  void SetCrop (GstVideoRectangle * crop);
  gboolean Process (gboolean input_is_free, GstBuffer * in_buffer);
  void SetNextProcess (VideoCropPadProcess * cb) { next_process_ = cb; };
  gint GetIndex () { return index_; };
  GstPad * GetPad () { return pad_; };
  void SetSinkSegment (GstSegment sink_segment)
  {
    sink_segment_ = sink_segment;
  };

 private:
  gboolean AllocateBuffers (GstVideoFormat format);
  static void FreeQueueItem (GstDataQueueItem * item);
  gboolean PushBufferToQueue (GstVideoCropVideoPad * vpad, GstBuffer * buffer);
  GstClockTime GetRunningTime (GstPad * pad);

  GstPad *pad_;
  gint index_;
  PreprocessingBuffers proc_buffers_;
  GstC2dVideoConverter    *c2dconvert_;
  GstVideoInfo in_video_info_;
  GstVideoCropType crop_type_;
  GstVideoRectangle crop_;
  gboolean do_scale_down_;
  gboolean do_color_convert_;
  VideoCropPadProcess *next_process_;
  GstSegment sink_segment_;
};

#endif // __GST_VIDEOCROP_PAD_PROCESS_H__
