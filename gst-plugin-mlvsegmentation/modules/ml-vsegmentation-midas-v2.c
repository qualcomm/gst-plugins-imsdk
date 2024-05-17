/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "ml-video-segmentation-module.h"


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

#define GFLOAT_PTR_CAST(data)       ((gfloat*)data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 256, 256, 1 > >; " \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 256, 256 > >"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // List of segmentation labels.
  GHashTable *labels;
};

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
  success = (submodule->labels != NULL);

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
  guint8 *indata = NULL, *outdata = NULL;
  guint idx = 0, bpp = 0, padding = 0, color = 0;
  gint row = 0, column = 0, width = 0, height = 0, length = 0;
  gdouble mindepth = G_MAXDOUBLE, maxdepth = G_MINDOUBLE;

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

  // Set the initial width and height of the source mask.
  width = GST_ML_FRAME_DIM (mlframe, 0, 2);
  height = GST_ML_FRAME_DIM (mlframe, 0, 1);

  indata = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  outdata = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

  // Set length as full width of the source mask.
  length = GST_ML_FRAME_DIM (mlframe, 0, 2);

  // Extract the SAR (Source Aspect Ratio) and adjust mask dimensions.
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    gint sar_n = 1, sar_d = 1;

    gst_structure_get_fraction (pmeta->info, "source-aspect-ratio",
        &sar_n, &sar_d);

    // Adjust dimensions so that only the mask with actual data will be used.
    if ((sar_n * height) > (width * sar_d)) // SAR > (width / height)
      height = gst_util_uint64_scale_int (width, sar_d, sar_n);
    else if ((sar_n * height) < (width * sar_d)) // SAR < (width / height)
      width = gst_util_uint64_scale_int (height, sar_n, sar_d);
  }

  // Find the minimum and maximum depth values in the mask.
  for (row = 0; row < height; row++) {
    for (column = 0; column < width; column++) {
      gdouble value = 0.0;

      idx = row * length + column;
      value = GFLOAT_PTR_CAST (indata)[idx];

      if (value > maxdepth)
        maxdepth = value;

      if (value < mindepth)
        mindepth = value;
    }
  }

  for (row = 0; row < GST_VIDEO_FRAME_HEIGHT (vframe); row++) {
    for (column = 0; column < GST_VIDEO_FRAME_WIDTH (vframe); column++) {
      GstLabel *label = NULL;
      guint id = G_MAXUINT8;

      // Calculate the source index.
      idx = length * gst_util_uint64_scale_int (row, height,
          GST_VIDEO_FRAME_HEIGHT (vframe));
      idx += gst_util_uint64_scale_int (column, width,
          GST_VIDEO_FRAME_WIDTH (vframe));

      id *= (GFLOAT_PTR_CAST (indata)[idx] - mindepth) / (maxdepth - mindepth);

      label = g_hash_table_lookup (submodule->labels, GUINT_TO_POINTER (id));
      color = (label != NULL) ? label->color : 0x000000FF;

      // Calculate the destination index.
      idx = (((row * GST_VIDEO_FRAME_WIDTH (vframe)) + column) * bpp) +
          (row * padding);

      outdata[idx] = EXTRACT_RED_COLOR (color);
      outdata[idx + 1] = EXTRACT_GREEN_COLOR (color);
      outdata[idx + 2] = EXTRACT_BLUE_COLOR (color);

      if (bpp == 4)
        outdata[idx + 3] = EXTRACT_ALPHA_COLOR (color);
    }
  }

  return TRUE;
}
