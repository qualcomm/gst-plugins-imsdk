/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_CV_META_H__
#define __GST_CV_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_CV_OPTCLFLOW_META_API_TYPE (gst_cv_optclflow_meta_api_get_type())
#define GST_CV_OPTCLFLOW_META_INFO  (gst_cv_optclflow_meta_get_info())

#define GST_CV_OPTCLFLOW_META_CAST(obj) ((GstCvOptclFlowMeta *) obj)

typedef struct _GstCvMotionVector GstCvMotionVector;
typedef struct _GstCvOptclFlowStats GstCvOptclFlowStats;
typedef struct _GstCvOptclFlowMeta GstCvOptclFlowMeta;

/**
 * GstCvMotionVector:
 * @x: Signed origin coordinate on the X axis.
 * @y: Signed origin coordinate on the Y axis
 * @dx: Signed deviation from the origin coordinate on the X axis.
 * @dy: Signed deviation from the origin coordinate on the Y axis..
 * @confidence: Motion vector confidence.
 *
 * Structure representing CV Motion Vector for a macro block.
 */
struct _GstCvMotionVector {
  gint16 x;
  gint16 y;
  gint16 dx;
  gint16 dy;
  gint8  confidence;
};

/**
 * GstCvOptclFlowStats:
 * @variance: Macro block variance.
 * @mean: Macro block mean.
 * @sad: SAD (Sum of Absolute Differences) of the (0,0) motion vectors.
 *
 * Structure representing CV Optical Flow statistics for a macro block.
 */
struct _GstCvOptclFlowStats {
  guint16 variance;
  guint8  mean;
  guint16 sad;
};

/**
 * GstCvOptclFlowMeta:
 * @meta: Parent #GstMeta
 * @id: ID corresponding to the memory index inside GstBuffer.
 * @mvectors: Array containing GstCvMotionVector data.
 * @stats: Array containing GstCvOptclFlowStats data.
 *
 * Extra buffer metadata describing CV Optical Flow properties
 */
struct _GstCvOptclFlowMeta {
  GstMeta meta;

  guint   id;

  GArray  *mvectors;
  GArray  *stats;
};

GST_API GType gst_cv_optclflow_meta_api_get_type (void);

GST_API const GstMetaInfo * gst_cv_optclflow_meta_get_info (void);

GST_API GstCvOptclFlowMeta *
gst_buffer_add_cv_optclflow_meta (GstBuffer * buffer, GArray * mvectors,
                                  GArray * stats);

GST_API GstCvOptclFlowMeta *
gst_buffer_get_cv_optclflow_meta (GstBuffer * buffer);

GST_API GstCvOptclFlowMeta *
gst_buffer_get_cv_optclflow_meta_id (GstBuffer * buffer, guint id);

G_END_DECLS

#endif /* __GST_CV_META_H__ */
