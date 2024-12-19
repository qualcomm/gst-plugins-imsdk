/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CAMERA_REPROCESS_CONTEXT_H__
#define __GST_CAMERA_REPROCESS_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstCameraReprocessBufferParams GstCameraReprocessBufferParams;
typedef struct _GstCameraReprocessContext GstCameraReprocessContext;

// Callback to pass ptr array of GstBuffers from context to plugin
typedef void (*GstCameraReprocessDataCb) (gpointer *array, gpointer userdata);
typedef void (*GstCameraReprocessEventCb) (guint event, gpointer userdata);

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
  GST_CAMERA_REPROCESS_EIS_V2,
  GST_CAMERA_REPROCESS_EIS_V3,
  GST_CAMERA_REPROCESS_EIS_NONE,
} GstCameraReprocessEis;

// Parameters to create camera module session
struct _GstCameraReprocessBufferParams {
  gint           width;
  gint           height;
  GstVideoFormat format;
  gboolean       isubwc;
};

/**
 * gst_camera_reprocess_context_new
 *
 * Allocate memory of GstCameraReprocessContext.
 *
 * Return: Pointer point to GstCameraReprocessContext or NULL on failure.
 */
GstCameraReprocessContext*
gst_camera_reprocess_context_new ();

/**
 * gst_camera_reprocess_context_connect
 * @context: The pointer point to GstCameraReprocessContext.
 * @callback: Callback of listening to camera's events.
 * @userdata: Pointer point to plugn instance, callback needed.
 *
 * Connect to service.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_reprocess_context_connect (GstCameraReprocessContext *context,
    GstCameraReprocessEventCb callback, gpointer userdata);

/**
 * gst_camera_reprocess_context_disconnect
 * @context: The pointer point to GstCameraReprocessContext.
 *
 * Disconnect to service.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_reprocess_context_disconnect (GstCameraReprocessContext *context);

/**
 * gst_camera_reprocess_context_create
 * @context: The pointer point to GstCameraReprocessContext.
 * @params: Array of GstCameraReprocessBufferParams for creating session.
 * @callback: Callback to bring output buffer back async.
 *
 * Create camera reprocess module session.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_reprocess_context_create (GstCameraReprocessContext *context,
    const GstCameraReprocessBufferParams params[2],
    GstCameraReprocessDataCb callback);

/**
 * gst_camera_reprocess_context_process
 * @context: The pointer point to GstCameraReprocessContext.
 * @inbuf: Pointer point to input GstBuffer.
 * @outbuf: Pointer point to output GstBuffer.
 *
 * Send requests to camera reprocess module to process.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_reprocess_context_process (GstCameraReprocessContext *context,
    GstBuffer *inbuf, GstBuffer *outbuf);

/**
 * gst_camera_reprocess_context_destroy
 * @context: The pointer point to GstCameraReprocessContext.
 *
 * Destroy camera reprocess module session.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_reprocess_context_destroy (GstCameraReprocessContext *context);

/**
 * gst_camera_reprocess_context_free
 * @context: The pointer point to GstCameraReprocessContext.
 *
 * Free structure of GstCameraReprocessContext.
 *
 * Return: None.
 */
void
gst_camera_reprocess_context_free (GstCameraReprocessContext *context);

/**
 * gst_camera_reprocess_context_set_property
 * @context: The pointer point to GstCameraReprocessContext.
 * @param_id: Param index enum.
 * @value: Value to set.
 *
 * Set properties in GstCameraReprocessContext.
 *
 * Return: None.
 */
void
gst_camera_reprocess_context_set_property (GstCameraReprocessContext *context,
    guint param_id, const GValue *value);

/**
 * gst_camera_reprocess_context_get_property
 * @context: The pointer point to GstCameraReprocessContext.
 * @param_id: Param index enum.
 * @value: Value to get.
 *
 * Get properties from GstCameraReprocessContext.
 *
 * Return: None.
 */
void
gst_camera_reprocess_context_get_property (GstCameraReprocessContext *context,
    guint param_id, GValue *value);

G_END_DECLS

#endif // __GST_CAMERA_REPROCESS_CONTEXT_H__
