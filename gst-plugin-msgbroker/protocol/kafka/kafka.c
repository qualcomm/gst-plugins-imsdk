/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <assert.h>

#include <librdkafka/rdkafka.h>

#include "kafka.h"

#define GST_CAT_DEFAULT gst_kafka_debug
GST_DEBUG_CATEGORY (gst_kafka_debug);

// Global structure used in dlsym to parse
GstProtocolCommonFunc GST_PROTOCOL_CFUNC_SYMBOL = {
  .new = gst_kafka_new,
  .free = gst_kafka_free,
  .config = gst_kafka_config,
  .connect = gst_kafka_connect,
  .disconnect = gst_kafka_disconnect,
  .publish = gst_kafka_publish,
  .subscribe = gst_kafka_subscribe
};

/**
 * GstKafkaClientRole:
 * @GST_KAFKA_CLIENT_ROLE_NONE: no client role.
 * @GST_KAFKA_CLIENT_ROLE_PUB: publisher.
 * @GST_KAFKA_CLIENT_ROLE_SUB: subscriber.
 *
 * Client role.
 */
typedef enum {
  GST_KAFKA_CLIENT_ROLE_NONE,
  GST_KAFKA_CLIENT_ROLE_PUB,
  GST_KAFKA_CLIENT_ROLE_SUB,
} GstKafkaClientRole;

/**
 * GstKafkaMessageStatus:
 * @GST_KAFKA_MSG_SUBMITTED: Message submitted to broker..
 * @GST_KAFKA_MSG_DELIVERY_SUCCESS: Message delivered to broker.
 * @GST_KAFKA_MSG_DELIVERY_FAIL: Message delivery to broker failed.
 *
 * Message delivery status.
 */
typedef enum {
  GST_KAFKA_MSG_SUBMITTED,
  GST_KAFKA_MSG_DELIVERY_SUCCESS,
  GST_KAFKA_MSG_DELIVERY_FAIL,
} GstKafkaMessageStatus;

struct _GstKafka {
  // Clent role (Consumer/Producer).
  GstKafkaClientRole          role;
  // Message topic.
  gchar                       *topic;
  // List of brokers to connect to.
  gchar                       *brokers;

  // Kafka client configuration.
  rd_kafka_conf_t             *conf;
  // Kafka client instance for producer.
  rd_kafka_t                  *producer;
  // Kafka client instance for consumer.
  rd_kafka_t                  *consumer;

  // Pointer to the adaptor.
  gpointer                    adaptor;
  // Subscriber callback to trigger on receiving a message.
  GstAdaptorSubscribeCallback callback;
  // Consumer task.
  GstTask                     *consumetask;
  // Consumer task mutex
  GRecMutex                   consumemutex;
  // Mutex to synchronize delivery callback and publish threads.
  GMutex                      msgmutex;
  // Message delivery status.
  GstKafkaMessageStatus       msgstatus;

  // Partition key used for publishing a message.
  gchar                       *partition_key;
  // Publisher timeout in seconds.
  guint64                     publish_timeout;
};

// Convert client role from gchar to GstKafkaClientRole.
static GstKafkaClientRole
gst_convert_client_role (const gchar * role)
{
  GstKafkaClientRole client_role = GST_KAFKA_CLIENT_ROLE_NONE;
  GST_INFO ("Received client role : %s", role);

  if (!strcmp (role, "pub"))
    client_role = GST_KAFKA_CLIENT_ROLE_PUB;
  else if (!strcmp (role, "sub"))
    client_role = GST_KAFKA_CLIENT_ROLE_SUB;
  else
    GST_ERROR ("Client Role: %s unknown", role);

  return client_role;
}

static void
gst_kafka_dr_msg_cb (rd_kafka_t * rk, const rd_kafka_message_t * rkmessage,
    void *opaque)
{
  GstKafka *self = (GstKafka *) (rkmessage->_private);

  GST_LOG ("Delivery callback triggered");

  g_mutex_lock (&self->msgmutex);

  if (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR ) {
    GST_ERROR ("Message delivery failed: %s",
        rd_kafka_err2str (rkmessage->err));
    self->msgstatus = GST_KAFKA_MSG_DELIVERY_FAIL;
  }

  GST_DEBUG ("Message delivered (%zd bytes, partition %d)", rkmessage->len,
      rkmessage->partition);

  self->msgstatus = GST_KAFKA_MSG_DELIVERY_SUCCESS;

  rd_kafka_yield (rk);

  g_mutex_unlock (&self->msgmutex);
}

static void
gst_kafka_consume_message (gpointer userdata)
{
  GstKafka *self = (GstKafka *) userdata;
  rd_kafka_message_t *rkm = NULL;

  if (self->consumer == NULL)
    return;

  // Returns NULL if no message received.
  rkm = rd_kafka_consumer_poll (self->consumer, 100);

  // No message received, try again.
  if (rkm == NULL)
    return;

  if (rkm->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
    // Most consumer errors are not fatal, the consumer
    // will automatically try to recover from all types of errors.
    GST_ERROR ("Kafka Consumer error: %s", rd_kafka_message_errstr (rkm));

    rd_kafka_message_destroy (rkm);
    return;
  }

  GST_TRACE ("Kafka Message received: topic=%s, payload=%s",
      rd_kafka_topic_name (rkm->rkt), (const char *) rkm->payload);

  if (self->callback != NULL) {
    GstMessageInfo *msginfo = NULL;
    GstAdaptorCallbackInfo cbinfo = {0};

    cbinfo.cbtype = GST_CALLBACK_INFO_MESSAGE;
    msginfo = &(cbinfo.info.msginfo);
    msginfo->topic = rd_kafka_topic_name (rkm->rkt);
    msginfo->message = gst_buffer_new_memdup (rkm->payload, rkm->len);

    self->callback (self->adaptor, &cbinfo);
  }

  rd_kafka_message_destroy (rkm);
}

static gboolean
gst_fetch_config_value (const gchar *path, const gchar *section,
    const gchar *cfg_key, gchar **cfg_val)
{
  GKeyFile *key_file = g_key_file_new ();
  GError *error = NULL;
  gchar *str = NULL;
  gboolean success = FALSE;

  if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error)) {
    GST_ERROR ("Failed to load config file %s : %s", path, error->message);
    goto cleanup;
  }

  str = g_key_file_get_string (key_file, section, cfg_key, &error);

  if (error != NULL) {
    success = (g_strcmp0 (cfg_key, "proto-cfg") == 0);
    if (success) {
      // The key is optional – treat the missing entry as a non‑error
      GST_INFO ("Optional key %s not found in group %s : ignoring",
          cfg_key, section);

      *cfg_val = NULL;
      goto cleanup;
    } else {
      GST_ERROR ("Key %s not found in group %s : %s", cfg_key,
          section, error->message);
      goto cleanup;
    }
  }

  *cfg_val = g_shell_unquote (str, &error);

  if (error != NULL) {
    GST_ERROR ("Failed to unquote %s : %s", str, error->message);
    goto cleanup;
  }

  success = TRUE;

cleanup:
  if (error != NULL)
    g_error_free (error);

  g_free (str);
  g_key_file_free (key_file);

  return success;
}

static gboolean
gst_kafka_parse_proto_cfg (gchar *confptr, rd_kafka_conf_t * conf)
{
  gchar *equalptr = NULL, *semiptr = NULL, *confkey = NULL, *confval = NULL;
  gchar *curptr = confptr;
  rd_kafka_conf_res_t err = RD_KAFKA_CONF_OK;
  gint keylen = 0, vallen = 0, conflen = strlen (confptr);
  gchar errstr[512];

  while ((curptr - confptr) < conflen &&
             (equalptr = strchr (curptr, '=')) != NULL) {
    keylen = (equalptr - curptr);

    if (equalptr >= (confptr + conflen))
      return FALSE;

    semiptr = strchr (equalptr + 1, ';');

    if (!semiptr)
      vallen = (confptr + conflen - equalptr - 1);
    else
      vallen = (semiptr - equalptr - 1);

    // Dynamically allocate and trim key and value
    confkey = g_strndup (curptr, keylen);
    confval = g_strndup (equalptr + 1, vallen);
    g_strstrip (confkey);
    g_strstrip (confval);

    err = rd_kafka_conf_set (conf, confkey, confval, errstr, sizeof (errstr));

    if (err != RD_KAFKA_CONF_OK) {
      GST_ERROR ("Error setting config %s = %s : %s", confkey, confval, errstr);
      return FALSE;
    }

    GST_INFO ("Setting config %s = %s", confkey, confval);

    g_free (confkey);
    g_free (confval);

    // Move to next key-value pair
    if (!semiptr)
      break;

    curptr = semiptr + 1;
  }

  return TRUE;
}

static inline void
gst_kafka_init_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_kafka_debug,
        "kafka", 0, "KAFKA wrapper based on rdkafka");
    g_once_init_leave (&catonce, TRUE);
  }
}

static gpointer
gst_kafka_new (const gchar * role)
{
  GstKafka *kafka = NULL;

  g_return_val_if_fail (role != NULL, NULL);

  gst_kafka_init_debug_category ();

  kafka = g_slice_new0 (GstKafka);
  if (!kafka) {
    GST_ERROR ("Failed to allocate kafka properties.");
    return NULL;
  }

  kafka->role = gst_convert_client_role (role);

  if (kafka->role == GST_KAFKA_CLIENT_ROLE_NONE) {
    g_slice_free (GstKafka, kafka);
    return NULL;
  }

  g_rec_mutex_init (&kafka->consumemutex);
  g_mutex_init (&kafka->msgmutex);

  GST_INFO ("GstKafka allocated and initialized.");

  return (gpointer) kafka;
}

static void
gst_kafka_free (gpointer * kafka)
{
  GstKafka *self = (GstKafka *) kafka;

  g_return_if_fail (self != NULL);

  g_clear_pointer (&self->topic, g_free);
  g_clear_pointer (&self->brokers, g_free);
  g_clear_pointer (&self->partition_key, g_free);

  g_rec_mutex_clear (&self->consumemutex);
  g_mutex_clear (&self->msgmutex);

  g_slice_free (GstKafka, self);
}

static gboolean
gst_kafka_config (gpointer * kafka, gchar * path)
{
  GstKafka *self = (GstKafka *) kafka;
  rd_kafka_conf_t *conf = rd_kafka_conf_new ();
  rd_kafka_conf_res_t err = RD_KAFKA_CONF_OK;
  gchar *proto_cfg = NULL, *producer_cfg = NULL, *consumer_cfg = NULL;
  gchar *timeout = NULL, *endptr = NULL, *consumer_group_id = NULL;
  gchar errstr[512];
  gboolean success = FALSE;

  if (path == NULL) {
    GST_ERROR ("Config file path is NULL");
    return FALSE;
  }

  if (!gst_fetch_config_value (path, SECTION_GLOBAL, "proto-cfg",
          &proto_cfg)) {
    GST_ERROR ("Failed to read proto-cfg from section %s", SECTION_GLOBAL);
    goto cleanup;
  }

  switch (self->role) {
    case GST_KAFKA_CLIENT_ROLE_PUB:
      if (!gst_fetch_config_value (path, SECTION_PRODUCER, "proto-cfg",
              &producer_cfg)) {
        GST_ERROR ("Failed to read proto-cfg from section %s",
            SECTION_PRODUCER);
        goto cleanup;
      }

      if (!gst_fetch_config_value (path, SECTION_PRODUCER, "partition-key",
              &self->partition_key)) {
        GST_ERROR ("Failed to read partition-key");
        goto cleanup;
      }

      GST_INFO ("partition-key set to %s", self->partition_key);

      if (!gst_fetch_config_value (path, SECTION_PRODUCER, "timeout-ms",
              &timeout)) {
        GST_ERROR ("Failed to read timeout-ms from section %s",
            SECTION_PRODUCER);
        goto cleanup;
      }

      self->publish_timeout = g_ascii_strtoull (timeout, &endptr, 10);

      if (endptr == timeout || *endptr != '\0') {
        GST_ERROR ("Failed to read publisher-timeout");
        goto cleanup;
      }

      GST_INFO ("Publisher timeout set to %ld milliseconds",
          self->publish_timeout);
      break;
    case GST_KAFKA_CLIENT_ROLE_SUB:
      if (!gst_fetch_config_value (path, SECTION_CONSUMER, "proto-cfg",
              &consumer_cfg)) {
        GST_ERROR ("Failed to read proto-cfg from section %s",
            SECTION_CONSUMER);
        goto cleanup;
      }

      if (!gst_fetch_config_value (path, SECTION_CONSUMER, "group-id",
              &consumer_group_id)) {
        GST_ERROR ("Failed to read group-id");
        goto cleanup;
      }

      GST_INFO ("Consumer group-id set to %s", consumer_group_id);
      break;
    default:
      GST_ERROR ("Invalid Client role");
      goto cleanup;
  }

  if (proto_cfg && !gst_kafka_parse_proto_cfg (proto_cfg, conf)) {
    GST_ERROR ("Failed to set proto-cfg");
    goto cleanup;
  }

  switch (self->role) {
    case GST_KAFKA_CLIENT_ROLE_PUB:
      if (producer_cfg &&
          !gst_kafka_parse_proto_cfg (producer_cfg, conf)) {
        GST_ERROR ("Failed to parse producer-proto-cfg");
        goto cleanup;
      }
      break;
    case GST_KAFKA_CLIENT_ROLE_SUB:
      if (consumer_cfg &&
          !gst_kafka_parse_proto_cfg (consumer_cfg, conf)) {
        GST_ERROR ("Failed to parse consumer-proto-cfg");
        goto cleanup;
      }

      err = rd_kafka_conf_set (conf, "group.id", consumer_group_id,
          errstr, sizeof (errstr));

      if (err != RD_KAFKA_CONF_OK) {
        GST_ERROR ("Error setting group.id = %s: %s", consumer_group_id,
            errstr);
        goto cleanup;
      }
      break;
    default:
      GST_ERROR ("Invalid client role");
      goto cleanup;
  }

  if (self->conf != NULL)
    rd_kafka_conf_destroy (self->conf);

  self->conf = conf;

  success = TRUE;

cleanup:
  g_clear_pointer (&proto_cfg, g_free);
  g_clear_pointer (&producer_cfg, g_free);
  g_clear_pointer (&consumer_cfg, g_free);
  g_clear_pointer (&timeout, g_free);
  g_clear_pointer (&consumer_group_id, g_free);

  if (!success)
    rd_kafka_conf_destroy (conf);

  return success;
}

static gboolean
gst_kafka_connect (gpointer * kafka, gchar * host, gint port)
{
  GstKafka *self = (GstKafka *) kafka;
  rd_kafka_t *rk = NULL;
  rd_kafka_conf_res_t err = RD_KAFKA_CONF_OK;
  gchar errstr[512];

  self->brokers = g_strdup_printf ("%s:%d", host, port);
  GST_DEBUG ("Connecting to brokers: %s", self->brokers);

  err = rd_kafka_conf_set (self->conf, "bootstrap.servers", self->brokers,
      errstr, sizeof (errstr));

  if (err != RD_KAFKA_CONF_OK)
    goto error;

  switch (self->role) {
    case GST_KAFKA_CLIENT_ROLE_PUB:
      rd_kafka_conf_set_dr_msg_cb (self->conf, gst_kafka_dr_msg_cb);
      rk = rd_kafka_new (RD_KAFKA_PRODUCER, self->conf, errstr, sizeof (errstr));

      if (rk == NULL) {
        GST_ERROR ("Failed to create new publisher: %s", errstr);
        goto error;
      }

      //conf structure is freed by rd_kafka_new on success.
      self->conf = NULL;
      self->producer = rk;
      break;
    case GST_KAFKA_CLIENT_ROLE_SUB:
      rk = rd_kafka_new (RD_KAFKA_CONSUMER, self->conf, errstr,
          sizeof (errstr));

      if (rk == NULL) {
        GST_ERROR (" Failed to create new consumer: %s", errstr);
        goto error;
      }

      //conf structure is freed by rd_kafka_new on success.
      self->conf = NULL;
      self->consumer = rk;
      break;
    default:
      GST_ERROR ("Invalid client role");
      return FALSE;
  }

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_kafka_disconnect (gpointer * kafka)
{
  GstKafka *self = (GstKafka *) kafka;
  rd_kafka_resp_err_t err = RD_KAFKA_CONF_OK;

  g_clear_pointer (&self->conf, rd_kafka_conf_destroy);

  // Destroy the producer instance
  if (self->producer != NULL) {
    err = rd_kafka_flush (self->producer, 10000);

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
      GST_ERROR ("Failed to flush producer instance with error: %s",
          rd_kafka_err2str (err));
      return FALSE;
    }

    g_clear_pointer (&self->producer, rd_kafka_destroy);
  }

  // Destroy the consumer instance
  if (self->consumer != NULL) {
    g_rec_mutex_lock (&self->consumemutex);

    if (!gst_task_stop (self->consumetask))
      GST_WARNING ("Failed to stop consumer task!");

    rd_kafka_consumer_close (self->consumer);
    g_clear_pointer (&self->consumer, rd_kafka_destroy);

    g_rec_mutex_unlock (&self->consumemutex);

    if (!gst_task_join (self->consumetask)) {
      GST_ERROR ("Failed to join consumer task!");
      return FALSE;
    }

    gst_object_unref (self->consumetask);
  }

  return TRUE;
}

static gboolean
gst_kafka_publish (gpointer * kafka, gchar * topic, gpointer payload)
{
  GstKafka *self = (GstKafka *) kafka;
  rd_kafka_resp_err_t err = RD_KAFKA_CONF_OK;
  gint64 end_time = 0, current_time = 0;
  gint payload_len = strlen (payload);

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (topic != NULL, FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  if (self->topic == NULL)
    self->topic = g_strdup (topic);

  g_mutex_lock (&self->msgmutex);
  self->msgstatus = GST_KAFKA_MSG_SUBMITTED;
  g_mutex_unlock (&self->msgmutex);

  err = rd_kafka_producev (self->producer, RD_KAFKA_V_TOPIC (self->topic),
      RD_KAFKA_V_MSGFLAGS (RD_KAFKA_MSG_F_COPY),
      RD_KAFKA_V_VALUE ((void *) payload, payload_len),
      RD_KAFKA_V_KEY (self->partition_key, strlen (self->partition_key)),
      RD_KAFKA_V_OPAQUE (self), RD_KAFKA_V_END);

  GST_INFO ("Tried publishing message %s", (char *) payload);

  // This err is to catch immediate client-side issues.
  if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
    GST_ERROR ("Failed to schedule kafka send: Error = %s on topic %s",
        rd_kafka_err2str (err), self->topic);
    return FALSE;
  }

  rd_kafka_poll (self->producer, self->publish_timeout);

  g_mutex_lock (&self->msgmutex);

  if (self->msgstatus != GST_KAFKA_MSG_DELIVERY_SUCCESS) {
    GST_ERROR ("Failed to publish message to Kafka topic %s", topic);
    return FALSE;
  }

  g_mutex_unlock (&self->msgmutex);

  GST_DEBUG ("Published successfully, topic: %s, length: %d", self->topic,
      payload_len);

  return TRUE;
}

static gboolean
gst_kafka_subscribe (gpointer * kafka, gchar * topic,
    GstAdaptorSubscribeCallback callback, gpointer adaptor)
{
  GstKafka *self = (GstKafka *) kafka;
  rd_kafka_topic_partition_list_t *subscription = NULL;
  rd_kafka_resp_err_t err = RD_KAFKA_CONF_OK;
  gint num_topics = 1;

  g_return_val_if_fail (kafka != NULL, FALSE);
  g_return_val_if_fail (topic != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);
  g_return_val_if_fail (adaptor != NULL, FALSE);

  if (self->topic == NULL)
    self->topic = g_strdup (topic);

  GST_INFO ("Subscribing to topic %s", self->topic);

  self->adaptor = adaptor;

  if (self->callback == NULL) {
    self->callback = callback;
    GST_INFO ("Callback to bring message to adaptor set.");
  } else if (self->callback != callback) {
    GST_ERROR ("Callback is already set. Cannot set a new one.");
    return FALSE;
  }

  subscription = rd_kafka_topic_partition_list_new (num_topics);
  rd_kafka_topic_partition_list_add (subscription, self->topic,
      RD_KAFKA_PARTITION_UA);

  //Subscribe to the list of topics.
  err = rd_kafka_subscribe (self->consumer, subscription);

  if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
    rd_kafka_topic_partition_list_destroy (subscription);
    GST_ERROR ("Failed to subscribe to topic %s, error: %s", topic,
        rd_kafka_err2str (err));
    return FALSE;
  }

  GST_DEBUG ("Successfully Subscribed to topic: %s", self->topic);
  rd_kafka_topic_partition_list_destroy (subscription);

  //Consumer thread to poll the broker for messages.
  self->consumetask = gst_task_new (gst_kafka_consume_message, self, NULL);

  gst_task_set_lock (self->consumetask, &self->consumemutex);

  if (!gst_task_start (self->consumetask)) {
    GST_ERROR ("Failed to start consumer task");
    return FALSE;
  }

  return TRUE;
}
