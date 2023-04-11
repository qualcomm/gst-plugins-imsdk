/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_C2_COMPONENT_H__
#define __GST_C2_COMPONENT_H__

#include <map>
#include <mutex>
#include <condition_variable>

#include <C2Config.h>
#include <C2Component.h>
#include <C2PlatformSupport.h>
#include <C2Buffer.h>
#include <C2AllocatorGBM.h>
#include <C2BlockInternal.h>
#include <gbm_priv.h>

#include "common.h"

#define ALIGN(num, to) (((num) + (to - 1)) & (~(to - 1)))

typedef void (*event_handler_cb)(
    GstC2EventType type, void *userdata, void *userdata2);

class C2ComponentWrapper {
public:
  C2ComponentWrapper(std::shared_ptr<C2ComponentStore> compstore,
                     const char* name);
  ~C2ComponentWrapper();

  bool SetHandler(event_handler_cb callback, gpointer userdata);
  gint GetBlockPoolId();
  bool Config(GPtrArray* config);
  bool Start();
  bool Stop();
  bool Queue(BufferDescriptor * buffer);
  bool FreeOutputBuffer(uint64_t buf_idx);
  c2_status_t CreateBlockpool(C2BlockPool::local_id_t type);
  std::map<uint64_t, std::shared_ptr<C2Buffer>> out_pending_buffers_;
  std::mutex out_pending_buffer_lock_;
  std::condition_variable condition_;

private:
  C2FrameData::flags_t ToC2Flag(GstC2Flag flag);
  guint32 ToGBMFormat(GstVideoFormat format, bool isubwc);
  c2_status_t CheckMaxAvailableQueues ();
  c2_status_t PrepareC2Buffer(BufferDescriptor* buffer,
                              std::shared_ptr<C2Buffer>* c2buffer);
  c2_status_t WaitForProgressOrStateChange(uint32_t max_pending_works,
                                           uint32_t timeout);

  std::shared_ptr<C2Component> component_;
  std::shared_ptr<C2ComponentInterface> compintf_;
  uint32_t n_pending_works_;
  std::mutex lock_;
  std::condition_variable workcondition_;
  std::shared_ptr<C2BlockPool> linear_pool_;
  std::shared_ptr<C2BlockPool> graphic_pool_;
  std::shared_ptr<C2BlockPool> out_graphic_pool_;

  friend class C2ComponentListener;
};

class EventCallback {
public:
  EventCallback(const gpointer userdata, event_handler_cb cb);

  void OnOutputBufferAvailable(const std::shared_ptr<C2Buffer> buffer,
      guint64 index, guint64 timestamp, C2FrameData::flags_t flag,
      gpointer userdata);
  void OnTripped(guint error, gpointer userdata);
  void OnError(guint error, gpointer userdata);

private:
  event_handler_cb callback_;
  const gpointer userdata_;
};

class C2ComponentListener : public C2Component::Listener {
public:
  C2ComponentListener(std::shared_ptr<C2Component> comp,
                      EventCallback * callback, void * userdata);

  void onWorkDone_nb(std::weak_ptr<C2Component> component,
                     std::list<std::unique_ptr<C2Work>> works) override;
  void onTripped_nb(std::weak_ptr<C2Component> component,
                    std::vector<std::shared_ptr<C2SettingResult>> results) override;
  void onError_nb(std::weak_ptr<C2Component> component,
                  uint32_t errorCode) override;

private:
  std::shared_ptr<C2Component> comp_;
  EventCallback *callback_;
  void *userdata_;
};

class C2VencBuffWrapper : public C2GraphicAllocation {
public:
  C2VencBuffWrapper (uint32_t width, uint32_t height,
      C2Allocator::id_t allocator_id, android::C2HandleGBM * handle);

  c2_status_t map (C2Rect rect, C2MemoryUsage usage, C2Fence * fence,
      C2PlanarLayout * layout, uint8_t ** addr) override;
  c2_status_t unmap (uint8_t ** addr, C2Rect rect, C2Fence * fence) override;

  const C2Handle *handle () const override;
  id_t getAllocatorId () const override;
  bool equals (
      const std::shared_ptr<const C2GraphicAllocation> &other) const override;

private:
  android::C2HandleGBM *handle_;
  void *base_;
  size_t mapsize_;
  struct gbm_bo *bo_;
  C2Allocator::id_t allocator_id_;
};

#endif // __GST_C2_COMPONENT_H__
