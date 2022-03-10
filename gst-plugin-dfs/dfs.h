/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_QTI_DFS_H__
#define __GST_QTI_DFS_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "dfs-engine.h"


G_BEGIN_DECLS

GType gst_dfs_mode_get_type (void);

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

typedef struct _GstDfs GstDfs;
typedef struct _GstDfsClass GstDfsClass;

struct _GstDfs {
  GstBaseTransform         parent;

  GstVideoInfo            *ininfo;

  GstBufferPool           *outpool;

  GstDfsEngine            *engine;

  GstVideoFormat          format;

  gchar                   *config_location;

  stereoConfiguration     stereo_parameter;

  DFSMode                 dfs_mode;

  gint                    min_disparity;

  gint                    num_disparity_levels;

  gint                    filter_width;

  gint                    filter_height;

  gboolean                rectification;

  gboolean                gpu_rect;
};

struct _GstDfsClass {
    GstBaseTransformClass parent;
};

G_END_DECLS

#endif // __GST_QTI_DFS_H__