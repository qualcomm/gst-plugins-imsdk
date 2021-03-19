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
*/

#include "qtifdsocket.h"

gint send_fd_message (gint sock, void * payload, gint psize, gint fd)
{
  struct cmsghdr *cmsg = NULL;
  struct msghdr msg = {0};
  struct iovec io = { .iov_base = payload, .iov_len = psize };
  gchar buf[CMSG_SPACE (sizeof (fd))] = {0};

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;

  if (fd >= 0) {
    msg.msg_control = buf;
    msg.msg_controllen = sizeof (buf);

    cmsg = CMSG_FIRSTHDR (&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN (sizeof (fd));

    memmove (CMSG_DATA (cmsg), &fd, sizeof (fd));

    msg.msg_controllen = CMSG_SPACE (sizeof (fd));
  }

  if (sendmsg (sock, (struct msghdr *) &msg, 0) <= 0)
    return -1;

  return 0;
}

gint receive_fd_message (gint sock, void * payload, gint psize, gint * fd)
{
  struct msghdr msg = {0};
  struct iovec io = { .iov_base = payload, .iov_len = psize };
  gchar buf[CMSG_SPACE (sizeof (*fd))] = {0};

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;

  if (fd != NULL) {
    msg.msg_control = buf;
    msg.msg_controllen = sizeof (buf);
  }

  if (recvmsg (sock, &msg, 0) <= 0)
    return -1;

  if (fd != NULL) {
    struct cmsghdr *cmsg = CMSG_FIRSTHDR (&msg);
    memmove (fd, CMSG_DATA (cmsg), sizeof (*fd));
  }
  return 0;
}
