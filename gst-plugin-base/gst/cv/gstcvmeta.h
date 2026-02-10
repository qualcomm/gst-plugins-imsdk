/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
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
 * @mvectors: A #GArray of #GstCvMotionVector data.
 * @stats:  A #GArray of #GstCvOptclFlowStats data.
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

/**
 * gst_buffer_add_cv_optclflow_meta:
 * @buffer: A #GstBuffer
 * @mvectors: (transfer full) (element-type GstCvMotionVector):
 *            A #GArray containing GstCvMotionVector values
 * @stats: (transfer full) (element-type GstCvOptclFlowMeta):
 *         A #GArray containing GstCvOptclFlowMeta values
 *
 * Attaches GstCvOptclFlowMeta metadata to @buffer with the given parameters.
 *
 * Returns: (transfer none): The #GstCvOptclFlowMeta on @buffer.
 */
GST_API GstCvOptclFlowMeta *
gst_buffer_add_cv_optclflow_meta (GstBuffer * buffer, GArray * mvectors,
                                  GArray * stats);

/**
 * gst_buffer_get_cv_optclflow_meta:
 * @buffer: A #GstBuffer
 *
 * Find the #GstCvOptclFlowMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstCvOptclFlowMeta metadata items.
 *
 * Returns: (transfer none) (nullable): The #GstCvOptclFlowMeta with lowest id
 *          (usually 0) or %NULL when there is no such metadata on @buffer.
 */
GST_API GstCvOptclFlowMeta *
gst_buffer_get_cv_optclflow_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_cv_optclflow_meta_id:
 * @buffer: A #GstBuffer
 * @id: a metadata id
 *
 * Find the #GstCvOptclFlowMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstCvOptclFlowMeta metadata items.
 *
 * Returns: (transfer none) (nullable): The #GstCvOptclFlowMeta with @id or
 *          %NULL when there is no such metadata on @buffer.
 */
GST_API GstCvOptclFlowMeta *
gst_buffer_get_cv_optclflow_meta_id (GstBuffer * buffer, guint id);

G_END_DECLS

#endif /* __GST_CV_META_H__ */
