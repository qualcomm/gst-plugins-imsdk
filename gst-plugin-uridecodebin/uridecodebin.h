/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_URI_DECODEBIN_H__
#define __GST_QTI_URI_DECODEBIN_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include <gst/video/gstvideodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_QTI_URI_DECODEBIN \
  (gst_uri_decodebin_get_type())
#define GST_QTI_URI_DECODEBIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
  GST_TYPE_QTI_URI_DECODEBIN,GstQtiURIDecodeBin))
#define GST_QTI_URI_DECODEBIN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),\
  GST_TYPE_QTI_URI_DECODEBIN,GstQtiURIDecodeBinClass))
#define GST_IS_QTI_URI_DECODEBIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTI_URI_DECODEBIN))
#define GST_IS_QTI_URI_DECODEBIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTI_URI_DECODEBIN))
#define GST_QTI_URI_DECODEBIN_CAST(obj) ((GstQtiURIDecodeBin *) (obj))

#define GST_QTI_URI_DECODEBIN_LOCK(dec) \
  (g_mutex_lock(&((GstQtiURIDecodeBin*)(dec))->lock))
#define GST_QTI_URI_DECODEBIN_UNLOCK(dec) \
  (g_mutex_unlock(&((GstQtiURIDecodeBin*)(dec))->lock))

typedef struct _GstQtiURIDecodeBin GstQtiURIDecodeBin;
typedef struct _GstQtiURIDecodeBinClass GstQtiURIDecodeBinClass;

struct _GstQtiURIDecodeBin
{
  GstBin        parent;

  /// Global mutex lock.
  GMutex        lock;

  /// Internal element (uridecodebin3)
  GstElement    *uridecodebin;

  /// Loop Implementation Variables.
  guint         cur_iter;
  guint         n_decoders;
  guint         n_eos;

  /// Timestamps Implementation
  gint64        duration;

  /// States of the Element.
  gboolean      loop;

  /// Properties.
  gchar         *uri;
  guint         iterations;
};

struct _GstQtiURIDecodeBinClass
{
  GstBinClass parent_class;
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

G_GNUC_INTERNAL GType gst_uri_decodebin_get_type (void);

G_END_DECLS

#endif // __GST_QTI_URI_DECODEBIN_H__
