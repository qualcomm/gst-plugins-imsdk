/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_RESTRICTED_ZONE_H__
#define __GST_QTI_RESTRICTED_ZONE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "restricted-zone-engine.h"

G_BEGIN_DECLS

#define GST_TYPE_RESTRICTED_ZONE  (gst_restricted_zone_get_type())
#define GST_RESTRICTED_ZONE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RESTRICTED_ZONE,GstRestrictedZone))
#define GST_RESTRICTED_ZONE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RESTRICTED_ZONE,GstRestrictedZoneClass))
#define GST_IS_RESTRICTED_ZONE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RESTRICTED_ZONE))
#define GST_IS_RESTRICTED_ZONE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RESTRICTED_ZONE))
#define GST_RESTRICTED_ZONE_CAST(obj)       ((GstRestrictedZone *)(obj))

typedef struct _GstRestrictedZone GstRestrictedZone;
typedef struct _GstRestrictedZoneClass GstRestrictedZoneClass;

struct _GstRestrictedZone {
  GstBaseTransform       parent;

  /// processing engine.
  GstRestrictedZoneEngine *engine;

  /// Properties.
  GstStructure           *config;
};

struct _GstRestrictedZoneClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_restricted_zone_get_type (void);

G_END_DECLS

#endif // __GST_QTI_RESTRICTED_ZONE_H__
