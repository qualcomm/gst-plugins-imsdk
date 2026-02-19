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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYright OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-module-pose.h"

gboolean
gst_ml_load_skeleton_links (GPtrArray * links, const GValue * list,
                            const guint idx)
{
  GstStructure *structure = NULL;
  const GValue *array = NULL, *value = NULL;
  GArray *link = NULL;
  guint num = 0, size = 0, s_kp_id = 0, d_kp_id = 0;

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, idx)));

  if (structure == NULL) {
    GST_ERROR ("Failed to extract structure!");
    return FALSE;
  }

  if (!gst_structure_has_field (structure, "links"))
    return TRUE;

  // Initial ID of the source keypoint.
  gst_structure_get_uint (structure, "id", &s_kp_id);

  array = gst_structure_get_value (structure, "links");
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (array), FALSE);

  size = gst_value_array_get_size (array);
  g_return_val_if_fail (size != 0, FALSE);

  for (num = 0; num < size; num++) {
    link = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
    g_array_set_size (link, 2);

    value = gst_value_array_get_value (array, num);
    g_return_val_if_fail (G_VALUE_HOLDS_UINT (value), FALSE);

    g_array_index (link, guint, 0) = s_kp_id;
    g_array_index (link, guint, 1) = d_kp_id = g_value_get_uint (value);

    g_ptr_array_add (links, link);

    // Recursively check and load the next link in teh chain/tree.
    if (!gst_ml_load_skeleton_links (links, list, d_kp_id))
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_ml_load_connections (GPtrArray * connections, const GValue * list)
{
  GstStructure *structure = NULL;
  GArray *connection = NULL;
  guint idx = 0, size = 0, s_kp_id = 0, d_kp_id = 0;

  size = gst_value_list_get_size (list);

  for (idx = 0; idx < size; idx++) {
    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (list, idx)));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure!");
      return FALSE;
    }

    if (!gst_structure_has_field (structure, "connection"))
      continue;

    connection = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
    g_array_set_size (connection, 2);

    gst_structure_get_uint (structure, "id", &s_kp_id);
    gst_structure_get_uint (structure, "connection", &d_kp_id);

    g_array_index (connection, guint, 0) = s_kp_id;
    g_array_index (connection, guint, 1) = d_kp_id;

    g_ptr_array_add (connections, connection);
  }

  return TRUE;
}

gboolean
gst_ml_module_pose_execute (GstMLModule * module, GstMLFrame * mlframe,
    GPtrArray * predictions)
{
  return gst_ml_module_execute (module, mlframe, (gpointer) predictions);
}
