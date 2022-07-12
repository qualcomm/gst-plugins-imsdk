/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_QTI_VIDEO_COMPOSER_H__
#define __GST_QTI_VIDEO_COMPOSER_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>

#ifdef USE_C2D_CONVERTER
#include <gst/video/c2d-video-converter.h>
#endif // USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
#include <gst/video/gles-video-converter.h>
#endif // USE_GLES_CONVERTER

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_COMPOSER \
  (gst_video_composer_get_type())
#define GST_VIDEO_COMPOSER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_COMPOSER,GstVideoComposer))
#define GST_VIDEO_COMPOSER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_COMPOSER,GstVideoComposerClass))
#define GST_IS_VIDEO_COMPOSER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_COMPOSER))
#define GST_IS_VIDEO_COMPOSER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_COMPOSER))
#define GST_VIDEO_COMPOSER_CAST(obj)       ((GstVideoComposer *)(obj))

#define GST_VIDEO_COMPOSER_GET_LOCK(obj) (&GST_VIDEO_COMPOSER(obj)->lock)
#define GST_VIDEO_COMPOSER_LOCK(obj) \
  g_mutex_lock(GST_VIDEO_COMPOSER_GET_LOCK(obj))
#define GST_VIDEO_COMPOSER_UNLOCK(obj) \
  g_mutex_unlock(GST_VIDEO_COMPOSER_GET_LOCK(obj))

typedef struct _GstVideoComposer GstVideoComposer;
typedef struct _GstVideoComposerClass GstVideoComposerClass;

struct _GstVideoComposer {
  GstAggregator        parent;

  /// Global mutex lock.
  GMutex               lock;

  /// Number of sink pads.
  guint                n_inputs;
  /// Number of source pads.
  guint                n_outputs;

  /// Output pad caps.
  GstVideoInfo         *outinfo;
  /// Output buffer pool.
  GstBufferPool        *outpool;

  /// Output buffer duration.
  GstClockTime         duration;

  /// Worker task.
  GstTask              *worktask;
  /// Worker task mutex.
  GRecMutex            worklock;
  /// Worker queue.
  GstDataQueue         *requests;

  /// Supported converters.
#ifdef USE_C2D_CONVERTER
  GstC2dVideoConverter *c2dconvert;
#endif // USE_C2D_CONVERTER
#ifdef USE_GLES_CONVERTER
  GstGlesVideoConverter *glesconvert;
#endif // USE_GLES_CONVERTER

  /// Properties.
  guint                background;
};

struct _GstVideoComposerClass {
  GstAggregatorClass parent;
};

G_GNUC_INTERNAL GType gst_video_composer_get_type (void);

G_END_DECLS

#endif // __GST_QTI_VIDEO_COMPOSER_H__
