/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "camera-image-reprocess-context.h"

#include <gst/allocators/allocators.h>

#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_offline_camera_params.h>
#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define GST_CAT_DEFAULT camera_image_reproc_context_debug_category()

#define PROCESS_MODE_OFFSET     4

#define PROCESS_MODE_GET(in, out) ((in << PROCESS_MODE_OFFSET) | out)

typedef enum {
  PROCESS_MODE_FLAG_UNKNOWN = 0,
  PROCESS_MODE_FLAG_YUV     = (1 << 0),
  PROCESS_MODE_FLAG_RAW     = (1 << 1),
  PROCESS_MODE_FLAG_JPEG    = (1 << 2),
} ProcessModeFlag;

typedef enum {
  PROCESS_MODE_INVALID    = 0,
  PROCESS_MODE_YUV_TO_YUV =
      ((PROCESS_MODE_FLAG_YUV << PROCESS_MODE_OFFSET) | PROCESS_MODE_FLAG_YUV),
  PROCESS_MODE_RAW_TO_YUV =
      ((PROCESS_MODE_FLAG_RAW << PROCESS_MODE_OFFSET) | PROCESS_MODE_FLAG_YUV),
  PROCESS_MODE_RAW_TO_JPEG =
      ((PROCESS_MODE_FLAG_RAW << PROCESS_MODE_OFFSET)
      | PROCESS_MODE_FLAG_JPEG),
} ProcessMode;

static GstDebugCategory *
camera_image_reproc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qticamimgreproc", 0,
        "Camera Image reprocess context");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

// Data Structure
struct _GstCameraImageReprocContext {
  /// QMMF Recorder instance
  ::qmmf::recorder::Recorder      *recorder;

  /// Callback to bring event to CameraImageReprocContext
  GstCameraImageReprocEventCb     event_cb;

  /// Callback to bring data to CameraImageReprocContext
  GstCameraImageReprocDataCb      data_cb;

  /// Plugin instance
  gpointer                        camimgreproc;

  /// Table recording all requests
  GHashTable                      *requests;

  /// Mutex
  GMutex                          lock;

  /// Signal to wait all output fd filled with data is back
  GCond                           requests_clear;

  /// Camera id to process
  guint                           camera_id[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM];

  /// Request metadata path
  gchar                           *req_meta_path[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM];
  /// Request metadata step
  guint                           req_meta_step[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM];

  /// Electronic Image Stabilization
  GstCameraImageReprocEis         eis[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM];
};

static void
event_callback (GstCameraImageReprocContext * context,
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

  context->event_cb (event, context->camimgreproc);
}

GstCameraImageReprocContext*
gst_camera_image_reproc_context_new (void)
{
  GstCameraImageReprocContext *context = NULL;
  gint idx;

  context = g_slice_new0 (GstCameraImageReprocContext);
  g_return_val_if_fail (context != NULL, NULL);

  context->recorder = new ::qmmf::recorder::Recorder ();
  if (!context->recorder) {
    GST_ERROR ("Failed to create Recorder.");
    g_slice_free (GstCameraImageReprocContext, context);
    return NULL;
  }

  context->requests = g_hash_table_new (NULL, NULL);
  for (idx = 0; idx < OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM; idx++) {
    context->camera_id[idx] = -1;
    context->req_meta_path[idx] = NULL;
    context->req_meta_step[idx] = 0;
  }

  g_mutex_init (&context->lock);
  g_cond_init (&context->requests_clear);

  return context;
}

gboolean
gst_camera_image_reproc_context_connect (GstCameraImageReprocContext *context,
    GstCameraImageReprocEventCb callback, gpointer userdata)
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
  context->camimgreproc = userdata;

  return TRUE;
}

gboolean
gst_camera_image_reproc_context_disconnect (GstCameraImageReprocContext *context)
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
gst_camera_image_reproc_context_free (GstCameraImageReprocContext *context)
{
  gint idx;

  delete context->recorder;
  context->recorder = NULL;

  if (context->requests != NULL) {
    g_hash_table_remove_all (context->requests);
    g_hash_table_destroy (context->requests);
    context->requests = NULL;
  }

  g_mutex_clear (&context->lock);
  g_cond_clear (&context->requests_clear);

  for (idx = 0; idx < OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM; idx++) {
    if (context->req_meta_path[idx] != NULL)
      g_free (context->req_meta_path[idx]);
  }

  g_slice_free (GstCameraImageReprocContext, context);

  GST_INFO ("GstCameraImageReprocContext freed.");
}

static void
gst_data_callback (GstCameraImageReprocContext *context, guint fd, guint size)
{
  GPtrArray *array = NULL;

  g_return_if_fail (context != NULL);
  g_return_if_fail (context->camimgreproc != NULL);

  GST_LOG ("Callback calling, outbuf fd(%u).", fd);

  g_mutex_lock (&context->lock);

  array = (GPtrArray *) g_hash_table_lookup (context->requests,
      GINT_TO_POINTER (fd));
  if (!array) {
    GST_WARNING ("Got uncached outbuf fd %u, func return.", fd);
    g_mutex_unlock (&context->lock);
    return;
  }

  // Callback will invoke gst_pad_push to push data downstream
  context->data_cb ((gpointer *)array, context->camimgreproc);

  g_ptr_array_unref(array);

  g_hash_table_remove (context->requests, GINT_TO_POINTER (fd));
  if (g_hash_table_size (context->requests) == 0)
    g_cond_signal (&context->requests_clear);

  g_mutex_unlock (&context->lock);
}

static ProcessMode
gst_parse_process_mode (GstVideoFormat in_format, GstVideoFormat out_format)
{
  ProcessModeFlag in_flag = PROCESS_MODE_FLAG_UNKNOWN;
  ProcessModeFlag out_flag = PROCESS_MODE_FLAG_UNKNOWN;
  ProcessMode mode = PROCESS_MODE_INVALID;

  switch (in_format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV12_Q08C:
    case GST_VIDEO_FORMAT_P010_10LE:
      in_flag = PROCESS_MODE_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_UNKNOWN:
      in_flag = PROCESS_MODE_FLAG_RAW;
      break;
    default:
      GST_WARNING ("Unsupported input format(%s) for camera reprocess.",
          gst_video_format_to_string (in_format));
      break;
  }

  switch (out_format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV12_Q08C:
    case GST_VIDEO_FORMAT_P010_10LE:
      out_flag = PROCESS_MODE_FLAG_YUV;
      break;
    case GST_VIDEO_FORMAT_ENCODED:
      out_flag = PROCESS_MODE_FLAG_JPEG;
      break;
    default:
      GST_WARNING ("Unsupported output format(%s) for camera reprocess.",
          gst_video_format_to_string (out_format));
      break;
  }

  mode = (ProcessMode)PROCESS_MODE_GET(in_flag, out_flag);

  return mode;
}

static ::qmmf::recorder::VideoFormat
gst_convert_to_video_format (GstVideoFormat videoformat)
{
  qmmf::recorder::VideoFormat format;

  switch (videoformat) {
    case GST_VIDEO_FORMAT_UNKNOWN:
      format = ::qmmf::recorder::VideoFormat::kBayerRDI10BIT;
      break;
    case GST_VIDEO_FORMAT_ENCODED:
      format = ::qmmf::recorder::VideoFormat::kJPEG;
      break;
    case GST_VIDEO_FORMAT_NV12:
      format = ::qmmf::recorder::VideoFormat::kNV12;
      break;
    case GST_VIDEO_FORMAT_NV12_Q08C:
      format = ::qmmf::recorder::VideoFormat::kNV12UBWC;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      format = ::qmmf::recorder::VideoFormat::kP010;
      break;
    default:
      GST_ERROR ("Unsupported format(%s).",
          gst_video_format_to_string (videoformat));
      format = ::qmmf::recorder::VideoFormat::kNV12;
      break;
  }

  return format;
}

static guint
gst_retrieve_vendor_tag_by_name (::camera::CameraMetadata *meta,
    const gchar * name)
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
gst_fill_metadata_from_properties (GstCameraImageReprocContext *context,
    gint idx, ::camera::CameraMetadata *meta)
{
  g_return_if_fail (context->recorder != NULL);
  g_return_if_fail (idx < OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM);

  // Eis
  switch (context->eis[idx]) {
    case GST_CAMERA_IMAGE_REPROC_EIS_NONE:
    {
      break;
    }
    case GST_CAMERA_IMAGE_REPROC_EIS_V2:
    case GST_CAMERA_IMAGE_REPROC_EIS_V3:
    {
      guint tag = 0;
      gint32 val = (gint32)context->eis[idx];

      tag = gst_retrieve_vendor_tag_by_name (meta,
          "org.codeaurora.qcamera3.sessionParameters.EISMode");
      if (tag == 0) {
        GST_WARNING ("Unsupported vendortag.");
        break;
      }

      if (!meta->update (tag, &val, 1))
        GST_DEBUG ("Metadata EISMode(%d) is updated.", val);
      else
        GST_ERROR ("Metadata EISMode(%d) failed to update.", val);

      break;
    }
    default:
    {
      GST_WARNING ("Unknown EISMode(%d).", (gint32)context->eis[0]);
      break;
    }
  }
}

void
gst_camera_image_reproc_context_update (GstCameraImageReprocContext *context,
    guint idx, guint camera_id, gchar *req_meta_path, guint req_meta_step,
    GstCameraImageReprocEis eis)
{
  if (idx >= OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM) {
    GST_ERROR ("Exceeded the maximum supported");
    return;
  }

  context->camera_id[idx] = camera_id;

  if (context->req_meta_path[idx] != NULL)
    g_free (context->req_meta_path[idx]);
  context->req_meta_path[idx] = req_meta_path;

  context->req_meta_step[idx] = req_meta_step;
  context->eis[idx] = eis;
}

gboolean
gst_camera_image_reproc_context_create (GstCameraImageReprocContext *context,
    const GstCameraImageParams params[2], GstCameraImageReprocDataCb callback)
{
  qmmf::OfflineCameraCreateParams offcam_params = {};
  ProcessMode process_mode = PROCESS_MODE_INVALID;
  ::camera::CameraMetadata meta;
  gint rc;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (params != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  // Fill CreateParams
  // CameraID
  offcam_params.camera_id[0] = context->camera_id[0];
  offcam_params.camera_id[1] = context->camera_id[1];

  // BufferParams of input
  offcam_params.in_buffer.width = params[0].width;
  g_return_val_if_fail (offcam_params.in_buffer.width > 0, FALSE);
  offcam_params.in_buffer.height = params[0].height;
  g_return_val_if_fail (offcam_params.in_buffer.height > 0, FALSE);
  offcam_params.in_buffer.format = gst_convert_to_video_format (params[0].format);

  GST_DEBUG ("InputParam: %u x %u, %s",
      offcam_params.in_buffer.width, offcam_params.in_buffer.height,
      gst_video_format_to_string (params[0].format));

  // BufferParams of output
  offcam_params.out_buffer.width = params[1].width;
  g_return_val_if_fail (offcam_params.out_buffer.width > 0, FALSE);
  offcam_params.out_buffer.height = params[1].height;
  g_return_val_if_fail (offcam_params.out_buffer.height > 0, FALSE);
  offcam_params.out_buffer.format = gst_convert_to_video_format (params[1].format);

  GST_DEBUG ("OutputParam: %u x %u, %s",
      offcam_params.out_buffer.width, offcam_params.out_buffer.height,
      gst_video_format_to_string (params[1].format));

  // Process mode
  process_mode = gst_parse_process_mode (params[0].format, params[1].format);
  switch (process_mode) {
    case PROCESS_MODE_INVALID:
      GST_ERROR ("Invalid process-mode.");
      return FALSE;
    case PROCESS_MODE_YUV_TO_YUV:
      offcam_params.process_mode = qmmf::YUVToYUV;
      GST_DEBUG ("Process-mode: YUVToYUV.");
      break;
    case PROCESS_MODE_RAW_TO_YUV:
      offcam_params.process_mode = qmmf::RAWToYUV;
      GST_DEBUG ("Process-mode: RAWToYUV.");
      break;
    case PROCESS_MODE_RAW_TO_JPEG:
      offcam_params.process_mode = qmmf::RAWToJPEGSBS;
      GST_DEBUG ("Process-mode: RAWToJPEGSBS.");
      break;
    default:
      GST_ERROR ("Unknown process-mode.");
      return FALSE;
  }

  // Request metadata path
  if (context->req_meta_path[0] != NULL)
    g_strlcpy (offcam_params.request_metadata_path[0],
        context->req_meta_path[0], OFFLINE_CAMERA_REQ_METADATA_PATH_MAX);
  if (context->req_meta_path[1] != NULL)
    g_strlcpy (offcam_params.request_metadata_path[1],
        context->req_meta_path[1], OFFLINE_CAMERA_REQ_METADATA_PATH_MAX);

  // Request metadata step
  offcam_params.metadata_step[0] = context->req_meta_step[0];
  offcam_params.metadata_step[1] = context->req_meta_step[1];
  GST_DEBUG ("request meta path: %s, request meta step: %u.",
      offcam_params.request_metadata_path[0], offcam_params.metadata_step[0]);

  // Fill metadata
  gst_fill_metadata_from_properties (context, 0, &meta);
  offcam_params.session_meta[0] = meta;
  gst_fill_metadata_from_properties (context, 1, &meta);
  offcam_params.session_meta[1] = meta;

  qmmf::recorder::OfflineCameraCb offcam_cb =
      [&, context] (guint buf_fd, guint encoded_size)
      { gst_data_callback (context, buf_fd, encoded_size); };

  rc = context->recorder->CreateOfflineCamera (offcam_params, offcam_cb);
  if (rc != 0) {
    GST_ERROR ("Failed to CreateOfflineCamera.");
    return FALSE;
  }

  context->data_cb = callback;

  return TRUE;
}

gboolean
gst_camera_image_reproc_context_process (GstCameraImageReprocContext *context,
    guint inbufnum, GstBuffer *inbuf[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM],
    GstBuffer *outbuf)
{
  GstMemory *inmem = NULL;
  GstMemory *outmem = NULL;
  GPtrArray *ptr_array = NULL;
  qmmf::OfflineCameraProcessParams params;
  guint idx = 0;
  gint in_buf_fd[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM] = {-1};
  gint out_buf_fd = -1;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (inbuf[0] != NULL, FALSE);
  g_return_val_if_fail (outbuf != NULL, FALSE);
  g_return_val_if_fail (inbufnum <= OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM, FALSE);

  for (idx = 0; idx < inbufnum; idx++) {
    inmem = gst_buffer_peek_memory (inbuf[idx], 0);
    if ((!inmem) || (!gst_is_fd_memory(inmem))) {
      GST_ERROR ("Failed to peek memory from input buffer(%p).", inbuf[idx]);
      return FALSE;
    }

    in_buf_fd[idx] = gst_fd_memory_get_fd (inmem);
    g_return_val_if_fail (in_buf_fd[idx] >= 0, GST_FLOW_ERROR);
  }

  outmem = gst_buffer_peek_memory (outbuf, 0);
  if ((!outmem) || (!gst_is_fd_memory(outmem))) {
    GST_ERROR ("Failed to peek memory from output buffer(%p).", outbuf);
    return FALSE;
  }

  out_buf_fd = gst_fd_memory_get_fd (outmem);
  g_return_val_if_fail (out_buf_fd >= 0, GST_FLOW_ERROR);

  params.in_buf_fd[0] = in_buf_fd[0];
  params.in_buf_fd[1] = in_buf_fd[1];
  params.out_buf_fd = out_buf_fd;

  GST_LOG ("inbuf fd0(%u), inbuf fd1(%u), outbuf fd(%u).", params.in_buf_fd[0],
      params.in_buf_fd[1], params.out_buf_fd);

  ptr_array = g_ptr_array_sized_new (OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM + 1);
  if (ptr_array == NULL) {
    GST_ERROR ("Failed to allocate GPtrArray.");
    return FALSE;
  }

  for (idx = 0; idx < OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM; idx++)
    g_ptr_array_add (ptr_array, inbuf[idx]);
  g_ptr_array_add (ptr_array, outbuf);

  g_mutex_lock (&context->lock);

  g_hash_table_insert (context->requests,
      GINT_TO_POINTER (params.out_buf_fd), ptr_array);

  if (context->recorder->ProcessOfflineCamera (params) != 0) {
    GST_ERROR ("Failed to ProcessOfflineCamera.");
    g_hash_table_remove (context->requests,
        GINT_TO_POINTER (params.out_buf_fd));
    g_ptr_array_free (ptr_array, TRUE);
    g_mutex_unlock (&context->lock);
    return FALSE;
  }

  g_mutex_unlock (&context->lock);

  return TRUE;
}

gboolean
gst_camera_image_reproc_context_destroy (GstCameraImageReprocContext *context)
{
  guint size = 0;

  g_return_val_if_fail (context != NULL, FALSE);

  // Wait all requests back
  g_mutex_lock (&context->lock);

  size = g_hash_table_size (context->requests);
  if (size > 0) {
    gint64 wait_time;
    gboolean timeout;

    GST_DEBUG ("Waiting last %u requests to return in %d microseconds.", size,
        OFFLINE_CAMERA_TIMEOUT);

    wait_time = g_get_monotonic_time () + OFFLINE_CAMERA_TIMEOUT;
    timeout = g_cond_wait_until (&context->requests_clear, &context->lock,
        wait_time);
    if (!timeout)
      GST_ERROR ("Timeout on wait for all requests to be received");

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
