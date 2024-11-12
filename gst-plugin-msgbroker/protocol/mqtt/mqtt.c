/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "mqtt.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <assert.h>

#include "mosquitto.h"
#include "mqtt_protocol.h"

#define GST_CAT_DEFAULT gst_mqtt_debug
GST_DEBUG_CATEGORY (gst_mqtt_debug);

#define MAX_BYTES_EACH_LINE 128

// Default mqtt properties
#define DEFAULT_MQTT_CLIENT_ROLE        GST_MQTT_CLIENT_ROLE_NONE
#define DEFAULT_MQTT_TOPIC              NULL
#define DEFAULT_MQTT_ID                 NULL
#define DEFAULT_MQTT_QOS                0
#define DEFAUlT_MQTT_CLEAN_SESSION      TRUE
#define DEFAULT_MQTT_KEEPALIVE          60
#define DEFALUT_MQTT_RETAIN             FALSE
#define DEFAULT_MQTT_VERSION            GST_MQTT_VERSION_NONE
#define DEFAULT_MQTT_WILL_TOPIC         NULL
#define DEFAULT_MQTT_WILL_PAYLOAD       NULL
#define DEFAULT_MQTT_WILL_QOS           0
#define DEFAULT_MQTT_WILL_RETAIN        FALSE
#define DEFAULT_MQTT_MAX_INFLIGHT       20
#define DEFAULT_MQTT_TCP_NODELAY        FALSE
#define DEFAULT_MQTT_USERNAME           NULL
#define DEFAULT_MQTT_PASSWORD           NULL
#define DEFAULT_MQTT_USD_PATH           NULL
#define DEFAULT_MQTT_PROPERTIES_V5      NULL
#define DEFAULT_MQTT_SOCKS5_HOST        NULL
#define DEFAULT_MQTT_SOCKS5_PORT        1883
#define DEFAULT_MQTT_SOCKS5_USERNAME    NULL
#define DEFAULT_MQTT_SOCKS5_PASSWORD    NULL
#define DEFAULT_ADAPTOR_SUB_CALLBACK    NULL

typedef struct _MosquittoHandler MosquittoHandler;
static MosquittoHandler * mosquitto_handler = NULL;

// Macro to wrap functions in libmosquitto
#define MOSQUITTO_LIB_INIT                mosquitto_handler->lib_init
#define MOSQUITTO_NEW                     mosquitto_handler->new
#define MOSQUITTO_CONNECT_BIND            mosquitto_handler->connect_bind_v5
#define MOSQUITTO_DISCONNECT              mosquitto_handler->disconnect_v5
#define MOSQUITTO_LOOP_START              mosquitto_handler->loop_start
#define MOSQUITTO_LOOP_STOP               mosquitto_handler->loop_stop
#define MOSQUITTO_PUBLISH                 mosquitto_handler->publish_v5
#define MOSQUITTO_SUBSCRIBE               mosquitto_handler->subscribe_v5
#define MOSQUITTO_DESTROY                 mosquitto_handler->destroy
#define MOSQUITTO_LIB_CLEANUP             mosquitto_handler->lib_cleanup
#define MOSQUITTO_INT_OPTION              mosquitto_handler->int_option
#define MOSQUITTO_WILL_SET                mosquitto_handler->will_set_v5
#define MOSQUITTO_USERNAME_PW_SET         mosquitto_handler->username_pw_set
#define MOSQUITTO_SOCKS5_SET              mosquitto_handler->socks5_set
#define MOSQUITTO_CONNECT_CALLBACK_SET    mosquitto_handler->connect_v5_callback_set
#define MOSQUITTO_DISCONNECT_CALLBACK_SET mosquitto_handler->disconnect_v5_callback_set
#define MOSQUITTO_PUBLISH_CALLBACK_SET    mosquitto_handler->publish_v5_callback_set
#define MOSQUITTO_SUBSCRIBE_CALLBACK_SET  mosquitto_handler->subscribe_v5_callback_set
#define MOSQUITTO_MESSAGE_CALLBACK_SET    mosquitto_handler->message_v5_callback_set
#define MOSQUITTO_TOPIC_MATCHES_SUB       mosquitto_handler->topic_matches_sub
#define MOSQUITTO_CONACK_STRING           mosquitto_handler->connack_string
#define MOSQUITTO_REASON_STRING           mosquitto_handler->reason_string
#define MOSQUITTO_STRERROR                mosquitto_handler->strerror

// Global structure used in dlsym to parse
GstProtocolCommonFunc GST_PROTOCOL_CFUNC_SYMBOL = {
  .new = gst_mqtt_new,
  .free = gst_mqtt_free,
  .config = gst_mqtt_config,
  .connect = gst_mqtt_connect,
  .disconnect = gst_mqtt_disconnect,
  .publish = gst_mqtt_publish,
  .subscribe = gst_mqtt_subscribe
};

/**
 * GstClientRole:
 * @GST_MQTT_CLIENT_ROLE_NONE: no client role.
 * @GST_MQTT_PUB: publisher.
 * @GST_MQTT_SUB: subscriber.
 *
 * Client role.
 */
typedef enum {
  GST_MQTT_CLIENT_ROLE_NONE,
  GST_MQTT_PUB,
  GST_MQTT_SUB,
} GstClientRole;

/**
 * GstMqttVersion:
 * @GST_MQTT_VERSION_NONE: no version.
 * @GST_MQTT_VERSION_31: MQTT 3.1.
 * @GST_MQTT_VERSION_311: MQTT 3.1.1.
 * @GST_MQTT_VERSION_5: MQTT 5.0.
 *
 * Protocol version of MQTT.
 */
typedef enum {
  GST_MQTT_VERSION_NONE,
  GST_MQTT_VERSION_31,
  GST_MQTT_VERSION_311,
  GST_MQTT_VERSION_5,
} GstMqttVersion;

struct _GstMqtt {
  /// Mosquitto client role
  GstClientRole                 role;

  /// Mqtt version
  GstMqttVersion                mqtt_version;

  /// Mosquitto client instance
  struct mosquitto              *mosq;

  /// Topic to publish or subscribe
  gchar                         *topic;

  /// Client id (if id is NULL, mosquitto server will give a random one)
  gchar                         *id;

  /// Quality of service level (0, 1, 2) for client
  gint                          qos;

  /// Clean existing sessions for the same client id or not
  gboolean                      clean_session;

  /// Seconds to keep alive for this client
  gint                          keepalive;

  /// Message should be retained or not
  gboolean                      retain;

  /// The topic to send to client in case of exceptionally disconnected
  gchar                         *will_topic;
  /// The topic to message to client in case of exceptionally disconnected
  gchar                         *will_payload;
  /// The quality of service of will in case of exceptionally disconnected
  gint                          will_qos;
  /// Will message should be retained or not in case of exceptionally disconnected
  gboolean                      will_retain;

  /// The maximum inflight messages for QoS 1/2
  guint                         max_inflight;

  /// Reduce socket sending latency or not for more packets being sent.
  gboolean                      tcp_nodelay;

  /// Username to verify
  gchar                         *username;
  /// Password to verify
  gchar                         *password;

  /// Connect via unix socket domain path
  gchar                         *usd_path;

  /// Socks5 server IP to connect
  gchar                         *socks5_host;
  /// Socks5 server port to connect
  gint                          socks5_port;
  /// Socks5 server username to verify
  gchar                         *socks5_username;
  /// Socks5 server password to verify
  gchar                         *socks5_password;

  /// Properties for MQTT 5.0 supported by libmosquitto
  mosquitto_property            *properties_v5;

  /// Pointer point to GstProtocolAdaptor used for callback
  gpointer                      adaptor;
  /// Callback to bring data to adaptor
  GstAdaptorSubscribeCallback   callback;
};

typedef struct mosquitto mosquitto;

struct _MosquittoHandler {
  // mosquitto library handle.
  gpointer                      lib_handle;

  // mosquitto library APIs.
  libmosq_EXPORT int                (*lib_init)                   (void);
  libmosq_EXPORT mosquitto *        (*new)                        (const char *,
      bool, void *);
  libmosq_EXPORT int                (*connect_bind_v5)            (mosquitto *,
      const char *, int, int, const char *, const mosquitto_property *);
  libmosq_EXPORT int                (*disconnect_v5)              (mosquitto *,
      int, const mosquitto_property *);
  libmosq_EXPORT int                (*loop_start)                 (mosquitto *);
  libmosq_EXPORT int                (*loop_stop)                  (mosquitto *,
      bool);
  libmosq_EXPORT int                (*publish_v5)                 (mosquitto *,
      int*, const char*, int, const void*, int ,bool , const mosquitto_property*);
  libmosq_EXPORT int                (*subscribe_v5)               (mosquitto *,
      int *, const char *, int, int, const mosquitto_property *);
  libmosq_EXPORT void               (*destroy)                    (mosquitto *);
  libmosq_EXPORT int                (*lib_cleanup)                (void);
  libmosq_EXPORT int                (*int_option)                 (mosquitto *,
      enum mosq_opt_t, int);
  libmosq_EXPORT int                (*will_set_v5)                (mosquitto *,
      const char *, int, const void *, int, bool, mosquitto_property *);
  libmosq_EXPORT int                (*username_pw_set)            (mosquitto *,
      const char *, const char *);
  libmosq_EXPORT int                (*socks5_set)                 (mosquitto *,
      const char *, int, const char *, const char *);
  libmosq_EXPORT void               (*connect_v5_callback_set)    (mosquitto *,
      void (*on_connect)(mosquitto*, void*, int, int, const mosquitto_property*));
  libmosq_EXPORT void               (*disconnect_v5_callback_set) (mosquitto *,
      void (*on_disconnect)(mosquitto*, void*, int, const mosquitto_property*));
  libmosq_EXPORT void               (*publish_v5_callback_set)    (mosquitto *,
      void (*on_publish)(mosquitto*, void*, int, int, const mosquitto_property*));
  libmosq_EXPORT void               (*subscribe_v5_callback_set)  (mosquitto *,
      void (*on_subscribe)(mosquitto*, void*, int, int, const int*,
          const mosquitto_property*));
  libmosq_EXPORT void               (*message_v5_callback_set)    (mosquitto *,
      void (*on_message)(mosquitto*, void*, const struct mosquitto_message*,
          const mosquitto_property*));
  libmosq_EXPORT int                (*topic_matches_sub)          (const char *,
      const char *, bool *);
  libmosq_EXPORT const char *       (*connack_string)             (int);
  libmosq_EXPORT const char *       (*reason_string)              (int);
  libmosq_EXPORT const char *       (*strerror)                   (int);
};

gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  gboolean success = TRUE;

  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR ("Failed to find symbol %s, error: %s!", name, dlerror ());
    success = FALSE;
  }

  return success;
}

static __attribute__((constructor))
void gst_mosquitto_handler_init ()
{
  mosquitto_handler = (MosquittoHandler *) g_malloc (sizeof(MosquittoHandler));

  if (NULL == mosquitto_handler) {
    GST_ERROR ("Failed to allocate memory for MosquittoHandler !");
  }

  assert (NULL != mosquitto_handler);

  mosquitto_handler->lib_handle = dlopen ("libmosquitto.so", RTLD_NOW | RTLD_LOCAL);

  if (NULL == mosquitto_handler->lib_handle) {
    GST_ERROR ("Failed to open mosquitto library, error: %s!", dlerror ());
  }

  assert (NULL != mosquitto_handler->lib_handle);

  gboolean success = load_symbol ((gpointer*)&MOSQUITTO_LIB_INIT,
      mosquitto_handler->lib_handle, "mosquitto_lib_init");

  success &= load_symbol ((gpointer*)&MOSQUITTO_NEW,
      mosquitto_handler->lib_handle, "mosquitto_new");

  success &= load_symbol ((gpointer*)&MOSQUITTO_CONNECT_BIND,
      mosquitto_handler->lib_handle, "mosquitto_connect_bind_v5");

  success &= load_symbol ((gpointer*)&MOSQUITTO_DISCONNECT,
      mosquitto_handler->lib_handle, "mosquitto_disconnect_v5");

  success &= load_symbol ((gpointer*)&MOSQUITTO_LOOP_START,
      mosquitto_handler->lib_handle, "mosquitto_loop_start");

  success &= load_symbol ((gpointer*)&MOSQUITTO_LOOP_STOP,
      mosquitto_handler->lib_handle, "mosquitto_loop_stop");

  success &= load_symbol ((gpointer*)&MOSQUITTO_PUBLISH,
      mosquitto_handler->lib_handle, "mosquitto_publish_v5");

  success &= load_symbol ((gpointer*)&MOSQUITTO_SUBSCRIBE,
      mosquitto_handler->lib_handle, "mosquitto_subscribe_v5");

  success &= load_symbol ((gpointer*)&MOSQUITTO_DESTROY,
      mosquitto_handler->lib_handle, "mosquitto_destroy");

  success &= load_symbol ((gpointer*)&MOSQUITTO_LIB_CLEANUP,
      mosquitto_handler->lib_handle, "mosquitto_lib_cleanup");

  success &= load_symbol ((gpointer*)&MOSQUITTO_INT_OPTION,
      mosquitto_handler->lib_handle, "mosquitto_int_option");

  success &= load_symbol ((gpointer*)&MOSQUITTO_WILL_SET,
      mosquitto_handler->lib_handle, "mosquitto_will_set_v5");

  success &= load_symbol ((gpointer*)&MOSQUITTO_USERNAME_PW_SET,
      mosquitto_handler->lib_handle, "mosquitto_username_pw_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_SOCKS5_SET,
      mosquitto_handler->lib_handle, "mosquitto_socks5_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_CONNECT_CALLBACK_SET,
      mosquitto_handler->lib_handle, "mosquitto_connect_v5_callback_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_DISCONNECT_CALLBACK_SET,
      mosquitto_handler->lib_handle, "mosquitto_disconnect_v5_callback_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_PUBLISH_CALLBACK_SET,
      mosquitto_handler->lib_handle, "mosquitto_publish_v5_callback_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_SUBSCRIBE_CALLBACK_SET,
      mosquitto_handler->lib_handle, "mosquitto_subscribe_v5_callback_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_MESSAGE_CALLBACK_SET,
      mosquitto_handler->lib_handle, "mosquitto_message_v5_callback_set");

  success &= load_symbol ((gpointer*)&MOSQUITTO_TOPIC_MATCHES_SUB,
      mosquitto_handler->lib_handle, "mosquitto_topic_matches_sub");

  success &= load_symbol ((gpointer*)&MOSQUITTO_CONACK_STRING,
      mosquitto_handler->lib_handle, "mosquitto_connack_string");

  success &= load_symbol ((gpointer*)&MOSQUITTO_REASON_STRING,
      mosquitto_handler->lib_handle, "mosquitto_reason_string");

  success &= load_symbol ((gpointer*)&MOSQUITTO_STRERROR,
      mosquitto_handler->lib_handle, "mosquitto_strerror");

  assert (FALSE != success);
}

static __attribute__((destructor))
void gst_mosquitto_handler_deinit ()
{
  if (mosquitto_handler->lib_handle != NULL)
    dlclose (mosquitto_handler->lib_handle);

  if (mosquitto_handler != NULL) {
    g_free (mosquitto_handler);
    mosquitto_handler = NULL;
  }
}

static inline void
gst_mqtt_init_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_mqtt_debug,
        "mqtt", 0, "MQTT wrapper based on mosquitto");
    g_once_init_leave (&catonce, TRUE);
  }
}

/*
  This is called when the library receives a CONNACK message in response
  to a connection.
*/
static void
connect_callback (struct mosquitto *mosq, void *obj, int result, int flags,
    const mosquitto_property *properties)
{
  GstMqtt *mqtt = (GstMqtt *) obj;

  // Handle success
  if (!result) {
    GST_DEBUG ("Connect ACK.");
    return;
  }

  // Handle errors
  switch (mqtt->mqtt_version) {
    case GST_MQTT_VERSION_31:
    case GST_MQTT_VERSION_311:
      GST_ERROR ("Connect ACK Error: %s", MOSQUITTO_CONACK_STRING (result));
      break;
    case GST_MQTT_VERSION_5:
      GST_ERROR ("Connect ACK Error: %s", MOSQUITTO_REASON_STRING (result));
      break;
    default:
      GST_ERROR ("Connect ACK Error.");
      break;
  }
  MOSQUITTO_DISCONNECT (mosq, 0, properties);
}

/*
  This is called when the broker has received the DISCONNECT command and
  has disconnected the client.
*/
static void
disconnect_callback (struct mosquitto *mosq, void *obj, int result,
    const mosquitto_property *properties)
{
  if (!result)
    GST_DEBUG ("Disconnect ACK.");
  else
    GST_ERROR ("Disconnect ACK Error: %s", MOSQUITTO_REASON_STRING (result));
}

/*
  This is called when a message(publish) initiated has been sent to server
  successfully or server responded with an error.
*/
static void
publish_callback (struct mosquitto *mosq, void *obj, int mid, int reason_code,
    const mosquitto_property *properties)
{
  if (!reason_code)
    GST_DEBUG ("Publish ACK.");
  else
    GST_ERROR ("Publish ACK Error: %s", MOSQUITTO_REASON_STRING (reason_code));
}

/*
  This is called when a topic(subscribe) initiated has been sent to server
  successfully or server responded with an error.
*/
static void
subscribe_callback (struct mosquitto *mosq, void *obj, int mid, int qos_count,
    const int *granted_qos, const mosquitto_property *props)
{
  GstMqtt *mqtt = (GstMqtt *) obj;

  GST_DEBUG ("Subscribe ACK. id:%s, topic: %s, qos: %d.", mqtt->id,
      mqtt->topic, mqtt->qos);
}

/*
  This is called when a message has been published to the server and the server
  is distributing that message to the client.
*/
static void
message_callback (struct mosquitto *mosq, void *obj,
    const struct mosquitto_message *message, const mosquitto_property *properties)
{
  GstMqtt *mqtt = (GstMqtt *) obj;
  GstAdaptorCallbackInfo *cbinfo = NULL;
  GstBuffer *buffer = NULL;
  bool ret = FALSE;

  // Check whether a topic matches a subscription
  MOSQUITTO_TOPIC_MATCHES_SUB (mqtt->topic, message->topic, &ret);
  if (ret)
    GST_DEBUG ("The topic matches the subscription.");
  else {
    GST_ERROR ("The topic doesn't match the subscription, drop it."
        "Subscription: %s, but message: %s", mqtt->topic, message->topic);
    return;
  }

  // Pass the message to adaptor
  if (mqtt->callback != NULL) {
    GstMessageInfo *msginfo = NULL;

    cbinfo = g_slice_new0 (GstAdaptorCallbackInfo);
    if (!cbinfo) {
      GST_ERROR ("Faild to allocate GstAdaptorCallbackInfo, skip.");
      return;
    }

    cbinfo->cbtype = GST_CALLBACK_INFO_MESSAGE;
    msginfo = &(cbinfo->info.msginfo);
    msginfo->topic = (gpointer)message->topic;
    buffer = gst_buffer_new_memdup (message->payload, message->payloadlen);
    msginfo->message = (gpointer)buffer;
    GST_DEBUG ("Topic: %s wrapped buffer %p with size (%d)", (gchar *)msginfo->topic,
        msginfo->message, message->payloadlen);

    mqtt->callback (mqtt->adaptor, cbinfo);
    g_slice_free (GstAdaptorCallbackInfo, cbinfo);

    GST_DEBUG ("Message (topic: %s; length: %d) has been sent to adaptor.",
        message->topic, message->payloadlen);
  }
  else
    GST_ERROR ("Callback (bring message to adaptor) is lost.");
}

// Convert protocol version from gstreamer to mosquitto
static gint
convert_protocol_version (GstMqttVersion mqtt_version)
{
  switch (mqtt_version) {
    case GST_MQTT_VERSION_31:
      return MQTT_PROTOCOL_V31;
      break;
    case GST_MQTT_VERSION_311:
      return MQTT_PROTOCOL_V311;
      break;
    case GST_MQTT_VERSION_5:
      return MQTT_PROTOCOL_V5;
      break;
    default:
      GST_WARNING ("Unsupported protocol, falling to MQTT_PROTOCOL_V311.");
      break;
  }

  return MQTT_PROTOCOL_V311;
}

// Convert client role from gchar to GstClientRole.
static GstClientRole
convert_client_role (const gchar *role)
{
  GstClientRole client_role = DEFAULT_MQTT_CLIENT_ROLE;

  if (!strcmp (role, "pub"))
    client_role = GST_MQTT_PUB;
  else if (!strcmp (role, "sub"))
    client_role = GST_MQTT_SUB;
  else
    GST_ERROR ("Client Role: %s unknown", role);

  return client_role;
}

static void
gst_mqtt_init (GstMqtt *prop)
{
  GstMqtt *mqtt = prop;

  g_return_if_fail (prop != NULL);

  GST_DEBUG ("Initialize GstMqtt based on libmosquitto.");

  mqtt->mqtt_version = DEFAULT_MQTT_VERSION;
  mqtt->topic = DEFAULT_MQTT_TOPIC;
  mqtt->id = DEFAULT_MQTT_ID;
  mqtt->qos = DEFAULT_MQTT_QOS;
  mqtt->clean_session = DEFAUlT_MQTT_CLEAN_SESSION;
  mqtt->keepalive = DEFAULT_MQTT_KEEPALIVE;
  mqtt->retain = DEFALUT_MQTT_RETAIN;
  mqtt->will_topic = DEFAULT_MQTT_WILL_TOPIC;
  mqtt->will_payload = DEFAULT_MQTT_WILL_PAYLOAD;
  mqtt->will_retain = DEFAULT_MQTT_WILL_RETAIN;
  mqtt->max_inflight = DEFAULT_MQTT_MAX_INFLIGHT;
  mqtt->tcp_nodelay = DEFAULT_MQTT_TCP_NODELAY;
  mqtt->username = DEFAULT_MQTT_USERNAME;
  mqtt->password = DEFAULT_MQTT_PASSWORD;
  mqtt->usd_path = DEFAULT_MQTT_USD_PATH;
  mqtt->socks5_host = DEFAULT_MQTT_SOCKS5_HOST;
  mqtt->socks5_port = DEFAULT_MQTT_SOCKS5_PORT;
  mqtt->socks5_username = DEFAULT_MQTT_SOCKS5_USERNAME;
  mqtt->socks5_password = DEFAULT_MQTT_SOCKS5_PASSWORD;
  mqtt->properties_v5 = DEFAULT_MQTT_PROPERTIES_V5;

  mqtt->adaptor = NULL;
  mqtt->callback = DEFAULT_ADAPTOR_SUB_CALLBACK;
}

static gpointer
gst_mqtt_new (const gchar *role)
{
  GstMqtt *mqtt = NULL;

  g_return_val_if_fail (role != NULL, NULL);

  gst_mqtt_init_debug_category();

  GST_DEBUG ("GstMqtt allocating.");

  mqtt = g_slice_new0 (GstMqtt);
  if (!mqtt) {
    GST_ERROR ("Failed to allocate mqtt properties.");
    return NULL;
  }

  mqtt->role = convert_client_role (role);
  if (mqtt->role == GST_MQTT_CLIENT_ROLE_NONE) {
    g_slice_free (GstMqtt, mqtt);
    mqtt = NULL;
    return NULL;
  }

  if (MOSQUITTO_LIB_INIT() != MOSQ_ERR_SUCCESS) {
    GST_ERROR ("Failed to initialize mosquitto library.");
    g_slice_free (GstMqtt, mqtt);
    mqtt = NULL;
    return NULL;
  }

  gst_mqtt_init (mqtt);

  GST_DEBUG ("GstMqtt allocated and initialized.");

  return (gpointer)mqtt;
}

static void
gst_mqtt_free (gpointer *prop)
{
  GstMqtt *mqtt = (GstMqtt *) prop;

  g_return_if_fail (prop != NULL);

  GST_DEBUG ("GstMqtt Free.");

  if (mqtt->mosq != NULL) {
    MOSQUITTO_DESTROY (mqtt->mosq);
    MOSQUITTO_LIB_CLEANUP ();
  }

  g_free (mqtt->topic);
  g_free (mqtt->id);
  g_free (mqtt->will_topic);
  g_free (mqtt->will_payload);
  g_free (mqtt->username);
  g_free (mqtt->password);
  g_free (mqtt->usd_path);
  g_free (mqtt->socks5_host);
  g_free (mqtt->socks5_username);
  g_free (mqtt->socks5_password);
  g_slice_free (GstMqtt, mqtt);
}

static void
config_parse (GstMqtt *mqtt, gchar *prop, gchar *value)
{
  g_return_if_fail (mqtt != NULL);
  g_return_if_fail (prop != NULL);
  g_return_if_fail (value != NULL);

  GST_DEBUG ("prop: %s, value: %s.", prop, value);

  if (!strcmp (prop, "id")) {
    mqtt->id = g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->id);
  } else if (!strcmp (prop, "qos")) {
    mqtt->qos = atoi (value);
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->qos);
  } else if (!strcmp (prop, "clean_session")) {
    mqtt->clean_session = (!strcmp (value, "TRUE"))? TRUE: FALSE;
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->clean_session);
  } else if (!strcmp (prop, "keepalive")) {
    mqtt->keepalive = atoi (value);
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->keepalive);
  } else if (!strcmp (prop, "retain")) {
    mqtt->retain = (!strcmp (value, "TRUE"))? TRUE: FALSE;
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->retain);
  } else if (!strcmp (prop, "mqtt_version")) {
    mqtt->mqtt_version =
        (!strcmp (value, "MQTTV31"))? GST_MQTT_VERSION_31:
        (!strcmp (value, "MQTTV311"))? GST_MQTT_VERSION_311:
        (!strcmp (value, "MQTTV5"))? GST_MQTT_VERSION_5: GST_MQTT_VERSION_NONE;
    GST_DEBUG ("Property %s set to %s.", prop, value);

    if (mqtt->mqtt_version == GST_MQTT_VERSION_NONE) {
      GST_ERROR ("Property %s has invalid value, falling back to MQTTV311.",
          prop);
      mqtt->mqtt_version = GST_MQTT_VERSION_311;
    }
  } else if (!strcmp (prop, "will_topic")) {
    mqtt->will_topic = (!strcmp (value, "NULL"))? NULL: g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->will_topic);
  } else if (!strcmp (prop, "will_payload")) {
    mqtt->will_payload = (!strcmp (value, "NULL"))? NULL: g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->will_payload);
  } else if (!strcmp (prop, "will_qos")) {
    mqtt->will_qos = atoi (value);
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->will_qos);
  } else if (!strcmp (prop, "will_retain")) {
    mqtt->will_retain = atoi (value);
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->will_retain);
  } else if (!strcmp (prop, "max_inflight")) {
    mqtt->max_inflight = atoi (value);
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->max_inflight);
  } else if (!strcmp (prop, "tcp_nodelay")) {
    mqtt->tcp_nodelay = (!strcmp(value, "TRUE"))? TRUE: FALSE;
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->tcp_nodelay);
  } else if (!strcmp (prop, "username")) {
    mqtt->username = (!strcmp (value, "NULL"))? NULL: g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->username);
  } else if (!strcmp (prop, "password")) {
    mqtt->password = (!strcmp (value, "NULL"))? NULL: g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->password);
  } else if (!strcmp (prop, "usd_path")) {
    mqtt->usd_path = (!strcmp (value, "NULL"))? NULL: g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->usd_path);
  } else if (!strcmp (prop, "socks5_host")) {
    mqtt->socks5_host = (!strcmp (value, "NULL"))? NULL: g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->socks5_host);
  } else if (!strcmp (prop, "socks5_port")) {
    mqtt->socks5_port = atoi (value);
    GST_DEBUG ("Property %s set to %d.", prop, mqtt->socks5_port);
  } else if (!strcmp (prop, "socks5_username")) {
    mqtt->socks5_username = (!strcmp (value, "NULL"))? NULL:
        g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->socks5_username);
  } else if (!strcmp (prop, "socks5_password")) {
    mqtt->socks5_password = (!strcmp (value, "NULL"))? NULL:
        g_strdup (value);
    GST_DEBUG ("Property %s set to %s.", prop, mqtt->socks5_password);
  } else if (!strcmp (prop, "mosquitto_property")) {
    // TODO: add this property for MQTTV5
  } else
    GST_WARNING ("Property %s could not be found.", prop);
}

static gboolean
extract_prop_from_file (GstMqtt *mqtt, gchar *path)
{
  FILE *file = NULL;
  gchar *line_buf = NULL;
  gchar *prop = NULL;
  gchar *value = NULL;

  g_return_val_if_fail (mqtt != NULL, FALSE);
  g_return_val_if_fail (path != NULL, FALSE);

  GST_DEBUG ("Reading %s to config.", path);

  file = fopen (path, "r");
  if (!file) {
    GST_ERROR ("Faild to open config file :%s, error: %s.", path,
        strerror (errno));
    return FALSE;
  }

  line_buf = g_new0 (gchar, MAX_BYTES_EACH_LINE);
  if (!line_buf) {
    GST_ERROR ("Failed to allocate buffer to read config file.");
    fclose (file);
    return FALSE;
  }

  while (!feof (file)) {
    gchar *re = NULL;

    memset (line_buf, 0, sizeof (*line_buf));
    prop = line_buf;
    value = line_buf;

    re = fgets (line_buf, MAX_BYTES_EACH_LINE, file);
    if (feof (file)) {
      GST_DEBUG ("Read config file done.");
      break;
    } else if (re == NULL) {
      GST_ERROR ("Failed to read contents.");
      break;
    }

    // Removes leading and trailing whitespace from a string.
    line_buf = g_strstrip (line_buf);

    // Skip comments or empty lines
    if (g_str_has_prefix (line_buf, "#") || g_str_has_prefix (line_buf, "\n"))
      continue;

    prop = line_buf;
    value = g_strrstr (line_buf, "=");
    if (!value)
      continue;

    *value = '\0';
    ++value;
    prop = g_strstrip (prop);
    value = g_strstrip (value);
    if (*prop == '\0' || *value == '\0')
      continue;

    // Check if prop and value are legal and set
    config_parse (mqtt, prop, value);
  }

  g_free (line_buf);
  fclose (file);

  return TRUE;
}

static gboolean
gst_mqtt_config (gpointer *prop, gchar *path)
{
  GstMqtt *mqtt = (GstMqtt *) prop;
  gint version_conv = 0;

  g_return_val_if_fail (prop != NULL, FALSE);

  GST_DEBUG ("Mqtt instance config.");

  // Extract properties from config file
  if (path && !extract_prop_from_file (mqtt, path)) {
    GST_ERROR ("Failed to extract properties from config file.");
    return FALSE;
  }

  if (mqtt->id == NULL && mqtt->clean_session == FALSE) {
    GST_WARNING ("clean_session has to be TRUE if id is NULL.");
    mqtt->clean_session = TRUE;
  }

  // Struct mosquitto
  mqtt->mosq = MOSQUITTO_NEW (mqtt->id, mqtt->clean_session, mqtt);
  if(!mqtt->mosq) {
    switch (errno) {
      case ENOMEM:
        GST_ERROR ("Create mosquitto: Out of memory.");
        break;
      case EINVAL:
        GST_ERROR ("Create mosquitto: Invalid id or clean_session.");
        break;
      default:
        GST_ERROR ("Create mosquitto: Unknown error.");
        break;
    }
    goto cleanmosq;
  }

  // Protocol
  version_conv = convert_protocol_version (mqtt->mqtt_version);
  if (MOSQUITTO_INT_OPTION (mqtt->mosq, MOSQ_OPT_PROTOCOL_VERSION,
      version_conv)) {
    GST_ERROR ("Protocol failed to set.");
    goto cleanmosq;
  }

  // Max inflight
  if (MOSQUITTO_INT_OPTION (mqtt->mosq, MOSQ_OPT_SEND_MAXIMUM,
      mqtt->max_inflight)) {
    GST_ERROR ("Max inflight failed to set.");
    goto cleanmosq;
  }

  // Will
  if (mqtt->will_topic && MOSQUITTO_WILL_SET (mqtt->mosq,
      mqtt->will_topic, (int)strlen (mqtt->will_payload),
      mqtt->will_payload, (int)(mqtt->qos), mqtt->will_retain,
      mqtt->properties_v5)) {
    GST_ERROR ("Will failed to set.");
    goto cleanmosq;
  }

  // Username and password
  if ((mqtt->username || mqtt->password) && MOSQUITTO_USERNAME_PW_SET (
      mqtt->mosq, mqtt->username, mqtt->password)) {
    GST_ERROR ("Username and password failed to set.");
    goto cleanmosq;
  }

  // TCP nodelay
  if (mqtt->tcp_nodelay && MOSQUITTO_INT_OPTION (mqtt->mosq,
      MOSQ_OPT_TCP_NODELAY, mqtt->tcp_nodelay)) {
    GST_ERROR ("Tcp nodelay failed to set.");
    goto cleanmosq;
  }

  // Socks5
  if (mqtt->socks5_host && MOSQUITTO_SOCKS5_SET (mqtt->mosq,
      mqtt->socks5_host, mqtt->socks5_port, mqtt->socks5_username,
      mqtt->socks5_password)) {
    GST_ERROR ("Socks5 failed to set.");
    goto cleanmosq;
  }

  // Set callbacks
  MOSQUITTO_CONNECT_CALLBACK_SET (mqtt->mosq, connect_callback);
  MOSQUITTO_DISCONNECT_CALLBACK_SET (mqtt->mosq, disconnect_callback);

  if (mqtt->role == GST_MQTT_PUB) {
    MOSQUITTO_PUBLISH_CALLBACK_SET (mqtt->mosq, publish_callback);
    GST_DEBUG ("Publish callback set.");
  } else if (mqtt->role == GST_MQTT_SUB) {
    MOSQUITTO_SUBSCRIBE_CALLBACK_SET (mqtt->mosq, subscribe_callback);
    MOSQUITTO_MESSAGE_CALLBACK_SET (mqtt->mosq, message_callback);
    GST_DEBUG ("Subscribe callback set.");
  } else {
    GST_ERROR ("Unknown client role to set callback.");
    goto cleanmosq;
  }

  return TRUE;

cleanmosq:
  GST_DEBUG ("Error in gst_mqtt_config, cleanup");
  MOSQUITTO_DESTROY (mqtt->mosq);
  mqtt->mosq = NULL;

  return FALSE;
}

static gboolean
gst_mqtt_connect (gpointer *prop, gchar *host, gint port)
{
  GstMqtt *mqtt = (GstMqtt *) prop;
  gint ret = 0;

  g_return_val_if_fail (prop != NULL, FALSE);
  g_return_val_if_fail (host != NULL, FALSE);
  g_return_val_if_fail (port >= 0, FALSE);

  ret = MOSQUITTO_CONNECT_BIND (mqtt->mosq, host, port,
      mqtt->keepalive, NULL, mqtt->properties_v5);
  if (ret) {
    GST_ERROR ("Connect error: %s", MOSQUITTO_STRERROR (ret));
    return FALSE;
  } else
    GST_DEBUG ("Connected successfully.");

  if (MOSQUITTO_LOOP_START (mqtt->mosq) != MOSQ_ERR_SUCCESS) {
    GST_ERROR ("Failed to start mosquitto loop, disconnect.");
    MOSQUITTO_DISCONNECT (mqtt->mosq, 0, mqtt->properties_v5);
    return FALSE;
  } else
    GST_DEBUG ("Mosquitto loop started in a new thread.");

  return TRUE;
}

static gboolean
gst_mqtt_disconnect (gpointer *prop)
{
  GstMqtt *mqtt = (GstMqtt *) prop;
  gint ret = 0;

  g_return_val_if_fail (prop != NULL, FALSE);

  ret = MOSQUITTO_DISCONNECT (mqtt->mosq, 0, mqtt->properties_v5);
  if (ret) {
    GST_ERROR ("Disconnect error: %s", MOSQUITTO_STRERROR (ret));
    return FALSE;
  } else
    GST_DEBUG ("Disconnect successfully.");

  if (MOSQUITTO_LOOP_STOP (mqtt->mosq, FALSE) != MOSQ_ERR_SUCCESS) {
    GST_ERROR ("Failed to stop mosquitto loop.");
    return FALSE;
  }
  GST_DEBUG ("Mosquitto loop stop.");

  return TRUE;
}

static gboolean
gst_mqtt_publish (gpointer *prop, gchar *topic, gpointer message)
{
  GstMqtt *mqtt = (GstMqtt *)prop;
  gint payload_len = strlen (message);
  gint ret = 0;

  g_return_val_if_fail (prop != NULL, FALSE);
  g_return_val_if_fail (topic != NULL, FALSE);
  g_return_val_if_fail (message != NULL, FALSE);

  if (mqtt->topic != NULL)
    g_free (mqtt->topic);

  mqtt->topic = g_strdup (topic);

  ret = MOSQUITTO_PUBLISH (mqtt->mosq, NULL, mqtt->topic, (int)(payload_len),
      message, mqtt->qos, mqtt->retain, mqtt->properties_v5);
  if (ret) {
    GST_ERROR ("Publish error: %s", MOSQUITTO_STRERROR (ret));
    return FALSE;
  } else
    GST_DEBUG ("Publish successfully, topic: %s, length: %d.", topic, payload_len);

  return TRUE;
}

static gboolean
gst_mqtt_subscribe (gpointer *prop, gchar *topic,
    GstAdaptorSubscribeCallback callback, gpointer adaptor)
{
  GstMqtt *mqtt = (GstMqtt *)prop;
  gint ret = 0;

  g_return_val_if_fail (prop != NULL, FALSE);
  g_return_val_if_fail (topic != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);
  g_return_val_if_fail (adaptor != NULL , FALSE);

  mqtt->topic = g_strdup (topic);
  mqtt->adaptor = adaptor;

  if (mqtt->callback == NULL) {
    mqtt->callback = callback;
    GST_DEBUG ("Callback to bring message to adaptor set.");
  } else if (mqtt->callback != callback) {
    GST_ERROR ("Callback is trying to set a new one.");
    return FALSE;
  }

  ret = MOSQUITTO_SUBSCRIBE (mqtt->mosq, NULL, mqtt->topic,
      mqtt->qos, 0, mqtt->properties_v5);
  if (ret) {
    GST_ERROR ("Subscribe error: %s", MOSQUITTO_STRERROR (ret));
    return FALSE;
  } else
    GST_DEBUG ("Subscribe succesfully. Topic: %s", mqtt->topic);

  return TRUE;
}
