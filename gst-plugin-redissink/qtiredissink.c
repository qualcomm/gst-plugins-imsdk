/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtiredissink.h"


#define GST_TYPE_REDIS_MODULES (gst_ml_modules_get_type())

#define REDIS_SINK_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

#define gst_redis_sink_parent_class parent_class
G_DEFINE_TYPE (GstRedisSink, gst_redis_sink, GST_TYPE_BASE_SINK);

GST_DEBUG_CATEGORY_STATIC (gst_redis_sink_debug);
#define GST_CAT_DEFAULT gst_redis_sink_debug


#define GST_REDIS_SINK_CAPS \
    "text/x-raw;" \
    "video/x-raw(ANY)"

#define DEFAULT_PROP_MODULE 0
#define DEFAULT_PROP_HOSTNAME "127.0.0.1"
#define DEFAULT_PROP_PORT 6379
#define DEFAULT_PROP_USERNAME ""
#define DEFAULT_PROP_PASSWORD ""
#define DEFAULT_PROP_CHANNEL ""

enum
{
  PROP_0,
  PROP_MODULE,
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


static void
gst_redis_sink_set_channels (GstRedisSink *sink, const GValue * value)
{
  GValue val = G_VALUE_INIT;
  const GValue *pval = NULL;
  GstStructure *structure = NULL;

  g_value_init (&val, GST_TYPE_STRUCTURE);

  if (!gst_value_deserialize (&val, g_value_get_string (value)))
    return;

  structure = GST_STRUCTURE (g_value_get_boxed (&val));

  pval = gst_structure_get_value (structure, "detection");
  if (pval) {
    g_free (sink->detection_channel);
    sink->detection_channel = g_strdup (g_value_get_string (pval));
  }

  pval = gst_structure_get_value (structure, "classification");
  if (pval) {
    g_free (sink->image_classification_channel);
    sink->image_classification_channel = g_strdup (g_value_get_string (pval));
  }

  pval = gst_structure_get_value (structure, "estimation");
  if (pval) {
    g_free (sink->pose_estimation_channel);
    sink->pose_estimation_channel = g_strdup (g_value_get_string (pval));
  }

  GST_DEBUG_OBJECT (sink, "Redis detection channel = %s",
    sink->detection_channel);

  GST_DEBUG_OBJECT (sink, "Redis image classification channel = %s",
    sink->image_classification_channel);

  GST_DEBUG_OBJECT (sink, "Redis pose estimation channel = %s",
    sink->pose_estimation_channel);

  return;
}

static gboolean
gst_redis_sink_publish (GstBaseSink * bsink, const gchar *string, gchar *channel)
{
  redisReply *reply = NULL;
  GstRedisSink *sink = GST_REDIS_SINK (bsink);

  g_return_val_if_fail (string != NULL, TRUE);

  GST_DEBUG_OBJECT (sink, "REDIS: PUBLISH %s %s", channel, string);

  reply = redisCommand (sink->redis, "PUBLISH %s %b",
      channel, string, strlen (string));
  freeReplyObject(reply);
  return TRUE;
}

static void
destroy_redissink (void *redis)
{
  if (redis) redisFree(redis);
}

static GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_enumarate_modules ("redis-ml-parser-");
  gtype = g_enum_register_static ("GstRedisModules", variants);

  return gtype;
}

static GstFlowReturn
gst_redis_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  const gchar *output_string;
  GstRedisSink *sink;
  GstMLFrame mlframe = { 0, };

  sink = GST_REDIS_SINK (bsink);

  if (sink->redis == NULL || sink->redis->err)
    sink->redis = redisConnect (sink->host, sink->port);

  if (sink->redis == NULL || sink->redis->err) {
    GST_WARNING_OBJECT (sink, "Not connected to REDIS service!");
    return GST_FLOW_OK;
  }

  gst_buffer_ref (buffer);

  GstStructure *output = gst_structure_new_empty ("output");

  mlframe.buffer = buffer;
  gst_ml_module_execute (sink->module, &mlframe, (gpointer) output);

  if (sink->detection_channel) {
    output_string = gst_structure_has_field (output, "ObjectDetection") ?
        gst_structure_get_string (output, "ObjectDetection") : NULL;
    gst_redis_sink_publish (bsink,
        output_string, sink->detection_channel);
  }

  if (sink->image_classification_channel) {
    output_string = gst_structure_has_field (output, "ImageClassification") ?
        gst_structure_get_string (output, "ImageClassification") : NULL;
    gst_redis_sink_publish (bsink,
        output_string, sink->image_classification_channel);
  }

  if (sink->pose_estimation_channel) {
    output_string = gst_structure_has_field (output, "PoseEstimation") ?
        gst_structure_get_string (output, "PoseEstimation") : NULL;
    gst_redis_sink_publish (bsink,
        output_string, sink->pose_estimation_channel);
  }

  gst_structure_free (output);
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

  g_clear_pointer (&(sink->redis), destroy_redissink);

  return TRUE;
}

static gboolean
gst_redis_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstRedisSink *sink = GST_REDIS_SINK (bsink);

  GstStructure *structure = NULL;
  const gchar *caps_name = NULL;
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;

  GST_INFO_OBJECT (sink, "Input caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  caps_name = gst_structure_get_name (structure);

  GST_DEBUG_OBJECT (sink, "Caps: %s", caps_name);

  if (!strcmp (caps_name, "text/x-raw")) {
    sink->data_type = GST_DATA_TYPE_TEXT;
  } else if (!strcmp (caps_name, "video/x-raw(ANY)")) {
    sink->data_type = GST_DATA_TYPE_VIDEO;
  } else {
    sink->data_type = GST_DATA_TYPE_NONE;
  }

  if (DEFAULT_PROP_MODULE == sink->mdlenum) {
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND, (NULL),
        ("Module name not set, automatic module pick up not supported!"));
    return FALSE;
  }

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_REDIS_MODULES));
  evalue = g_enum_get_value (eclass, sink->mdlenum);

  gst_ml_module_free (sink->module);
  sink->module = gst_ml_module_new (evalue->value_name);

  if (!gst_ml_module_init (sink->module)) {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));
    return FALSE;
  }

  structure = gst_structure_new ("options",
      GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, caps,
      NULL);

  if (!gst_ml_module_set_opts (sink->module, structure)) {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  return TRUE;
}

static void
gst_redis_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRedisSink *sink = GST_REDIS_SINK (object);
  const gchar *propname = g_param_spec_get_name (pspec);

  GstState state = GST_STATE (sink);

  if (!REDIS_SINK_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
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
      gst_redis_sink_set_channels (sink, value);
      break;
    case PROP_MODULE:
      sink->mdlenum = g_value_get_enum (value);
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
      break;
    case PROP_MODULE:
      g_value_set_enum (value, sink->mdlenum);
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
  g_clear_pointer (&(sink->detection_channel), g_free);
  g_clear_pointer (&(sink->image_classification_channel), g_free);
  g_clear_pointer (&(sink->pose_estimation_channel), g_free);

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

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          GST_TYPE_REDIS_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  sink->data_type = GST_DATA_TYPE_NONE;
  sink->redis = NULL;
  sink->host = NULL;
  sink->port = 0;
  sink->password = NULL;
  sink->username = NULL;
  sink->detection_channel = NULL;
  sink->image_classification_channel = NULL;
  sink->pose_estimation_channel = NULL;

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
