/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
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
     (GST_VIDEO_ROI_META_CAST (meta)->roi_type != \
          g_quark_from_static_string ("ImageRegion")))

#define GST_META_IS_IMAGE_CLASSIFICATION(meta) \
    (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE)

#define GST_META_IS_POSE_ESTIMATION(meta) \
    (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE)

#define GST_META_IS_CV_OPTCLFLOW(meta) \
    (meta->info->api == GST_CV_OPTCLFLOW_META_API_TYPE)

#define GST_OVERLAY_DEFAULT_X          0
#define GST_OVERLAY_DEFAULT_Y          0
#define GST_OVERLAY_DEFAULT_FONTSIZE   12
#define GST_OVERLAY_DEFAULT_COLOR      0xFF0000FF
#define GST_OVERLAY_DEFAULT_FORMATTING "\"%d/%m/%Y %H:%M:%S\""

static inline void
gst_video_blit_release (GstVideoBlit * blit)
{
  GstBuffer *buffer = NULL;

  blit->source.x = blit->source.y = 0;
  blit->source.w = blit->source.h = 0;

  blit->destination.x = blit->destination.y = 0;
  blit->destination.w = blit->destination.h = 0;

  buffer = blit->frame->buffer;
  gst_video_frame_unmap (blit->frame);

  // Unreference buffer twice as 2nd refcount has been set when blit was cached.
  gst_buffer_unref (buffer);
  gst_buffer_unref (buffer);

  g_slice_free (GstVideoFrame, blit->frame);
  blit->frame = NULL;
}

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
gst_extract_bboxes (const GValue * value, GArray * bboxes)
{
  GstStructure *structure = NULL;
  const GValue *position = NULL, *dimensions = NULL;
  guint idx = 0, num = 0, size = 0;
  gint x = -1, y = -1, width = 0, height = 0, color = 0;
  gboolean changed = FALSE;

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

    // Check if there is bounding box entry with the same identification name.
    for (num = 0; num < bboxes->len; num++) {
      bbox = &(g_array_index (bboxes, GstOverlayBBox, num));

      if (bbox->name == gst_structure_get_name_id (structure))
        break;
    }

    // There is no pre-existing box with that name, create and initialize it.
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

      bbox->destination.x = GST_OVERLAY_DEFAULT_X;
      bbox->destination.y = GST_OVERLAY_DEFAULT_Y;

      bbox->color = GST_OVERLAY_DEFAULT_COLOR;
    }

    name = g_quark_to_string (bbox->name);

    if (gst_structure_has_field (structure, "dimensions")) {
      dimensions = gst_structure_get_value (structure, "dimensions");

      if (gst_value_array_get_size (dimensions) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'dimensions' field!", idx);
        goto cleanup;
      }

      width = g_value_get_int (gst_value_array_get_value (dimensions, 0));
      height = g_value_get_int (gst_value_array_get_value (dimensions, 1));

      if ((width == 0) || (height == 0)) {
        GST_ERROR ("Invalid width and/or height for structure at idx %u", idx);
        goto cleanup;
      }

      // Raise the flag for clearing cached blit if dimensions are different.
      changed |=
          (bbox->destination.w != width) || (bbox->destination.h != height);

      bbox->destination.w = width;
      bbox->destination.h = height;
    }

    GST_TRACE ("%s: Dimensions: [%d, %d]", name, bbox->destination.w,
        bbox->destination.h);

    if (gst_structure_has_field (structure, "position")) {
      position = gst_structure_get_value (structure, "position");

      if (gst_value_array_get_size (position) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'position' field!", idx);
        goto cleanup;
      }

      x = g_value_get_int (gst_value_array_get_value (position, 0));
      y = g_value_get_int (gst_value_array_get_value (position, 1));

      // Raise the flag for clearing cached blit if position is different.
      changed |= (bbox->destination.x != x) || (bbox->destination.y != y);

      bbox->destination.x = x;
      bbox->destination.y = y;
    }

    GST_TRACE ("%s: Position: [%d, %d]", name, bbox->destination.x,
        bbox->destination.y);

    if (gst_structure_has_field (structure, "color")) {
      gst_structure_get_int (structure, "color", &color);

      // Raise the flag for clearing cached blit if color is different.
      changed |= bbox->color != color;

      bbox->color = color;
    }

    GST_TRACE ("%s: Color: 0x%X", name, bbox->color);

    // Clear the cached blit if the flag has been raised.
    if (changed && (bbox->blit.frame != NULL))
      gst_video_blit_release (&(bbox->blit));

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
  gint x = -1, y = -1, color = 0, fontsize = 0;

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

      timestamp->position.x = GST_OVERLAY_DEFAULT_X;
      timestamp->position.y = GST_OVERLAY_DEFAULT_Y;

      timestamp->color = GST_OVERLAY_DEFAULT_COLOR;
      timestamp->fontsize = GST_OVERLAY_DEFAULT_FONTSIZE;
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

      x = g_value_get_int (gst_value_array_get_value (position, 0));
      y = g_value_get_int (gst_value_array_get_value (position, 1));

      timestamp->position.x = x;
      timestamp->position.y = y;
    }

    GST_TRACE ("%s: Position: [%d, %d]", name, timestamp->position.x,
        timestamp->position.y);

    if (gst_structure_has_field (structure, "color")) {
      gst_structure_get_int (structure, "color", &color);
      timestamp->color = color;
    }

    GST_TRACE ("%s: Color: 0x%X", name, timestamp->color);

    if (gst_structure_has_field (structure, "fontsize")) {
      gst_structure_get_int (structure, "fontsize", &fontsize);
      timestamp->fontsize = fontsize;
    }

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
  gint x = -1, y = -1, color = 0, fontsize = 0;
  gboolean changed = FALSE;

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

      string->position.x = GST_OVERLAY_DEFAULT_X;
      string->position.y = GST_OVERLAY_DEFAULT_Y;

      string->color = GST_OVERLAY_DEFAULT_COLOR;
      string->fontsize = GST_OVERLAY_DEFAULT_FONTSIZE;
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

      x = g_value_get_int (gst_value_array_get_value (position, 0));
      y = g_value_get_int (gst_value_array_get_value (position, 1));

      // Raise the flag for clearing cached blit if position is different.
      changed |= (string->position.x != x) || (string->position.y != y);

      string->position.x = x;
      string->position.y = y;
    }

    GST_TRACE ("%s: Position: [%d, %d]", name, string->position.x,
        string->position.y);

    if (gst_structure_has_field (structure, "color")) {
      gst_structure_get_int (structure, "color", &color);

      // Raise the flag for clearing cached blit if color is different.
      changed |= string->color != color;

      string->color = color;
    }

    GST_TRACE ("%s: Color: 0x%X", name, string->color);

    if (gst_structure_has_field (structure, "fontsize")) {
      gst_structure_get_int (structure, "fontsize", &fontsize);

      // Raise the flag for clearing cached blit if color is different.
      changed |= string->fontsize != fontsize;

      string->fontsize = fontsize;
    }


    GST_TRACE ("%s: Font size: %d", name, string->fontsize);

    // Clear the cached blit if the flag has been raised.
    if (changed && (string->blit.frame != NULL))
      gst_video_blit_release (&(string->blit));

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
  gint x = -1, y = -1, width = 0, height = 0, radius = 0, color = 0;
  gboolean changed = FALSE, infill = FALSE;

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
          !gst_structure_has_field (structure, "rectangle") &&
          !gst_structure_has_field (structure, "polygon")) {
        GST_ERROR ("Structure at idx %u does not contain neither 'circle' "
            "nor 'rectangle' or 'polygon' field!", idx);
        return FALSE;
      }

      // Resize the array which will create new entry at the end.
      g_array_set_size (masks, num + 1);
      mask = &(g_array_index (masks, GstOverlayMask, num));

      mask->name = gst_structure_get_name_id (structure);
      mask->enable = TRUE;
      mask->color = GST_OVERLAY_DEFAULT_COLOR;
      mask->infill = TRUE;

      memset(&(mask->dims), -1, sizeof (mask->dims));
    }

    name = g_quark_to_string (mask->name);

    if (gst_structure_has_field (structure, "circle")) {
      const GValue *circle = gst_structure_get_value (structure, "circle");

      if (gst_value_array_get_size (circle) != 3) {
        GST_ERROR ("Structure at idx %u has invalid 'circle' field!", idx);
        goto cleanup;
      }

      mask->type = GST_OVERLAY_MASK_CIRCLE;
      x = g_value_get_int (gst_value_array_get_value (circle, 0));
      y = g_value_get_int (gst_value_array_get_value (circle, 1));

      // Raise the flag for clearing cached blit if position is different.
      changed |= (mask->dims.circle.x != x) || (mask->dims.circle.y != y);

      mask->dims.circle.x = x;
      mask->dims.circle.y = y;

      radius = g_value_get_int (gst_value_array_get_value (circle, 2));

      if (radius == 0) {
        GST_ERROR ("Invalid radius for the circle at index %u", idx);
        goto cleanup;
      }

      // Raise the flag for clearing cached blit if radious is different.
      changed |= mask->dims.circle.radius != radius;

      mask->dims.circle.radius = radius;

      GST_TRACE ("%s: Circle radius: %d, centre: [%d, %d]", name,
          mask->dims.circle.radius, mask->dims.circle.x, mask->dims.circle.y);
    } else if (gst_structure_has_field (structure, "rectangle")) {
      const GValue *rectangle = gst_structure_get_value (structure, "rectangle");

      if (gst_value_array_get_size (rectangle) != 4) {
        GST_ERROR ("Structure at idx %u has invalid 'rectangle' field!", idx);
        goto cleanup;
      }

      mask->type = GST_OVERLAY_MASK_RECTANGLE;
      x = g_value_get_int (gst_value_array_get_value (rectangle, 0));
      y = g_value_get_int (gst_value_array_get_value (rectangle, 1));

      // Raise the flag for clearing cached blit if position is different.
      changed |= (mask->dims.rectangle.x != x) || (mask->dims.rectangle.y != y);

      mask->dims.rectangle.x = x;
      mask->dims.rectangle.y = y;

      width = g_value_get_int (gst_value_array_get_value (rectangle, 2));
      height = g_value_get_int (gst_value_array_get_value (rectangle, 3));

      if (width == 0 || height == 0) {
        GST_ERROR ("Invalid width and/or height for rectangle at idx %u", idx);
        goto cleanup;
      }

      // Raise the flag for clearing cached blit if dimensions are different.
      changed |= (mask->dims.rectangle.w != width) ||
          (mask->dims.rectangle.h != height);

      mask->dims.rectangle.w = width;
      mask->dims.rectangle.h = height;

      GST_TRACE ("%s: Rectangle coordinates: [%d, %d] dimensions: %dx%d", name,
          mask->dims.rectangle.x,  mask->dims.rectangle.y,
          mask->dims.rectangle.w, mask->dims.rectangle.h);
    } else if (gst_structure_has_field (structure, "polygon")) {
      const GValue *polygon = gst_structure_get_value (structure, "polygon");
      guint idx = 0, size = 0;

      size = gst_value_array_get_size (polygon);

      if ((size < GST_VIDEO_POLYGON_MIN_POINTS) ||
          (size > GST_VIDEO_POLYGON_MAX_POINTS)) {
        GST_ERROR ("Structure at idx %u has invalid 'polygon' field!", idx);
        goto cleanup;
      }

      mask->type = GST_OVERLAY_MASK_POLYGON;
      mask->dims.polygon.n_points = size;

      for (idx = 0; idx < size; idx++) {
        const GValue *point = gst_value_array_get_value (polygon, idx);

        x = g_value_get_int (gst_value_array_get_value (point, 0));
        y = g_value_get_int (gst_value_array_get_value (point, 1));

        // Initialize the X and Y axis with the first point.
        if (mask->dims.polygon.region.x == -1)
          mask->dims.polygon.region.x = x;

        if (mask->dims.polygon.region.y == -1)
          mask->dims.polygon.region.y = y;

        // Find the coordinates of the rectangle in which the polygon fits.
        mask->dims.polygon.region.x = MIN (mask->dims.polygon.region.x, x);
        mask->dims.polygon.region.y = MIN (mask->dims.polygon.region.y, y);
        mask->dims.polygon.region.w = MAX (mask->dims.polygon.region.w, x);
        mask->dims.polygon.region.h = MAX (mask->dims.polygon.region.h, y);

        // Raise the flag for clearing cached blit if any point is different.
        changed |= (mask->dims.polygon.points[idx].x != x) ||
            (mask->dims.polygon.points[idx].y != y);

        mask->dims.polygon.points[idx].x = x;
        mask->dims.polygon.points[idx].y = y;

        GST_TRACE ("%s: Polygon Coordinate: [%d, %d]", name,
            mask->dims.polygon.points[idx].x, mask->dims.polygon.points[idx].y);
      }

      // Adjust polygon region with margins due to draw lines width.
      mask->dims.polygon.region.x -= 2;
      mask->dims.polygon.region.y -= 2;

      mask->dims.polygon.region.w -= mask->dims.polygon.region.x - 2;
      mask->dims.polygon.region.h -= mask->dims.polygon.region.y - 2;

      GST_TRACE ("%s: Polygon Region: [%d, %d] %dx%d", name,
          mask->dims.polygon.region.x, mask->dims.polygon.region.y,
          mask->dims.polygon.region.w, mask->dims.polygon.region.h);
    }

    if (gst_structure_has_field (structure, "color")) {
      gst_structure_get_int (structure, "color", &color);

      // Raise the flag for clearing cached blit if color is different.
      changed |= mask->color != color;
      mask->color = color;
    }

    if (gst_structure_has_field (structure, "infill")) {
      gst_structure_get_boolean (structure, "infill", &infill);

      // Raise the flag for clearing cached blit if infill is changed.
      changed |= mask->infill != infill;
      mask->infill = infill;
    }

    GST_TRACE ("%s: Color: 0x%X, Infill: %s", name, mask->color,
        mask->infill ? "YES" : "NO");

    // Clear the cached blit if the flag has been raised.
    if (changed && (mask->blit.frame != NULL))
      gst_video_blit_release (&(mask->blit));

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
  gint x = -1, y = -1, width = 0, height = 0;
  gboolean changed = FALSE;

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
      const gchar *path = NULL;

      path = gst_structure_get_string (structure, "path");
      resolution = gst_structure_get_value (structure, "resolution");

      if (gst_value_array_get_size (resolution) != 2) {
        GST_ERROR ("Structure at idx %u has invalid 'resolution' field!", idx);
        goto cleanup;
      }

      // Raise the flag for clearing cached blit if path is different.
      changed |= g_strcmp0 (image->path, path) != 0;

      g_free (image->path);
      image->path = g_strdup (path);

      width = g_value_get_int (gst_value_array_get_value (resolution, 0));
      height = g_value_get_int (gst_value_array_get_value (resolution, 1));

      // Raise the flag for clearing cached blit if dimensions are different.
      changed |= (image->width != width) || (image->height != height);

      image->width = width;
      image->height = height;

      GST_TRACE ("%s: Path: '%s'", name, image->path);
      GST_TRACE ("%s: Dimensions: [%d, %d]", name, image->width, image->height);
    }

    if (gst_structure_has_field (structure, "destination")) {
      destination = gst_structure_get_value (structure, "destination");

      if (gst_value_array_get_size (destination) != 4) {
        GST_ERROR ("Structure at idx %u has invalid 'destination' field!", idx);
        goto cleanup;
      }

      x = g_value_get_int (gst_value_array_get_value (destination, 0));
      y = g_value_get_int (gst_value_array_get_value (destination, 1));
      width = g_value_get_int (gst_value_array_get_value (destination, 2));
      height = g_value_get_int (gst_value_array_get_value (destination, 3));

      // Raise the flag for clearing cached blit if destination is different.
      changed |= (image->destination.x != x) || (image->destination.y != y) ||
          (image->destination.w != width) || (image->destination.h != height);

      image->destination.x = x;
      image->destination.y = y;
      image->destination.w = width;
      image->destination.h = height;

      GST_TRACE ("%s: Destination: [%d, %d, %d, %d]", name, image->destination.x,
          image->destination.y, image->destination.w, image->destination.h);
    }

    // Clear the cached blit if the flag has been raised.
    if (changed && (image->blit.frame != NULL))
      gst_video_blit_release (&(image->blit));

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
        "color", G_TYPE_UINT, mask->color, "infill", G_TYPE_BOOLEAN,
        mask->infill, NULL);

    g_value_init (&value, G_TYPE_INT);
    g_value_init (&array, GST_TYPE_ARRAY);

    if (mask->type == GST_OVERLAY_MASK_CIRCLE) {
      g_value_set_int (&value, mask->dims.circle.x);
      gst_value_array_append_value (&array, &value);

      g_value_set_int (&value, mask->dims.circle.y);
      gst_value_array_append_value (&array, &value);

      g_value_set_int (&value, mask->dims.circle.radius);
      gst_value_array_append_value (&array, &value);

      gst_structure_set_value (entry, "circle", &array);
    } else if (mask->type == GST_OVERLAY_MASK_RECTANGLE) {
      g_value_set_int (&value, mask->dims.rectangle.x);
      gst_value_array_append_value (&array, &value);

      g_value_set_int (&value, mask->dims.rectangle.y);
      gst_value_array_append_value (&array, &value);

      g_value_set_int (&value, mask->dims.rectangle.w);
      gst_value_array_append_value (&array, &value);

      g_value_set_int (&value, mask->dims.rectangle.h);
      gst_value_array_append_value (&array, &value);

      gst_structure_set_value (entry, "rectangle", &array);
    } else if (mask->type == GST_OVERLAY_MASK_POLYGON) {
      GValue point = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&point, GST_TYPE_ARRAY);

      for (idx = 0; idx < mask->dims.polygon.n_points; idx++) {
        g_value_set_int (&value, mask->dims.polygon.points[idx].x);
        gst_value_array_append_value (&point, &value);

        g_value_set_int (&value, mask->dims.polygon.points[idx].y);
        gst_value_array_append_value (&point, &value);

        gst_value_array_append_value (&array, &point);
        g_value_reset (&point);
      }

      gst_structure_set_value (entry, "polygon", &array);
      g_value_unset (&point);
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
