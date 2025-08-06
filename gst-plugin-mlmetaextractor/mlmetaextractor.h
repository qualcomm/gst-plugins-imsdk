/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_MLMETA_EXTRACTOR_H__
#define __GST_QTI_MLMETA_EXTRACTOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_MLMETA_EXTRACTOR (gst_mlmeta_extractor_get_type())
#define GST_MLMETA_EXTRACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MLMETA_EXTRACTOR,GstMLMetaExtractor))
#define GST_MLMETA_EXTRACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MLMETA_EXTRACTOR,GstMLMetaExtractorClass))
#define GST_IS_MLMETA_EXTRACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MLMETA_EXTRACTOR))
#define GST_IS_MLMETA_EXTRACTOR_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MLMETA_EXTRACTOR))
#define GST_MLMETA_EXTRACTOR_CAST(obj)     ((GstMLMetaExtractor *)(obj))

#define GST_MLMETA_EXTRACTOR_GET_LOCK(obj)   (&GST_MLMETA_EXTRACTOR(obj)->lock)
#define GST_MLMETA_EXTRACTOR_LOCK(obj)       g_mutex_lock(GST_MLMETA_EXTRACTOR_GET_LOCK(obj))
#define GST_MLMETA_EXTRACTOR_UNLOCK(obj)     g_mutex_unlock(GST_MLMETA_EXTRACTOR_GET_LOCK(obj))

typedef struct _GstMLMetaExtractor GstMLMetaExtractor;
typedef struct _GstMLMetaExtractorClass GstMLMetaExtractorClass;

struct _GstMLMetaExtractor
{
  /// Inherited parent structure.
  GstBaseTransform         parent;

  /// Buffer pools.
  GstBufferPool            *outpool;

  /// Global mutex lock.
  GMutex                   lock;

  /// HashTables containing Lists of metas, related by parent_id.
  GHashTable               *classmetas;
  GHashTable               *ldmrkmetas;
  GHashTable               *roimetas;

  /// Local reference to video info.
  GstVideoInfo             vinfo;
};

struct _GstMLMetaExtractorClass {
  /// Inherited parent structure.
  GstBaseTransformClass parent;
};

GType gst_mlmeta_extractor_get_type (void);

G_END_DECLS

#endif // __GST_QTI_MLMETA_EXTRACTOR_H__
