/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "videocomposerutils.h"

GType
gst_video_composer_rotate_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue methods[] = {
    { GST_VIDEO_COMPOSER_ROTATE_NONE,
        "No rotation", "none"
    },
    { GST_VIDEO_COMPOSER_ROTATE_90_CW,
        "Rotate 90 degrees clockwise", "90CW"
    },
    { GST_VIDEO_COMPOSER_ROTATE_90_CCW,
        "Rotate 90 degrees counter-clockwise", "90CCW"
    },
    { GST_VIDEO_COMPOSER_ROTATE_180,
        "Rotate 180 degrees", "180"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstVideoComposerRotate", methods);

  return gtype;
}

gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  for (idx = 0; idx < gst_caps_get_size (caps); idx++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    if (feature == NULL && ((gst_caps_features_get_size (features) == 0) ||
            gst_caps_features_is_any (features)))
      return TRUE;

    // Skip ANY caps and return immediately if feature is present.
    if ((feature != NULL) && !gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}
