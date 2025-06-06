/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_C2_AENC_H_
#define _GST_C2_AENC_H_

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>
#include <gst/allocators/allocators.h>
#include <gst/pbutils/codec-utils.h>

#include "c2-engine/c2-engine.h"
#include "c2-engine/c2-engine-params.h"

G_BEGIN_DECLS

#define GST_TYPE_C2_AENC (gst_c2_aenc_get_type())
#define GST_C2_AENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_C2_AENC, GstC2AEncoder))
#define GST_C2_AENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_C2_AENC, GstC2AEncoderClass))
#define GST_IS_C2_AENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_C2_AENC))
#define GST_IS_C2_AENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_C2_AENC))
#define GST_C2_AENC_CAST(obj) ((GstC2AEncoder *)(obj))

typedef struct _GstC2AEncoder GstC2AEncoder;
typedef struct _GstC2AEncoderClass GstC2AEncoderClass;

struct _GstC2AEncoder {
  GstAudioEncoder parent;

  /// The name of the codec component
  gchar           *name;
  /// Codec engine handle
  GstC2Engine     *engine;
  /// SPS/PPS/VPS NALs headers.
  GList           *headers;
  /// Contains audio info data as bitrate and channels number
  GstAudioInfo    ainfo;
  /// Mutex for hash table.
  GMutex          framesmutex;
  /// Map contains input samles count for the specific queued index
  GHashTable      *framesmap;
  /// Frame number counter
  guint64         framenum;
  /// Audio bitrate
  guint           bitrate;

  /// Properties
};

struct _GstC2AEncoderClass {
  GstAudioEncoderClass parent;
};

G_GNUC_INTERNAL GType gst_c2_aenc_get_type (void);

G_END_DECLS

#endif // _GST_C2_AENC_H_
