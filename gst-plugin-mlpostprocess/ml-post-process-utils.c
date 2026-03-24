/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-post-process-utils.h"

GST_DEBUG_CATEGORY_EXTERN (gst_ml_post_process_debug);
#define GST_CAT_DEFAULT gst_ml_post_process_debug

void
gst_value_array_append_and_take_ml_structure (GValue * array, const guint id,
    GstStructure * structure)
{
  GValue value = G_VALUE_INIT;

  gst_structure_set (structure, "id", G_TYPE_UINT, id, NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);
  g_value_take_boxed (&value, structure);

  gst_value_array_append_and_take_value (array, &value);
}

void
gst_ml_predictions_list_append (GValue * list, const GQuark mltype,
    GValue * results, const GstStructure * mlparam)
{
  GstStructure *structure = NULL;
  const GValue *value = NULL;
  GValue entry = G_VALUE_INIT;

  if (GST_IS_AUDIO_CLASSIFICATION (mltype)) {
    structure = gst_structure_new_empty ("AudioClassification");
    gst_structure_take_value (structure, "labels", results);
  } else if (GST_IS_IMAGE_CLASSIFICATION (mltype)) {
    structure = gst_structure_new_empty ("ImageClassification");
    gst_structure_take_value (structure, "labels", results);
  } else if (GST_IS_DETECTION (mltype)) {
    structure = gst_structure_new_empty ("ObjectDetection");
    gst_structure_take_value (structure, "bounding-boxes", results);
  } else if (GST_IS_POSE (mltype)) {
    structure = gst_structure_new_empty ("PoseEstimation");
    gst_structure_take_value (structure, "poses", results);
  }

  value = gst_structure_get_value (mlparam, "timestamp");
  gst_structure_set_value (structure, "timestamp", value);

  value = gst_structure_get_value (mlparam, "sequence-index");
  gst_structure_set_value (structure, "sequence-index", value);

  value = gst_structure_get_value (mlparam, "sequence-num-entries");
  gst_structure_set_value (structure, "sequence-num-entries", value);

  if ((value = gst_structure_get_value (mlparam, "stream-id")))
    gst_structure_set_value (structure, "stream-id", value);

  if ((value = gst_structure_get_value (mlparam, "stream-timestamp")))
    gst_structure_set_value (structure, "stream-timestamp", value);

  if ((value = gst_structure_get_value (mlparam, "parent-id")))
    gst_structure_set_value (structure, "parent-id", value);

  g_value_init (&entry, GST_TYPE_STRUCTURE);
  g_value_take_boxed (&entry, structure);

  gst_value_list_append_and_take_value (list, &entry);
}

gboolean
gst_buffer_serialize_and_take_value (GstBuffer * buffer, GValue * value)
{
  GstMemory *memory = NULL;
  gchar *string = NULL;
  guint length = 0;

  // Serialize the predictions list into string format.
  string = gst_value_serialize (value);
  g_value_unset (value);

  if (string == NULL) {
    GST_ERROR ("Failed serialize predictions structure!");
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  memory = gst_memory_new_wrapped (0, string, length, 0, length, string, g_free);
  gst_buffer_append_memory (buffer, memory);

  return TRUE;
}

gboolean
gst_cairo_draw_setup (GstVideoFrame * vframe, cairo_surface_t ** surface,
    cairo_t ** context)
{
  cairo_format_t format;
  cairo_font_options_t *options = NULL;
  gdouble fontsize = DEFAULT_FONT_SIZE, linewidth = 2.0F;

  switch (GST_VIDEO_FRAME_FORMAT (vframe)) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:
      format = CAIRO_FORMAT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB16:
      format = CAIRO_FORMAT_RGB16_565;
      break;
    default:
      GST_ERROR ("Unsupported format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (vframe)));
      return FALSE;
  }

  *surface = cairo_image_surface_create_for_data (
      GST_VIDEO_FRAME_PLANE_DATA (vframe, 0), format,
      GST_VIDEO_FRAME_WIDTH (vframe), GST_VIDEO_FRAME_HEIGHT (vframe),
      GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0));
  g_return_val_if_fail (*surface, FALSE);

  *context = cairo_create (*surface);
  g_return_val_if_fail (*context, FALSE);

  // Select font.
  cairo_select_font_face (*context, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_antialias (*context, CAIRO_ANTIALIAS_BEST);

  // Set font options.
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);

  cairo_set_font_options (*context, options);
  cairo_font_options_destroy (options);

  cairo_set_line_width (*context, linewidth);
  cairo_set_font_size (*context, fontsize);

  // Clear any leftovers from previous operations.
  cairo_set_operator (*context, CAIRO_OPERATOR_CLEAR);
  cairo_paint (*context);
  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (*surface);

  // Set operator to draw over the source.
  cairo_set_operator (*context, CAIRO_OPERATOR_OVER);
  // Mark the surface dirty so Cairo clears its caches.
  cairo_surface_mark_dirty (*surface);

  return TRUE;
}

void
gst_cairo_draw_cleanup (cairo_surface_t * surface, cairo_t * context)
{
  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (surface);

  cairo_destroy (context);
  cairo_surface_destroy (surface);
}

gboolean
gst_cairo_draw_label (cairo_t * context, const guint index,
    const gchar * label, const guint32 color)
{
  cairo_matrix_t matrix;
  guint length = 0, textcolor = 0;
  gdouble fontsize = 0;

  // Set text background color.
  cairo_set_source_rgba (context, GST_FLOAT_COLOR_BLUE (color),
      GST_FLOAT_COLOR_GREEN (color), GST_FLOAT_COLOR_RED (color),
      GST_FLOAT_COLOR_ALPHA (color));

  cairo_get_font_matrix (context, &matrix);
  fontsize = matrix.yy;

  length = strlen (label) * fontsize * 3.0F / 5.0F;

  cairo_rectangle (context, 0, (index * fontsize), length, fontsize);
  cairo_fill (context);

  // Choose the best contrasting text color to the background.
  textcolor = GST_COLOR_ALPHA (color);
  textcolor += ((GST_COLOR_BLUE (color) > 0x7F) ? 0x00 : 0xFF) << 8;
  textcolor += ((GST_COLOR_GREEN (color) > 0x7F) ? 0x00 : 0xFF) << 16;
  textcolor += ((GST_COLOR_RED (color) > 0x7F) ? 0x00 : 0xFF) << 24;

  cairo_set_source_rgba (context, GST_FLOAT_COLOR_BLUE (textcolor),
      GST_FLOAT_COLOR_GREEN (textcolor), GST_FLOAT_COLOR_RED (textcolor),
      GST_FLOAT_COLOR_ALPHA (textcolor));

  // (0,0) is at top left corner of the buffer and draw the string.
  cairo_move_to (context, 0, ((index + 1) * fontsize * 4.0F / 5.0F));
  cairo_show_text (context, label);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

gboolean
gst_cairo_draw_detection (cairo_t * context, GstMLDetection * detection,
    GstVideoRegionOfInterestMeta * roimeta)
{
  cairo_matrix_t matrix;
  const gchar *label = NULL;
  gdouble x = 0, y = 0, width = 0, height = 0, fontsize = 0;
  guint32 textcolor = 0;

  label = g_quark_to_string (detection->name);

  x = roimeta->x + (CLAMP (detection->left, 0.0f, 1.0f) * roimeta->w);
  y = roimeta->y + (CLAMP (detection->top, 0.0f, 1.0f) * roimeta->h);
  width = CLAMP (detection->right - detection->left, 0.0f, 1.0f) * roimeta->w;
  height = CLAMP (detection->bottom - detection->top, 0.0f, 1.0f) * roimeta->h;

  cairo_set_source_rgba (context, GST_FLOAT_COLOR_BLUE (detection->color),
      GST_FLOAT_COLOR_GREEN (detection->color),
      GST_FLOAT_COLOR_RED (detection->color),
      GST_FLOAT_COLOR_ALPHA (detection->color));

  GST_TRACE ("Object: '%s' [%.1f %.1f %.1f %.1f] Confidence %.2f", label,
      x, y, width, height, detection->confidence);

  // Set rectangle position and dimensions.
  cairo_rectangle (context, x, y, width, height);
  cairo_stroke (context);

  g_return_val_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

  cairo_get_font_matrix (context, &matrix);
  fontsize = matrix.yy;

  // Set the width and height of the label background rectangle.
  width = strlen (label) * fontsize * 3.0F / 5.0F;
  height = fontsize;

  // Calculate the X and Y position of the label.
  if ((y -= height) < 0.0)
    y = roimeta->y + roimeta->h;

  if ((x + width - 1) > (gdouble) roimeta->w)
    x = roimeta->x + roimeta->w - width;

  cairo_rectangle (context, (x - 1), y, width, height);
  cairo_fill (context);

  // Choose the best contrasting text color to the background.
  textcolor = GST_COLOR_ALPHA (detection->color);
  textcolor += ((GST_COLOR_BLUE (detection->color) > 0x7F) ? 0x00 : 0xFF) << 8;
  textcolor += ((GST_COLOR_GREEN (detection->color) > 0x7F) ? 0x00 : 0xFF) << 16;
  textcolor += ((GST_COLOR_RED (detection->color) > 0x7F) ? 0x00 : 0xFF) << 24;

  cairo_set_source_rgba (context, GST_FLOAT_COLOR_BLUE (textcolor),
      GST_FLOAT_COLOR_GREEN (textcolor), GST_FLOAT_COLOR_RED (textcolor),
      GST_FLOAT_COLOR_ALPHA (textcolor));

  // Set the starting position of the label text and draw the string.
  cairo_move_to (context, x, (y + (fontsize * 4.0F / 5.0F)));
  cairo_show_text (context, label);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

gboolean
gst_cairo_draw_keypoint (cairo_t * context, GstMLKeypoint * keypoint,
    GstVideoRegionOfInterestMeta * roimeta)
{
  gdouble x = 0, y = 0, linewidth = cairo_get_line_width (context);

  // Adjust coordinates based on the output buffer dimensions.
  x = roimeta->x + (CLAMP (keypoint->x, 0.0f, 1.0f) * roimeta->w);
  y = roimeta->y + (CLAMP (keypoint->y, 0.0f, 1.0f) * roimeta->h);

  cairo_set_source_rgba (context, GST_FLOAT_COLOR_BLUE (keypoint->color),
      GST_FLOAT_COLOR_GREEN (keypoint->color),
      GST_FLOAT_COLOR_RED (keypoint->color),
      GST_FLOAT_COLOR_ALPHA (keypoint->color));

  GST_TRACE ("Keypoint: '%s' [%.1f %.1f] Confidence %.2f",
      g_quark_to_string (keypoint->name), x, y, keypoint->confidence);

  // Set circle position and dimensions.
  cairo_arc (context, x, y, linewidth, 0, 2 * G_PI);
  cairo_fill (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

gboolean
gst_cairo_draw_link (cairo_t * context, GstMLKeypointLink * link,
    GstVideoRegionOfInterestMeta * roimeta)
{
  gdouble x = 0, y = 0, dx = 0, dy = 0;

  // Adjust coordinates based on the output buffer dimensions.
  x = roimeta->x + (CLAMP (link->l_kp.x, 0.0f, 1.0f) * roimeta->w);
  y = roimeta->y + (CLAMP (link->l_kp.y, 0.0f, 1.0f) * roimeta->h);

  dx = roimeta->x + (CLAMP (link->r_kp.x, 0.0f, 1.0f) * roimeta->w);
  dy = roimeta->y + (CLAMP (link->r_kp.y, 0.0f, 1.0f) * roimeta->h);

  cairo_set_source_rgba (context, GST_FLOAT_COLOR_BLUE (link->color),
      GST_FLOAT_COLOR_GREEN (link->color), GST_FLOAT_COLOR_RED (link->color),
      GST_FLOAT_COLOR_ALPHA (link->color));

  GST_TRACE ("Link: '%s' [%.1f x %.2f] <--> '%s' [%.1f x %.1f]",
      g_quark_to_string (link->l_kp.name), x, y,
      g_quark_to_string (link->r_kp.name), dx, dy);

  cairo_move_to (context, x, y);
  cairo_line_to (context, dx, dy);
  cairo_stroke (context);

  return (cairo_status (context) == CAIRO_STATUS_SUCCESS) ? TRUE : FALSE;
}

GstVideoRegionOfInterestMeta *
gst_buffer_setup_image_region (GstBuffer * buffer, const GstStructure * mlparam)
{
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);
  GstVideoRectangle region = { 0, };

  // Extract the source tensor region with actual data.
  gst_ml_structure_get_source_region (mlparam, &region);

  // Update the image region then add it to the buffer.
  if ((region.w * vmeta->height) > (region.h * vmeta->width)) {
    region.h = gst_util_uint64_scale_int (vmeta->width, region.h, region.w);
    region.w = vmeta->width;
  } else if ((region.w * vmeta->height) < (region.h * vmeta->width)) {
    region.w = gst_util_uint64_scale_int (vmeta->height, region.w, region.h);
    region.h = vmeta->height;
  } else {
    region.w = vmeta->width;
    region.h = vmeta->height;
  }

  GST_TRACE ("Region [%d %d %d %d]", region.x, region.y, region.w, region.h);

  // Add ROI meta with the actual part of the buffer filled with image data.
  return gst_buffer_add_video_region_of_interest_meta (buffer, "ImageRegion",
      region.x, region.y, region.w, region.h);
}

gboolean
gst_ml_structure_get_inverse_affine_matrix (const GstStructure * structure,
    gdouble matrix[3][3])
{
  const GValue *val = NULL;
  guint idx = 0, row = 0, col = 0;

  if ((val = gst_structure_get_value (structure, "inverse-affine-matrix")) == NULL)
    return FALSE;

  // Expected number of values is 9 for a 3x3 affine matrix.
  if (gst_value_array_get_size (val) != 9) {
    GST_ERROR ("Invalid number of values in the 'inverse-affine-matrix' field!");
    return FALSE;
  }

  for (idx = 0; idx < 9; idx++, row = (idx / 3), col = (idx % 3))
    matrix[row][col] = g_value_get_double (gst_value_array_get_value (val, idx));

  GST_TRACE ("Matrix [%f, %f, %f]", matrix[0][0], matrix[0][1], matrix[0][2]);
  GST_TRACE ("Matrix [%f, %f, %f]", matrix[1][0], matrix[1][1], matrix[1][2]);
  GST_TRACE ("Matrix [%f, %f, %f]", matrix[2][0], matrix[2][1], matrix[2][2]);
  return TRUE;
}
