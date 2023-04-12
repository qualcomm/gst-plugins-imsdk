/*
* Copyright (c) 2021, 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "jpegenc-context.h"

#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_recorder_params.h>
#include <qmmf-sdk/qmmf_offline_jpeg_params.h>

#define GST_CAT_DEFAULT jpeg_enc_context_debug_category()
static GstDebugCategory *
jpeg_enc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtijpegenc", 0,
        "JPEG Encoder context");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

struct _GstJPEGEncoderContext {
  /// QMMF Recorder instance.
  ::qmmf::recorder::Recorder *recorder;
  /// Callback from Jpeg encoder
  GstJPEGEncoderCallback callback;
  /// User data for the callback from Jpeg encoder
  gpointer userdata;
  /// Contains all request sent to offline jpeg encoder
  GHashTable *requests;
  // Mutex
  GMutex lock;
  // A signal used to wait for all requests received from the JPEG encoder
  GCond requests_received;
};

static void
camera_event_callback (GstJPEGEncoderContext * context,
    ::qmmf::recorder::EventType type, void * payload, size_t size)
{
  gint event = EVENT_UNKNOWN;

  switch (type) {
    case ::qmmf::recorder::EventType::kServerDied:
      event = EVENT_SERVICE_DIED;
      break;
    default:
      event = EVENT_UNKNOWN;
      break;
  }
}

GstJPEGEncoderContext *
gst_jpeg_enc_context_new (GstJPEGEncoderCallback callback, gpointer userdata)
{
  GstJPEGEncoderContext *context = NULL;
  ::qmmf::recorder::RecorderCb cbs;

  context = g_slice_new0 (GstJPEGEncoderContext);
  g_return_val_if_fail (context != NULL, NULL);

  context->recorder = new ::qmmf::recorder::Recorder();
  if (!context->recorder) {
    g_slice_free (GstJPEGEncoderContext, context);
    GST_ERROR ("QMMF Recorder creation failed!");
    return NULL;
  }

  // Register a events function which will call the EOS callback if necessary.
  cbs.event_cb =
      [&, context] (::qmmf::recorder::EventType type, void *data, size_t size)
      { camera_event_callback (context, type, data, size); };

  if (context->recorder->Connect (cbs)) {
    delete context->recorder;
    g_slice_free (GstJPEGEncoderContext, context);
    GST_ERROR ("QMMF Recorder Connect failed!");
    return NULL;
  }

  context->callback = callback;
  context->userdata = userdata;
  context->requests = g_hash_table_new (NULL, NULL);
  g_mutex_init (&context->lock);
  g_cond_init (&context->requests_received);

  GST_INFO ("Created Jpeg encoder context: %p", context);
  return context;
}

void
gst_jpeg_enc_context_free (GstJPEGEncoderContext * context)
{
  if (context->requests != NULL) {
    g_hash_table_remove_all (context->requests);
    g_hash_table_destroy (context->requests);
    context->requests = NULL;
  }

  g_cond_clear (&context->requests_received);
  g_mutex_clear (&context->lock);

  context->recorder->Disconnect ();
  delete context->recorder;

  GST_INFO ("Destroyed Jpeg encoder context: %p", context);
  g_slice_free (GstJPEGEncoderContext, context);
}

static void
gst_jpeg_enc_callback (GstJPEGEncoderContext * context, guint buf_fd,
    guint encoded_size)
{
  if (buf_fd == -1) {
    GST_ERROR ("Failed: Invalid request id");
    return;
  }

  g_mutex_lock (&context->lock);
  GstVideoCodecFrame *frame = (GstVideoCodecFrame *) g_hash_table_lookup (
      context->requests, GINT_TO_POINTER (buf_fd));
  g_hash_table_remove (context->requests, GINT_TO_POINTER (buf_fd));
  g_mutex_unlock (&context->lock);

  if (frame) {
    // Resize the buffer to the encoded size
    GstMemory *memory = gst_buffer_peek_memory (frame->output_buffer, 0);
    gsize maxsize = 0;
    gst_memory_get_sizes (memory, 0, &maxsize);
    if (encoded_size < maxsize)
      gst_memory_resize (memory, 0, encoded_size);

    GST_DEBUG ("End compressing, encoded_size: %d", encoded_size);
  } else {
    GST_ERROR ("Failed to find a request with fd %d", buf_fd);
  }

  // Call the callback
  if (context->callback)
    context->callback (frame, context->userdata);

  g_mutex_lock (&context->lock);
  // Check if all requests are received and send a signal
  if (g_hash_table_size (context->requests) == 0)
      g_cond_signal (&context->requests_received);
  g_mutex_unlock (&context->lock);
}

gboolean
gst_jpeg_enc_context_create (GstJPEGEncoderContext * context,
    GstStructure * params)
{
  gboolean ret = TRUE;
  qmmf::OfflineJpegCreateParams jpeg_params;

  if (context == NULL || params == NULL) {
    GST_ERROR ("NULL pointers!");
    return FALSE;
  }

  jpeg_params.process_mode = 0;
  gst_structure_get_uint (
      params, GST_JPEG_ENC_INPUT_WIDTH, &jpeg_params.in_buffer.width);
  gst_structure_get_uint (
      params, GST_JPEG_ENC_INPUT_HEIGHT, &jpeg_params.in_buffer.height);
  gst_structure_get_uint (
      params, GST_JPEG_ENC_INPUT_FORMAT, &jpeg_params.in_buffer.format);

  gst_structure_get_uint (
      params, GST_JPEG_ENC_OUTPUT_WIDTH, &jpeg_params.out_buffer.width);
  gst_structure_get_uint (
      params, GST_JPEG_ENC_OUTPUT_HEIGHT, &jpeg_params.out_buffer.height);
  gst_structure_get_uint (
      params, GST_JPEG_ENC_OUTPUT_FORMAT, &jpeg_params.out_buffer.format);
  gst_structure_free (params);
  qmmf::recorder::OfflineJpegCb callback =
      [&, context] (guint buf_fd, guint encoded_size)
      { gst_jpeg_enc_callback (context, buf_fd, encoded_size); };

  if (context->recorder->CreateOfflineJPEG(jpeg_params, callback) != 0) {
    GST_ERROR ("Cannot create the JPEG encoder");
    return FALSE;
  }

  GST_INFO ("Jpeg encoder created");

  return ret;
}

gboolean
gst_jpeg_enc_context_destroy (GstJPEGEncoderContext * context)
{
  if (context == NULL) {
    GST_ERROR ("NULL pointers!");
    return FALSE;
  }

  g_mutex_lock (&context->lock);
  if (g_hash_table_size (context->requests) > 0) {
    GST_INFO ("Waiting for all requests to be received");
    // Wait for all requests to be received before destry the JPEG encoder
    gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (10000000);
    gboolean timeout = g_cond_wait_until (&context->requests_received,
        &context->lock, wait_time);
    if (!timeout) {
      GST_ERROR ("Timeout on wait for all requests to be received");
    }
    GST_INFO ("All request are received");
  } else {
    GST_INFO ("No pending requests");
  }
  g_mutex_unlock (&context->lock);

  if (context->recorder->DestroyOfflineJPEG () != 0) {
    GST_ERROR ("Failed to destroy OfflineJPEG");
    return FALSE;
  }

  GST_INFO ("Jpeg encoder destroyed");

  return TRUE;
}

gboolean
gst_jpeg_enc_context_execute (GstJPEGEncoderContext * context,
    GstVideoCodecFrame * frame, gint quality)
{
  gboolean ret = TRUE;
  GST_DEBUG ("Jpeg encoder execute");
  GstMemory *inmemory = gst_buffer_peek_memory (frame->input_buffer, 0);
  GstMemory *outmemory = gst_buffer_peek_memory (frame->output_buffer, 0);

  if (!gst_is_fd_memory (inmemory)) {
    GST_ERROR ("Input buffer is not FD memory");
    return FALSE;
  }

  if (!gst_is_fd_memory (outmemory)) {
    GST_ERROR ("Output buffer is not FD memory");
    return FALSE;
  }

  qmmf::OfflineJpegProcessParams proc_params;
  proc_params.in_buf_fd = gst_fd_memory_get_fd (inmemory);
  proc_params.out_buf_fd = gst_fd_memory_get_fd (outmemory);
  proc_params.metadata.quality = quality;

  // calling EncodeOfflineJPEG() may cause thread context switch
  // to avoid this, we need to use mutex for EncodeOfflineJPEG()
  // and hash table access

  g_mutex_lock (&context->lock);

  g_hash_table_insert (context->requests,
      GINT_TO_POINTER (proc_params.out_buf_fd), frame);

  if (context->recorder->EncodeOfflineJPEG(proc_params) != 0) {
    GST_ERROR ("Failed to execute the Jpeg encoder");
    g_hash_table_remove (context->requests,
        GINT_TO_POINTER (proc_params.out_buf_fd));
    g_mutex_unlock (&context->lock);
    return FALSE;
  }

  g_mutex_unlock (&context->lock);

  return TRUE;
}
