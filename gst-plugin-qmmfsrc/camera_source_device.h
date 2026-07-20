/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifndef __GST_QMMFSRC_DEVICE_H__
#define __GST_QMMFSRC_DEVICE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

// GStreamer device provider type that enumerates the available cameras so
// that gst-device-monitor-1.0 can list them. Registered in the plugin's
// plugin_init() via gst_device_provider_register().
#define GST_TYPE_QMMFSRC_DEVICE_PROVIDER \
    (gst_qmmfsrc_device_provider_get_type ())
G_DECLARE_FINAL_TYPE (GstQmmfSrcDeviceProvider, gst_qmmfsrc_device_provider,
    GST, QMMFSRC_DEVICE_PROVIDER, GstDeviceProvider)

// gst_qmmfsrc_get_num_cameras:
//
// Query the number of cameras reported by the QMMF camera service.
// Establishes a short-lived Recorder connection, fetches the static info
// list and returns its size. Safe to call without a GstQmmfContext.
//
// Returns: number of cameras, or 0 on failure.
GST_API
guint gst_qmmfsrc_get_num_cameras (void);

// gst_qmmfsrc_get_camera_static_caps:
// @camera_id: index of the camera to query.
//
// Build the supported GstCaps (formats, resolution range and framerate)
// for a specific camera, parsed from that camera's static metadata.
// Establishes a short-lived Recorder connection. Safe to call without a
// GstQmmfContext.
//
// Returns: (transfer full): newly allocated GstCaps, or NULL on failure.
// Caller owns the reference.
GST_API
GstCaps * gst_qmmfsrc_get_camera_static_caps (guint camera_id);

G_END_DECLS

#endif // __GST_QMMFSRC_DEVICE_H__
