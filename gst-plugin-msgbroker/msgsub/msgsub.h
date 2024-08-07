/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_MSG_SUB_H__
#define __GST_MSG_SUB_H__

#include <stdio.h>

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstdataqueue.h>

#include "msgadaptor/msg-adaptor.h"

G_BEGIN_DECLS

// Macros for defining types for this element.
#define GST_TYPE_MSG_SUB (gst_msg_sub_get_type())
#define GST_MSG_SUB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSG_SUB,GstMsgSub))
#define GST_MSG_SUB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSG_SUB,GstMsgSubClass))
#define GST_IS_MSG_SUB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSG_SUB))
#define GST_IS_MSG_SUB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSG_SUB))

typedef struct _GstMsgSub GstMsgSub;
typedef struct _GstMsgSubClass GstMsgSubClass;

struct _GstMsgSub {
  GstBaseSrc          parent;

  /// Data queue to save messages from callback
  GstDataQueue        *msg_queue;

  /// Underlying protocol
  gchar               *protocol;

  /// IP to connect to
  gchar               *host;
  /// Port to connect to
  gint                port;

  /// Topic to subscribe
  gchar               *topic;

  /// The path of config file to parse
  gchar               *config;

  /// Adaptor of underlying protocol
  GstMsgProtocol      *adaptor;
};

struct _GstMsgSubClass {
  GstBaseSrcClass parent_class;
};

// Function returning type information.
GType gst_msg_sub_get_type (void);

G_END_DECLS

#endif // __GST_MSG_SUB_H__