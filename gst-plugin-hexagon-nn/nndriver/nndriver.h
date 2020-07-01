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

#ifndef NNDRIVER_H
#define NNDRIVER_H

#include <mutex>

#include <inttypes.h>
#include <utils/Log.h>

#include "hexagon_nn.h"

class NNDriver {
public:
  virtual int32_t Init(
      uint8_t**   input_buf,
      int32_t     width,
      int32_t     height,
      int32_t     num_outputs,
      int32_t*    out_sizes,
      std::string &lib_name) = 0;

  virtual int32_t Process(
      uint8_t*  in_buffer,
      void**    outs) = 0;

  virtual void DeInit() = 0;
};

class NNDriverHVX : public NNDriver {
public:

  NNDriverHVX() : handle_(-1),
                  graph_id_(0),
                  out_sizes_{},
                  nn_out_bufs_{},
                  nn_in_buf_{} {};

  int32_t Init(
      uint8_t**   input_buf,
      int32_t     width,
      int32_t     height,
      int32_t     num_outputs,
      int32_t*    out_sizes,
      std::string &lib_name) override;

  int32_t Process(
      uint8_t*  in_buffer,
      void**    outs) override;

  void DeInit() override;

private:

  uint32_t GraphSetup(std::string &lib_name);

  static const int32_t       kMaxOut          = 4;
  static const uint32_t      kIonHeapIdSystem = 25;
  static const char          kURI[];

  remote_handle64            handle_;
  uint32_t                   graph_id_;
  int32_t                    width_;
  int32_t                    height_;
  int32_t                    num_outputs_;
  int32_t                    out_sizes_[kMaxOut];
  void*                      nn_out_bufs_[kMaxOut];
  uint8_t*                   nn_in_buf_;

  hexagon_nn_tensordef       in_tensor_;
  hexagon_nn_tensordef       out_tensors_[kMaxOut];

};

#endif // NNDRIVER_H
