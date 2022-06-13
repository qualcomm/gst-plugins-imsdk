/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "c2-component.h"

#include <vidc/media/msm_media_info.h>

#define MAX_PENDING_WORK 6

#define GST_CAT_DEFAULT gst_c2_venc_context_debug_category ()
static GstDebugCategory *
gst_c2_venc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtic2venc", 0,
        "C2 encoder context");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

std::shared_ptr<C2Buffer> createLinearBuffer(const std::shared_ptr<C2LinearBlock>& block)
{
  return C2Buffer::CreateLinearBuffer(block->share(block->offset(), block->size(), ::C2Fence()));
}

std::shared_ptr<C2Buffer> createGraphicBuffer(const std::shared_ptr<C2GraphicBlock>& block)
{
  return C2Buffer::CreateGraphicBuffer(block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
}


C2ComponentWrapper::C2ComponentWrapper (
    std::shared_ptr<C2ComponentStore> compstore, const char * name)
{
  c2_status_t result;

  numpendingworks_ = 0;

  result = compstore->createComponent (C2String (name), &component_);
  if ((result != C2_OK) || (component_.get () == nullptr)) {
    GST_ERROR ("Failed to create C2venc component");
    return;
  }

  compintf_ = std::shared_ptr<C2ComponentInterface>(component_->intf ());
  if (compintf_.get () == nullptr) {
    GST_ERROR ("Failed to create C2venc component interface");
    return;
  }
}

C2ComponentWrapper::~C2ComponentWrapper ()
{
  out_pending_buffers_.clear ();
}

bool
C2ComponentWrapper::SetHandler (event_handler_cb callback, gpointer userdata)
{
  if (component_.get () == nullptr) {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }

  EventCallback *clbk = new EventCallback (userdata, callback);
  std::shared_ptr<C2Component::Listener> listener =
      std::make_shared<C2ComponentListener> (component_, clbk, this);

  if (component_->setListener_vb (listener, C2_MAY_BLOCK) != C2_OK) {
    GST_ERROR ("Failed to set component callback");
    return FALSE;
  }
  return TRUE;
}

bool
C2ComponentWrapper::Config (GPtrArray * config)
{
  if (compintf_.get () != nullptr) {
    std::vector<C2Param*> stackParams;
    std::list<std::unique_ptr<C2Param>> settings;
    c2_status_t result;
    std::vector<std::unique_ptr<C2SettingResult>> failures;

    g_ptr_array_foreach (config, push_to_settings, &settings);

    for (auto& item : settings)
      stackParams.push_back (item.get ());

    result = compintf_->config_vb (stackParams, C2_MAY_BLOCK, &failures);
    if ((C2_OK != result) || (failures.size () != 0)) {
        GST_ERROR ("Configuration failed(%d)", static_cast<int32_t> (result));
        return FALSE;
    }
  } else {
    GST_ERROR ("The component interface is not valid");
    return FALSE;
  }

  GST_TRACE ("C2venc component interface config");

  return TRUE;
}

bool
C2ComponentWrapper::Start ()
{
  if (component_.get () != nullptr) {
    component_->start ();
  } else {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }
  return TRUE;
}

bool
C2ComponentWrapper::Stop ()
{
  if (component_.get () != nullptr) {
    component_->stop ();
  } else {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }
  return TRUE;
}

c2_status_t C2ComponentWrapper::prepareC2Buffer(BufferDescriptor* buffer, std::shared_ptr<C2Buffer>* c2Buf)
{
  uint8_t* rawBuffer = buffer->data;
  uint8_t* destBuffer = nullptr;
  uint32_t frameSize = buffer->size;
  C2BlockPool::local_id_t poolType = buffer->pool_type;
  c2_status_t result = C2_OK;
  uint32_t allocSize = 0;

  if (rawBuffer == nullptr) {
    result = C2_BAD_VALUE;
  } else {
    std::shared_ptr<C2LinearBlock> linear_block;
    std::shared_ptr<C2GraphicBlock> graphic_block;

    std::shared_ptr<C2Buffer> buf;
    c2_status_t err = C2_OK;
    C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };

    if (poolType == C2BlockPool::BASIC_LINEAR) {
      allocSize = ALIGN(frameSize, 4096);
      err = mLinearPool_->fetchLinearBlock(allocSize, usage, &linear_block);
      if (err != C2_OK || linear_block == nullptr) {
        GST_ERROR("Linear pool failed to allocate input buffer of size : (%d)", frameSize);
        return C2_NO_MEMORY;
      }

      C2WriteView view = linear_block->map().get();
      if (view.error() != C2_OK) {
        GST_ERROR("C2LinearBlock::map() failed : %d", view.error());
        return C2_NO_MEMORY;
      }
      destBuffer = view.base();
      memcpy(destBuffer, rawBuffer, frameSize);
      linear_block->mSize = frameSize;
      GST_INFO("@@@ input size %d",frameSize);
      buf = createLinearBuffer(linear_block);
    } else if (poolType == C2BlockPool::BASIC_GRAPHIC) {
      err = mGraphicPool_->fetchGraphicBlock(buffer->width, buffer->height,
              gst_to_c2_gbmformat (buffer->format),
              {C2MemoryUsage::CPU_WRITE, C2MemoryUsage::CPU_READ}, &graphic_block);
      if (err != C2_OK || graphic_block == nullptr) {
        GST_ERROR("Graphic pool failed to allocate");
        return C2_NO_MEMORY;
      }

      C2GraphicView view = graphic_block->map().get();
      if (view.error() != C2_OK) {
        GST_ERROR("C2GraphicBlock::map() failed : %d", view.error());
        return C2_NO_MEMORY;
      }
      uint8_t * const *data = view.data();

      switch (buffer->format) {
        case GST_VIDEO_FORMAT_NV12: {
          uint32_t i, j, src_stride, dest_stride, height;
          uint8_t *src, *dest;

          src = rawBuffer;
          src_stride = buffer->width;
          for (i=0; i<2; i++) {
            if (0 == i){
              dest_stride = VENUS_Y_STRIDE (COLOR_FMT_NV12, buffer->width);
              height = ((buffer->size / buffer->width) / 3) * 2;
              dest = (uint8_t *)*data;
            } else {
              dest_stride = VENUS_UV_STRIDE (COLOR_FMT_NV12, buffer->width);
              height = (buffer->size / buffer->width) / 3;
              dest = (uint8_t *)*data + VENUS_Y_STRIDE (COLOR_FMT_NV12, buffer->width)
                      * VENUS_Y_SCANLINES(COLOR_FMT_NV12, buffer->height);
            }

            for (j = 0; j < height; j++) {
              memcpy (dest, src, buffer->width);
              src += src_stride;
              dest += dest_stride;
            }
          }
          break;
        }
        default:
          GST_ERROR("Unsupported format");
          return C2_BAD_VALUE;
      }

      buf = createGraphicBuffer(graphic_block);
    }

    *c2Buf = buf;
  }

  return result;
}

bool
C2ComponentWrapper::Queue (BufferDescriptor * buffer)
{
  if (component_.get() != nullptr) {
    C2FrameData::flags_t inputFrameFlag = toC2Flag (buffer->flag);
    uint64_t frame_index = buffer->index;
    uint64_t timestamp = buffer->timestamp;
    gint width = buffer->width;
    gint height = buffer->height;
    C2BlockPool::local_id_t poolType = buffer->pool_type;
    std::list<std::unique_ptr<C2Work>> workList;
    std::unique_ptr<C2Work> work = std::make_unique<C2Work> ();

    GST_INFO ("Component work queued, Frame index : %lu, Timestamp : %lu",
        frame_index, timestamp);

    work->input.flags = inputFrameFlag;
    work->input.ordinal.timestamp = timestamp;
    work->input.ordinal.frameIndex = frame_index;
    bool isEOSFrame = inputFrameFlag & C2FrameData::FLAG_END_OF_STREAM;

    work->input.buffers.clear ();

    if (!isEOSFrame) {
      if(poolType == C2BlockPool::BASIC_GRAPHIC) {
        if (buffer->fd != -1) {  //zero copy
          std::shared_ptr<C2GraphicBlock> graphic_block;

          android::C2HandleGBM *gbm_handle = new android::C2HandleGBM ();
          gbm_handle->version = android::C2HandleGBM::VERSION;
          gbm_handle->numFds = android::C2HandleGBM::NUM_FDS;
          gbm_handle->numInts = android::C2HandleGBM::NUM_INTS;
          gbm_handle->mFds.buffer_fd = buffer->fd;
          gbm_handle->mFds.meta_buffer_fd = -1;

          gbm_handle->mInts.width = buffer->width;
          gbm_handle->mInts.height = buffer->height;
          gbm_handle->mInts.stride = VENUS_Y_STRIDE (COLOR_FMT_NV12, buffer->width);
          gbm_handle->mInts.slice_height = VENUS_Y_SCANLINES (COLOR_FMT_NV12,
            buffer->height);
          gbm_handle->mInts.format = gst_to_c2_gbmformat (buffer->format);
          gbm_handle->mInts.usage_lo = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
          gbm_handle->mInts.size = buffer->size;
          // Use fd as the unique buffer id for C2Buffer
          gbm_handle->mInts.id = buffer->fd;

          std::shared_ptr<C2GraphicAllocation> alloc =
            std::make_shared<C2VencBuffWrapper> (
              buffer->width, buffer->height,
              android::C2PlatformAllocatorStore::DEFAULT_GRAPHIC, gbm_handle);
          graphic_block = _C2BlockFactory::CreateGraphicBlock (alloc);

          std::shared_ptr<C2Buffer> buf = C2Buffer::CreateGraphicBuffer (
            graphic_block->share (C2Rect(graphic_block->width (),
            graphic_block->height ()), ::C2Fence()));
          if (buf == nullptr) {
            GST_ERROR ("Graphic pool failed to allocate input buffer");
            return FALSE;
          } else {
            GST_INFO ("Graphic pool success to allocate input buffer");
          }
          work->input.buffers.emplace_back (buf);
        } else {  //copy
          GST_INFO ("graphic mem pool Queue");
          std::shared_ptr<C2Buffer> clientBuf;
          prepareC2Buffer(buffer, &clientBuf);
          work->input.buffers.emplace_back(clientBuf);
        }
      } else if (poolType == C2BlockPool::BASIC_LINEAR) {
        GST_INFO ("Linear mem pool Queue");
        std::shared_ptr<C2Buffer> clientBuf;
        prepareC2Buffer(buffer, &clientBuf);
        work->input.buffers.emplace_back(clientBuf);
      }
    } else {
      GST_INFO ("queue EOS frame");
    }

    work->worklets.clear ();
    work->worklets.emplace_back (new C2Worklet);
    workList.push_back (std::move (work));

    if (!isEOSFrame) {
      // If pending works reach maximum, CheckMaxAvailableQueues will wait
      // and no more buffer will be queued to the component.
      CheckMaxAvailableQueues ();
    } else {
      GST_INFO ("EOS reached");
    }

    if (C2_OK != component_->queue_nb (&workList)) {
      GST_ERROR ("Failed to queue work");
    } else {
      GST_INFO ("Success queued buffer");
    }

    std::unique_lock<std::mutex> ul (lock_);
    numpendingworks_++;
  } else {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }

  GST_INFO ("C2venc component queue");

  return TRUE;
}

bool
C2ComponentWrapper::FreeOutputBuffer(uint64_t bufferIdx)
{
  std::map<uint64_t, std::shared_ptr<C2Buffer> >::iterator it;

  std::unique_lock<std::mutex> ul (out_pending_buffer_lock_);
  it = out_pending_buffers_.find (bufferIdx);
  if (it != out_pending_buffers_.end ()) {
    out_pending_buffers_.erase (it);
  } else {
    GST_INFO ("Buffer index(%lu) not found", bufferIdx);
    return FALSE;
  }

  return TRUE;
}

C2FrameData::flags_t
C2ComponentWrapper::toC2Flag (FLAG_TYPE flag)
{
  uint32_t result = 0;

  if (FLAG_TYPE_DROP_FRAME & flag) {
    result |= C2FrameData::FLAG_DROP_FRAME;
  }
  if (FLAG_TYPE_END_OF_STREAM & flag) {
    result |= C2FrameData::FLAG_END_OF_STREAM;
  }
  if (FLAG_TYPE_INCOMPLETE & flag) {
    result |= C2FrameData::FLAG_INCOMPLETE;
  }
  if (FLAG_TYPE_CODEC_CONFIG & flag) {
    result |= C2FrameData::FLAG_CODEC_CONFIG;
  }

  return static_cast<C2FrameData::flags_t> (result);
}

guint32
C2ComponentWrapper::gst_to_c2_gbmformat (GstVideoFormat format)
{
  guint32 result = 0;

  switch (format) {
  case GST_VIDEO_FORMAT_NV12:
    result = GBM_FORMAT_NV12;
    break;
  case GST_VIDEO_FORMAT_P010_10LE:
    result = GBM_FORMAT_YCbCr_420_P010_VENUS;
    break;
  default:
    GST_WARNING ("unsupported video format:%s",
      gst_video_format_to_string (format));
    break;
  }

  return result;
}

c2_status_t
C2ComponentWrapper::CheckMaxAvailableQueues ()
{
  std::unique_lock<std::mutex> ul (lock_);
  GST_DEBUG ("pending works: %d", numpendingworks_);
  while (numpendingworks_ > MAX_PENDING_WORK) {
    workcondition_.wait (ul);
  }
  return C2_OK;
}

c2_status_t C2ComponentWrapper::createBlockpool(C2BlockPool::local_id_t poolType){
  c2_status_t ret;
  if (poolType == C2BlockPool::BASIC_LINEAR) {
    ret = android::GetCodec2BlockPool(poolType, component_, &mLinearPool_);
    if (ret != C2_OK || mLinearPool_ == nullptr) {
      return ret;
    }
  } else if (poolType == C2BlockPool::BASIC_GRAPHIC) {
    ret = android::GetCodec2BlockPool(poolType, component_, &mGraphicPool_);
    if (ret != C2_OK || mGraphicPool_ == nullptr) {
      return ret;
    }
  }

  return ret;
}

C2ComponentListener::C2ComponentListener (std::shared_ptr<C2Component> comp,
      EventCallback * callback, void * userdata) :
      comp_ (comp), callback_ (callback), userdata_ (userdata)
{
}

void
C2ComponentListener::onWorkDone_nb (std::weak_ptr<C2Component> component,
    std::list<std::unique_ptr<C2Work>> workItems)
{
  C2ComponentWrapper *component_wrapper = (C2ComponentWrapper*) userdata_;

  GST_TRACE ("Component listener onWorkDone_nb");

  while (!workItems.empty ()) {
    std::unique_ptr<C2Work> work = std::move (workItems.front ());

    workItems.pop_front ();
    if (!work) {
      continue;
    }

    if (work->worklets.empty ()) {
      GST_INFO ("Component(%p) worklet empty", this);
      continue;
    }

    if (work->result == C2_NOT_FOUND) {
      GST_INFO ("No output for component(%p)", this);
      continue;
    }

    if (work->result != C2_OK) {
      GST_ERROR ("Failed to generate output for component(%p)", this);
      continue;
    }

    const std::unique_ptr<C2Worklet>& worklet = work->worklets.front ();
    std::shared_ptr<C2Buffer> buffer = nullptr;
    uint64_t bufferIdx = 0;
    C2FrameData::flags_t outputFrameFlag = worklet->output.flags;
    uint64_t timestamp = worklet->output.ordinal.timestamp.peeku ();

    if (worklet->output.buffers.size () == 1u) {
      buffer = worklet->output.buffers[0];
      bufferIdx = worklet->output.ordinal.frameIndex.peeku ();

      GST_INFO (
          "Output buffer available, Frame index : %lu, Timestamp : %lu, flag: %x",
          bufferIdx, worklet->output.ordinal.timestamp.peeku (), outputFrameFlag);

      // ref count ++
      {
        std::unique_lock<std::mutex> ul (component_wrapper->out_pending_buffer_lock_);
        component_wrapper->out_pending_buffers_[bufferIdx] = buffer;
      }

      if (callback_) {
        callback_->onOutputBufferAvailable (buffer, bufferIdx, timestamp,
            outputFrameFlag, NULL);
      }
      std::unique_lock<std::mutex> ul (component_wrapper->lock_);
      component_wrapper->numpendingworks_--;
      component_wrapper->workcondition_.notify_one ();
    } else {
      if (outputFrameFlag & C2FrameData::FLAG_END_OF_STREAM) {
        GST_INFO ("Component(%p) reached EOS on output", this);
        if (callback_) {
          callback_->onOutputBufferAvailable (NULL, bufferIdx, timestamp,
              outputFrameFlag, NULL);
        }
      } else if (outputFrameFlag & C2FrameData::FLAG_INCOMPLETE) {
        GST_INFO ("Work incomplete, means an input frame results in multiple"
            "output frames, or codec config update event");
        continue;
      } else {
        GST_ERROR ("Incorrect number of output buffers: %lu",
            worklet->output.buffers.size ());
      }
      std::unique_lock<std::mutex> ul (component_wrapper->lock_);
      component_wrapper->numpendingworks_--;
      component_wrapper->workcondition_.notify_one ();
    }
  }
}

void
C2ComponentListener::onTripped_nb (std::weak_ptr<C2Component> component,
    std::vector<std::shared_ptr<C2SettingResult>> settingResult)
{
  GST_TRACE ("Component listener onTripped_nb");

  if (callback_) {
    for (auto& f : settingResult) {
      callback_->onTripped (static_cast<uint32_t> (f->failure), NULL);
    }
  }
}

void
C2ComponentListener::onError_nb (std::weak_ptr<C2Component> component,
    uint32_t errorCode)
{
  GST_TRACE ("Component listener onError_nb");

  if (callback_) {
    callback_->onError (errorCode, userdata_);
  }
}

EventCallback::EventCallback(const gpointer userdata, event_handler_cb cb):
    userdata_ (userdata), callback_ (cb)
{
};

void
EventCallback::onOutputBufferAvailable (const std::shared_ptr<C2Buffer> buffer,
    uint64_t index, uint64_t timestamp, C2FrameData::flags_t flag,
    gpointer userdata)
{
  GST_TRACE ("onOutputBufferAvailable");
  if (!callback_) {
    GST_INFO ("Callback not set");
    return;
  }

  BufferDescriptor outBuf;
  memset (&outBuf, 0, sizeof (BufferDescriptor));
  uint32_t flag_res = 0;
  FLAG_TYPE flag_type;
  if (C2FrameData::FLAG_DROP_FRAME & flag) {
    flag_res |= FLAG_TYPE_DROP_FRAME;
  }
  if (C2FrameData::FLAG_END_OF_STREAM & flag) {
    flag_res |= FLAG_TYPE_END_OF_STREAM;
  }
  if (C2FrameData::FLAG_INCOMPLETE & flag) {
    flag_res |= FLAG_TYPE_INCOMPLETE;
  }
  if (C2FrameData::FLAG_CODEC_CONFIG & flag) {
    flag_res |= FLAG_TYPE_CODEC_CONFIG;
  }
  flag_type = static_cast<FLAG_TYPE> (flag_res);

  if (buffer) {
    C2BufferData::type_t buf_type = buffer->data ().type ();
    outBuf.timestamp = timestamp;
    outBuf.index = index;
    outBuf.flag = flag_type;

    if (buf_type == C2BufferData::LINEAR) {
      const C2ConstLinearBlock linear_block =
      buffer->data ().linearBlocks ().front ();
      const C2Handle *handle = linear_block.handle ();
      if (nullptr == handle) {
        GST_ERROR ("C2ConstLinearBlock handle is null");
        return;
      }
      outBuf.size = linear_block.size ();
      outBuf.fd = handle->data[0];
      GST_INFO ("outBuf linear fd:%d size:%d\n", outBuf.fd, outBuf.size);
      // Check for codec data
      auto csd =
          std::static_pointer_cast<const C2StreamInitDataInfo::output> (
          buffer->getInfo (C2StreamInitDataInfo::output::PARAM_TYPE));
      if (csd) {
        GST_INFO ("get codec config data, size: %lu data:%p",
        csd->flexCount (), (guint8*) csd->m.value);
        outBuf.config_data = (guint8*) &csd->m.value;
        outBuf.config_size = csd->flexCount ();
        outBuf.flag = FLAG_TYPE_CODEC_CONFIG;
      }
      callback_ (EVENT_OUTPUTS_DONE, &outBuf, userdata_);
    } else if (buf_type == C2BufferData::GRAPHIC) {
      const C2ConstGraphicBlock graphic_block =
                  buffer->data().graphicBlocks().front();
      const C2Handle* handle = graphic_block.handle();
      if (nullptr == handle) {
        GST_ERROR("C2ConstGraphicBlock handle is null");
        return;
      }
      outBuf.fd = handle->data[0];
      guint32 stride = 0;
      guint64 usage = 0;
      guint32 size = 0;
      guint32 format = 0;
      guint64 bo = 0;
      guint32 width = 0;
      guint32 height = 0;
      C2Rect crop;
      const C2GraphicView view = graphic_block.map().get();

      _UnwrapNativeCodec2GBMMetadata(handle, &width, &height, &format, &usage, &stride, &size);

      outBuf.size = size;
      /* The actual value of bo here is a pointer to struct gbm_bo.
        * To avoid including GBM header, use void* instead. */
      //outBuf.gbm_bo = reinterpret_cast<void*>(bo);
      crop = view.crop();
      GST_INFO("get crop info (%d,%d) [%dx%d] ", crop.left, crop.top, crop.width, crop.height);
      outBuf.width = crop.width;
      outBuf.height = crop.height;
      outBuf.stride = stride;
      callback_ (EVENT_OUTPUTS_DONE, &outBuf, userdata_);
      GST_INFO("out buffer size:%d width:%d height:%d stride:%d data:%p\n",
          size, width, height, stride, outBuf.data);
    } else {
      GST_ERROR ("Not supported output buffer type!");
    }
  } else if (flag & C2FrameData::FLAG_END_OF_STREAM) {
    GST_INFO ("Mark EOS buffer");
    //outBuf.data = NULL;
    outBuf.fd = -1;
    outBuf.size = 0;
    outBuf.timestamp = 0;
    outBuf.index = 0;
    outBuf.flag = flag_type;
    callback_ (EVENT_OUTPUTS_DONE, &outBuf, userdata_);
  } else {
    GST_INFO ("Buffer is null");
  }
}

void
EventCallback::onTripped (uint32_t errorCode, gpointer userdata)
{
  GST_TRACE ("onTripped");
  if (!callback_) {
    GST_INFO ("Callback not set in CodecCallback(%p)", this);
    return;
  }
  callback_ (EVENT_TRIPPED, &errorCode, userdata_);
}

void
EventCallback::onError (uint32_t errorCode, gpointer userdata)
{
  GST_TRACE ("onError");
  if (!callback_) {
    GST_INFO ("Callback not set in CodecCallback(%p)", this);
    return;
  }
  callback_ (EVENT_ERROR, &errorCode, userdata_);
}

C2VencBuffWrapper::C2VencBuffWrapper (uint32_t width, uint32_t height,
      C2Allocator::id_t allocator_id, android::C2HandleGBM * handle) :
      C2GraphicAllocation (width, height), base_ (nullptr), mapsize_ (0),
      allocator_id_ (allocator_id), handle_ (handle)
{
}

c2_status_t
C2VencBuffWrapper::map (C2Rect rect, C2MemoryUsage usage,
    C2Fence * fence, C2PlanarLayout * layout, uint8_t ** addr)
{
  return C2_OK;
}

c2_status_t
C2VencBuffWrapper::unmap (uint8_t **addr, C2Rect rect, C2Fence * fence)
{
  return C2_OK;
}

const C2Handle *
C2VencBuffWrapper::handle () const
{
  return reinterpret_cast<const C2Handle*>(handle_);
}

id_t
C2VencBuffWrapper::getAllocatorId () const
{
  return allocator_id_;
}

bool
C2VencBuffWrapper::equals (
  const std::shared_ptr<const C2GraphicAllocation> &other) const
{
  return other && other->handle() == handle();
};
