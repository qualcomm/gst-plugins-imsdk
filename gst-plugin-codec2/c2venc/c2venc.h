/*
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

#ifndef __GST_QTI_C2_VENC_ENC_H__
#define __GST_QTI_C2_VENC_ENC_H__

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/allocators/allocators.h>
#include "c2-engine/c2-wrapper.h"

G_BEGIN_DECLS

#define GST_TYPE_C2_VENC_ENC \
  (gst_c2_venc_get_type())
#define GST_C2_VENC_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_C2_VENC_ENC,GstC2_VENCEncoder))
#define GST_C2_VENC_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_C2_VENC_ENC,GstC2_VENCEncoderClass))
#define GST_IS_C2_VENC_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_C2_VENC_ENC))
#define GST_IS_C2_VENC_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_C2_VENC_ENC))
#define GST_C2_VENC_ENC_CAST(obj)       ((GstC2_VENCEncoder *)(obj))

typedef struct _GstC2_VENCEncoder GstC2_VENCEncoder;
typedef struct _GstC2_VENCEncoderClass GstC2_VENCEncoderClass;
typedef struct _GstC2_ROIenc GstC2_ROIenc;

// Maximum number of input frame queued
#define MAX_QUEUED_FRAME 32

struct _GstC2_ROIenc {
  guint top;
  guint left;
  guint bottom;
  guint right;
};

struct _GstC2_VENCEncoder {
  GstVideoEncoderClass parent;

  GstBufferPool *pool;

  gchar *comp_name;
  gboolean input_setup;
  gboolean output_setup;
  gint width;
  gint height;
  GstVideoFormat input_format;
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  GstC2Wrapper *wrapper;

  guint64 queued_frame[MAX_QUEUED_FRAME];
  guint64 frame_index;
  GMutex pending_lock;
  GCond pending_cond;
  gint rate_numerator;

  gboolean eos_reached;

  rc_mode_t rcMode;
  color_matrix_t matrix;
  ir_mode_t intra_refresh_mode;
  guint32 intra_refresh_mbs;
  guint32 target_bitrate;
  float framerate;
  slice_mode_t slice_mode;
  guint32 slice_size;
  gboolean iframe_only;
  guint32 idr_interval;

  guint32 max_qp_b_frames;
  guint32 max_qp_i_frames;
  guint32 max_qp_p_frames;
  guint32 min_qp_b_frames;
  guint32 min_qp_i_frames;
  guint32 min_qp_p_frames;
  GstC2_ROIenc roi_encoding;
  gint roi_encoding_qp_delta;

  entropy_mode_t entropy_mode;
  loop_filter_mode_t loop_filter_mode;
  guint32 quant_i_frames;
  guint32 quant_p_frames;
  guint32 quant_b_frames;
  guint32 num_ltr_frames;
  rotate_t rotate;
  gboolean is_ubwc;
};

struct _GstC2_VENCEncoderClass {
  GstVideoEncoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_venc_get_type (void);

G_END_DECLS

#endif // __GST_QTI_C2_VENC_ENC_H__
