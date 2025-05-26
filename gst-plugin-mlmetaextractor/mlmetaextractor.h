/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_MLMETA_EXTRACTOR_H__
#define __GST_QTI_MLMETA_EXTRACTOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "mlmetaextractorpads.h"

G_BEGIN_DECLS

#define GST_TYPE_MLMETA_EXTRACTOR (gst_mlmeta_extractor_get_type())
#define GST_MLMETA_EXTRACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MLMETA_EXTRACTOR,GstMlMetaExtractor))
#define GST_MLMETA_EXTRACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MLMETA_EXTRACTOR,GstMlMetaExtractorClass))
#define GST_IS_MLMETA_EXTRACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MLMETA_EXTRACTOR))
#define GST_IS_MLMETA_EXTRACTOR_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MLMETA_EXTRACTOR))

#define GST_MLMETA_EXTRACTOR_GET_LOCK(obj)   (&GST_MLMETA_EXTRACTOR(obj)->lock)
#define GST_MLMETA_EXTRACTOR_LOCK(obj)       g_mutex_lock(GST_MLMETA_EXTRACTOR_GET_LOCK(obj))
#define GST_MLMETA_EXTRACTOR_UNLOCK(obj)     g_mutex_unlock(GST_MLMETA_EXTRACTOR_GET_LOCK(obj))

typedef struct _GstMlMetaExtractorParams GstMlMetaExtractorParams;
typedef struct _GstMlMetaExtractor GstMlMetaExtractor;
typedef struct _GstMlMetaExtractorClass GstMlMetaExtractorClass;

struct _GstMlMetaExtractorParams
{
  GstMlMetaExtractor *extractor;
  GValue             *output_list;
  GstBuffer          *inbuffer;
  GList              *metalist;

  gint               process_type;
  gint               n_entries;
  gint               *seq_index;
  gint               parent_id;
};

struct _GstMlMetaExtractor
{
  /// Inherited parent structure.
  GstElement         parent;

  /// Global mutex lock.
  GMutex             lock;
  /// Segment.
  GstSegment         segment;

  /// Convenient local reference to source video pad.
  GstMlMetaExtractorSrcPad *vpad;
  /// Convenient local reference to source meta pad.
  GstMlMetaExtractorSrcPad *metapad;

  /// HashTables containing Lists of metas, related by parent_id.
  GHashTable         *classmetas;
  GHashTable         *ldmrkmetas;
  GHashTable         *roimetas;

  /// Local reference to video info.
  GstVideoInfo       vinfo;
};

struct _GstMlMetaExtractorClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_mlmeta_extractor_get_type (void);

G_END_DECLS

#endif // __GST_QTI_MLMETA_EXTRACTOR_H__
