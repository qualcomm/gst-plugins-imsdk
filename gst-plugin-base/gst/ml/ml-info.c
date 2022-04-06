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

#include "ml-info.h"

#define GST_CAT_DEFAULT gst_ml_info_debug_category()
static GstDebugCategory *
gst_ml_info_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("mlinfo", 0, "ML Info");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

void
gst_ml_info_init (GstMLInfo * info)
{
  g_return_if_fail (info != NULL);

  memset (info, 0, sizeof (GstMLInfo));

  info->type = GST_ML_TYPE_UNKNOWN;
  info->n_tensors = 0;
}

GstMLInfo *
gst_ml_info_new (void)
{
  GstMLInfo *info;

  info = g_slice_new (GstMLInfo);
  gst_ml_info_init (info);

  return info;
}

GstMLInfo *
gst_ml_info_copy (const GstMLInfo * info)
{
  return g_slice_dup (GstMLInfo, info);
}

void
gst_ml_info_free (GstMLInfo * info)
{
  g_slice_free (GstMLInfo, info);
}

gboolean
gst_ml_info_from_caps (GstMLInfo * info, const GstCaps * caps)
{
  GstStructure *structure;
  GstMLType type = GST_ML_TYPE_UNKNOWN;
  guint tensors[GST_ML_MAX_TENSORS][GST_ML_TENSOR_MAX_DIMS];
  guint idx = 0, dim = 0, n_tensors = 0, n_dimensions[GST_ML_MAX_TENSORS];
  const GValue *value;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  GST_DEBUG ("Parsing caps %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (structure, "neural-network/tensors")) {
    GST_ERROR ("Wrong name '%s', expected neural-network/tensors!",
        gst_structure_get_name (structure));
    return FALSE;
  }

  value = gst_structure_get_value (structure, "type");
  g_return_val_if_fail (value != NULL, FALSE);

  type = GST_VALUE_HOLDS_LIST (value) ? GST_ML_TYPE_UNKNOWN :
      gst_ml_type_from_string (g_value_get_string (value));

  GST_LOG ("Type: %s", gst_ml_type_to_string (type));

  // Reset all values to 0;
  memset (tensors, 0, sizeof (tensors));

  value = gst_structure_get_value (structure, "dimensions");
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (value), FALSE);

  for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
    const GValue *tensor = gst_value_array_get_value (value, idx);
    g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (tensor), FALSE);

    for (dim = 0; dim < gst_value_array_get_size (tensor); dim++) {
      const GValue *dimension = gst_value_array_get_value (tensor, dim);

      // In case of value range take the maximum.
      tensors[idx][dim] = GST_VALUE_HOLDS_INT_RANGE (dimension) ?
          gst_value_get_int_range_max (dimension) : g_value_get_int (dimension);

      GST_LOG ("Tensor[%u]: Dimension[%u] = %u", idx, dim, tensors[idx][dim]);
    }

    n_dimensions[idx] = gst_value_array_get_size (tensor);
    n_tensors++;
  }

  gst_ml_info_init (info);

  info->type = type;
  info->n_tensors = n_tensors;

  for (idx = 0; idx < n_tensors; idx++) {
    info->n_dimensions[idx] = n_dimensions[idx];

    for (dim = 0; dim < n_dimensions[idx]; dim++)
      info->tensors[idx][dim] = tensors[idx][dim];
  }

  return TRUE;
}

GstCaps *
gst_ml_info_to_caps (const GstMLInfo * info)
{
  GstCaps *caps = NULL;
  const gchar *type = NULL;
  GValue tensors = G_VALUE_INIT;
  guint idx = 0, dim = 0;

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->type != GST_ML_TYPE_UNKNOWN, NULL);

  type = gst_ml_type_to_string (info->type);
  g_return_val_if_fail (type != NULL, NULL);

  caps = gst_caps_new_simple ("neural-network/tensors",
      "type", G_TYPE_STRING, type, NULL);

  g_value_init (&tensors, GST_TYPE_ARRAY);

  for (idx = 0; idx < info->n_tensors; idx++) {
    GValue dimensions = G_VALUE_INIT;
    g_value_init (&dimensions, GST_TYPE_ARRAY);

    for (dim = 0; dim < info->n_dimensions[idx]; dim++) {
      GValue dimension = G_VALUE_INIT;

      g_value_init (&dimension, G_TYPE_INT);

      g_value_set_int (&dimension, info->tensors[idx][dim]);
      gst_value_array_append_value (&dimensions, &dimension);
      g_value_unset (&dimension);
    }

    gst_value_array_append_value (&tensors, &dimensions);
    g_value_unset (&dimensions);
  }

  gst_caps_set_value (caps, "dimensions", &tensors);
  g_value_unset (&tensors);

  GST_DEBUG ("Returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

gboolean
gst_ml_info_is_equal (const GstMLInfo * l_info, const GstMLInfo * r_info)
{
  guint idx = 0, dim = 0;

  if (l_info->n_tensors != r_info->n_tensors)
    return FALSE;

  if (l_info->type != r_info->type)
    return FALSE;

  for (idx = 0; idx < l_info->n_tensors; idx++) {
    if (l_info->n_dimensions[idx] != r_info->n_dimensions[idx])
      return FALSE;

    for (dim = 0; dim < l_info->n_dimensions[idx]; dim++)
      if (l_info->tensors[idx][dim] != r_info->tensors[idx][dim])
        return FALSE;
  }

  return TRUE;
}

gsize
gst_ml_info_tensor_size  (const GstMLInfo * info, guint index)
{
  guint dim = 0, value = 0;
  gsize size = 0;

  if (index >= info->n_tensors) {
    GST_ERROR ("There is no tensor at index %u!", index);
    return 0;
  }

  for (dim = 0; dim < info->n_dimensions[index]; dim++) {
    value = (info->tensors[index][dim] != 0) ? info->tensors[index][dim] : 1;
    size = (size != 0) ? (size * value) : value;
  }

  size *= gst_ml_type_get_size (info->type);
  GST_LOG ("Tensor[%u] size %" G_GSIZE_FORMAT, index, size);

  return size;
}

gsize
gst_ml_info_size  (const GstMLInfo * info)
{
  guint idx = 0;
  gsize size = 0;

  for (idx = 0; idx < info->n_tensors; idx++)
    size += gst_ml_info_tensor_size (info, idx);

  return size;
}
