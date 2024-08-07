/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef _MSG_ADAPTOR_H_
#define _MSG_ADAPTOR_H_

#include <gst/gst.h>

#include "msg-adaptor-api.h"

G_BEGIN_DECLS

typedef struct _GstMsgProtocol GstMsgProtocol;

/**
 * gst_msg_protocol_new
 * @protocol: message distribution protocol.
 * @role: publisher, subscriber or other role of client.
 *
 * Construct GstMsgProtocol and initiate protocol instance.
 *
 * Return: pointer point to ProtocolFunc prototype.
 */
GstMsgProtocol *
gst_msg_protocol_new (gchar *protocol, const gchar *role);

/**
 * gst_msg_protocol_free
 * @adaptor: the structure of message distribution protocol adaptor.
 *
 * Free GstMsgProtocol and protocol instance.
 *
 * Return: NULL.
 */
void
gst_msg_protocol_free (GstMsgProtocol *adaptor);

/**
 * gst_msg_protocol_config:
 * @adaptor: the structure of message distribution protocol adaptor.
 * @path: the absolute path of config file.
 *
 * Config properties for message distribution server.
 *
 * Return: TRUE if config successfully.
 */
gboolean
gst_msg_protocol_config (GstMsgProtocol *adaptor, gchar *path);

/**
 * gst_msg_protocol_connect:
 * @adaptor: the structure of message distribution protocol adaptor.
 * @host: the IP of server to connect to.
 * @port: the port of server to connect to.
 *
 * Connect via message protocol.
 *
 * Return: TRUE if connect successfully.
 */
gboolean
gst_msg_protocol_connect (GstMsgProtocol *adaptor, gchar *host, gint port);

/**
 * gst_msg_protocol_disconnect:
 * @adaptor: the structure of message distribution protocol adaptor.
 *
 * Disconnect via message distribution protocol.
 *
 * Return: TRUE if disconnect successfully.
 */
gboolean
gst_msg_protocol_disconnect (GstMsgProtocol *adaptor);

/**
 * gst_msg_protocol_publish:
 * @adaptor: the structure of message distribution protocol adaptor.
 * @topic: the topic related to the message.
 * @message: the message to send.
 *
 * Publish message on topic via message distribution protocol.
 *
 * Return: TRUE if publish is done (don't wait for ACK).
 */
gboolean
gst_msg_protocol_publish (GstMsgProtocol *adaptor, gchar *topic,
                          gpointer message);

/**
 * gst_msg_protocol_subscribe:
 * @adaptor: the structure of message distribution protocol adaptor.
 * @topic: the topic to subscribe.
 * @queue: the data queue from sub along with the callback to save message.
 * @callback: callback to convert message to gstbuffer and send to upper caller.
 *
 * Subscribe topics via message distribution protocol.
 *
 * Return: TRUE if subscribe is done (don't wait for ACK).
 */
gboolean
gst_msg_protocol_subscribe (GstMsgProtocol *adaptor, gchar *topic,
                            gpointer queue, GstSubscribeCallback callback);

G_END_DECLS

#endif // _MSG_ADAPTOR_H_