/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <chrono>
#include <deque>
#include <vector>

namespace ipc {
// Simple length-prefixed protocol over a stream socket.
// All integers are little-endian on the wire (same host/arch in this use-case).
enum : uint32_t {
  MSG_REQUEST  = 1,
  MSG_RESPONSE = 2,
};

struct ReqHeader {
  uint32_t type;
  uint32_t reserved;
  uint64_t client_id;
  uint32_t assistant_len;
  uint32_t question_len;
  uint32_t buf_len;
  float temperature;
  float top_p;
  float seed;
  float presence_penalty;
  float frequency_penalty;
  void *userdata;
};

struct RespHeader {
  uint32_t type;
  uint32_t ok;
  uint64_t client_id;
  uint32_t text_len;
  uint32_t err_len;
  void *userdata;
};

static bool write_all(int fd, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (len) {
    ssize_t w = ::write(fd, p, len);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (w == 0) return false;
    p += static_cast<size_t>(w);
    len -= static_cast<size_t>(w);
  }
  return true;
}

static bool read_all(int fd, void* data, size_t len) {
  uint8_t* p = static_cast<uint8_t*>(data);
  while (len) {
    ssize_t r = ::read(fd, p, len);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (r == 0) return false;
    p += static_cast<size_t>(r);
    len -= static_cast<size_t>(r);
  }
  return true;
}

static void set_fd_cloexec(int fd, bool cloexec) {
  int flags = fcntl(fd, F_GETFD);
  if (flags < 0) return;
  if (cloexec) flags |= FD_CLOEXEC;
  else flags &= ~FD_CLOEXEC;
  fcntl(fd, F_SETFD, flags);
}

static bool set_fd_nonblock(int fd, bool nonblock) {
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0) return false;
  if (nonblock) flags |= O_NONBLOCK;
  else flags &= ~O_NONBLOCK;
  return (fcntl(fd, F_SETFL, flags) == 0);
}
} // namespace ipc
