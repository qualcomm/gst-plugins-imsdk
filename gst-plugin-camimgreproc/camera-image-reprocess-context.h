/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_CAMERA_IMAGE_REPROCESS_CONTEXT_H__
#define __GST_CAMERA_IMAGE_REPROCESS_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM 2
// timeout unit is micro second
#define OFFLINE_CAMERA_TIMEOUT             2000000

typedef struct _GstCameraImageParams GstCameraImageParams;
typedef struct _GstCameraImageReprocContext GstCameraImageReprocContext;

// Callback to pass ptr array of GstBuffers from context to plugin
typedef void (*GstCameraImageReprocDataCb) (gpointer *array, gpointer userdata);
typedef void (*GstCameraImageReprocEventCb) (guint event, gpointer userdata);

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
};

typedef enum {
  GST_CAMERA_IMAGE_REPROC_EIS_V3 = 0,
  GST_CAMERA_IMAGE_REPROC_EIS_V2 = 1,
  GST_CAMERA_IMAGE_REPROC_EIS_NONE,
} GstCameraImageReprocEis;

// Parameters to create camera module session
struct _GstCameraImageParams {
  gint           width;
  gint           height;
  GstVideoFormat format;
};

/**
 * gst_camera_image_reproc_context_new
 *
 * Allocate memory of GstCameraImageReprocContext.
 *
 * Return: Pointer point to GstCameraImageReprocContext or NULL on failure.
 */
GstCameraImageReprocContext*
gst_camera_image_reproc_context_new (void);

/**
 * gst_camera_image_reproc_context_connect
 * @context: The pointer point to GstCameraImageReprocContext.
 * @callback: Callback of listening to camera's events.
 * @userdata: Pointer point to plugn instance, callback needed.
 *
 * Connect to service.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_image_reproc_context_connect (GstCameraImageReprocContext *context,
                                         GstCameraImageReprocEventCb callback,
                                         gpointer userdata);

/**
 * gst_camera_image_reproc_context_disconnect
 * @context: The pointer point to GstCameraImageReprocContext.
 *
 * Disconnect to service.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_image_reproc_context_disconnect (GstCameraImageReprocContext *context);

/**
 * gst_camera_image_reproc_context_create
 * @context: The pointer point to GstCameraImageReprocContext.
 * @params: Array of GstCameraImageParams for creating session.
 * @callback: Callback to bring output buffer back async.
 *
 * Create camera reprocess module session.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_image_reproc_context_create (GstCameraImageReprocContext *context,
                                        const GstCameraImageParams
                                        params[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM],
                                        GstCameraImageReprocDataCb callback);

/**
 * gst_camera_image_reproc_context_process
 * @context: The pointer point to GstCameraImageReprocContext.
 * @inbuf: Pointer point to input GstBuffer.
 * @outbuf: Pointer point to output GstBuffer.
 *
 * Send requests to camera reprocess module to process.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_image_reproc_context_process (GstCameraImageReprocContext *context,
                                         guint inbufnum,
                                         GstBuffer *inbuf[OFFLINE_CAMERA_INPUT_IMAGE_MAX_NUM],
                                         GstBuffer *outbuf);

/**
 * gst_camera_image_reproc_context_destroy
 * @context: The pointer point to GstCameraImageReprocContext.
 *
 * Destroy camera reprocess module session.
 *
 * Return: TRUE on success or FALSE on failure.
 */
gboolean
gst_camera_image_reproc_context_destroy (GstCameraImageReprocContext *context);

/**
 * gst_camera_image_reproc_context_free
 * @context: The pointer point to GstCameraImageReprocContext.
 *
 * Free structure of GstCameraImageReprocContext.
 *
 * Return: None.
 */
void
gst_camera_image_reproc_context_free (GstCameraImageReprocContext *context);

/**
 * gst_camera_image_reproc_context_update
 * @context: The pointer point to GstCameraImageReprocContext.
 * @idx: Sinkpad index.
 * @camera_id: camera id number.
 * @req_meta_path: Request metadata path.
 * @req_meta_step: Request metadata step.
 * @eis: Electronic Image Stabilization.
 *
 * Update the Pad parameters to GstCameraImageReprocContext.
 *
 * Return: None.
 */
void
gst_camera_image_reproc_context_update (GstCameraImageReprocContext *context,
                                        guint idx, guint camera_id,
                                        gchar *req_meta_path,
                                        guint req_meta_step,
                                        GstCameraImageReprocEis eis);

G_END_DECLS

#endif // __GST_CAMERA_IMAGE_REPROCESS_CONTEXT_H__
