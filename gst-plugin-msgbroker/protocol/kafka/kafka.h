/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/gst.h>

#include "msgadaptor/msg-adaptor-api.h"

typedef struct _GstKafka GstKafka;

#define SECTION_GLOBAL   "global-config"
#define SECTION_PRODUCER "producer-config"
#define SECTION_CONSUMER "consumer-config"

/**
 * gst_kafka_new:
 * @role: publisher, subscriber or other role of client.
 *
 * Allocate instance for kafka protocol.
 *
 * Return: a pointer point to GstKafka structure.
 */
static gpointer
gst_kafka_new (const gchar * role);

/**
 * gst_kafka_free:
 * @prop: the properties of message distribution protocol.
 *
 * Free instance for kafka protocol.
 *
 * Return: NULL.
 */
static void
gst_kafka_free (gpointer * prop);

/**
 * gst_kafka_config:
 * @prop: the properties of message distribution protocol.
 * @path: the absolute path of config file.
 *
 * Config properties for kafka server (broker).
 *
 * Return: TRUE if config is successful.
 */
static gboolean
gst_kafka_config (gpointer * prop, gchar * path);

/**
 * gst_kafka_connect:
 * @prop: the properties of message distribution protocol.
 * @host: the IP of server to connect to.
 * @port: the port of server to connect to.
 *
 * Connect to kafka server(broker).
 *
 * Return: TRUE if connect is successful (don't wait for ACK).
 */
static gboolean
gst_kafka_connect (gpointer * prop, gchar * host, gint port);

/**
 * gst_kafka_disconnect:
 * @prop: the properties of message distribution protocol.
 *
 * Disconnect with kafka server(broker).
 *
 * Return: TRUE if disconnect is done.
 */
static gboolean
gst_kafka_disconnect (gpointer * prop);

/**
 * gst_kafka_publish:
 * @prop: the properties of message distribution protocol.
 * @topic: the topic to subscribe.
 * @message: the message to publish.
 *
 * Publish message on topic.
 *
 * Return: TRUE if publish is done.
 */
static gboolean
gst_kafka_publish (gpointer * prop, gchar * topic, gpointer message);

/**
 * gst_kafka_subscribe:
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
gst_kafka_subscribe (gpointer * prop, gchar * topic,
                     GstAdaptorSubscribeCallback callback, gpointer adaptor);
