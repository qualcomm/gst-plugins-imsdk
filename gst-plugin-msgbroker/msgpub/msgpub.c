/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "msgpub.h"

GST_DEBUG_CATEGORY_STATIC (msgpub_debug);
#define GST_CAT_DEFAULT msgpub_debug

#define gst_msg_pub_parent_class parent_class
G_DEFINE_TYPE (GstMsgPub, gst_msg_pub, GST_TYPE_BASE_SINK);

#define MSG_PUB_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
    : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
    : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
    : (state <= GST_STATE_NULL))))

#define DEFAULT_MSG_PUB_PROTOCOL    NULL
#define DEFAULT_MSG_PUB_HOST        NULL
#define DEFAULT_MAG_PUB_PORT        1883
#define DEFAULT_MSG_PUB_TOPIC       NULL
#define DEFAULT_MSG_PUB_MESSAGE_CMD NULL
#define DEFAULT_MSG_PUB_CONFIG      NULL
#define DEFAULT_MSG_PUB_JSON        FALSE

enum
{
  PROP_0,
  PROP_PROTOCOL,
  PROP_HOST,
  PROP_PORT,
  PROP_TOPIC,
  PROP_MESSAGE_CMD,
  PROP_CONFIG,
  PROP_JSON
};

enum
{
  SIGNAL_ADD_PUBLISH,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GstStaticPadTemplate msg_pub_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);

static void extract_json_gstring_from_gvalue (GString *js, const GValue *val);
static gboolean extract_json_gstring_from_gstructure (GQuark field,
    const GValue * val, gpointer userdata);

static gboolean
publisher_add_publish (GstMsgPub *pub, gchar *atopic, gchar *amessage)
{
  g_return_val_if_fail (pub != NULL, FALSE);
  g_return_val_if_fail (atopic != NULL, FALSE);
  g_return_val_if_fail (amessage != NULL, FALSE);

  GST_DEBUG_OBJECT (pub, "Sending additional topic and message");

  if (!gst_msg_protocol_publish (pub->adaptor, atopic, (gpointer)amessage)) {
    GST_ERROR_OBJECT (pub, "Sending additional topic and message");
    return FALSE;
  }

  GST_DEBUG_OBJECT (pub, "Sent additional topic and message");
  return TRUE;
}

static void
gst_msg_pub_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
  GstMsgPub *pub = GST_MSG_PUB (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (pub);

  if (!MSG_PUB_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (pub);
  switch (property_id) {
    case PROP_PROTOCOL:
      pub->protocol = g_strdup (g_value_get_string (value));
      break;
    case PROP_HOST:
      pub->host = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      pub->port = g_value_get_int (value);
      break;
    case PROP_TOPIC:
      pub->topic = g_strdup (g_value_get_string (value));
      break;
    case PROP_MESSAGE_CMD:
      pub->message_cmd = g_strdup (g_value_get_string (value));
      break;
    case PROP_CONFIG:
      pub->config = g_strdup (g_value_get_string (value));
      break;
    case PROP_JSON:
      pub->json = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (pub);
}

static void
gst_msg_pub_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
  GstMsgPub *pub = GST_MSG_PUB (object);

  GST_OBJECT_LOCK (pub);
  switch (property_id) {
    case PROP_PROTOCOL:
      g_value_set_string (value, pub->protocol);
      break;
    case PROP_HOST:
      g_value_set_string (value, pub->host);
      break;
    case PROP_PORT:
      g_value_set_int (value, pub->port);
      break;
    case PROP_TOPIC:
      g_value_set_string (value, pub->topic);
      break;
    case PROP_MESSAGE_CMD:
      g_value_set_string (value, pub->message_cmd);
      break;
    case PROP_CONFIG:
      g_value_set_string (value, pub->config);
      break;
    case PROP_JSON:
      g_value_set_boolean (value, pub->json);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (pub);
}

static void
gst_msg_pub_finalize (GObject *object)
{
  GstMsgPub *pub = GST_MSG_PUB (object);

  g_free (pub->protocol);
  g_free (pub->host);
  g_free (pub->topic);
  g_free (pub->message_cmd);
  g_free (pub->config);

  if (pub->adaptor != NULL)
    gst_msg_protocol_free (pub->adaptor);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_msg_pub_start (GstBaseSink *sink)
{
  GstMsgPub *pub = GST_MSG_PUB (sink);

  g_return_val_if_fail (pub->protocol != NULL, FALSE);
  g_return_val_if_fail (pub->host != NULL, FALSE);
  g_return_val_if_fail (pub->port >= 0, FALSE);

  pub->adaptor = gst_msg_protocol_new (pub->protocol, "pub");
  if (!pub->adaptor) {
    GST_ERROR_OBJECT (pub, "Failed to initialize protocol adaptor.");
    return FALSE;
  }

  if (!gst_msg_protocol_config (pub->adaptor, pub->config))
    goto start_failed;

  if (!gst_msg_protocol_connect (pub->adaptor, pub->host, pub->port))
    goto start_failed;

  return TRUE;

start_failed:
  gst_msg_protocol_free (pub->adaptor);
  pub->adaptor = NULL;
  return FALSE;
}

static gboolean
extract_json_gstring_from_gstructure (GQuark field, const GValue * val,
    gpointer userdata)
{
  GString *json_data = (GString *)userdata;
  const GValue *value = val;

  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      g_string_append_printf (json_data, "\"%s\":\"%s\"",
        g_quark_to_string (field), g_value_get_string (value));
      break;
    default:
      if (G_VALUE_TYPE (value) == GST_TYPE_ARRAY) {
        guint array_size = gst_value_array_get_size (value);

        g_string_append_printf (json_data, "\"%s\":[", g_quark_to_string (field));

        for (guint index = 0; index < array_size; ++index) {
          const GValue * val = gst_value_array_get_value (value, index);

          extract_json_gstring_from_gvalue (json_data, val);

          if (index < array_size - 1)
            g_string_append (json_data, ",");
        }
        g_string_append (json_data, "]");
      } else
        g_string_append_printf (json_data, "\"%s\":%s",
          g_quark_to_string (field), gst_value_serialize (value));
      break;
  }

  g_string_append (json_data, ",");

  return TRUE;
}

static void
extract_json_gstring_from_gvalue (GString *js, const GValue *val)
{
  GString *json = js;
  const GValue *value = val;

  if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *val_structure = NULL;
    const gchar *name = NULL;
    guint list_size = gst_value_list_get_size (value);
    gchar square_recorder[list_size];

    for (guint i = 0; i < list_size; ++i)
      square_recorder[i] = '0';

    // Square bracket only used in case list_size >= 2
    if (list_size > 1) {
      val_structure = g_value_get_boxed (gst_value_list_get_value (value, 0));
      name = gst_structure_get_name (GST_STRUCTURE (val_structure));
      square_recorder[0] = '[';
    }

    // Record the index need to add '[' and ']'
    for (guint index = 1; index < list_size; ++index) {
      val_structure = g_value_get_boxed (gst_value_list_get_value (value, index));
      if (name == gst_structure_get_name (GST_STRUCTURE (val_structure))) {
        square_recorder[index] = ']';

        if (square_recorder[index - 1] == '[')
          continue;
        else if (square_recorder[index - 1] == ']') {
          square_recorder [index - 1] = '\0';
        }
      } else {
        square_recorder[index] = '[';
        if (square_recorder[index - 1] == '[')
          square_recorder[index - 1] = '0';
      }

      if (square_recorder[list_size - 1] == '[')
        square_recorder[list_size - 1] = '0';

      name = gst_structure_get_name (GST_STRUCTURE (val_structure));
    }

    // '{' for list
    g_string_append (json, "{");

    for (guint index = 0; index < list_size; ++index) {
      const GValue *v = NULL;

      v = gst_value_list_get_value (value, index);
      if (v == NULL) {
        GST_WARNING ("GValue in value_list is NULL!");
        continue;
      }

      if (square_recorder[index] == '[')
        // GList contains GValue of GstStructure is hard coded by upstream
        g_string_append_printf (json, "\"%s\":[",
            gst_structure_get_name (GST_STRUCTURE (g_value_get_boxed (v))));
      else if (square_recorder[index] == '0')
        g_string_append_printf (json, "\"%s\":",
            gst_structure_get_name (GST_STRUCTURE (g_value_get_boxed (v))));

      // Invoke for each gvalue in list
      extract_json_gstring_from_gvalue (json, v);

      if (square_recorder[index] == ']')
        g_string_append (json, "]");

      if (index < list_size - 1)
        g_string_append (json, ",");
    }

    // '}' for list
    g_string_append(json, "}");
  } else if (G_VALUE_TYPE (value) == GST_TYPE_STRUCTURE) {
    GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (val));

    g_string_append (json, "{");
    gst_structure_foreach (structure, extract_json_gstring_from_gstructure, json);
    json = g_string_truncate (json, json->len - 1);
    g_string_append(json, "}");
  } else if (G_VALUE_TYPE (value) == G_TYPE_STRING) {
      g_string_append_printf (json, "\"%s\"", g_value_get_string (value));
  } else
      g_string_append_printf (json, "%s", gst_value_serialize (value));

  return;
}

// Convert data to with json format
static GString*
convert_to_json (gchar *topic, gchar *data)
{
  GString *json_data = NULL;
  GString *result = NULL;
  GValue value = G_VALUE_INIT;

  g_return_val_if_fail (topic != NULL, NULL);
  g_return_val_if_fail (data != NULL, NULL);

  g_value_init (&value, GST_TYPE_LIST);

  if (!gst_value_deserialize (&value, data)) {
    GST_WARNING ("Failed to deserialize");
    g_value_unset (&value);
    return NULL;
  }

  json_data = g_string_new (NULL);
  extract_json_gstring_from_gvalue (json_data, &value);

  result = g_string_new (NULL);
  g_string_printf (result, "{\"%s\":\"%s\",\"%s\":%s}\n",
      "Topic", topic, "Message", json_data->str);

  if (!json_data)
    g_string_free (json_data, TRUE);

  return result;
}

static GstFlowReturn
gst_msg_pub_render (GstBaseSink *sink, GstBuffer *buffer)
{
  GstMsgPub *pub = GST_MSG_PUB (sink);
  GstMemory *mem = NULL;
  GString *message = NULL;
  GstMapInfo info;

  // Send message passed from commandline
  if (pub->message_cmd) {
    if (pub->json) {
      GstStructure *struc_cmd = NULL;
      GValue val_struc = G_VALUE_INIT;
      GValue val_cmd = G_VALUE_INIT;

      // Message in commandline follow the same pattern in post process plugins
      GST_DEBUG_OBJECT (pub, "Construct GValue to convert to json.");

      struc_cmd = gst_structure_new ("MessageInCommandline",
          "contents", G_TYPE_STRING, pub->message_cmd, NULL);
      g_value_init (&val_cmd, GST_TYPE_LIST);
      g_value_init (&val_struc, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&val_struc, struc_cmd);
      gst_value_list_append_value (&val_cmd, &val_struc);
      g_value_unset (&val_struc);

      message = convert_to_json (pub->topic, gst_value_serialize (&val_cmd));
      g_value_unset (&val_cmd);

      if (!message) {
        GST_WARNING_OBJECT (pub,
            "Handle contents in commandline as normal string.");
        message = g_string_new (pub->message_cmd);
      }
    } else
      message = g_string_new (pub->message_cmd);

    if (!gst_msg_protocol_publish (pub->adaptor, pub->topic,
        (gpointer)(message->str)))
      GST_ERROR_OBJECT (pub, "Failed to publish message in commandline.");
    else {
      g_free (pub->message_cmd);
      pub->message_cmd = NULL;
    }

    if (!message)
      g_string_free (message, TRUE);
  }

  if (!gst_buffer_get_size (buffer)) {
    // GstBuffer has no memory block, quit
    GST_WARNING_OBJECT (pub, "GstBuffer has no memory block, return eos.");
    return GST_FLOW_EOS;
  }

  // Send message stored in gstbuffer
  mem = gst_buffer_get_memory (buffer, 0);
  if (!gst_memory_map (mem, &info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (pub, "Failed to map memory.");
    gst_memory_unref (mem);
    return GST_FLOW_ERROR;
  }

  if (pub->json) {
    message = convert_to_json (pub->topic, (gchar *)info.data);

    // If fail, fill mapped data to gvalue
    if (!message) {
      GstStructure *struc_buf = NULL;
      GValue val_struc = G_VALUE_INIT;
      GValue val_buf = G_VALUE_INIT;

      GST_DEBUG_OBJECT (pub, "Construct GValue to convert to json.");

      struc_buf = gst_structure_new ("MessageInGstBuffer",
          "contents", G_TYPE_STRING, (gchar *)info.data, NULL);
      g_value_init (&val_buf, GST_TYPE_LIST);
      g_value_init (&val_struc, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&val_struc, struc_buf);
      gst_value_list_append_value (&val_buf, &val_struc);
      g_value_unset (&val_struc);

      message = convert_to_json (pub->topic, gst_value_serialize (&val_buf));
      g_value_unset (&val_buf);

      if (!message) {
        GST_WARNING_OBJECT (pub,
            "Handle contents in gstbuffer as normal string.");
        message = g_string_new ((gchar *)info.data);
      }
    }
  } else
    message = g_string_new ((gchar *)info.data);

  if (message && !gst_msg_protocol_publish (pub->adaptor, pub->topic,
      (gpointer)(message->str))) {
    GST_ERROR_OBJECT (pub, "Failed to publish messages.");
    gst_memory_unmap (mem, &info);
    gst_memory_unref (mem);
    return GST_FLOW_ERROR;
  }

  gst_memory_unmap (mem, &info);
  gst_memory_unref (mem);

  if (!message)
    g_string_free (message, TRUE);

  return GST_FLOW_OK;
}

static gboolean
gst_msg_pub_stop (GstBaseSink *sink)
{
  GstMsgPub *pub = GST_MSG_PUB (sink);

  if (!gst_msg_protocol_disconnect (pub->adaptor)) {
    GST_ERROR_OBJECT (pub, "Failed to disconnect.");
    return FALSE;
  }

  return TRUE;
}

static void
gst_msg_pub_class_init (GstMsgPubClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_msg_pub_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_msg_pub_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_msg_pub_finalize);

  g_object_class_install_property (gobject, PROP_PROTOCOL,
      g_param_spec_string ("protocol", "protocol",
          "Message protocol (mqtt .etc).",
          DEFAULT_MSG_PUB_PROTOCOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_HOST,
      g_param_spec_string ("host", "host",
          "The IP address to send packets to.",
          DEFAULT_MSG_PUB_HOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port to send packets to.",
          0, G_MAXINT, DEFAULT_MAG_PUB_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_TOPIC,
      g_param_spec_string ("topic", "topic",
          "The topic to publish to.",
          DEFAULT_MSG_PUB_TOPIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_MESSAGE_CMD,
      g_param_spec_string ("message", "message from commandline",
          "The message from commandline to publish.",
          DEFAULT_MSG_PUB_MESSAGE_CMD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CONFIG,
      g_param_spec_string ("config", "config file",
          "The absolute path of protocol config file.",
          DEFAULT_MSG_PUB_CONFIG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_JSON,
      g_param_spec_boolean ("json", "json format",
          "Send message in json format", DEFAULT_MSG_PUB_JSON,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  signals[SIGNAL_ADD_PUBLISH] =
      g_signal_new_class_handler ("add-publish", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK (publisher_add_publish),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 2, G_TYPE_STRING, G_TYPE_STRING);

  gst_element_class_set_static_metadata (element_class,
      "Message Publisher Client", "Sink/Network",
      "Send message on topic via plugin based on specified protocol", "QTI");

  gst_element_class_add_static_pad_template (element_class, &msg_pub_template);

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_msg_pub_start);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_msg_pub_render);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_msg_pub_stop);

  GST_DEBUG_CATEGORY_INIT (msgpub_debug, "qtimsgpub", 0,
      "Message Publisher Client");
}

static void
gst_msg_pub_init (GstMsgPub *pub)
{
  GST_DEBUG_OBJECT (pub, "Init instance.");
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qtimsgpub",
      GST_RANK_PRIMARY, GST_TYPE_MSG_PUB);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimsgpub,
    "Message Publisher Client Library",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)