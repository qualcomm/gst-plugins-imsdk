/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "msg-adaptor.h"

#include <string.h>
#include <dlfcn.h>

#define GST_CAT_DEFAULT gst_msg_adaptor_debug
GST_DEBUG_CATEGORY (gst_msg_adaptor_debug);

struct _GstMsgProtocol {
  /// Client role
  gchar                       *role;

  /// Protocol to adapt
  gchar                       *protocol;

  /// Func pointers of underlying protocol
  GstProtocolCommonFunc       *cfunc;
  /// Property structure of underlying protocol
  gpointer                    prop;
  /// Shared object handle of underlying protocol
  gpointer                    libhandle;

  /// Data queue derived from upper-level caller used for callback
  gpointer                    queue;
  /// Callback to send data to upper-level caller in case of subscription
  GstSubscribeCallback        callback;
};

static inline void
gst_msg_adaptor_init_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_msg_adaptor_debug,
        "msg-adaptor", 0, "QTI Message adaptor");
    g_once_init_leave (&catonce, TRUE);
  }
}

static gboolean
load_symbol (GstMsgProtocol *adaptor)
{
  GST_DEBUG ("Loading GST_PROTOCOL_CFUNC_SYMBOL.");
  adaptor->cfunc = dlsym (adaptor->libhandle,
      G_STRINGIFY (GST_PROTOCOL_CFUNC_SYMBOL));
  if (adaptor->cfunc == NULL) {
    GST_ERROR ("Failed to load GST_PROTOCOL_CFUNC_SYMBOL, error: %s", dlerror());
    return FALSE;
  }
  GST_DEBUG ("GST_PROTOCOL_CFUNC_SYMBOL loaded.");

  return TRUE;
}

static void
gst_msg_protocol_prop_free (GstMsgProtocol *adaptor)
{
  g_return_if_fail (adaptor != NULL);

  GST_DEBUG ("Free protocol instance.");

  adaptor->cfunc->free (adaptor->prop);
}

static gboolean
gst_msg_protocol_prop_new (GstMsgProtocol *adaptor)
{
  g_return_val_if_fail (adaptor != NULL, FALSE);

  if (adaptor->prop) {
    GST_WARNING ("Protocol's properties is not NULL, renew it.");
    gst_msg_protocol_prop_free (adaptor);
    adaptor->prop = NULL;
  }

  GST_DEBUG ("Allocating protocol instance.");
  adaptor->prop = adaptor->cfunc->new (adaptor->role);
  if (!adaptor->prop) {
    GST_ERROR ("Failed to allocate protocol library.");
    return FALSE;
  }
  GST_DEBUG ("Protocol instance allocated successfully.");

  return TRUE;
}

GstMsgProtocol *
gst_msg_protocol_new (gchar *protocol, const gchar *role)
{
  GstMsgProtocol *adaptor = NULL;
  gchar filename[50];

  g_return_val_if_fail (protocol != NULL, NULL);
  g_return_val_if_fail (role != NULL, NULL);

  gst_msg_adaptor_init_debug_category ();

  GST_INFO ("Message protocol allocating.");

  GST_DEBUG ("Message adaptor allocating.");
  adaptor = g_slice_new0 (GstMsgProtocol);
  if (!adaptor) {
    GST_ERROR ("Failed to allocate message protocol adaptor.");
    return NULL;
  }
  GST_DEBUG ("Message adaptor allocated.");

  adaptor->role = g_strdup (role);
  adaptor->protocol = g_strdup (protocol);

  snprintf (filename, 50, "libgstqti%sadaptor.so", protocol);
  GST_DEBUG ("Trying to dlopen, filename: %s.", filename);
  adaptor->libhandle = dlopen (filename, RTLD_NOW);
  if (adaptor->libhandle == NULL) {
    GST_ERROR ("Failed to load %s, error: %s.", filename, dlerror());
    goto cleanup;
  }
  GST_DEBUG ("File loaded successfully.");

  GST_DEBUG ("Trying to load symbols dynamically.");
  if (!load_symbol (adaptor)) {
    dlclose (adaptor->libhandle);
    goto cleanup;
  }
  GST_DEBUG ("Symbols loaded successfully.");

  // Properties for specific protocol
  if (!adaptor->prop && !gst_msg_protocol_prop_new (adaptor))
    goto cleanup;

  GST_INFO ("Message protocol allocated.");

  return adaptor;

cleanup:
  GST_DEBUG ("Error in gst_msg_protocol_new, cleanup.");
  g_free (adaptor->role);
  g_free (adaptor->protocol);
  adaptor->libhandle = NULL;
  g_slice_free (GstMsgProtocol, adaptor);

  return NULL;
}

void
gst_msg_protocol_free (GstMsgProtocol *adaptor)
{
  g_return_if_fail (adaptor != NULL);

  GST_DEBUG ("Message adaptor free.");

  g_free (adaptor->role);
  g_free (adaptor->protocol);

  if (adaptor->prop != NULL) {
    gst_msg_protocol_prop_free (adaptor);
    adaptor->prop = NULL;
  }

  if (adaptor->libhandle != NULL)
    dlclose (adaptor->libhandle);

  g_slice_free (GstMsgProtocol, adaptor);
}

gboolean
gst_msg_protocol_config (GstMsgProtocol *adaptor, gchar *path)
{
  gboolean success = TRUE;

  g_return_val_if_fail (adaptor != NULL, FALSE);

  GST_DEBUG ("Message protocol config.");

  success = adaptor->cfunc->config (adaptor->prop, path);
  if (!success) {
    GST_ERROR ("Failed to config message protocol instance.");
    return FALSE;
  }

  return success;
}

gboolean
gst_msg_protocol_connect (GstMsgProtocol *adaptor, gchar *host, gint port)
{
  gboolean success = TRUE;

  g_return_val_if_fail (adaptor != NULL, FALSE);
  g_return_val_if_fail (host != NULL, FALSE);
  g_return_val_if_fail (port >= 0, FALSE);

  GST_INFO ("Message protocol connect to %s:%d.", host, port);

  success = adaptor->cfunc->connect (adaptor->prop, host, port);
  if (!success) {
    GST_ERROR ("Failed to connect.");
    return FALSE;
  }

  return success;
}

gboolean
gst_msg_protocol_disconnect (GstMsgProtocol *adaptor)
{
  gboolean success = TRUE;

  g_return_val_if_fail (adaptor != NULL, FALSE);

  GST_INFO ("Message protocol disconnect.");

  success = adaptor->cfunc->disconnect (adaptor->prop);
  if (!success) {
    GST_ERROR ("Failed to disconnect.");
    return FALSE;
  }

  return success;
}

gboolean
gst_msg_protocol_publish (GstMsgProtocol *adaptor, gchar *topic, gpointer message)
{
  gboolean success = TRUE;

  g_return_val_if_fail (adaptor != NULL, FALSE);
  g_return_val_if_fail (topic != NULL, FALSE);
  g_return_val_if_fail (message != NULL, FALSE);

  GST_INFO ("Message protocol publish on %s.", topic);

  success = adaptor->cfunc->publish (adaptor->prop, topic, message);
  if (!success) {
    GST_ERROR ("Failed to publish message on topic(%s).", topic);
    return FALSE;
  }

  return success;
}

static void
gst_adaptor_sub_callback (gpointer adaptor, GstAdaptorCallbackInfo *cbinfo)
{
  GstMsgProtocol *msg_adaptor = (GstMsgProtocol *) adaptor;

  switch (cbinfo->cbtype) {
    case GST_CALLBACK_INFO_MESSAGE:
      msg_adaptor->callback (msg_adaptor->queue, cbinfo);
      break;
    case GST_CALLBACK_INFO_EVENT:
      break;
    default:
      GST_WARNING ("Unknown callback type in gst_adaptor_sub_callback.");
      break;
  }
}

gboolean
gst_msg_protocol_subscribe (GstMsgProtocol *adaptor, gchar *topic,
    gpointer queue, GstSubscribeCallback callback)
{
  gboolean success = TRUE;

  g_return_val_if_fail (adaptor != NULL, FALSE);
  g_return_val_if_fail (topic != NULL, FALSE);
  g_return_val_if_fail (queue != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  GST_INFO ("Message protocol subscribe on topic(%s).", topic);

  adaptor->callback = callback;
  adaptor->queue = queue;

  success = adaptor->cfunc->subscribe (adaptor->prop, topic,
      gst_adaptor_sub_callback, (gpointer)adaptor);
  if (!success) {
    GST_ERROR ("Failed to subscribe on topic(%s).", topic);
    return FALSE;
  }

  return success;
}
