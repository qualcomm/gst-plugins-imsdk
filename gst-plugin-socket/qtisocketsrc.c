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

#include "qtisocketsrc.h"

#include <errno.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video-format.h>
#include <gst/video/video-frame.h>
#include <gst/utils/common-utils.h>

#include "qtifdsocket.h"


#define DEFAULT_SOCKET   NULL
#define DEFAULT_TIMEOUT  1000

#define gst_socket_src_parent_class parent_class
G_DEFINE_TYPE (GstFdSocketSrc, gst_socket_src, GST_TYPE_PUSH_SRC);

GST_DEBUG_CATEGORY_STATIC (gst_socket_src_debug);
#define GST_CAT_DEFAULT gst_socket_src_debug

enum
{
  PROP_0,
  PROP_SOCKET,
  PROP_TIMEOUT
};

static GstStaticPadTemplate socket_src_template =
  GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

static G_DEFINE_QUARK (SocketBufferQDataQuark, socket_buffer_qdata);

static gboolean
gst_socket_src_set_location (GstFdSocketSrc * src, const gchar * location)
{
  g_free (src->sockfile);

  if (location != NULL) {
    src->sockfile = g_strdup (location);
    GST_INFO_OBJECT (src, "Socket file : %s", src->sockfile);
  } else {
    src->sockfile = NULL;
  }
  return TRUE;
}

static void
gst_socket_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (src);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (src);
  switch (prop_id) {
    case PROP_SOCKET:
      gst_socket_src_set_location (src, g_value_get_string (value));
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_uint64 (value);
      GST_DEBUG_OBJECT (src, "Socket poll timeout %" GST_TIME_FORMAT,
          GST_TIME_ARGS (src->timeout));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (src);
}

static void
gst_socket_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (object);

  GST_OBJECT_LOCK (src);
  switch (prop_id) {
    case PROP_SOCKET:
      g_value_set_string (value, src->sockfile);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, src->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_socket_src_start (GstBaseSrc * bsrc)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (bsrc);
  struct sockaddr_un address = {0};
  gint addrlen = 0;

  src->socket = socket (AF_UNIX, SOCK_STREAM, 0);
  if (src->socket < 0) {
    GST_ERROR_OBJECT (src, "Socket creation error");
    return FALSE;
  }

  unlink (src->sockfile);

  address.sun_family = AF_UNIX;
  g_strlcpy (address.sun_path, src->sockfile, sizeof (address.sun_path));
  if (bind (src->socket, (struct sockaddr *) &address, sizeof (address)) < 0) {
    GST_ERROR_OBJECT (src, "Socket bind failed");
    close (src->socket);
    src->socket = 0;
    return FALSE;
  }

  if (listen (src->socket, 3) < 0) {
    GST_ERROR_OBJECT (src, "Socket bind failed");
    close (src->socket);
    unlink (src->sockfile);
    src->socket = 0;
    return FALSE;
  }

  addrlen = sizeof (address);
  src->client_sock = accept (src->socket,
      (struct sockaddr *) &address, (socklen_t *) &addrlen);
  if (src->client_sock < 0) {
    GST_ERROR_OBJECT (src, "Socket accept failed");
    close (src->socket);
    unlink (src->sockfile);
    src->socket = 0;
    return FALSE;
  }

  src->fdmap = g_hash_table_new (NULL, NULL);
  g_mutex_init (&src->fdmaplock);

  GST_INFO_OBJECT (src, "Socket connected");

  return TRUE;
}

static gboolean
gst_socket_src_socket_release (GstFdSocketSrc * src)
{
  GList *keys_list = NULL;
  GST_INFO_OBJECT (src, "Socket release");

  shutdown (src->client_sock, SHUT_RDWR);
  close (src->client_sock);
  src->client_sock = 0;

  shutdown (src->socket, SHUT_RDWR);
  close (src->socket);
  src->socket = 0;

  g_mutex_lock (&src->fdmaplock);
  keys_list = g_hash_table_get_keys (src->fdmap);

  for (GList *element = keys_list; element; element = element->next) {
    gint buf_id = GPOINTER_TO_INT (element->data);
    gint fd;

    fd = GPOINTER_TO_INT (g_hash_table_lookup (src->fdmap,
        GINT_TO_POINTER (buf_id)));

    g_hash_table_remove (src->fdmap, GINT_TO_POINTER (buf_id));

    GST_INFO_OBJECT (src, "Cleanup buffer fd: %d, buf_id: %d", fd, buf_id);
    close(fd);
  }

  g_list_free (keys_list);
  g_mutex_unlock (&src->fdmaplock);

  g_hash_table_destroy (src->fdmap);
  g_mutex_clear (&src->fdmaplock);

  unlink (src->sockfile);

  return TRUE;
}

static gboolean
gst_socket_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  return GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
}

static void
gst_socket_src_buffer_release (GstStructure * structure)
{
  GstFdMessage info = {0};
  gint fd = 0;
  gint socket = 0;

  GST_DEBUG ("%s", gst_structure_to_string (structure));

  gst_structure_get_int (structure, "socket", &socket);
  gst_structure_get_int (structure, "fd", &fd);
  gst_structure_get_int (structure, "bufid", &info.return_frame.buf_id);

  info.id = MESSAGE_RETURN_FRAME;

  GST_DEBUG ("Return buffer %d", info.return_frame.buf_id);

  if (send_fd_message (socket, &info, sizeof (info), -1) < 0)
    GST_ERROR ("Unable to release buffer");

  gst_structure_free (structure);
}

static GstFlowReturn
gst_socket_src_fill_buffer (GstFdSocketSrc * src, GstBuffer ** outbuf)
{
  GstAllocator *allocator = NULL;
  GstMemory *gstmemory = NULL;
  GstBuffer *gstbuffer = NULL;
  GstStructure *structure = NULL;
  gint fd = 0;
  GstFdMessage info = {0};

  if (receive_fd_message (src->client_sock, &info, sizeof (info), &fd) < 0) {
    GST_ERROR_OBJECT (src, "Unable to receive fd message");
    return GST_FLOW_ERROR;
  }

  if (info.id == MESSAGE_EOS) {
    GST_INFO_OBJECT (src, "MESSAGE_EOS");
    return GST_FLOW_EOS;
  }

  g_return_val_if_fail (info.id == MESSAGE_NEW_FRAME, GST_FLOW_ERROR);

  GST_DEBUG_OBJECT (src, "info: msg_id: %d, buf_id %d, width: %d, height: %d",
      info.id, info.new_frame.buf_id, info.new_frame.width, info.new_frame.height);

  if (fd <= 0) {
    g_mutex_lock (&src->fdmaplock);
    fd = GPOINTER_TO_INT (g_hash_table_lookup (src->fdmap,
        GINT_TO_POINTER (info.new_frame.buf_id)));
    g_mutex_unlock (&src->fdmaplock);

    if (fd <= 0) {
      GST_ERROR_OBJECT (src, "Unable to get fd");
      return GST_FLOW_ERROR;
    }
  }

  // Create a GstBuffer.
  gstbuffer = gst_buffer_new ();
  g_return_val_if_fail (gstbuffer != NULL, GST_FLOW_ERROR);

  // Create a FD backed allocator.
  allocator = gst_fd_allocator_new ();
  if (allocator == NULL) {
    gst_buffer_unref (gstbuffer);
    GST_ERROR_OBJECT (src, "Failed to create FD allocator!");
    return GST_FLOW_ERROR;
  }

  // Wrap our buffer memory block in FD backed memory.
  gstmemory = gst_fd_allocator_alloc (allocator, fd, info.new_frame.maxsize,
      GST_FD_MEMORY_FLAG_DONT_CLOSE);
  if (gstmemory == NULL) {
    gst_buffer_unref (gstbuffer);
    gst_object_unref (allocator);
    GST_ERROR_OBJECT (src, "Failed to allocate FD memory block!");
    return GST_FLOW_ERROR;
  }

  // Set the actual size filled with data.
  gst_memory_resize (gstmemory, 0, info.new_frame.size);

  // Set GStreamer buffer video metadata.
  gst_buffer_add_video_meta_full (
      gstbuffer, GST_VIDEO_FRAME_FLAG_NONE,
      (GstVideoFormat)info.new_frame.format, info.new_frame.width,
      info.new_frame.height, info.new_frame.n_planes,
      info.new_frame.offset,info.new_frame.stride
  );

  // Append the FD backed memory to the newly created GstBuffer.
  gst_buffer_append_memory (gstbuffer, gstmemory);

  // Unreference the allocator so that it is owned only by the gstmemory.
  gst_object_unref (allocator);

  GST_BUFFER_PTS (gstbuffer) = info.new_frame.timestamp;
  GST_BUFFER_DTS (gstbuffer) = GST_CLOCK_TIME_NONE;

  // GSreamer structure for later recreating the sink buffer to be returned.
  structure = gst_structure_new_empty ("SOCKET_BUFFER");
  if (structure == NULL) {
    gst_buffer_unref (gstbuffer);
    GST_ERROR_OBJECT (src, "Failed to create buffer structure!");
    return GST_FLOW_ERROR;
  }

  // info needed to return buffer
  gst_structure_set (structure,
      "socket", G_TYPE_INT, src->client_sock,
      "fd", G_TYPE_INT, fd,
       "bufid", G_TYPE_INT, info.new_frame.buf_id,
      NULL);

  // Set a notification function to signal when the buffer is no longer used.
  gst_mini_object_set_qdata (
      GST_MINI_OBJECT (gstbuffer), socket_buffer_qdata_quark (),
      structure, (GDestroyNotify) gst_socket_src_buffer_release
  );

  g_mutex_lock (&src->fdmaplock);
  g_hash_table_insert (src->fdmap, GINT_TO_POINTER (info.new_frame.buf_id),
      GINT_TO_POINTER (fd));
  g_mutex_unlock (&src->fdmaplock);

  *outbuf = gstbuffer;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_socket_src_wait_buffer (GstFdSocketSrc * src)
{
  GstClockTime timeout;
  gboolean retry;
  gint retval;
  struct pollfd poll_fd;

  timeout = (src->timeout > 0) ? src->timeout * GST_USECOND : GST_CLOCK_TIME_NONE;

  do {
    retry = FALSE;

    GST_DEBUG_OBJECT (src, "socket poll timeout %" GST_TIME_FORMAT,
        GST_TIME_ARGS (src->timeout));

    poll_fd.fd = src->client_sock;
    poll_fd.events = POLLIN;
    retval = poll (&poll_fd, 1, timeout);

    if (G_UNLIKELY (retval < 0)) {
      if (errno == EINTR || errno == EAGAIN) {
        retry = TRUE;
      } else if (errno == EBUSY) {
        return GST_FLOW_FLUSHING;
      } else {
        GST_DEBUG_OBJECT (src, "Socket polling error");
        return GST_FLOW_ERROR;
      }
    } else if (G_UNLIKELY (retval == 0)) {
      retry = TRUE;
      GST_DEBUG_OBJECT (src, "Socket polling timeout.");
    }
  } while (G_UNLIKELY (retry));

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_socket_src_change_state (GstElement * element, GstStateChange transition)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFdMessage msg = {0};

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_socket_src_socket_release (src);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      msg.id = MESSAGE_DISCONNECT;
      if (send_fd_message (src->client_sock, &msg, sizeof (msg), -1) < 0)
        GST_INFO_OBJECT (src, "Unable to send disconnect message.");
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (src, "Failure");
    return ret;
  }

  return ret;
}

static GstFlowReturn
gst_socket_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (psrc);

  GstFlowReturn ret = gst_socket_src_wait_buffer (src);
  g_return_val_if_fail (ret == GST_FLOW_OK, ret);

  return gst_socket_src_fill_buffer (src, outbuf);
}

static void
gst_socket_src_dispose (GObject * obj)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (obj);

  g_free (src->sockfile);
  src->sockfile = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_socket_src_init (GstFdSocketSrc * src)
{
  src->socket = 0;
  src->client_sock = 0;
  src->timeout = 0;

  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  GST_DEBUG_CATEGORY_INIT (gst_socket_src_debug, "qtisocketsrc", 0,
    "qtisocketsrc object");
}

static void
gst_socket_src_class_init (GstFdSocketSrcClass * klass)
{
  GObjectClass *gobject;
  GstElementClass *gstelement;
  GstBaseSrcClass *gstbasesrc;
  GstPushSrcClass *gstpush_src;

  gobject = G_OBJECT_CLASS (klass);
  gstelement = GST_ELEMENT_CLASS (klass);
  gstbasesrc = GST_BASE_SRC_CLASS (klass);
  gstpush_src = GST_PUSH_SRC_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_socket_src_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_socket_src_get_property);
  gobject->dispose      = GST_DEBUG_FUNCPTR (gst_socket_src_dispose);

  g_object_class_install_property (gobject, PROP_SOCKET,
    g_param_spec_string ("socket", "Socket Location",
        "Location of the Unix Domain Socket", DEFAULT_SOCKET,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
        GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject, PROP_TIMEOUT,
    g_param_spec_uint64 ("timeout", "Socket timeout",
        "Socket post timeout", 0, G_MAXUINT64, DEFAULT_TIMEOUT,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
        GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (gstelement,
      "QTI Socket Source Element", "Socket Source Element",
      "This plugin receive GST buffer over Unix Domain Socket", "QTI");

  gst_element_class_add_static_pad_template (gstelement, &socket_src_template);

  gstbasesrc->start = GST_DEBUG_FUNCPTR (gst_socket_src_start);
  gstbasesrc->query = GST_DEBUG_FUNCPTR (gst_socket_src_query);
  gstpush_src->create = GST_DEBUG_FUNCPTR (gst_socket_src_create);

  gstelement->change_state = GST_DEBUG_FUNCPTR (gst_socket_src_change_state);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisocketsrc", GST_RANK_PRIMARY,
            GST_TYPE_SOCKET_SRC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisocketsrc,
    "Transfer GST buffer over Unix Domain Socket",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
