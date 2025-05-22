/*
* Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jpegenc.h"

#include <string.h>

#include <gst/base/base.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT jpeg_enc_debug
GST_DEBUG_CATEGORY_STATIC (jpeg_enc_debug);

#define gst_jpeg_enc_parent_class parent_class
G_DEFINE_TYPE (GstJPEGEncoder, gst_jpeg_enc, GST_TYPE_VIDEO_ENCODER);

#define GST_TYPE_JPEG_ENC_ORIENTATION (gst_jpeg_enc_orientation_get_type())

#define DEFAULT_PROP_JPEG_QUALITY   85
#define DEFAULT_PROP_ORIENTATION    GST_JPEG_ENC_ORIENTATION_0

#define DEFAULT_PROP_MIN_BUFFERS    2
#define DEFAULT_PROP_MAX_BUFFERS    10

// Caps formats.
#define GST_VIDEO_FORMATS "{ NV12, NV21 }"

static GstStaticPadTemplate gst_jpeg_enc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS))
);

static GstStaticPadTemplate gst_jpeg_enc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 1, 65535 ], "
        "height = (int) [ 1, 65535 ], "
        "framerate = (fraction) [ 0/1, MAX ]")
    );

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_ORIENTATION,
};

struct _GstVideoFrameData {
  GstJPEGEncoder *jpegenc;
  GstVideoCodecFrame *frame;
};

static GType
gst_jpeg_enc_orientation_get_type (void)
{
  static GType type = 0;
  static const GEnumValue methods[] = {
    { GST_JPEG_ENC_ORIENTATION_0,
        "Orientation 0 degrees", "0"
    },
    { GST_JPEG_ENC_ORIENTATION_90,
        "Orientation 90 degrees", "90"
    },
    { GST_JPEG_ENC_ORIENTATION_180,
        "Orientation 180 degrees", "180"
    },
    { GST_JPEG_ENC_ORIENTATION_270,
        "Orientation 270 degrees", "270"
    },
    {0, NULL, NULL},
  };
  if (!type) {
    type =
        g_enum_register_static ("GstJpegEncodeRotation", methods);
  }
  return type;
}

static GstBufferPool *
gst_jpeg_enc_create_pool (GstJPEGEncoder * jpegenc, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (jpegenc, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (jpegenc, "Failed to create image pool!");
    return NULL;
  }

  // Align size to 64 lines
  gint alignedw = (GST_VIDEO_INFO_WIDTH (&info) + 64-1) & ~(64-1);
  gint alignedh = (GST_VIDEO_INFO_HEIGHT (&info) + 64-1) & ~(64-1);
  gsize aligned_size = alignedw * alignedh * 4;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, aligned_size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  if (allocator == NULL) {
    GST_ERROR_OBJECT (jpegenc, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  GST_INFO_OBJECT (jpegenc, "Buffer pool uses DMA memory");
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (jpegenc, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  g_object_unref (allocator);

  return pool;
}

static void
gst_jpeg_enc_callback (GstVideoCodecFrame * frame, gpointer userdata)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (userdata);

  if (frame) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (jpegenc), frame);
  } else {
    GST_ERROR_OBJECT (jpegenc, "The received frame is NULL");
  }
}

static void
gst_jpeg_enc_process_task_loop (gpointer userdata)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (jpegenc->inframes, &item)) {
    GstVideoFrameData *framedata = (GstVideoFrameData *) item->object;
    GstVideoCodecFrame *frame = framedata->frame;

    // Get new buffer from the pool
    if (GST_FLOW_OK == gst_buffer_pool_acquire_buffer (jpegenc->outpool,
        &frame->output_buffer, NULL)) {

      // Copy the flags and timestamps from the input buffer.
      gst_buffer_copy_into (frame->output_buffer, frame->input_buffer,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

      GST_DEBUG_OBJECT (jpegenc, "Start compressing");
      // Process the JPEG
      if (!gst_jpeg_enc_context_execute (jpegenc->context, frame, jpegenc->quality)) {
        GST_ERROR_OBJECT (jpegenc, "Failed to execute Jpeg encoder!");
        gst_buffer_unref (frame->output_buffer);
        frame->output_buffer = NULL;
        gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (jpegenc), frame);
      }
    } else {
      GST_ERROR_OBJECT (jpegenc, "Failed to acquire output buffer!");
      gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (jpegenc), frame);
    }

    g_slice_free (GstVideoFrameData, framedata);
    g_slice_free (GstDataQueueItem, item);
  } else {
    GST_DEBUG_OBJECT (jpegenc, "The queue is in flushing state");
  }
}

static gboolean
gst_jpeg_enc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (encoder);
  GstVideoInfo *info = &state->info;
  GstStructure *params = NULL;
  GstVideoCodecState *output_state = NULL;
  GstCaps *outcaps = NULL;

  // Set output caps
  outcaps = gst_caps_new_simple ("image/jpeg",
      "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH (info),
      "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT (info),
      NULL);

  // Unref previouly created pool
  if (jpegenc->outpool) {
    gst_buffer_pool_set_active (jpegenc->outpool, FALSE);
    gst_object_unref (jpegenc->outpool);
  }

  // Creat a new output memory pool
  jpegenc->outpool = gst_jpeg_enc_create_pool (jpegenc, outcaps);
  if (!jpegenc->outpool) {
    GST_ERROR_OBJECT (jpegenc, "Failed to create output pool!");
  }

  // Activate the pool
  if (!gst_buffer_pool_is_active (jpegenc->outpool) &&
      !gst_buffer_pool_set_active (jpegenc->outpool, TRUE)) {
    GST_ERROR_OBJECT (jpegenc, "Failed to activate output buffer pool!");
    return FALSE;
  }

  // Configuration of the JPEG encoder
  params = gst_structure_new ("qtijpegenc",
      GST_JPEG_ENC_INPUT_WIDTH, G_TYPE_UINT, GST_VIDEO_INFO_WIDTH (info),
      GST_JPEG_ENC_INPUT_HEIGHT, G_TYPE_UINT, GST_VIDEO_INFO_HEIGHT (info),
      GST_JPEG_ENC_INPUT_FORMAT, G_TYPE_UINT, HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
      GST_JPEG_ENC_OUTPUT_WIDTH, G_TYPE_UINT, GST_VIDEO_INFO_WIDTH (info),
      GST_JPEG_ENC_OUTPUT_HEIGHT, G_TYPE_UINT, GST_VIDEO_INFO_HEIGHT (info),
      GST_JPEG_ENC_OUTPUT_FORMAT, G_TYPE_UINT, HAL_PIXEL_FORMAT_BLOB,
      GST_JPEG_ENC_QUALITY, G_TYPE_UINT, jpegenc->quality,
      GST_JPEG_ENC_ORIENTATION, GST_TYPE_JPEG_ENC_ORIENTATION,
          jpegenc->orientation,
      NULL);

  if (!gst_jpeg_enc_context_create (jpegenc->context, params)) {
    GST_ERROR_OBJECT (jpegenc, "Failed to create the encoder!");
    gst_buffer_pool_set_active (jpegenc->outpool, FALSE);
    gst_object_unref (jpegenc->outpool);
    return FALSE;
  }

  GST_DEBUG_OBJECT (jpegenc, "Encoder configured: width - %d, height - %d",
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

  output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (jpegenc),
      outcaps, state);
  gst_video_codec_state_unref (output_state);

  return TRUE;
}

static void
gst_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  GstVideoFrameData *framedata = (GstVideoFrameData *) item->object;
  gst_video_encoder_finish_frame (
      GST_VIDEO_ENCODER (framedata->jpegenc), framedata->frame);
  g_slice_free (GstVideoFrameData, framedata);
  g_slice_free (GstDataQueueItem, item);
}

static GstFlowReturn
gst_jpeg_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (encoder);

  GstVideoFrameData *framedata = g_slice_new0 (GstVideoFrameData);
  framedata->jpegenc = jpegenc;
  framedata->frame = frame;

  // Put the new frame in a queue for processing
  GstDataQueueItem *item = NULL;
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (framedata);
  item->visible = TRUE;
  item->destroy = gst_free_queue_item;
  if (!gst_data_queue_push (jpegenc->inframes, item)) {
    GST_ERROR_OBJECT (jpegenc, "ERROR: Cannot push data to the queue!\n");
    item->destroy (item);
    return GST_FLOW_OK;
  }
  GST_DEBUG_OBJECT (jpegenc, "Handle a new frame, put in the queue");

  return GST_FLOW_OK;
}

static gboolean
gst_jpeg_enc_start (GstVideoEncoder * encoder)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (encoder);
  GST_DEBUG_OBJECT (jpegenc, "Encoder start");

  if (jpegenc->worktask != NULL)
    return TRUE;

  // Create process task
  jpegenc->worktask =
      gst_task_new (gst_jpeg_enc_process_task_loop, jpegenc, NULL);
  GST_INFO_OBJECT (jpegenc, "Created task %p", jpegenc->worktask);

  gst_task_set_lock (jpegenc->worktask, &jpegenc->worklock);

  if (!gst_task_start (jpegenc->worktask)) {
    GST_ERROR_OBJECT (jpegenc, "Failed to start worker task!");
    return FALSE;
  }

  // Disable requests queue in flushing state to enable normal work.
  gst_data_queue_set_flushing (jpegenc->inframes, FALSE);

  return TRUE;
}

static gboolean
gst_jpeg_enc_stop (GstVideoEncoder * encoder)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (encoder);
  GST_DEBUG_OBJECT (jpegenc, "Encoder stop");

  if (NULL == jpegenc->worktask)
    return TRUE;

  // Set the inframes queue in flushing state.
  gst_data_queue_set_flushing (jpegenc->inframes, TRUE);

  if (!gst_task_join (jpegenc->worktask)) {
    GST_ERROR_OBJECT (jpegenc, "Failed to join worker task!");
    return FALSE;
  }

  gst_data_queue_flush (jpegenc->inframes);

  GST_INFO_OBJECT (jpegenc, "Removing task %p", jpegenc->worktask);

  gst_object_unref (jpegenc->worktask);
  jpegenc->worktask = NULL;

  if (gst_buffer_pool_is_active (jpegenc->outpool) &&
      !gst_buffer_pool_set_active (jpegenc->outpool, FALSE)) {
    GST_ERROR_OBJECT (jpegenc, "Failed to deactivate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  if (!gst_jpeg_enc_context_destroy (jpegenc->context)) {
    GST_ERROR_OBJECT (jpegenc, "Failed to destroy the encoder!");
    return FALSE;
  }

  return TRUE;
}

static void
gst_jpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (jpegenc);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (jpegenc, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (jpegenc);

  switch (prop_id) {
    case PROP_QUALITY:
      jpegenc->quality = g_value_get_int (value);
      break;
    case PROP_ORIENTATION:
      jpegenc->orientation = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (jpegenc);
}

static void
gst_jpeg_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (object);

  GST_OBJECT_LOCK (jpegenc);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_int (value, jpegenc->quality);
      break;
    case PROP_ORIENTATION:
      g_value_set_enum (value, jpegenc->orientation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (jpegenc);
}

static void
gst_jpeg_enc_finalize (GObject * object)
{
  GstJPEGEncoder *jpegenc = GST_JPEG_ENC (object);

  if (jpegenc->outpool != NULL)
    gst_object_unref (jpegenc->outpool);

  if (jpegenc->context != NULL) {
    gst_jpeg_enc_context_free (jpegenc->context);
    jpegenc->context = NULL;
  }

  if (jpegenc->inframes != NULL) {
    gst_data_queue_set_flushing (jpegenc->inframes, TRUE);
    gst_data_queue_flush (jpegenc->inframes);
    gst_object_unref (GST_OBJECT_CAST(jpegenc->inframes));
  }

  g_rec_mutex_clear (&jpegenc->worklock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (jpegenc));
}

static void
gst_jpeg_enc_class_init (GstJPEGEncoderClass * klass)
{
  GObjectClass *gobject        = G_OBJECT_CLASS (klass);
  GstElementClass *element     = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_jpeg_enc_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_jpeg_enc_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_jpeg_enc_finalize);

  g_object_class_install_property (gobject, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality of encoding",
          0, 100, DEFAULT_PROP_JPEG_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_ORIENTATION,
      g_param_spec_enum ("orientation", "Orientation",
          "Orientation of Jpeg encoder",
          GST_TYPE_JPEG_ENC_ORIENTATION, DEFAULT_PROP_ORIENTATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Jpeg encoder", "JPEG/Encoder",
      "Jpeg encoding", "QTI");

  gst_element_class_add_static_pad_template (element,
      &gst_jpeg_enc_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_jpeg_enc_src_pad_template);

  venc_class->start = gst_jpeg_enc_start;
  venc_class->stop = gst_jpeg_enc_stop;
  venc_class->set_format = gst_jpeg_enc_set_format;
  venc_class->handle_frame = gst_jpeg_enc_handle_frame;
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_jpeg_enc_init (GstJPEGEncoder * jpegenc)
{
  g_rec_mutex_init (&jpegenc->worklock);

  jpegenc->quality = DEFAULT_PROP_JPEG_QUALITY;
  jpegenc->outpool = NULL;
  jpegenc->worktask = NULL;

  jpegenc->inframes =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  gst_data_queue_set_flushing (jpegenc->inframes, FALSE);

  GST_LOG_OBJECT (jpegenc, "Create Jpeg encoder context");
  jpegenc->context = gst_jpeg_enc_context_new (
      (GstJPEGEncoderCallback) G_CALLBACK (gst_jpeg_enc_callback), jpegenc);
  g_return_if_fail (jpegenc->context != NULL);

  GST_DEBUG_CATEGORY_INIT (jpeg_enc_debug, "qtijpegenc", 0,
      "QTI jpeg encoder");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtijpegenc", GST_RANK_PRIMARY,
      GST_TYPE_JPEG_ENC);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtijpegenc,
    "Jpeg encoding",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
