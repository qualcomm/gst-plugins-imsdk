/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
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

#ifndef __GST_QTI_ML_VIDEO_CONVERTER_H__
#define __GST_QTI_ML_VIDEO_CONVERTER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/ml/ml-info.h>
#include <gst/video/video-converter-engine.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_VIDEO_CONVERTER (gst_ml_video_converter_get_type())
#define GST_ML_VIDEO_CONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ML_VIDEO_CONVERTER, \
                              GstMLVideoConverter))
#define GST_ML_VIDEO_CONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ML_VIDEO_CONVERTER, \
                           GstMLVideoConverterClass))
#define GST_IS_ML_VIDEO_CONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ML_VIDEO_CONVERTER))
#define GST_IS_ML_VIDEO_CONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ML_VIDEO_CONVERTER))
#define GST_ML_VIDEO_CONVERTER_CAST(obj) ((GstMLVideoConverter *)(obj))

#define GST_TYPE_ML_CONVERSION_MODE    (gst_ml_conversion_mode_get_type())
#define GST_TYPE_ML_VIDEO_DISPOSITION  (gst_ml_video_disposition_get_type())
#define GST_TYPE_ML_VIDEO_PIXEL_LAYOUT (gst_ml_video_pixel_layout_get_type())

typedef struct _GstMLVideoConverter GstMLVideoConverter;
typedef struct _GstMLVideoConverterClass GstMLVideoConverterClass;

typedef enum {
  GST_ML_CONVERSION_MODE_IMAGE_NON_CUMULATIVE,
  GST_ML_CONVERSION_MODE_IMAGE_CUMULATIVE,
  GST_ML_CONVERSION_MODE_ROI_NON_CUMULATIVE,
  GST_ML_CONVERSION_MODE_ROI_CUMULATIVE,
} GstConversionMode;

typedef enum {
  GST_ML_VIDEO_DISPOSITION_TOP_LEFT,
  GST_ML_VIDEO_DISPOSITION_CENTRE,
  GST_ML_VIDEO_DISPOSITION_STRETCH,
} GstVideoDisposition;

typedef enum {
  GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR,
  GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE,
} GstVideoPixelLayout;

struct _GstMLVideoConverter {
  GstBaseTransform     parent;

  /// Input video info.
  GstVideoInfo         *ininfo;

  /// Output ML info.
  GstMLInfo            *mlinfo;
  /// Video info for the converter engine output, created from the ML info.
  GstVideoInfo         *vinfo;

  /// The ID of the previous ML inference stage.
  guint                src_stage_id;
  /// The ID of current ML inference stage.
  guint                stage_id;

  /// Buffer pools.
  GstBufferPool        *outpool;

  /// Queue for managing stashed buffers with not enough ROI / memory.
  GQueue               *bufqueue;

  /// Tracker for the current index in the sequential ROI/memory entries.
  guint                seq_idx;
  /// The total number of ROI/memory entries to batch from the queued buffer.
  guint                n_seq_entries;

  /// Tracker for the current batch position to be filled in the tensor.
  guint                batch_idx;

  /// The next image block in the queued muxed stream buffer to be processed.
  gint                 next_mem_idx;
  /// The ID of next ROI meta in the queued buffer to be processed.
  gint                 next_roi_id;

  /// Video converter engine.
  GstVideoConvEngine   *converter;
  GstVideoComposition  composition;

  /// Properties.
  GstConversionMode    mode;
  GstVideoConvBackend  backend;
  GstVideoDisposition  disposition;
  GstVideoPixelLayout  pixlayout;
  GArray               *mean;
  GArray               *sigma;
};

struct _GstMLVideoConverterClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_video_converter_get_type (void);

G_GNUC_INTERNAL GType gst_ml_conversion_mode_get_type (void);

G_GNUC_INTERNAL GType gst_ml_video_disposition_get_type (void);

G_GNUC_INTERNAL GType gst_ml_video_pixel_layout_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_VIDEO_CONVERTER_H__
