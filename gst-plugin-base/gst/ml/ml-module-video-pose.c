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
 * ​​​​​Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "ml-module-video-pose.h"

void
gst_ml_pose_entry_cleanup (GstMLPoseEntry * entry)
{
  if (entry->keypoints != NULL)
    g_array_free (entry->keypoints, TRUE);

  // if (entry->connections != NULL)
  //   g_array_free (entry->connections, TRUE);
}

void
gst_ml_pose_prediction_cleanup (GstMLPosePrediction * prediction)
{
  g_array_set_clear_func (prediction->entries,
      (GDestroyNotify) gst_ml_pose_entry_cleanup);

  if (prediction->entries != NULL)
    g_array_free (prediction->entries, TRUE);
}

gint
gst_ml_pose_compare_entries (const GstMLPoseEntry * l_entry,
    const GstMLPoseEntry * r_entry)
{
  if (l_entry->confidence > r_entry->confidence)
    return -1;
  else if (l_entry->confidence < r_entry->confidence)
    return 1;

  return 0;
}

gboolean
gst_ml_load_links (const GValue * list, const guint idx, GArray * links)
{
  GstStructure *structure = NULL;
  const GValue *array = NULL, *value = NULL;
  GstMLKeypointsLink link = { 0, };
  guint id = 0, num = 0, size = 0;

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, idx)));

  if (structure == NULL) {
    GST_ERROR ("Failed to extract structure!");
    return FALSE;
  }

  if (!gst_structure_has_field (structure, "links"))
    return TRUE;

  // Initial ID of the source keypoint.
  gst_structure_get_uint (structure, "id", &id);
  link.s_kp_id = id;

  array = gst_structure_get_value (structure, "links");
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (array), FALSE);

  size = gst_value_array_get_size (array);
  g_return_val_if_fail (size != 0, FALSE);

  for (num = 0; num < size; num++) {
    value = gst_value_array_get_value (array, num);
    g_return_val_if_fail (G_VALUE_HOLDS_UINT (value), FALSE);

    link.d_kp_id = id = g_value_get_uint (value);
    g_array_append_val (links, link);

    // Recursively check and load the next link in teh chain/tree.
    if (!gst_ml_load_links (list, id, links))
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_ml_load_connections (const GValue * list, GArray * connections)
{
  GstStructure *structure = NULL;
  GstMLKeypointsLink connection = { 0, };
  guint idx = 0, size = 0;

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

    gst_structure_get_uint (structure, "id", &(connection.s_kp_id));
    gst_structure_get_uint (structure, "connection", &(connection.d_kp_id));

    g_array_append_val (connections, connection);
  }

  return TRUE;
}

void
gst_ml_keypoint_transform_coordinates (GstMLKeypoint * keypoint,
    GstVideoRectangle * region)
{
  keypoint->x = (keypoint->x - region->x) / region->w;
  keypoint->y = (keypoint->y - region->y) / region->h;
}

gboolean
gst_ml_module_video_pose_execute (GstMLModule * module, GstMLFrame * mlframe,
    GArray * predictions)
{
  return gst_ml_module_execute (module, mlframe, (gpointer) predictions);
}
