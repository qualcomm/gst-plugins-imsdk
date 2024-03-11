/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <gst/gst.h>

#include "msgadaptor/msg-adaptor-api.h"

typedef struct _GstMqtt GstMqtt;

/**
 * gst_mqtt_new:
 * @role: publisher, subscriber or other role of client.
 *
 * Allocate instance for mqtt protocol.
 *
 * Return: a pointer point to GstMqtt structure.
 */
static gpointer
gst_mqtt_new (const gchar *role);

/**
 * gst_mqtt_free:
 * @prop: the properties of message distribution protocol.
 *
 * Free instance for mqtt protocol.
 *
 * Return: NULL.
 */
static void
gst_mqtt_free (gpointer *prop);

/**
 * gst_mqtt_config:
 * @prop: the properties of message distribution protocol.
 * @path: the absolute path of config file.
 *
 * Config properties for mqtt server (broker).
 *
 * Return: TRUE if config is successful.
 */
static gboolean
gst_mqtt_config (gpointer *prop, gchar *path);

/**
 * gst_mqtt_connect:
 * @prop: the properties of message distribution protocol.
 * @host: the IP of server to connect to.
 * @port: the port of server to connect to.
 *
 * Connect to mqtt server(broker).
 *
 * Return: TRUE if connect is successful (don't wait for ACK).
 */
static gboolean
gst_mqtt_connect (gpointer *prop, gchar *host, gint port);

/**
 * gst_mqtt_disconnect:
 * @prop: the properties of message distribution protocol.
 *
 * Disconnect with mqtt server(broker).
 *
 * Return: TRUE if disconnect is done.
 */
static gboolean
gst_mqtt_disconnect (gpointer *prop);

/**
 * gst_mqtt_publish:
 * @prop: the properties of message distribution protocol.
 * @topic: the topic to subscribe.
 * @message: the message to publish.
 *
 * Publish message on topic.
 *
 * Return: TRUE if publish is done.
 */
static gboolean
gst_mqtt_publish (gpointer *prop, gchar *topic, gpointer message);

/**
 * gst_mqtt_subscribe:
 * @prop: the properties of message distribution protocol.
 * @topic: the topic to subscribe.
 * @callback: callback to pass message to adaptor.
 * @adaptor: the pointer of message distribution protocol adaptor.
 *
 * Subscribe topic.
 *
 * Return: TRUE if subscribe is done.
 */
static gboolean
gst_mqtt_subscribe (gpointer *prop, gchar *topic,
                    GstAdaptorSubscribeCallback callback,
                    gpointer adaptor);