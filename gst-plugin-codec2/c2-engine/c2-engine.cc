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

#include "c2-engine.h"

#include "c2-component.h"
#include "c2-engine-params.h"
#include "c2-engine-utils.h"


#define GST_CAT_DEFAULT ensure_debug_category()
static G_DEFINE_QUARK (GstC2BufferQuark, gst_c2_buffer_qdata);

#define GST_C2_MODE_ENCODE(engine) \
  ((engine->mode == GST_C2_ENGINE_MODE_ENCODE) ? TRUE : FALSE)
#define GST_C2_MODE_DECODE(engine) \
  ((engine->mode == GST_C2_ENGINE_MODE_DECODE) ? TRUE : FALSE)

#define GST_C2_ENGINE_INCREMENT_PENDING_WORK(engine) \
{ \
  g_mutex_lock (&engine->lock); \
  engine->n_pending++; \
  g_mutex_unlock (&engine->lock); \
}

#define GST_C2_ENGINE_DECREMENT_PENDING_WORK(engine) \
{ \
  g_mutex_lock (&engine->lock); \
  engine->n_pending--; \
  g_cond_broadcast (&engine->workdone); \
  g_mutex_unlock (&engine->lock); \
}

#define GST_C2_ENGINE_ZERO_OUT_PENDING_WORK(engine) \
{ \
  g_mutex_lock (&engine->lock); \
  engine->n_pending = 0; \
  g_cond_broadcast (&engine->workdone); \
  g_mutex_unlock (&engine->lock); \
}

#define GST_C2_ENGINE_CHECK_AND_WAIT_PENDING_WORK(engine, max) \
{ \
  g_mutex_lock (&engine->lock); \
  \
  while (engine->n_pending > max) { \
    GST_LOG ("Waiting until pending frames are equal of below %u, current " \
        "pending works: %u", max, engine->n_pending); \
    g_cond_wait (&engine->workdone, &engine->lock); \
  } \
  g_mutex_unlock (&engine->lock); \
}

#define MAX_NUM_PENDING_WORK      (11)

enum
{
  GST_C2_ENGINE_MODE_ENCODE,
  GST_C2_ENGINE_MODE_DECODE,
};

struct _GstC2Engine {
  /// Component name, used mainly for debugging.
  gchar           *name;
  /// Codec2 component instance.
  C2Module        *c2module;
  /// Component mode/type: Encode or Decode.
  guint32         mode;

  /// Draining state & pending frames lock.
  GMutex          lock;
  /// Tracking the number of pending frames.
  guint32         n_pending;
  /// Condition signalled when pending frame has been processed.
  GCond           workdone;

  GstC2Callbacks  *callbacks;
  gpointer        userdata;
};

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("c2-engine", 0,
        "Codec2 Engine");
    g_once_init_leave (&catonce, catdone);
  }

  return (GstDebugCategory *) catonce;
}

// Wrapper class for C2 buffer which is attached to the corresponding
// GST buffer and will be deleted when the GST buffer is released. By deleting
// this wrapper the shared pointer to the C2 buffer will be released as well.
class GstC2BufferQData {
 public:
  GstC2BufferQData(std::shared_ptr<C2Buffer>& c2buffer) : c2buffer_(c2buffer) {}
  ~GstC2BufferQData() = default;

 private:
  std::shared_ptr<C2Buffer> c2buffer_;
};

static void
gst_c2_buffer_qdata_release (gpointer userdata)
{
  GstC2BufferQData *qdata = reinterpret_cast<GstC2BufferQData*>(userdata);
  delete qdata;
}

// Nofifier class for C2 buffers and events. Translates the C2 data into the
// GStreamer equivalent and then calls the registered engine callbacks.
class GstC2Notifier : public IC2Notifier {
 public:
  GstC2Notifier(GstC2Engine* engine) : engine_(engine) {}

  void EventHandler(C2EventType event, void* payload) override {

    guint type = GST_C2_EVENT_UNKNOWN;

    switch (event) {
      case C2EventType::kError:
        type = GST_C2_EVENT_ERROR;
        break;
      case C2EventType::kEOS:
        GST_C2_ENGINE_ZERO_OUT_PENDING_WORK (engine_);
        type = GST_C2_EVENT_EOS;
        break;
      default:
        GST_WARNING ("Unknown event '%u'!", static_cast<uint32_t>(event));
        return;
    }

    engine_->callbacks->event (type, payload, engine_->userdata);
  }

  void FrameAvailable(std::shared_ptr<C2Buffer>& c2buffer, uint64_t index,
                      uint64_t timestamp, C2FrameData::flags_t flags) override {

    GstBuffer *buffer = NULL;
    GstAllocator *allocator = NULL;
    GstMemory *memory = NULL;
    uint32_t fd = 0, size = 0;

    if ((buffer = gst_buffer_new ()) == NULL) {
      GST_ERROR ("Failed to create GST buffer!");
      return;
    }

    if (c2buffer->data().type() == C2BufferData::LINEAR) {
      const C2ConstLinearBlock block = c2buffer->data().linearBlocks().front();
      const C2Handle *handle = block.handle();

      size = block.size();
      fd = handle->data[0];

    } else if (c2buffer->data().type() == C2BufferData::GRAPHIC) {
      const C2ConstGraphicBlock block = c2buffer->data().graphicBlocks().front();
      const C2GraphicView view = block.map().get();
      auto handle = static_cast<const android::C2HandleGBM*>(block.handle());

      size = handle->mInts.size;
      fd = handle->mFds.buffer_fd;

      if (!GstC2Utils::ExtractHandleInfo (buffer, handle)) {
        GST_ERROR ("Failed to extract GBM handle info!");
        gst_buffer_unref (buffer);
        return;
      }

      GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);

      vmeta->width = view.crop().width;
      vmeta->height = view.crop().height;

      GST_LOG ("Crop rectangle (%d,%d) [%dx%d] ", view.crop().left,
          view.crop().top, view.crop().width, view.crop().height);
    } else {
      GST_ERROR ("Unknown Codec2 buffer type!");
      gst_buffer_unref (buffer);
      return;
    }

    if ((allocator = gst_fd_allocator_new ()) == NULL) {
      GST_ERROR ("Failed to create FD allocator!");
      gst_buffer_unref (buffer);
      return;
    }

    if ((memory = gst_fd_allocator_alloc (allocator, fd, size,
            GST_FD_MEMORY_FLAG_DONT_CLOSE)) == NULL) {
      GST_ERROR ("Failed to create memory block!");
      gst_buffer_unref (buffer);
      gst_object_unref (allocator);
      return;
    }

    gst_buffer_append_memory (buffer, memory);
    gst_object_unref (allocator);

    // Check whetehr this is a key/sync frame.
    std::shared_ptr<const C2Info> c2info =
        c2buffer->getInfo (C2StreamPictureTypeInfo::output::PARAM_TYPE);
    auto pictype =
        std::static_pointer_cast<const C2StreamPictureTypeInfo::output>(c2info);

    if (pictype && (pictype->value == C2Config::SYNC_FRAME))
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_SYNC);

    if (flags & C2FrameData::FLAG_CODEC_CONFIG)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_HEADER);

    if (flags & C2FrameData::FLAG_DROP_FRAME)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DROPPABLE);

    if (!(flags & C2FrameData::FLAG_INCOMPLETE))
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_MARKER);

    GST_BUFFER_OFFSET (buffer) = index;
    GST_BUFFER_TIMESTAMP (buffer) =
        gst_util_uint64_scale (timestamp, GST_SECOND, 1000000);

    GstC2BufferQData *qdata = new GstC2BufferQData(c2buffer);

    // Set a notification function to signal when the buffer is no longer used.
    gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer),
        gst_c2_buffer_qdata_quark (), qdata, gst_c2_buffer_qdata_release);

    GST_TRACE ("Available %" GST_PTR_FORMAT, buffer);
    engine_->callbacks->buffer (buffer, engine_->userdata);

    // Deincrement the number of pending works if frame is complete.
    if (!(flags & C2FrameData::FLAG_INCOMPLETE))
      GST_C2_ENGINE_DECREMENT_PENDING_WORK (engine_);
  }

 private:
  GstC2Engine* engine_;
};

GstC2Engine *
gst_c2_engine_new (const gchar * name, GstC2Callbacks * callbacks,
    gpointer userdata)
{
  GstC2Engine *engine = NULL;

  engine = g_new0 (GstC2Engine, 1);
  g_return_val_if_fail (engine != NULL, NULL);

  g_mutex_init (&engine->lock);
  g_cond_init (&engine->workdone);

  try {
    engine->c2module = C2Factory::GetModule (name);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create C2 module, error: '%s'!", e.what());
    gst_c2_engine_free (engine);
    return NULL;
  }

  try {
    std::shared_ptr<IC2Notifier> notifier =
        std::make_shared<GstC2Notifier>(engine);

    engine->c2module->Initialize (notifier);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to initialize, error: '%s'!", e.what());
    gst_c2_engine_free (engine);
    return NULL;
  }

  engine->name = g_strdup (name);
  engine->mode = g_str_has_suffix (name, ".encoder") ?
      GST_C2_ENGINE_MODE_ENCODE : GST_C2_ENGINE_MODE_DECODE;

  engine->callbacks = callbacks;
  engine->userdata = userdata;

  engine->n_pending = 0;

  GST_INFO ("Created C2 engine: %p", engine);
  return engine;
}

void
gst_c2_engine_free (GstC2Engine * engine)
{
  GST_INFO ("Destroyed C2 engine: %p", engine);

  g_cond_clear (&engine->workdone);
  g_mutex_clear (&engine->lock);

  g_free (engine->name);
  delete engine->c2module;

  g_free (engine);
}

gboolean
gst_c2_engine_get_parameter (GstC2Engine * engine, guint type, gpointer payload)
{
  C2Module *c2module = engine->c2module;

  try {
    C2Param::Index index = GstC2Utils::ParamIndex(type);

    std::unique_ptr<C2Param> c2param = c2module->QueryParam (index);
    GstC2Utils::PackPayload(type, c2param, payload);

    GST_DEBUG ("Query parameter '%s' was successful", GstC2Utils::ParamName(type));
  } catch (std::exception& e) {
    GST_ERROR ("Failed to query c2module parameter, error: '%s'!", e.what());
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_c2_engine_set_parameter (GstC2Engine * engine, guint type, gpointer payload)
{
  C2Module *c2module = engine->c2module;

  try {
    std::unique_ptr<C2Param> c2param;
    GstC2Utils::UnpackPayload(type, payload, c2param);

    c2module->SetParam (c2param);
    GST_DEBUG ("Set parameter '%s' was successful", GstC2Utils::ParamName(type));
  } catch (std::exception& e) {
    GST_ERROR ("Failed to set c2module parameter, error: '%s'!", e.what());
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_c2_engine_start (GstC2Engine * engine)
{
  C2Module *c2module = engine->c2module;

  try {
    c2module->Start ();
    GST_DEBUG ("Started c2module '%s'", engine->name);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to start c2module, error: '%s'!", e.what());
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_c2_engine_stop (GstC2Engine * engine)
{
  C2Module *c2module = engine->c2module;

  try {
    c2module->Stop ();
    GST_DEBUG ("Stopped c2module '%s'", engine->name);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to stop c2module, error: '%s'!", e.what());
    return FALSE;
  }

  // Wait until all work is completed or EOS.
  GST_C2_ENGINE_CHECK_AND_WAIT_PENDING_WORK (engine, 0);

  return TRUE;
}

gboolean
gst_c2_engine_flush (GstC2Engine * engine)
{
  C2Module *c2module = engine->c2module;

  try {
    c2module->Flush (C2Component::FLUSH_COMPONENT);
    GST_DEBUG ("Flushed c2module '%s'", engine->name);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to flush c2module, error: '%s'!", e.what());
    return FALSE;
  }

  // Wait until all work is completed or EOS.
  GST_C2_ENGINE_CHECK_AND_WAIT_PENDING_WORK (engine, 0);

  return TRUE;
}

gboolean
gst_c2_engine_drain (GstC2Engine * engine)
{
  C2Module *c2module = engine->c2module;
  std::shared_ptr<C2Buffer> c2buffer;
  std::list<std::unique_ptr<C2Param>> settings;

  uint64_t index = 0;
  uint64_t timestamp = 0;
  uint32_t flags = C2FrameData::FLAG_END_OF_STREAM;

  // TODO Switch to Drain API when drain with EOS is supported.
  // try {
  //   c2module->Drain (C2Component::DRAIN_COMPONENT_WITH_EOS);
  //   GST_DEBUG ("Drain c2module '%s'", engine->name);
  // } catch (std::exception& e) {
  //   GST_ERROR ("Failed to drain c2module, error: '%s'!", e.what());
  //   return FALSE;
  // }

  try {
    c2module->Queue (c2buffer, settings, index, timestamp, flags);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to queue EOS, error: '%s'!", e.what());
    return FALSE;
  }

  // Wait until all work is completed or EOS.
  GST_C2_ENGINE_CHECK_AND_WAIT_PENDING_WORK (engine, 0);

  return TRUE;
}

gboolean
gst_c2_engine_queue (GstC2Engine * engine, GstVideoCodecFrame * frame)
{
  C2Module *c2module = engine->c2module;
  GstBuffer *buffer = frame->input_buffer;
  std::shared_ptr<C2Buffer> c2buffer;
  std::list<std::unique_ptr<C2Param>> settings;

  uint64_t index = frame->system_frame_number;
  uint64_t timestamp = 0;
  uint32_t flags = 0;

  // Check and wait in case maximum number of pending frames has been reached.
  GST_C2_ENGINE_CHECK_AND_WAIT_PENDING_WORK (engine, MAX_NUM_PENDING_WORK);

  if (GST_C2_MODE_ENCODE (engine) && (gst_buffer_n_memory (buffer) > 0) &&
      gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {

    c2buffer = GstC2Utils::ImportGraphicBuffer (buffer);
  } else if (GST_C2_MODE_ENCODE (engine) && (gst_buffer_n_memory (buffer) > 0)) {
    GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);
    g_return_val_if_fail (vmeta != NULL, FALSE);

    gboolean isubwc = GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_UBWC);
    C2PixelFormat format = GstC2Utils::PixelFormat(vmeta->format, isubwc);

    uint32_t width = vmeta->width;
    uint32_t height = vmeta->height;

    std::shared_ptr<C2GraphicBlock> block;

    try {
      std::shared_ptr<C2GraphicMemory> c2mem = c2module->GetGraphicMemory();
      block = c2mem->Fetch(width, height, format);
    } catch (std::exception& e) {
      GST_ERROR ("Failed to fetch memory block, error: '%s'!", e.what());
      return FALSE;
    }

    c2buffer = GstC2Utils::CreateBuffer (buffer, block);
#if defined(ENABLE_LINEAR_DMABUF)
  } else if (GST_C2_MODE_DECODE (engine) && (gst_buffer_n_memory (buffer) > 0) &&
      gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {

    c2buffer = GstC2Utils::ImportLinearBuffer (buffer);
#endif // ENABLE_LINEAR_DMABUF
  } else if (GST_C2_MODE_DECODE (engine) && (gst_buffer_n_memory (buffer) > 0)) {
    std::shared_ptr<C2LinearBlock> block;
    uint32_t size = gst_buffer_get_size (buffer);

    try {
      std::shared_ptr<C2LinearMemory> c2mem = c2module->GetLinearMemory();
      block = c2mem->Fetch(size);
    } catch (std::exception& e) {
      GST_ERROR ("Failed to fetch memory block, error: '%s'!", e.what());
      return FALSE;
    }

    c2buffer = GstC2Utils::CreateBuffer (buffer, block);
  }

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DROPPABLE))
    flags |= C2FrameData::FLAG_DROP_FRAME;

  if (GST_CLOCK_TIME_IS_VALID (frame->dts))
    timestamp = GST_TIME_AS_USECONDS (frame->dts);
  else if (GST_CLOCK_TIME_IS_VALID (frame->pts))
    timestamp = GST_TIME_AS_USECONDS (frame->pts);

  // Get per frame settings. TODO: Right now this is only ROI data.
  GstC2QuantRegions *roiparam = reinterpret_cast<GstC2QuantRegions*>(
      gst_video_codec_frame_get_user_data (frame));

  if (roiparam != NULL) {
    std::unique_ptr<C2Param> c2param;
    GstC2Utils::UnpackPayload(GST_C2_PARAM_ROI_ENCODE, roiparam, c2param);
    settings.push_back(std::move(c2param));
  }

  try {
    c2module->Queue (c2buffer, settings, index, timestamp, flags);
    GST_DEBUG ("Queued buffer %p", buffer);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to queue frame, error: '%s'!", e.what());
    return FALSE;
  }

  GST_C2_ENGINE_INCREMENT_PENDING_WORK (engine);
  return TRUE;
}
