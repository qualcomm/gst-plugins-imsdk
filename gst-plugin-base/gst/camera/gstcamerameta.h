/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef GST_CAMERA_META_H
#define GST_CAMERA_META_H

#include <gst/gst.h>
#include "system/camera_metadata.h"

G_BEGIN_DECLS

typedef struct _GstCameraMeta {
    GstMeta meta;
    camera_metadata_t *metadata;
} GstCameraMeta;

#define GST_CAMERA_META_API_TYPE (gst_camera_meta_api_get_type())
#define GST_CAMERA_META_INFO (gst_camera_meta_get_info())

GType gst_camera_meta_api_get_type (void);
const GstMetaInfo *gst_camera_meta_get_info (void);

GstCameraMeta *gst_buffer_add_camera_meta (GstBuffer *buffer, struct camera_metadata *metadata);
GstCameraMeta *gst_buffer_get_camera_meta (GstBuffer *buffer);

G_END_DECLS

#endif // GST_CAMERA_META_H
