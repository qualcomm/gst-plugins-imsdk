/*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifdef HAVE_MMM_COLOR_FMT_H
#include <display/media/mmm_color_fmt.h>
#else
#include <vidc/media/msm_media_info.h>
#define MMM_COLOR_FMT_NV12 COLOR_FMT_NV12
#define MMM_COLOR_FMT_NV12_UBWC COLOR_FMT_NV12_UBWC
#define MMM_COLOR_FMT_Y_STRIDE VENUS_Y_STRIDE
#define MMM_COLOR_FMT_Y_SCANLINES VENUS_Y_SCANLINES
#define MMM_COLOR_FMT_UV_STRIDE VENUS_UV_STRIDE
#define MMM_COLOR_FMT_BUFFER_SIZE_USED VENUS_BUFFER_SIZE_USED
#endif

#include <gbm.h>
#include <gbm_priv.h>

#define MAX_PENDING_WORK 6

#define GST_CAT_DEFAULT gst_c2_venc_context_debug_category ()
static GstDebugCategory *
gst_c2_venc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtic2engine", 0,
        "C2 encoder context");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

std::shared_ptr<C2Buffer>
CreateLinearBuffer(const std::shared_ptr<C2LinearBlock>& block) {

  return C2Buffer::CreateLinearBuffer(
      block->share(block->offset(), block->size(), ::C2Fence()));
}

std::shared_ptr<C2Buffer>
CreateGraphicBuffer(const std::shared_ptr<C2GraphicBlock>& block) {

  return C2Buffer::CreateGraphicBuffer(
      block->share (C2Rect(block->width(), block->height()), ::C2Fence()));
}

C2ComponentWrapper::C2ComponentWrapper(std::shared_ptr<C2ComponentStore> store,
                                       const char * name) {

  c2_status_t result;

  n_pending_works_ = 0;

  result = store->createComponent(C2String (name), &component_);
  if ((result != C2_OK) || (component_.get () == nullptr)) {
    GST_ERROR ("Failed to create C2venc component");
    return;
  }

  compintf_ = std::shared_ptr<C2ComponentInterface> (component_->intf ());
  if (compintf_.get () == nullptr) {
    GST_ERROR ("Failed to create C2venc component interface");
    return;
  }
}

C2ComponentWrapper::~C2ComponentWrapper() {

  out_pending_buffers_.clear ();
}

bool C2ComponentWrapper::SetHandler(event_handler_cb callback, gpointer userdata) {

  if (component_.get () == nullptr) {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }

  EventCallback *clbk = new EventCallback(userdata, callback);
  std::shared_ptr<C2Component::Listener> listener =
      std::make_shared<C2ComponentListener>(component_, clbk, this);

  if (component_->setListener_vb (listener, C2_MAY_BLOCK) != C2_OK) {
    GST_ERROR ("Failed to set component callback");
    return FALSE;
  }

  return TRUE;
}

bool C2ComponentWrapper::Config (GPtrArray * config) {

  if (compintf_.get () != nullptr) {
    std::vector<C2Param*> stackParams;
    std::list<std::unique_ptr<C2Param>> settings;
    c2_status_t result;
    std::vector<std::unique_ptr<C2SettingResult>> failures;

    g_ptr_array_foreach (config, push_to_settings, &settings);

    for (auto& item : settings)
      stackParams.push_back(item.get ());

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

bool C2ComponentWrapper::Start () {

  if (component_.get() != nullptr) {
    component_->start();
  } else {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }
  return TRUE;
}

bool
C2ComponentWrapper::Stop () {

  if (component_.get() != nullptr) {
    component_->stop();
  } else {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }
  return TRUE;
}

int32_t C2ComponentWrapper::GetBlockPoolId () {

  if(out_graphic_pool_)
    return (int32_t)out_graphic_pool_->getLocalId();

  return -1;
}

c2_status_t C2ComponentWrapper::PrepareC2Buffer(BufferDescriptor* buffer,
                                                std::shared_ptr<C2Buffer>* c2buffer) {

  uint8_t* rawBuffer = buffer->data;
  uint8_t* destBuffer = nullptr;
  uint32_t frameSize = buffer->size;
  C2BlockPool::local_id_t type = buffer->pool_type;
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

    if (type == C2BlockPool::BASIC_LINEAR) {
      allocSize = ALIGN(frameSize, 4096);
      err = linear_pool_->fetchLinearBlock(allocSize, usage, &linear_block);
      if (err != C2_OK || linear_block == nullptr) {
        GST_ERROR ("Linear pool failed to allocate input buffer of size : (%d)", frameSize);
        return C2_NO_MEMORY;
      }

      C2WriteView view = linear_block->map().get();
      if (view.error() != C2_OK) {
        GST_ERROR ("C2LinearBlock::map() failed : %d", view.error());
        return C2_NO_MEMORY;
      }
      destBuffer = view.base();
      memcpy (destBuffer, rawBuffer, frameSize);
      linear_block->mSize = frameSize;
      GST_INFO ("@@@ input size %d",frameSize);
      buf = CreateLinearBuffer (linear_block);
    } else if (type == C2BlockPool::BASIC_GRAPHIC) {
      if (buffer->format == GST_VIDEO_FORMAT_NV12
          && buffer->ubwc_flag) {
        GST_INFO ("NV12: usage add UBWC");
        usage = { C2MemoryUsage::CPU_READ | GBM_BO_USAGE_UBWC_ALIGNED_QTI,
          C2MemoryUsage::CPU_WRITE };
      }
      err = graphic_pool_->fetchGraphicBlock(buffer->width, buffer->height,
              ToGBMFormat (buffer->format, buffer->ubwc_flag),
              usage, &graphic_block);
      if (err != C2_OK || graphic_block == nullptr) {
        GST_ERROR ("Graphic pool failed to allocate");
        return C2_NO_MEMORY;
      }

      C2GraphicView view = graphic_block->map().get();
      if (view.error() != C2_OK) {
        GST_ERROR ("C2GraphicBlock::map() failed : %d", view.error());
        return C2_NO_MEMORY;
      }
      uint8_t * const *data = view.data();

      switch (buffer->format) {
        case GST_VIDEO_FORMAT_NV12: {
          uint32_t i, j, src_stride, dest_stride, height;
          uint8_t *src, *dest;

          if (buffer->ubwc_flag) {
            uint32_t buf_size = MMM_COLOR_FMT_BUFFER_SIZE_USED (MMM_COLOR_FMT_NV12_UBWC,
              buffer->width, buffer->height, 0);

            src = rawBuffer;
            dest = (uint8_t *)*data;
            memcpy (dest, src, buf_size);
          } else {
            src = rawBuffer;
            src_stride = buffer->width;
            for (i = 0; i < 2; i++) {
              if (0 == i){
                dest_stride = MMM_COLOR_FMT_Y_STRIDE (MMM_COLOR_FMT_NV12, buffer->width);
                height = ((buffer->size / buffer->width) / 3) * 2;
                dest = (uint8_t *)*data;
              } else {
                dest_stride = MMM_COLOR_FMT_UV_STRIDE (MMM_COLOR_FMT_NV12, buffer->width);
                height = (buffer->size / buffer->width) / 3;
                dest = (uint8_t *)*data + MMM_COLOR_FMT_Y_STRIDE (MMM_COLOR_FMT_NV12,
                       buffer->width) * MMM_COLOR_FMT_Y_SCANLINES (MMM_COLOR_FMT_NV12,
                       buffer->height);
              }

              for (j = 0; j < height; j++) {
                memcpy (dest, src, buffer->width);
                src += src_stride;
                dest += dest_stride;
              }
            }
          }
          break;
        }
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_NV12_10LE32: {
          uint8_t *src, *dest;
          src = rawBuffer;
          dest = (uint8_t *)*data;
          memcpy (dest, src, buffer->size);
          break;
        }
        default:
          GST_ERROR("Unsupported format");
          return C2_BAD_VALUE;
      }

      buf = CreateGraphicBuffer (graphic_block);
    }

    *c2buffer = buf;
  }

  return result;
}

bool C2ComponentWrapper::Queue(BufferDescriptor * buffer) {

  if (component_.get() != nullptr) {
    C2FrameData::flags_t inputFrameFlag = ToC2Flag(buffer->flag);
    uint64_t frame_index = buffer->index;
    uint64_t timestamp = buffer->timestamp;
    gint width = buffer->width;
    gint height = buffer->height;
    C2BlockPool::local_id_t type = buffer->pool_type;
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
      if (type == C2BlockPool::BASIC_GRAPHIC) {
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
          struct gbm_buf_info bufinfo = { 0, };
          guint size = 0, usage = 0;
          bufinfo.width = buffer->width;
          bufinfo.height = buffer->height;
          bufinfo.format = ToGBMFormat (buffer->format, buffer->ubwc_flag);

          gbm_perform (GBM_PERFORM_GET_BUFFER_STRIDE_SCANLINE_SIZE, &bufinfo, usage,
            &gbm_handle->mInts.stride, &gbm_handle->mInts.slice_height, &size);
          GST_INFO ("gbm_perform %d %d %d %d", gbm_handle->mInts.stride,
               gbm_handle->mInts.slice_height, size, buffer->size);
          gbm_handle->mInts.format = ToGBMFormat (buffer->format, buffer->ubwc_flag);
          gbm_handle->mInts.usage_lo = GBM_BO_USE_SCANOUT
                                       | GBM_BO_USE_RENDERING;
          if (buffer->ubwc_flag) {
            gbm_handle->mInts.usage_lo |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
          }
          gbm_handle->mInts.size = buffer->size;
          // Use fd as the unique buffer id for C2Buffer
          gbm_handle->mInts.id = buffer->fd;

          std::shared_ptr<C2GraphicAllocation> alloc =
            std::make_shared<C2VencBuffWrapper> (
              buffer->width, buffer->height,
              android::C2PlatformAllocatorStore::DEFAULT_GRAPHIC, gbm_handle);
          graphic_block = _C2BlockFactory::CreateGraphicBlock (alloc);

          std::shared_ptr<C2Buffer> buf = C2Buffer::CreateGraphicBuffer(
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
          PrepareC2Buffer (buffer, &clientBuf);
          work->input.buffers.emplace_back (clientBuf);
        }
      } else if (type == C2BlockPool::BASIC_LINEAR) {
        GST_INFO ("Linear mem pool Queue");
        std::shared_ptr<C2Buffer> clientBuf;
        PrepareC2Buffer (buffer, &clientBuf);
        work->input.buffers.emplace_back (clientBuf);
      }
    } else {
      GST_INFO ("queue EOS frame");
    }

    work->worklets.clear ();
    work->worklets.emplace_back (new C2Worklet);

    if (buffer->config_data) {
      auto& worklet = work->worklets.front ();

      std::list<std::unique_ptr<C2Param>> settings;
      push_to_settings (buffer->config_data, &settings);
      std::for_each (settings.begin (), settings.end (),
          [&] (std::unique_ptr<C2Param>& param) {
              worklet->tunings.push_back (std::unique_ptr<C2Tuning> (
                  reinterpret_cast<C2Tuning *> (param.release())));
          });
    }
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
    n_pending_works_++;
  } else {
    GST_ERROR ("The component is not valid");
    return FALSE;
  }

  GST_INFO ("C2venc component queue");

  return TRUE;
}

bool C2ComponentWrapper::FreeOutputBuffer(uint64_t buf_idx) {

  std::map<uint64_t, std::shared_ptr<C2Buffer> >::iterator it;

  std::unique_lock<std::mutex> ul (out_pending_buffer_lock_);
  it = out_pending_buffers_.find (buf_idx);
  if (it != out_pending_buffers_.end ()) {
    out_pending_buffers_.erase (it);
  } else {
    GST_INFO ("Buffer index(%lu) not found", buf_idx);
    return FALSE;
  }

  return TRUE;
}

C2FrameData::flags_t C2ComponentWrapper::ToC2Flag(GstC2Flag flag) {

  uint32_t result = 0;

  if (GST_C2_FLAG_DROP_FRAME & flag) {
    result |= C2FrameData::FLAG_DROP_FRAME;
  }
  if (GST_C2_FLAG_END_OF_STREAM & flag) {
    result |= C2FrameData::FLAG_END_OF_STREAM;
  }
  if (GST_C2_FLAG_INCOMPLETE & flag) {
    result |= C2FrameData::FLAG_INCOMPLETE;
  }
  if (GST_C2_FLAG_CODEC_CONFIG & flag) {
    result |= C2FrameData::FLAG_CODEC_CONFIG;
  }

  return static_cast<C2FrameData::flags_t>(result);
}

guint32 C2ComponentWrapper::ToGBMFormat(GstVideoFormat format, bool isubwc) {

  guint32 result = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      result = GBM_FORMAT_NV12;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      result = GBM_FORMAT_YCbCr_420_P010_VENUS;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      if (isubwc) {
        result = GBM_FORMAT_YCbCr_420_TP10_UBWC;
      } else {
        GST_WARNING ("TP10 without ubwc is not supported");
      }
      break;
    default:
      GST_WARNING ("unsupported video format:%s",
        gst_video_format_to_string (format));
      break;
  }

  return result;
}

c2_status_t C2ComponentWrapper::CheckMaxAvailableQueues() {

  std::unique_lock<std::mutex> ul (lock_);
  GST_DEBUG ("pending works: %d", n_pending_works_);
  while (n_pending_works_ > MAX_PENDING_WORK) {
    workcondition_.wait (ul);
  }
  return C2_OK;
}

c2_status_t C2ComponentWrapper::CreateBlockpool(C2BlockPool::local_id_t type) {

  c2_status_t ret;
  if (type == C2BlockPool::BASIC_LINEAR) {
    ret = android::GetCodec2BlockPool(type, component_, &linear_pool_);
    if (ret != C2_OK || linear_pool_ == nullptr) {
      return ret;
    }
  } else if (type == C2BlockPool::BASIC_GRAPHIC) {
    ret = android::GetCodec2BlockPool(type, component_, &graphic_pool_);
    if (ret != C2_OK || graphic_pool_ == nullptr) {
      return ret;
    }
  }

#ifdef CODEC2_CONFIG_VERSION_2_0
  if (type == C2AllocatorStore::GRAPHIC_NON_CONTIGUOUS) {
    ret = android::CreateCodec2BlockPool(type, component_, &out_graphic_pool_);
    if (ret != C2_OK || out_graphic_pool_ == nullptr) {
      GST_ERROR ("Create NON CONTIGUOUS GRAPHIC failed: %d", ret);
      return ret;
    }
    GST_INFO ("create Graphic block-pool ID %u",
        (uint32_t) out_graphic_pool_->getLocalId());
  }
#endif

  return ret;
}

C2ComponentListener::C2ComponentListener (std::shared_ptr<C2Component> comp,
      EventCallback * callback, void * userdata) :
      comp_ (comp), callback_ (callback), userdata_ (userdata)
{
}

void C2ComponentListener::onWorkDone_nb(std::weak_ptr<C2Component> component,
                                        std::list<std::unique_ptr<C2Work>> works) {

  C2ComponentWrapper *component_wrapper = (C2ComponentWrapper*) userdata_;

  GST_TRACE ("Component listener onWorkDone_nb");

  while (!works.empty ()) {
    std::unique_ptr<C2Work> work = std::move(works.front ());

    works.pop_front ();
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

    const std::unique_ptr<C2Worklet>& worklet = work->worklets.front();
    std::shared_ptr<C2Buffer> buffer = nullptr;
    uint64_t buf_idx = 0;
    C2FrameData::flags_t outputFrameFlag = worklet->output.flags;
    uint64_t timestamp = worklet->output.ordinal.timestamp.peeku();

    if (worklet->output.buffers.size () == 1u) {
      buffer = worklet->output.buffers[0];
      buf_idx = worklet->output.ordinal.frameIndex.peeku();

      GST_INFO (
          "Output buffer available, Frame index : %lu, Timestamp : %lu, flag: %x",
          buf_idx, worklet->output.ordinal.timestamp.peeku(), outputFrameFlag);

      // ref count ++
      {
        std::unique_lock<std::mutex> ul (component_wrapper->out_pending_buffer_lock_);
        component_wrapper->out_pending_buffers_[buf_idx] = buffer;
      }

      if (callback_) {
        callback_->OnOutputBufferAvailable(buffer, buf_idx, timestamp,
                                           outputFrameFlag, NULL);
      }
      if (not (C2FrameData::FLAG_INCOMPLETE & outputFrameFlag)) {
        std::unique_lock<std::mutex> ul(component_wrapper->lock_);
        component_wrapper->n_pending_works_--;
        component_wrapper->workcondition_.notify_one ();
      }
    } else {
      if (outputFrameFlag & C2FrameData::FLAG_END_OF_STREAM) {
        GST_INFO ("Component(%p) reached EOS on output", this);
        if (callback_) {
          callback_->OnOutputBufferAvailable(NULL, buf_idx, timestamp,
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
      std::unique_lock<std::mutex> ul(component_wrapper->lock_);
      component_wrapper->n_pending_works_--;
      component_wrapper->workcondition_.notify_one ();
    }
  }
}

void C2ComponentListener::onTripped_nb(std::weak_ptr<C2Component> component,
    std::vector<std::shared_ptr<C2SettingResult>> results) {

  GST_TRACE ("Component listener onTripped_nb");

  if (callback_) {
    for (auto& f : results) {
      callback_->OnTripped(static_cast<uint32_t> (f->failure), NULL);
    }
  }
}

void C2ComponentListener::onError_nb(std::weak_ptr<C2Component> component,
                                     uint32_t error) {

  GST_TRACE ("Component listener onError_nb");

  if (callback_) {
    callback_->OnError(error, userdata_);
  }
}

EventCallback::EventCallback(const gpointer userdata, event_handler_cb cb)
    : userdata_(userdata),
      callback_(cb) {

};

void EventCallback::OnOutputBufferAvailable(const std::shared_ptr<C2Buffer> buffer,
                                            uint64_t index, uint64_t timestamp,
                                            C2FrameData::flags_t flag,
                                            gpointer userdata) {

  GST_TRACE ("OnOutputBufferAvailable");
  if (!callback_) {
    GST_INFO ("Callback not set");
    return;
  }

  BufferDescriptor out_buf;
  memset (&out_buf, 0, sizeof (BufferDescriptor));
  uint32_t flag_res = 0;
  GstC2Flag flag_type;
  if (C2FrameData::FLAG_DROP_FRAME & flag) {
    flag_res |= GST_C2_FLAG_DROP_FRAME;
  }
  if (C2FrameData::FLAG_END_OF_STREAM & flag) {
    flag_res |= GST_C2_FLAG_END_OF_STREAM;
  }
  if (C2FrameData::FLAG_INCOMPLETE & flag) {
    flag_res |= GST_C2_FLAG_INCOMPLETE;
  }
  if (C2FrameData::FLAG_CODEC_CONFIG & flag) {
    flag_res |= GST_C2_FLAG_CODEC_CONFIG;
  }

  if (buffer) {
    C2BufferData::type_t buf_type = buffer->data ().type ();
    if (buf_type == C2BufferData::LINEAR) {
      // check for sync frame
      auto picTypeInfo =
          std::static_pointer_cast<const C2StreamPictureTypeInfo::output>(
          buffer->getInfo(C2StreamPictureTypeInfo::output::PARAM_TYPE));
      if (picTypeInfo) {
        if (picTypeInfo->value == C2Config::SYNC_FRAME) {
          flag_res |= GST_C2_FLAG_SYNC_FRAME;
        }
      }
    }
  }

  flag_type = static_cast<GstC2Flag> (flag_res);

  if (buffer) {
    C2BufferData::type_t buf_type = buffer->data ().type ();
    out_buf.timestamp = timestamp;
    out_buf.index = index;
    out_buf.flag = flag_type;

    if (buf_type == C2BufferData::LINEAR) {
      const C2ConstLinearBlock linear_block =
      buffer->data ().linearBlocks ().front ();
      const C2Handle *handle = linear_block.handle ();
      if (nullptr == handle) {
        GST_ERROR ("C2ConstLinearBlock handle is null");
        return;
      }
      out_buf.size = linear_block.size ();
      out_buf.fd = handle->data[0];
      GST_INFO ("out_buf linear fd:%d size:%d\n", out_buf.fd, out_buf.size);
      // Check for codec data
      auto csd = std::static_pointer_cast<const C2StreamInitDataInfo::output>(
          buffer->getInfo (C2StreamInitDataInfo::output::PARAM_TYPE));
      if (csd) {
        GST_INFO ("get codec config data, size: %lu data:%p",
        csd->flexCount (), (guint8*) csd->m.value);
        out_buf.config_data = (guint8*) &csd->m.value;
        out_buf.config_size = csd->flexCount();
        out_buf.flag = static_cast<GstC2Flag>(static_cast<uint32_t> (
            GST_C2_FLAG_CODEC_CONFIG) | static_cast<uint32_t> (out_buf.flag));
      }
      callback_ (GST_C2_EVENT_OUTPUTS_DONE, &out_buf, userdata_);
    } else if (buf_type == C2BufferData::GRAPHIC) {
      const C2ConstGraphicBlock graphic_block =
          buffer->data().graphicBlocks().front();
      const C2Handle* handle = graphic_block.handle();
      if (nullptr == handle) {
        GST_ERROR("C2ConstGraphicBlock handle is null");
        return;
      }
      out_buf.fd = handle->data[0];
      guint32 stride = 0;
      guint32 scanline = 0;
      guint64 usage = 0;
      guint32 size = 0;
      guint32 format = 0;
      guint64 bo = 0;
      guint32 width = 0;
      guint32 height = 0;
      C2Rect crop;
      const C2GraphicView view = graphic_block.map().get();
      struct gbm_buf_info bufinfo = { 0, };

      _UnwrapNativeCodec2GBMMetadata(handle, &width, &height, &format, &usage,
                                     &stride, &size);

      out_buf.size = size;
      /* The actual value of bo here is a pointer to struct gbm_bo.
        * To avoid including GBM header, use void* instead. */
      //out_buf.gbm_bo = reinterpret_cast<void*>(bo);
      crop = view.crop();
      GST_INFO("get crop info (%d,%d) [%dx%d] ", crop.left, crop.top,
          crop.width, crop.height);
      out_buf.width = crop.width;
      out_buf.height = crop.height;
      out_buf.stride = stride;
      bufinfo.width = out_buf.width;
      bufinfo.height = out_buf.height;
      bufinfo.format = format;
      gbm_perform (GBM_PERFORM_GET_BUFFER_STRIDE_SCANLINE_SIZE, &bufinfo, usage,
          &stride, &scanline, &size);
      out_buf.scanline = scanline;

      callback_ (GST_C2_EVENT_OUTPUTS_DONE, &out_buf, userdata_);
      GST_INFO("out buffer size:%d width:%d height:%d stride:%d data:%p\n",
          size, width, height, stride, out_buf.data);
    } else {
      GST_ERROR ("Not supported output buffer type!");
    }
  } else if (flag & C2FrameData::FLAG_END_OF_STREAM) {
    GST_INFO ("Mark EOS buffer");
    //out_buf.data = NULL;
    out_buf.fd = -1;
    out_buf.size = 0;
    out_buf.timestamp = 0;
    out_buf.index = 0;
    out_buf.flag = flag_type;
    callback_ (GST_C2_EVENT_OUTPUTS_DONE, &out_buf, userdata_);
  } else {
    GST_INFO ("Buffer is null");
  }
}

void EventCallback::OnTripped (uint32_t error, gpointer userdata) {

  GST_TRACE ("OnTripped");
  if (!callback_) {
    GST_INFO ("Callback not set in CodecCallback(%p)", this);
    return;
  }
  callback_(GST_C2_EVENT_TRIPPED, &error, userdata_);
}

void EventCallback::OnError (uint32_t error, gpointer userdata) {

  GST_TRACE ("OnError");
  if (!callback_) {
    GST_INFO ("Callback not set in CodecCallback(%p)", this);
    return;
  }
  callback_(GST_C2_EVENT_ERROR, &error, userdata_);
}

C2VencBuffWrapper::C2VencBuffWrapper(uint32_t width, uint32_t height,
                                     C2Allocator::id_t allocator_id,
                                     android::C2HandleGBM * handle)
    : C2GraphicAllocation(width, height),
      base_(nullptr),
      mapsize_(0),
      allocator_id_(allocator_id),
      handle_(handle) {

}

c2_status_t C2VencBuffWrapper::map(C2Rect rect, C2MemoryUsage usage,
                                   C2Fence * fence, C2PlanarLayout * layout,
                                   uint8_t ** addr)
{
  return C2_OK;
}

c2_status_t C2VencBuffWrapper::unmap(uint8_t **addr, C2Rect rect,
                                     C2Fence * fence) {

  return C2_OK;
}

const C2Handle * C2VencBuffWrapper::handle() const {

  return reinterpret_cast<const C2Handle*> (handle_);
}

id_t C2VencBuffWrapper::getAllocatorId() const {

  return allocator_id_;
}

bool C2VencBuffWrapper::equals (
  const std::shared_ptr<const C2GraphicAllocation> &other) const {

  return other && other->handle() == handle();
};
