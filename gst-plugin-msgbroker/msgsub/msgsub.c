/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "msgsub.h"

GST_DEBUG_CATEGORY_STATIC (msgsub_debug);
#define GST_CAT_DEFAULT msgsub_debug

#define parent_class gst_msg_sub_parent_class
G_DEFINE_TYPE (GstMsgSub, gst_msg_sub, GST_TYPE_BASE_SRC);

#define MSG_SUB_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
    : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
    : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
    : (state <= GST_STATE_NULL))))

#define DEFAULT_MSG_SUB_PROTOCOL  NULL
#define DEFAULT_MSG_SUB_HOST      NULL
#define DEFAULT_MAG_SUB_PORT      1883
#define DEFAULT_MSG_SUB_TOPIC     NULL
#define DEFAULT_MSG_SUB_CONFIG    NULL

enum
{
  PROP_0,
  PROP_PROTOCOL,
  PROP_HOST,
  PROP_PORT,
  PROP_TOPIC,
  PROP_CONFIG
};

static GstStaticPadTemplate msg_sub_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);

static void
gst_msg_sub_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
  GstMsgSub *sub = GST_MSG_SUB (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (sub);

  if (!MSG_SUB_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (sub);
  switch (property_id) {
    case PROP_PROTOCOL:
      sub->protocol = g_strdup (g_value_get_string (value));
      break;
    case PROP_HOST:
      sub->host = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      sub->port = g_value_get_int (value);
      break;
    case PROP_TOPIC:
      sub->topic = g_strdup (g_value_get_string (value));
      break;
    case PROP_CONFIG:
      sub->config = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sub);
}

static void
gst_msg_sub_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
  GstMsgSub *sub = GST_MSG_SUB (object);

  GST_OBJECT_LOCK (sub);
  switch (property_id) {
    case PROP_PROTOCOL:
      g_value_set_string (value, sub->protocol);
      break;
    case PROP_HOST:
      g_value_set_string (value, sub->host);
      break;
    case PROP_PORT:
      g_value_set_int (value, sub->port);
      break;
    case PROP_TOPIC:
      g_value_set_string (value, sub->topic);
      break;
    case PROP_CONFIG:
      g_value_set_string (value, sub->config);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sub);
}

static void
gst_msg_sub_finalize (GObject *object)
{
  GstMsgSub *sub = GST_MSG_SUB (object);

  if (sub->msg_queue != NULL) {
    gst_data_queue_set_flushing (sub->msg_queue, TRUE);
    gst_data_queue_flush (sub->msg_queue);
    gst_object_unref (GST_OBJECT_CAST (sub->msg_queue));
    sub->msg_queue = NULL;
  }

  g_free (sub->protocol);
  g_free (sub->host);
  g_free (sub->topic);
  g_free (sub->config);

  if (sub->adaptor != NULL)
    gst_msg_protocol_free (sub->adaptor);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
msgsub_free_queue_item (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

// Bring data from adaptor to plugin
static void
gst_plugin_sub_callback (gpointer queue, GstAdaptorCallbackInfo *cbinfo)
{
  switch (cbinfo->cbtype) {
    case GST_CALLBACK_INFO_MESSAGE:
      GstMessageInfo *msginfo = &(cbinfo->info.msginfo);
      GstDataQueueItem *item;
      GstDataQueue *msg_queue = (GstDataQueue *) queue;

      item = g_slice_new0 (GstDataQueueItem);
      item->object = GST_MINI_OBJECT (msginfo->message);
      item->size = gst_buffer_get_size ((GstBuffer *)(msginfo->message));
      item->duration = GST_BUFFER_DURATION ((GstBuffer *)(msginfo->message));
      item->visible = TRUE;
      item->destroy = (GDestroyNotify) msgsub_free_queue_item;

      if (!gst_data_queue_push (msg_queue, item)) {
        GST_ERROR ("Failed to push item %p in data queue.", item);
        item->destroy (item);
        break;
      }

      GST_DEBUG ("Queued buffer %p", item->object);
      break;
    case GST_CALLBACK_INFO_EVENT:
      break;
    default:
      GST_WARNING ("Unknown callbackinfo type.");
  }
}

static gboolean
gst_msg_sub_start (GstBaseSrc *src)
{
  GstMsgSub *sub = GST_MSG_SUB (src);

  g_return_val_if_fail (sub->protocol != NULL, FALSE);
  g_return_val_if_fail (sub->host != NULL, FALSE);
  g_return_val_if_fail (sub->port >= 0, FALSE);
  g_return_val_if_fail (sub->msg_queue != NULL, FALSE);
  g_return_val_if_fail (gst_plugin_sub_callback != NULL, FALSE);

  sub->adaptor = gst_msg_protocol_new (sub->protocol, "sub");
  if (!sub->adaptor) {
    GST_ERROR_OBJECT (sub, "Failed to initialize protocol adaptor.");
    return FALSE;
  }

  if (!gst_msg_protocol_config (sub->adaptor, sub->config))
    goto start_failed;

  if (!gst_msg_protocol_connect (sub->adaptor, sub->host, sub->port))
    goto start_failed;

  if (!gst_msg_protocol_subscribe (sub->adaptor, sub->topic, sub->msg_queue,
      gst_plugin_sub_callback))
    goto start_failed;

  return TRUE;

start_failed:
  gst_msg_protocol_free (sub->adaptor);
  sub->adaptor = NULL;
  return FALSE;
}

static gboolean
gst_msg_sub_stop (GstBaseSrc *src)
{
  GstMsgSub *sub = GST_MSG_SUB (src);

  if (!gst_msg_protocol_disconnect (sub->adaptor)) {
    GST_ERROR_OBJECT (sub, "Failed to disconnect.");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_msg_sub_create (GstBaseSrc *basesrc, guint64 offset, guint size,
    GstBuffer **buf)
{
  GstMsgSub *sub = GST_MSG_SUB (basesrc);
  GstDataQueueItem *item = NULL;

  GST_DEBUG_OBJECT (sub, "Poping buffer, queue empty: %s.",
      gst_data_queue_is_empty (sub->msg_queue)? "True": "False");

  if (gst_data_queue_pop (sub->msg_queue, &item)) {
    GST_DEBUG_OBJECT (sub, "Poped buffer %p.", item->object);
    *buf = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);
  }

  if (*buf == NULL)
    return GST_FLOW_EOS;

  return GST_FLOW_OK;
}

static gboolean
gst_msg_sub_send_event (GstElement * element, GstEvent * event)
{
  GstMsgSub *sub = GST_MSG_SUB (element);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (sub, "Received EOS, flush data queue.");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_data_queue_set_flushing (sub->msg_queue, TRUE);
      gst_data_queue_flush (sub->msg_queue);
      break;
    default:
      break;
  }

  // Invoke send_event defined in GstBaseSrc
  ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);

  return ret;
}

static void
gst_msg_sub_class_init (GstMsgSubClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_msg_sub_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_msg_sub_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_msg_sub_finalize);

  g_object_class_install_property (gobject, PROP_PROTOCOL,
      g_param_spec_string ("protocol", "protocol",
          "Message protocol (mqtt .etc).",
          DEFAULT_MSG_SUB_PROTOCOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_HOST,
      g_param_spec_string ("host", "host",
          "The IP address to send packets to.",
          DEFAULT_MSG_SUB_HOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port to send packets to.",
          0, G_MAXINT, DEFAULT_MAG_SUB_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject, PROP_TOPIC,
      g_param_spec_string ("topic", "topic",
          "The topic to sublish to.",
          DEFAULT_MSG_SUB_TOPIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_CONFIG,
      g_param_spec_string ("config", "config file",
          "The absolute path of protocol config file.",
          DEFAULT_MSG_SUB_CONFIG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  gst_element_class_set_static_metadata (element_class,
      "Message Subscriber Client", "Src/Network",
      "Send message on topic via plugin based on specified protocol", "QTI");

  gst_element_class_add_static_pad_template (element_class, &msg_sub_template);

  element_class->send_event = GST_DEBUG_FUNCPTR (gst_msg_sub_send_event);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_msg_sub_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_msg_sub_stop);
  basesrc_class->create = GST_DEBUG_FUNCPTR (gst_msg_sub_create);

  GST_DEBUG_CATEGORY_INIT (msgsub_debug, "qtimsgsub", 0,
      "Message Subscriber Client");
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_msg_sub_init (GstMsgSub *sub)
{
  GST_DEBUG_OBJECT (sub, "Init instance.");
  sub->msg_queue = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  gst_base_src_set_live (GST_BASE_SRC (sub), TRUE);
  gst_base_src_set_async (GST_BASE_SRC (sub), FALSE);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qtimsgsub",
      GST_RANK_PRIMARY, GST_TYPE_MSG_SUB);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimsgsub,
    "Message Subscriber Client Library",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)