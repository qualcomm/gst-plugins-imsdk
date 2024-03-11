/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>

#include <gst/gst.h>

#define GST_PROTOCOL_CFUNC_SYMBOL gstmsgbrokeradaptorcommonfunc

typedef struct _GstProtocolCommonFunc GstProtocolCommonFunc;
typedef struct _GstEventInfo GstEventInfo;
typedef struct _GstMessageInfo GstMessageInfo;
typedef struct _GstAdaptorCallbackInfo GstAdaptorCallbackInfo;

/******************************** Callbacks ********************************/
/**
 * GstSubscribeCallback:
 * @queue: data queue derived from upper-level callers for saving data.
 * @cbinfo: information of callback including type and data.
 *
 * Function prototype for passing data from adaptor to upper-level callers
 * in case of subscription.
 *
 * Returns: NONE.
 */
typedef void (*GstSubscribeCallback) (gpointer queue,
                                      GstAdaptorCallbackInfo *cbinfo);

/**
 * GstAdaptorSubscribeCallback:
 * @adaptor: message distribution protocol adaptor.
 * @cbinfo: information of callback including type and data.
 *
 * Function prototype for protocol instance to send data to adaptor
 * in case of subscription.
 *
 * Returns: NONE.
 */
typedef void (*GstAdaptorSubscribeCallback) (gpointer adaptor,
                                             GstAdaptorCallbackInfo *cbinfo);

/**************************** CommonFunctions ****************************/
/**
 * GstProtocolNewFunction:
 * @role: publisher, subscriber or other role of client.
 *
 * Function prototype for allocating structure of protocol instance.
 *
 * Returns: the pointer point to the structure of protocol instance.
 */
typedef gpointer (*GstProtocolNewFunction) (const gchar *role);

/**
 * GstProtocolFreeFunction:
 * @prop: structure of protocol instance containing the properties.
 *
 * Function prototype for freeing structure of protocol instance.
 *
 * Returns: NONE.
 */
typedef void (*GstProtocolFreeFunction) (gpointer *prop);

/**
 * GstProtocolConfigFunction:
 * @prop: structure of protocol instance containing the properties.
 * @path: the absolute path of config file to config properties, could be NULL.
 *
 * Function prototype for protocol instance to config.
 *
 * Returns: TRUE if config succesfully.
 */
typedef gboolean (*GstProtocolConfigFunction) (gpointer *prop, gchar *path);

/**
 * GstProtocolConnectFunction:
 * @prop: structure of protocol instance containing the properties.
 * @host: the IP of server to connect to.
 * @port: the port of server to connect to.
 *
 * Function prototype for protocol instance to connect.
 *
 * Returns: TRUE if connect succesfully.
 */
typedef gboolean (*GstProtocolConnectFunction) (gpointer *prop, gchar *host,
                                                gint port);

/**
 * GstProtocolDisconnectFunction:
 * @prop: structure of protocol instance containing the properties.
 *
 * Function prototype for protocol instance to disconnect.
 *
 * Returns: TRUE if disconnect succesfully.
 */
typedef gboolean (*GstProtocolDisconnectFunction) (gpointer *prop);

/**
 * GstProtocolPublishFunction:
 * @prop: structure of protocol instance containing the properties.
 * @topic: the topic related to the message.
 * @message: the message to publish.
 *
 * Function prototype for protocol instance to publish.
 *
 * Returns: TRUE if publish succesfully.
 */
typedef gboolean (*GstProtocolPublishFunction) (gpointer *prop, gchar *topic,
                                                gpointer message);

/**
 * GstProtocolSubscribeFunction:
 * @prop: structure of protocol instance containing the properties.
 * @topic: the topic to subscribe.
 * @callback: callback to bring data back to adaptor.
 * @adaptor: message distribution protocol adaptor, used for callback.
 *
 * Function prototype for protocol instance to subscribe.
 *
 * Returns: TRUE if subscribe succesfully.
 */
typedef gboolean (*GstProtocolSubscribeFunction) (
    gpointer *prop, gchar *topic, GstAdaptorSubscribeCallback callback,
    gpointer adaptor);

/**
 * GstProtocolCommonFunc:
 * @new: pointer point to the new funtion of underlying protocol.
 * @free: pointer point to the free funtion of underlying protocol.
 * @config: pointer point to the config funtion of underlying protocol.
 * @connect: pointer point to the connect funtion of underlying protocol.
 * @disconnect: pointer point to the disconnect funtion of underlying protocol.
 * @publish: pointer point to the publish funtion of underlying protocol.
 * @subscribe: pointer point to the subscribe funtion of underlying protocol.
 *
 * Structure to save common function pointers of underlying protocol.
 */
struct _GstProtocolCommonFunc {
  GstProtocolNewFunction        new;
  GstProtocolFreeFunction       free;
  GstProtocolConfigFunction     config;
  GstProtocolConnectFunction    connect;
  GstProtocolDisconnectFunction disconnect;
  GstProtocolPublishFunction    publish;
  GstProtocolSubscribeFunction  subscribe;
};

/************************ Structure for callback ************************/
/**
 * GstCallbackInfoType:
 * @GST_CALLBACK_INFO_MESSAGE: message type.
 * @GST_CALLBACK_INFO_EVENT: event type.
 *
 * Type of data stored in callback.
 */
typedef enum {
  GST_CALLBACK_INFO_MESSAGE,
  GST_CALLBACK_INFO_EVENT,
} GstCallbackInfoType;

/**
 * GstEventInfoType:
 * @GST_EVENT_INFO_CONNECT: connect event.
 * @GST_EVENT_INFO_DISCONNECT: disconnect event.
 * @GST_EVENT_INFO_PUBLISH: publish event.
 * @GST_EVENT_INFO_SUBSCRIBE: subscribe event.
 *
 * Type of events stored in callback.
 */
typedef enum {
  GST_EVENT_INFO_CONNECT,
  GST_EVENT_INFO_DISCONNECT,
  GST_EVENT_INFO_PUBLISH,
  GST_EVENT_INFO_SUBSCRIBE,
} GstEventInfoType;

/**
 * GstEventInfo:
 * @type: type of event.
 * @event: content of event.
 *
 * Event information with type and content.
 */
struct _GstEventInfo {
  GstEventInfoType  type;
  gpointer          event;
};

/**
 * GstMessageInfo:
 * @topic: topic of message.
 * @message: data of message.
 *
 * Message information with topic and data.
 */
struct _GstMessageInfo {
  gpointer  topic;
  gpointer  message;
};

/**
 * GstAdaptorCallbackInfo:
 * @cbtype: the type of callback, event or message.
 * @info: information of event (evtinfo) or message (msginfo).
 *
 * Structure of callback information containing event or message.
 */
struct _GstAdaptorCallbackInfo {
  GstCallbackInfoType  cbtype;
  union {
    GstEventInfo    evtinfo;
    GstMessageInfo  msginfo;
  } info;
};