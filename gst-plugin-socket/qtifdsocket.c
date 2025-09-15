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

#include <arpa/inet.h>
#include <stdint.h>

#include "qtifdsocket.h"

void
free_pl_struct (GstPayloadInfo * pl_info)
{
  if (pl_info->fd_count != NULL) {
    g_free (pl_info->fd_count);
    pl_info->fd_count = NULL;
  }

  if (pl_info->message != NULL) {
    g_free (pl_info->message);
    pl_info->message = NULL;
  }

  if (pl_info->buffer_info != NULL) {
    g_free (pl_info->buffer_info);
    pl_info->buffer_info = NULL;
  }

  if (pl_info->return_buffer != NULL) {
    g_free (pl_info->return_buffer);
    pl_info->return_buffer = NULL;
  }

  if (pl_info->mem_block_info != NULL) {
    g_ptr_array_free (pl_info->mem_block_info, TRUE);
    pl_info->mem_block_info = NULL;
  }

  if (pl_info->protection_metadata_info != NULL) {
    g_ptr_array_free (pl_info->protection_metadata_info, TRUE);
    pl_info->protection_metadata_info = NULL;
  }

  if (pl_info->roi_meta_info != NULL) {
    g_ptr_array_free (pl_info->roi_meta_info, TRUE);
    pl_info->roi_meta_info = NULL;
  }

  if (pl_info->class_meta_info != NULL) {
    g_ptr_array_free (pl_info->class_meta_info, TRUE);
    pl_info->class_meta_info = NULL;
  }

  if (pl_info->lm_meta_info != NULL) {
    g_ptr_array_free (pl_info->lm_meta_info, TRUE);
    pl_info->lm_meta_info = NULL;
  }
}

gint
send_socket_message (gint sock, GstPayloadInfo * pl_info)
{
  struct cmsghdr *cmsg = NULL;
  GPtrArray * send_arr = g_ptr_array_new ();
  struct msghdr msg = {0};
  gchar buf[CMSG_SPACE (sizeof (*pl_info->fds) * GST_PL_INFO_GET_N_FDS (pl_info))];

  uint32_t payload_len = 0, msg_len_net;
  gint data_size = 0;

  memset (buf, 0, sizeof (buf));

  if (pl_info->fd_count != NULL)
    g_ptr_array_add (send_arr, pl_info->fd_count);

  if (pl_info->buffer_info != NULL)
    g_ptr_array_add (send_arr, pl_info->buffer_info);

  if (pl_info->message != NULL)
    g_ptr_array_add (send_arr, pl_info->message);

  if (pl_info->return_buffer != NULL)
    g_ptr_array_add (send_arr, pl_info->return_buffer);

  if (pl_info->mem_block_info != NULL) {
    for (guint i = 0; i < pl_info->mem_block_info->len; i++) {
      g_ptr_array_add (send_arr, g_ptr_array_index (pl_info->mem_block_info, i));
    }
  }

  if (pl_info->protection_metadata_info != NULL) {
    for (guint i = 0; i < pl_info->protection_metadata_info->len; i++) {
      g_ptr_array_add (send_arr, g_ptr_array_index (pl_info->protection_metadata_info, i));
    }
  }

  if (pl_info->roi_meta_info != NULL) {
    for (guint i = 0; i < pl_info->roi_meta_info->len; i++) {
      g_ptr_array_add (send_arr, g_ptr_array_index (pl_info->roi_meta_info, i));
    }
  }

  if (pl_info->class_meta_info != NULL) {
    for (guint i = 0; i < pl_info->class_meta_info->len; i++) {
      g_ptr_array_add (send_arr, g_ptr_array_index (pl_info->class_meta_info, i));
    }
  }

  if (pl_info->lm_meta_info != NULL) {
    for (guint i = 0; i < pl_info->lm_meta_info->len; i++) {
      g_ptr_array_add (send_arr, g_ptr_array_index (pl_info->lm_meta_info, i));
    }
  }

  struct iovec io[send_arr->len];

  for (guint i = 0; i < send_arr->len; i++) {
    gpointer ptr = g_ptr_array_index (send_arr, i);

    GST_DEBUG ("Sending payload with msg_id: %d and size %d",
        GST_SOCKET_MSG_IDENTITY (ptr), get_payload_size (ptr));

    io[i].iov_base = ptr;
    io[i].iov_len = get_payload_size (ptr);
    payload_len += io[i].iov_len;
  }

  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  msg.msg_iov = io;
  msg.msg_iovlen = send_arr->len;

  if (pl_info->fds != NULL && pl_info->fd_count != NULL &&
      GST_PL_INFO_GET_N_FDS (pl_info) > 0) {
    msg.msg_control = buf;
    msg.msg_controllen = sizeof (buf);
    cmsg = CMSG_FIRSTHDR (&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN (
        (sizeof (*pl_info->fds)) * (GST_PL_INFO_GET_N_FDS (pl_info)));
    msg.msg_controllen = cmsg->cmsg_len;

    memmove (CMSG_DATA (cmsg), pl_info->fds, cmsg->cmsg_len);
  }

  // Send the message length first as prefix
  // Convert to network byte order, for compatibility with different host env
  msg_len_net = htonl (payload_len);
  data_size = send (sock, &msg_len_net, sizeof (msg_len_net), 0);
  if (data_size != sizeof (msg_len_net)) {
    GST_ERROR ("Failed to send message length");
    return data_size;
  }

  errno = 0;
  data_size = sendmsg (sock, &msg, 0);

  return data_size;
}

gint
receive_socket_message (gint sock, GstPayloadInfo * pl_info, int msg_flags)
{
  struct msghdr msg = {0};
  struct iovec io;
  g_autofree gchar *io_buf = NULL;
  gchar buf[CMSG_SPACE (sizeof (*pl_info->fds) * GST_MAX_MEM_BLOCKS)] = {0};
  ssize_t recv_len;
  uint32_t msg_len_net, msg_len;

  recv_len = recv (sock, &msg_len_net, sizeof (msg_len_net), msg_flags | MSG_WAITALL);
  if (recv_len != sizeof (msg_len_net)) {
      GST_ERROR ("Failed to read message length, returned %zd", recv_len);
      return recv_len;
  }

  // Convert to host byte order
  msg_len = ntohl (msg_len_net);

  // Allocate buffer for the full message
  io_buf = g_malloc (msg_len);
  if (!io_buf) {
      GST_ERROR ("Failed to allocate buffer %s", strerror (errno));
      return -1;
  }

  io.iov_base = io_buf;
  io.iov_len = msg_len;

  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;

  msg.msg_control = buf;
  msg.msg_controllen = sizeof (buf);

  errno = 0;
  recv_len = recvmsg (sock, &msg, msg_flags | MSG_WAITALL);

  if (recv_len < 0)
    return recv_len;

  for (uint32_t offset = 0; offset < recv_len; ) {
    gpointer payload;
    gpointer iterator = io_buf + offset;
    gint size = get_payload_size (iterator);

    if (size == -1) {
      break;
    }

    payload = malloc (size);
    memmove (payload, iterator, size);

    GST_DEBUG ("Received payload with msg_id %d and size %d",
        GST_SOCKET_MSG_IDENTITY (payload), size);

    switch (GST_SOCKET_MSG_IDENTITY (payload)) {
      case MESSAGE_EOS:
      case MESSAGE_DISCONNECT:
        pl_info->message = (GstMessagePayload *) payload;
        break;
      case MESSAGE_BUFFER_INFO:
        pl_info->buffer_info = (GstBufferPayload *) payload;
        break;
      case MESSAGE_FRAME:
      case MESSAGE_TENSOR:
      case MESSAGE_TEXT:
        g_ptr_array_add (pl_info->mem_block_info, payload);
        break;
      case MESSAGE_RETURN_BUFFER:
        pl_info->return_buffer = (GstReturnBufferPayload *) payload;
        break;
      case MESSAGE_FD_COUNT:
        pl_info->fd_count = (GstFdCountPayload *) payload;
        break;
      case MESSAGE_PROTECTION_META:
        g_ptr_array_add (pl_info->protection_metadata_info, payload);
        break;
      case MESSAGE_VIDEO_ROI_META:
        g_ptr_array_add (pl_info->roi_meta_info, payload);
        break;
      case MESSAGE_VIDEO_CLASS_META:
        g_ptr_array_add (pl_info->class_meta_info, payload);
        break;
      case MESSAGE_VIDEO_LM_META:
        g_ptr_array_add (pl_info->lm_meta_info, payload);
        break;
      default:
        break;
    }

    offset += size;
  }

  if ((pl_info->fds != NULL) && (pl_info->fd_count != NULL) &&
      (GST_PL_INFO_GET_N_FDS (pl_info) > 0)) {
    struct cmsghdr *cmsg = CMSG_FIRSTHDR (&msg);

    if (cmsg) {
      memmove (pl_info->fds, CMSG_DATA (cmsg),
          ((sizeof (gint)) * (GST_PL_INFO_GET_N_FDS (pl_info))));
    }
  }

  return recv_len;
}

gint
get_payload_size (gpointer payload) {
  switch (GST_SOCKET_MSG_IDENTITY (payload)) {
    case MESSAGE_EOS:
      return sizeof (GstMessagePayload);
      break;
    case MESSAGE_DISCONNECT:
      return sizeof (GstMessagePayload);
      break;
    case MESSAGE_BUFFER_INFO:
      return sizeof (GstBufferPayload);
      break;
    case MESSAGE_FRAME:
      return sizeof (GstFramePayload);
      break;
    case MESSAGE_TENSOR:
      return sizeof (GstTensorPayload);
      break;
    case MESSAGE_TEXT:
      return sizeof (GstTextPayload);
      break;
    case MESSAGE_RETURN_BUFFER:
      return sizeof (GstReturnBufferPayload);
      break;
    case MESSAGE_FD_COUNT:
      return sizeof (GstFdCountPayload);
      break;
    case MESSAGE_PROTECTION_META:
      return sizeof (GstProtectionMetadataPayload);
      break;
    case MESSAGE_VIDEO_ROI_META:
      return sizeof (GstVideoRoiMetaPayload);
      break;
    case MESSAGE_VIDEO_CLASS_META:
      return sizeof (GstVideoClassMetaPayload);
      break;
    case MESSAGE_VIDEO_LM_META:
      return sizeof (GstVideoLmMetaPayload);
      break;

    default:
      return -1;
  }
}
