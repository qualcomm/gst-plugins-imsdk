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

#ifndef __GST_CVP_OPTCLFLOW_H__
#define __GST_CVP_OPTCLFLOW_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/allocators/allocators.h>
#include <ml-meta/ml_meta.h>

#include "opticalflow.h"

G_BEGIN_DECLS

#define GST_TYPE_CVP_OPTCLFLOW \
  (gst_cvp_optclflow_get_type())
#define GST_CVP_OPTCLFLOW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CVP_OPTCLFLOW,GstCVPOPTCLFLOW))
#define GST_CVP_OPTCLFLOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CVP_OPTCLFLOW,GstCVPOPTCLFLOWClass))
#define GST_IS_CVP_OPTCLFLOW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CVP_OPTCLFLOW))
#define GST_IS_CVP_OPTCLFLOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CVP_OPTCLFLOW))
#define GST_CVP_OPTCLFLOW_CAST(obj)       ((GstCVPOPTCLFLOW *)(obj))

typedef struct _GstCVPOPTCLFLOW      GstCVPOPTCLFLOW;
typedef struct _GstCVPOPTCLFLOWClass GstCVPOPTCLFLOWClass;

struct _GstCVPOPTCLFLOW {
  GstVideoFilter      parent;

    cvp::CVPInputParams source_info;
    cvp::OFEngine* engine;
    gboolean is_init;
    guint property_mask;

    gchar *output_location;
    gboolean stats_enable;
    guint fps;

    GstVideoInfo            *ininfo;
    GstVideoInfo            *outinfo;

    // Output buffer pool
    GstBufferPool           *outpool;
};

struct _GstCVPOPTCLFLOWClass {
  GstVideoFilterClass parent;
};

G_GNUC_INTERNAL GType gst_cvp_optclflow_get_type(void);

G_END_DECLS

#endif // __GST_CVP_OPTCLFLOW_H__
