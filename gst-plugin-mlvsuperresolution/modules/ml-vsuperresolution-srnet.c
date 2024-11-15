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

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/ml/ml-module-video-super-resolution.h>


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, INT32, FLOAT32 }, " \
    "dimensions = (int) < <1, [32, 4096], [32, 4096]> >; " \
    "neural-network/tensors, " \
    "type = (string) { INT8, UINT8, INT32, FLOAT32 }, " \
    "dimensions = (int) < <1, [32, 4096], [32, 4096], [1, 3]> >"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;

struct _GstMLSubModule {
  // Configurated ML capabilities in structure format.
  GstMLInfo  mlinfo;

  // Offset values for each of the tensors for dequantization of some tensors.
  gdouble    qoffsets[GST_ML_MAX_TENSORS];
  // Scale values for each of the tensors for dequantization of some tensors.
  gdouble    qscales[GST_ML_MAX_TENSORS];
};

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
  gboolean success = FALSE;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  if (!(success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_CAPS))) {
    GST_ERROR ("Settings stucture does not contain configuration caps!");
    goto cleanup;
  }

  // Fetch the configuration capabilities.
  gst_structure_get (settings, GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, &caps,
      NULL);
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

  if ((GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_INT8) ||
      (GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_UINT8) ||
      (GST_ML_INFO_TYPE (&(submodule->mlinfo)) == GST_ML_TYPE_FLOAT32)) {
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

  gst_structure_free (settings);

  return success;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  GstVideoFrame *vframe = (GstVideoFrame *) output;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  guint8 *indata = NULL, *outdata = NULL;
  guint idx = 0, bpp = 0, padding = 0;
  gint row = 0, column = 0;

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

  indata = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);
  outdata = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
  mltype = GST_ML_FRAME_TYPE (mlframe);

  // TODO: Right now this won't work with any output resolution.
  // TODO: Expolore the possible use of OpenGL or OpenCL
  for (row = 0; row < GST_VIDEO_FRAME_HEIGHT (vframe); row++) {
    for (column = 0; column < GST_VIDEO_FRAME_WIDTH (vframe); column++) {
      // Calculate the destination index.
      idx = (((row * GST_VIDEO_FRAME_WIDTH (vframe)) + column) * bpp) +
          (row * padding);

      outdata[idx] = gst_ml_tensor_extract_value (mltype, indata, idx,
            submodule->qoffsets[0], submodule->qscales[0]);
      outdata[idx + 1] = gst_ml_tensor_extract_value (mltype, indata, (idx + 1),
            submodule->qoffsets[0], submodule->qscales[0]);
      outdata[idx + 2] = gst_ml_tensor_extract_value (mltype, indata, (idx + 2),
            submodule->qoffsets[0], submodule->qscales[0]);

      // If output has an alpha channel set it to opaque.
      if (bpp == 4)
        outdata[idx + 3] = 0xFF;
    }
  }

  return TRUE;
}
