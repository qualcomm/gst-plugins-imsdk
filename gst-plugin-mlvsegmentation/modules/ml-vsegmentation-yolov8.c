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

#include <stdio.h>
#include <math.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-segmentation.h>
#include <gst/ml/ml-module-video-detection.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < <1, [21, 42840], 4>, <1, [21, 42840]>, <1, [21, 42840], [1, 32]>, <1, [21, 42840]>, <1, [1, 32], [32, 2048], [32, 2048]> > "

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // The width of the model input tensor.
  guint      inwidth;
  // The height of the model input tensor.
  guint      inheight;

  // List of bbox labels.
  GHashTable *labels;
  // Confidence threshold value.
  gfloat     threshold;
};

static void
gst_ml_module_bbox_parse_tripleblock_tensors (GstMLSubModule * submodule,
    GstMLFrame * mlframe, GArray * bboxes, GArray * mask_matrix_indices)
{
  GstMLLabel *label = NULL;
  gfloat  *mlboxes = NULL, *scores = NULL, *classes = NULL;
  gint nms = -1, num = 0;
  guint idx = 0, class_idx = 0, n_paxels = 0;
  gfloat confidence = 0;

  n_paxels = GST_ML_FRAME_DIM (mlframe, 0, 1);

  mlboxes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 0));
  scores = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 1));
  classes = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 3));

  for (idx = 0; idx < n_paxels; idx++) {
    GstMLBoxEntry bbox = { 0, };

    confidence = scores[idx];
    class_idx = classes[idx];

    // Discard results below the minimum confidence threshold.
    if (confidence < submodule->threshold)
      continue;

    bbox.left = mlboxes[idx * 4];
    bbox.top = mlboxes[idx * 4 + 1];
    bbox.right = mlboxes[idx * 4 + 2];
    bbox.bottom = mlboxes[idx * 4 + 3];

    GST_TRACE ("Class: %u Box[%f, %f, %f, %f] Confidence: %f", class_idx,
        bbox.top, bbox.left, bbox.bottom, bbox.right, confidence);

    // Translate absolute dimensions to relative.
    gst_ml_box_relative_translation (&bbox, submodule->inwidth, submodule->inheight);

    label = g_hash_table_lookup (
        submodule->labels, GUINT_TO_POINTER (class_idx));

    bbox.confidence = confidence * 100.0F;
    bbox.name = g_quark_from_string (label ? label->name : "unknown");
    bbox.color = label ? label->color : 0x000000FF;

    // Non-Max Suppression (NMS) algorithm.
    nms = gst_ml_box_non_max_suppression (&bbox, bboxes);

    // If the NMS result is -2 don't add the bbox to the list.
    if (nms == (-2))
      continue;

    GST_LOG ("Label: %s  Box[%f, %f, %f, %f] Confidence: %f",
        g_quark_to_string (bbox.name), bbox.top, bbox.left, bbox.bottom,
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
  gfloat *masks = NULL, *protos = NULL;
  guint idx = 0, num = 0, n_blocks = 0;
  guint row = 0, column = 0, top = 0, left = 0, bottom = 0, right = 0;
  gfloat confidence = 0;

  // Number blocks in the 5t (protos) tensor.
  n_blocks = GST_ML_FRAME_DIM (mlframe, 4, 3) * GST_ML_FRAME_DIM (mlframe, 4, 2);

  masks = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 2));
  protos = GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, 4));

  // Allocate memory for the resulted segmentation mask.
  colormask = g_new0 (guint32, n_blocks);

  // Process the segmentation data only the in recognized box bboxes.
  for (idx = 0; idx < bboxes->len; idx++) {
    GstMLBoxEntry *bbox = NULL;
    gdouble m_value = 0.0, p_value = 0.0;
    guint m_idx = 0, b_idx= 0;

    bbox = &(g_array_index (bboxes, GstMLBoxEntry, idx));
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
          m_value = masks[m_idx + num];

          // Get the protos value for current channel/class and macro block.
          p_value = protos[b_idx + num * n_blocks];

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

  submodule = g_slice_new0 (GstMLSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

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
  GstVideoRectangle region = { 0, };
  gint row = 0, column = 0, width = 0, height = 0;
  guint idx = 0, num = 0, bpp = 0, stride = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (vframe != NULL, FALSE);

  pmeta = gst_buffer_get_protection_meta_id (mlframe->buffer,
      gst_batch_channel_name (0));

  // Extract the dimensions of the input tensor that produced the output tensors.
  if (submodule->inwidth == 0 || submodule->inheight == 0) {
    gst_ml_structure_get_source_dimensions (pmeta->info, &(submodule->inwidth),
        &(submodule->inheight));
  }

  width = GST_VIDEO_FRAME_WIDTH (vframe);
  height = GST_VIDEO_FRAME_HEIGHT (vframe);

  // Retrive the video frame Bytes Per Pixel for later calculations.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (vframe->info.finfo) *
      GST_VIDEO_INFO_N_COMPONENTS (&(vframe)->info) / CHAR_BIT;

  stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0);

  // Initialize the array with bounding box predictions.
  bboxes = g_array_new (FALSE, FALSE, sizeof (GstMLBoxEntry));

  // Initialize the array with indices to mask matrices for the bouding boxes.
  mask_matrix_indices = g_array_new (FALSE, FALSE, sizeof (guint));

  // First find the boxes in which there are recognized objects.
  gst_ml_module_bbox_parse_tripleblock_tensors (submodule, mlframe, bboxes,
      mask_matrix_indices);

  // If no objects are recognized return immediately, nothing further to do.
  if (bboxes->len == 0)
    goto cleanup;

  // Extract the source tensor region for color mask extraction.
  gst_ml_structure_get_source_region (pmeta->info, &region);

  // Transform source tensor region dimensions to dimensions in the color mask.
  region.x *= (GST_ML_FRAME_DIM (mlframe, 4, 3) / (gfloat) submodule->inwidth);
  region.y *= (GST_ML_FRAME_DIM (mlframe, 4, 2) / (gfloat) submodule->inheight);
  region.w *= (GST_ML_FRAME_DIM (mlframe, 4, 3) / (gfloat) submodule->inwidth);
  region.h *= (GST_ML_FRAME_DIM (mlframe, 4, 2) / (gfloat) submodule->inheight);

  // Process the segmentation data only the in recognized box bboxes.
  colormask = gst_ml_module_colormask_parse_monoblock_tensor (submodule,
      mlframe, bboxes, mask_matrix_indices);

  // Convinient pointer to the data in the output video frame.
  outdata = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  for (row = 0; row < height; row++) {
    idx = row * stride;

    for (column = 0; column < width; column++, idx += bpp) {
      // Calculate the source index. First calculate the row offset.
      num = GST_ML_FRAME_DIM (mlframe, 4, 3) *
          (region.y + gst_util_uint64_scale_int (row, region.h, height));

      // Calculate the source index. Second calculate the column offset.
      num += region.x + gst_util_uint64_scale_int (column, region.w, width);

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
