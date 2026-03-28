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
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-post-process-keypoint.h"

#include <gst/video/video-utils.h>

G_DEFINE_BOXED_TYPE (GstMLKeypoint, gst_ml_keypoint,
    (GBoxedCopyFunc) gst_ml_keypoint_copy,
    (GBoxedFreeFunc) gst_ml_keypoint_free);

G_DEFINE_BOXED_TYPE (GstMLKeypointLink, gst_ml_keypoint_link,
    (GBoxedCopyFunc) gst_ml_keypoint_link_copy,
    (GBoxedFreeFunc) gst_ml_keypoint_link_free);

GstMLKeypoint*
gst_ml_keypoint_copy (const GstMLKeypoint * keypoint)
{
  GstMLKeypoint *newkeypoint = NULL;

  g_return_val_if_fail (keypoint != NULL, NULL);

  newkeypoint = g_slice_new (GstMLKeypoint);

  newkeypoint->name = keypoint->name;
  newkeypoint->confidence = keypoint->confidence;
  newkeypoint->color = keypoint->color;
  newkeypoint->x = keypoint->x;
  newkeypoint->y = keypoint->y;

  return newkeypoint;
}

void
gst_ml_keypoint_free (GstMLKeypoint * keypoint)
{
  if (keypoint == NULL)
    return;

  g_slice_free (GstMLKeypoint, keypoint);
}

void
gst_ml_keypoint_relative_transform (GstMLKeypoint * keypoint,
    const GstVideoRectangle * region, const gboolean clamp)
{
  g_return_if_fail (keypoint != NULL);
  g_return_if_fail (region != NULL);

  keypoint->x = (keypoint->x - region->x) / region->w;
  keypoint->y = (keypoint->y - region->y) / region->h;

  if (!clamp)
    return;

  keypoint->x = CLAMP (keypoint->x, 0.0f, 1.0f);
  keypoint->y = CLAMP (keypoint->y, 0.0f, 1.0f);
}

void
gst_ml_keypoint_affine_transform (GstMLKeypoint * keypoint, gdouble matrix[3][3])
{
  GstVideoPoint point = { 0, };

  g_return_if_fail (keypoint != NULL);

  point = (GstVideoPoint) {keypoint->x, keypoint->y};
  gst_video_point_affine_transform (&point, matrix);

  keypoint->x = point.x;
  keypoint->y = point.y;
}

GstStructure *
gst_ml_keypoint_to_structure (GstMLKeypoint * keypoint)
{
  GstStructure *structure = NULL;
  gchar *name = NULL;

  // Replace empty spaces otherwise subsequent stream parse call will fail.
  name = g_strdup (g_quark_to_string (keypoint->name));
  name = g_strdelimit (name, " ", '.');

  structure = gst_structure_new (name, "confidence", G_TYPE_DOUBLE,
      keypoint->confidence, "x", G_TYPE_DOUBLE, keypoint->x, "y", G_TYPE_DOUBLE,
      keypoint->y, "color", G_TYPE_UINT, keypoint->color, NULL);

  g_free (name);
  memset (keypoint, 0, sizeof (GstMLKeypoint));

  return structure;
}

GstMLKeypointLink*
gst_ml_keypoint_link_copy (const GstMLKeypointLink * link)
{
  GstMLKeypointLink *newlink = NULL;

  g_return_val_if_fail (link != NULL, NULL);

  newlink = g_slice_new (GstMLKeypointLink);

  newlink->l_kp = link->l_kp;
  newlink->r_kp = link->r_kp;
  newlink->color = link->color;

  return newlink;
}

void
gst_ml_keypoint_link_free (GstMLKeypointLink * link)
{
  if (link == NULL)
    return;

  g_slice_free (GstMLKeypointLink, link);
}
