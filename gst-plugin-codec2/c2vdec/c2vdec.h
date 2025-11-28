/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
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

  /// Properties
  gboolean           secure;
};

struct _GstC2VDecoderClass {
  GstVideoDecoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_vdec_get_type (void);

G_END_DECLS

#endif
