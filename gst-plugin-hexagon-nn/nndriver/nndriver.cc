/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#include <dlfcn.h>
#include <string.h>

#include "rpcmem.h"

#include "nndriver.h"

const char NNDriverHVX::kURI[] =
  "file:///libhexagon_nn_skel_dp.so?hexagon_nn_domains_skel_handle_invoke&_modver=1.0&_dom=cdsp";

uint32_t NNDriverHVX::GraphSetup(std::string &lib_name)
{
  int32_t ret = 0;
  uint32_t id = 0;

  dlerror();

  void *libptr = dlopen(lib_name.c_str(), RTLD_LAZY);
  auto err = dlerror();
  if (err) {
    ALOGE("%s: dlopen failed: %s", __func__, dlerror());
    return id;
  }

  void (*init_graph)(remote_handle64 handle, int nn_id);

  *(void **)&(init_graph) = dlsym(libptr, "init_graph");
  err = dlerror();
  if (err) {
    ALOGE("dlsym failed: %s", err);
    dlclose(libptr);
    return id;
  }

  ret = hexagon_nn_domains_init(handle_, (hexagon_nn_nn_id *)(&id));
  if (0 == ret) {
    ret = hexagon_nn_domains_set_powersave_level(handle_, 0);
    if (0 == ret) {
      hexagon_nn_domains_set_debug_level(handle_, id, 0);

      init_graph(handle_, id);

      ret = hexagon_nn_domains_prepare(handle_, id);
      if (0 != ret) {
        ALOGE(" hexagon_nn_domains_prepare failed: %d\n", ret);
        id = 0;
      }
    } else {
      ALOGE(" hexagon_nn_domains_set_powersave_level failed: %d\n", ret);
    }
  } else {
    ALOGE(" hexagon_nn_domains_init failed: %d\n", ret);
  }

  dlclose(libptr);

  return id;
}

int32_t NNDriverHVX::Process(
    uint8_t* in_buffer,
    void**   outs)
{
  int32_t ret = hexagon_nn_domains_execute_new(handle_, graph_id_, &in_tensor_,
      1, &out_tensors_[0], num_outputs_);
  if (0 == ret) {
    for (int32_t i = 0; i < num_outputs_; i++) {
      outs[i] = nn_out_bufs_[i];
    }
  }

  return ret;
}

int32_t NNDriverHVX::Init(
    uint8_t     **input_buf,
    int32_t     width,
    int32_t     height,
    int32_t     num_outputs,
    int32_t     *out_sizes,
    std::string &lib_name)
{
  int32_t ret = 0;

  width_ = width;
  height_ = height;
  num_outputs_ = num_outputs;

  nn_in_buf_ = static_cast<uint8_t *>(rpcmem_alloc(kIonHeapIdSystem,
      RPCMEM_DEFAULT_FLAGS, (height_ * width_ * 3)));
  *input_buf = nn_in_buf_;

  if (NULL == nn_in_buf_) {
    ALOGE(" RPC buffer allocation failed\n");
    ret = -1;
  }

  for (int32_t i = 0; i < num_outputs_; i++) {
    out_sizes_[i] = out_sizes[i];
    nn_out_bufs_[i] = static_cast<void *>(
        rpcmem_alloc(kIonHeapIdSystem, RPCMEM_DEFAULT_FLAGS, out_sizes_[i]));
    if (NULL == nn_out_bufs_[i]) {
      ALOGE(" RPC buffer allocation failed\n");
      ret = -1;
      break;
    }
  }

  if (0 == ret) {
    hexagon_nn_domains_open(kURI, &handle_);

    struct remote_rpc_control_latency data;
    data.enable = 1;
    remote_handle64_control(handle_, DSPRPC_CONTROL_LATENCY, (void*)&data,
        sizeof(data));

    hexagon_nn_domains_config(handle_);

    graph_id_ = GraphSetup(lib_name);
    if (0 == graph_id_) {
      ALOGE(" Graph setup failed\n");
      ret = -1;
    } else {
      ALOGD(" NNDriver_Init success\n");
    }
  }

  memset(&in_tensor_, 0x0, sizeof(hexagon_nn_tensordef));
  memset(&out_tensors_[0], 0x0, kMaxOut * sizeof(hexagon_nn_tensordef));

  in_tensor_.height = height_;
  in_tensor_.width = width_;
  in_tensor_.depth = 3;
  in_tensor_.batches = 1;
  in_tensor_.data = nn_in_buf_;
  in_tensor_.dataLen = (width_ * height_ * 3);
  in_tensor_.data_valid_len = (width_ * height_ * 3);

  for (int32_t i = 0; i < num_outputs_; i++) {
    out_tensors_[i].data = (uint8_t*)(nn_out_bufs_[i]);
    out_tensors_[i].dataLen = out_sizes_[i];
  }

  if (ret < 0) {
    if (NULL != nn_in_buf_) {
      rpcmem_free(nn_in_buf_);
      nn_in_buf_ = nullptr;
    }

    for (int32_t i = 0; i < num_outputs_; i++) {
      if (NULL != nn_out_bufs_[i]) {
        rpcmem_free(nn_out_bufs_[i]);
        nn_out_bufs_[i] = nullptr;
      }
    }
  }

  return ret;
}

void NNDriverHVX::DeInit()
{
  if (0 != graph_id_) {
    hexagon_nn_domains_set_powersave_level(handle_, 255);
    hexagon_nn_domains_teardown(handle_, graph_id_);
    hexagon_nn_domains_close(handle_);
  }

  if (NULL != nn_in_buf_) {
    rpcmem_free(nn_in_buf_);
    nn_in_buf_ = nullptr;
  }

  for (int32_t i = 0; i < num_outputs_; i++) {
    if (NULL != nn_out_bufs_[i]) {
      rpcmem_free(nn_out_bufs_[i]);
      nn_out_bufs_[i] = nullptr;
    }
  }
}
