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

#ifndef __GST_QTI_SOCKET_H__
#define __GST_QTI_SOCKET_H__

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include <gst/video/gstvideometa.h>
#include <gst/ml/gstmlmeta.h>

#define GST_SOCKET_MSG_IDENTITY(payload) \
    (((GstMessagePayload *)(payload))->identity)
#define GST_PL_INFO_IS_MESSAGE(pl_info, msg_id)  \
    (((GstPayloadInfo *)(pl_info))->message != NULL) && \
    (((GstPayloadInfo *)(pl_info))->message->identity == msg_id)
#define GST_PL_INFO_GET_N_FDS(pl_info) \
    (((GstPayloadInfo *)(pl_info))->fd_count != NULL) ? \
    (((GstPayloadInfo *)(pl_info))->fd_count->n_fds) : 0

#define GST_EXPECTED_MEM_BLOCKS(socket) \
    (socket->mode == DATA_MODE_TENSOR ? socket->mlinfo->n_tensors : \
    (socket->mode == DATA_MODE_TEXT || socket->mode == DATA_MODE_VIDEO ? 1 : 0))

#define GST_MAX_MEM_BLOCKS 10

typedef enum {
  DATA_MODE_NONE,
  DATA_MODE_VIDEO,
  DATA_MODE_TENSOR,
  DATA_MODE_TEXT
} GstFdSocketDataType;

typedef struct _GstMessagePayload GstMessagePayload;
typedef struct _GstBufferPayload GstBufferPayload;
typedef struct _GstFramePayload GstFramePayload;
typedef struct _GstTensorPayload GstTensorPayload;
typedef struct _GstTextPayload GstTextPayload;
typedef struct _GstReturnBufferPayload GstReturnBufferPayload;
typedef struct _GstFdCountPayload GstFdCountPayload;
typedef struct _GstPayloadInfo GstPayloadInfo;
typedef struct _GstProtectionMetadataPayload GstProtectionMetadataPayload;

struct __attribute__((packed, aligned(4))) _GstMessagePayload {
  guint32 identity; // Message identity / type
};

struct __attribute__((packed, aligned(4))) _GstBufferPayload {
  guint32  identity; // Message identity / type
  gint     buf_id[GST_MAX_MEM_BLOCKS];
  guint64  pts;
  guint64  dts;
  guint64  duration;
  gboolean use_buffer_pool;
};

struct __attribute__((packed, aligned(4))) _GstFramePayload {
  guint32 identity; // Message identity / type
  guint   width;
  guint   height;
  guint32 format;
  guint   n_planes;
  gsize   offset[GST_VIDEO_MAX_PLANES];
  gint    stride[GST_VIDEO_MAX_PLANES];
  guint   flags;

  gsize   size;
  gsize   maxsize;
};

struct __attribute__((packed, aligned(4))) _GstTensorPayload {
  guint32 identity; // Message identity / type
  guint32 type;
  guint   n_dimensions;
  guint   dimensions[GST_ML_TENSOR_MAX_DIMS];

  gsize   size;
  gsize   maxsize;
};

struct __attribute__((packed, aligned(4))) _GstTextPayload {
  guint32 identity; // Message identity / type
  gchar   contents[1024];

  gsize   size;
  gsize   maxsize;
};

struct __attribute__((packed, aligned(4))) _GstReturnBufferPayload {
  guint32 identity; // Message identity / type
  gint    buf_id[GST_MAX_MEM_BLOCKS];
};

//Used only in send and receive functions
struct __attribute__((packed, aligned(4))) _GstFdCountPayload {
  guint32 identity; // Message identity / type
  gint    n_fds;
};

struct __attribute__((packed, aligned(4))) _GstProtectionMetadataPayload {
  guint32 identity; // Message identity / type
  gchar   contents[1024];

  gsize   size;
  gsize   maxsize;
};

// Struct used to pass info to send and receive functions.
// message carries a message (EOS/DISCONNECT).
// buffer_info is the buffer description (BUFFER).
// return_buffer carries the id of the return buffer (RETURN_BUFFER).
// mem_block_info carries fd/non-fd memory info (FRAME/TENSOR/TEXT).
struct __attribute__((packed, aligned(4))) _GstPayloadInfo {
  GstMessagePayload *      message;
  GstBufferPayload *       buffer_info;
  GstReturnBufferPayload * return_buffer;
  GstFdCountPayload *      fd_count;
  gint *                   fds;
  GPtrArray *              mem_block_info;
  GPtrArray *              protection_metadata_info;
};

// List of message identities
enum
{
  MESSAGE_EOS,            //0
  MESSAGE_DISCONNECT,     //1
  MESSAGE_BUFFER_INFO,    //2
  MESSAGE_FRAME,          //3
  MESSAGE_TENSOR,         //4
  MESSAGE_TEXT,           //5
  MESSAGE_RETURN_BUFFER,  //6
  MESSAGE_FD_COUNT,       //7
  MESSAGE_PROTECTION_META //8
};

void
free_pl_struct (GstPayloadInfo * pl_info);
gint
send_socket_message (gint sock, GstPayloadInfo * pl_info);
gint
receive_socket_message (gint sock, GstPayloadInfo * pl_info, int msg_flags);
gint
get_payload_size (gpointer payload);
#endif  // __GST_QTI_SOCKET_H__
