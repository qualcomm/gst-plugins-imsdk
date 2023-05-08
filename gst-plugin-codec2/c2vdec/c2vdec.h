/*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef _GST_C2_VDEC_H_
#define _GST_C2_VDEC_H_

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/allocators/allocators.h>

#include "c2-engine/c2-engine.h"
#include "c2-engine/c2-engine-params.h"

G_BEGIN_DECLS

#define GST_TYPE_C2_VDEC (gst_c2_vdec_get_type())
#define GST_C2_VDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_C2_VDEC, GstC2VDecoder))
#define GST_C2_VDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_C2_VDEC, GstC2VDecoderClass))
#define GST_IS_C2_VDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_C2_VDEC))
#define GST_IS_C2_VDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_C2_VDEC))
#define GST_C2_VDEC_CAST(obj) ((GstC2VDecoder *)(obj))

typedef struct _GstC2VDecoder GstC2VDecoder;
typedef struct _GstC2VDecoderClass GstC2VDecoderClass;

struct _GstC2VDecoder {
  GstVideoDecoder    parent;

  gchar              *name;
  GstC2Engine        *engine;

  /// Negotiated output resolution, format, etc.
  GstVideoCodecState *outstate;
  /// TRUE if the negotiated output format is UBWC.
  gboolean           isubwc;

  /// Properties
  gboolean           secure;
};

struct _GstC2VDecoderClass {
  GstVideoDecoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_vdec_get_type (void);

G_END_DECLS

#endif
