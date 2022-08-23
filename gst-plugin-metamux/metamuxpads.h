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

#ifndef __GST_METAMUX_PADS_H__
#define __GST_METAMUX_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_METAMUX_DATA_PAD (gst_metamux_data_pad_get_type())
#define GST_METAMUX_DATA_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_METAMUX_DATA_PAD,\
      GstMetaMuxDataPad))
#define GST_METAMUX_DATA_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_METAMUX_DATA_PAD,\
      GstMetaMuxDataPadClass))
#define GST_IS_METAMUX_DATA_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_METAMUX_DATA_PAD))
#define GST_IS_METAMUX_DATA_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_METAMUX_DATA_PAD))
#define GST_METAMUX_DATA_PAD_CAST(obj) ((GstMetaMuxDataPad *)(obj))

#define GST_TYPE_METAMUX_SINK_PAD (gst_metamux_sink_pad_get_type())
#define GST_METAMUX_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_METAMUX_SINK_PAD,\
      GstMetaMuxSinkPad))
#define GST_METAMUX_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_METAMUX_SINK_PAD,\
      GstMetaMuxSinkPadClass))
#define GST_IS_METAMUX_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_METAMUX_SINK_PAD))
#define GST_IS_METAMUX_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_METAMUX_SINK_PAD))
#define GST_METAMUX_SINK_PAD_CAST(obj) ((GstMetaMuxSinkPad *)(obj))

#define GST_TYPE_METAMUX_SRC_PAD (gst_metamux_src_pad_get_type())
#define GST_METAMUX_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_METAMUX_SRC_PAD,\
      GstMetaMuxSrcPad))
#define GST_METAMUX_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_METAMUX_SRC_PAD,\
      GstMetaMuxSrcPadClass))
#define GST_IS_METAMUX_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_METAMUX_SRC_PAD))
#define GST_IS_METAMUX_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_METAMUX_SRC_PAD))
#define GST_METAMUX_SRC_PAD_CAST(obj) ((GstMetaMuxSrcPad *)(obj))

#define GST_METAMUX_SRC_GET_LOCK(obj) (&GST_METAMUX_SRC_PAD(obj)->lock)
#define GST_METAMUX_SRC_LOCK(obj) \
    g_mutex_lock(GST_METAMUX_SRC_GET_LOCK(obj))
#define GST_METAMUX_SRC_UNLOCK(obj) \
    g_mutex_unlock(GST_METAMUX_SRC_GET_LOCK(obj))

typedef struct _GstMetaEntry GstMetaEntry;

typedef struct _GstMetaMuxDataPad GstMetaMuxDataPad;
typedef struct _GstMetaMuxDataPadClass GstMetaMuxDataPadClass;

typedef struct _GstMetaMuxSinkPad GstMetaMuxSinkPad;
typedef struct _GstMetaMuxSinkPadClass GstMetaMuxSinkPadClass;

typedef struct _GstMetaMuxSrcPad GstMetaMuxSrcPad;
typedef struct _GstMetaMuxSrcPadClass GstMetaMuxSrcPadClass;

typedef enum {
  GST_DATA_TYPE_UNKNOWN,
  GST_DATA_TYPE_TEXT,
  GST_DATA_TYPE_OPTICAL_FLOW,
} GstDataType;

struct _GstMetaEntry {
  /// Parsed metadata in GST_TYPE_LIST format containing GST_TYPE_STRUCTURE.
  GValue       value;
  /// The timestamp corresponding to the metadata entry.
  GstClockTime timestamp;
};

struct _GstMetaMuxDataPad {
  /// Inherited parent structure.
  GstPad      parent;

  // Format of negotiated metadata.
  GstDataType type;
  /// Segment.
  GstSegment  segment;

  /// Variable for temporarily storing partial data(meta).
  gchar       *stash;
  /// Queue for managing parsed #GstMetaEntry data.
  GQueue      *queue;
};

struct _GstMetaMuxDataPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstMetaMuxSinkPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Queue for managing incoming video/audio buffers.
  GstDataQueue *buffers;
};

struct _GstMetaMuxSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstMetaMuxSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;

  /// Segment.
  GstSegment   segment;

  /// Queue for output buffers.
  GstDataQueue *buffers;
};

struct _GstMetaMuxSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};


GType gst_metamux_data_pad_get_type (void);

GType gst_metamux_sink_pad_get_type (void);

GType gst_metamux_src_pad_get_type (void);

gboolean gst_metamux_src_pad_event (GstPad * pad, GstObject * parent,
                                    GstEvent * event);
gboolean gst_metamux_src_pad_query (GstPad * pad, GstObject * parent,
                                    GstQuery * query);
gboolean gst_metamux_src_pad_activate_mode (GstPad * pad, GstObject * parent,
                                            GstPadMode mode, gboolean active);

G_END_DECLS

#endif // __GST_METAMUX_PADS_H__
