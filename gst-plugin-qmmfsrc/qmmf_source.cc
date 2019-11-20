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

#include <qmmf-sdk/qmmf_recorder_extra_param_tags.h>

#include "qmmf_source_utils.h"
#include "qmmf_source_image_pad.h"
#include "qmmf_source_video_pad.h"
#include "qmmf_source_audio_pad.h"

// Declare static GstDebugCategory variable for qmmfsrc.
GST_DEBUG_CATEGORY_STATIC (qmmfsrc_debug);
#define GST_CAT_DEFAULT qmmfsrc_debug

#define DEFAULT_PROP_CAMERA_ID              0
#define DEFAULT_PROP_CAMERA_EFFECT_MODE     EFFECT_MODE_OFF
#define DEFAULT_PROP_CAMERA_SCENE_MODE      SCENE_MODE_DISABLED
#define DEFAULT_PROP_CAMERA_ANTIBANDING     ANTIBANDING_MODE_AUTO
#define DEFAULT_PROP_CAMERA_AE_COMPENSATION 0
#define DEFAULT_PROP_CAMERA_AE_LOCK         FALSE
#define DEFAULT_PROP_CAMERA_AWB_MODE        AWB_MODE_AUTO
#define DEFAULT_PROP_CAMERA_AWB_LOCK        FALSE

static void gst_qmmfsrc_child_proxy_init (gpointer g_iface, gpointer data);

// Declare qmmfsrc_class_init() and qmmfsrc_init() functions, implement
// qmmfsrc_get_type() function and set qmmfsrc_parent_class variable.
G_DEFINE_TYPE_WITH_CODE (GstQmmfSrc, qmmfsrc, GST_TYPE_ELEMENT,
     G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY, gst_qmmfsrc_child_proxy_init));

enum
{
  CAPTURE_IMAGE_SIGNAL,
  CANCEL_CAPTURE_SIGNAL,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CAMERA_EFFECT_MODE,
  PROP_CAMERA_SCENE_MODE,
  PROP_CAMERA_ANTIBANDING_MODE,
  PROP_CAMERA_AE_COMPENSATION,
  PROP_CAMERA_AE_LOCK,
  PROP_CAMERA_AWB_MODE,
  PROP_CAMERA_AWB_LOCK,
};

static guint qmmfsrc_signals[LAST_SIGNAL];

static G_DEFINE_QUARK(QmmfBufferQDataQuark, qmmf_buffer_qdata);

static GstStaticPadTemplate qmmfsrc_video_src_template =
    GST_STATIC_PAD_TEMPLATE("video_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_VIDEO_H264_CAPS (
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_H264_CAPS_WITH_FEATURES (
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_RAW_CAPS(
                "{ NV12 }"
            ) "; "
            QMMFSRC_VIDEO_RAW_CAPS_WITH_FEATURES(
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            )
        )
    );

static GstStaticPadTemplate qmmfsrc_audio_src_template =
    GST_STATIC_PAD_TEMPLATE("audio_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_AUDIO_AAC_CAPS "; "
            QMMFSRC_AUDIO_AMR_CAPS "; "
            QMMFSRC_AUDIO_AMRWB_CAPS "; "
            QMMFSRC_AUDIO_PCM_CAPS
        )
    );

static GstStaticPadTemplate qmmfsrc_image_src_template =
    GST_STATIC_PAD_TEMPLATE("image_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
            QMMFSRC_IMAGE_JPEG_CAPS (
                "{ NV12 }"
            ) "; "
            QMMFSRC_IMAGE_JPEG_CAPS_WITH_FEATURES (
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            ) "; "
            QMMFSRC_IMAGE_RAW_CAPS(
                "{ NV12 }"
            ) "; "
            QMMFSRC_IMAGE_RAW_CAPS_WITH_FEATURES(
                GST_CAPS_FEATURE_MEMORY_GBM,
                "{ NV12 }"
            )
        )
    );

static gboolean
qmmfsrc_pad_push_event (GstElement * element, GstPad * pad, GstEvent * event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_copy (event));
}

static gboolean
qmmfsrc_pad_send_event (GstElement * element, GstPad * pad, GstEvent * event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_send_event (pad, gst_event_copy (event));
}

static gboolean
qmmfsrc_pad_flush_buffers (GstElement * element, GstPad * pad, gpointer flush)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstSegment segment;
  GstEvent *event;

  GST_DEBUG_OBJECT (qmmfsrc, "Flush pad: %s", GST_PAD_NAME (pad));

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    qmmfsrc_video_pad_flush_buffers_queue (pad, GPOINTER_TO_UINT (flush));
  } else if (GST_IS_QMMFSRC_AUDIO_PAD (pad)) {
    qmmfsrc_audio_pad_flush_buffers_queue (pad, GPOINTER_TO_UINT (flush));
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    qmmfsrc_image_pad_flush_buffers_queue (pad, GPOINTER_TO_UINT (flush));
  }

  // Ensure segment (format) is properly setup.
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  gst_pad_push_event (pad, event);

  return TRUE;
}

static GstPad*
qmmfsrc_request_pad (GstElement * element, GstPadTemplate * templ,
                     const gchar * reqname, const GstCaps * caps)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  gchar *padname = nullptr;
  GHashTable *indexes = nullptr;
  guint index = 0, nextindex = 0;
  gboolean isvideo = FALSE, isaudio = FALSE, isimage = FALSE;
  GstPad *srcpad = nullptr;

  isvideo = (templ == gst_element_class_get_pad_template (klass, "video_%u"));
  isaudio = (templ == gst_element_class_get_pad_template (klass, "audio_%u"));
  isimage = (templ == gst_element_class_get_pad_template (klass, "image_%u"));

  if (!isvideo && !isaudio && !isimage) {
    GST_ERROR_OBJECT (qmmfsrc, "Invalid pad template");
    return nullptr;
  }

  GST_QMMFSRC_LOCK (qmmfsrc);

  if ((reqname && sscanf (reqname, "video_%u", &index) == 1) ||
      (reqname && sscanf (reqname, "audio_%u", &index) == 1) ||
      (reqname && sscanf (reqname, "image_%u", &index) == 1)) {
    if (g_hash_table_contains (qmmfsrc->srcpads, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (qmmfsrc, "Source pad name %s is not unique", reqname);
      GST_QMMFSRC_UNLOCK (qmmfsrc);
      return nullptr;
    }

    // Update the next video pad index set his name.
    nextindex = (index >= qmmfsrc->nextidx) ? index + 1 : qmmfsrc->nextidx;
  } else {
    index = qmmfsrc->nextidx;
    // Find an unused source pad index.
    while (g_hash_table_contains (qmmfsrc->srcpads, GUINT_TO_POINTER (index))) {
      index++;
    }
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  if (isvideo) {
    padname = g_strdup_printf ("video_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting video pad %s (%d)", padname, index);
    srcpad = qmmfsrc_request_video_pad (templ, padname, index);

    indexes = qmmfsrc->vidindexes;
    g_free (padname);
  } else if (isaudio) {
    padname = g_strdup_printf ("audio_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting audio pad %s (%d)", padname, index);
    srcpad = qmmfsrc_request_audio_pad (templ, padname, index);

    indexes = qmmfsrc->audindexes;
    g_free (padname);
  } else if (isimage) {
    // Currently there is support for only one image pad.
    g_return_val_if_fail (g_hash_table_size (qmmfsrc->imgindexes) == 0, NULL);

    padname = g_strdup_printf ("image_%u", index);

    GST_DEBUG_OBJECT(element, "Requesting image pad %d (%s)", index, padname);
    srcpad = qmmfsrc_request_image_pad (templ, padname, index);

    indexes = qmmfsrc->imgindexes;
    g_free (padname);
  }

  if (srcpad == nullptr) {
    GST_ERROR_OBJECT (element, "Failed to create pad %d!", index);
    GST_QMMFSRC_UNLOCK (qmmfsrc);
    return nullptr;
  }

  GST_DEBUG_OBJECT (qmmfsrc, "Created pad with index %d", index);

  qmmfsrc->nextidx = nextindex;
  g_hash_table_insert (qmmfsrc->srcpads, GUINT_TO_POINTER (index), srcpad);
  g_hash_table_insert (indexes, GUINT_TO_POINTER (index), nullptr);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  gst_element_add_pad (element, srcpad);
  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (srcpad),
      GST_OBJECT_NAME (srcpad));

  return srcpad;
}

static void
qmmfsrc_release_pad (GstElement * element, GstPad * pad)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  guint index = 0;

  GST_QMMFSRC_LOCK (qmmfsrc);

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    index = GST_QMMFSRC_VIDEO_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing video pad %d", index);

    qmmfsrc_release_video_pad (element, pad);
    g_hash_table_remove (qmmfsrc->vidindexes, GUINT_TO_POINTER (index));
  } else if (GST_IS_QMMFSRC_AUDIO_PAD (pad)) {
    index = GST_QMMFSRC_AUDIO_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing audio pad %d", index);

    qmmfsrc_release_audio_pad (element, pad);
    g_hash_table_remove (qmmfsrc->audindexes, GUINT_TO_POINTER (index));
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    index = GST_QMMFSRC_IMAGE_PAD (pad)->index;
    GST_DEBUG_OBJECT (element, "Releasing image pad %d", index);

    qmmfsrc_release_image_pad (element, pad);
    g_hash_table_remove (qmmfsrc->imgindexes, GUINT_TO_POINTER (index));
  }

  g_hash_table_remove (qmmfsrc->srcpads, GUINT_TO_POINTER (index));
  GST_DEBUG_OBJECT (qmmfsrc, "Deleted pad %d", index);

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

void
qmmfsrc_gst_buffer_release (GstStructure * structure)
{
  GstPad *pad = nullptr;
  GstQmmfSrc *qmmfsrc = nullptr;

  guint value;
  std::vector<qmmf::BufferDescriptor> buffers;
  qmmf::BufferDescriptor buffer;

  gst_structure_get_uint (structure, "pad", &value);
  pad = GST_PAD (GUINT_TO_POINTER(value));

  gst_structure_get_uint (structure, "data", &value);
  buffer.data = GUINT_TO_POINTER (value);

  gst_structure_get_int (structure, "fd", &buffer.fd);
  gst_structure_get_uint (structure, "bufid", &buffer.buf_id);
  gst_structure_get_uint (structure, "size", &buffer.size);
  gst_structure_get_uint (structure, "capacity", &buffer.capacity);
  gst_structure_get_uint (structure, "offset", &buffer.offset);
  gst_structure_get_uint64 (structure, "timestamp", &buffer.timestamp);
  gst_structure_get_uint (structure, "flag", &buffer.flag);

  buffers.push_back (buffer);

  qmmfsrc = GST_QMMFSRC (gst_pad_get_parent(pad));

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    guint track_id = GST_QMMFSRC_VIDEO_PAD (pad)->id;
    qmmfsrc->recorder->ReturnTrackBuffer (qmmfsrc->session_id, track_id, buffers);
  } else if (GST_IS_QMMFSRC_AUDIO_PAD (pad)) {
    guint track_id = GST_QMMFSRC_AUDIO_PAD (pad)->id;
    qmmfsrc->recorder->ReturnTrackBuffer (qmmfsrc->session_id, track_id, buffers);
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    gint camera_id = qmmfsrc->camera_id;
    qmmfsrc->recorder->ReturnImageCaptureBuffer (camera_id, buffer);
  }

  gst_structure_free (structure);
}

GstBuffer *
qmmfsrc_gst_buffer_new_wrapped (GstPad * pad, const qmmf::BufferDescriptor * buffer)
{
  GstAllocator *allocator = nullptr;
  GstMemory *gstmemory = nullptr;
  GstBuffer *gstbuffer = nullptr;
  GstStructure *structure = nullptr;

  // Create a GstBuffer.
  gstbuffer = gst_buffer_new ();
  g_return_val_if_fail (gstbuffer != nullptr, nullptr);

  // Create a FD backed allocator.
  allocator = gst_fd_allocator_new ();
  g_return_val_if_fail (allocator != nullptr, nullptr);

  // Wrap our buffer memory block in FD backed memory.
  gstmemory = gst_fd_allocator_alloc (
      allocator, buffer->fd, buffer->capacity,
      GST_FD_MEMORY_FLAG_DONT_CLOSE
  );
  g_return_val_if_fail (gstmemory != nullptr, nullptr);

  // Set the actual size filled with data.
  gst_memory_resize (gstmemory, buffer->offset, buffer->size);

  // Append the FD backed memory to the newly created GstBuffer.
  gst_buffer_append_memory (gstbuffer, gstmemory);

  // Unreference the allocator so that it is owned only by the gstmemory.
  gst_object_unref (allocator);

  // GSreamer structure for later recreating the QMMF buffer to be returned.
  structure = gst_structure_new (
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
  g_return_val_if_fail (structure != nullptr, nullptr);

  // Set a notification function to signal when the buffer is no longer used.
  gst_mini_object_set_qdata (
      GST_MINI_OBJECT (gstbuffer), qmmf_buffer_qdata_quark (),
      structure, (GDestroyNotify) qmmfsrc_gst_buffer_release
  );

  return gstbuffer;
}

void
qmmfsrc_free_queue_item (GstDataQueueItem * item)
{
  g_slice_free (GstDataQueueItem, item);
}

// TODO Maybe put Recorder C++ code in separate files ?!
void VideoTrackEventCb (uint32_t track_id, qmmf::recorder::EventType type,
                        void * data, size_t size) {
}

// TODO Maybe put Recorder C++ code in separate files ?!
void AudioTrackEventCb (uint32_t track_id, qmmf::recorder::EventType type,
                        void * data, size_t size) {
}

// TODO Maybe put Recorder C++ code in separate files ?!
void VideoTrackDataCb(GstPad *pad, uint32_t track_id,
                      std::vector<qmmf::BufferDescriptor> buffers,
                      std::vector<qmmf::recorder::MetaData> metabufs) {
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (gst_pad_get_parent(pad));
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

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
    GST_BUFFER_FLAG_SET (gstbuffer, gstflags);

    if (meta.meta_flag &
        static_cast<uint32_t>(qmmf::recorder::MetaParamType::kCamBufMetaData)) {
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
                      std::vector<qmmf::recorder::MetaData> metabufs)
{
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

void ImageDataCb(GstPad *pad, uint32_t camera_id, uint32_t imgcount,
                 qmmf::BufferDescriptor buffer, qmmf::recorder::MetaData meta) {
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(gst_pad_get_parent(pad));
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD(pad);

  GstBuffer *gstbuffer = nullptr;
  GstBufferFlags gstflags;

  GstDataQueueItem *item = nullptr;

  gstbuffer = qmmfsrc_gst_buffer_new_wrapped(pad, &buffer);
  g_return_if_fail(gstbuffer != nullptr);

  gstflags = (buffer.flag & (guint)qmmf::BufferFlags::kFlagCodecConfig) ?
      GST_BUFFER_FLAG_HEADER : GST_BUFFER_FLAG_LIVE;
  GST_BUFFER_FLAG_SET(gstbuffer, gstflags);

  GST_QMMFSRC_IMAGE_PAD_LOCK(pad);
  // Initialize the timestamp base value for calculating running time.
  ipad->tsbase = (ipad->tsbase == 0) ? buffer.timestamp : ipad->tsbase;

  gstbuffer->pts = buffer.timestamp - ipad->tsbase;
  gstbuffer->pts *= G_GUINT64_CONSTANT(1000);

  // TODO Does it need more precise calculations?!
  gstbuffer->duration = ipad->duration;
  GST_QMMFSRC_IMAGE_PAD_UNLOCK(pad);

  item = g_slice_new0(GstDataQueueItem);
  item->object = GST_MINI_OBJECT(gstbuffer);
  item->size = gst_buffer_get_size(gstbuffer);
  item->duration = GST_BUFFER_DURATION(gstbuffer);
  item->visible = TRUE;
  item->destroy = (GDestroyNotify)qmmfsrc_free_queue_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push(ipad->buffers, item)) {
    gst_buffer_unref(gstbuffer);
    item->destroy(item);
  }
}

static void
qmmfsrc_capture_image (GstQmmfSrc * qmmfsrc)
{
  GstQmmfSrcImagePad *ipad;
  GList *keys;
  gint status;

  GST_QMMFSRC_LOCK (qmmfsrc);

  // Currently there is support for only one image pad.
  keys = g_hash_table_get_keys (qmmfsrc->imgindexes);
  ipad = GST_QMMFSRC_IMAGE_PAD (g_hash_table_lookup (
      qmmfsrc->srcpads, g_list_nth_data (keys, 0)));

  GST_QMMFSRC_IMAGE_PAD_LOCK(ipad);

  qmmf::recorder::ImageParam imgparam;
  imgparam.width = ipad->width;
  imgparam.height = ipad->height;

  if (ipad->codec == GST_IMAGE_CODEC_TYPE_JPEG) {
    imgparam.image_format = qmmf::ImageFormat::kJPEG;
  } else {
    switch (ipad->format) {
      case GST_VIDEO_FORMAT_NV12:
        imgparam.image_format = qmmf::ImageFormat::kNV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        imgparam.image_format = qmmf::ImageFormat::kNV21;
        break;
      default:
        GST_ERROR_OBJECT (qmmfsrc, "Unsupported format %s",
            gst_video_format_to_string (ipad->format));
        GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);
        GST_QMMFSRC_UNLOCK (qmmfsrc);
        return;
    }
  }

  gst_structure_get_uint (ipad->params, "quality", &imgparam.image_quality);

  std::vector<android::CameraMetadata> metadata;
  android::CameraMetadata meta;

  status = qmmfsrc->recorder->GetDefaultCaptureParam(qmmfsrc->camera_id, meta);
  if (status != 0) {
    GST_ERROR_OBJECT (qmmfsrc, "GetDefaultCaptureParam Failed!");
    GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);
    GST_QMMFSRC_UNLOCK (qmmfsrc);
    return;
  }

  metadata.push_back(meta);

  qmmf::recorder::ImageCaptureCb cb =
      [&, ipad] (uint32_t camera_id, uint32_t imgcount,
          qmmf::BufferDescriptor buffer, qmmf::recorder::MetaData meta)
      { ImageDataCb(GST_PAD (ipad), camera_id, imgcount, buffer, meta); };

  status = qmmfsrc->recorder->CaptureImage(qmmfsrc->camera_id, imgparam,
                                           1, metadata, cb);
  if (status != 0) {
    GST_ERROR_OBJECT (qmmfsrc, "CaptureImage Failed!");
  }

  GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

static void
qmmfsrc_cancel_capture (GstQmmfSrc * qmmfsrc)
{
  gint status;

  GST_QMMFSRC_LOCK (qmmfsrc);

  status = qmmfsrc->recorder->CancelCaptureImage(qmmfsrc->camera_id);
  if (status != 0) {
    GST_ERROR_OBJECT (qmmfsrc, "CancelCaptureImage Failed!");
  }

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

static gboolean
qmmfsrc_open (GstElement * element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  gint status, camera_id = -1;

  GST_TRACE_OBJECT (qmmfsrc, "Open QMMF source");

  qmmf::recorder::RecorderCb cb;
  cb.event_cb = [] (qmmf::recorder::EventType type, void *data, size_t size)
      {  };

  status = qmmfsrc->recorder->Connect(cb);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE, "Connect Failed!");

  GHashTableIter iter;
  gpointer key, value;

  GST_QMMFSRC_LOCK(qmmfsrc);

  g_hash_table_iter_init(&iter, qmmfsrc->vidindexes);

  qmmfsrc->camera_id = camera_id;

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    GstPad *pad = GST_PAD(g_list_nth_data (element->srcpads, g_direct_hash(key)));
    GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
    GstCaps *caps;
    GstStructure *structure;
    gboolean success;
    const GValue *camera;

    GST_QMMFSRC_VIDEO_PAD_LOCK (vpad);

    caps = gst_pad_get_allowed_caps (pad);
    caps = gst_caps_make_writable (caps);

    structure = gst_caps_get_structure (caps, 0);
    if (gst_structure_has_field (structure, "camera")) {
      camera = gst_structure_get_value (structure, "camera");

      if (!gst_value_is_fixed (camera)) {
        gst_structure_fixate_field_nearest_int (structure, "camera",
            DEFAULT_PROP_CAMERA_ID);
      }
      gst_structure_get_int (structure, "camera", &camera_id);
    } else {
      gst_structure_set (structure, "camera", G_TYPE_INT,
          DEFAULT_PROP_CAMERA_ID, NULL);
      camera_id = DEFAULT_PROP_CAMERA_ID;
    }

    GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);

    if ((qmmfsrc->camera_id != -1) && (camera_id != qmmfsrc->camera_id)) {
      GST_ERROR_OBJECT (qmmfsrc, "Multiple cameras for the same plugin not "
          "supported!");
      return FALSE;
    }

    qmmfsrc->camera_id = camera_id;
    gst_caps_unref (caps);
  }

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  status = qmmfsrc->recorder->StartCamera(qmmfsrc->camera_id, 30);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "StartCamera Failed!");

  GST_TRACE_OBJECT (qmmfsrc, "QMMF Source opened");

  return TRUE;
}

static gboolean
qmmfsrc_close (GstElement * element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  gint status;

  GST_TRACE_OBJECT (qmmfsrc, "Closing QMMF source");

  status = qmmfsrc->recorder->StopCamera(qmmfsrc->camera_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE, "StopCamera Failed!");

  status = qmmfsrc->recorder->Disconnect();
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE, "Disconnect Failed!");

  GST_TRACE_OBJECT (qmmfsrc, "QMMF Source closed");

  return TRUE;
}

static gboolean
qmmfsrc_create_session (GstElement * element)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC(element);

  GHashTableIter iter;
  gpointer key, value;

  GstPad *pad;
  GstQmmfSrcVideoPad *vpad;
  GstQmmfSrcAudioPad *apad;
  GstQmmfSrcImagePad *ipad;
  gint status;

  qmmf::recorder::SessionCb session_cbs;
  qmmf::recorder::TrackCb track_cbs;
  guint session_id, track_id;

  GST_DEBUG_OBJECT(qmmfsrc, "Create session");

  GST_QMMFSRC_LOCK(qmmfsrc);

  session_cbs.event_cb =
      [] (qmmf::recorder::EventType type, void *data, size_t size) { };

  status = qmmfsrc->recorder->CreateSession(session_cbs, &session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "CreateSession Failed!");

  qmmfsrc->session_id = session_id;

  g_hash_table_iter_init(&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    qmmfsrc_video_pad_fixate_caps (pad);

    vpad = GST_QMMFSRC_VIDEO_PAD(pad);

    GST_QMMFSRC_VIDEO_PAD_LOCK(vpad);

    qmmf::VideoFormat format;
    switch (vpad->codec) {
      case GST_VIDEO_CODEC_TYPE_NONE:
        format = qmmf::VideoFormat::kYUV;
        break;
      case GST_VIDEO_CODEC_TYPE_H264:
        format = qmmf::VideoFormat::kAVC;
        break;
      default:
        GST_ERROR_OBJECT (qmmfsrc, "Unsupported video format!");
        return FALSE;
    }

    qmmf::recorder::VideoTrackCreateParam params(
      qmmfsrc->camera_id, format, vpad->width, vpad->height, vpad->framerate
    );

    track_cbs.event_cb =
        [&] (uint32_t track_id, qmmf::recorder::EventType type,
            void *data, size_t size)
        { VideoTrackEventCb(track_id, type, data, size); };
    track_cbs.data_cb =
        [&, pad] (uint32_t track_id,
            std::vector<qmmf::BufferDescriptor> buffers,
            std::vector<qmmf::recorder::MetaData> metabufs)
        { VideoTrackDataCb(pad, track_id, buffers, metabufs); };

    ::qmmf::recorder::VideoExtraParam extraparam;

    gboolean isvalid = g_hash_table_contains (qmmfsrc->vidindexes,
        GINT_TO_POINTER (vpad->srcidx));

    if ((vpad->srcidx != -1) && isvalid) {
      ::qmmf::recorder::SourceVideoTrack source_track;
      source_track.source_track_id = vpad->srcidx + VIDEO_TRACK_ID_OFFSET;
      extraparam.Update(
          ::qmmf::recorder::QMMF_SOURCE_VIDEO_TRACK_ID, source_track);
    } else if ((vpad->srcidx != -1) && !isvalid) {
      GST_ERROR_OBJECT (qmmfsrc, "Invalid source index!");
      return FALSE;
    }

    track_id = vpad->index + VIDEO_TRACK_ID_OFFSET;
    status = qmmfsrc->recorder->CreateVideoTrack(session_id, track_id, params,
                                                 extraparam,track_cbs);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
        "CreateVideoTrack Failed!");

    vpad->id = track_id;

    GST_QMMFSRC_VIDEO_PAD_UNLOCK(vpad);
  }

  g_hash_table_iter_init(&iter, qmmfsrc->audindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    qmmfsrc_audio_pad_fixate_caps (pad);

    apad = GST_QMMFSRC_AUDIO_PAD(pad);

    GST_QMMFSRC_AUDIO_PAD_LOCK(apad);

    qmmf::recorder::AudioTrackCreateParam params;
    params.channels = apad->channels;
    params.sample_rate = apad->samplerate;
    params.bit_depth = apad->bitdepth;
    params.in_devices_num = 1;
    params.in_devices[0] = apad->device;
    params.out_device = 0;
    params.flags = 0;

    switch (apad->codec) {
      case GST_AUDIO_CODEC_TYPE_AAC:
      {
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
        break;
      }
      case GST_AUDIO_CODEC_TYPE_AMR:
        params.format = qmmf::AudioFormat::kAMR;
        params.codec_params.amr.bit_rate = 12200;
        params.codec_params.amr.isWAMR = FALSE;
        break;
      case GST_AUDIO_CODEC_TYPE_AMRWB:
        params.format = qmmf::AudioFormat::kAMR;
        params.codec_params.amr.bit_rate = 12200;
        params.codec_params.amr.isWAMR = TRUE;
        break;
      case GST_AUDIO_CODEC_TYPE_NONE:
        params.format = qmmf::AudioFormat::kPCM;
        break;
      default:
        GST_ERROR_OBJECT (qmmfsrc, "Unsupported audio format %d!", apad->codec);
        return FALSE;
    }

    track_cbs.event_cb =
        [&] (uint32_t track_id, qmmf::recorder::EventType type,
            void *data, size_t size)
        { AudioTrackEventCb(track_id, type, data, size); };
    track_cbs.data_cb =
        [&, pad] (uint32_t track_id,
            std::vector<qmmf::BufferDescriptor> buffers,
            std::vector<qmmf::recorder::MetaData> metabufs)
        { AudioTrackDataCb(pad, track_id, buffers, metabufs); };

    track_id = apad->index + AUDIO_TRACK_ID_OFFSET;
    status = qmmfsrc->recorder->CreateAudioTrack(session_id, track_id,
                                                 params, track_cbs);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
        "CreateAudioTrack Failed!");


    apad->id = track_id;

    GST_QMMFSRC_AUDIO_PAD_UNLOCK(apad);
  }

  g_hash_table_iter_init(&iter, qmmfsrc->imgindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    qmmfsrc_image_pad_fixate_caps (pad);

    ipad = GST_QMMFSRC_IMAGE_PAD(pad);

    GST_QMMFSRC_IMAGE_PAD_LOCK(ipad);

    qmmf::recorder::ImageConfigParam config;

    if (ipad->codec == GST_IMAGE_CODEC_TYPE_JPEG) {
      qmmf::recorder::ImageThumbnail thumbnail;
      qmmf::recorder::ImageExif exif;
      guint width, height, quality;

      gst_structure_get_uint (ipad->params, "thumbnail-width", &width);
      gst_structure_get_uint (ipad->params, "thumbnail-height", &height);
      gst_structure_get_uint (ipad->params, "thumbnail-quality", &quality);

      if (width > 0 && height > 0) {
        thumbnail.width = width;
        thumbnail.height = height;
        thumbnail.quality = quality;
        config.Update(qmmf::recorder::QMMF_IMAGE_THUMBNAIL, thumbnail, 0);
      }

      gst_structure_get_uint (ipad->params, "screennail-width", &width);
      gst_structure_get_uint (ipad->params, "screennail-height", &height);
      gst_structure_get_uint (ipad->params, "screennail-quality", &quality);

      if (width > 0 && height > 0) {
        thumbnail.width = width;
        thumbnail.height = height;
        thumbnail.quality = quality;
        config.Update(qmmf::recorder::QMMF_IMAGE_THUMBNAIL, thumbnail, 1);
      }

      config.Update(qmmf::recorder::QMMF_EXIF, exif, 0);
    }

    status = qmmfsrc->recorder->ConfigImageCapture(qmmfsrc->camera_id, config);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
        "ConfigImageCapture Failed!");

    GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);
  }

  android::CameraMetadata meta;
  gint mvalue;

  mvalue = gst_qmmfsrc_effect_mode_android_value (qmmfsrc->effect);
  meta.update(ANDROID_CONTROL_EFFECT_MODE, &mvalue, 1);

  mvalue = gst_qmmfsrc_scene_mode_android_value (qmmfsrc->scene);
  meta.update(ANDROID_CONTROL_SCENE_MODE, &mvalue, 1);

  mvalue = gst_qmmfsrc_antibanding_android_value (qmmfsrc->antibanding);
  meta.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mvalue, 1);

  mvalue = qmmfsrc->aecomp;
  meta.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &mvalue, 1);

  mvalue = qmmfsrc->aelock;
  meta.update(ANDROID_CONTROL_AE_LOCK, &mvalue, 1);

  mvalue = gst_qmmfsrc_awb_mode_android_value (qmmfsrc->awbmode);
  meta.update(ANDROID_CONTROL_AWB_MODE, &mvalue, 1);

  mvalue = qmmfsrc->awblock;
  meta.update(ANDROID_CONTROL_AWB_LOCK, &mvalue, 1);

  status = qmmfsrc->recorder->SetCameraParam(qmmfsrc->camera_id, meta);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "SetCameraParam Failed!");

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
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    apad = GST_QMMFSRC_AUDIO_PAD(pad);

    status = qmmfsrc->recorder->DeleteAudioTrack(qmmfsrc->session_id, apad->id);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
        "DeleteAudioTrack Failed!");
  }

  g_hash_table_iter_init(&iter, qmmfsrc->vidindexes);

  while (g_hash_table_iter_next(&iter, &key, &value)) {
    pad = GST_PAD (g_hash_table_lookup (qmmfsrc->srcpads, key));
    vpad = GST_QMMFSRC_VIDEO_PAD(pad);

    status = qmmfsrc->recorder->DeleteVideoTrack(qmmfsrc->session_id, vpad->id);
    QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
        "DeleteVideoTrack Failed!");
  }

  status = qmmfsrc->recorder->DeleteSession(qmmfsrc->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "DeleteSession Failed!");

  qmmfsrc->session_id = 0;

  status = qmmfsrc->recorder->CancelCaptureImage(qmmfsrc->camera_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "CancelCaptureImage Failed!");

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
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  status = qmmfsrc->recorder->StartSession(qmmfsrc->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "StartSession Failed!");

  GST_QMMFSRC_UNLOCK(qmmfsrc);

  GST_DEBUG_OBJECT(qmmfsrc, "Session started");

  return TRUE;
}

static void
qmmfsrc_set_camera_property (GstQmmfSrc * qmmfsrc, const guint tag,
                             const guchar value)
{
  android::CameraMetadata meta;
  meta.update(tag, &value, 1);

  qmmfsrc->recorder->SetCameraParam(qmmfsrc->camera_id, meta);
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
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, success, FALSE,
      "Failed to flush source pads!");

  status = qmmfsrc->recorder->StopSession(qmmfsrc->session_id, false);
  QMMFSRC_RETURN_VAL_IF_FAIL (qmmfsrc, status == 0, FALSE,
      "StopSession Failed!");

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
qmmfsrc_send_event (GstElement * element, GstEvent * event)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (element);
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (qmmfsrc, "Event: %s", GST_EVENT_TYPE_NAME(event));

  switch (GST_EVENT_TYPE(event)) {
    // Bidirectional events.
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing FLUSH_START event");
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_send_event, event
      );
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing FLUSH_STOP event");
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_send_event, event
      );
      gst_event_unref(event);
      break;

    // Downstream serialized events.
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (qmmfsrc, "Pushing EOS event downstream");
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_push_event, event
      );
      success = gst_element_foreach_src_pad (
          element, (GstElementForeachPadFunc) qmmfsrc_pad_flush_buffers,
          GUINT_TO_POINTER (TRUE)
      );
      gst_event_unref (event);
      break;
    default:
      success =
          GST_ELEMENT_CLASS (qmmfsrc_parent_class)->send_event (element, event);
      break;
  }

  return success;
}

// GstElement virtual method implementation. Sets the element's properties.
static void
qmmfsrc_set_property (GObject * object, guint property_id,
                      const GValue * value, GParamSpec * pspec)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (object);

  GST_QMMFSRC_LOCK (qmmfsrc);

  switch (property_id) {
    case PROP_CAMERA_EFFECT_MODE:
      qmmfsrc->effect = g_value_get_enum (value);
      qmmfsrc_set_camera_property (qmmfsrc, ANDROID_CONTROL_EFFECT_MODE,
          gst_qmmfsrc_effect_mode_android_value (qmmfsrc->effect));
      break;
    case PROP_CAMERA_SCENE_MODE:
      qmmfsrc->scene = g_value_get_enum (value);
      qmmfsrc_set_camera_property (qmmfsrc, ANDROID_CONTROL_SCENE_MODE,
          gst_qmmfsrc_scene_mode_android_value (qmmfsrc->scene));
      break;
    case PROP_CAMERA_ANTIBANDING_MODE:
      qmmfsrc->antibanding = g_value_get_enum (value);
      qmmfsrc_set_camera_property (qmmfsrc, ANDROID_CONTROL_AE_ANTIBANDING_MODE,
          gst_qmmfsrc_antibanding_android_value (qmmfsrc->antibanding));
      break;
    case PROP_CAMERA_AE_COMPENSATION:
      qmmfsrc->aecomp = g_value_get_int (value);
      qmmfsrc_set_camera_property (
          qmmfsrc, ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
          gst_qmmfsrc_antibanding_android_value (qmmfsrc->aecomp));
      break;
    case PROP_CAMERA_AE_LOCK:
      qmmfsrc->aelock = g_value_get_boolean (value);
      qmmfsrc_set_camera_property (qmmfsrc, ANDROID_CONTROL_AE_LOCK,
          qmmfsrc->aelock);
      break;
    case PROP_CAMERA_AWB_MODE:
      qmmfsrc->awbmode = g_value_get_enum (value);
      qmmfsrc_set_camera_property (qmmfsrc, ANDROID_CONTROL_AWB_MODE,
          gst_qmmfsrc_awb_mode_android_value (qmmfsrc->awbmode));
      break;
    case PROP_CAMERA_AWB_LOCK:
      qmmfsrc->awblock = g_value_get_boolean (value);
      qmmfsrc_set_camera_property (qmmfsrc, ANDROID_CONTROL_AWB_LOCK,
          qmmfsrc->awblock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

// GstElement virtual method implementation. Sets the element's properties.
static void
qmmfsrc_get_property (GObject * object, guint property_id,
                      GValue * value, GParamSpec * pspec)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (object);

  GST_QMMFSRC_LOCK (qmmfsrc);

  switch (property_id) {
    case PROP_CAMERA_EFFECT_MODE:
      g_value_set_enum (value, qmmfsrc->effect);
      break;
    case PROP_CAMERA_SCENE_MODE:
      g_value_set_enum (value, qmmfsrc->scene);
      break;
    case PROP_CAMERA_ANTIBANDING_MODE:
      g_value_set_enum (value, qmmfsrc->antibanding);
      break;
    case PROP_CAMERA_AE_COMPENSATION:
      g_value_set_int (value, qmmfsrc->aecomp);
      break;
    case PROP_CAMERA_AE_LOCK:
      g_value_set_boolean (value, qmmfsrc->aelock);
      break;
    case PROP_CAMERA_AWB_MODE:
      g_value_set_enum (value, qmmfsrc->awbmode);
      break;
    case PROP_CAMERA_AWB_LOCK:
      g_value_set_boolean (value, qmmfsrc->awbmode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_QMMFSRC_UNLOCK (qmmfsrc);
}

// GstElement virtual method implementation. Called when plugin is destroyed.
static void
qmmfsrc_finalize (GObject * object)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (object);

  if (qmmfsrc->srcpads != nullptr) {
    g_hash_table_remove_all (qmmfsrc->srcpads);
    g_hash_table_destroy (qmmfsrc->srcpads);
    qmmfsrc->srcpads = nullptr;
  }

  if (qmmfsrc->vidindexes != nullptr) {
    g_hash_table_remove_all (qmmfsrc->vidindexes);
    g_hash_table_destroy (qmmfsrc->vidindexes);
    qmmfsrc->vidindexes = nullptr;
  }

  if (qmmfsrc->audindexes != nullptr) {
    g_hash_table_remove_all (qmmfsrc->audindexes);
    g_hash_table_destroy (qmmfsrc->audindexes);
    qmmfsrc->audindexes = nullptr;
  }

  if (qmmfsrc->imgindexes != nullptr) {
    g_hash_table_remove_all (qmmfsrc->imgindexes);
    g_hash_table_destroy (qmmfsrc->imgindexes);
    qmmfsrc->imgindexes = nullptr;
  }

  delete qmmfsrc->recorder;
  qmmfsrc->recorder = nullptr;

  G_OBJECT_CLASS (qmmfsrc_parent_class)->finalize (object);
}

// GObject element class initialization function.
static void
qmmfsrc_class_init (GstQmmfSrcClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (qmmfsrc_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (qmmfsrc_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (qmmfsrc_finalize);

  gst_element_class_add_static_pad_template (
      gstelement, &qmmfsrc_video_src_template
  );
  gst_element_class_add_static_pad_template (
      gstelement, &qmmfsrc_audio_src_template
  );
  gst_element_class_add_static_pad_template (
      gstelement, &qmmfsrc_image_src_template
  );
  gst_element_class_set_static_metadata (
      gstelement, "QMMF Video/Audio Source", "Source/Video",
      "Reads frames from a device via QMMF service", "QTI"
  );

  g_object_class_install_property (gobject, PROP_CAMERA_EFFECT_MODE,
      g_param_spec_enum ("effect", "Effect",
           "Effect applied on the camera frames",
           GST_TYPE_QMMFSRC_EFFECT_MODE, DEFAULT_PROP_CAMERA_EFFECT_MODE,
           (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
               G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CAMERA_SCENE_MODE,
      g_param_spec_enum ("scene", "Scene",
           "Camera optimizations depending on the scene",
           GST_TYPE_QMMFSRC_SCENE_MODE, DEFAULT_PROP_CAMERA_SCENE_MODE,
           (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
               G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CAMERA_ANTIBANDING_MODE,
      g_param_spec_enum ("antibanding", "Antibanding",
           "Camera antibanding routine for the current illumination condition",
           GST_TYPE_QMMFSRC_ANTIBANDING, DEFAULT_PROP_CAMERA_ANTIBANDING,
           (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
               G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CAMERA_AE_COMPENSATION,
      g_param_spec_int ("ae-compensation", "AE Compensation",
          "Auto Exposure Compensation",
          -12, 12, DEFAULT_PROP_CAMERA_AE_COMPENSATION,
          (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CAMERA_AE_LOCK,
      g_param_spec_boolean ("ae-lock", "AE Lock",
          "Auto Exposure lock", DEFAULT_PROP_CAMERA_AE_LOCK,
          (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CAMERA_AWB_MODE,
      g_param_spec_enum ("awb-mode", "AWB Mode",
           "Auto White Balance mode",
           GST_TYPE_QMMFSRC_AWB_MODE, DEFAULT_PROP_CAMERA_AWB_MODE,
           (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
               G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property (gobject, PROP_CAMERA_AWB_LOCK,
      g_param_spec_boolean ("awb-lock", "AWB Lock",
          "Auto White Balance lock", DEFAULT_PROP_CAMERA_AWB_LOCK,
          (GParamFlags)(G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  qmmfsrc_signals[CAPTURE_IMAGE_SIGNAL] =
      g_signal_new_class_handler ("capture-image",
      G_TYPE_FROM_CLASS (klass),
      (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (qmmfsrc_capture_image),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  qmmfsrc_signals[CANCEL_CAPTURE_SIGNAL] =
      g_signal_new_class_handler ("cancel-capture",
      G_TYPE_FROM_CLASS (klass),
      (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (qmmfsrc_cancel_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gstelement->request_new_pad = GST_DEBUG_FUNCPTR (qmmfsrc_request_pad);
  gstelement->release_pad = GST_DEBUG_FUNCPTR (qmmfsrc_release_pad);

  gstelement->send_event = GST_DEBUG_FUNCPTR (qmmfsrc_send_event);
  gstelement->change_state = GST_DEBUG_FUNCPTR (qmmfsrc_change_state);

  // Initializes a new qmmfsrc GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (qmmfsrc_debug, "qmmfsrc", 0, "QTI QMMF Source");
}

// GObject element initialization function.
static void
qmmfsrc_init (GstQmmfSrc * qmmfsrc)
{
  GST_DEBUG_OBJECT (qmmfsrc, "Initializing");

  qmmfsrc->srcpads = g_hash_table_new (nullptr, nullptr);
  qmmfsrc->nextidx = 0;

  qmmfsrc->vidindexes = g_hash_table_new (nullptr, nullptr);
  qmmfsrc->audindexes = g_hash_table_new (nullptr, nullptr);
  qmmfsrc->imgindexes = g_hash_table_new (nullptr, nullptr);

  qmmfsrc->recorder = new qmmf::recorder::Recorder();
  g_return_if_fail (qmmfsrc->recorder != nullptr);

  GST_OBJECT_FLAG_SET (qmmfsrc, GST_ELEMENT_FLAG_SOURCE);
}

static GObject *
gst_qmmsrc_child_proxy_get_child_by_index (GstChildProxy * proxy, guint index)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (proxy);
  GObject *gobject = NULL;

  GST_QMMFSRC_LOCK (qmmfsrc);

  gobject = G_OBJECT (g_hash_table_lookup (
      qmmfsrc->srcpads, GUINT_TO_POINTER (index)));

  if (gobject != NULL)
    g_object_ref (gobject);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  return gobject;
}

static guint
gst_qmmsrc_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstQmmfSrc *qmmfsrc = GST_QMMFSRC (proxy);
  guint count = 0;

  GST_QMMFSRC_LOCK (qmmfsrc);

  count = g_hash_table_size (qmmfsrc->srcpads);
  GST_INFO_OBJECT (qmmfsrc, "Children Count: %d", count);

  GST_QMMFSRC_UNLOCK (qmmfsrc);

  return count;
}

static void
gst_qmmfsrc_child_proxy_init (gpointer g_iface, gpointer data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_qmmsrc_child_proxy_get_child_by_index;
  iface->get_children_count = gst_qmmsrc_child_proxy_get_children_count;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qmmfsrc", GST_RANK_PRIMARY,
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
