/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "videosplitpads.h"

#include <gst/video/gstqtibufferpool.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

G_DEFINE_TYPE(GstVideoSplitSinkPad, gst_video_split_sinkpad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstVideoSplitSrcPad, gst_video_split_srcpad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_EXTERN (gst_video_split_debug);
#define GST_CAT_DEFAULT gst_video_split_debug

#define GST_TYPE_VIDEO_SPLIT_MODE   (gst_video_split_mode_get_type())

#define DEFAULT_PROP_MODE           GST_VSPLIT_MODE_NONE
#define DEFAULT_PROP_MIN_BUFFERS    2
#define DEFAULT_PROP_MAX_BUFFERS    20
#define GST_VSPLIT_MAX_QUEUE_LEN    16

enum
{
  PROP_0,
  PROP_MODE,
};

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  GstPad *pad = GST_PAD (checkdata);

  if (GST_IS_VIDEO_SPLIT_SRCPAD (pad)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD_CAST (pad);
    GST_VIDEO_SPLIT_PAD_SIGNAL_IDLE (srcpad, FALSE);
  } else if (GST_IS_VIDEO_SPLIT_SINKPAD (pad)) {
    GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD_CAST (pad);
    GST_VIDEO_SPLIT_PAD_SIGNAL_IDLE (sinkpad, FALSE);
  }

  return (visible >= GST_VSPLIT_MAX_QUEUE_LEN) ? TRUE : FALSE;
}

static void
queue_empty_cb (GstDataQueue * queue, gpointer checkdata)
{
  GstPad *pad = GST_PAD (checkdata);

  if (GST_IS_VIDEO_SPLIT_SRCPAD (pad)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD_CAST (pad);
    GST_VIDEO_SPLIT_PAD_SIGNAL_IDLE (srcpad, TRUE);
  } else if (GST_IS_VIDEO_SPLIT_SINKPAD (pad)) {
    GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD_CAST (pad);
    GST_VIDEO_SPLIT_PAD_SIGNAL_IDLE (sinkpad, TRUE);
  }
}

static GType
gst_video_split_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue methods[] = {
    { GST_VSPLIT_MODE_NONE,
        "Incoming buffer is rescaled and color converted in order to match the "
        "negotiated pad caps. If the input and output caps match then the "
        "input buffer will be propagated directly to the output and its "
        "reference count increased.", "none"
    },
    { GST_VSPLIT_MODE_FORCE_TRANSFORM,
        "Incoming buffer is rescaled and color converted in order to match the "
        "negotiated pad caps. New buffer is produced even if the negotiated "
        "input and output caps match.", "force-transform"
    },
    { GST_VSPLIT_MODE_ROI_SINGLE,
        "Incoming buffer is checked for ROI meta. If there is a meta entry that "
        "corresponds to this pad a crop, rescale and color conversion operations "
        "are performed on the input buffer. The thus transformed buffer is sent "
        "to the next plugin. Pad with no corresponding ROI meta will produce "
        "GAP buffer.", "single-roi-meta"
    },
    { GST_VSPLIT_MODE_ROI_BATCH,
        "Incoming buffer is checked for ROI meta. For each meta entry a crop, "
        "rescale and color conversion are performed on the input buffer. Thus "
        "for each ROI meta entry a buffer will be produced and sent to the "
        "next plugin downstream. In case no ROI meta is present the pad will "
        "produce GAP buffer.", "batch-roi-meta"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstVideoSplitMode", methods);

  return gtype;
}

GstBufferPool *
gst_video_split_create_pool (GstPad * pad, GstCaps * caps,
    GstVideoAlignment * align, GstAllocationParams * params)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if (gst_gbm_qcom_backend_is_supported ()) {
    // If downstream allocation query supports GBM, allocate gbm memory.
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (pad, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (pad, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    config = gst_buffer_pool_get_config (pool);
    allocator = gst_fd_allocator_new ();

  } else {
    pool = gst_qti_buffer_pool_new ();
    config = gst_buffer_pool_get_config (pool);

    gst_video_info_align (&info, align);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, align);

    allocator = gst_qti_allocator_new ();
    if (allocator == NULL) {
      GST_ERROR_OBJECT (pad, "Failed to create QTI allocator");
      gst_clear_object (&pool);
      return NULL;
    }
  }

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);
  gst_buffer_pool_config_set_allocator (config, allocator, params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (pad, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  g_object_unref (allocator);
  return pool;
}

static void
gst_video_split_score_format (GstPad * pad, const GstVideoFormatInfo * ininfo,
    const GValue * value, gint * score, const GstVideoFormatInfo ** outinfo)
{
  const GstVideoFormatInfo *info;
  GstVideoFormat format;
  gint l_score = 0;

  format = gst_video_format_from_string (g_value_get_string (value));
  info = gst_video_format_get_info (format);

  // Same formats, increase the score.
  l_score += (GST_VIDEO_FORMAT_INFO_FORMAT (ininfo) ==
      GST_VIDEO_FORMAT_INFO_FORMAT (info)) ? 1 : 0;

  // Same base format conversion, increase the score.
  l_score += GST_VIDEO_FORMAT_INFO_IS_YUV (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_YUV (info) ? 1 : 0;
  l_score += GST_VIDEO_FORMAT_INFO_IS_RGB (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_RGB (info) ? 1 : 0;
  l_score += GST_VIDEO_FORMAT_INFO_IS_GRAY (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_GRAY (info) ? 1 : 0;

  // Both formats have aplha channels, increase the score.
  l_score += GST_VIDEO_FORMAT_INFO_HAS_ALPHA (ininfo) &&
      GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info) ? 1 : 0;

  // Loss of color, decrease the score.
  l_score -= !(GST_VIDEO_FORMAT_INFO_IS_GRAY (ininfo)) &&
      GST_VIDEO_FORMAT_INFO_IS_GRAY (info) ? 1 : 0;

  // Loss of alpha channel, decrease the score.
  l_score -= GST_VIDEO_FORMAT_INFO_HAS_ALPHA (ininfo) &&
      !(GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info)) ? 1 : 0;

  GST_DEBUG_OBJECT (pad, "Score %s -> %s = %d",
      GST_VIDEO_FORMAT_INFO_NAME (ininfo),
      GST_VIDEO_FORMAT_INFO_NAME (info), l_score);

  if (l_score > *score) {
    GST_DEBUG_OBJECT (pad, "Found new best score %d (%s)", l_score,
        GST_VIDEO_FORMAT_INFO_NAME (info));
    *outinfo = info;
    *score = l_score;
  }
}

static void
gst_video_split_fixate_format (GstPad * pad, GstStructure * input,
    GstStructure * output)
{
  const GstVideoFormatInfo *ininfo, *outinfo = NULL;
  const GValue *format = NULL, *value = NULL;
  gint idx, length, score = G_MININT;
  const gchar *infmt = NULL;
  gboolean sametype = FALSE;

  infmt = gst_structure_get_string (input, "format");
  g_return_if_fail (infmt != NULL);

  GST_DEBUG_OBJECT (pad, "Source format %s", infmt);

  ininfo = gst_video_format_get_info (gst_video_format_from_string (infmt));
  g_return_if_fail (ininfo != NULL);

  format = gst_structure_get_value (output, "format");
  g_return_if_fail (format != NULL);

  if (GST_VALUE_HOLDS_LIST (format)) {
    length = gst_value_list_get_size (format);

    GST_DEBUG_OBJECT (pad, "Have %u formats", length);

    for (idx = 0; idx < length; idx++) {
      value = gst_value_list_get_value (format, idx);

      if (G_VALUE_HOLDS_STRING (value)) {
        gst_video_split_score_format (pad, ininfo, value, &score, &outinfo);
      } else {
        GST_WARNING_OBJECT (pad, "Format value has invalid type!");
      }
    }
  } else if (G_VALUE_HOLDS_STRING (format)) {
    gst_video_split_score_format (pad, ininfo, format, &score, &outinfo);
  } else {
    GST_WARNING_OBJECT (pad, "Format field has invalid type!");
  }

  if (outinfo != NULL)
    gst_structure_fixate_field_string (output, "format",
        GST_VIDEO_FORMAT_INFO_NAME (outinfo));

  sametype |= GST_VIDEO_FORMAT_INFO_IS_YUV (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_YUV (outinfo);
  sametype |= GST_VIDEO_FORMAT_INFO_IS_RGB (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_RGB (outinfo);
  sametype |= GST_VIDEO_FORMAT_INFO_IS_GRAY (ininfo) &&
      GST_VIDEO_FORMAT_INFO_IS_GRAY (outinfo);

  if (gst_structure_has_field (input, "colorimetry") && sametype) {
    const gchar *string = gst_structure_get_string (input, "colorimetry");

    if (gst_structure_has_field (output, "colorimetry"))
      gst_structure_fixate_field_string (output, "colorimetry", string);
    else
      gst_structure_set (output, "colorimetry", G_TYPE_STRING, string, NULL);
  }

  if (gst_structure_has_field (input, "chroma-site") && sametype) {
    const gchar *string = gst_structure_get_string (input, "chroma-site");

    if (gst_structure_has_field (output, "chroma-site"))
      gst_structure_fixate_field_string (output, "chroma-site", string);
    else
      gst_structure_set (output, "chroma-site", G_TYPE_STRING, string, NULL);
  }

  if (gst_structure_has_field (input, "compression") && sametype) {
    const gchar *string = gst_structure_get_string (input, "compression");

    if (gst_structure_has_field (output, "compression"))
      gst_structure_fixate_field_string (output, "compression", string);
    else
      gst_structure_set (output, "compression", G_TYPE_STRING, string, NULL);
  }
}

static gboolean
gst_video_split_fixate_pixel_aspect_ratio (GstPad * pad, GstStructure * input,
    GstStructure * output, gint out_width, gint out_height)
{
  gint in_width = 0, in_height = 0, in_par_n = 1, in_par_d = 1;
  guint out_par_n = 1, out_par_d = 1;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (pad, "Output dimensions fixed to: %dx%d",
      out_width, out_height);

  {
    // Retrieve the output PAR (pixel aspect ratio) value.
    const GValue *par = gst_structure_get_value (output, "pixel-aspect-ratio");

    if ((par != NULL) && gst_value_is_fixed (par)) {
      out_par_n = gst_value_get_fraction_numerator (par);
      out_par_d = gst_value_get_fraction_denominator (par);

      GST_DEBUG_OBJECT (pad, "Output PAR is fixed to: %d/%d",
          out_par_n, out_par_d);
      return TRUE;
    }
  }

  {
    // Retrieve the input PAR (pixel aspect ratio) value.
    const GValue *par = gst_structure_get_value (input, "pixel-aspect-ratio");

    if (par != NULL) {
      in_par_n = gst_value_get_fraction_numerator (par);
      in_par_d = gst_value_get_fraction_denominator (par);
    }
  }

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  success = gst_video_calculate_display_ratio (&out_par_n, &out_par_d,
      in_width, in_height, in_par_n, in_par_d, out_width, out_height);

  if (success) {
    GST_DEBUG_OBJECT (pad, "Fixating output PAR to %d/%d",
        out_par_n, out_par_d);

    gst_structure_fixate_field_nearest_fraction (output,
        "pixel-aspect-ratio", out_par_n, out_par_d);
  }

  return TRUE;
}

static gboolean
gst_video_split_fixate_width (GstPad * pad, GstStructure * input,
    GstStructure * output, gint out_height)
{
  const GValue *in_par, *out_par;
  gint in_par_n = 1, in_par_d = 1, in_dar_n = 0, in_dar_d = 0;
  gint in_width = 0, in_height = 0;
  gboolean success;

  GST_DEBUG_OBJECT (pad, "Output height is fixed to: %d", out_height);

  // Retrieve the PAR (pixel aspect ratio) values for the input and output.
  in_par = gst_structure_get_value (input, "pixel-aspect-ratio");
  out_par = gst_structure_get_value (output, "pixel-aspect-ratio");

  if (in_par != NULL) {
    in_par_n = gst_value_get_fraction_numerator (in_par);
    in_par_d = gst_value_get_fraction_denominator (in_par);
  }

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to calculate the input DAR!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (pad, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  // PAR is fixed, choose width that is nearest to the width with the same DAR.
  if ((out_par != NULL) && gst_value_is_fixed (out_par)) {
    gint out_par_n = 1, out_par_d = 1, num = 0, den = 0, out_width = 0;

    out_par_d = gst_value_get_fraction_denominator (out_par);
    out_par_n = gst_value_get_fraction_numerator (out_par);

    GST_DEBUG_OBJECT (pad, "Output PAR fixed to: %d/%d",
        out_par_n, out_par_d);

    // Calculate width scale factor from input DAR and output PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        out_par_d, out_par_n, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate input width scale factor!");
      return FALSE;
    }

     out_width = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_height, num, den));

    gst_structure_fixate_field_nearest_int (output, "width", out_width);
    gst_structure_get_int (output, "width", &out_width);

    GST_DEBUG_OBJECT (pad, "Output width fixated to: %d", out_width);
  } else {
    // PAR is not fixed, try to keep the input DAR and PAR.
    GstStructure *structure = gst_structure_copy (output);
    gint out_par_n = 1, out_par_d = 1, set_par_n = 1, set_par_d = 1;
    gint num = 0, den = 0, out_width = 0;

    // Calculate output width scale factor from input DAR and PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        in_par_n, in_par_d, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output width scale factor!");
      gst_structure_free (structure);
      return FALSE;
    }

    // Scale the output width to a value nearest to the input with same DAR
    // and adjust the output PAR if needed.
    out_width = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_height, num, den));

    gst_structure_fixate_field_nearest_int (structure, "width", out_width);
    gst_structure_get_int (structure, "width", &out_width);

    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        out_height, out_width, &out_par_n, &out_par_d);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output PAR!");
      gst_structure_free (structure);
      return FALSE;
    }

    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", out_par_n, out_par_d);
    gst_structure_get_fraction (structure, "pixel-aspect-ratio",
        &set_par_n, &set_par_d);

    gst_structure_free (structure);

    // Validate the adjusted output PAR and update the output fields.
    if (set_par_n == out_par_n && set_par_d == out_par_d) {
      gst_structure_set (output, "width", G_TYPE_INT, out_width,
          "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (pad, "Output width fixated to: %d, and PAR fixated"
          " to: %d/%d", out_width, set_par_n, set_par_d);
      return TRUE;
    }

    // The above approach failed, scale the width to the new PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        set_par_d, set_par_n, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output width!");
      return FALSE;
    }

    out_width = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_height, num, den));
    gst_structure_fixate_field_nearest_int (output, "width", out_width);
    gst_structure_get_int (structure, "width", &out_width);

    gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        set_par_n, set_par_d, NULL);

    GST_DEBUG_OBJECT (pad, "Output width fixated to: %d, and PAR fixated"
        " to: %d/%d", out_width, set_par_n, set_par_d);
  }

  return TRUE;
}

static gboolean
gst_video_split_fixate_height (GstPad * pad, GstStructure * input,
    GstStructure * output, gint out_width)
{
  const GValue *in_par, *out_par;
  gint in_par_n = 1, in_par_d = 1, in_dar_n = 0, in_dar_d = 0;
  gint in_width = 0, in_height = 0;
  gboolean success;

  GST_DEBUG_OBJECT (pad, "Output width is fixed to: %d", out_width);

  // Retrieve the PAR (pixel aspect ratio) values for the input and output.
  in_par = gst_structure_get_value (input, "pixel-aspect-ratio");
  out_par = gst_structure_get_value (output, "pixel-aspect-ratio");

  if (in_par != NULL) {
    in_par_n = gst_value_get_fraction_numerator (in_par);
    in_par_d = gst_value_get_fraction_denominator (in_par);
  }

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to calculate input DAR!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (pad, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  // PAR is fixed, choose height that is nearest to the height with the same DAR.
  if ((out_par != NULL) && gst_value_is_fixed (out_par)) {
    gint out_par_n = 1, out_par_d = 1, num = 0, den = 0, out_height = 0;

    out_par_n = gst_value_get_fraction_numerator (out_par);
    out_par_d = gst_value_get_fraction_denominator (out_par);

    GST_DEBUG_OBJECT (pad, "Output PAR fixed to: %d/%d",
        out_par_n, out_par_d);

    // Calculate height from input DAR and output PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        out_par_d, out_par_n, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output width!");
      return FALSE;
    }

    out_height = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_width, den, num));

    gst_structure_fixate_field_nearest_int (output, "height", out_height);
    gst_structure_get_int (output, "height", &out_height);

    GST_DEBUG_OBJECT (pad, "Output height fixated to: %d", out_height);
  } else {
    // PAR is not fixed, try to keep the input DAR and PAR.
    GstStructure *structure = gst_structure_copy (output);
    gint out_par_n = 1, out_par_d = 1, set_par_n = 1, set_par_d = 1;
    gint num = 0, den = 0, out_height = 0;

    // Calculate output width scale factor from input DAR and PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        in_par_n, in_par_d, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output height scale factor!");
      gst_structure_free (structure);
      return FALSE;
    }

    // Scale the output height to a value nearest to the input with same DAR
    // and adjust the output PAR if needed.
    out_height = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_width, den, num));

    gst_structure_fixate_field_nearest_int (structure, "height", out_height);
    gst_structure_get_int (structure, "height", &out_height);

    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        out_height, out_width, &out_par_n, &out_par_d);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output PAR!");
      gst_structure_free (structure);
      return FALSE;
    }

    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", out_par_n, out_par_d);
    gst_structure_get_fraction (structure, "pixel-aspect-ratio",
        &set_par_n, &set_par_d);

    gst_structure_free (structure);

    // Validate the adjusted output PAR and update the output fields.
    if (set_par_n == out_par_n && set_par_d == out_par_d) {
      gst_structure_set (output, "height", G_TYPE_INT, out_height,
          "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);

      GST_DEBUG_OBJECT (pad, "Output height fixated to: %d, and PAR fixated"
          " to: %d/%d", out_height, set_par_n, set_par_d);
      return TRUE;
    }

    // The above approach failed, scale the width to the new PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d,
        set_par_d, set_par_n, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output width!");
      return FALSE;
    }

    out_height = GST_ROUND_UP_4 (gst_util_uint64_scale_int (out_width, den, num));
    gst_structure_fixate_field_nearest_int (output, "height", out_height);
    gst_structure_get_int (output, "height", &out_height);

    gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        set_par_n, set_par_d, NULL);

    GST_DEBUG_OBJECT (pad, "Output height fixated to: %d, and PAR fixated"
        " to: %d/%d", out_height, set_par_n, set_par_d);
  }

  return TRUE;
}

static gboolean
gst_video_split_fixate_width_and_height (GstPad * pad, GstStructure * input,
    GstStructure * output)
{
  const GValue *value = NULL;
  gint in_par_n = 1, in_par_d = 1, out_par_n = 1, out_par_d = 1;
  gint in_dar_n = 0, in_dar_d = 0, in_width = 0, in_height = 0;
  gboolean success;

  // Retrieve the output PAR (pixel aspect ratio) value.
  value = gst_structure_get_value (output, "pixel-aspect-ratio");

  out_par_n = gst_value_get_fraction_numerator (value);
  out_par_d = gst_value_get_fraction_denominator (value);

  GST_DEBUG_OBJECT (pad, "Output PAR is fixed to: %d/%d",
      out_par_n, out_par_d);

  {
    // Retrieve the PAR (pixel aspect ratio) values for the input.
    const GValue *in_par = gst_structure_get_value (input,
        "pixel-aspect-ratio");

    if (in_par != NULL) {
      in_par_n = gst_value_get_fraction_numerator (in_par);
      in_par_d = gst_value_get_fraction_denominator (in_par);
    }
  }

  // Retrieve the input width and height.
  gst_structure_get_int (input, "width", &in_width);
  gst_structure_get_int (input, "height", &in_height);

  // Calculate input DAR (display aspect ratio) from the dimensions and PAR.
  success = gst_util_fraction_multiply (in_width, in_height,
      in_par_n, in_par_d, &in_dar_n, &in_dar_d);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to calculate input DAR!");
    return FALSE;
  }

  GST_DEBUG_OBJECT (pad, "Input DAR is: %d/%d", in_dar_n, in_dar_d);

  {
    GstStructure *structure = gst_structure_copy (output);
    gint out_width, out_height, set_w, set_h, num, den, value;

    // Calculate output dimensions scale factor from input DAR and output PAR.
    success = gst_util_fraction_multiply (in_dar_n, in_dar_d, out_par_n,
        out_par_d, &num, &den);

    if (!success) {
      GST_ERROR_OBJECT (pad, "Failed to calculate output scale factor!");
      gst_structure_free (structure);
      return FALSE;
    }

    // Keep the input height (because of interlacing).
    gst_structure_fixate_field_nearest_int (structure, "height", in_height);
    gst_structure_get_int (structure, "height", &set_h);

    // Scale width in order to keep DAR.
    set_w = GST_ROUND_UP_4 (gst_util_uint64_scale_int (set_h, num, den));

    gst_structure_fixate_field_nearest_int (structure, "width", set_w);
    gst_structure_get_int (structure, "width", &value);

    // We kept the DAR and the height nearest to the original.
    if (set_w == value) {
      gst_structure_set (output, "width", G_TYPE_INT, set_w,
          "height", G_TYPE_INT, set_h, NULL);
      gst_structure_free (structure);

      GST_DEBUG_OBJECT (pad, "Output dimensions fixated to: %dx%d", set_w, set_h);
      return TRUE;
    }

    // Store the values from initial run, they will be used if all else fails.
    out_width = set_w;
    out_height = set_h;

    // Failed to set output width while keeping the input height, try width.
    gst_structure_fixate_field_nearest_int (structure, "width", in_width);
    gst_structure_get_int (structure, "width", &set_w);

    // Scale height in order to keep DAR.
    set_h = GST_ROUND_UP_4 (gst_util_uint64_scale_int (set_w, den, num));

    gst_structure_fixate_field_nearest_int (structure, "height", set_h);
    gst_structure_get_int (structure, "height", &value);

    gst_structure_free (structure);

    // We kept the DAR and the width nearest to the original.
    if (set_h == value) {
      gst_structure_set (output, "width", G_TYPE_INT, set_w,
          "height", G_TYPE_INT, set_h, NULL);

      GST_DEBUG_OBJECT (pad, "Output dimensions fixated to: %dx%d", set_w, set_h);
      return TRUE;
    }

    // All of the above approaches failed, keep the height that was
    // nearest to the original height and the nearest possible width.
    gst_structure_set (output, "width", G_TYPE_INT, out_width,
        "height", G_TYPE_INT, out_height, NULL);

    GST_DEBUG_OBJECT (pad, "Output dimensions fixated to: %dx%d",
        out_width, out_height);
  }

  return TRUE;
}

static gboolean
gst_video_split_fixate_framerate (GstPad * pad, GstStructure * input,
    GstStructure * output)
{
  gint status = GST_VALUE_UNORDERED;

  if (!gst_structure_has_field (output, "framerate")) {
    gst_structure_set_value (output, "framerate",
        gst_structure_get_value (input, "framerate"));
    return TRUE;
  }

  if (!gst_value_is_fixed (gst_structure_get_value (output, "framerate"))) {
    gboolean success = FALSE;
    GValue value = G_VALUE_INIT;

    success = gst_value_intersect (&value,
        gst_structure_get_value (input, "framerate"),
        gst_structure_get_value (output, "framerate"));

    if (success)
      gst_structure_set_value (output, "framerate", &value);
    else
      GST_ERROR_OBJECT (pad, "Input and output framerate do not intersect!");

    g_value_unset (&value);
    return success;
  }

  status = gst_value_compare (gst_structure_get_value (input, "framerate"),
      gst_structure_get_value (output, "framerate"));

  if (status != GST_VALUE_EQUAL) {
    GST_ERROR_OBJECT (pad, "Input and output framerate not equal!");
    return FALSE;
  }

  return TRUE;
}

static void
gst_video_split_sinkpad_finalize (GObject * object)
{
  GstVideoSplitSinkPad *pad = GST_VIDEO_SPLIT_SINKPAD (object);

  gst_data_queue_set_flushing (pad->requests, TRUE);
  gst_data_queue_flush (pad->requests);

  gst_object_unref (GST_OBJECT_CAST(pad->requests));

  if (pad->info != NULL)
    gst_video_info_free (pad->info);

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_video_split_sinkpad_parent_class)->finalize(object);
}

void
gst_video_split_sinkpad_class_init (GstVideoSplitSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_finalize);
}

void
gst_video_split_sinkpad_init (GstVideoSplitSinkPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  pad->is_idle = TRUE;

  pad->info = NULL;

  pad->requests =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);
}

static GstCaps *
gst_video_split_srcpad_fixate_caps (GstVideoSplitSrcPad * srcpad,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstCapsFeatures *features = NULL;
  GstStructure *input = NULL, *output = NULL;
  GstVideoMultiviewMode mviewmode = GST_VIDEO_MULTIVIEW_MODE_MONO;
  GstVideoMultiviewFlags mviewflags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
  gint width = 0, height = 0;
  gboolean success = TRUE;

  // Overwrite the default multiview mode depending on the pad mode.
  if (srcpad->mode == GST_VSPLIT_MODE_ROI_BATCH)
    mviewmode = GST_VIDEO_MULTIVIEW_MODE_MULTIVIEW_FRAME_BY_FRAME;

  // Prefer caps with feature memory:GBM and removeall others.
  if (gst_gbm_qcom_backend_is_supported () &&
      gst_caps_has_feature (outcaps, GST_CAPS_FEATURE_MEMORY_GBM))
    features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL);
  else
    features = gst_caps_features_new_empty ();

  // Trancate and set the prefered features if any.
  outcaps = gst_caps_truncate (outcaps);
  gst_caps_set_features (outcaps, 0, features);

  // Get underlying structure to the only remaining caps.
  output = gst_caps_get_structure (outcaps, 0);

  // Remove compression field if caps do not contain memory:GBM feature.
  if (!gst_caps_has_feature (outcaps, GST_CAPS_FEATURE_MEMORY_GBM))
    gst_structure_remove_field (output, "compression");

  // Take a copy of the input caps structure so we can freely modify it.
  input = gst_caps_get_structure (incaps, 0);
  input = gst_structure_copy (input);

  GST_DEBUG_OBJECT (srcpad, "Trying to fixate output caps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Set multiview related fields based on the operational mode.
  gst_structure_set (output, "multiview-mode", G_TYPE_STRING,
      gst_video_multiview_mode_to_caps_string (mviewmode), "multiview-flags",
      GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, mviewflags, GST_FLAG_SET_MASK_EXACT,
      NULL);

  // Fill default pixel-aspect-ratio field if they wasn't set in the caps.
  if (!gst_structure_has_field (output, "pixel-aspect-ratio"))
    gst_structure_set (output, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

  // First fixate the output format.
  gst_video_split_fixate_format (GST_PAD (srcpad), input, output);

  // Retrieve the output width and height.
  gst_structure_get_int (output, "width", &width);
  gst_structure_get_int (output, "height", &height);

  // Check which values are fixed and take the necessary actions.
  if (width && height) {
    gst_video_split_fixate_pixel_aspect_ratio (GST_PAD (srcpad), input,
        output, width, height);
  } else if (width) {
    // The output width is set, try to calculate output height.
    success &= gst_video_split_fixate_height (GST_PAD (srcpad), input,
        output, width);
  } else if (height) {
    // The output height is set, try to calculate output width.
    success &= gst_video_split_fixate_width (GST_PAD (srcpad), input, output,
        height);
  } else {
    // The output PAR is set, try to calculate the output width and height.
    success &= gst_video_split_fixate_width_and_height (GST_PAD (srcpad),
        input, output);
  }

  // Fixate the output framerate.
  success &= gst_video_split_fixate_framerate (GST_PAD (srcpad), input, output);

  // Free the local copy of the input caps structure.
  gst_structure_free (input);

  if (!success) {
    GST_ERROR_OBJECT (srcpad, "Failed to fixate output caps");
    return NULL;
  }

  // Fixate the remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (srcpad, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_video_split_srcpad_decide_allocation (GstVideoSplitSrcPad * pad,
    GstQuery * query)
{
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, };
  guint size = 0, minbuffers = 0, maxbuffers = 0;
  GstVideoInfo info;
  GstVideoAlignment align = { 0, }, ds_align = { 0, };

  gst_query_parse_allocation (query, &caps, NULL);

  if (NULL == caps) {
    GST_ERROR_OBJECT (pad, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (pad->pool != NULL) {
    gst_buffer_pool_set_active (pad->pool, FALSE);
    gst_clear_object (&pad->pool);
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  gst_video_utils_get_gpu_align (&info, &align);
  gst_query_get_video_alignment (query, &ds_align);
  align = gst_video_calculate_common_alignment (&align, &ds_align);

  if (gst_query_get_n_allocation_params (query))
    gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

  // Create a new buffer pool.
  pool = gst_video_split_create_pool (GST_PAD (pad), caps, &align, &params);
  if (pool == NULL) {
    GST_ERROR_OBJECT (pad, "Failed to create buffer pool!");
    return FALSE;
  }

  pad->pool = pool;
  gst_buffer_pool_set_active (pad->pool, TRUE);

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers,
        maxbuffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

gboolean
gst_video_split_srcpad_setcaps (GstVideoSplitSrcPad * srcpad, GstCaps * incaps)
{
  GstCaps *outcaps = NULL;
  GstQuery *query = NULL;
  GstVideoInfo info = { 0, };

  // Get the negotiated caps between the srcpad and its peer.
  outcaps = gst_pad_get_allowed_caps (GST_PAD (srcpad));
  // Fixate output caps based on the input caps.
  outcaps = gst_video_split_srcpad_fixate_caps (srcpad, incaps, outcaps);

  if ((outcaps == NULL) || gst_caps_is_empty (outcaps)) {
    GST_DEBUG_OBJECT (srcpad, "Failed to fixate caps!");

    if (outcaps != NULL)
      gst_caps_unref (outcaps);

    return FALSE;
  }

  if (!gst_pad_set_caps (GST_PAD (srcpad), outcaps)) {
    GST_DEBUG_OBJECT (srcpad, "Failed to set caps!");
    gst_caps_unref (outcaps);
    return FALSE;
  }

  // Query and decide buffer pool allocation.
  query = gst_query_new_allocation (outcaps, TRUE);

  if (!gst_pad_peer_query (GST_PAD (srcpad), query))
    GST_DEBUG_OBJECT (srcpad, "Failed to query peer allocation!");

  if (!gst_video_split_srcpad_decide_allocation (srcpad, query)) {
    GST_DEBUG_OBJECT (srcpad, "Failed to decide allocation!");
    gst_query_unref (query);
    return FALSE;
  }

  gst_query_unref (query);

  // Fill video info structure from the negotiated caps.
  if (!gst_video_info_from_caps (&info, outcaps)) {
    GST_DEBUG_OBJECT (srcpad, "Failed to extract video info!");
    return FALSE;
  }

  if (srcpad->info != NULL)
    gst_video_info_free (srcpad->info);

  srcpad->info = gst_video_info_copy (&info);

  // Enable passthrough if mode is 'none' and the sink and source caps intersect.
  srcpad->passthrough = (srcpad->mode == GST_VSPLIT_MODE_NONE) &&
      gst_caps_can_intersect (incaps, outcaps);

  GST_DEBUG_OBJECT (srcpad, "Negotiated caps: %" GST_PTR_FORMAT, outcaps);
  return TRUE;
}

static void
gst_video_split_srcpad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (object);
  GstElement *parent = gst_pad_get_parent_element (GST_PAD (srcpad));
  const gchar *propname = g_param_spec_get_name (pspec);

  // Extract the state from the pad parent or in case there is no parent
  // use default value as parameters are being set upon object construction.
  GstState state = parent ? GST_STATE (parent) : GST_STATE_VOID_PENDING;

  // Decrease the pad parent reference count as it is not needed any more.
  if (parent != NULL)
    gst_object_unref (parent);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (srcpad, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  switch (prop_id) {
    case PROP_MODE:
      srcpad->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_srcpad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, srcpad->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_srcpad_finalize (GObject * object)
{
  GstVideoSplitSrcPad *pad = GST_VIDEO_SPLIT_SRCPAD (object);

  gst_data_queue_set_flushing (pad->buffers, TRUE);
  gst_data_queue_flush (pad->buffers);

  gst_object_unref (GST_OBJECT_CAST(pad->buffers));

  if (pad->pool != NULL) {
    gst_buffer_pool_set_active (pad->pool, FALSE);
    gst_object_unref (pad->pool);
  }

  if (pad->info != NULL)
    gst_video_info_free (pad->info);

  g_cond_clear (&pad->drained);
  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_video_split_srcpad_parent_class)->finalize(object);
}

void
gst_video_split_srcpad_class_init (GstVideoSplitSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_split_srcpad_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_split_srcpad_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_split_srcpad_finalize);

  g_object_class_install_property (gobject, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Operational mode",
          GST_TYPE_VIDEO_SPLIT_MODE, DEFAULT_PROP_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
}

void
gst_video_split_srcpad_init (GstVideoSplitSrcPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  g_mutex_init (&pad->lock);
  g_cond_init (&pad->drained);

  pad->is_idle = TRUE;

  pad->info = NULL;
  pad->passthrough = FALSE;

  pad->pool = NULL;
  pad->buffers =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, pad);

  pad->mode = DEFAULT_PROP_MODE;
}
