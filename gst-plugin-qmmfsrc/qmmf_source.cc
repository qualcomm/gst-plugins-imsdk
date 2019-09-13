/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qmmf_source.h"

#include <gst/gstplugin.h>
#include <gst/gstpadtemplate.h>
#include <gst/gstelementfactory.h>
#include <gst/allocators/allocators.h>

#include "qmmf_source_video_pad.h"
#include "qmmf_source_audio_pad.h"

using namespace qmmf::recorder;

#define QMMF_CHECK(element, expr, error) \
  if (expr) { \
    GST_ERROR_OBJECT(element, "%s", error); \
    return FALSE; \
  }

#define VIDEO_TRACK_ID_OFFSET (0x01)
#define AUDIO_TRACK_ID_OFFSET (0xFF)

// Declare static GstDebugCategory variable for qmmfsrc.
GST_DEBUG_CATEGORY_STATIC(qmmfsrc_debug);

// Define default gstreamer core debug log category for qmmfsrc.
#define GST_CAT_DEFAULT qmmfsrc_debug

// Declare qmmfsrc_class_init() and qmmfsrc_init() functions, implement
// qmmfsrc_get_type() function and set qmmfsrc_parent_class variable.
G_DEFINE_TYPE(GstQmmfSrc, qmmfsrc, GST_TYPE_ELEMENT);

enum {
  LAST_SIGNAL
};

enum {
  ARG_0
};

enum {
  PROP_0,
};

static G_DEFINE_QUARK(QmmfBufferQDataQuark, qmmf_buffer_qdata);

static GstStaticPadTemplate kVideoRequestTemplate =
    GST_STATIC_PAD_TEMPLATE("video_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_VIDEO_CAPS("x-h264",
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_CAPS_WITH_FEATURES("x-h264",
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_CAPS("x-h265",
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_CAPS_WITH_FEATURES("x-h265",
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_CAPS("x-raw",
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_CAPS_WITH_FEATURES("x-raw",
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            )
        )
    );

static GstStaticPadTemplate kAudioRequestTemplate =
    GST_STATIC_PAD_TEMPLATE("audio_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_AUDIO_AAC_CAPS "; "
            QMMFSRC_AUDIO_AMR_CAPS "; "
            QMMFSRC_AUDIO_AMRWB_CAPS
        )
    );

static gboolean
qmmfsrc_pad_push_event(GstElement *element, GstPad *pad, GstEvent *event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  GST_DEBUG_OBJECT(qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME(event));
  return gst_pad_push_event(pad, gst_event_copy(event));
}

static gboolean
qmmfsrc_pad_send_event(GstElement *element, GstPad *pad, GstEvent *event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  GST_DEBUG_OBJECT(qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME(event));
  return gst_pad_send_event(pad, gst_event_copy(event));
}

static gboolean
qmmfsrc_pad_flush_buffers(GstElement *element, GstPad *pad, gpointer flush)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  GST_DEBUG_OBJECT(qmmfsrc, "Flush pad: %s", GST_PAD_NAME(pad));

  if (GST_IS_QMMFSRC_VIDEO_PAD(pad)) {
    qmmfsrc_video_pad_flush_buffers_queue(pad, GPOINTER_TO_UINT(flush));
  } else if (GST_IS_QMMFSRC_AUDIO_PAD(pad)) {
    qmmfsrc_audio_pad_flush_buffers_queue(pad, GPOINTER_TO_UINT(flush));
  }
  return TRUE;
}

static GstPad*
qmmfsrc_request_pad(GstElement *element, GstPadTemplate *templ,
                    const gchar *reqname, const GstCaps *caps)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS(element);

  gchar *padname = nullptr;
  GHashTable *indexes = nullptr;
  guint index = 0, nextindex = 0;
  gboolean isvideo = FALSE, isaudio = FALSE;
  GstPad *srcpad = nullptr;

  isvideo = (templ == gst_element_class_get_pad_template(klass, "video_%u"));
  isaudio = (templ == gst_element_class_get_pad_template(klass, "audio_%u"));

  if (!isvideo && !isaudio) {
    GST_ERROR_OBJECT(qmmfsrc, "Invalid pad template");
    return nullptr;
  }

  GST_QMMFSRC_LOCK(qmmfsrc);

  if ((reqname && sscanf(reqname, "video_%u", &index) == 1) ||
      (reqname && sscanf(reqname, "audio_%u", &index) == 1)) {
    if (g_hash_table_contains(qmmfsrc->srcindexes, GUINT_TO_POINTER(index))) {
      GST_ERROR_OBJECT(qmmfsrc, "Source pad name %s is not unique", reqname);
      GST_QMMFSRC_UNLOCK(qmmfsrc);
      return nullptr;
    }

    // Update the next video pad index set his name.
    nextindex = (index >= qmmfsrc->nextidx) ? index + 1 : qmmfsrc->nextidx;
  } else {
    index = qmmfsrc->nextidx;
    // Find an unused source pad index.
    while (g_hash_table_contains(qmmfsrc->srcindexes, GUINT_TO_POINTER(index))) {
      index++;
    }
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  if (isvideo) {
    padname = g_strdup_printf("video_%u", index);
    srcpad = qmmfsrc_request_video_pad(element, templ, padname, index);

    indexes = qmmfsrc->vidindexes;
    g_free(padname);
  } else if (isaudio) {
    padname = g_strdup_printf("audio_%u", index);
    srcpad = qmmfsrc_request_audio_pad(element, templ, padname, index);

    indexes = qmmfsrc->audindexes;
    g_free(padname);
  }

  if (srcpad == nullptr) {
    GST_ERROR_OBJECT(element, "Failed to create pad!");
    GST_QMMFSRC_UNLOCK(qmmfsrc);
    return nullptr;
  }

  qmmfsrc->nextidx = nextindex;
  g_hash_table_insert(qmmfsrc->srcindexes, GUINT_TO_POINTER(index), nullptr);
  g_hash_table_insert(indexes, GUINT_TO_POINTER(index), nullptr);

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  return srcpad;
}

static void
qmmfsrc_release_pad(GstElement *element, GstPad *pad)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  guint index = 0;

  GST_QMMFSRC_LOCK(qmmfsrc);

  if (GST_IS_QMMFSRC_VIDEO_PAD(pad)) {
    index = GST_QMMFSRC_VIDEO_PAD(pad)->index;
    qmmfsrc_release_video_pad(element, pad);
    g_hash_table_remove(qmmfsrc->vidindexes, GUINT_TO_POINTER(index));
  } else if (GST_IS_QMMFSRC_AUDIO_PAD(pad)) {
    index = GST_QMMFSRC_AUDIO_PAD(pad)->index;
    qmmfsrc_release_audio_pad(element, pad);
    g_hash_table_remove(qmmfsrc->audindexes, GUINT_TO_POINTER(index));
  }

  g_hash_table_remove(qmmfsrc->srcindexes, GUINT_TO_POINTER(index));

  GST_QMMFSRC_UNLOCK(qmmfsrc);
}

void
qmmfsrc_gst_buffer_release(GstStructure *structure)
{
  GstPad *pad = nullptr;
  GstQmmfSrc *qmmfsrc = nullptr;

  guint value, track_id;
  std::vector<qmmf::BufferDescriptor> buffers;
  qmmf::BufferDescriptor buffer;

  gst_structure_get_uint(structure, "pad", &value);
  pad = GST_PAD(GUINT_TO_POINTER(value));

  qmmfsrc = GST_QMMFSRC(gst_pad_get_parent(pad));
  if (GST_IS_QMMFSRC_VIDEO_PAD(pad)) {
    track_id = GST_QMMFSRC_VIDEO_PAD(pad)->id;
  } else if (GST_IS_QMMFSRC_AUDIO_PAD(pad)) {
    track_id = GST_QMMFSRC_AUDIO_PAD(pad)->id;
  }

  gst_structure_get_uint(structure, "data", &value);
  buffer.data = GUINT_TO_POINTER(value);

  gst_structure_get_int(structure, "fd", &buffer.fd);
  gst_structure_get_uint(structure, "bufid", &buffer.buf_id);
  gst_structure_get_uint(structure, "size", &buffer.size);
  gst_structure_get_uint(structure, "capacity", &buffer.capacity);
  gst_structure_get_uint(structure, "offset", &buffer.offset);
  gst_structure_get_uint64(structure, "timestamp", &buffer.timestamp);
  gst_structure_get_uint(structure, "flag", &buffer.flag);

  buffers.push_back(buffer);
  qmmfsrc->recorder->ReturnTrackBuffer(qmmfsrc->session_id, track_id, buffers);

  gst_structure_free(structure);
}

GstBuffer *
qmmfsrc_gst_buffer_new_wrapped(GstPad *pad, const qmmf::BufferDescriptor *buffer)
{
  GstAllocator *allocator = nullptr;
  GstMemory *gstmemory = nullptr;
  GstBuffer *gstbuffer = nullptr;
  GstStructure *structure = nullptr;

  // Create a GstBuffer.
  gstbuffer = gst_buffer_new();
  g_return_val_if_fail(gstbuffer != nullptr, nullptr);

  // Create a FD backed allocator.
  allocator = gst_fd_allocator_new();
  g_return_val_if_fail(allocator != nullptr, nullptr);

  // Wrap our buffer memory block in FD backed memory.
  gstmemory = gst_fd_allocator_alloc(
      allocator, buffer->fd, buffer->capacity,
      GST_FD_MEMORY_FLAG_DONT_CLOSE
  );
  g_return_val_if_fail(gstmemory != nullptr, nullptr);

  // Set the actual size filled with data.
  gst_memory_resize(gstmemory, buffer->offset, buffer->size);

  // Append the FD backed memory to the newly created GstBuffer.
  gst_buffer_append_memory(gstbuffer, gstmemory);

  // Unreference the allocator so that it is owned only by the gstmemory.
  gst_object_unref(allocator);

  // GSreamer structure for later recreating the QMMF buffer to be returned.
  structure = gst_structure_new(
      "QMMF_BUFFER",
      "pad", G_TYPE_UINT, GPOINTER_TO_UINT(pad),
      "data", G_TYPE_UINT, GPOINTER_TO_UINT(buffer->data),
      "fd", G_TYPE_INT, buffer->fd,
      "bufid", G_TYPE_UINT, buffer->buf_id,
      "size", G_TYPE_UINT, buffer->size,
      "capacity", G_TYPE_UINT, buffer->capacity,
      "offset", G_TYPE_UINT, buffer->offset,
      "timestamp", G_TYPE_UINT64, buffer->timestamp,
      "flag", G_TYPE_UINT, buffer->flag,
      nullptr
  );
  g_return_val_if_fail(structure != nullptr, nullptr);

  // Set a notification function to signal when the buffer is no longer used.
  gst_mini_object_set_qdata(
      GST_MINI_OBJECT(gstbuffer), qmmf_buffer_qdata_quark(),
      structure, (GDestroyNotify)qmmfsrc_gst_buffer_release
  );

  return gstbuffer;
}

void
qmmfsrc_free_queue_item(GstDataQueueItem *item)
{
  g_slice_free(GstDataQueueItem, item);
}

// TODO Maybe put Recorder C++ code in separate files ?!
void VideoTrackEventCb(uint32_t track_id, qmmf::recorder::EventType type,
                       void *data, size_t size) {
}

// TODO Maybe put Recorder C++ code in separate files ?!
void AudioTrackEventCb(uint32_t track_id, qmmf::recorder::EventType type,
                       void *data, size_t size) {
}

// TODO Maybe put Recorder C++ code in separate files ?!
void VideoTrackDataCb(GstPad *pad, uint32_t track_id,
                      std::vector<qmmf::BufferDescriptor> buffers,
                      std::vector<qmmf::recorder::MetaData> metabufs) {
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(gst_pad_get_parent(pad));
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD(pad);

  guint idx = 0;

  guint numplanes = 0;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gint  stride[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };

  GstBuffer *gstbuffer = nullptr;
  GstBufferFlags gstflags;

  GstDataQueueItem *item = nullptr;

  for (idx = 0; idx < buffers.size(); ++idx) {
    qmmf::BufferDescriptor& buffer = buffers[idx];
    qmmf::recorder::MetaData& meta = metabufs[idx];

    gstbuffer = qmmfsrc_gst_buffer_new_wrapped(pad, &buffer);
    g_return_if_fail(gstbuffer != nullptr);

    gstflags = (buffer.flag & (guint)qmmf::BufferFlags::kFlagCodecConfig) ?
        GST_BUFFER_FLAG_HEADER : GST_BUFFER_FLAG_LIVE;
    GST_BUFFER_FLAG_SET(gstbuffer, gstflags);

    if (meta.meta_flag & static_cast<uint32_t>(MetaParamType::kCamBufMetaData)) {
      for (size_t i = 0; i < meta.cam_buffer_meta_data.num_planes; ++i) {
        stride[i] = meta.cam_buffer_meta_data.plane_info[i].stride;
        offset[i] = meta.cam_buffer_meta_data.plane_info[i].offset;
        numplanes++;
      }
    }

    // Set GStreamer buffer video metadata.
    gst_buffer_add_video_meta_full(
        gstbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        vpad->format, vpad->width, vpad->height,
        numplanes, offset, stride
    );

    GST_QMMFSRC_VIDEO_PAD_LOCK(pad);
    // Initialize the timestamp base value for calculating running time.
    vpad->tsbase = (vpad->tsbase == 0) ? buffer.timestamp : vpad->tsbase;

    gstbuffer->pts = buffer.timestamp - vpad->tsbase;
    gstbuffer->pts *= G_GUINT64_CONSTANT(1000);

    // TODO Does it need more precise calculations?!
    gstbuffer->duration = vpad->duration;
    GST_QMMFSRC_VIDEO_PAD_UNLOCK(pad);

    item = g_slice_new0(GstDataQueueItem);
    item->object = GST_MINI_OBJECT(gstbuffer);
    item->size = gst_buffer_get_size(gstbuffer);
    item->duration = GST_BUFFER_DURATION(gstbuffer);
    item->visible = TRUE;
    item->destroy = (GDestroyNotify)qmmfsrc_free_queue_item;

    // Push the buffer into the queue or free it on failure.
    if (!gst_data_queue_push(vpad->buffers, item)) {
      gst_buffer_unref(gstbuffer);
      item->destroy(item);
    }
  }
}

// TODO Maybe put Recorder C++ code in separate files ?!
void AudioTrackDataCb(GstPad *pad, uint32_t track_id,
                      std::vector<qmmf::BufferDescriptor> buffers,
                      std::vector<qmmf::recorder::MetaData> metabufs) {
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(gst_pad_get_parent(pad));
  GstQmmfSrcAudioPad *apad = GST_QMMFSRC_AUDIO_PAD(pad);

  guint idx = 0;

  GstBuffer *gstbuffer = nullptr;
  GstBufferFlags gstflags;

  GstDataQueueItem *item = nullptr;

  for (idx = 0; idx < buffers.size(); ++idx) {
    qmmf::BufferDescriptor& buffer = buffers[idx];

    gstbuffer = qmmfsrc_gst_buffer_new_wrapped(pad, &buffer);
    g_return_if_fail(gstbuffer != nullptr);

    gstflags = (buffer.flag & (guint)qmmf::BufferFlags::kFlagCodecConfig) ?
        GST_BUFFER_FLAG_HEADER : GST_BUFFER_FLAG_LIVE;
    GST_BUFFER_FLAG_SET(gstbuffer, gstflags);

    GST_QMMFSRC_AUDIO_PAD_LOCK(pad);
    // Initialize the timestamp base value for calculating running time.
    apad->tsbase = (apad->tsbase == 0) ? buffer.timestamp : apad->tsbase;

    gstbuffer->pts = buffer.timestamp - apad->tsbase;
    gstbuffer->pts *= G_GUINT64_CONSTANT(1000);

    // TODO Does it need more precise calculations?!
    gstbuffer->duration = apad->duration;
    GST_QMMFSRC_AUDIO_PAD_UNLOCK(pad);

    item = g_slice_new0(GstDataQueueItem);
    item->object = GST_MINI_OBJECT(gstbuffer);
    item->size = gst_buffer_get_size(gstbuffer);
    item->duration = GST_BUFFER_DURATION(gstbuffer);
    item->visible = TRUE;
    item->destroy = (GDestroyNotify)qmmfsrc_free_queue_item;

    // Push the buffer into the queue or free it on failure.
    if (!gst_data_queue_push(apad->buffers, item)) {
      gst_buffer_unref(gstbuffer);
      item->destroy(item);
    }
  }
}

static gboolean
qmmfsrc_open(GstElement *element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  GHashTableIter iter;
  gpointer key, value;

  GstPad *pad;
  GstQmmfSrcVideoPad *vpad;
  GstQmmfSrcAudioPad *apad;

  GstCaps *caps;
  GstStructure *structure;
  gboolean success;
  gint status;

  GST_DEBUG_OBJECT(qmmfsrc, "QMMF Source open");

  qmmf::recorder::RecorderCb cb;
  cb.event_cb = [] (qmmf::recorder::EventType type, void *data, size_t size)
      {  };

  status = qmmfsrc->recorder->Connect(cb);
  QMMF_CHECK(qmmfsrc, (status != 0), "Connect Failed!");

  GST_QMMFSRC_LOCK(qmmfsrc);

  g_hash_table_iter_init(&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD(g_list_nth_data(element->srcpads, g_direct_hash(key)));
    caps = gst_pad_get_allowed_caps(pad);
    structure = gst_caps_get_structure(caps, 0);

    vpad = GST_QMMFSRC_VIDEO_PAD(pad);

    GST_QMMFSRC_VIDEO_PAD_LOCK(vpad);

    success = gst_structure_get_int(structure, "camera", &vpad->camera);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps camera field!");

    success = gst_structure_get_int(structure, "width", &vpad->width);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps width field!");

    success = gst_structure_get_int(structure, "height", &vpad->height);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps height field!");

    gint fps_n, fps_d;
    success = gst_structure_get_fraction(structure, "framerate", &fps_n, &fps_d);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps framerate field!");

    vpad->duration = gst_util_uint64_scale_int(GST_SECOND, fps_d, fps_n);
    vpad->framerate = 1 / GST_TIME_AS_SECONDS(
        gst_guint64_to_gdouble(vpad->duration));

    const gchar *name = gst_structure_get_name(structure);
    if (g_strcmp0(name, "video/x-h264") == 0) {
      vpad->params = gst_structure_new_empty("h264");
      vpad->format = GST_VIDEO_FORMAT_ENCODED;
    } else if (g_strcmp0(name, "video/x-h265") == 0) {
      vpad->params = gst_structure_new_empty("h265");
      vpad->format = GST_VIDEO_FORMAT_ENCODED;
    } else if (g_strcmp0(name, "video/x-raw") == 0) {
      const gchar *format = gst_structure_get_string(structure, "format");
      vpad->params = gst_structure_new_empty(format);
      vpad->format = GST_VIDEO_FORMAT_NV12;
    }

    gpointer camera_id = GINT_TO_POINTER(vpad->camera);

    if (!g_hash_table_contains(qmmfsrc->camera_ids, camera_id)) {
      status = qmmfsrc->recorder->StartCamera(vpad->camera, vpad->framerate);
      QMMF_CHECK(qmmfsrc, (status != 0), "StartCamera Failed!");
      g_hash_table_insert(qmmfsrc->camera_ids, camera_id, nullptr);
    }

    GST_QMMFSRC_VIDEO_PAD_UNLOCK(vpad);

    gst_caps_unref(caps);
  }

  g_hash_table_iter_init(&iter, qmmfsrc->audindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD(g_list_nth_data(element->srcpads, g_direct_hash(key)));
    caps = gst_pad_get_allowed_caps(pad);
    structure = gst_caps_get_structure(caps, 0);

    apad = GST_QMMFSRC_AUDIO_PAD(pad);

    GST_QMMFSRC_AUDIO_PAD_LOCK(apad);

    success = gst_structure_get_int(structure, "device", &apad->device);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps device field!");

    success = gst_structure_get_int(structure, "channels", &apad->channels);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps channels field!");

    success = gst_structure_get_int(structure, "rate", &apad->samplerate);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps rate field!");

    success = gst_structure_get_int(structure, "bitdepth", &apad->bitdepth);
    QMMF_CHECK(qmmfsrc, !success, "Failed to get caps bitdepth field!");

    const gchar *name = gst_structure_get_name(structure);
    if (g_strcmp0(name, "audio/mpeg") == 0) {
      apad->params = gst_structure_new_empty("AAC");

      const gchar *type = gst_structure_get_string(structure, "stream-format");
      gst_structure_set(apad->params, "type", G_TYPE_STRING, type, nullptr);

      apad->duration = gst_util_uint64_scale_int(
          GST_SECOND, 1024, apad->samplerate);
    } else if (g_strcmp0(name, "audio/AMR") == 0) {
      apad->params = gst_structure_new_empty("AMR");

      // AMR has a hardcoded framerate of 50 fps.
      apad->duration = gst_util_uint64_scale_int(GST_SECOND, 1, 50);
      gst_structure_set(apad->params, "iswb", G_TYPE_BOOLEAN, FALSE, nullptr);
    } else if (g_strcmp0(name, "audio/AMR-WB") == 0) {
      apad->params = gst_structure_new_empty("AMR");
      gst_structure_set(apad->params, "iswb", G_TYPE_BOOLEAN, TRUE, nullptr);

      // AMR has a hardcoded framerate of 50 fps.
      apad->duration = gst_util_uint64_scale_int(GST_SECOND, 1, 50);
    }
    apad->format = GST_AUDIO_FORMAT_ENCODED;

    GST_QMMFSRC_AUDIO_PAD_UNLOCK(apad);

    gst_caps_unref(caps);
  }

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  GST_DEBUG_OBJECT(qmmfsrc, "QMMF Source opened");

  return TRUE;
}

static gboolean
qmmfsrc_close(GstElement *element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  GHashTableIter iter;
  gpointer key, value;
  gint status;

  GST_DEBUG_OBJECT(qmmfsrc, "QMMF Source close");

  GST_QMMFSRC_LOCK(qmmfsrc);

  g_hash_table_iter_init(&iter, qmmfsrc->camera_ids);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    status = qmmfsrc->recorder->StopCamera(g_direct_hash(key));
    QMMF_CHECK(qmmfsrc, (status != 0), "StopCamera Failed!");
  }
  g_hash_table_remove_all(qmmfsrc->camera_ids);

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  status = qmmfsrc->recorder->Disconnect();
  QMMF_CHECK(qmmfsrc, (status != 0), "Disconnect Failed!");

  GST_DEBUG_OBJECT(qmmfsrc, "QMMF Source closed");

  return TRUE;
}

static gboolean
qmmfsrc_create_session(GstElement *element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  GHashTableIter iter;
  gpointer key, value;

  GstPad *pad;
  GstQmmfSrcVideoPad *vpad;
  GstQmmfSrcAudioPad *apad;
  gint status;

  qmmf::recorder::SessionCb session_cbs;
  qmmf::recorder::TrackCb track_cbs;
  guint session_id, track_id;

  GST_DEBUG_OBJECT(qmmfsrc, "Create session");

  GST_QMMFSRC_LOCK(qmmfsrc);

  session_cbs.event_cb =
      [] (qmmf::recorder::EventType type, void *data, size_t size) { };

  status = qmmfsrc->recorder->CreateSession(session_cbs, &session_id);
  QMMF_CHECK(qmmfsrc, (status != 0), "CreateSession Failed!");
  qmmfsrc->session_id = session_id;

  g_hash_table_iter_init(&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD(g_list_nth_data(element->srcpads, g_direct_hash(key)));
    vpad = GST_QMMFSRC_VIDEO_PAD(pad);

    GST_QMMFSRC_VIDEO_PAD_LOCK(vpad);

    qmmf::VideoFormat format;

    const gchar *name = gst_structure_get_name(vpad->params);
    if (g_strcmp0(name, "h264") == 0) {
      format = qmmf::VideoFormat::kAVC;
    } else if (g_strcmp0(name, "h265") == 0) {
      format = qmmf::VideoFormat::kHEVC;
    } else if (g_strcmp0(name, "NV12") == 0) {
      format = qmmf::VideoFormat::kYUV;
    }

    VideoTrackCreateParam params(
      vpad->camera, format, vpad->width, vpad->height, vpad->framerate
    );

    track_cbs.event_cb =
        [&] (uint32_t track_id, EventType type, void *data, size_t size)
        { VideoTrackEventCb(track_id, type, data, size); };
    track_cbs.data_cb =
        [&, pad] (uint32_t track_id,
            std::vector<qmmf::BufferDescriptor> buffers,
            std::vector<MetaData> metabufs)
        { VideoTrackDataCb(pad, track_id, buffers, metabufs); };

    track_id = vpad->index + VIDEO_TRACK_ID_OFFSET;
    status = qmmfsrc->recorder->CreateVideoTrack(session_id, track_id,
                                                 params, track_cbs);
    QMMF_CHECK(qmmfsrc, (status != 0), "CreateVideoTrack Failed!");

    vpad->id = track_id;

    GST_QMMFSRC_VIDEO_PAD_UNLOCK(vpad);
  }

  g_hash_table_iter_init(&iter, qmmfsrc->audindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD(g_list_nth_data(element->srcpads, g_direct_hash(key)));
    apad = GST_QMMFSRC_AUDIO_PAD(pad);

    GST_QMMFSRC_AUDIO_PAD_LOCK(apad);

    AudioTrackCreateParam params;
    params.channels = apad->channels;
    params.sample_rate = apad->samplerate;
    params.bit_depth = apad->bitdepth;
    params.in_devices_num = 1;
    params.in_devices[0] = apad->device;
    params.out_device = 0;
    params.flags = 0;

    const gchar *name = gst_structure_get_name(apad->params);
    if (g_strcmp0(name, "AAC") == 0) {
      params.format = qmmf::AudioFormat::kAAC;
      params.codec_params.aac.bit_rate = 8 * apad->samplerate * apad->channels;
      params.codec_params.aac.mode = qmmf::AACMode::kAALC;

      const gchar *type = gst_structure_get_string(apad->params, "type");
      if (g_strcmp0(type, "adts") == 0) {
        params.codec_params.aac.format = qmmf::AACFormat::kADTS;
      } else if (g_strcmp0(type, "adif") == 0) {
        params.codec_params.aac.format = qmmf::AACFormat::kADIF;
      } else if (g_strcmp0(type, "raw") == 0) {
        params.codec_params.aac.format = qmmf::AACFormat::kRaw;
      } else if (g_strcmp0(type, "mp4ff") == 0) {
        params.codec_params.aac.format = qmmf::AACFormat::kMP4FF;
      }
    } else if (g_strcmp0(name, "AMR") == 0) {
      params.format = qmmf::AudioFormat::kAMR;
      params.codec_params.amr.bit_rate = 12200;

      gboolean iswb;
      gst_structure_get_boolean(apad->params, "iswb", &iswb);
      params.codec_params.amr.isWAMR = iswb;
    }

    track_cbs.event_cb =
        [&] (uint32_t track_id, EventType type, void *data, size_t size)
        { AudioTrackEventCb(track_id, type, data, size); };
    track_cbs.data_cb =
        [&, pad] (uint32_t track_id,
            std::vector<qmmf::BufferDescriptor> buffers,
            std::vector<MetaData> metabufs)
        { AudioTrackDataCb(pad, track_id, buffers, metabufs); };

    track_id = apad->index + AUDIO_TRACK_ID_OFFSET;
    status = qmmfsrc->recorder->CreateAudioTrack(session_id, track_id,
                                                 params, track_cbs);
    QMMF_CHECK(qmmfsrc, (status != 0), "CreateAudioTrack Failed!");

    apad->id = track_id;

    GST_QMMFSRC_AUDIO_PAD_UNLOCK(apad);
  }

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  GST_DEBUG_OBJECT(qmmfsrc, "Session created");

  return TRUE;
}

static gboolean
qmmfsrc_delete_session(GstElement *element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  GHashTableIter iter;
  gpointer key, value;

  GstPad *pad;
  GstQmmfSrcVideoPad *vpad;
  GstQmmfSrcAudioPad *apad;
  gint status;

  GST_DEBUG_OBJECT(qmmfsrc, "Delete session");

  GST_QMMFSRC_LOCK(qmmfsrc);

  g_hash_table_iter_init(&iter, qmmfsrc->audindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD(g_list_nth_data(element->srcpads, g_direct_hash(key)));
    apad = GST_QMMFSRC_AUDIO_PAD(pad);

    status = qmmfsrc->recorder->DeleteAudioTrack(qmmfsrc->session_id, apad->id);
    QMMF_CHECK(qmmfsrc, (status != 0), "DeleteVideoTrack Failed!");
  }

  g_hash_table_iter_init(&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD(g_list_nth_data(element->srcpads, g_direct_hash(key)));
    vpad = GST_QMMFSRC_VIDEO_PAD(pad);

    status = qmmfsrc->recorder->DeleteVideoTrack(qmmfsrc->session_id, vpad->id);
    QMMF_CHECK(qmmfsrc, (status != 0), "DeleteVideoTrack Failed!");
  }

  status = qmmfsrc->recorder->DeleteSession(qmmfsrc->session_id);
  QMMF_CHECK(qmmfsrc, (status != 0), "DeleteSession Failed!");

  qmmfsrc->session_id = 0;

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  GST_DEBUG_OBJECT(qmmfsrc, "Session deleted");

  return TRUE;
}

static gboolean
qmmfsrc_start_session(GstElement *element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  gboolean success = FALSE, flush = FALSE;
  gint status = 0;

  GST_DEBUG_OBJECT(qmmfsrc, "Starting session");

  GST_QMMFSRC_LOCK(qmmfsrc);

  success = gst_element_foreach_src_pad(
      element, (GstElementForeachPadFunc)qmmfsrc_pad_flush_buffers,
      GUINT_TO_POINTER(flush)
  );
  QMMF_CHECK(qmmfsrc, !success, "Failed to flush source pads!");

  status = qmmfsrc->recorder->StartSession(qmmfsrc->session_id);
  QMMF_CHECK(qmmfsrc, (status != 0), "StartSession Failed!");

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  GST_DEBUG_OBJECT(qmmfsrc, "Session started");

  return TRUE;
}

static gboolean
qmmfsrc_stop_session(GstElement *element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  gboolean success = FALSE, flush = TRUE;
  gint status = 0;

  GST_DEBUG_OBJECT(qmmfsrc, "Stopping session");

  GST_QMMFSRC_LOCK(qmmfsrc);

  success = gst_element_foreach_src_pad(
      element, (GstElementForeachPadFunc)qmmfsrc_pad_flush_buffers,
      GUINT_TO_POINTER(flush)
  );
  QMMF_CHECK(qmmfsrc, !success, "Failed to flush source pads!");

  status = qmmfsrc->recorder->StopSession(qmmfsrc->session_id, false);
  QMMF_CHECK(qmmfsrc, (status != 0), "StopSession Failed!");

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  GST_DEBUG_OBJECT(qmmfsrc, "Session stopped");

  return TRUE;
}

static GstStateChangeReturn
qmmfsrc_change_state(GstElement *element, GstStateChange transition)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!qmmfsrc_open(element)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to Open!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!qmmfsrc_create_session(element)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to create session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!qmmfsrc_start_session(element)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to start session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS(qmmfsrc_parent_class)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT(qmmfsrc, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      // Return NO_PREROLL to inform bin/pipeline we won't be able to
      // produce data in the PAUSED state, as this is a live source.
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (!qmmfsrc_stop_session(element)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to stop session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!qmmfsrc_delete_session(element)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to delete session!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!qmmfsrc_close(element)) {
        GST_ERROR_OBJECT(qmmfsrc, "Failed to Close!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      // Otherwise it's success, we don't want to return spurious
      // NO_PREROLL or ASYNC from internal elements as we care for
      // state changes ourselves here
      // This is to catch PAUSED->PAUSED and PLAYING->PLAYING transitions.
      ret = (GST_STATE_TRANSITION_NEXT(transition) == GST_STATE_PAUSED) ?
          GST_STATE_CHANGE_NO_PREROLL : GST_STATE_CHANGE_SUCCESS;
      break;
  }

  return ret;
}

static gboolean
qmmfsrc_send_event(GstElement *element, GstEvent *event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);
  gboolean success = TRUE;

  GST_DEBUG_OBJECT(qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME(event));

  switch (GST_EVENT_TYPE(event)) {
    // Bidirectional events.
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT(qmmfsrc, "Pushing FLUSH_START event");
      success = gst_element_foreach_src_pad(
          element, (GstElementForeachPadFunc)qmmfsrc_pad_send_event, event
      );
      gst_event_unref(event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT(qmmfsrc, "Pushing FLUSH_STOP event");
      success = gst_element_foreach_src_pad(
          element, (GstElementForeachPadFunc)qmmfsrc_pad_send_event, event
      );
      gst_event_unref(event);
      break;

    // Downstream serialized events.
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT(qmmfsrc, "Pushing EOS event downstream");
      success = gst_element_foreach_src_pad(
          element, (GstElementForeachPadFunc)qmmfsrc_pad_push_event, event
      );
      success = gst_element_foreach_src_pad(
          element, (GstElementForeachPadFunc)qmmfsrc_pad_flush_buffers,
          GUINT_TO_POINTER(TRUE)
      );
      gst_event_unref(event);
      break;
    default:
      success =
          GST_ELEMENT_CLASS(qmmfsrc_parent_class)->send_event(element, event);
      break;
  }

  return success;
}

// GstElement virtual method implementation. Sets the element's properties.
static void
qmmfsrc_set_property(GObject *object, guint property_id,
                     const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

// GstElement virtual method implementation. Sets the element's properties.
static void
qmmfsrc_get_property(GObject *object, guint property_id,
                     GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

// GstElement virtual method implementation. Called when plugin is destroyed.
static void
qmmfsrc_finalize(GObject* object)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(object);

  if (qmmfsrc->srcindexes != nullptr) {
    g_hash_table_remove_all(qmmfsrc->srcindexes);
    g_hash_table_destroy(qmmfsrc->srcindexes);
    qmmfsrc->srcindexes = nullptr;
  }

  if (qmmfsrc->vidindexes != nullptr) {
    g_hash_table_remove_all(qmmfsrc->vidindexes);
    g_hash_table_destroy(qmmfsrc->vidindexes);
    qmmfsrc->vidindexes = nullptr;
  }

  if (qmmfsrc->audindexes != nullptr) {
    g_hash_table_remove_all(qmmfsrc->audindexes);
    g_hash_table_destroy(qmmfsrc->audindexes);
    qmmfsrc->audindexes = nullptr;
  }

  if (qmmfsrc->camera_ids != nullptr) {
    g_hash_table_remove_all(qmmfsrc->camera_ids);
    g_hash_table_destroy(qmmfsrc->camera_ids);
    qmmfsrc->camera_ids = nullptr;
  }

  delete qmmfsrc->recorder;
  qmmfsrc->recorder = nullptr;

  G_OBJECT_CLASS(qmmfsrc_parent_class)->finalize(object);
}

// GObject element class initialization function.
static void
qmmfsrc_class_init(GstQmmfSrcClass *klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement = GST_ELEMENT_CLASS(klass);

  gobject->set_property = GST_DEBUG_FUNCPTR(qmmfsrc_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR(qmmfsrc_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR(qmmfsrc_finalize);

  gst_element_class_add_static_pad_template(
      gstelement, &kVideoRequestTemplate
  );
  gst_element_class_add_static_pad_template(
      gstelement, &kAudioRequestTemplate
  );
  gst_element_class_set_static_metadata(
      gstelement, "QMMF Video/Audio Source", "Source/Video",
      "Reads frames from a device via QMMF service", "QTI"
  );

  gstelement->request_new_pad = GST_DEBUG_FUNCPTR(qmmfsrc_request_pad);
  gstelement->release_pad = GST_DEBUG_FUNCPTR(qmmfsrc_release_pad);

  gstelement->send_event = GST_DEBUG_FUNCPTR(qmmfsrc_send_event);
  gstelement->change_state = GST_DEBUG_FUNCPTR(qmmfsrc_change_state);

  // Initializes a new qmmfsrc GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT(qmmfsrc_debug, "qmmfsrc", 0, "QTI QMMF Source");
}

// GObject element initialization function.
static void
qmmfsrc_init(GstQmmfSrc *qmmfsrc)
{
  GST_DEBUG_OBJECT(qmmfsrc, "Initializing");

  qmmfsrc->srcindexes = g_hash_table_new(nullptr, nullptr);
  qmmfsrc->nextidx = 0;

  qmmfsrc->vidindexes = g_hash_table_new(nullptr, nullptr);
  qmmfsrc->audindexes = g_hash_table_new(nullptr, nullptr);

  qmmfsrc->camera_ids = g_hash_table_new(nullptr, nullptr);

  qmmfsrc->recorder = new qmmf::recorder::Recorder();
  g_return_if_fail(qmmfsrc->recorder != nullptr);

  GST_OBJECT_FLAG_SET(qmmfsrc, GST_ELEMENT_FLAG_SOURCE);
}

static gboolean
plugin_init(GstPlugin* plugin)
{
  return gst_element_register(plugin, "qmmfsrc", GST_RANK_PRIMARY,
                              GST_TYPE_QMMFSRC);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qmmfsrc,
    "QTI QMMF plugin library",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
