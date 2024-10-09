/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtiredissink.h"
#include <gst/utils/common-utils.h>

#define gst_redis_sink_parent_class parent_class
G_DEFINE_TYPE (GstRedisSink, gst_redis_sink, GST_TYPE_BASE_SINK);

GST_DEBUG_CATEGORY_STATIC (gst_redis_sink_debug);
#define GST_CAT_DEFAULT gst_redis_sink_debug


#define GST_REDIS_SINK_CAPS \
    "text/x-raw"

#define DEFAULT_PROP_HOSTNAME "127.0.0.1"
#define DEFAULT_PROP_PORT 6379
#define DEFAULT_PROP_USERNAME NULL
#define DEFAULT_PROP_PASSWORD NULL
#define DEFAULT_PROP_CHANNEL NULL

enum
{
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_USERNAME,
  PROP_PASSWORD,
  PROP_CHANNEL
};

static GstStaticPadTemplate redis_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_REDIS_SINK_CAPS));

static gboolean
gst_redis_sink_publish (GstBaseSink * bsink, const gchar *string, gchar *channel)
{
  redisReply *reply = NULL;
  GstRedisSink *sink = GST_REDIS_SINK (bsink);

  g_return_val_if_fail (string != NULL, FALSE);
  g_return_val_if_fail (channel != NULL, FALSE);

  GST_DEBUG_OBJECT (sink, "REDIS: PUBLISH %s %s", channel, string);

  reply = redisCommand (sink->redis, "PUBLISH %s %b",
      channel, string, strlen (string));
  freeReplyObject(reply);
  return TRUE;
}

static GstFlowReturn
gst_redis_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstMapInfo bufmap = { 0, };
  GstRedisSink *sink;

  sink = GST_REDIS_SINK (bsink);

  if (sink->redis == NULL)
    sink->redis = redisConnect (sink->host, sink->port);

  if (sink->redis == NULL || sink->redis->err) {
    GST_WARNING_OBJECT (sink, "Not connected to REDIS service!");
    if (sink->redis) {
      redisFree (sink->redis);
      sink->redis = NULL;
    }
    return GST_FLOW_OK;
  }

  if (!gst_buffer_map (buffer, &bufmap, GST_MAP_READ)) {
    GST_ERROR ("Unable to map buffer!");
    return GST_FLOW_ERROR;
  }

  gst_buffer_ref (buffer);

  if (sink->channel) {
    gst_redis_sink_publish (bsink,
        (gchar*) bufmap.data, sink->channel);
  }

  gst_buffer_unmap (buffer, &bufmap);
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static gboolean
gst_redis_sink_start (GstBaseSink * basesink)
{
  GstRedisSink *sink = GST_REDIS_SINK (basesink);

  sink->redis = redisConnect (sink->host, sink->port);

  if (sink->redis == NULL || sink->redis->err)
    GST_INFO_OBJECT (sink, "Unable to REDIS connect");

  return TRUE;
}

static gboolean
gst_redis_sink_stop (GstBaseSink * basesink)
{
  GstRedisSink *sink = GST_REDIS_SINK (basesink);

  GST_INFO_OBJECT (sink, "Stop");

  if (sink->redis)
    redisFree(sink->redis);

  sink->redis = NULL;

  return TRUE;
}

static gboolean
gst_redis_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstRedisSink *sink = GST_REDIS_SINK (bsink);

  GST_INFO_OBJECT (sink, "Input caps: %" GST_PTR_FORMAT, caps);

  return TRUE;
}

static void
gst_redis_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRedisSink *sink = GST_REDIS_SINK (object);
  const gchar *propname = g_param_spec_get_name (pspec);

  GstState state = GST_STATE (sink);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (sink);

  switch (prop_id) {
    case PROP_HOST:
      sink->host = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      sink->port = g_value_get_uint (value);
      break;
    case PROP_USERNAME:
      sink->username = g_strdup (g_value_get_string (value));
      break;
    case PROP_PASSWORD:
      sink->password = g_strdup (g_value_get_string (value));
      break;
    case PROP_CHANNEL:
      sink->channel = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_redis_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRedisSink *sink = GST_REDIS_SINK (object);

  GST_OBJECT_LOCK (sink);
  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, sink->host);
      break;
    case PROP_PORT:
      g_value_set_uint (value, sink->port);
      break;
    case PROP_USERNAME:
      g_value_set_string (value, sink->username);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, sink->password);
      break;
    case PROP_CHANNEL:
      g_value_set_string (value, sink->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_redis_sink_dispose (GObject * obj)
{
  GstRedisSink *sink = GST_REDIS_SINK (obj);

  g_clear_pointer (&(sink->host), g_free);
  g_clear_pointer (&(sink->password), g_free);
  g_clear_pointer (&(sink->username), g_free);
  g_clear_pointer (&(sink->channel), g_free);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_redis_sink_class_init (GstRedisSinkClass * klass)
{
  GObjectClass *gobject;
  GstElementClass *gstelement;
  GstBaseSinkClass *gstbasesink;

  gobject = G_OBJECT_CLASS (klass);
  gstelement = GST_ELEMENT_CLASS (klass);
  gstbasesink = GST_BASE_SINK_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_redis_sink_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_redis_sink_get_property);
  gobject->dispose      = GST_DEBUG_FUNCPTR (gst_redis_sink_dispose);

  g_object_class_install_property (gobject, PROP_HOST,
      g_param_spec_string ("host", "Redis service hostname",
          "Hostname of REDIS service", DEFAULT_PROP_HOSTNAME,
           G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
           GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject, PROP_PORT,
      g_param_spec_uint ("port", "Redis service port",
          "Redis service TCP port",
          0, G_MAXUINT, DEFAULT_PROP_PORT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject, PROP_USERNAME,
      g_param_spec_string ("username", "Redis hostname",
          "Hostname of REDIS service", DEFAULT_PROP_USERNAME,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject, PROP_PASSWORD,
      g_param_spec_string ("password", "Redis hostname",
          "Hostname of REDIS service",
          DEFAULT_PROP_PASSWORD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject, PROP_CHANNEL,
      g_param_spec_string ("channel", "Redis channels definition",
          "Redis channels definition", DEFAULT_PROP_CHANNEL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (gstelement,
      "QTI Redis Sink Element", "Redis Sink Element",
      "This plugin send ML data to Redis service", "QTI");

  gst_element_class_add_static_pad_template (gstelement, &redis_sink_template);

  gstbasesink->render = GST_DEBUG_FUNCPTR (gst_redis_sink_render);
  gstbasesink->start = GST_DEBUG_FUNCPTR (gst_redis_sink_start);
  gstbasesink->stop = GST_DEBUG_FUNCPTR (gst_redis_sink_stop);
  gstbasesink->set_caps = GST_DEBUG_FUNCPTR (gst_redis_sink_set_caps);
}

static void
gst_redis_sink_init (GstRedisSink * sink)
{
  sink->redis = NULL;
  sink->host = NULL;
  sink->port = 0;
  sink->password = NULL;
  sink->username = NULL;
  sink->channel = NULL;

  GST_DEBUG_CATEGORY_INIT (gst_redis_sink_debug, "qtiredissink", 0,
    "qtiredissink object");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiredissink", GST_RANK_PRIMARY,
      GST_TYPE_REDIS_SINK);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiredissink,
    "Send ML data to Redis service",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
