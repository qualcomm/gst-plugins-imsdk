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

#include "IpcProtocol.h"

// Non-blocking client-side IPC stream (buffered framing)
struct IpcStream {
  int fd{-1};
  std::vector<uint8_t> in;
  std::vector<uint8_t> out;

  bool enqueue_request(uint64_t client_id,
                       const std::vector<uint8_t>& buf,
                       const std::string& assistant,
                       const std::string& question,
                       float temperature,
                       float top_p,
                       float seed,
                       float presence_penalty,
                       float frequency_penalty,
                       void *userdata) {
    ipc::ReqHeader hdr{};
    hdr.type = ipc::MSG_REQUEST;
    hdr.client_id = client_id;
    hdr.assistant_len = static_cast<uint32_t>(assistant.size());
    hdr.question_len = static_cast<uint32_t>(question.size());
    hdr.buf_len = static_cast<uint32_t>(buf.size());
    hdr.temperature = temperature;
    hdr.top_p = top_p;
    hdr.seed = seed;
    hdr.presence_penalty = presence_penalty;
    hdr.frequency_penalty = frequency_penalty;
    hdr.userdata = userdata;

    const size_t start = out.size();
    const size_t total = sizeof(hdr) + assistant.size() + question.size() + buf.size();
    out.resize(start + total);
    uint8_t* p = out.data() + start;
    memcpy(p, &hdr, sizeof(hdr));
    p += sizeof(hdr);
    if (!assistant.empty()) {
      memcpy(p, assistant.data(), assistant.size());
      p += assistant.size();
    }
    if (!question.empty()) {
      memcpy(p, question.data(), question.size());
      p += question.size();
    }
    if (!buf.empty()) {
      memcpy(p, buf.data(), buf.size());
      p += buf.size();
    }
    return true;
  }

  // Try to flush queued outgoing bytes (non-blocking). Returns false on fatal error.
  bool flush_out() {
    while (!out.empty()) {
      ssize_t w = ::write(fd, out.data(), out.size());
      if (w < 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
      }
      if (w == 0) return false;
      out.erase(out.begin(), out.begin() + static_cast<size_t>(w));
    }
    return true;
  }

  // Read as much as available (non-blocking). Returns false on fatal error.
  bool pump_in() {
    uint8_t tmp[64 * 1024];
    while (true) {
      ssize_t r = ::read(fd, tmp, sizeof(tmp));
      if (r < 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
      }
      if (r == 0) return false; // closed
      in.insert(in.end(), tmp, tmp + r);
    }
  }

  // Try to pop one complete response. Returns true if a response was popped.
  // Returns false if not enough data OR protocol error (errno=EPROTO).
  bool try_pop_response(uint64_t& client_id, bool& ok, std::string& text, std::string& err, void **userdata) {
    if (in.size() < sizeof(ipc::RespHeader)) return false;
    ipc::RespHeader hdr{};
    memcpy(&hdr, in.data(), sizeof(hdr));
    if (hdr.type != ipc::MSG_RESPONSE) {
      errno = EPROTO;
      return false;
    }
    const size_t need = sizeof(hdr) + hdr.text_len + hdr.err_len;
    if (in.size() < need) return false;

    client_id = hdr.client_id;
    ok = (hdr.ok != 0);
    if (userdata) {
      *userdata = hdr.userdata;
    }
    text.assign(reinterpret_cast<const char*>(in.data() + sizeof(hdr)), hdr.text_len);
    err.assign(reinterpret_cast<const char*>(in.data() + sizeof(hdr) + hdr.text_len), hdr.err_len);
    in.erase(in.begin(), in.begin() + need);
    return true;
  }
};
