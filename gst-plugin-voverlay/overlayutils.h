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

#ifndef __GST_QTI_OVERLAY_UTILS_H__
#define __GST_QTI_OVERLAY_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/cv/gstcvmeta.h>

G_BEGIN_DECLS

#define GST_GPOINTER_CAST(obj)          ((gpointer)(obj))
#define GST_VIDEO_ROI_META_CAST(obj)    ((GstVideoRegionOfInterestMeta *)(obj))
#define GST_OVERLAY_BBOX_CAST(obj)      ((GstOverlayBBox *)(obj))
#define GST_OVERLAY_TIMESTAMP_CAST(obj) ((GstOverlayTimestamp *)(obj))
#define GST_OVERLAY_STRING_CAST(obj)    ((GstOverlayString *)(obj))
#define GST_OVERLAY_IMAGE_CAST(obj)     ((GstOverlayImage *)(obj))
#define GST_OVERLAY_MASK_CAST(obj)      ((GstOverlayMask *)(obj))

typedef struct _GstOverlayPosition GstOverlayPosition;
typedef struct _GstOverlayTimestamp GstOverlayTimestamp;
typedef struct _GstOverlayBBox GstOverlayBBox;
typedef struct _GstOverlayString GstOverlayString;
typedef struct _GstOverlayImage GstOverlayImage;
typedef struct _GstOverlayMask GstOverlayMask;

enum
{
  GST_OVERLAY_TYPE_BBOX,
  GST_OVERLAY_TYPE_TIMESTAMP,
  GST_OVERLAY_TYPE_STRING,
  GST_OVERLAY_TYPE_MASK,
  GST_OVERLAY_TYPE_IMAGE,
  GST_OVERLAY_TYPE_DETECTION,
  GST_OVERLAY_TYPE_CLASSIFICATION,
  GST_OVERLAY_TYPE_POSE_ESTIMATION,
  GST_OVERLAY_TYPE_OPTCLFLOW,
  GST_OVERLAY_TYPE_MAX
};

enum
{
  GST_OVERLAY_TIMESTAMP_DATE_TIME,
  GST_OVERLAY_TIMESTAMP_PTS_DTS,
};

enum
{
  GST_OVERLAY_MASK_CIRCLE,
  GST_OVERLAY_MASK_RECTANGLE,
};

struct _GstOverlayPosition {
  gint x;
  gint y;
};

struct _GstOverlayBBox {
  GQuark            name;
  GstVideoRectangle destination;
  gint              color;
  gboolean          enable;
};

struct _GstOverlayTimestamp {
  gint               type;
  gchar              *format;
  gint               fontsize;
  GstOverlayPosition position;
  gint               color;
  gboolean           enable;
};

struct _GstOverlayString {
  GQuark             name;
  gchar              *contents;
  gint               fontsize;
  GstOverlayPosition position;
  gint               color;
  gboolean           enable;
};

struct _GstOverlayMask {
  GQuark             name;
  gint               type;
  union {
    gint             radius;
    gint             wh[2];
  } dims;
  GstOverlayPosition position;
  gint               color;
  gboolean           enable;
};

struct _GstOverlayImage {
  GQuark            name;
  gchar             *path;
  gint              width;
  gint              height;
  GstVideoRectangle destination;
  gchar             *contents;
  gboolean          enable;
};

void
gst_overlay_timestamp_free (GstOverlayTimestamp * timestamp);

void
gst_overlay_string_free (GstOverlayString * string);

void
gst_overlay_image_free (GstOverlayImage * simage);

guint
gst_meta_overlay_type (GstMeta * meta);

gboolean
gst_parse_property_value (const gchar * input, GValue * value);

gboolean
gst_extract_bboxes (const GValue * value, GArray * bboxes);

gboolean
gst_extract_timestamps (const GValue * value, GArray * timestamps);

gboolean
gst_extract_strings (const GValue * value, GArray * strings);

gboolean
gst_extract_masks (const GValue * value, GArray * masks);

gboolean
gst_extract_static_images (const GValue * value, GArray * images);

gchar *
gst_serialize_bboxes (GArray * bboxes);

gchar *
gst_serialize_timestamps (GArray * timestamps);

gchar *
gst_serialize_strings (GArray * paragraphs);

gchar *
gst_serialize_masks (GArray * masks);

gchar *
gst_serialize_static_images (GArray * simages);

G_END_DECLS

#endif // __GST_QTI_OVERLAY_UTILS_H__
