/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __QTI_RESTRICTED_ZONE_ENGINE_H__
#define __QTI_RESTRICTED_ZONE_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstRestrictedZoneEngine GstRestrictedZoneEngine;

GST_API GstRestrictedZoneEngine *
gst_restricted_zone_engine_new (GstStructure * settings);

GST_API void
gst_restricted_zone_engine_free (GstRestrictedZoneEngine * engine);

GST_API gboolean
gst_restricted_zone_engine_process (GstRestrictedZoneEngine * engine, GstBuffer * buffer);

G_END_DECLS

#endif // __GST_QTI_RESTRICTED_ZONE_H__
