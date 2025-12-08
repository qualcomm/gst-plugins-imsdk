/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_HEXAGON_H__
#define __GST_QTI_HEXAGON_H__

#include "hexagon-module.h"

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_HEXAGON (gst_hexagon_get_type())
#define GST_HEXAGON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HEXAGON, \
                              GstHexagon))
#define GST_HEXAGON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HEXAGON, \
                           GstHexagonClass))
#define GST_IS_HEXAGON(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HEXAGON))
#define GST_IS_HEXAGON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HEXAGON))
#define GST_HEXAGON_CAST(obj) ((GstHexagon *)(obj))

typedef struct _GstHexagon GstHexagon;
typedef struct _GstHexagonClass GstHexagonClass;

struct _GstHexagon {
  GstBaseTransform     parent;

  GstHexagonModule     *module;

  GstBufferPool        *outpool;

  /// Properties.
  gint                 mdlenum;
};

struct _GstHexagonClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_hexagon_get_type (void);

G_END_DECLS

#endif // __GST_QTI_HEXAGON_H__
