/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_MSG_PUB_H__
#define __GST_MSG_PUB_H__

#include <stdio.h>

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include "msgadaptor/msg-adaptor.h"

G_BEGIN_DECLS

// Macros for defining types for this element.
#define GST_TYPE_MSG_PUB (gst_msg_pub_get_type())
#define GST_MSG_PUB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSG_PUB,GstMsgPub))
#define GST_MSG_PUB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSG_PUB,GstMsgPubClass))
#define GST_IS_MSG_PUB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSG_PUB))
#define GST_IS_MSG_PUB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSG_PUB))

typedef struct _GstMsgPub GstMsgPub;
typedef struct _GstMsgPubClass GstMsgPubClass;

struct _GstMsgPub {
  GstBaseSink         parent;

  /// Underlying protocol
  gchar               *protocol;

  /// IP to connect to
  gchar               *host;
  /// Port to connect to
  gint                port;

  /// Topic to publish
  gchar               *topic;

  /// Message passed in commandline
  gchar               *message_cmd;

  /// The path of config file to parse
  gchar               *config;

  /// Convert message in json format or not
  gboolean            json;

  /// Adaptor of underlying protocol
  GstMsgProtocol      *adaptor;
};

struct _GstMsgPubClass {
  GstBaseSinkClass parent_class;
};

// Function returning type information.
GType gst_msg_pub_get_type (void);

G_END_DECLS

#endif // __GST_MSG_PUB_H__