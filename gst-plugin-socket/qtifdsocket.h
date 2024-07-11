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

typedef struct _GstFdMessage GstFdMessage;
typedef struct _GstNewFrameMgs GstNewFrameMgs;
typedef struct _GstReturnFrameMsg GstReturnFrameMsg;
typedef struct _GstTensorMgs GstTensorMgs;

struct __attribute__((packed, aligned(4))) _GstNewFrameMgs
{
  gint buf_id;
  gint width;
  gint height;
  gsize size;
  gsize maxsize;
  gint32 format;
  gint n_planes;
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  guint flags;
  guint64 timestamp;
  gboolean use_buffer_pool;
};

struct __attribute__((packed, aligned(4))) _GstTensorMgs
{
  gint buf_id;
  gsize size;
  gsize maxsize;
  GstMLTensorMeta meta;
  guint64 timestamp;
};

struct __attribute__((packed, aligned(4))) _GstReturnFrameMsg
{
  gint buf_id;
};

struct __attribute__((packed, aligned(4))) _GstFdMessage
{
  gint id;
  union
  {
    // EMPTY for MESSAGE_EOS
    // EMPTY for MESSAGE_DISCONNECT
    GstNewFrameMgs new_frame; // MESSAGE_NEW_FRAM
    GstTensorMgs tensor_frame; // MESSAGE_NEW_TENSOR_FRAME
    GstReturnFrameMsg return_frame; // MESSAGE_RETURN_FRAME
  };
};

enum
{
  MESSAGE_EOS,
  MESSAGE_DISCONNECT,
  MESSAGE_NEW_FRAME,
  MESSAGE_NEW_TENSOR_FRAME,
  MESSAGE_RETURN_FRAME
};

gint send_fd_message (gint sock, void * payload, gint psize, gint fd);
gint receive_fd_message (gint sock, void * payload, gint psize, gint * fd);

#endif  // __GST_QTI_SOCKET_H__