/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc All rights reserved.
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
 *     * Neither the name of Qualcomm Innovation Center, Inc nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
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

#include "ml-video-segmentation-module.h"

#include <stdio.h>
#include <math.h>


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define EXTRACT_RED_COLOR(color)    ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color)  ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)   ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color)  ((color) & 0xFF)

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GINT8_PTR_CAST(data)        ((gint8*) data)
#define GUINT8_PTR_CAST(data)       ((guint8*) data)

// Non-maximum Suppression (NMS) threshold (50%), corresponding to 2/3 overlap.
#define NMS_INTERSECTION_THRESHOLD  0.5F

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, FLOAT32 }, " \
    "dimensions = (int) < <1, [21, 42840], 4>, <1, [21, 42840]>, <1, [21, 42840], [1, 32]>, <1, [21, 42840]>, <1, [1, 32], [32, 2048], [32, 2048]> > "

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLBoxPrediction GstMLBoxPrediction;
typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLBoxPrediction {
  GQuark  label;
  gfloat  confidence;
  guint32 color;

  gfloat  top;
  gfloat  left;
  gfloat  bottom;
  gfloat  right;
};


struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // List of bbox labels.
  GHashTable *labels;
  // Confidence threshold value.
  gfloat     threshold;

  // Offset values for each of the tensors for dequantization of some tensors.
  gdouble    qoffsets[GST_ML_MAX_TENSORS];
  // Scale values for each of the tensors for dequantization of some tensors.
  gdouble    qscales[GST_ML_MAX_TENSORS];
};

static inline gdouble
gst_ml_module_get_dequant_value (gpointer pdata, GstMLType mltype, guint idx,
    gdouble offset, gdouble scale)
{
  switch (mltype) {
    case GST_ML_TYPE_INT8:
      return ((GINT8_PTR_CAST (pdata))[idx] - offset) * scale;
    case GST_ML_TYPE_UINT8:
      return ((GUINT8_PTR_CAST (pdata))[idx] - offset) * scale;
    case GST_ML_TYPE_FLOAT32:
      return (GFLOAT_PTR_CAST (pdata))[idx];
    default:
      break;
  }
  return 0.0;
}

static inline void
gst_ml_box_relative_translation (GstMLBoxPrediction * box, guint width,
    guint height)
{
  box->top /= height;
  box->bottom /= height;
  box->left /= width;
  box->right /= width;
}

static inline gdouble
gst_ml_box_intersection_score (GstMLBoxPrediction * l_box, GstMLBoxPrediction * r_box)
{
  gdouble width = 0, height = 0, intersection = 0, l_area = 0, r_area = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN (l_box->right, r_box->right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_box->left, r_box->left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_box->bottom, r_box->bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_box->top, r_box->top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  intersection = width * height;

  // Calculate the area of the 2 objects.
  l_area = (l_box->right - l_box->left) * (l_box->bottom - l_box->top);
  r_area = (r_box->right - r_box->left) * (r_box->bottom - r_box->top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

static inline gint
gst_ml_non_max_suppression (GstMLBoxPrediction * l_bbox, GArray * boxes)
{
  GstMLBoxPrediction *r_bbox = NULL;
  guint idx = 0;
  gdouble score = 0.0;

  for (idx = 0; idx < boxes->len;  idx++) {
    r_bbox = &(g_array_index (boxes, GstMLBoxPrediction, idx));

    // If labels do not match, continue with next list entry.
    if (l_bbox->label != r_bbox->label)
      continue;

    score = gst_ml_box_intersection_score (l_bbox, r_bbox);

    // If the score is below the threshold, continue with next list entry.
    if (score <= NMS_INTERSECTION_THRESHOLD)
      continue;

    // If confidence of current bbox is higher, remove the old entry.
    if (l_bbox->confidence > r_bbox->confidence)
      return idx;

    // If confidence of current bbox is lower, don't add it to the list.
    if (l_bbox->confidence <= r_bbox->confidence)
      return -2;
  }

  // If this point is reached then add current bbox to the list;
  return -1;
}

static void
gst_ml_module_bbox_parse_tripleblock_tensors (GstMLSubModule * submodule,
    GstMLFrame * mlframe, GArray * bboxes, GArray * mask_matrix_indices)
{
  GstProtectionMeta *pmeta = NULL;
  GstMLLabel *label = NULL;
  gpointer mlboxes = NULL, scores = NULL, classes = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gint nms = -1, num = 0;
  guint idx = 0, class_idx = 0, inheight = 0, inwidth = 0, n_paxels = 0;
  gfloat confidence = 0;

  pmeta = gst_buffer_get_protection_meta (mlframe->buffer);

  // Extract the dimensions of the input tensor that produced the output tensors.
  gst_structure_get_uint (pmeta->info, "input-tensor-height", &inheight);
  gst_structure_get_uint (pmeta->info, "input-tensor-width", &inwidth);

  mltype = GST_ML_FRAME_TYPE (mlframe);
  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 1);

  mlboxes = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  scores = GST_ML_FRAME_BLOCK_DATA (mlframe, 1);
  classes = GST_ML_FRAME_BLOCK_DATA (mlframe, 3);

  for (idx = 0; idx < n_paxels; idx++) {
    GstMLBoxPrediction bbox = { 0, };

    confidence = gst_ml_module_get_dequant_value (scores, mltype, idx,
        submodule->qoffsets[1], submodule->qscales[1]);
    class_idx = gst_ml_module_get_dequant_value (classes, mltype, idx,
        submodule->qoffsets[3], submodule->qscales[3]);

    // Discard results below the minimum confidence threshold.
    if (confidence < submodule->threshold)
      continue;

    bbox.left = gst_ml_module_get_dequant_value (mlboxes, mltype,
        (idx * 4) + 0, submodule->qoffsets[0], submodule->qscales[0]);
    bbox.top = gst_ml_module_get_dequant_value (mlboxes, mltype,
        (idx * 4) + 1, submodule->qoffsets[0], submodule->qscales[0]);
    bbox.right  = gst_ml_module_get_dequant_value (mlboxes, mltype,
        (idx * 4) + 2, submodule->qoffsets[0], submodule->qscales[0]);
    bbox.bottom  = gst_ml_module_get_dequant_value (mlboxes, mltype,
        (idx * 4) + 3, submodule->qoffsets[0], submodule->qscales[0]);

    GST_TRACE ("Class: %u Box[%f, %f, %f, %f] Confidence: %f", class_idx,
        bbox.top, bbox.left, bbox.bottom, bbox.right, confidence);

    // Translate absolute dimensions to relative.
    gst_ml_box_relative_translation (&bbox, inwidth, inheight);

    label = g_hash_table_lookup (
        submodule->labels, GUINT_TO_POINTER (class_idx));

    bbox.confidence = confidence * 100.0F;
    bbox.label = g_quark_from_string (label ? label->name : "unknown");
    bbox.color = label ? label->color : 0x000000FF;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_non_max_suppression (&bbox, bboxes);

    // If the NMS result is -2 don't add the bbox to the list.
    if (nms == (-2))
      continue;

    GST_LOG ("Label: %s  Box[%f, %f, %f, %f] Confidence: %f",
        g_quark_to_string (bbox.label), bbox.top, bbox.left, bbox.bottom,
        bbox.right, bbox.confidence);

    // If the NMS result is above -1 remove the entry with the nms index.
    if (nms >= 0) {
      bboxes = g_array_remove_index (bboxes, nms);
      mask_matrix_indices = g_array_remove_index (mask_matrix_indices, nms);
    }

    bboxes = g_array_append_val (bboxes, bbox);

    // Save the index to the corresponding mask matrix.
    num = idx * GST_ML_FRAME_DIM (mlframe, 2, 2);
    mask_matrix_indices = g_array_append_val (mask_matrix_indices, num);
  }
}

static guint32*
gst_ml_module_colormask_parse_monoblock_tensor (GstMLSubModule * submodule,
    GstMLFrame * mlframe, GArray * bboxes, GArray * mask_matrix_indices)
{
  guint32 *colormask = NULL;
  gpointer masks = NULL, protos = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint idx = 0, num = 0, n_blocks = 0;
  guint row = 0, column = 0, top = 0, left = 0, bottom = 0, right = 0;
  gfloat confidence = 0;

  mltype = GST_ML_FRAME_TYPE (mlframe);

  // Number blocks in the 5t (protos) tensor.
  n_blocks = GST_ML_FRAME_DIM (mlframe, 4, 3) * GST_ML_FRAME_DIM (mlframe, 4, 2);

  masks = GST_ML_FRAME_BLOCK_DATA (mlframe, 2);
  protos = GST_ML_FRAME_BLOCK_DATA (mlframe, 4);

  // Allocate memory for the resulted segmentation mask.
  colormask = g_new0 (guint32, n_blocks);

  // Process the segmentation data only the in recognized box bboxes.
  for (idx = 0; idx < bboxes->len; idx++) {
    GstMLBoxPrediction *bbox = NULL;
    gdouble m_value = 0.0, p_value = 0.0;
    guint m_idx = 0, b_idx= 0;

    bbox = &(g_array_index (bboxes, GstMLBoxPrediction, idx));
    m_idx = g_array_index (mask_matrix_indices, guint, idx);

    // Transform to dimensions in the color mask.
    top = bbox->top * GST_ML_FRAME_DIM (mlframe, 4, 2);
    left = bbox->left * GST_ML_FRAME_DIM (mlframe, 4, 3);
    bottom = bbox->bottom * GST_ML_FRAME_DIM (mlframe, 4, 2);
    right = bbox->right * GST_ML_FRAME_DIM (mlframe, 4, 3);

    for (row = top; row < bottom; row++) {
      for (column = left; column < right; column++) {
        // Index of the current macro block in the 5th (protos) tensor.
        b_idx = column + (row * GST_ML_FRAME_DIM (mlframe, 4, 3));

        // Perform matrix multiplication for current macro block.
        for (num = 0; num < GST_ML_FRAME_DIM (mlframe, 2, 2); num++) {
          // Get the mask value for current channel/class.
          m_value = gst_ml_module_get_dequant_value (masks, mltype,
              m_idx + num, submodule->qoffsets[2], submodule->qscales[2]);

          // Get the protos value for current channel/class and macro block.
          p_value = gst_ml_module_get_dequant_value (protos, mltype,
              b_idx + num * n_blocks, submodule->qoffsets[4], submodule->qscales[4]);

          // Confidence score for this macro block.
          confidence += m_value * p_value;
        }

        // Apply sigmoid on the end confidence for this macro block.
        confidence = 1 / (1 + expf (- confidence));

        colormask[b_idx] = (confidence > 0.5) ? bbox->color : 0x00000000;
      }
    }
  }

  return colormask;
}

gpointer
gst_ml_module_open (void)
{
  GstMLSubModule *submodule = NULL;
  guint idx = 0;

  submodule = g_slice_new0 (GstMLSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  // Initialize the quantization offsets and scales.
  for (idx = 0; idx < GST_ML_MAX_TENSORS; idx++) {
    submodule->qoffsets[idx] = 0.0;
    submodule->qscales[idx] = 1.0;
  }

  return (gpointer) submodule;
}

void
gst_ml_module_close (gpointer instance)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  if (submodule->labels != NULL)
    g_hash_table_destroy (submodule->labels);

  g_slice_free (GstMLSubModule, submodule);
}

GstCaps *
gst_ml_module_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&modulecaps);
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

gboolean
gst_ml_module_configure (gpointer instance, GstStructure * settings)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GstCaps *caps = NULL, *mlcaps = NULL;
  const gchar *input = NULL;
  GValue list = G_VALUE_INIT;
  gboolean success = FALSE;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  if (!(success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_CAPS))) {
    GST_ERROR ("Settings stucture does not contain configuration caps!");
    goto cleanup;
  }

  // Fetch the configuration capabilities.
  gst_structure_get (settings, GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, &caps, NULL);
  // Get the set of supported capabilities.
  mlcaps = gst_ml_module_caps ();

  // Make sure that the configuration capabilities are fixated and supported.
  if (!(success = gst_caps_is_fixed (caps))) {
    GST_ERROR ("Configuration caps are not fixated!");
    goto cleanup;
  } else if (!(success = gst_caps_can_intersect (caps, mlcaps))) {
    GST_ERROR ("Configuration caps are not supported!");
    goto cleanup;
  }

  if (!(success = gst_ml_info_from_caps (&(submodule->mlinfo), caps))) {
    GST_ERROR ("Failed to get ML info from confguration caps!");
    goto cleanup;
  }

  input = gst_structure_get_string (settings, GST_ML_MODULE_OPT_LABELS);

  // Parse funtion will print error message if it fails, simply goto cleanup.
  if (!(success = gst_ml_parse_labels (input, &list)))
    goto cleanup;

  submodule->labels = gst_ml_load_labels (&list);

  // Labels funtion will print error message if it fails, simply goto cleanup.
  if (!(success = (submodule->labels != NULL)))
    goto cleanup;

  submodule->threshold = 0.51;

  if ((GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_INT8) ||
      (GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_UINT8)) {
    GstStructure *constants = NULL;
    const GValue *qoffsets = NULL, *qscales = NULL;
    guint idx = 0, n_tensors = 0;

    success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_CONSTANTS);
    if (!success) {
      GST_ERROR ("Settings stucture does not contain constants value!");
      goto cleanup;
    }

    constants = GST_STRUCTURE (g_value_get_boxed (
        gst_structure_get_value (settings, GST_ML_MODULE_OPT_CONSTANTS)));

    if (!(success = gst_structure_has_field (constants, "q-offsets"))) {
      GST_ERROR ("Missing quantization offsets coefficients!");
      goto cleanup;
    } else if (!(success = gst_structure_has_field (constants, "q-scales"))) {
      GST_ERROR ("Missing quantization scales coefficients!");
      goto cleanup;
    }

    qoffsets = gst_structure_get_value (constants, "q-offsets");
    qscales = gst_structure_get_value (constants, "q-scales");
    n_tensors = GST_ML_INFO_N_TENSORS (&(submodule->mlinfo));

    if (!(success = (gst_value_array_get_size (qoffsets) == n_tensors))) {
      GST_ERROR ("Expecting %u dequantization offsets entries but received "
          "only %u!", n_tensors, gst_value_array_get_size (qoffsets));
      goto cleanup;
    } else if (!(success = (gst_value_array_get_size (qscales) == n_tensors))) {
      GST_ERROR ("Expecting %u dequantization scales entries but received "
          "only %u!", n_tensors, gst_value_array_get_size (qscales));
      goto cleanup;
    }

    for (idx = 0; idx < n_tensors; idx++) {
      submodule->qoffsets[idx] =
          g_value_get_double (gst_value_array_get_value (qoffsets, idx));
      submodule->qscales[idx] =
          g_value_get_double (gst_value_array_get_value (qscales, idx));
    }
  }

cleanup:
  if (caps != NULL)
    gst_caps_unref (caps);

  g_value_unset (&list);
  gst_structure_free (settings);

  return success;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GstVideoFrame *vframe = (GstVideoFrame *) output;
  GstProtectionMeta *pmeta = NULL;
  GArray *bboxes = NULL, *mask_matrix_indices = NULL;
  guint32 *colormask = NULL;
  guint8 *outdata = NULL;
  gint row = 0, column = 0, sar_n = 1, sar_d = 1, n_rows = 0, n_columns = 0;
  guint idx = 0, num = 0, bpp = 0, padding = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (vframe != NULL, FALSE);

  if (!gst_ml_info_is_equal (&(mlframe->info), &(submodule->mlinfo))) {
    GST_ERROR ("ML frame with unsupported layout!");
    return FALSE;
  }

  // Retrive the video frame Bytes Per Pixel for later calculations.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (vframe->info.finfo) *
      GST_VIDEO_INFO_N_COMPONENTS (&(vframe)->info) / CHAR_BIT;

  // Calculate the row padding in bytes.
  padding = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) -
      (GST_VIDEO_FRAME_WIDTH (vframe) * bpp);

  // Initialize the array with bounding box predictions.
  bboxes = g_array_new (FALSE, FALSE, sizeof (GstMLBoxPrediction));

  // Initialize the array with indices to mask matrices for the bouding boxes.
  mask_matrix_indices = g_array_new (FALSE, FALSE, sizeof (guint));

  // First find the boxes in which there are recognized objects.
  gst_ml_module_bbox_parse_tripleblock_tensors (submodule, mlframe, bboxes,
      mask_matrix_indices);

  // If no objects are recognized return immediately, nothing further to do.
  if (bboxes->len == 0)
    goto cleanup;

  // Set the initial dimensions of the color mask matrix.
  n_columns = GST_ML_FRAME_DIM (mlframe, 4, 3);
  n_rows = GST_ML_FRAME_DIM (mlframe, 4, 2);

  pmeta = gst_buffer_get_protection_meta (mlframe->buffer);

  // Extract the SAR (Source Aspect Ratio) for color mask adjustments.
  gst_structure_get_fraction (pmeta->info, "source-aspect-ratio", &sar_n, &sar_d);

  // Adjust dimensions so that only the mask with actual data will be used.
  if ((sar_n * n_rows) > (n_columns * sar_d)) // SAR > (n_columns / n_rows)
    n_rows = gst_util_uint64_scale_int (n_columns, sar_d, sar_n);
  else if ((sar_n * n_rows) < (n_columns * sar_d)) // SAR < (n_columns / n_rows)
    n_columns = gst_util_uint64_scale_int (n_rows, sar_n, sar_d);

  // Process the segmentation data only the in recognized box bboxes.
  colormask = gst_ml_module_colormask_parse_monoblock_tensor (submodule,
      mlframe, bboxes, mask_matrix_indices);

  // Convinient pointer to the data in the output video frame.
  outdata = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  for (row = 0; row < GST_VIDEO_FRAME_HEIGHT (vframe); row++) {
    for (column = 0; column < GST_VIDEO_FRAME_WIDTH (vframe); column++) {
      // Calculate the source index. First calculate the row offset.
      num = GST_ML_FRAME_DIM (mlframe, 4, 3) *
          gst_util_uint64_scale_int (row, n_rows, GST_VIDEO_FRAME_HEIGHT (vframe));

      // Calculate the source index. Second calculate the column offset.
      num += gst_util_uint64_scale_int (column, n_columns,
          GST_VIDEO_FRAME_WIDTH (vframe));

      // Calculate the destination index.
      idx = (((row * GST_VIDEO_FRAME_WIDTH (vframe)) + column) * bpp) +
          (row * padding);

      outdata[idx] = EXTRACT_RED_COLOR (colormask[num]);
      outdata[idx + 1] = EXTRACT_GREEN_COLOR (colormask[num]);
      outdata[idx + 2] = EXTRACT_BLUE_COLOR (colormask[num]);

      if (bpp == 4)
        outdata[idx + 3] = EXTRACT_ALPHA_COLOR (colormask[num]);
    }
  }

cleanup:
  g_array_free (mask_matrix_indices, TRUE);
  g_array_free (bboxes, TRUE);
  g_free (colormask);

  return TRUE;
}
