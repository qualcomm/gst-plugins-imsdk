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

#define DEFAULT_SOCKET   NULL
#define DEFAULT_TIMEOUT  1000

#define gst_socket_src_parent_class parent_class
G_DEFINE_TYPE (GstFdSocketSrc, gst_socket_src, GST_TYPE_PUSH_SRC);

#define GST_SOCKET_SRC_CAPS \
    "neural-network/tensors;" \
    "video/x-raw(ANY);" \
    "text/x-raw"

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
      GST_STATIC_CAPS (GST_SOCKET_SRC_CAPS));


// Declare SocketSrc buffer pool
G_DEFINE_TYPE(GstSocketSrcBufferPool, gst_socketsrc_buffer_pool, GST_TYPE_BUFFER_POOL);
#define socketsrc_pool_parent_class gst_socketsrc_buffer_pool_parent_class

// Declare socket_buffer_qdata_quark() to return Quark for SocketSrc buffer data
static G_DEFINE_QUARK (SocketBufferQDataQuark, socket_buffer_qdata);

static void
gst_socketsrc_buffer_pool_reset (GstBufferPool * pool, GstBuffer * buffer)
{
  GST_LOG_OBJECT (pool, "SOCKET_SRC buffer reset %p", buffer);

  // Invoke the previously registered destroy notify function
  gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer),
      socket_buffer_qdata_quark (), NULL, NULL);

  gst_buffer_remove_all_memory (buffer);
  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);

  GST_BUFFER_POOL_CLASS (socketsrc_pool_parent_class)->reset_buffer (pool, buffer);
}

GstBufferPool *
gst_socketsrc_buffer_pool_new ()
{
  gboolean success = TRUE;
  GstStructure *config = NULL;
  GstSocketSrcBufferPool *pool;

  pool = (GstSocketSrcBufferPool *) g_object_new (
      GST_TYPE_SOCKET_SRC_BUFFER_POOL, NULL);
  g_return_val_if_fail (pool != NULL, NULL);
  gst_object_ref_sink (pool);

  GST_LOG_OBJECT (pool, "New socket src buffer pool %p", pool);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

  gst_buffer_pool_config_set_params (config, NULL, 0, 3, 0);

  success = gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);
  if (success == FALSE) {
    gst_object_unref (pool);
    GST_ERROR("Failed to set pool configuration!");
    return NULL;
  }

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_socketsrc_buffer_pool_class_init (GstSocketSrcBufferPoolClass * klass)
{
  GstBufferPoolClass *pool = (GstBufferPoolClass *) klass;

  pool->reset_buffer = gst_socketsrc_buffer_pool_reset;
}

static void
gst_socketsrc_buffer_pool_init (GstSocketSrcBufferPool * pool)
{
  GST_DEBUG ("Initializing pool!");
}

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
  GstClockTime timeout;
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
      timeout = (src->timeout > 0) ?
          src->timeout * GST_USECOND : GST_CLOCK_TIME_NONE;

      GST_DEBUG_OBJECT (src, "Socket poll timeout %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timeout));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_socket_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (bsrc);
  GstStructure *structure = NULL;
  GstMLInfo mlinfo;

  GST_INFO_OBJECT (src, "Input caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    src->mode = DATA_MODE_VIDEO;
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    src->mode = DATA_MODE_TEXT;
  } else if (gst_structure_has_name (structure, "neural-network/tensors")) {
     src->mode = DATA_MODE_TENSOR;

    if (!gst_ml_info_from_caps (&mlinfo, caps)) {
      GST_ERROR_OBJECT (src, "Failed to get input ML info from caps %"
          GST_PTR_FORMAT "!", caps);
      return FALSE;
    }

    if (src->mlinfo != NULL)
      gst_ml_info_free (src->mlinfo);

    src->mlinfo = gst_ml_info_copy (&mlinfo);
  }

  return TRUE;
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

static gpointer
gst_socket_src_connection_handler (gpointer userdata)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (userdata);

  struct sockaddr_un address = {0};
  GstBufferPool *pool = NULL;
  gint addrlen = 0;

  g_mutex_lock (&src->mutex);

  src->socket = socket (AF_UNIX, SOCK_SEQPACKET, 0);
  if (src->socket < 0) {
    GST_ERROR_OBJECT (src, "Socket creation error");
    g_mutex_unlock (&src->mutex);
    return NULL;
  }

  unlink (src->sockfile);

  address.sun_family = AF_UNIX;
  g_strlcpy (address.sun_path, src->sockfile, sizeof (address.sun_path));

  addrlen = sizeof (address);
  if (bind (src->socket, (struct sockaddr *) &address, addrlen) < 0) {
    GST_ERROR_OBJECT (src, "Socket bind failed");
    src->stop_thread = TRUE;
    g_mutex_unlock (&src->mutex);
    return NULL;
  }

  if (listen (src->socket, 3) < 0) {
    GST_ERROR_OBJECT (src, "Socket listen failed");
    unlink (src->sockfile);
    src->socket = 0;
    src->stop_thread = TRUE;
    g_mutex_unlock (&src->mutex);
    return NULL;
  }

  GST_DEBUG_OBJECT (src, "Socket accept");

  g_mutex_unlock (&src->mutex);

  src->client_sock = accept (src->socket,
      (struct sockaddr *) &address, (socklen_t *) &addrlen);

  if (src->client_sock < 0) {
    GST_WARNING_OBJECT (src, "Socket accept failed");
    src->client_sock = 0;
    g_mutex_lock (&src->mutex);
    src->stop_thread = TRUE;
    g_mutex_unlock (&src->mutex);
    return NULL;
  }

  src->fdmap = g_hash_table_new (NULL, NULL);
  g_mutex_init (&src->fdmaplock);

  pool = gst_socketsrc_buffer_pool_new ();
  if (!pool) {
    GST_ERROR_OBJECT (src, "Failed to create buffer pool!");
    g_mutex_lock (&src->mutex);
    src->stop_thread = TRUE;
    g_mutex_unlock (&src->mutex);
    return FALSE;
  }

  gst_buffer_pool_set_active (pool, TRUE);
  src->pool = pool;

  GST_DEBUG_OBJECT (src, "Socket connected");

  g_mutex_lock (&src->mutex);

  src->thread_done = TRUE;
  g_cond_signal (&src->cond);

  g_mutex_unlock (&src->mutex);

  return NULL;
}

static gboolean
gst_socket_src_socket_release (GstFdSocketSrc * src)
{
  GList *keys_list = NULL;

  GST_DEBUG_OBJECT (src, "Socket release");

  g_mutex_lock (&src->mutex);
  if (src->release_done) {
    g_mutex_unlock (&src->mutex);
    return TRUE;
  }

  src->release_done = TRUE;
  src->stop_thread = TRUE;

  g_cond_broadcast (&src->cond);

  g_mutex_unlock (&src->mutex);

  if (src->pool) {
    gst_buffer_pool_set_active (src->pool, FALSE);
    gst_object_unref (src->pool);
  }

  if (src->fdmap) {
    g_mutex_lock (&src->fdmaplock);
    keys_list = g_hash_table_get_keys (src->fdmap);

    for (GList *element = keys_list; element; element = element->next) {
      gint buf_id = GPOINTER_TO_INT (element->data);
      gint fd;

      fd = GPOINTER_TO_INT (g_hash_table_lookup (src->fdmap,
          GINT_TO_POINTER (buf_id)));

      g_hash_table_remove (src->fdmap, GINT_TO_POINTER (buf_id));

      GST_DEBUG_OBJECT (src, "Cleanup buffer fd: %d, buf_id: %d", fd, buf_id);
      close (fd);
    }

    g_list_free (keys_list);
    g_mutex_unlock (&src->fdmaplock);

    g_hash_table_destroy (src->fdmap);
    g_mutex_clear (&src->fdmaplock);
  }

  if (src->socket > 0) {
    shutdown (src->socket, SHUT_RDWR);
    close (src->socket);
    src->socket = 0;
  }

  if (src->client_sock > 0) {
    shutdown (src->client_sock, SHUT_RDWR);
    close (src->client_sock);
    src->client_sock = 0;
  }

  g_thread_join (src->thread);

  g_mutex_lock (&src->mutex);

  src->thread = NULL;
  src->thread_done = FALSE;
  unlink (src->sockfile);

  g_mutex_unlock (&src->mutex);

  return TRUE;
}

static gboolean
gst_socket_src_start (GstBaseSrc * bsrc)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (bsrc);

  g_mutex_lock (&src->mutex);
  if ((src->thread = g_thread_new ("Connection thread",
      gst_socket_src_connection_handler, src)) == NULL) {
    g_printerr ("ERROR: Failed to create thread!\n");
    g_mutex_unlock (&src->mutex);
    return FALSE;
  }

  src->stop_thread = FALSE;
  src->release_done = FALSE;

  g_mutex_unlock (&src->mutex);

  return TRUE;
}

static gboolean
gst_socket_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  return GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
}

static void
gst_socket_src_buffer_release (GstBufferReleaseData * release_data)
{
  GstPayloadInfo pl_info = {0};
  GstReturnBufferPayload ret_pl;
  GstFdCountPayload fd_count = { .identity = MESSAGE_FD_COUNT,
      .n_fds = release_data->n_fds};

  ret_pl.identity = MESSAGE_RETURN_BUFFER;
  for (guint i = 0; i < release_data->n_fds; i++) {
    ret_pl.buf_id[i] = release_data->buf_id[i];
  }
  pl_info.return_buffer = &ret_pl;
  pl_info.fd_count = &fd_count;

  if (send_socket_message (release_data->socket, &pl_info) < 0) {
    GST_ERROR ("Unable to release buffer");
  }

  g_free (release_data);
}

static void
gst_socket_src_flush_socket_queue (GstFdSocketSrc * src)
{
  gint ret;

  do {
    struct pollfd poll_fd;

    poll_fd.fd = src->client_sock;
    poll_fd.events = POLLIN;

    ret = poll (&poll_fd, 1, 0);

    if (poll_fd.revents & POLLHUP ||
        poll_fd.revents & POLLERR) {
      break;
    }

    if (ret > 0) {
      GstBufferReleaseData * release_data = g_malloc0 (sizeof (GstBufferReleaseData));
      GstPayloadInfo pl_info = {
        .mem_block_info = g_ptr_array_new (),
        .protection_metadata_info = g_ptr_array_new ()
      };

      gint fds[GST_MAX_MEM_BLOCKS] = {0};

      pl_info.fds = fds;

      release_data->socket = src->client_sock;

      if (receive_socket_message (src->client_sock, &pl_info, 0) < 0) {
        g_free (release_data);
        free_pl_struct (&pl_info);
        break;
      }

      if (pl_info.fd_count != NULL) {
        release_data->n_fds = pl_info.fd_count->n_fds;
      } else {
        release_data->n_fds = pl_info.mem_block_info->len;
      }

      if (GST_PL_INFO_IS_MESSAGE (&pl_info, MESSAGE_EOS)) {
        g_free (release_data);
        free_pl_struct (&pl_info);
        break;
      }

      for (guint i = 0; i < pl_info.mem_block_info->len; i++) {
        release_data->buf_id[i] = pl_info.buffer_info->buf_id[i];
      }

      gst_socket_src_buffer_release (release_data);
    }

  } while (ret > 0);
}

static GstFlowReturn
gst_socket_src_fill_buffer (GstFdSocketSrc * src, GstBuffer ** outbuf)
{
  GstAllocator *allocator = NULL;
  GstMemory *gstmemory = NULL;
  GstBuffer *gstbuffer = NULL;
  GstBufferReleaseData * release_data = g_malloc0 (sizeof (GstBufferReleaseData));
  GstPayloadInfo pl_info = {
      .mem_block_info = g_ptr_array_new (),
      .protection_metadata_info = g_ptr_array_new ()
  };

  gint fds[GST_MAX_MEM_BLOCKS] = {0};
  gint n_fds = 0;

  pl_info.fds = fds;

  release_data->socket = src->client_sock;
  if (receive_socket_message (src->client_sock, &pl_info, 0) <= 0) {
    g_free (release_data);
    free_pl_struct (&pl_info);
    return GST_FLOW_ERROR;
  }

  if (pl_info.fd_count != NULL) {
    n_fds = pl_info.fd_count->n_fds;
    release_data->n_fds = pl_info.fd_count->n_fds;
  } else {
    n_fds = 0;
    // we need this to know how many buffers to unref
    release_data->n_fds = pl_info.mem_block_info->len;
  }

  if (GST_PL_INFO_IS_MESSAGE (&pl_info, MESSAGE_EOS)) {
    GST_INFO_OBJECT (src, "MESSAGE_EOS");
    g_free (release_data);
    free_pl_struct (&pl_info);
    return GST_FLOW_EOS;
  }

  for (guint i = 0; i < pl_info.mem_block_info->len; i++) {
    gpointer ptr = g_ptr_array_index (pl_info.mem_block_info, i);

    if (get_payload_size (ptr) == -1) {
      g_free (release_data);
      free_pl_struct (&pl_info);
      return GST_FLOW_ERROR;
    }
  }

  // Create or acquire a GstBuffer.
  if (pl_info.buffer_info->use_buffer_pool) {
    gst_buffer_pool_acquire_buffer (src->pool, &gstbuffer, NULL);
  } else {
    gstbuffer = gst_buffer_new ();
  }

  g_return_val_if_fail (gstbuffer != NULL, GST_FLOW_ERROR);

  if (src->mode != DATA_MODE_TEXT) {
    // Create a FD backed allocator.
    allocator = gst_fd_allocator_new ();
    if (allocator == NULL) {
      gst_buffer_unref (gstbuffer);
      g_free (release_data);
      free_pl_struct (&pl_info);
      GST_ERROR_OBJECT (src, "Failed to create FD allocator!");
      return GST_FLOW_ERROR;
    }
  }

  if (pl_info.buffer_info == NULL)
    GST_ERROR_OBJECT (src, "Didn't receive GstBufferPayload");

  //Start logic for batching from here
  for (guint i = 0; i < pl_info.mem_block_info->len; i++) {
    release_data->buf_id[i] = pl_info.buffer_info->buf_id[i];

    if (src->mode == DATA_MODE_TEXT) {
      GstTextPayload *text_pl =
          (GstTextPayload *) g_ptr_array_index (pl_info.mem_block_info, i);

      gpointer data = g_malloc0 (text_pl->size);
      memmove (data, text_pl->contents, text_pl->size);

      gstmemory = gst_memory_new_wrapped (
          GST_MEMORY_FLAG_ZERO_PADDED & GST_MEMORY_FLAG_ZERO_PREFIXED,
          data, text_pl->maxsize, 0, text_pl->size, NULL, NULL);
    }

    if (src->mode == DATA_MODE_TENSOR) {
      GstTensorPayload *tensor_pl =
          (GstTensorPayload *) g_ptr_array_index (pl_info.mem_block_info, i);

      GST_DEBUG_OBJECT (src,
          "info: msg_id: %d, buf_id %d, pool: %d",
          tensor_pl->identity, pl_info.buffer_info->buf_id[i],
          pl_info.buffer_info->use_buffer_pool);

      // number of fds should match number of memory blocks
      if (n_fds == 0) {
        g_mutex_lock (&src->fdmaplock);
        fds[i] = GPOINTER_TO_INT (g_hash_table_lookup (src->fdmap,
            GINT_TO_POINTER (pl_info.buffer_info->buf_id[i])));
        g_mutex_unlock (&src->fdmaplock);

        if (fds[i] < 0) {
          GST_ERROR_OBJECT (src, "Unable to get fd; Received value: %d", fds[i]);
          return GST_FLOW_ERROR;
        }
      }
      else {
        g_mutex_lock (&src->fdmaplock);
        g_hash_table_insert (src->fdmap,
            GINT_TO_POINTER (pl_info.buffer_info->buf_id[i]),
            GINT_TO_POINTER (fds[i]));
        g_mutex_unlock (&src->fdmaplock);
      }

      // Wrap our buffer memory block in FD backed memory.
      gstmemory = gst_fd_allocator_alloc (allocator, fds[i], tensor_pl->maxsize,
          pl_info.buffer_info->use_buffer_pool ?
          GST_FD_MEMORY_FLAG_DONT_CLOSE : GST_FD_MEMORY_FLAG_NONE);
      if (gstmemory == NULL) {
        gst_buffer_unref (gstbuffer);
        gst_object_unref (allocator);
        g_free (release_data);
        free_pl_struct (&pl_info);
        GST_ERROR_OBJECT (src, "Failed to allocate FD memory block!");
        return GST_FLOW_ERROR;
      }

      // Set the actual size filled with data.
      gst_memory_resize (gstmemory, 0, tensor_pl->size);

      gst_buffer_add_ml_tensor_meta (gstbuffer,
          tensor_pl->type ,
          tensor_pl->n_dimensions,
          tensor_pl->dimensions);
    }

    if (src->mode == DATA_MODE_VIDEO) {
      GstFramePayload *frame_pl =
          (GstFramePayload *) g_ptr_array_index (pl_info.mem_block_info, i);

      GST_DEBUG_OBJECT (src, "info: msg_id: %d, buf_id %d",
          frame_pl->identity, pl_info.buffer_info->buf_id[i]);

      // number of fds should match number of memory blocks
      if (n_fds == 0) {
        g_mutex_lock (&src->fdmaplock);
        fds[i] = GPOINTER_TO_INT (g_hash_table_lookup (src->fdmap,
            GINT_TO_POINTER (pl_info.buffer_info->buf_id[i])));
        g_mutex_unlock (&src->fdmaplock);

        if (fds[i] < 0) {
          GST_ERROR_OBJECT (src, "Unable to get fd; Received value: %d", fds[i]);
          return GST_FLOW_ERROR;
        }
      }
      else {
        g_mutex_lock (&src->fdmaplock);
        g_hash_table_insert (src->fdmap,
            GINT_TO_POINTER (pl_info.buffer_info->buf_id[i]),
            GINT_TO_POINTER (fds[i]));
        g_mutex_unlock (&src->fdmaplock);
      }

      // Wrap our buffer memory block in FD backed memory.
      gstmemory = gst_fd_allocator_alloc (allocator, fds[i], frame_pl->maxsize,
          pl_info.buffer_info->use_buffer_pool ?
          GST_FD_MEMORY_FLAG_DONT_CLOSE : GST_FD_MEMORY_FLAG_NONE);
      if (gstmemory == NULL) {
        gst_buffer_unref (gstbuffer);
        gst_object_unref (allocator);
        g_free (release_data);
        free_pl_struct (&pl_info);
        GST_ERROR_OBJECT (src, "Failed to allocate FD memory block!");
        return GST_FLOW_ERROR;
      }

      // Set the actual size filled with data.
      gst_memory_resize (gstmemory, 0, frame_pl->size);

      gst_buffer_add_video_meta_full (
        gstbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        (GstVideoFormat) frame_pl->format, frame_pl->width,
        frame_pl->height, frame_pl->n_planes,
        frame_pl->offset, frame_pl->stride);
    }

    // Append the FD backed memory to the newly created GstBuffer.
    gst_buffer_append_memory (gstbuffer, gstmemory);
  }

  if (src->mode != DATA_MODE_TEXT) {
    // Unreference the allocator so that it is owned only by the gstmemory.
    gst_object_unref (allocator);
  }

  if (pl_info.buffer_info->pts != ~0LU) {
    GST_BUFFER_PTS (gstbuffer) = pl_info.buffer_info->pts;
    GST_BUFFER_DTS (gstbuffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (gstbuffer) = pl_info.buffer_info->duration;

    if (GST_FORMAT_UNDEFINED == src->segment.format) {
      gst_segment_init (&src->segment, GST_FORMAT_TIME);
      GstPad *pad = gst_element_get_static_pad (GST_ELEMENT(src), "src");
      gst_pad_push_event (pad, gst_event_new_segment (&src->segment));
    }
  }

  // Set a notification function to signal when the buffer is no longer used.
  gst_mini_object_set_qdata (
      GST_MINI_OBJECT (gstbuffer), socket_buffer_qdata_quark (),
      release_data, (GDestroyNotify) gst_socket_src_buffer_release
  );

  for (guint i = 0; i < pl_info.protection_metadata_info->len; i++) {
    GstStructure *pmeta = NULL;
    GstTextPayload *pmeta_pl = (GstTextPayload *) g_ptr_array_index (
        pl_info.protection_metadata_info, i);

    GST_DEBUG_OBJECT (src, "Protection meta added: %s", pmeta_pl->contents);

    pmeta = gst_structure_from_string (pmeta_pl->contents, NULL);
    gst_buffer_add_protection_meta (gstbuffer, pmeta);
  }

  free_pl_struct (&pl_info);

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

    GST_DEBUG_OBJECT (src, "socket poll timeout %" GST_TIME_FORMAT ", fd: %d",
        GST_TIME_ARGS (timeout), src->client_sock);

    poll_fd.fd = src->client_sock;
    poll_fd.events = POLLIN;
    retval = poll (&poll_fd, 1, src->timeout);
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
  GstPayloadInfo msg_info = {0};
  GstMessagePayload disc_msg = { .identity = MESSAGE_DISCONNECT};
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_socket_src_flush_socket_queue (src);
      gst_socket_src_socket_release (src);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      msg_info.message = &disc_msg;
      if (send_socket_message (src->client_sock, &msg_info) < 0) {
        GST_INFO_OBJECT (src, "Unable to send disconnect message.");
      }
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

static gboolean
gst_socket_src_unlock (GstBaseSrc * bsrc)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (bsrc);
  gboolean release = FALSE;

  g_mutex_lock (&src->mutex);
  release = !src->thread_done;
  g_mutex_unlock (&src->mutex);

  if (release) {
    gst_socket_src_socket_release (src);
  }

  return TRUE;
}

static GstFlowReturn
gst_socket_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstFdSocketSrc *src = GST_SOCKET_SRC (psrc);

  GST_INFO_OBJECT (src, "Creating src out");

  g_mutex_lock (&src->mutex);

  while (!src->thread_done && !src->stop_thread) {
    GST_INFO_OBJECT (src, "Waiting for thread");
    g_cond_wait (&src->cond, &src->mutex);
  }

  if (!src->thread_done || src->stop_thread) {
    g_mutex_unlock (&src->mutex);
    return GST_FLOW_FLUSHING;
  }

  g_mutex_unlock (&src->mutex);

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
  src->thread = NULL;
  src->stop_thread = FALSE;
  src->thread_done = FALSE;
  src->mlinfo = NULL;
  src->pool = NULL;
  src->mode = DATA_MODE_NONE;

  g_cond_init (&src->cond);
  g_mutex_init (&src->mutex);

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
        GST_PARAM_MUTABLE_READY | G_PARAM_CONSTRUCT));

  gst_element_class_set_static_metadata (gstelement,
      "QTI Socket Source Element", "Socket Source Element",
      "This plugin receive GST buffer over Unix Domain Socket", "QTI");

  gst_element_class_add_static_pad_template (gstelement, &socket_src_template);

  gstbasesrc->start = GST_DEBUG_FUNCPTR (gst_socket_src_start);
  gstbasesrc->query = GST_DEBUG_FUNCPTR (gst_socket_src_query);
  gstbasesrc->unlock = GST_DEBUG_FUNCPTR (gst_socket_src_unlock);

  gstpush_src->create = GST_DEBUG_FUNCPTR (gst_socket_src_create);
  gstelement->change_state = GST_DEBUG_FUNCPTR (gst_socket_src_change_state);
  gstbasesrc->set_caps = GST_DEBUG_FUNCPTR (gst_socket_src_set_caps);
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
