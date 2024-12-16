/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_OFFLINECAMERA_CONTEXT_H__
#define __GST_OFFLINECAMERA_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstOfflineCameraBufferParams GstOfflineCameraBufferParams;
typedef struct _GstOfflineCameraContext GstOfflineCameraContext;

// Callback to pass buffer from context to plugin
typedef void (*GstOfflineCameraDataCb) (GstBuffer *buffer, gpointer userdata);
typedef void (*GstOfflineCameraEventCb) (guint event, gpointer userdata);

enum {
  EVENT_UNKNOWN,
  EVENT_SERVICE_DIED,
  EVENT_CAMERA_ERROR,
  EVENT_FRAME_ERROR,
  EVENT_METADATA_ERROR,
};

enum {
  PARAM_CAMERA_ID,
  PARAM_REQ_META_PATH,
  PARAM_REQ_META_STEP,
  PARAM_EIS,
  PARAM_SESSION_METADATA,
};

typedef enum {
  GST_OFFLINE_CAMERA_EIS_V2,
  GST_OFFLINE_CAMERA_EIS_V3,
  GST_OFFLINE_CAMERA_EIS_NONE,
} GstOfflineCameraEis;

// Parameters to create camera module session
struct _GstOfflineCameraBufferParams {
  gint           width;
  gint           height;
  GstVideoFormat format;
};

/**
 * gst_offline_camera_context_new
 *
 * Allocate memory of GstOfflineCameraContext.
 *
 * Return: Pointer point to GstOfflineCameraContext or NULL on failure.
 */
GstOfflineCameraContext*
gst_offline_camera_context_new ();

/**
 * gst_offline_camera_context_connect
 * @context: The pointer point to GstOfflineCameraContext.
 * @callback: Callback of listening to camera's events.
 * @userdata: Pointer point to plugn instance, callback needed.
 *
 * Connect to service.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_offline_camera_context_connect (GstOfflineCameraContext *context,
    GstOfflineCameraEventCb callback, gpointer userdata);

/**
 * gst_offline_camera_context_disconnect
 * @context: The pointer point to GstOfflineCameraContext.
 *
 * Disconnect to service.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_offline_camera_context_disconnect (GstOfflineCameraContext *context);

/**
 * gst_offline_camera_context_create
 * @context: The pointer point to GstOfflineCameraContext.
 * @params: Array of GstOfflineCameraBufferParams for creating session.
 * @callback: Callback to bring output buffer back async.
 *
 * Create offline camera module session.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_offline_camera_context_create (GstOfflineCameraContext *context,
    const GstOfflineCameraBufferParams params[2], GstOfflineCameraDataCb callback);

/**
 * gst_offline_camera_context_process
 * @context: The pointer point to GstOfflineCameraContext.
 * @inbuf: Pointer point to input GstBuffer.
 * @outbuf: Pointer point to output GstBuffer.
 *
 * Send requests to offline camera module to process.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_offline_camera_context_process (GstOfflineCameraContext *context,
    GstBuffer *inbuf, GstBuffer *outbuf);

/**
 * gst_offline_camera_context_destroy
 * @context: The pointer point to GstOfflineCameraContext.
 *
 * Destroy offline camera module session.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_offline_camera_context_destroy (GstOfflineCameraContext *context);

/**
 * gst_offline_camera_context_free
 * @context: The pointer point to GstOfflineCameraContext.
 *
 * Free structure of GstOfflineCameraContext.
 *
 * Return: None.
 */
void
gst_offline_camera_context_free (GstOfflineCameraContext *context);

/**
 * gst_offline_camera_context_set_property
 * @context: The pointer point to GstOfflineCameraContext.
 * @param_id: Param index enum.
 * @value: Value to set.
 *
 * Set properties in GstOfflineCameraContext.
 *
 * Return: None.
 */
void
gst_offline_camera_context_set_property (GstOfflineCameraContext *context,
    guint param_id, const GValue *value);

/**
 * gst_offline_camera_context_get_property
 * @context: The pointer point to GstOfflineCameraContext.
 * @param_id: Param index enum.
 * @value: Value to get.
 *
 * Get properties from GstOfflineCameraContext.
 *
 * Return: None.
 */
void
gst_offline_camera_context_get_property (GstOfflineCameraContext *context,
    guint param_id, GValue *value);

G_END_DECLS

#endif // __GST_OFFLINECAMERA_CONTEXT_H__
