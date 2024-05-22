/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtiredissink.h"


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


#define DEFAULT_PROP_HOSTNAME "127.0.0.1"
#define DEFAULT_PROP_PORT 6379
#define DEFAULT_PROP_USERNAME ""
#define DEFAULT_PROP_PASSWORD ""
#define DEFAULT_PROP_CHANNEL "temp,channel=\"default\",detection=\"detection\",classification=\"classification\";"

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
gst_structure_to_json_append (GstStructure *structure, GString *json, gboolean is_name_flag);


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

  return TRUE;
}

static gboolean
gst_redis_sink_publish (GstBaseSink * bsink, GString *json, gchar *channel)
{
  redisReply *reply = NULL;
  GstRedisSink *sink = GST_REDIS_SINK (bsink);

  g_return_val_if_fail (json != NULL, TRUE);

  GST_DEBUG_OBJECT (sink, "REDIS: PUBLISH %s %s", channel, json->str);

  reply = redisCommand (sink->redis, "PUBLISH %s %b",
      channel, json->str, json->len);
  freeReplyObject(reply);
  return TRUE;
}

static void
destroy_redissink (void *redis)
{
  if (redis) redisFree(redis);
}

static gboolean
gst_array_to_json_append (const GValue * value, const gchar * name, GString *json)
{
  guint idx;
  guint size = gst_value_array_get_size (value);

  if (name != NULL) {
    g_string_append_printf (json, "\"%s\":[", name);
  } else {
    g_string_append_printf (json, "[");
  }

  for (idx = 0; idx < size; idx++) {
    const GValue * val = gst_value_array_get_value (value, idx);

    if (G_VALUE_TYPE (val) == G_TYPE_STRING) {
      g_string_append_printf (json, "\"%s\"", g_value_get_string (val));
    } else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE) {
      GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (val));
      gst_structure_to_json_append (structure, json, FALSE);
    } else if (G_VALUE_TYPE (val) == GST_TYPE_ARRAY) {
      gst_array_to_json_append (val, NULL, json);
    } else {
      g_string_append_printf (json, "%s", gst_value_serialize (val));
    }

    if (idx < size - 1) g_string_append_printf (json, ",");
  }

  g_string_append_printf (json, "]");
  return TRUE;
}

static gboolean
gst_structure_json_serialize (GQuark field, const GValue * value, gpointer userdata)
{
  GstJsonString * json = (GstJsonString *) userdata;
  gchar *name = g_quark_to_string (field);

  g_return_val_if_fail (json != NULL, FALSE);

  if (!json->is_first) {
    g_string_append_printf (json->str, ",");
  }
  json->is_first = FALSE;

  if (G_VALUE_TYPE (value)  == GST_TYPE_ARRAY) {
    gst_array_to_json_append (value, name, json->str);
  } else if (G_VALUE_TYPE (value)  == G_TYPE_STRING) {
    g_string_append_printf (json->str, "\"%s\":\"%s\"",
        g_quark_to_string (field), g_value_get_string (value));
  } else {
    g_string_append_printf (json->str, "\"%s\":%s",
        g_quark_to_string (field), gst_value_serialize (value));
  }
  return TRUE;
}

static gboolean
gst_structure_to_json_append (GstStructure *structure, GString *json, gboolean is_name_flag)
{
  const gchar *name = NULL;
  GstJsonString json_string;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (json != NULL, FALSE);

  name = gst_structure_get_name (structure);

  if (is_name_flag) {
    g_string_append_printf (json, "\"%s\":{", name);
  } else {
    g_string_append_printf (json, "{\"name\":\"%s\",", name);
  }

  json_string.str = json;
  json_string.is_first = TRUE;

  gst_structure_foreach (structure, gst_structure_json_serialize, &json_string);
  g_string_append (json, "}");

  return TRUE;
}

static gboolean
gst_list_to_json_append (GValue *list, GString *json)
{
  GstStructure *structure = NULL;
  const gchar *name = NULL;
  guint idx;

  g_return_val_if_fail (list != NULL, FALSE);
  g_return_val_if_fail (json != NULL, FALSE);
  g_return_val_if_fail (gst_value_list_get_size (list) != 0, TRUE);

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, 0)));

  name = gst_structure_get_name (structure);

  g_string_append_printf (json, "\"%s\":[", name);

  for (idx = 0; idx < gst_value_list_get_size (list); idx++) {
      structure = GST_STRUCTURE (
          g_value_get_boxed (gst_value_list_get_value (list, idx)));

      if (structure == NULL) {
        GST_WARNING ("Structure is NULL!");
        continue;
      }

      if (idx > 0) g_string_append (json, ",");

      gst_structure_to_json_append (structure, json, FALSE);
  }

  g_string_append(json, "]");

  return TRUE;
}

static GstFlowReturn
gst_redis_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstMapInfo buffer_info;
  GValue object_detection = G_VALUE_INIT;
  GValue image_classification = G_VALUE_INIT;
  GValue pose_estimation = G_VALUE_INIT;
  GstStructure *structure = NULL;
  GValue value = G_VALUE_INIT;
  GValue list = G_VALUE_INIT;
  guint idx = 0;
  const gchar *name = NULL;
  GString *object_detection_json;
  GString *image_classification_json;
  GString *pose_estimation_json;
  GstRedisSink *sink;

  object_detection_json = g_string_new (NULL);
  image_classification_json = g_string_new (NULL);
  pose_estimation_json = g_string_new (NULL);

  sink = GST_REDIS_SINK (bsink);

  g_value_init (&object_detection, GST_TYPE_LIST);
  g_value_init (&image_classification, GST_TYPE_LIST);
  g_value_init (&pose_estimation, GST_TYPE_LIST);
  g_value_init (&value, GST_TYPE_STRUCTURE);
  g_value_init (&list, GST_TYPE_LIST);

  if (sink->redis == NULL || sink->redis->err)
    sink->redis = redisConnect(sink->host, sink->port);

  if (sink->redis == NULL || sink->redis->err) {
    GST_WARNING_OBJECT (sink, "Not connected to REDIS service!");
    return GST_FLOW_OK;
  }

  gst_buffer_ref (buffer);

  if (!gst_buffer_map (buffer, &buffer_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (sink, "Unable to map buffer!");
    return GST_FLOW_ERROR;
  }

  if (sink->data_type == GST_DATA_TYPE_TEXT) {
    if (buffer_info.data == NULL) {
      GST_DEBUG ("Null data");
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    }

    if (!gst_value_deserialize (&list, buffer_info.data)) {
      GST_WARNING ("Failed to deserialize");
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    }

    for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
      structure = GST_STRUCTURE (
          g_value_get_boxed (gst_value_list_get_value (&list, idx)));
      if (structure == NULL) {
        GST_WARNING ("Structure is NULL!");
        continue;
      }

      gst_structure_remove_field (structure, "sequence-id");
      gst_structure_remove_field (structure, "batch-index");
      g_value_take_boxed (&value, structure);

      name = gst_structure_get_name (structure);
      if (!strcmp(name, "ObjectDetection")) {
        gst_value_list_append_value (&object_detection, &value);
      } else if (!strcmp (name, "ImageClassification")) {
        gst_value_list_append_value (&image_classification, &value);
      } else if (!strcmp (name, "PoseEstimation")) {
        gst_value_list_append_value (&pose_estimation, &value);
      }
      g_value_reset (&value);
    }

    if (gst_value_list_get_size (&object_detection) > 0) {
      g_string_append (object_detection_json, "{");
      gst_list_to_json_append (&object_detection, object_detection_json);
      g_string_append (object_detection_json, "}");
    }

    if (gst_value_list_get_size (&image_classification) > 0) {
      g_string_append (image_classification_json, "{");
      gst_list_to_json_append (&image_classification, image_classification_json);
      g_string_append (image_classification_json, "}");
    }

    if (gst_value_list_get_size (&pose_estimation) > 0) {
      g_string_append (pose_estimation_json, "{");
      gst_list_to_json_append (&pose_estimation, pose_estimation_json);
      g_string_append (pose_estimation_json, "}");
    }

    GST_DEBUG_OBJECT (sink, "%s",  buffer_info.data);

  } else if (sink->data_type == GST_DATA_TYPE_VIDEO) {
    GST_DEBUG_OBJECT (sink, "METADATA");
  } else {

  }

  if (object_detection_json->len && sink->detection_channel) {
    gst_redis_sink_publish (bsink,
        object_detection_json, sink->detection_channel);
  }

  if (image_classification_json->len && sink->image_classification_channel) {
    gst_redis_sink_publish (bsink,
        image_classification_json, sink->image_classification_channel);
  }

  if (pose_estimation_json->len && sink->pose_estimation_channel) {
    gst_redis_sink_publish (bsink,
        pose_estimation_json, sink->pose_estimation_channel);
  }

  gst_buffer_unref (buffer);

  g_string_free (object_detection_json, TRUE);
  g_string_free (image_classification_json, TRUE);
  g_string_free (pose_estimation_json, TRUE);

  return GST_FLOW_OK;
}

static gboolean
gst_redis_sink_start (GstBaseSink * basesink)
{
  GstRedisSink *sink = GST_REDIS_SINK (basesink);

  sink->redis = redisConnect(sink->host, sink->port);

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
