/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_META_MUX_PADS_H__
#define __GST_META_MUX_PADS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_META_MUX_DATA_PAD (gst_meta_mux_data_pad_get_type())
#define GST_META_MUX_DATA_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_META_MUX_DATA_PAD,\
      GstMetaMuxDataPad))
#define GST_META_MUX_DATA_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_META_MUX_DATA_PAD,\
      GstMetaMuxDataPadClass))
#define GST_IS_META_MUX_DATA_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_META_MUX_DATA_PAD))
#define GST_IS_META_MUX_DATA_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_META_MUX_DATA_PAD))
#define GST_META_MUX_DATA_PAD_CAST(obj) ((GstMetaMuxDataPad *)(obj))

typedef struct _GstMetaMuxDataPad GstMetaMuxDataPad;
typedef struct _GstMetaMuxDataPadClass GstMetaMuxDataPadClass;

typedef enum {
  GST_DATA_TYPE_UNKNOWN,
  GST_DATA_TYPE_TEXT,
  GST_DATA_TYPE_OPTICAL_FLOW,
} GstDataType;

struct _GstMetaMuxDataPad {
  /// Inherited parent structure.
  GstPad      parent;

  // Format of negotiated metadata.
  GstDataType type;
  /// Segment.
  GstSegment  segment;

  /// Variable for temporarily storing partial data(meta).
  gpointer    stash;
  /// Queue for managing incoming data(meta) buffers.
  GQueue      *queue;
};

struct _GstMetaMuxDataPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};


GType gst_meta_mux_data_pad_get_type (void);

G_END_DECLS

#endif // __GST_META_MUX_PADS_H__
