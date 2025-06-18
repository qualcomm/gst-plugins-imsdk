/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_OBJTRACKER_H__
#define __GST_QTI_OBJTRACKER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "objtracker-algo.h"

G_BEGIN_DECLS

#define GST_TYPE_OBJ_TRACKER  (gst_objtracker_get_type())
#define GST_OBJ_TRACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OBJ_TRACKER,GstObjTracker))
#define GST_OBJ_TRACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OBJ_TRACKER,GstObjTrackerClass))
#define GST_IS_OBJ_TRACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OBJ_TRACKER))
#define GST_IS_OBJ_TRACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OBJ_TRACKER))
#define GST_OBJ_TRACKER_CAST(obj)       ((GstObjtracker *)(obj))

typedef struct _GstObjTracker GstObjTracker;
typedef struct _GstObjTrackerClass GstObjTrackerClass;

struct _GstObjTracker {
  GstBaseTransform      parent;

  /// Video object tracker algorithm.
  GstObjTrackerAlgo     *algo;

  ///Properties
  gchar                 *backend;
  GstStructure          *algoparameters;

};

struct _GstObjTrackerClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_objtracker_get_type (void);

G_END_DECLS

#endif // __GST_QTI_OBJTRACKER_H__
