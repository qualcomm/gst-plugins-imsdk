/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_META_TRANSFORM_H__
#define __GST_QTI_META_TRANSFORM_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "meta-transform-module.h"

G_BEGIN_DECLS

#define GST_TYPE_META_TRANSFORM  (gst_meta_transform_get_type())
#define GST_META_TRANSFORM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_META_TRANSFORM,GstMetaTransform))
#define GST_META_TRANSFORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_META_TRANSFORM,GstMetaTransformClass))
#define GST_IS_META_TRANSFORM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_META_TRANSFORM))
#define GST_IS_META_TRANSFORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_META_TRANSFORM))
#define GST_META_TRANSFORM_CAST(obj)       ((GstMetaTransform *)(obj))

typedef struct _GstMetaTransform GstMetaTransform;
typedef struct _GstMetaTransformClass GstMetaTransformClass;

struct _GstMetaTransform {
  GstBaseTransform       parent;

  /// Meta processing module.
  GstMetaTransformModule *module;

  /// Properties.
  gint                   backend;
  GstStructure           *params;
};

struct _GstMetaTransformClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_meta_transform_get_type (void);

G_END_DECLS

#endif // __GST_QTI_META_TRANSFORM_H__
