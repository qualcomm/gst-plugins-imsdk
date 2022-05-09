/*
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

#include "overlayutils.h"

GST_DEBUG_CATEGORY_EXTERN (gst_overlay_debug);
#define GST_CAT_DEFAULT gst_overlay_debug

#define GST_META_IS_OBJECT_DETECTION(meta) \
    ((meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) && \
    (GST_VIDEO_ROI_META_CAST (meta)->roi_type == \
        g_quark_from_static_string ("ObjectDetection")))

#define GST_META_IS_IMAGE_CLASSIFICATION(meta) \
    ((meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) && \
    (GST_VIDEO_ROI_META_CAST (meta)->roi_type == \
        g_quark_from_static_string ("ImageClassification")))

#define GST_META_IS_POSE_ESTIMATION(meta) \
    ((meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) && \
    (GST_VIDEO_ROI_META_CAST (meta)->roi_type == \
        g_quark_from_static_string ("PoseEstimation")))

#define GST_META_IS_CVP_OPTCLFLOW(meta) \
    (meta->info->api == GST_CVP_OPTCLFLOW_META_API_TYPE)

static void
gst_overlay_timestamp_free (GstOverlayTimestamp * timestamp)
{
  if (timestamp->format != NULL)
    g_free (timestamp->format);
}

static void
gst_overlay_string_free (GstOverlayString * string)
{
  if (string->contents != NULL)
    g_free (string->contents);
}

static void
gst_overlay_image_free (GstOverlayImage * simage)
{
  if (simage->contents != NULL)
    g_free (simage->contents);

  if (simage->path != NULL)
    g_free (simage->path);
}

guint
gst_meta_overlay_type (GstMeta * meta)
{
  if (GST_META_IS_OBJECT_DETECTION (meta))
    return GST_OVERLAY_TYPE_DETECTION;

  if (GST_META_IS_IMAGE_CLASSIFICATION (meta))
    return GST_OVERLAY_TYPE_CLASSIFICATION;

  if (GST_META_IS_POSE_ESTIMATION (meta))
    return GST_OVERLAY_TYPE_POSE_ESTIMATION;

  if (GST_META_IS_CVP_OPTCLFLOW (meta))
    return GST_OVERLAY_TYPE_OPTCLFLOW;

  return GST_OVERLAY_TYPE_MAX;
}

gboolean
gst_parse_property_value (const gchar * input, GValue * value)
{
  if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
    GString *string = NULL;
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (input, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    string = g_string_new (contents);
    g_free (contents);

    // Add opening and closing brackets.
    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    // Get the raw character data.
    contents = g_string_free (string, FALSE);

    success = gst_value_deserialize (value, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize file contents!");
      return FALSE;
    }
  } else if (!gst_value_deserialize (value, input)) {
    GST_ERROR ("Failed to deserialize string!");
    return FALSE;
  }

  return TRUE;
}

GArray *
gst_extract_bboxes (const GValue * value)
{
  GArray *bboxes = NULL;
  guint idx = 0, size = 0, n_params = 0;

  size = gst_value_list_get_size (value);
  bboxes = g_array_sized_new (FALSE, FALSE, sizeof (GstOverlayBBox), size);

  g_array_set_size (bboxes, size);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_array_get_value (value, idx);
    GstOverlayBBox *bbox = &(g_array_index (bboxes, GstOverlayBBox, idx));

    n_params = gst_value_array_get_size (entry);

    if (n_params < 4 || n_params > 5) {
      GST_ERROR ("Invalid box dimensions at index %u!", idx);
      g_array_free (bboxes, TRUE);
      return NULL;
    }

    bbox->destination.x = g_value_get_int (gst_value_array_get_value (entry, 0));
    bbox->destination.y = g_value_get_int (gst_value_array_get_value (entry, 1));
    bbox->destination.w = g_value_get_int (gst_value_array_get_value (entry, 2));
    bbox->destination.h = g_value_get_int (gst_value_array_get_value (entry, 3));

    if (n_params == 5)
      bbox->color = g_value_get_int (gst_value_array_get_value (entry, 4));

    if (bbox->destination.w == 0 || bbox->destination.h == 0) {
      GST_ERROR ("Invalid width and/or height for the box at index %u", idx);
      g_array_free (bboxes, TRUE);
      return NULL;
    }
  }

  return bboxes;
}

GArray *
gst_extract_timestamps (const GValue * value)
{
  GArray *timestamps = NULL;
  GstStructure *structure = NULL;
  const GValue *position = NULL;
  guint idx = 0, size = 0;

  size = gst_value_list_get_size (value);
  timestamps = g_array_sized_new (FALSE, TRUE, sizeof (GstOverlayTimestamp), size);

  g_array_set_size (timestamps, size);
  g_array_set_clear_func (timestamps,
      (GDestroyNotify) gst_overlay_timestamp_free);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayTimestamp *timestamp =
        &(g_array_index (timestamps, GstOverlayTimestamp, idx));

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    } else if (!gst_structure_has_name (structure, "Date/Time") &&
               !gst_structure_has_name (structure, "PTS/DTS")) {
      GST_ERROR ("Structure at idx %u invalid name!", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    } else if (!gst_structure_has_field (structure, "fontsize") ||
               !gst_structure_has_field (structure, "position") ||
               !gst_structure_has_field (structure, "color")) {
      GST_ERROR ("Structure at idx %u does not contain 'fontsize', 'position' "
          "and/or 'color' fields!", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    }

    if (gst_structure_has_name (structure, "Date/Time") &&
        !gst_structure_has_field (structure, "format")) {
      GST_ERROR ("Structure at idx %u does not contain 'format' field!", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    } else if (gst_structure_has_name (structure, "PTS/DTS") &&
               gst_structure_has_field (structure, "format")) {
      GST_ERROR ("Structure at idx %u contains invalid 'format' field!", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    }

    position = gst_structure_get_value (structure, "position");

    if (gst_value_array_get_size (position) != 2) {
      GST_ERROR ("Structure at idx %u has invalid 'position' field!", idx);
      g_array_free (timestamps, TRUE);
      return NULL;
    }

    timestamp->position.x =
        g_value_get_int (gst_value_array_get_value (position, 0));
    timestamp->position.y =
        g_value_get_int (gst_value_array_get_value (position, 1));

    if (gst_structure_has_name (structure, "Date/Time")) {
      timestamp->format =
          g_strdup (gst_structure_get_string (structure, "format"));
      timestamp->type = GST_OVERLAY_TIMESTAMP_DATE_TIME;
    } else if (gst_structure_has_name (structure, "PTS/DTS")) {
      timestamp->format = NULL;
      timestamp->type = GST_OVERLAY_TIMESTAMP_PTS_DTS;
    }

    gst_structure_get_int (structure, "fontsize", &(timestamp)->fontsize);
    gst_structure_get_int (structure, "color", &(timestamp)->color);
  }

  return timestamps;
}

GArray *
gst_extract_strings (const GValue * value)
{
  GArray *strings = NULL;
  GstStructure *structure = NULL;
  const GValue *position = NULL;
  guint idx = 0, size = 0;

  size = gst_value_list_get_size (value);
  strings = g_array_sized_new (FALSE, TRUE, sizeof (GstOverlayString), size);

  g_array_set_size (strings, size);
  g_array_set_clear_func (strings, (GDestroyNotify) gst_overlay_string_free);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayString *string = &(g_array_index (strings, GstOverlayString, idx));

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      g_array_free (strings, TRUE);
      return NULL;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      g_array_free (strings, TRUE);
      return NULL;
    } else if (!gst_structure_has_name (structure, "Text")) {
      GST_ERROR ("Structure at idx %u invalid name!", idx);
      g_array_free (strings, TRUE);
      return NULL;
    } else if (!gst_structure_has_field (structure, "contents") ||
               !gst_structure_has_field (structure, "fontsize") ||
               !gst_structure_has_field (structure, "position") ||
               !gst_structure_has_field (structure, "color")) {
      GST_ERROR ("Structure at idx %u does not contain 'contents', 'fontsize',"
          " 'position' and/or 'color' fields!", idx);
      g_array_free (strings, TRUE);
      return NULL;
    }

    position = gst_structure_get_value (structure, "position");

    if (gst_value_array_get_size (position) != 2) {
      GST_ERROR ("Structure at idx %u has invalid 'position' field!", idx);
      g_array_free (strings, TRUE);
      return NULL;
    }

    string->position.x =
        g_value_get_int (gst_value_array_get_value (position, 0));
    string->position.y =
        g_value_get_int (gst_value_array_get_value (position, 1));

    string->contents =
        g_strdup (gst_structure_get_string (structure, "contents"));

    gst_structure_get_int (structure, "fontsize", &(string)->fontsize);
    gst_structure_get_int (structure, "color", &(string)->color);
  }

  return strings;
}

GArray *
gst_extract_masks (const GValue * value)
{
  GArray *masks = NULL;
  GstStructure *structure = NULL;
  guint idx = 0, size = 0;

  size = gst_value_list_get_size (value);
  masks = g_array_sized_new (FALSE, FALSE, sizeof (GstOverlayMask), size);

  g_array_set_size (masks, size);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayMask *mask = &(g_array_index (masks, GstOverlayMask, idx));

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      g_array_free (masks, TRUE);
      return NULL;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      g_array_free (masks, TRUE);
      return NULL;
    } else if (!gst_structure_has_name (structure, "Mask")) {
      GST_ERROR ("Structure at idx %u invalid name!", idx);
      g_array_free (masks, TRUE);
      return NULL;
    } else if (!gst_structure_has_field (structure, "color")) {
      GST_ERROR ("Structure at idx %u does not contain 'color' field!", idx);
      g_array_free (masks, TRUE);
      return NULL;
    }

    gst_structure_get_int (structure, "color", &(mask)->color);

    if (gst_structure_has_field (structure, "circle")) {
      const GValue *circle = gst_structure_get_value (structure, "circle");

      if (gst_value_array_get_size (circle) != 3) {
        GST_ERROR ("Structure at idx %u has invalid 'circle' field!", idx);
        g_array_free (masks, TRUE);
        return NULL;
      }

      mask->type = GST_OVERLAY_MASK_CIRCLE;
      mask->position.x = g_value_get_int (gst_value_array_get_value (circle, 0));
      mask->position.y = g_value_get_int (gst_value_array_get_value (circle, 1));
      mask->dims.radius = g_value_get_int (gst_value_array_get_value (circle, 2));

      if (mask->dims.radius == 0) {
        GST_ERROR ("Invalid radius for the circle at index %u", idx);
        g_array_free (masks, TRUE);
        return NULL;
      }
    } else if (gst_structure_has_field (structure, "rectangle")) {
      const GValue *rectangle = gst_structure_get_value (structure, "rectangle");

      if (gst_value_array_get_size (rectangle) != 4) {
        GST_ERROR ("Structure at idx %u has invalid 'rectangle' field!", idx);
        g_array_free (masks, TRUE);
        return NULL;
      }

      mask->type = GST_OVERLAY_MASK_RECTANGLE;
      mask->position.x =
          g_value_get_int (gst_value_array_get_value (rectangle, 0));
      mask->position.y =
          g_value_get_int (gst_value_array_get_value (rectangle, 1));
      mask->dims.wh[0] =
          g_value_get_int (gst_value_array_get_value (rectangle, 2));
      mask->dims.wh[1] =
          g_value_get_int (gst_value_array_get_value (rectangle, 3));

      if (mask->dims.wh[0] == 0 || mask->dims.wh[1] == 0) {
        GST_ERROR ("Invalid width and/or height for rectangle at idx %u", idx);
        g_array_free (masks, TRUE);
        return NULL;
      }
    } else {
      GST_ERROR ("Structure at idx %u does not contain neither 'circle' nor "
          "'rectangle' field!", idx);
      g_array_free (masks, TRUE);
      return NULL;
    }
  }

  return masks;
}

GArray *
gst_extract_static_images (const GValue * value)
{
  GArray *simages = NULL;
  GstStructure *structure = NULL;
  const GValue *resolution = NULL, *destination = NULL;
  guint idx = 0, size = 0;

  size = gst_value_list_get_size (value);
  simages = g_array_sized_new (FALSE, TRUE, sizeof (GstOverlayImage), size);

  g_array_set_size (simages, size);
  g_array_set_clear_func (simages, (GDestroyNotify) gst_overlay_image_free);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayImage *simage = &(g_array_index (simages, GstOverlayImage, idx));

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      g_array_free (simages, TRUE);
      return NULL;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      g_array_free (simages, TRUE);
      return NULL;
    } else if (!gst_structure_has_name (structure, "Image")) {
      GST_ERROR ("Structure at idx %u invalid name!", idx);
      g_array_free (simages, TRUE);
      return NULL;
    } else if (!gst_structure_has_field (structure, "path") ||
               !gst_structure_has_field (structure, "resolution") ||
               !gst_structure_has_field (structure, "destination")) {
      GST_ERROR ("Structure at idx %u does not contain 'path', 'resolution' "
          "and/or 'destination' fields!", idx);
      g_array_free (simages, TRUE);
      return NULL;
    }

    resolution = gst_structure_get_value (structure, "resolution");
    destination = gst_structure_get_value (structure, "destination");

    if (gst_value_array_get_size (resolution) != 2) {
      GST_ERROR ("Structure at idx %u has invalid 'resolution' field!", idx);
      g_array_free (simages, TRUE);
      return NULL;
    } else if (gst_value_array_get_size (destination) != 4) {
      GST_ERROR ("Structure at idx %u has invalid 'destination' field!", idx);
      g_array_free (simages, TRUE);
      return NULL;
    }

    simage->path =
        g_strdup (gst_structure_get_string (structure, "path"));
    simage->width = g_value_get_int (gst_value_array_get_value (resolution, 0));
    simage->height = g_value_get_int (gst_value_array_get_value (resolution, 1));

    simage->destination.x = g_value_get_int (
        gst_value_array_get_value (destination, 0));
    simage->destination.y = g_value_get_int (
        gst_value_array_get_value (destination, 1));
    simage->destination.w = g_value_get_int (
        gst_value_array_get_value (destination, 2));
    simage->destination.h = g_value_get_int (
        gst_value_array_get_value (destination, 3));
  }

  return simages;
}

gchar *
gst_serialize_timestamps (GArray * timestamps)
{
  gchar *string = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0, length = 0;

  g_value_init (&list, GST_TYPE_LIST);
  length = (timestamps != NULL) ? timestamps->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstOverlayTimestamp *timestamp = NULL;
    GstStructure *entry = NULL;
    GValue value = G_VALUE_INIT, position = G_VALUE_INIT;

    timestamp = &(g_array_index (timestamps, GstOverlayTimestamp, idx));

    if (GST_OVERLAY_TIMESTAMP_DATE_TIME == timestamp->type)
      entry = gst_structure_new ("Date/Time",
          "format", G_TYPE_STRING, timestamp->format, NULL);
    else if (GST_OVERLAY_TIMESTAMP_PTS_DTS == timestamp->type)
      entry = gst_structure_new_empty ("PTS/DTS");

    gst_structure_set (entry, "fontsize", G_TYPE_INT, timestamp->fontsize,
        "color", G_TYPE_INT, timestamp->color, NULL);

    g_value_init (&value, G_TYPE_INT);
    g_value_init (&position, GST_TYPE_ARRAY);

    g_value_set_int (&value, timestamp->position.x);
    gst_value_array_append_value (&position, &value);

    g_value_set_int (&value, timestamp->position.y);
    gst_value_array_append_value (&position, &value);

    gst_structure_set_value (entry, "position", &position);
    g_value_unset (&position);

    g_value_unset (&value);
    g_value_init (&value, GST_TYPE_STRUCTURE);

    gst_value_set_structure (&value, entry);
    gst_structure_free (entry);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR ("Failed serialize timestamps!");
    return NULL;
  }

  return string;
}

gchar *
gst_serialize_strings (GArray * strings)
{
  gchar *string = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0, length = 0;

  g_value_init (&list, GST_TYPE_LIST);
  length = (strings != NULL) ? strings->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstOverlayString *string = NULL;
    GstStructure *entry = NULL;
    GValue value = G_VALUE_INIT, position = G_VALUE_INIT;

    string = &(g_array_index (strings, GstOverlayString, idx));

    entry = gst_structure_new ("Text",
        "contents", G_TYPE_STRING, string->contents,
        "fontsize", G_TYPE_INT, string->fontsize,
        "color", G_TYPE_INT, string->color,
        NULL);

    g_value_init (&value, G_TYPE_INT);
    g_value_init (&position, GST_TYPE_ARRAY);

    g_value_set_int (&value, string->position.x);
    gst_value_array_append_value (&position, &value);

    g_value_set_int (&value, string->position.y);
    gst_value_array_append_value (&position, &value);

    gst_structure_set_value (entry, "position", &position);
    g_value_unset (&position);

    g_value_unset (&value);
    g_value_init (&value, GST_TYPE_STRUCTURE);

    gst_value_set_structure (&value, entry);
    gst_structure_free (entry);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR ("Failed serialize strings!");
    return NULL;
  }

  return string;
}

gchar *
gst_serialize_masks (GArray * masks)
{
  gchar *string = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0, length = 0;

  g_value_init (&list, GST_TYPE_LIST);
  length = (masks != NULL) ? masks->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstOverlayMask *mask = NULL;
    GstStructure *entry = NULL;
    GValue value = G_VALUE_INIT, array = G_VALUE_INIT;

    mask = &(g_array_index (masks, GstOverlayMask, idx));

    entry = gst_structure_new ("Mask",
        "color", G_TYPE_INT, mask->color,
        NULL);

    g_value_init (&value, G_TYPE_INT);
    g_value_init (&array, GST_TYPE_ARRAY);

    g_value_set_int (&value, mask->position.x);
    gst_value_array_append_value (&array, &value);

    g_value_set_int (&value, mask->position.y);
    gst_value_array_append_value (&array, &value);

    if (mask->type == GST_OVERLAY_MASK_CIRCLE) {
      g_value_set_int (&value, mask->dims.radius);
      gst_value_array_append_value (&array, &value);

      gst_structure_set_value (entry, "circle", &array);
    } else if (mask->type == GST_OVERLAY_MASK_RECTANGLE) {
      g_value_set_int (&value, mask->dims.wh[0]);
      gst_value_array_append_value (&array, &value);

      g_value_set_int (&value, mask->dims.wh[1]);
      gst_value_array_append_value (&array, &value);

      gst_structure_set_value (entry, "rectangle", &array);
    }

    g_value_unset (&array);
    g_value_unset (&value);

    g_value_init (&value, GST_TYPE_STRUCTURE);

    gst_value_set_structure (&value, entry);
    gst_structure_free (entry);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR ("Failed serialize privacy masks!");
    return NULL;
  }

  return string;
}

gchar *
gst_serialize_static_images (GArray * simages)
{
  gchar *string = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0, length = 0;

  g_value_init (&list, GST_TYPE_LIST);
  length = (simages != NULL) ? simages->len : 0;

  for (idx = 0; idx < length; idx++) {
    GstOverlayImage *simage = NULL;
    GstStructure *entry = NULL;
    GValue value = G_VALUE_INIT, array = G_VALUE_INIT;

    simage = &(g_array_index (simages, GstOverlayImage, idx));

    entry = gst_structure_new ("Image",
        "path", G_TYPE_STRING, simage->path,
        NULL);

    g_value_init (&value, G_TYPE_INT);
    g_value_init (&array, GST_TYPE_ARRAY);

    g_value_set_int (&value, simage->width);
    gst_value_array_append_value (&array, &value);

    g_value_set_int (&value, simage->height);
    gst_value_array_append_value (&array, &value);

    gst_structure_set_value (entry, "resolution", &array);
    g_value_unset (&array);

    g_value_set_int (&value, simage->destination.x);
    gst_value_array_append_value (&array, &value);

    g_value_set_int (&value, simage->destination.y);
    gst_value_array_append_value (&array, &value);

    g_value_set_int (&value, simage->destination.w);
    gst_value_array_append_value (&array, &value);

    g_value_set_int (&value, simage->destination.h);
    gst_value_array_append_value (&array, &value);

    gst_structure_set_value (entry, "destination", &array);
    g_value_unset (&array);

    g_value_unset (&value);
    g_value_init (&value, GST_TYPE_STRUCTURE);

    gst_value_set_structure (&value, entry);
    gst_structure_free (entry);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  // Serialize the predictions into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR ("Failed serialize static images!");
    return NULL;
  }

  return string;
}
