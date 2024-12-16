/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "offline-camera-context.h"

#include <gst/allocators/allocators.h>
#include <system/graphics.h>

#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_offline_camera_params.h>
#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define GST_CAT_DEFAULT offline_camera_context_debug_category()

#define PROCESS_MODE_OFFSET     4

#define PROCESS_MODE_GET(in, out) ((in << PROCESS_MODE_OFFSET) | out)

typedef enum {
  PROCESS_MODE_FLAG_UNKNOWN = 0,
  PROCESS_MODE_FLAG_YUV     = (1 << 0),
} ProcessModeFlag;

typedef enum {
  PROCESS_MODE_INVALID    = 0,
  PROCESS_MODE_YUV_TO_YUV =
      ((PROCESS_MODE_FLAG_YUV << PROCESS_MODE_OFFSET) | PROCESS_MODE_FLAG_YUV),
} ProcessMode;

static GstDebugCategory *
offline_camera_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtiofflinecamera", 0,
        "Offline camera context");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

// Data Structure
struct _GstOfflineCameraContext {
  /// QMMF Recorder instance
  ::qmmf::recorder::Recorder      *recorder;

  /// QMMF Offline Camera Input Buffer params
  qmmf::OfflineCameraBufferParams inbuf_params;
  /// QMMF Offline Camera Output Buffer params
  qmmf::OfflineCameraBufferParams outbuf_params;

  /// Callback to bring event to OfflineCameraContext
  GstOfflineCameraEventCb         event_cb;

  /// Callback to bring data to OfflineCameraContext
  GstOfflineCameraDataCb          data_cb;

  /// Plugin instance
  gpointer                        offcam;

  /// Table recording all requests
  GHashTable                      *requests;

  /// Mutex
  GMutex                          lock;

  /// Signal to wait all output fd filled with data is back
  GCond                           requests_clear;

  /// Camera id to process
  guint                           camera_id;

  /// Request metadata path
  gchar                           *req_meta_path;
  /// Request metadata step
  guint                           req_meta_step;

  /// Electronic Image Stabilization
  GstOfflineCameraEis             eis;

  /// Session metadata
  camera::CameraMetadata          *session_metadata;
};

static void
event_callback (GstOfflineCameraContext * context,
    ::qmmf::recorder::EventType type, void * payload, size_t size)
{
  guint event = 0;

  switch (type) {
    case qmmf::recorder::EventType::kServerDied:
      event = EVENT_SERVICE_DIED;
      break;
    case qmmf::recorder::EventType::kCameraError:
    {
      g_assert (size == sizeof (guint));

      event = EVENT_CAMERA_ERROR;
      break;
    }
    case qmmf::recorder::EventType::kFrameError:
    {
      g_assert (size == sizeof (guint));

      event = EVENT_FRAME_ERROR;
      break;
    }
    case qmmf::recorder::EventType::kMetadataError:
    {
      g_assert (size == sizeof (guint));

      event = EVENT_METADATA_ERROR;
      break;
    }
    case qmmf::recorder::EventType::kUnknown:
    default:
      event = EVENT_UNKNOWN;
      GST_WARNING ("Unknown event type occured.");
      return;
  }

  context->event_cb (event, context->offcam);
}

GstOfflineCameraContext*
gst_offline_camera_context_new ()
{
  GstOfflineCameraContext *context = NULL;

  context = g_slice_new0 (GstOfflineCameraContext);
  g_return_val_if_fail (context != NULL, NULL);

  context->recorder = new ::qmmf::recorder::Recorder ();
  if (!context->recorder) {
    GST_ERROR ("Failed to create Recorder.");
    g_slice_free (GstOfflineCameraContext, context);
    return NULL;
  }

  context->requests = g_hash_table_new (NULL, NULL);

  g_mutex_init (&context->lock);
  g_cond_init (&context->requests_clear);

  context->session_metadata = NULL;

  return context;
}

gboolean
gst_offline_camera_context_connect (GstOfflineCameraContext *context,
    GstOfflineCameraEventCb callback, gpointer userdata)
{
  ::qmmf::recorder::RecorderCb cbs;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);
  g_return_val_if_fail (userdata != NULL, FALSE);
  g_return_val_if_fail (context->recorder != NULL, FALSE);

  cbs.event_cb =
      [&, context] (::qmmf::recorder::EventType type, void *data, size_t size)
      { event_callback (context, type, data, size); };

  GST_INFO ("Connecting to QMMF Recorder.");

  if (context->recorder->Connect (cbs)) {
    GST_ERROR ("Failed to connect to QMMF Recorder!");
    return FALSE;
  }

  GST_INFO ("Connected to QMMF Recorder.");

  context->event_cb = callback;
  context->offcam = userdata;

  return TRUE;
}

gboolean
gst_offline_camera_context_disconnect (GstOfflineCameraContext *context)
{
  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (context->recorder != NULL, FALSE);

  GST_INFO ("Disconnecting QMMF Recorder.");

  if (context->recorder->Disconnect ()) {
    GST_ERROR ("Failed to disconnect QMMF Recorder.");
    return FALSE;
  }

  GST_INFO ("Disconnected QMMF Recorder.");

  return TRUE;
}

void
gst_offline_camera_context_free (GstOfflineCameraContext *context)
{
  delete context->recorder;
  context->recorder = NULL;

  if (context->requests != NULL) {
    g_hash_table_remove_all (context->requests);
    g_hash_table_destroy (context->requests);
    context->requests = NULL;
  }

  g_mutex_clear (&context->lock);
  g_cond_clear (&context->requests_clear);

  g_free (context->req_meta_path);

  g_slice_free (GstOfflineCameraContext, context);

  GST_INFO ("GstOfflineCameraContext freed.");
}

static void
data_callback (GstOfflineCameraContext *context, guint fd, guint size)
{
  GstBuffer *outbuf = NULL;
  GstBuffer *inbuf = NULL;
  GPtrArray *array = NULL;

  g_return_if_fail (context != NULL);
  g_return_if_fail (context->offcam != NULL);

  GST_LOG ("Callback calling, outbuf fd(%u).", fd);

  g_mutex_lock (&context->lock);
  array = (GPtrArray *) g_hash_table_lookup (context->requests,
      GINT_TO_POINTER (fd));

  inbuf = (GstBuffer *) g_ptr_array_index (array, 0);
  outbuf = (GstBuffer *) g_ptr_array_index (array, 1);
  g_ptr_array_free (array, FALSE);
  gst_buffer_unref (inbuf);

  if (!outbuf) {
    GST_WARNING ("Got uncached outbuf fd %u, func return.", fd);
    g_mutex_unlock (&context->lock);
    return;
  }

  // Callback will invoke gst_pad_push to push data downstream
  context->data_cb (outbuf, context->offcam);

  g_hash_table_remove (context->requests, GINT_TO_POINTER (fd));
  if (g_hash_table_size (context->requests) == 0)
    g_cond_signal (&context->requests_clear);
  g_mutex_unlock (&context->lock);
}

static ProcessMode
parse_process_mode (GstVideoFormat in_format, GstVideoFormat out_format)
{
  ProcessModeFlag in_flag = PROCESS_MODE_FLAG_UNKNOWN;
  ProcessModeFlag out_flag = PROCESS_MODE_FLAG_UNKNOWN;
  ProcessMode mode = PROCESS_MODE_INVALID;

  switch (in_format) {
    case GST_VIDEO_FORMAT_NV12:
      in_flag = PROCESS_MODE_FLAG_YUV;
      break;
    default:
      GST_WARNING ("Unsupported input format(%s) for offline camera.",
          gst_video_format_to_string (in_format));
      break;
  }

  switch (out_format) {
    case GST_VIDEO_FORMAT_NV12:
      out_flag = PROCESS_MODE_FLAG_YUV;
      break;
    default:
      GST_WARNING ("Unsupported output format(%s) for offline camera.",
          gst_video_format_to_string (out_format));
      break;
  }

  mode = (ProcessMode)PROCESS_MODE_GET(in_flag, out_flag);

  return mode;
}

static guint
convert_video_format_to_graphic_format (GstVideoFormat format)
{
  guint ret = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      ret = HAL_PIXEL_FORMAT_YCBCR_420_888;
      break;
    default:
      GST_ERROR ("Unsupported format(%s).", gst_video_format_to_string (format));
      break;
  }
  return ret;
}

static guint
retrieve_vendor_tag_by_name (::camera::CameraMetadata *meta, const gchar * name)
{
  std::shared_ptr<VendorTagDescriptor> vtags;
  guint tag_id = 0;

  vtags = VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return 0;
  }

  if (meta->getTagFromName (name, vtags.get(), &tag_id) != 0) {
    GST_ERROR ("Failed to found tag %u of %s", tag_id, name);
    return 0;
  }

  GST_DEBUG ("Found tag %u of %s", tag_id, name);

  return tag_id;
}

static void
fill_metadata_from_properties (GstOfflineCameraContext *context,
    ::camera::CameraMetadata *meta)
{
  g_return_if_fail (context->recorder != NULL);

  // Eis
  switch (context->eis) {
    case GST_OFFLINE_CAMERA_EIS_NONE:
    {
      break;
    }
    case GST_OFFLINE_CAMERA_EIS_V2:
    {
      guint tag = 0;
      guint8 val = 1;

      tag = retrieve_vendor_tag_by_name (meta,
          "org.codeaurora.qcamera3.sessionParameters.EnableEisV2");
      if (tag > 0) {
        if (!meta->update (tag, &val, 1))
          GST_DEBUG ("Metadata EisV2 is updated.");
        else
          GST_ERROR ("Metadata EisV2 is failed to update.");
      }
      break;
    }
    case GST_OFFLINE_CAMERA_EIS_V3:
    {
      guint tag = 0;
      guint8 val = 1;

      tag = retrieve_vendor_tag_by_name (meta,
          "org.codeaurora.qcamera3.sessionParameters.EnableEisV3");
      if (tag > 0) {
        if (!meta->update (tag, &val, 1))
          GST_DEBUG ("Metadata EisV3 is updated.");
        else
          GST_ERROR ("Metadata EisV3 is failed to update.");
      }
      break;
    }
    default:
    {
      GST_WARNING ("Unknown Eis.");
      break;
    }
  }
}

gboolean
gst_offline_camera_context_create (GstOfflineCameraContext *context,
    const GstOfflineCameraBufferParams params[2],
    GstOfflineCameraDataCb callback)
{
  qmmf::OfflineCameraCreateParams offcam_params;
  ProcessMode process_mode = PROCESS_MODE_INVALID;
  const gchar *req_meta_path = NULL;
  ::camera::CameraMetadata meta;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (params != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  // Fill CreateParams
    // CameraID
  offcam_params.camera_id = context->camera_id;
    // BufferParams of input
  offcam_params.in_buffer.width = params[0].width;
  g_return_val_if_fail (offcam_params.in_buffer.width > 0, FALSE);
  offcam_params.in_buffer.height = params[0].height;
  g_return_val_if_fail (offcam_params.in_buffer.height > 0, FALSE);
  offcam_params.in_buffer.format =
      convert_video_format_to_graphic_format (params[0].format);
  g_return_val_if_fail (offcam_params.in_buffer.format > 0, FALSE);

    // BufferParams of output
  offcam_params.out_buffer.width = params[1].width;
  g_return_val_if_fail (offcam_params.out_buffer.width > 0, FALSE);
  offcam_params.out_buffer.height = params[1].height;
  g_return_val_if_fail (offcam_params.out_buffer.height > 0, FALSE);
  offcam_params.out_buffer.format =
      convert_video_format_to_graphic_format (params[1].format);
  g_return_val_if_fail (offcam_params.out_buffer.format > 0, FALSE);

    // Process mode
  process_mode = parse_process_mode (params[0].format, params[1].format);
  switch (process_mode) {
    case PROCESS_MODE_INVALID:
      GST_ERROR ("Invalid process-mode.");
      return FALSE;
    case PROCESS_MODE_YUV_TO_YUV:
      offcam_params.process_mode = qmmf::YUVToYUV;
      GST_DEBUG ("Process-mode: YUVToYUV.");
      break;
    default:
      GST_ERROR ("Unknown process-mode.");
      return FALSE;
  }

    // Request metadata path
  if (context->req_meta_path != NULL)
    g_strlcpy (offcam_params.request_metadata_path,
        context->req_meta_path, OFFLINE_CAMERA_REQ_METADATA_PATH_MAX);

    // Request metadata step
  offcam_params.metadata_step = context->req_meta_step;
  GST_DEBUG ("request meta path: %s, request meta step: %u.",
      offcam_params.request_metadata_path, offcam_params.metadata_step);

    // Fill metadata
  if (context->session_metadata == NULL) {
    GST_DEBUG ("Fill metadata from properties.");
    fill_metadata_from_properties (context, &meta);
    offcam_params.session_meta = meta;
  } else {
    GST_DEBUG ("Fill metadata from external pointer.");
    offcam_params.session_meta = *context->session_metadata;
  }

  qmmf::recorder::OfflineCameraCb offcam_cb =
      [&, context] (guint buf_fd, guint encoded_size)
      { data_callback (context, buf_fd, encoded_size); };

  if (context->recorder->CreateOfflineCamera (offcam_params, offcam_cb) != 0) {
    GST_ERROR ("Failed to CreateOfflineCamera.");
    return FALSE;
  }

  context->data_cb = callback;

  return TRUE;
}

gboolean
gst_offline_camera_context_process (GstOfflineCameraContext *context,
    GstBuffer *inbuf, GstBuffer *outbuf)
{
  GstMemory *inmem = NULL;
  GstMemory *outmem = NULL;
  GPtrArray *ptr_array = NULL;
  qmmf::OfflineCameraProcessParams params;
  gint in_buf_fd = -1;
  gint out_buf_fd = -1;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (inbuf != NULL, FALSE);
  g_return_val_if_fail (outbuf != NULL, FALSE);

  inmem = gst_buffer_peek_memory (inbuf, 0);
  if (!inmem) {
    GST_ERROR ("Failed to peek memory from input buffer(%p).", inbuf);
    return FALSE;
  }
  in_buf_fd = gst_fd_memory_get_fd (inmem);
  g_return_val_if_fail (in_buf_fd >= 0, GST_FLOW_ERROR);

  outmem = gst_buffer_peek_memory (outbuf, 0);
  if (!outmem) {
    GST_ERROR ("Failed to peek memory from output buffer(%p).", outbuf);
    return FALSE;
  }
  out_buf_fd = gst_fd_memory_get_fd (outmem);
  g_return_val_if_fail (out_buf_fd >= 0, GST_FLOW_ERROR);

  params.in_buf_fd = in_buf_fd;
  params.out_buf_fd = out_buf_fd;
  GST_LOG ("inbuf fd(%u), outbuf fd(%u).", params.in_buf_fd, params.out_buf_fd);

  ptr_array = g_ptr_array_sized_new (2);
  if (ptr_array == NULL) {
    GST_ERROR ("Failed to allocate GPtrArray.");
    return FALSE;
  }
  g_ptr_array_index (ptr_array, 0) = inbuf;
  g_ptr_array_index (ptr_array, 1) = outbuf;

  g_mutex_lock (&context->lock);

  g_hash_table_insert (context->requests,
      GINT_TO_POINTER (params.out_buf_fd), ptr_array);

  if (context->recorder->ProcessOfflineCamera (params) != 0) {
    GST_ERROR ("Failed to ProcessOfflineCamera.");
    g_hash_table_remove (context->requests, GINT_TO_POINTER (params.out_buf_fd));
    g_ptr_array_free (ptr_array, FALSE);
    g_mutex_unlock (&context->lock);
    return FALSE;
  }

  g_mutex_unlock (&context->lock);

  return TRUE;
}

gboolean
gst_offline_camera_context_destroy (GstOfflineCameraContext *context)
{
  guint size = 0;

  g_return_val_if_fail (context != NULL, FALSE);

  // Wait all requests back
  g_mutex_lock (&context->lock);
  size = g_hash_table_size (context->requests);
  if (size > 0) {
    GST_DEBUG ("Waiting last %u requests to return in 2 seconds.", size);
    gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (2000000);
    gboolean timeout = g_cond_wait_until (&context->requests_clear,
        &context->lock, wait_time);
    if (!timeout) {
      GST_ERROR ("Timeout on wait for all requests to be received");
    }
    GST_DEBUG ("All request are received");
  } else {
    GST_DEBUG ("No pending requests");
  }
  g_mutex_unlock (&context->lock);

  if (context->recorder->DestroyOfflineCamera () != 0) {
    GST_ERROR ("Failed to DestroyOfflineCamera.");
    return FALSE;
  }

  return TRUE;
}

void
gst_offline_camera_context_set_property (GstOfflineCameraContext *context,
    guint param_id, const GValue *value)
{
  switch (param_id) {
    case PARAM_CAMERA_ID:
      context->camera_id = g_value_get_uint (value);
      break;
    case PARAM_REQ_META_PATH:
      context->req_meta_path = g_strdup (g_value_get_string (value));
      break;
    case PARAM_REQ_META_STEP:
      context->req_meta_step = g_value_get_uint (value);
      break;
    case PARAM_EIS:
      context->eis = (GstOfflineCameraEis)g_value_get_enum (value);
      break;
    case PARAM_SESSION_METADATA:
      context->session_metadata =
          (camera::CameraMetadata *)g_value_get_pointer (value);
      break;
    default:
      GST_ERROR ("OfflineCameraContext doesn't support this property.");
      break;
  }
}

void
gst_offline_camera_context_get_property (GstOfflineCameraContext *context,
    guint param_id, GValue *value)
{
  switch (param_id) {
    case PARAM_CAMERA_ID:
      g_value_set_uint (value, context->camera_id);
      break;
    case PARAM_REQ_META_PATH:
      g_value_set_string (value, context->req_meta_path);
      break;
    case PARAM_REQ_META_STEP:
      g_value_set_uint (value, context->req_meta_step);
      break;
    case PARAM_EIS:
      g_value_set_enum (value, context->eis);
      break;
    case PARAM_SESSION_METADATA:
      g_value_set_pointer (value, (gpointer)context->session_metadata);
      break;
    default:
      GST_ERROR ("OfflineCameraContext doesn't support this property.");
      break;
  }
}
