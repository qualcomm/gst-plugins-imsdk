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

#define GST_META_IS_CV_OPTCLFLOW(meta) \
    (meta->info->api == GST_CV_OPTCLFLOW_META_API_TYPE)

#define GST_OVERLAY_DEFAULT_X          0
#define GST_OVERLAY_DEFAULT_Y          0
#define GST_OVERLAY_DEFAULT_FONTSIZE   12
#define GST_OVERLAY_DEFAULT_COLOR      0xFF0000FF
#define GST_OVERLAY_DEFAULT_FORMATTING "\"%d/%m/%Y %H:%M:%S\""

void
gst_overlay_timestamp_free (GstOverlayTimestamp * timestamp)
{
  if (timestamp->format != NULL)
    g_free (timestamp->format);
}

void
gst_overlay_string_free (GstOverlayString * string)
{
  if (string->contents != NULL)
    g_free (string->contents);
}

void
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

  if (GST_META_IS_CV_OPTCLFLOW (meta))
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
      GST_ERROR ("Failed to get file contents, cleanup: %s!",
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

gboolean
gst_extract_bboxes (const GValue * value, GArray * bboxes)
{
  GstStructure *structure = NULL;
  const GValue *position = NULL, *dimensions = NULL;
  guint idx = 0, num = 0, size = 0;

  size = gst_value_list_get_size (value);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayBBox *bbox = NULL;
    const gchar *name = NULL;

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      return FALSE;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      return FALSE;
    }

    // Check if there is text entry with the same identification name.
    for (num = 0; num < bboxes->len; num++) {
      bbox = &(g_array_index (bboxes, GstOverlayBBox, num));

      if (bbox->name == gst_structure_get_name_id (structure))
        break;
    }

    // There is no pre-existing string with that name, create and initialize it.
    if (num >= bboxes->len) {
      // User must provide at leats 'dimensions' of the new entry.
      if (!gst_structure_has_field (structure, "dimensions")) {
        GST_ERROR ("Structure at idx %u does not have 'dimensions' field!", idx);
        return FALSE;
      }

      // Resize the array which will create new entry at the end.
      g_array_set_size (bboxes, num + 1);
      bbox = &(g_array_index (bboxes, GstOverlayBBox, num));

      bbox->name = gst_structure_get_name_id (structure);
      bbox->enable = TRUE;

      bbox->destination.x = -1;
      bbox->destination.y = -1;
    }

    name = g_quark_to_string (bbox->name);

    if (gst_structure_has_field (structure, "dimensions")) {
      dimensions = gst_structure_get_value (structure, "dimensions");

      if (gst_value_array_get_size (dimensions) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'dimensions' field!", idx);
        goto cleanup;
      }

      bbox->destination.w = g_value_get_int (
          gst_value_array_get_value (dimensions, 0));
      bbox->destination.h = g_value_get_int (
          gst_value_array_get_value (dimensions, 1));

      if ((bbox->destination.w == 0) || (bbox->destination.h == 0)) {
        GST_ERROR ("Invalid width and/or height for structure at idx %u", idx);
        goto cleanup;
      }
    }

    GST_TRACE ("%s: Dimensions: [%d, %d]", name, bbox->destination.w,
        bbox->destination.h);

    if (gst_structure_has_field (structure, "position")) {
      position = gst_structure_get_value (structure, "position");

      if (gst_value_array_get_size (position) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'position' field!", idx);
        goto cleanup;
      }

      bbox->destination.x = g_value_get_int (
          gst_value_array_get_value (position, 0));
      bbox->destination.y = g_value_get_int (
          gst_value_array_get_value (position, 1));
    } else if ((bbox->destination.x == -1) && (bbox->destination.y == -1)) {
      bbox->destination.x = GST_OVERLAY_DEFAULT_X;
      bbox->destination.y = GST_OVERLAY_DEFAULT_Y;
    }

    GST_TRACE ("%s: Position: [%d, %d]", name, bbox->destination.x,
        bbox->destination.y);

    if (gst_structure_has_field (structure, "color"))
      gst_structure_get_int (structure, "color", &(bbox)->color);
    else if (bbox->color == 0)
      bbox->color = GST_OVERLAY_DEFAULT_COLOR;

    GST_TRACE ("%s: Color: 0x%X", name, bbox->color);

    if (gst_structure_has_field (structure, "enable"))
      gst_structure_get_boolean (structure, "enable", &(bbox)->enable);

    GST_TRACE ("%s: %s", name, bbox->enable ? "Enabled" : "Disabled");
  }

  return TRUE;

cleanup:
  // On failure resize the array to the old length, removing the new entry.
  g_array_set_size (bboxes, bboxes->len - 1);
  return FALSE;
}

gboolean
gst_extract_timestamps (const GValue * value, GArray * timestamps)
{
  GstStructure *structure = NULL;
  const GValue *position = NULL;
  guint idx = 0, num = 0, size = 0;

  size = gst_value_list_get_size (value);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayTimestamp *timestamp = NULL;
    const gchar *name = NULL;
    gint type = 0;

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      return FALSE;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      return FALSE;
    }

    if (gst_structure_has_name (structure, "Date/Time")) {
      type = GST_OVERLAY_TIMESTAMP_DATE_TIME;
    } else if (gst_structure_has_name (structure, "PTS/DTS")) {
      type = GST_OVERLAY_TIMESTAMP_PTS_DTS;
    } else {
      GST_ERROR ("Structure at idx %u invalid name!", idx);
      return FALSE;
    }

    if ((type == GST_OVERLAY_TIMESTAMP_PTS_DTS) &&
         gst_structure_has_field (structure, "format")) {
      GST_ERROR ("PTS/DTS at idx %u contains invalid 'format' field!", idx);
      return FALSE;
    }

    // Check if there is timestamp of the same type/name.
    for (num = 0; num < timestamps->len; num++) {
      timestamp = &(g_array_index (timestamps, GstOverlayTimestamp, num));

      if (timestamp->type == type)
        break;
    }

    // There is no pre-existing timestamp of this type, create and initialize it.
    if (num >= timestamps->len) {
      // Resize the array which will create new entry at the end.
      g_array_set_size (timestamps, num + 1);
      timestamp = &(g_array_index (timestamps, GstOverlayTimestamp, num));

      timestamp->type = type;
      timestamp->enable = TRUE;

      timestamp->position.x = -1;
      timestamp->position.y = -1;
    }

    name = gst_structure_get_name (structure);

    if (gst_structure_has_field (structure, "format")) {
      g_free (timestamp->format);

      timestamp->format = g_strdup (
          gst_structure_get_string (structure, "format"));
    } else if ((timestamp->type == GST_OVERLAY_TIMESTAMP_DATE_TIME) &&
               (timestamp->format == NULL)) {
      timestamp->format = g_strdup (GST_OVERLAY_DEFAULT_FORMATTING);
    }

    GST_TRACE ("%s: Format '%s'", name, timestamp->format);

    if (gst_structure_has_field (structure, "position")) {
      position = gst_structure_get_value (structure, "position");

      if (gst_value_array_get_size (position) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'position' field!", idx);
        goto cleanup;
      }

      timestamp->position.x =
          g_value_get_int (gst_value_array_get_value (position, 0));
      timestamp->position.y =
          g_value_get_int (gst_value_array_get_value (position, 1));
    } else if ((timestamp->position.x == -1) && (timestamp->position.y == -1)) {
      timestamp->position.x = GST_OVERLAY_DEFAULT_X;
      timestamp->position.y = GST_OVERLAY_DEFAULT_Y;
    }

    GST_TRACE ("%s: Position: [%d, %d]", name, timestamp->position.x,
        timestamp->position.y);

    if (gst_structure_has_field (structure, "color"))
      gst_structure_get_int (structure, "color", &(timestamp)->color);
    else if (timestamp->color == 0)
      timestamp->color = GST_OVERLAY_DEFAULT_COLOR;

    GST_TRACE ("%s: Color: 0x%X", name, timestamp->color);

    if (gst_structure_has_field (structure, "fontsize"))
      gst_structure_get_int (structure, "fontsize", &(timestamp)->fontsize);
    else if (timestamp->fontsize == 0)
      timestamp->fontsize = GST_OVERLAY_DEFAULT_FONTSIZE;

    GST_TRACE ("%s: Font size: %d", name, timestamp->fontsize);

    if (gst_structure_has_field (structure, "enable"))
      gst_structure_get_boolean (structure, "enable", &(timestamp)->enable);

    GST_TRACE ("%s: %s", name, timestamp->enable ? "Enabled" : "Disabled");
  }

  return TRUE;

cleanup:
  // On failure resize the array to the old length, removing the new entry.
  g_array_set_size (timestamps, timestamps->len - 1);
  return FALSE;
}

gboolean
gst_extract_strings (const GValue * value, GArray * strings)
{
  GstStructure *structure = NULL;
  const GValue *position = NULL;
  guint idx = 0, num = 0, size = 0;

  size = gst_value_list_get_size (value);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayString *string = NULL;
    const gchar *name = NULL;

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      return FALSE;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      return FALSE;
    }

    // Check if there is text entry with the same identification name.
    for (num = 0; num < strings->len; num++) {
      string = &(g_array_index (strings, GstOverlayString, num));

      if (string->name == gst_structure_get_name_id (structure))
        break;
    }

    // There is no pre-existing string with that name, create and initialize it.
    if (num >= strings->len) {
      // User must provide at leats 'contents' of the new string entry.
      if (!gst_structure_has_field (structure, "contents")) {
        GST_ERROR ("Structure at idx %u does not have 'contents' field!", idx);
        return FALSE;
      }

      // Resize the array which will create new entry at the end.
      g_array_set_size (strings, num + 1);
      string = &(g_array_index (strings, GstOverlayString, num));

      string->name = gst_structure_get_name_id (structure);
      string->enable = TRUE;

      string->position.x = -1;
      string->position.y = -1;
    }

    name = g_quark_to_string (string->name);

    if (gst_structure_has_field (structure, "contents")) {
      const gchar *contents = gst_structure_get_string (structure, "contents");

      g_free (string->contents);
      string->contents = g_strdup (contents);
    }

    if (gst_structure_has_field (structure, "position")) {
      position = gst_structure_get_value (structure, "position");

      if (gst_value_array_get_size (position) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'position' field!", idx);
        goto cleanup;
      }

      string->position.x =
          g_value_get_int (gst_value_array_get_value (position, 0));
      string->position.y =
          g_value_get_int (gst_value_array_get_value (position, 1));
    } else if ((string->position.x == -1) && (string->position.y == -1)) {
      string->position.x = GST_OVERLAY_DEFAULT_X;
      string->position.y = GST_OVERLAY_DEFAULT_Y;
    }

    GST_TRACE ("%s: Position: [%d, %d]", name, string->position.x,
        string->position.y);

    if (gst_structure_has_field (structure, "color"))
      gst_structure_get_int (structure, "color", &(string)->color);
    else if (string->color == 0)
      string->color = GST_OVERLAY_DEFAULT_COLOR;

    GST_TRACE ("%s: Color: 0x%X", name, string->color);

    if (gst_structure_has_field (structure, "fontsize"))
      gst_structure_get_int (structure, "fontsize", &(string)->fontsize);
    else if (string->fontsize == 0)
      string->fontsize = GST_OVERLAY_DEFAULT_FONTSIZE;

    GST_TRACE ("%s: Font size: %d", name, string->fontsize);

    if (gst_structure_has_field (structure, "enable"))
      gst_structure_get_boolean (structure, "enable", &(string)->enable);

    GST_TRACE ("%s: %s", name, string->enable ? "Enabled" : "Disabled");
  }

  return TRUE;

cleanup:
  // On failure resize the array to the old length, removing the new entry.
  g_array_set_size (strings, strings->len - 1);
  return FALSE;
}

gboolean
gst_extract_masks (const GValue * value, GArray * masks)
{
  GstStructure *structure = NULL;
  guint idx = 0, num = 0, size = 0;

  size = gst_value_list_get_size (value);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayMask *mask = NULL;
    const gchar *name = NULL;

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      return FALSE;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      return FALSE;
    }

    // Check if there is text entry with the same identification name.
    for (num = 0; num < masks->len; num++) {
      mask = &(g_array_index (masks, GstOverlayMask, num));

      if (mask->name == gst_structure_get_name_id (structure))
        break;
    }

    // There is no pre-existing mask with that name, create and initialize it.
    if (num >= masks->len) {
      // User must provide at leats 'rectangle/circle' of the new entry.
      if (!gst_structure_has_field (structure, "circle") &&
          !gst_structure_has_field (structure, "rectangle")) {
        GST_ERROR ("Structure at idx %u does not contain neither 'circle' "
            "nor 'rectangle' field!", idx);
        return FALSE;
      }

      // Resize the array which will create new entry at the end.
      g_array_set_size (masks, num + 1);
      mask = &(g_array_index (masks, GstOverlayMask, num));

      mask->name = gst_structure_get_name_id (structure);
      mask->enable = TRUE;

      mask->dims.wh[0] = -1;
      mask->dims.wh[1] = -1;

      mask->position.x = -1;
      mask->position.y = -1;
    }

    name = g_quark_to_string (mask->name);

    if (gst_structure_has_field (structure, "circle")) {
      const GValue *circle = gst_structure_get_value (structure, "circle");

      if (gst_value_array_get_size (circle) != 3) {
        GST_ERROR ("Structure at idx %u has invalid 'circle' field!", idx);
        goto cleanup;
      }

      mask->type = GST_OVERLAY_MASK_CIRCLE;
      mask->position.x = g_value_get_int (gst_value_array_get_value (circle, 0));
      mask->position.y = g_value_get_int (gst_value_array_get_value (circle, 1));
      mask->dims.radius = g_value_get_int (gst_value_array_get_value (circle, 2));

      if (mask->dims.radius == 0) {
        GST_ERROR ("Invalid radius for the circle at index %u", idx);
        goto cleanup;
      }

      GST_TRACE ("%s: Circle radius: %d", name, mask->dims.radius);
    } else if (gst_structure_has_field (structure, "rectangle")) {
      const GValue *rectangle = gst_structure_get_value (structure, "rectangle");

      if (gst_value_array_get_size (rectangle) != 4) {
        GST_ERROR ("Structure at idx %u has invalid 'rectangle' field!", idx);
        goto cleanup;
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
        goto cleanup;
      }

      GST_TRACE ("%s: Rectangle: [%d, %d]", name, mask->dims.wh[0],
          mask->dims.wh[1]);
    }

    if (gst_structure_has_field (structure, "color"))
      gst_structure_get_int (structure, "color", &(mask)->color);
    else if (mask->color == 0)
      mask->color = GST_OVERLAY_DEFAULT_COLOR;

    GST_TRACE ("%s: Color: 0x%X", name, mask->color);

    if (gst_structure_has_field (structure, "enable"))
      gst_structure_get_boolean (structure, "enable", &(mask)->enable);

    GST_TRACE ("%s: %s", name, mask->enable ? "Enabled" : "Disabled");
  }

  return TRUE;

cleanup:
  // On failure resize the array to the old length, removing the new entry.
  g_array_set_size (masks, masks->len - 1);
  return FALSE;
}

gboolean
gst_extract_static_images (const GValue * value, GArray * images)
{
  GstStructure *structure = NULL;
  const GValue *resolution = NULL, *destination = NULL;
  guint idx = 0, num = 0, size = 0;

  size = gst_value_list_get_size (value);

  for (idx = 0; idx < size; idx++) {
    const GValue *entry = gst_value_list_get_value (value, idx);
    GstOverlayImage *image = NULL;
    const gchar *name = NULL;

    if (G_VALUE_TYPE (entry) != GST_TYPE_STRUCTURE) {
      GST_ERROR ("GValue at idx %u is not structure", idx);
      return FALSE;
    }

    structure = GST_STRUCTURE (g_value_get_boxed (entry));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure at idx %u!", idx);
      return FALSE;
    }

    // Check if there is text entry with the same identification name.
    for (num = 0; num < images->len; num++) {
      image = &(g_array_index (images, GstOverlayImage, num));

      if (image->name == gst_structure_get_name_id (structure))
        break;
    }

    // There is no pre-existing mask with that name, create and initialize it.
    if (num >= images->len) {
      // User must provide 'path/resolution/destination' of the new entry.
      if (!gst_structure_has_field (structure, "path") ||
          !gst_structure_has_field (structure, "resolution") ||
          !gst_structure_has_field (structure, "destination")) {
        GST_ERROR ("Structure at idx %u does not contain 'path', 'resolution' "
            "and 'destination' fields!", idx);
        return FALSE;
      }

      // Resize the array which will create new entry at the end.
      g_array_set_size (images, num + 1);
      image = &(g_array_index (images, GstOverlayImage, num));

      image->name = gst_structure_get_name_id (structure);
      image->enable = TRUE;
    }

    name = g_quark_to_string (image->name);

    if (gst_structure_has_field (structure, "path") &&
        gst_structure_has_field (structure, "resolution")) {
      resolution = gst_structure_get_value (structure, "resolution");

      if (gst_value_array_get_size (resolution) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'resolution' field!", idx);
        goto cleanup;
      }

      image->width = g_value_get_int (
          gst_value_array_get_value (resolution, 0));
      image->height = g_value_get_int (
          gst_value_array_get_value (resolution, 1));

      image->path = g_strdup (gst_structure_get_string (structure, "path"));

      GST_TRACE ("%s: Dimensions: [%d, %d]", name, image->width, image->height);
      GST_TRACE ("%s: Path: '%s'", name, image->path);
    }

    if (gst_structure_has_field (structure, "destination")) {
      destination = gst_structure_get_value (structure, "destination");

      if (gst_value_array_get_size (destination) != 4) {
        GST_ERROR ("Structure at idx %u has invalid 'destination' field!", idx);
        goto cleanup;
      }

      image->destination.x = g_value_get_int (
          gst_value_array_get_value (destination, 0));
      image->destination.y = g_value_get_int (
          gst_value_array_get_value (destination, 1));
      image->destination.w = g_value_get_int (
          gst_value_array_get_value (destination, 2));
      image->destination.h = g_value_get_int (
          gst_value_array_get_value (destination, 3));

      GST_TRACE ("%s: Destination: [%d, %d, %d, %d]", name, image->destination.x,
          image->destination.y, image->destination.w, image->destination.h);
    }

    if (gst_structure_has_field (structure, "enable"))
      gst_structure_get_boolean (structure, "enable", &(image)->enable);

    GST_TRACE ("%s: %s", name, image->enable ? "Enabled" : "Disabled");
  }

  return TRUE;

cleanup:
  // On failure resize the array to the old length, removing the new entry.
  g_array_set_size (images, images->len - 1);
  return FALSE;
}

gchar *
gst_serialize_bboxes (GArray * bboxes)
{
  gchar *string = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0;

  g_value_init (&list, GST_TYPE_LIST);

  for (idx = 0; idx < bboxes->len; idx++) {
    GstOverlayBBox *bbox = NULL;
    GstStructure *entry = NULL;
    GValue position = G_VALUE_INIT, dimensions = G_VALUE_INIT;
    GValue value = G_VALUE_INIT;

    bbox = &(g_array_index (bboxes, GstOverlayBBox, idx));

    entry = gst_structure_new_empty (g_quark_to_string (bbox->name));
    gst_structure_set (entry, "color", G_TYPE_INT, bbox->color, NULL);

    g_value_init (&value, G_TYPE_INT);
    g_value_init (&position, GST_TYPE_ARRAY);
    g_value_init (&dimensions, GST_TYPE_ARRAY);

    g_value_set_int (&value, bbox->destination.x);
    gst_value_array_append_value (&position, &value);

    g_value_set_int (&value, bbox->destination.y);
    gst_value_array_append_value (&position, &value);

    gst_structure_set_value (entry, "position", &position);
    g_value_unset (&position);

    g_value_set_int (&value, bbox->destination.w);
    gst_value_array_append_value (&dimensions, &value);

    g_value_set_int (&value, bbox->destination.h);
    gst_value_array_append_value (&dimensions, &value);

    gst_structure_set_value (entry, "dimensions", &dimensions);
    g_value_unset (&dimensions);

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
    GST_ERROR ("Failed serialize bboxes!");
    return NULL;
  }

  return string;
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

    entry = gst_structure_new (g_quark_to_string (string->name),
        "contents", G_TYPE_STRING, string->contents, "fontsize", G_TYPE_INT,
        string->fontsize, "color", G_TYPE_INT, string->color, NULL);

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

    entry = gst_structure_new (g_quark_to_string (mask->name),
        "color", G_TYPE_UINT, mask->color, NULL);

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

    entry = gst_structure_new (g_quark_to_string (simage->name),
        "path", G_TYPE_STRING, simage->path, NULL);

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
