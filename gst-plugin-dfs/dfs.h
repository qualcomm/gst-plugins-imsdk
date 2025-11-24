/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_DFS_H__
#define __GST_QTI_DFS_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "dfs-engine.h"


G_BEGIN_DECLS

GType gst_dfs_mode_get_type (void);
GType gst_dfs_pplevel_get_type (void);

#define GST_TYPE_DFS (gst_dfs_get_type())
#define GST_DFS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DFS, GstDfs))
#define GST_DFS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DFS, GstDfsClass))
#define GST_IS_DFS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DFS))
#define GST_IS_DFS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DFS))
#define GST_DFS_CAST(obj) ((GstDfs *)(obj))

#define GST_TYPE_DFS_MODE (gst_dfs_mode_get_type())
#define GST_TYPE_DFS_PPLEVEL (gst_dfs_pplevel_get_type())

typedef struct _GstDfs GstDfs;
typedef struct _GstDfsClass GstDfsClass;

struct _GstDfs {
  GstBaseTransform         parent;

  GstVideoInfo            *ininfo;

  GstBufferPool           *outpool;

  GstDfsEngine            *engine;

  GstVideoFormat          format;

  gchar                   *config_location;

  OutputMode              output_mode;

  stereoConfiguration     stereo_parameter;

  DFSMode                 dfs_mode;

  gint                    min_disparity;

  gint                    num_disparity_levels;

  gint                    filter_width;

  gint                    filter_height;

  gboolean                rectification;

  gboolean                gpu_rect;

  DFSPPLevel              pplevel;
};

struct _GstDfsClass {
    GstBaseTransformClass parent;
};

G_END_DECLS

#endif // __GST_QTI_DFS_H__
