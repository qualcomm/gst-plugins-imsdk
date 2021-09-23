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

#ifndef __GST_BATCH_H__
#define __GST_BATCH_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gstpad.h>
#include <gst/base/gstaggregator.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_BATCH (gst_batch_get_type())
#define GST_BATCH(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_BATCH, GstBatch))
#define GST_BATCH_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_BATCH, GstBatchClass))
#define GST_IS_BATCH(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_BATCH))
#define GST_IS_BATCH_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_BATCH))

#define BATCH_CAPS " {BGRA, ARGB, RGBA, ABGR, YUY2, UYVY, YVYU, I420, YV12, NV12, NV12_UBWC, NV21, RGB, BGR, xRGB, xBGR, RGBx, BGRx } "

typedef struct _GstBatch GstBatch;
typedef struct _GstBatchClass GstBatchClass;

struct _GstBatch
{
  GstAggregator parent;

  gboolean silent;
};

struct _GstBatchClass {
  GstAggregatorClass parent_class;
};

GType gst_batch_get_type (void);

G_END_DECLS

#endif