/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "common-utils.h"

gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    if (feature == NULL && ((gst_caps_features_get_size (features) == 0) ||
            gst_caps_features_is_any (features)))
      return TRUE;

    // Skip ANY caps and return immediately if feature is present.
    if ((feature != NULL) && !gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
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

gboolean
gst_parse_string_property_value (const GValue * value, GValue * output)
{
  const gchar *input = g_value_get_string (value);

  if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (input, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get file contents, error: '%s'!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    // Add opening and closing brackets if output value is of type list.
    if (G_VALUE_HOLDS (output, GST_TYPE_LIST)) {
      GString *string = g_string_new (contents);

      string = g_string_prepend (string, "{ ");
      string = g_string_append (string, " }");

      g_free (contents);

      // Get the raw character data.
      contents = g_string_free (string, FALSE);
    }

    success = gst_value_deserialize (output, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize file contents!");
      return FALSE;
    }
  } else if (!gst_value_deserialize (output, input)) {
    GST_ERROR ("Failed to deserialize string!");
    return FALSE;
  }

  return TRUE;
}

GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    if (gst_structure_has_name (GST_PROTECTION_META_CAST (meta)->info, name))
      return GST_PROTECTION_META_CAST (meta);
  }

  return NULL;
}

void
gst_buffer_copy_protection_meta (GstBuffer * destination, GstBuffer * source)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (source, &state,
              GST_PROTECTION_META_API_TYPE))) {
    gst_buffer_add_protection_meta (destination,
        gst_structure_copy (GST_PROTECTION_META_CAST (meta)->info));
  }
}