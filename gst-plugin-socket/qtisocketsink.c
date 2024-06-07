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

#include <gst/ml/gstmlmeta.h>

#include <errno.h>

#define gst_socket_sink_parent_class parent_class
G_DEFINE_TYPE (GstFdSocketSink, gst_socket_sink, GST_TYPE_BASE_SINK);

GST_DEBUG_CATEGORY_STATIC (gst_socket_sink_debug);
#define GST_CAT_DEFAULT gst_socket_sink_debug

#define GST_SOCKET_SINK_CAPS \
    "neural-network/tensors;" \
    "video/x-raw(ANY);" \
    "text/x-raw"

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
    sink->socket = -1;
    errno = 0;
    g_mutex_unlock (&sink->socklock);
    GST_INFO_OBJECT (sink, "connect unsuccessfull");
    return FALSE;
  }

  g_rec_mutex_lock (&sink->tasklock);
  gst_task_start (sink->task);
  g_rec_mutex_unlock (&sink->tasklock);

  g_mutex_unlock (&sink->socklock);

  GST_INFO_OBJECT (sink, "Socket connected");

  return TRUE;
}

static void
gst_socket_deinitialize_for_buffers (GstFdSocketSink * sink)
{
  GList *keys_list = NULL;

  g_mutex_lock (&sink->bufmaplock);

  GST_INFO_OBJECT (sink, "Cleaning potential buffers %p", sink->bufmap);

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
}

static gboolean
gst_socket_sink_disconnect (GstFdSocketSink * sink)
{
  GST_INFO_OBJECT (sink, "Enter gst_socket_sink_disconnect");

  g_mutex_lock (&sink->socklock);

  if (gst_task_get_state (sink->task) == GST_TASK_STOPPED) {
    g_mutex_unlock (&sink->socklock);
    return TRUE;
  }

  gst_socket_deinitialize_for_buffers (sink);

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
  GstBufferPayload * buffer_pl = NULL;
  GstMemory *memory = NULL;
  GstPayloadInfo pl_info = {NULL, NULL, NULL, NULL, NULL, NULL};
  gint memory_fds[GST_MAX_MEM_BLOCKS]; // todo expand
  gint memory_fds_send[GST_MAX_MEM_BLOCKS]; // todo expand
  guint n_memory = 0;
  gint n_memory_send = 0;

  GST_DEBUG_OBJECT (sink, "gst_socket_sink_render %d", sink->mode);

  if (g_atomic_int_get (&sink->should_stop))
    return GST_FLOW_OK;

  if (!gst_socket_sink_try_connect (sink))
    return GST_FLOW_OK;

  n_memory = gst_buffer_n_memory (buffer);
  if (n_memory != GST_EXPECTED_MEM_BLOCKS(sink)) {
    GST_ERROR_OBJECT (sink, "Invalid number of memory buffers!");
    return GST_FLOW_ERROR;
  }

  buffer_pl = g_malloc (sizeof (GstBufferPayload));

  pl_info.mem_block_info =
      g_ptr_array_new_full (GST_EXPECTED_MEM_BLOCKS(sink), g_free);
  buffer_pl->identity = MESSAGE_BUFFER_INFO;
  buffer_pl->pts = GST_BUFFER_PTS (buffer);
  buffer_pl->dts = GST_BUFFER_DTS (buffer);
  buffer_pl->duration = GST_BUFFER_DURATION (buffer);
  buffer_pl->use_buffer_pool = buffer->pool != NULL;
  pl_info.buffer_info = buffer_pl;

  //From here needs batching logic
  for (guint i = 0; i < n_memory; i++) {
    gsize size = 0;
    gsize maxsize = 0;

    memory = gst_buffer_peek_memory (buffer, i);

    size = memory->size;
    maxsize = memory->maxsize;

    if (sink->mode == DATA_MODE_TEXT) {
      GstTextPayload * text_pl = NULL;
      GstMapInfo map_info;

      *(buffer_pl->buf_id) = -1;
      buffer_pl->use_buffer_pool = 0;

      if (sizeof (text_pl->contents) < size) {
        GST_ERROR ("Got too much text from memory block");
        free_pl_struct (&pl_info);
        return GST_FLOW_ERROR;
      }

      text_pl = g_malloc (sizeof (GstTextPayload));

      text_pl->identity = MESSAGE_TEXT;

      gst_memory_map (memory, &map_info, GST_MAP_READ);
      memmove (text_pl->contents, map_info.data, map_info.size);
      gst_memory_unmap (memory, &map_info);

      text_pl->size = size;
      text_pl->maxsize = maxsize;
      g_ptr_array_add (pl_info.mem_block_info, text_pl);
    } else if (!gst_is_fd_memory (memory)) {
      free_pl_struct (&pl_info);
      GST_ERROR_OBJECT (sink, "Memory allocator is not fd");
      return GST_FLOW_ERROR;
    }

    if (sink->mode == DATA_MODE_TENSOR) {
      GstTensorPayload * tensor_pl = NULL;

      memory_fds[i] = gst_fd_memory_get_fd (memory);
      buffer_pl->buf_id[i] = memory_fds[i];

      GstMLTensorMeta *mlmeta =
        gst_buffer_get_ml_tensor_meta (buffer);
      if (mlmeta == NULL) {
        GST_ERROR_OBJECT (sink, "Invalid mlmeta");
        return GST_FLOW_ERROR;
      }

      tensor_pl = g_malloc (sizeof (GstTensorPayload));

      tensor_pl->identity = MESSAGE_TENSOR;
      tensor_pl->type = mlmeta->type;
      tensor_pl->n_dimensions = mlmeta->n_dimensions;

      for (guint j = 0; j < mlmeta->n_dimensions; j++) {
        tensor_pl->dimensions[j] = mlmeta->dimensions[j];
      }

      GST_DEBUG_OBJECT (sink, "Buffer ML meta buf_id: %d", buffer_pl->buf_id[i]);

      tensor_pl->size = size;
      tensor_pl->maxsize = maxsize;
      g_ptr_array_add (pl_info.mem_block_info, tensor_pl);
    }

    if (sink->mode == DATA_MODE_VIDEO) {
      GstFramePayload * frame_pl = NULL;

      memory_fds[i] = gst_fd_memory_get_fd (memory);
      buffer_pl->buf_id[i] = memory_fds[i];

      GstVideoMeta *meta = gst_buffer_get_video_meta (buffer);
      if (meta == NULL) {
        GST_ERROR_OBJECT (sink, "Invalid video meta");
        return GST_FLOW_ERROR;
      }

      frame_pl = g_malloc (sizeof (GstFramePayload));

      frame_pl->identity = MESSAGE_FRAME;
      frame_pl->width = meta->width;
      frame_pl->height = meta->height;
      frame_pl->format = meta->format;
      frame_pl->n_planes = meta->n_planes;
      frame_pl->flags = meta->flags;

      for (guint j = 0; j < meta->n_planes; j++) {
        frame_pl->offset[j] = meta->offset[j];
        frame_pl->stride[j] = meta->stride[j];
      }

      frame_pl->size = size;
      frame_pl->maxsize = maxsize;
      g_ptr_array_add (pl_info.mem_block_info, frame_pl);
    }

    if (sink->mode != DATA_MODE_TEXT &&
        sink->mode != DATA_MODE_TENSOR &&
        sink->mode != DATA_MODE_VIDEO) {
      GST_ERROR_OBJECT (sink, "Unsupported mode: %d", sink->mode);
      return GST_FLOW_ERROR;
    }

    if (sink->mode != DATA_MODE_TEXT) {
      g_mutex_lock (&sink->bufmaplock);
      if (!buffer->pool ||
          !g_hash_table_contains (sink->bufmap, GINT_TO_POINTER (memory_fds[i]))) {
        memory_fds_send[n_memory_send++] = memory_fds[i];
        g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (memory_fds[i]), buffer);
      } else {
        g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (memory_fds[i]), buffer);
      }
      sink->bufcount++;
      g_mutex_unlock (&sink->bufmaplock);

      gst_buffer_ref (buffer);
    }
  }

  if (n_memory_send > 0) {
    pl_info.fd_count = g_malloc (sizeof (GstFdCountPayload));
    pl_info.fd_count->identity = MESSAGE_FD_COUNT;
    pl_info.fd_count->n_fds = n_memory_send;
  }

  if (sink->mode == DATA_MODE_TEXT) {
    pl_info.fds = NULL;
  } else {
    pl_info.fds = memory_fds_send;
  }

  if (send_socket_message (sink->socket, &pl_info) < 0) {
    if (sink->mode == DATA_MODE_TEXT) {
      GST_ERROR_OBJECT (sink, "Send text message failed! %d", errno);
    } else {
      GST_ERROR_OBJECT (sink, "Send Fd message failed! %d", errno);
    }

    free_pl_struct (&pl_info);
    gst_socket_sink_disconnect (sink);
    return GST_FLOW_ERROR;
  }

  free_pl_struct (&pl_info);

  GST_INFO_OBJECT (sink, "Sent data of type %d; Number of mem blocks sent: %d",
      sink->mode, n_memory_send);

  return GST_FLOW_OK;
}

static void
gst_socket_sink_connect_loop (gpointer user_data)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (user_data);

  if (g_atomic_int_get (&sink->should_stop)) {
    GST_INFO_OBJECT (sink, "Stopping connected task");
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
gst_socket_sink_wait_message_loop (gpointer user_data)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (user_data);
  GstBuffer *buffer = NULL;
  GstPayloadInfo pl_info = {NULL, NULL, NULL, NULL, NULL, NULL};

  if (g_atomic_int_get (&sink->should_stop)) {
    if (sink->mode != DATA_MODE_TEXT) {
      g_mutex_lock (&sink->bufmaplock);

      if (sink->bufcount == 0) {
        GST_INFO_OBJECT (sink, "Stopping buffer loop");
        g_mutex_unlock (&sink->bufmaplock);
        gst_socket_sink_disconnect (sink);
        GST_INFO_OBJECT (sink, "Buffer loop stop");
        return;
      }

      g_mutex_unlock (&sink->bufmaplock);
    } else {
      GST_INFO_OBJECT (sink, "Stopping buffer loop");
      gst_socket_sink_disconnect (sink);
      GST_INFO_OBJECT (sink, "Buffer loop stop");
      return;
    }
  }

  if (receive_socket_message (sink->socket, &pl_info, 0) == -1) {
    GST_INFO_OBJECT (sink, "Socket mesage receive failed");
    gst_socket_sink_disconnect (sink);
    free_pl_struct (&pl_info);
    return;
  }

  if (GST_PL_INFO_IS_MESSAGE (&pl_info, MESSAGE_DISCONNECT)) {
    g_atomic_int_set (&sink->should_stop, TRUE);
    GST_INFO_OBJECT (sink, "MESSAGE_DISCONNECT");
    free_pl_struct (&pl_info);
    return;
  }

  if (pl_info.return_buffer != NULL) {
    if (sink->mode == DATA_MODE_TEXT) {
      GST_WARNING_OBJECT (sink, "Received return buffer, but text mode was chosen!");
      return;
    }

    if (pl_info.fd_count == NULL) {
      GST_ERROR_OBJECT (sink, "Received return buffer, but no fd_count");
      return;
    }

    GST_DEBUG_OBJECT (sink, "Number of returned fds: %d", pl_info.fd_count->n_fds);

    for (gint i = 0; i < pl_info.fd_count->n_fds; i++) {
      gint buf_id = pl_info.return_buffer->buf_id[i];

      g_mutex_lock (&sink->bufmaplock);
      buffer = g_hash_table_lookup (sink->bufmap, GINT_TO_POINTER (buf_id));
      g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (buf_id), NULL);
      sink->bufcount--;
      g_mutex_unlock (&sink->bufmaplock);
      gst_buffer_unref (buffer);

      GST_DEBUG_OBJECT (sink, "Buffer returned %p, buf_id: %d, count: %d",
          buffer, buf_id, sink->bufcount);
    }
  }

  free_pl_struct (&pl_info);
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
  g_rec_mutex_init (&sink->tasklock);
  g_rec_mutex_init (&sink->connect_tasklock);

  g_atomic_int_set (&sink->should_stop, FALSE);
  sink->task = gst_task_new (gst_socket_sink_wait_message_loop, sink, NULL);
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

  if (sink->bufmap) {
    g_hash_table_destroy (sink->bufmap);
  }

  g_mutex_clear (&sink->bufmaplock);
  g_mutex_clear (&sink->socklock);

  if (sink->mlinfo != NULL) {
    gst_ml_info_free (sink->mlinfo);
  }

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
  GstPayloadInfo pl_info = {NULL, NULL, NULL, NULL, NULL, NULL};
  GstMessagePayload msg;

  GST_DEBUG_OBJECT (sink, "GST EVENT: %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (sink, "EOS event");

      g_atomic_int_set (&sink->should_stop, TRUE);

      msg.identity = MESSAGE_EOS;
      pl_info.message = &msg;

      if ((gst_task_get_state (sink->task) == GST_TASK_STARTED) &&
            (send_socket_message (sink->socket, &pl_info) < 0)) {
        gst_socket_sink_disconnect (sink);
        GST_WARNING_OBJECT (sink, "Unable to send EOS message.");
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
  sink->socket = -1;
  sink->mode = DATA_MODE_NONE;

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
    sink->mode = DATA_MODE_VIDEO;
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    sink->mode = DATA_MODE_TEXT;
  } else if (gst_structure_has_name (structure, "neural-network/tensors")) {
    sink->mode = DATA_MODE_TENSOR;

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
      "This plugin sends a GST buffer over Unix Domain Socket", "QTI");

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
