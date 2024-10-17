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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtisocketsink.h"

#include <gst/allocators/gstfdmemory.h>
#include <gst/utils/common-utils.h>

#include "qtifdsocket.h"

#include <gst/ml/gstmlmeta.h>


#define gst_socket_sink_parent_class parent_class
G_DEFINE_TYPE (GstFdSocketSink, gst_socket_sink, GST_TYPE_BASE_SINK);

GST_DEBUG_CATEGORY_STATIC (gst_socket_sink_debug);
#define GST_CAT_DEFAULT gst_socket_sink_debug

#define GST_SOCKET_SINK_CAPS \
    "neural-network/tensors;" \
    "video/x-raw(ANY)"

enum
{
  PROP_0,
  PROP_SOCKET
};

static GstStaticPadTemplate socket_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_SOCKET_SINK_CAPS));

static gboolean
gst_socket_sink_set_location (GstFdSocketSink * sink, const gchar * location)
{
  g_free (sink->sockfile);

  if (location != NULL) {
    sink->sockfile = g_strdup (location);
    GST_INFO_OBJECT (sink, "sockfile : %s", sink->sockfile);
  } else {
    sink->sockfile = NULL;
  }
  return TRUE;
}

static void
gst_socket_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (sink);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (sink);
  switch (prop_id) {
    case PROP_SOCKET:
      gst_socket_sink_set_location (sink, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_socket_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (object);

  GST_OBJECT_LOCK (sink);
  switch (prop_id) {
    case PROP_SOCKET:
      g_value_set_string (value, sink->sockfile);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
}

static gboolean
gst_socket_sink_try_connect (GstFdSocketSink * sink)
{
  struct sockaddr_un address = {0};

  g_mutex_lock (&sink->socklock);

  if (gst_task_get_state (sink->task) == GST_TASK_STARTED) {
    g_mutex_unlock (&sink->socklock);
    return TRUE;
  }

  if ((sink->socket = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
    GST_ERROR_OBJECT (sink, "Socket creation error");
    g_mutex_unlock (&sink->socklock);
    return FALSE;
  }

  address.sun_family = AF_UNIX;
  g_strlcpy (address.sun_path, sink->sockfile, sizeof (address.sun_path));

  if (connect (sink->socket,
      (struct sockaddr *) &address, sizeof (address)) < 0) {
    close (sink->socket);
    sink->socket = 0;
    g_mutex_unlock (&sink->socklock);
    GST_DEBUG_OBJECT (sink, "connect unsuccessfull");
    return FALSE;
  }

  gst_task_start (sink->task);

  g_mutex_unlock (&sink->socklock);

  GST_INFO_OBJECT (sink, "Socket connected");

  return TRUE;
}

static gboolean
gst_socket_sink_disconnect (GstFdSocketSink * sink)
{
  GList *keys_list = NULL;

  g_mutex_lock (&sink->socklock);

  if (gst_task_get_state (sink->task) == GST_TASK_STOPPED) {
    g_mutex_unlock (&sink->socklock);
    return TRUE;
  }

  g_mutex_lock (&sink->bufmaplock);

  keys_list = g_hash_table_get_keys (sink->bufmap);
  for (GList *element = keys_list; element; element = element->next) {
    gint buf_id = GPOINTER_TO_INT (element->data);
    GstBuffer *buffer =
        g_hash_table_lookup (sink->bufmap, GINT_TO_POINTER (buf_id));

    g_hash_table_remove (sink->bufmap, GINT_TO_POINTER (buf_id));

    if (buffer) {
      gst_buffer_unref (buffer);
      sink->bufcount--;
    }

    GST_INFO_OBJECT (sink, "Cleanup buffer: %p, buf_id: %d", buffer, buf_id);
  }
  g_list_free (keys_list);

  g_mutex_unlock (&sink->bufmaplock);

  shutdown (sink->socket, SHUT_RDWR);
  close (sink->socket);
  unlink (sink->sockfile);
  sink->socket = 0;

  gst_task_stop (sink->task);
  g_atomic_int_set (&sink->should_stop, FALSE);

  g_mutex_unlock (&sink->socklock);

  GST_INFO_OBJECT (sink, "Socket disconnected");

  return TRUE;
}

static GstFlowReturn
gst_socket_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);
  GstMemory *memory = NULL;

  gint buffer_fd = 0;
  GstFdMessage info = {0};
  gint mcount = 0;

  GST_DEBUG_OBJECT (sink, "gst_socket_sink_render");

  if (g_atomic_int_get (&sink->should_stop))
    return GST_FLOW_OK;

  if (!gst_socket_sink_try_connect (sink))
    return GST_FLOW_OK;

  mcount = gst_buffer_n_memory (buffer);
  if (mcount != 1) {
    GST_ERROR_OBJECT (sink, "Unexpected number of memory blocks: %d", mcount);
    return GST_FLOW_ERROR;
  }

  memory = gst_buffer_peek_memory (buffer, 0);
  if (!GST_IS_FD_ALLOCATOR (memory->allocator)) {
    GST_ERROR_OBJECT (sink, "Memory allocator is not fd");
    return GST_FLOW_ERROR;
  }

  if (sink->mode == INPUT_MODE_VIDEO) {
    buffer_fd = gst_fd_memory_get_fd (memory);

    GstVideoMeta *meta = gst_buffer_get_video_meta (buffer);
    if (meta == NULL) {
      GST_ERROR_OBJECT (sink, "Invalid video meta");
      return GST_FLOW_ERROR;
    }

    info.id = MESSAGE_NEW_FRAME;
    info.new_frame.buf_id = buffer_fd;
    info.new_frame.width = meta->width;
    info.new_frame.height = meta->height;
    info.new_frame.size = gst_memory_get_sizes (memory, NULL, &info.new_frame.maxsize);
    info.new_frame.format = meta->format;
    info.new_frame.flags = meta->flags;
    info.new_frame.n_planes = meta->n_planes;
    info.new_frame.use_buffer_pool = buffer->pool != NULL;

    for (guint i = 0; i < meta->n_planes; i++) {
      info.new_frame.stride[i] = meta->stride[i];
      info.new_frame.offset[i] = meta->offset[i];
    }

    GST_DEBUG_OBJECT (sink,
        "Buffer: %p w: %d h: %d buf_id: %d size: %" G_GSIZE_FORMAT, buffer,
        meta->width, meta->height, info.new_frame.buf_id, info.new_frame.size);
  } else if (sink->mode == INPUT_MODE_TENSOR) {
    buffer_fd = gst_fd_memory_get_fd (memory);

    GstMLTensorMeta *mlmeta =
      gst_buffer_get_ml_tensor_meta (buffer);

    info.id = MESSAGE_NEW_TENSOR_FRAME;
    info.tensor_frame.buf_id = buffer_fd;
    info.tensor_frame.meta = *mlmeta;
    info.tensor_frame.size = gst_memory_get_sizes (memory, NULL, &info.tensor_frame.maxsize);

    GST_DEBUG_OBJECT (sink,
        "Buffer ML meta buf_id: %d", info.tensor_frame.buf_id);
  } else {
    GST_ERROR_OBJECT (sink, "Unsupported mode: %d", sink->mode);
    return GST_FLOW_ERROR;
  }

  info.new_frame.timestamp = GST_BUFFER_TIMESTAMP (buffer);

  g_mutex_lock (&sink->bufmaplock);
  // Transfer FDs if source does not use buffer pool or
  // if we send this buffer for first time.
  if (buffer->pool &&
      g_hash_table_contains (sink->bufmap, GINT_TO_POINTER (info.new_frame.buf_id)))
    buffer_fd = -1;

  g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (info.new_frame.buf_id), buffer);
  sink->bufcount++;
  g_mutex_unlock (&sink->bufmaplock);

  gst_buffer_ref (buffer);

  if (send_fd_message (sink->socket, &info, sizeof (info), buffer_fd) < 0) {
    GST_ERROR_OBJECT (sink, "Send Fd message failed!");
    gst_socket_sink_disconnect (sink);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_socket_sink_connect_loop (gpointer user_data)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (user_data);

  if (g_atomic_int_get (&sink->should_stop)) {
    gst_task_stop (sink->connect_task);
    return;
  }

  if (gst_socket_sink_try_connect (sink)) {
    gst_task_stop (sink->connect_task);
    return;
  }
  sleep(1);
}

static void
gst_socket_sink_wait_buffer_loop (gpointer user_data)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (user_data);
  GstBuffer *buffer = NULL;

  GstFdMessage info = {0};

  g_mutex_lock (&sink->bufmaplock);

  if (g_atomic_int_get (&sink->should_stop) && sink->bufcount == 0) {
    g_mutex_unlock (&sink->bufmaplock);
    gst_socket_sink_disconnect (sink);
    GST_INFO_OBJECT (sink, "Buffer loop stop");
    return;
  }

  g_mutex_unlock (&sink->bufmaplock);

  if (receive_fd_message (sink->socket, &info, sizeof (info), NULL) < 0) {
    GST_INFO_OBJECT (sink, "Socket mesage receive failed");
    gst_socket_sink_disconnect (sink);
    return;
  }

  if (info.id == MESSAGE_DISCONNECT) {
    g_atomic_int_set (&sink->should_stop, TRUE);
    GST_INFO_OBJECT (sink, "MESSAGE_DISCONNECT");
    return;
  }

  g_mutex_lock (&sink->bufmaplock);
  buffer = g_hash_table_lookup (sink->bufmap, GINT_TO_POINTER (info.new_frame.buf_id));
  g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (info.new_frame.buf_id), NULL);
  sink->bufcount--;
  g_mutex_unlock (&sink->bufmaplock);

  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (sink, "Buffer returned %p, buf_id: %d, count: %d",
      buffer, info.new_frame.buf_id, sink->bufcount);
}

static gboolean
gst_socket_sink_start (GstBaseSink * basesink)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (basesink);

  GST_INFO_OBJECT (sink, "Socket file : %s", sink->sockfile);

  g_object_set (G_OBJECT (basesink), "sync", FALSE, NULL);

  sink->bufmap = g_hash_table_new (NULL, NULL);
  g_mutex_init (&sink->bufmaplock);
  sink->bufcount = 0;
  g_mutex_init (&sink->socklock);

  g_atomic_int_set (&sink->should_stop, FALSE);
  sink->task = gst_task_new (gst_socket_sink_wait_buffer_loop, sink, NULL);
  gst_task_set_lock (sink->task, &sink->tasklock);

  sink->connect_task = gst_task_new (gst_socket_sink_connect_loop, sink, NULL);
  gst_task_set_lock (sink->connect_task, &sink->connect_tasklock);
  gst_task_start (sink->connect_task);

  return TRUE;
}

static gboolean
gst_socket_sink_stop (GstBaseSink * basesink)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (basesink);

  GST_INFO_OBJECT (sink, "Stop");

  g_atomic_int_set (&sink->should_stop, TRUE);

  // Handle the case where the socket is still not conected
  g_mutex_lock (&sink->socklock);
  if (gst_task_get_state (sink->task) == GST_TASK_STOPPED && sink->socket > 0) {
      shutdown (sink->socket, SHUT_RDWR);
      close (sink->socket);
      unlink (sink->sockfile);
      sink->socket = 0;
  }
  g_mutex_unlock (&sink->socklock);

  gst_task_join (sink->connect_task);
  gst_task_join (sink->task);
  sink->task = NULL;

  g_hash_table_destroy (sink->bufmap);

  g_mutex_clear (&sink->bufmaplock);
  g_mutex_clear (&sink->socklock);

  if (sink->mlinfo != NULL)
    gst_ml_info_free (sink->mlinfo);

  return TRUE;
}

static gboolean
gst_socket_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);
  gboolean retval = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_BYTES);
      retval = TRUE;
      break;
    default:
      retval = FALSE;
      break;
  }

  if (!retval)
    retval = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);

  return retval;
}

static gboolean
gst_socket_sink_event (GstBaseSink *bsink, GstEvent *event)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);
  GstFdMessage msg = {0};
  gint fd = 0;

  GST_DEBUG_OBJECT (sink, "GST EVENT: %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (sink, "EOS event");
      g_atomic_int_set (&sink->should_stop, TRUE);
      msg.id = MESSAGE_EOS;
      if (send_fd_message (sink->socket, &msg, sizeof (msg), fd) < 0) {
        gst_socket_sink_disconnect (sink);
        GST_ERROR_OBJECT (sink, "Unable to send EOS message.");
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static void
gst_socket_sink_dispose (GObject * obj)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (obj);

  g_free (sink->sockfile);
  sink->sockfile = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_socket_sink_init (GstFdSocketSink * sink)
{
  sink->task = NULL;
  sink->connect_task = NULL;
  sink->socket = 0;
  sink->mode = INPUT_MODE_NONE;
  g_atomic_int_set (&sink->should_stop, FALSE);

  GST_DEBUG_CATEGORY_INIT (gst_socket_sink_debug, "qtisocketsink", 0,
    "qtisocketsink object");
}

static gboolean
gst_socket_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);

  GstStructure *structure = NULL;
  GstMLInfo mlinfo;

  GST_INFO_OBJECT (sink, "Input caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    sink->mode = INPUT_MODE_VIDEO;
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    sink->mode = INPUT_MODE_TEXT;
  } else if (gst_structure_has_name (structure, "neural-network/tensors")) {
     GST_INFO_OBJECT (sink, "TENSOR caps");
     sink->mode = INPUT_MODE_TENSOR;
    if (!gst_ml_info_from_caps (&mlinfo, caps)) {
      GST_ERROR_OBJECT (sink, "Failed to get input ML info from caps %"
          GST_PTR_FORMAT "!", caps);
      return FALSE;
    }

    if (sink->mlinfo != NULL)
      gst_ml_info_free (sink->mlinfo);

    sink->mlinfo = gst_ml_info_copy (&mlinfo);
  }

  return TRUE;
}

static void
gst_socket_sink_class_init (GstFdSocketSinkClass * klass)
{
  GObjectClass *gobject;
  GstElementClass *gstelement;
  GstBaseSinkClass *gstbasesink;

  gobject = G_OBJECT_CLASS (klass);
  gstelement = GST_ELEMENT_CLASS (klass);
  gstbasesink = GST_BASE_SINK_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_socket_sink_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_socket_sink_get_property);
  gobject->dispose      = GST_DEBUG_FUNCPTR (gst_socket_sink_dispose);

  g_object_class_install_property (gobject, PROP_SOCKET,
    g_param_spec_string ("socket", "Socket Location",
        "Location of the Unix Domain Socket", NULL,
        G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS |
        GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (gstelement,
      "QTI Socket Sink Element", "Socket Sink Element",
      "This plugin send GST buffer over Unix Domain Socket", "QTI");

  gst_element_class_add_static_pad_template (gstelement, &socket_sink_template);

  gstbasesink->render = GST_DEBUG_FUNCPTR (gst_socket_sink_render);
  gstbasesink->start = GST_DEBUG_FUNCPTR (gst_socket_sink_start);
  gstbasesink->stop = GST_DEBUG_FUNCPTR (gst_socket_sink_stop);
  gstbasesink->query = GST_DEBUG_FUNCPTR (gst_socket_sink_query);
  gstbasesink->event = GST_DEBUG_FUNCPTR (gst_socket_sink_event);
  gstbasesink->set_caps = GST_DEBUG_FUNCPTR (gst_socket_sink_set_caps);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisocketsink", GST_RANK_PRIMARY,
          GST_TYPE_SOCKET_SINK);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisocketsink,
    "Transfer GST buffer over Unix Domain Socket",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
