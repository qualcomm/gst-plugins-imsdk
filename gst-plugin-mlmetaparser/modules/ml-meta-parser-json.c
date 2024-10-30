/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <math.h>

#include "parsermodule.h"

#include <gst/utils/common-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>
#include <json-glib/json-glib.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_parser_module_debug
GST_DEBUG_CATEGORY (gst_parser_module_debug);

#define OBJECT_DETECTION_NAME     "ObjectDetection"
#define IMAGE_CLASSIFICATION_NAME "ImageClassification"
#define POSE_ESTIMATION_NAME      "PoseEstimation"

#define GST_PARSER_SUB_MODULE_CAST(obj) ((GstParserSubModule*)(obj))

#define GST_META_IS_OBJECT_DETECTION(meta) \
    ((meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) && \
     (GST_VIDEO_ROI_META_CAST (meta)->roi_type != \
          g_quark_from_static_string ("ImageRegion")))

#define GST_META_IS_IMAGE_CLASSIFICATION(meta) \
    (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE)

#define GST_META_IS_POSE_ESTIMATION(meta) \
    (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE)

#define IS_OBJECT_DETECTION(id) \
    (id == g_quark_from_static_string (OBJECT_DETECTION_NAME))

#define IS_CLASSIFICATION(id) \
    (id == g_quark_from_static_string (IMAGE_CLASSIFICATION_NAME))

#define IS_POSE_ESTIMATION(id) \
    (id == g_quark_from_static_string (POSE_ESTIMATION_NAME))

typedef struct _GstParserSubModule GstParserSubModule;

struct _GstParserSubModule {
  GstDataType data_type;
};

static gboolean
gst_structure_to_json_append (JsonBuilder * builder,
    GstStructure * structure, gboolean is_name);

static gboolean
gst_array_to_json_append (JsonBuilder * builder,
    const GValue * value, const gchar * name);

static gboolean
gst_gvalue_to_json_append (JsonBuilder * builder,
    const GValue * value, gboolean is_name)
{
  if (G_VALUE_TYPE (value) == G_TYPE_STRING) {
    json_builder_add_string_value (builder, g_value_get_string (value));
  } else if (G_VALUE_TYPE (value) == GST_TYPE_STRUCTURE) {
    GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (value));
    gst_structure_to_json_append (builder, structure, is_name);
  } else if (G_VALUE_TYPE (value) == GST_TYPE_ARRAY) {
    gst_array_to_json_append (builder, value, NULL);
  } else if (G_VALUE_TYPE (value) == G_TYPE_INT) {
    json_builder_add_int_value (builder, g_value_get_int (value));
  } else if (G_VALUE_TYPE (value) == G_TYPE_UINT) {
    json_builder_add_int_value (builder, g_value_get_uint (value));
  } else if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE) {
    json_builder_add_double_value (builder, g_value_get_double (value));
  } else if (G_VALUE_TYPE (value) == G_TYPE_FLOAT) {
    json_builder_add_double_value (builder, g_value_get_float (value));
  } else {
    json_builder_add_string_value (builder, gst_value_serialize (value));
  }
  return TRUE;
}

static gboolean
gst_array_to_json_append (JsonBuilder * builder,
    const GValue * value, const gchar * name)
{
  guint idx;
  guint size = gst_value_array_get_size (value);

  if (name != NULL) json_builder_set_member_name (builder, name);

  json_builder_begin_array (builder);

  for (idx = 0; idx < size; idx++) {
    const GValue *val = gst_value_array_get_value (value, idx);
    gst_gvalue_to_json_append (builder, val, TRUE);
  }

  json_builder_end_array (builder);

  return TRUE;
}

static gboolean
gst_structure_json_serialize (GQuark field, const GValue * value,
    gpointer userdata)
{
  JsonBuilder *builder = (JsonBuilder *) userdata;

  const gchar *name = g_quark_to_string (field);

  g_return_val_if_fail (builder != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (name) json_builder_set_member_name (builder, name);

  gst_gvalue_to_json_append (builder, value, FALSE);

  return TRUE;
}

static gboolean
gst_structure_to_json_append (JsonBuilder * builder,
    GstStructure * structure, gboolean is_name)
{
  const gchar *name = NULL;

  g_return_val_if_fail (structure != NULL, FALSE);

  name = gst_structure_get_name (structure);

  json_builder_begin_object (builder);

  if (is_name) {
    json_builder_set_member_name (builder, "name");
    json_builder_add_string_value (builder, name);
  }

  gst_structure_foreach (structure, gst_structure_json_serialize, builder);

  json_builder_end_object (builder);

  return TRUE;
}

static gboolean
gst_parser_module_detection_meta_to_json_append (JsonBuilder * builder,
    GstBuffer * buffer, GstVideoMeta * vmeta,
    GstVideoRegionOfInterestMeta * roimeta)
{
  GstStructure *structure = NULL, *param = NULL;
  GstMeta *meta = NULL;
  GstVideoRegionOfInterestMeta *rmeta;
  GList *list = NULL;
  GArray *lndmrks = NULL;
  gpointer state = NULL;
  gdouble confidence = 0.0;
  guint color = 0x000000FF;
  gint len = 0, num = 0;
  guint tracking_id = 0;
  gboolean nested_detection = FALSE;
  gboolean has_landmarks = FALSE;
  gboolean has_image_classification = FALSE;

  structure = gst_video_region_of_interest_meta_get_param (roimeta,
      OBJECT_DETECTION_NAME);
  gst_structure_get_double (structure, "confidence", &confidence);
  gst_structure_get_uint (structure, "color", &color);

  json_builder_begin_object (builder);

  if (gst_structure_has_field (structure, "tracking-id")) {
    gst_structure_get_uint (structure, "tracking-id", &tracking_id);
    json_builder_set_member_name (builder, "tracking_id");
    json_builder_add_int_value (builder, tracking_id);
  }

  json_builder_set_member_name (builder, "label");
  json_builder_add_string_value (builder, g_quark_to_string (roimeta->roi_type));

  json_builder_set_member_name (builder, "confidence");
  json_builder_add_double_value (builder, confidence);

  json_builder_set_member_name (builder, "color");
  json_builder_add_int_value (builder, color);

  json_builder_set_member_name (builder, "rectangle");
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "x");
  json_builder_add_double_value (builder, (gdouble) roimeta->x / vmeta->width);
  json_builder_set_member_name (builder, "y");
  json_builder_add_double_value (builder, (gdouble) roimeta->y / vmeta->height);
  json_builder_set_member_name (builder, "width");
  json_builder_add_double_value (builder, (gdouble) roimeta->w / vmeta->width);
  json_builder_set_member_name (builder, "height");
  json_builder_add_double_value (builder, (gdouble) roimeta->h / vmeta->height);

  json_builder_end_object (builder);

  if (gst_structure_has_field (structure, "landmarks")) {
    lndmrks = g_value_get_boxed (gst_structure_get_value (structure, "landmarks"));

    len = (lndmrks) ? lndmrks->len : 0;

    json_builder_set_member_name (builder, "landmarks");
    json_builder_begin_object (builder);

    for (num = 0; num < len; num++) {
      GstVideoKeypoint *kp = &(g_array_index (lndmrks, GstVideoKeypoint, num));

      json_builder_set_member_name (builder, g_quark_to_string (kp->name));
      json_builder_begin_object (builder);

      json_builder_set_member_name (builder, "x");
      json_builder_add_double_value (builder,
          (gdouble) (kp->x + roimeta->x) / vmeta->width);

      json_builder_set_member_name (builder, "y");
      json_builder_add_double_value (builder,
          (gdouble) (kp->y + roimeta->y) / vmeta->height);

      json_builder_end_object (builder);
    }

    json_builder_end_object (builder);
  }

  for (list = roimeta->params; list != NULL; list = g_list_next (list)) {
    param = GST_STRUCTURE_CAST (list->data);
    if (gst_structure_has_name (param, "VideoLandmarks")) {
      has_landmarks = TRUE;
      break;
    }
  }

  for (list = roimeta->params; list != NULL; list = g_list_next (list)) {
    param = GST_STRUCTURE_CAST (list->data);
    if (gst_structure_has_name (param, IMAGE_CLASSIFICATION_NAME)) {
      has_image_classification = TRUE;
      break;
    }
  }

  if (has_landmarks) {
    json_builder_set_member_name (builder, "video_landmarks");
    json_builder_begin_array (builder);

    for (list = roimeta->params; list != NULL; list = g_list_next (list)) {
      param = GST_STRUCTURE_CAST (list->data);

      if (gst_structure_has_name (param, "VideoLandmarks")) {
        GArray *keypoints = NULL, *links = NULL;
        gint length = 0, num = 0;

        keypoints =
          g_value_get_boxed (gst_structure_get_value (param, "keypoints"));
        links = g_value_get_boxed (gst_structure_get_value (param, "links"));

        gst_structure_get_double (param, "confidence", &confidence);

        length = (keypoints != NULL) ? keypoints->len : 0;

        json_builder_begin_object (builder);

        json_builder_set_member_name (builder, "keypoints");
        json_builder_begin_array (builder);

        for (num = 0; num < length; num++) {
          GstVideoKeypoint *kp =
                &(g_array_index (keypoints, GstVideoKeypoint, num));

          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "keypoint");
          json_builder_add_string_value (builder, g_quark_to_string (kp->name));

          json_builder_set_member_name (builder, "x");
          json_builder_add_double_value (builder,
              (gdouble) (kp->x + roimeta->x) / vmeta->width);

          json_builder_set_member_name (builder, "y");
          json_builder_add_double_value (builder,
              (gdouble) (kp->y + roimeta->y) / vmeta->height);

          json_builder_set_member_name (builder, "confidence");
          json_builder_add_double_value (builder, kp->confidence);

          json_builder_set_member_name (builder, "color");
          json_builder_add_int_value (builder, kp->color);

          json_builder_end_object (builder);
        }

        json_builder_end_array (builder);
        json_builder_set_member_name (builder, "links");

        json_builder_begin_array (builder);

        length = (links != NULL) ? links->len : 0;

        for (num = 0; num < length; num++) {
          GstVideoKeypointLink *link =
            &(g_array_index (links, GstVideoKeypointLink, num));

          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "start");

          json_builder_add_int_value (builder, link->s_kp_idx);
          json_builder_set_member_name (builder, "end");

          json_builder_add_int_value (builder, link->d_kp_idx);
          json_builder_end_object (builder);
        }

        json_builder_end_array (builder);

        if (gst_structure_has_field (param, "xtraparams")) {
          GstStructure *xtraparams = NULL;

          xtraparams = GST_STRUCTURE (
              g_value_get_boxed (gst_structure_get_value (param, "xtraparams")));

          json_builder_set_member_name (builder, "xtraparams");
          gst_structure_to_json_append (builder, xtraparams, FALSE);
        }

        json_builder_end_object (builder);
      }
    }
    json_builder_end_array (builder);
  }

  if (has_image_classification) {
    json_builder_set_member_name (builder, "image_classification");
    json_builder_begin_array (builder);

    for (list = roimeta->params; list != NULL; list = g_list_next (list)) {
      param = GST_STRUCTURE_CAST (list->data);
      if (gst_structure_has_name (param, IMAGE_CLASSIFICATION_NAME)) {
        GArray *labels = NULL;
        gint length = 0, num = 0;

        labels = g_value_get_boxed (gst_structure_get_value (param, "labels"));

        length = (labels != NULL) ? labels->len : 0;

        for (num = 0; num < length; num++) {
          GstClassLabel *label = &(g_array_index (labels, GstClassLabel, num));

          json_builder_begin_object (builder);

          json_builder_set_member_name (builder, "label");
          json_builder_add_string_value (builder, g_quark_to_string (label->name));

          json_builder_set_member_name (builder, "confidence");
          json_builder_add_double_value (builder, label->confidence);

          json_builder_set_member_name (builder, "color");
          json_builder_add_int_value (builder, label->color);

          if (label->xtraparams != NULL) {
            json_builder_set_member_name (builder, "xtraparams");
            gst_structure_to_json_append (builder, label->xtraparams, FALSE);
          }

          json_builder_end_object (builder);
        }
      }
    }
    json_builder_end_array (builder);
  }

  while ((meta = gst_buffer_iterate_meta (buffer, &state)) != NULL) {
    if (GST_META_IS_OBJECT_DETECTION (meta)) {
      rmeta = GST_VIDEO_ROI_META_CAST(meta);

      if (roimeta->id == rmeta->parent_id) {
        if (nested_detection == FALSE) {
          json_builder_set_member_name (builder, "object_detection");
          json_builder_begin_array (builder);
          nested_detection = TRUE;
        }

        gst_parser_module_detection_meta_to_json_append (builder, buffer, vmeta,
            GST_VIDEO_ROI_META_CAST (meta));
      }
    }
  }

  if (nested_detection == TRUE) {
    json_builder_end_array (builder);
  }

  if (gst_structure_has_field (structure, "xtraparams")) {
    GstStructure * xtraparams = NULL;

    xtraparams = GST_STRUCTURE (
        g_value_get_boxed (gst_structure_get_value (structure, "xtraparams")));

    json_builder_set_member_name (builder, "xtraparams");
    gst_structure_to_json_append (builder, xtraparams, FALSE);
  }

  json_builder_end_object (builder);

  return TRUE;
}

static gboolean
gst_parser_module_image_classification_meta_to_json_append (JsonBuilder * builder,
    GstVideoClassificationMeta * meta)
{
  gint num = 0, length = 0;

  length = (meta->labels != NULL) ? (meta->labels)->len : 0;

  for (num = 0; num < length; num++) {
    GstClassLabel *label = &(g_array_index (meta->labels, GstClassLabel, num));

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "label");
    json_builder_add_string_value (builder, g_quark_to_string (label->name));

    json_builder_set_member_name (builder, "confidence");
    json_builder_add_double_value (builder, label->confidence);

    json_builder_set_member_name (builder, "color");
    json_builder_add_int_value (builder, label->color);

    if (label->xtraparams != NULL) {
      json_builder_set_member_name (builder, "xtraparams");
      gst_structure_to_json_append (builder, label->xtraparams, FALSE);
    }

    json_builder_end_object (builder);
  }

  return TRUE;
}

static gboolean
gst_parser_module_pose_estimation_meta_to_json_append (JsonBuilder * builder,
    GstVideoMeta * vmeta, GstVideoLandmarksMeta * meta)
{
  GArray *keypoints = GST_VIDEO_LANDMARKS_META_CAST (meta)->keypoints;
  GArray *links = GST_VIDEO_LANDMARKS_META_CAST (meta)->links;
  GstStructure *xtraparams = GST_VIDEO_LANDMARKS_META_CAST (meta)->xtraparams;
  gint num = 0, length = 0;

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "keypoints");
  json_builder_begin_array (builder);

  length = (keypoints != NULL) ? keypoints->len : 0;

  for (num = 0; num < length; num++) {
    GstVideoKeypoint *kp =
        &(g_array_index (keypoints, GstVideoKeypoint, num));

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "keypoint");
    json_builder_add_string_value (builder, g_quark_to_string (kp->name));

    json_builder_set_member_name (builder, "x");
    json_builder_add_double_value (builder, (double) kp->x / vmeta->width);

    json_builder_set_member_name (builder, "y");
    json_builder_add_double_value (builder, (double) kp->y / vmeta->height);

    json_builder_set_member_name (builder, "confidence");
    json_builder_add_double_value (builder, kp->confidence);

    json_builder_set_member_name (builder, "color");
    json_builder_add_int_value (builder, kp->color);

    json_builder_end_object (builder);
  }

  json_builder_end_array (builder);

  json_builder_set_member_name (builder, "links");
  json_builder_begin_array (builder);

  length = (links != NULL) ? links->len : 0;

  for (num = 0; num < length; num++) {
    GstVideoKeypointLink *link =
        &(g_array_index (links, GstVideoKeypointLink, num));

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "start");
    json_builder_add_int_value (builder, link->s_kp_idx);

    json_builder_set_member_name (builder, "end");
    json_builder_add_int_value (builder, link->d_kp_idx);

    json_builder_end_object (builder);
  }

  json_builder_end_array (builder);

  if (xtraparams != NULL) {
    json_builder_set_member_name (builder, "xtraparams");
    gst_structure_to_json_append (builder, xtraparams, FALSE);
  }

  json_builder_end_object (builder);

  return TRUE;
}

gboolean
gst_detection_text_metadata_to_json_append (JsonBuilder * builder,
    GstStructure * structure)
{
  const GValue *bboxes = NULL;
  guint size = 0, idx = 0;

  if (!gst_structure_has_name (structure, OBJECT_DETECTION_NAME)) {
    return FALSE;
  }

  bboxes = gst_structure_get_value (structure, "bounding-boxes");

  size = gst_value_array_get_size (bboxes);

  for (idx = 0; idx < size; idx++) {
    const GValue *value = NULL;
    GstStructure *entry = NULL;
    const gchar *label = NULL;
    gdouble confidence = 0.0;
    guint color = 0x000000FF, tracking_id = 0;
    gdouble x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    gint length = 0;

    value = gst_value_array_get_value (bboxes, idx);
    entry = GST_STRUCTURE (g_value_get_boxed (value));

    value = gst_structure_get_value (entry, "rectangle");

    x = g_value_get_float (gst_value_array_get_value (value, 0));
    y = g_value_get_float (gst_value_array_get_value (value, 1));
    width = g_value_get_float (gst_value_array_get_value (value, 2));
    height = g_value_get_float (gst_value_array_get_value (value, 3));

    label = gst_structure_get_name (entry);

    gst_structure_get_double (entry, "confidence", &confidence);
    gst_structure_get_uint (entry, "color", &color);

    json_builder_begin_object (builder);

    if (gst_structure_has_field (entry, "tracking-id")) {
      gst_structure_get_uint (entry, "tracking-id", &tracking_id);
      json_builder_set_member_name (builder, "tracking_id");
      json_builder_add_int_value (builder, tracking_id);
    }

    json_builder_set_member_name (builder, "label");
    json_builder_add_string_value (builder, label);

    json_builder_set_member_name (builder, "confidence");
    json_builder_add_double_value (builder, confidence);

    json_builder_set_member_name (builder, "color");
    json_builder_add_int_value (builder, color);

    json_builder_set_member_name (builder, "rectangle");
    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "x");
    json_builder_add_double_value (builder, x);

    json_builder_set_member_name (builder, "y");
    json_builder_add_double_value (builder, y);

    json_builder_set_member_name (builder, "width");
    json_builder_add_double_value (builder, width);

    json_builder_set_member_name (builder, "height");
    json_builder_add_double_value (builder, height);

    json_builder_end_object (builder);

    value = gst_structure_get_value (entry, "landmarks");
    length = (value != NULL) ? gst_value_array_get_size (value) : 0;

    if (length > 0) {
      gint num = 0;

      json_builder_set_member_name (builder, "landmarks");
      json_builder_begin_object (builder);

      for (num = 0; num < length; num++) {
        GstStructure *param = NULL;
        gchar *label = NULL;
        gdouble lx = 0.0, ly = 0.0;

        param = GST_STRUCTURE (
            g_value_get_boxed (gst_value_array_get_value (value, num)));

        label = g_strdup (gst_structure_get_name (param));

        gst_structure_get_double (param, "x", &lx);
        gst_structure_get_double (param, "y", &ly);

        lx = x + lx * width;
        ly = y + ly * height;

        json_builder_set_member_name (builder, label);
        json_builder_begin_object (builder);

        json_builder_set_member_name (builder, "x");
        json_builder_add_double_value (builder, lx);

        json_builder_set_member_name (builder, "y");
        json_builder_add_double_value (builder, ly);

        json_builder_end_object (builder);

        g_free (label);
      }

      json_builder_end_object (builder);
    }

    if (gst_structure_has_field (entry, "xtraparams")) {
      GstStructure *xtraparams = NULL;

      xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (entry, "xtraparams")));

      json_builder_set_member_name (builder, "xtraparams");
      gst_structure_to_json_append (builder, xtraparams, FALSE);
    }

    json_builder_end_object (builder);
  }
  return TRUE;
}

gboolean
gst_classification_text_metadata_to_json_append (JsonBuilder * builder,
    GstStructure * structure)
{
  const GValue *value;
  guint idx = 0;
  guint size = 0;

  if (!gst_structure_has_name (structure, IMAGE_CLASSIFICATION_NAME)) {
    return FALSE;
  }

  value = gst_structure_get_value (structure, "labels");
  size = gst_value_array_get_size (value);

  if (size == 0) return TRUE;

  for (idx = 0; idx < size; idx++) {
    const GValue *val = NULL;
    GstStructure *entry = NULL;
    const gchar *name = NULL;
    gdouble confidence = 0.0;
    guint color = 0x000000FF;

    val = gst_value_array_get_value (value, idx);
    entry = GST_STRUCTURE (g_value_get_boxed (val));

    name = gst_structure_get_name (entry);

    gst_structure_get_double (entry, "confidence", &confidence);
    gst_structure_get_uint (entry, "color", &color);

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "label");
    json_builder_add_string_value (builder, name);

    json_builder_set_member_name (builder, "confidence");
    json_builder_add_double_value (builder, confidence);

    json_builder_set_member_name (builder, "color");
    json_builder_add_int_value (builder, color);

    if (gst_structure_has_field (entry, "xtraparams")) {
      GstStructure *xtraparams = NULL;

      xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (entry, "xtraparams")));

      json_builder_set_member_name (builder, "xtraparams");
      gst_structure_to_json_append (builder, xtraparams, FALSE);
    }

    json_builder_end_object (builder);
  }

  return TRUE;
}

gint
gst_find_keypoint_index (const GValue * value, const gchar * name)
{
  gint num = 0, length = 0;

  length = gst_value_array_get_size (value);

  for (num = 0; num < length; num++) {
      GstStructure *keypoint = NULL;
      const GValue *val;
      gchar *kp_name = NULL;

      val = gst_value_array_get_value (value, num);
      keypoint = GST_STRUCTURE (g_value_get_boxed (val));

      kp_name = g_strdup (gst_structure_get_name (keypoint));
      g_strdelimit (kp_name, ".", ' ');

      if (!strcmp (kp_name, name)) {
        g_free (kp_name);
        return num;
      }
      g_free (kp_name);
  }
  return -1;
}

gboolean
gst_pose_estimation_text_metadata_to_json_append (JsonBuilder * builder,
    GstStructure * structure)
{
  const GValue *value;
  guint idx = 0;
  guint size = 0;

  if (!gst_structure_has_name (structure, POSE_ESTIMATION_NAME)) {
    return FALSE;
  }

  value = gst_structure_get_value (structure, "poses");
  size = gst_value_array_get_size (value);

  if (size == 0) return TRUE;

  for (idx = 0; idx < size; idx++) {
    const GValue *val = NULL, *kp_value = NULL;
    GstStructure *pose = NULL;
    guint length = 0;
    guint num = 0;

    val = gst_value_array_get_value (value, idx);
    pose = GST_STRUCTURE (g_value_get_boxed (val));

    kp_value = gst_structure_get_value (pose, "keypoints");

    length = gst_value_array_get_size (kp_value);

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "keypoints");
    json_builder_begin_array (builder);

    for (num = 0; num < length; num++) {
      GstStructure *keypoint = NULL;
      const gchar *name = NULL;
      gdouble x = 0.0, y = 0.0, confidence = 0.0;
      guint color = 0x000000FF;

      value = gst_value_array_get_value (kp_value, num);
      keypoint = GST_STRUCTURE (g_value_get_boxed (value));

      name = gst_structure_get_name (keypoint);

      gst_structure_get_double (keypoint, "x", &x);
      gst_structure_get_double (keypoint, "y", &y);

      gst_structure_get_double (keypoint, "confidence", &confidence);

      gst_structure_get_uint (keypoint, "color", &color);

      json_builder_begin_object (builder);

      json_builder_set_member_name (builder, "keypoint");
      json_builder_add_string_value (builder, name);

      json_builder_set_member_name (builder, "x");
      json_builder_add_double_value (builder, x);

      json_builder_set_member_name (builder, "y");
      json_builder_add_double_value (builder, y);

      json_builder_set_member_name (builder, "confidence");
      json_builder_add_double_value (builder, confidence);

      json_builder_set_member_name (builder, "color");
      json_builder_add_int_value (builder, color);

      json_builder_end_object (builder);
    }

    json_builder_end_array (builder);

    val = gst_structure_get_value (pose, "connections");
    length = gst_value_array_get_size (val);

    json_builder_set_member_name (builder, "links");
    json_builder_begin_array (builder);

    for (num = 0; num < length; num++) {
      const gchar *start = NULL, *end = NULL;

      value = gst_value_array_get_value (val, num);
      start = g_value_get_string (gst_value_array_get_value (value, 0));
      end = g_value_get_string (gst_value_array_get_value (value, 1));

      json_builder_begin_object (builder);

      json_builder_set_member_name (builder, "start");
      json_builder_add_int_value (builder, gst_find_keypoint_index (kp_value, start));

      json_builder_set_member_name (builder, "end");
      json_builder_add_int_value (builder, gst_find_keypoint_index (kp_value, end));

      json_builder_end_object (builder);
    }

    json_builder_end_array (builder);

    if (gst_structure_has_field (pose, "xtraparams")) {
      GstStructure *xtraparams = NULL;

      xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (pose, "xtraparams")));

      json_builder_set_member_name (builder, "xtraparams");
      gst_structure_to_json_append (builder, xtraparams, FALSE);
    }

    json_builder_end_object (builder);
  }

  return TRUE;
}

static gboolean
gst_parser_module_set_output (JsonBuilder * builder, gchar ** output)
{
  JsonNode *root = NULL;
  JsonGenerator *generator = NULL;

  root = json_builder_get_root (builder);
  g_return_val_if_fail (root != NULL, FALSE);

  generator = json_generator_new ();
  if (NULL == generator) {
    GST_ERROR ("Failed to create JSON generator!");
    json_node_free (root);
    return FALSE;
  }

  json_generator_set_root (generator, root);
  *output = json_generator_to_data (generator, NULL);

  g_object_unref (generator);
  json_node_free (root);
  return TRUE;
}

static void
gst_parser_module_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_parser_module_debug,
        "mlmetaparsermodule", 0, "QTI ML meta parser module");
    g_once_init_leave (&catonce, TRUE);
  }
}

gpointer
gst_parser_module_open (void)
{
  GstParserSubModule *submodule = NULL;

  submodule = g_slice_new0 (GstParserSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  // Initialize the debug category.
  gst_parser_module_initialize_debug_category ();

  return (gpointer) submodule;
}

void
gst_parser_module_close (gpointer instance)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (instance);

  g_slice_free (GstParserSubModule, submodule);
}

gboolean
gst_parser_module_configure (gpointer instance, GstStructure * settings)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (instance);
  GstDataType data_type;

  g_return_val_if_fail (settings != NULL, FALSE);
  g_return_val_if_fail (submodule != NULL, FALSE);

  gst_structure_get (settings, GST_PARSER_MODULE_OPT_DATA_TYPE, G_TYPE_ENUM,
      &data_type, NULL);

  submodule->data_type = data_type;

  return TRUE;
}

gboolean
gst_parser_module_process (gpointer instance, GstBuffer * inbuffer,
    gchar ** output)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (instance);
  JsonBuilder *json_builder = NULL;
  gchar *timestamp = NULL;
  gboolean has_object_detection = FALSE;
  gboolean has_image_classification = FALSE;
  gboolean has_posenet = FALSE;
  gboolean success = TRUE;

  json_builder = json_builder_new ();
  g_return_val_if_fail (json_builder != NULL, FALSE);

  timestamp = g_strdup_printf("%lu", GST_BUFFER_PTS (inbuffer));

  if (submodule->data_type == GST_DATA_TYPE_TEXT) {
    GstStructure *structure = NULL;
    GstMapInfo memmap;
    GValue list = G_VALUE_INIT;
    guint idx = 0;
    gchar *input_text = NULL;

    success = gst_buffer_map (inbuffer, &memmap, GST_MAP_READ);

    if (!success) {
      GST_ERROR ("Unable to map buffer!");
      goto cleanup;
    }

    GST_TRACE ("Text metadata: %s", memmap.data);

    // Copy of the buffer's data is needed because gst_value_deserialize()
    // modifies the given data by placing null character at the end of the string.
    // This causes data loss when two plugins are modifying the same buffer data.
    input_text = g_strndup ((gchar *) memmap.data, memmap.size);
    gst_buffer_unmap (inbuffer, &memmap);

    g_value_init (&list, GST_TYPE_LIST);
    success = gst_value_deserialize (&list, input_text);
    g_free (input_text);

    if (!success) {
      GST_ERROR ("Failed to deserialize");
      g_value_unset (&list);
      goto cleanup;
    }

    for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
      GQuark id = 0;
      structure = GST_STRUCTURE (
          g_value_get_boxed (gst_value_list_get_value (&list, idx)));
      id = gst_structure_get_name_id (structure);

      if (IS_OBJECT_DETECTION (id)) has_object_detection = TRUE;
      if (IS_CLASSIFICATION (id)) has_image_classification = TRUE;
      if (IS_POSE_ESTIMATION (id)) has_posenet = TRUE;
    }

    json_builder_begin_object (json_builder);

    if (has_object_detection) {
      json_builder_set_member_name (json_builder, "object_detection");
      json_builder_begin_array (json_builder);

      for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
        GQuark id = 0;
        structure = GST_STRUCTURE (
            g_value_get_boxed (gst_value_list_get_value (&list, idx)));

        id = gst_structure_get_name_id (structure);

        if (IS_OBJECT_DETECTION (id))
          gst_detection_text_metadata_to_json_append (json_builder, structure);

      }

      json_builder_end_array (json_builder);
    }

    if (has_image_classification) {
      json_builder_set_member_name (json_builder, "image_classification");
      json_builder_begin_array (json_builder);

      for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
        GQuark id = 0;
        structure = GST_STRUCTURE (
            g_value_get_boxed (gst_value_list_get_value (&list, idx)));

        id = gst_structure_get_name_id (structure);

        if (IS_CLASSIFICATION (id))
          gst_classification_text_metadata_to_json_append (json_builder,
              structure);

      }

      json_builder_end_array (json_builder);
    }

    if (has_posenet) {
      json_builder_set_member_name (json_builder, "video_landmarks");
      json_builder_begin_array (json_builder);

      for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
        GQuark id = 0;
        structure = GST_STRUCTURE (
            g_value_get_boxed (gst_value_list_get_value (&list, idx)));

        id = gst_structure_get_name_id (structure);

        if (IS_POSE_ESTIMATION (id))
          gst_pose_estimation_text_metadata_to_json_append (json_builder,
              structure);

      }

      json_builder_end_array (json_builder);
    }

    json_builder_set_member_name (json_builder, "parameters");
    json_builder_begin_object (json_builder);
    json_builder_set_member_name (json_builder, "timestamp");
    json_builder_add_string_value  (json_builder, timestamp);
    json_builder_end_object (json_builder);

    json_builder_end_object (json_builder);

    g_value_unset (&list);

    success = gst_parser_module_set_output (json_builder, output);
    if (!success) {
      GST_ERROR ("Failed to set module output for text mode!");
      goto cleanup;
    }

  } else if (submodule->data_type == GST_DATA_TYPE_VIDEO) {
    GstMeta *meta = NULL;
    GstVideoRegionOfInterestMeta *rmeta = NULL;
    GstVideoMeta *vmeta = NULL;
    gpointer state = NULL;

    vmeta = gst_buffer_get_video_meta (inbuffer);
    if (NULL == vmeta) {
      GST_ERROR ("Failed to get video meta!");
      goto cleanup;
    }

    while ((meta = gst_buffer_iterate_meta (inbuffer, &state)) != NULL) {

      if (GST_META_IS_OBJECT_DETECTION (meta))
        has_object_detection = TRUE;
      else if (GST_META_IS_IMAGE_CLASSIFICATION (meta))
        has_image_classification = TRUE;
      else if (GST_META_IS_POSE_ESTIMATION (meta))
        has_posenet = TRUE;

    }

    json_builder_begin_object (json_builder);

    if (has_object_detection) {
      json_builder_set_member_name (json_builder, "object_detection");
      json_builder_begin_array (json_builder);

      state = NULL;
      while ((meta = gst_buffer_iterate_meta (inbuffer, &state)) != NULL) {
        if (GST_META_IS_OBJECT_DETECTION (meta)) {
          rmeta = GST_VIDEO_ROI_META_CAST (meta);

          if (rmeta->parent_id == -1)
            gst_parser_module_detection_meta_to_json_append (json_builder,
                inbuffer, vmeta, GST_VIDEO_ROI_META_CAST (meta));

        }
      }

      json_builder_end_array (json_builder);
    }

    if (has_image_classification) {
      json_builder_set_member_name (json_builder, "image_classification");
      json_builder_begin_array (json_builder);

      state = NULL;
      while ((meta = gst_buffer_iterate_meta (inbuffer, &state)) != NULL) {
        if (GST_META_IS_IMAGE_CLASSIFICATION (meta))
          gst_parser_module_image_classification_meta_to_json_append (
            json_builder, GST_VIDEO_CLASSIFICATION_META_CAST (meta));

      }

      json_builder_end_array (json_builder);
    }

    if (has_posenet) {
      json_builder_set_member_name (json_builder, "video_landmarks");
      json_builder_begin_array (json_builder);

      state = NULL;
      while ((meta = gst_buffer_iterate_meta (inbuffer, &state)) != NULL) {
        if (GST_META_IS_POSE_ESTIMATION (meta))
          gst_parser_module_pose_estimation_meta_to_json_append (
            json_builder, vmeta, GST_VIDEO_LANDMARKS_META_CAST (meta));

      }

      json_builder_end_array (json_builder);
    }

    json_builder_set_member_name (json_builder, "parameters");
    json_builder_begin_object (json_builder);
    json_builder_set_member_name (json_builder, "timestamp");
    json_builder_add_string_value  (json_builder, timestamp);
    json_builder_end_object (json_builder);

    json_builder_end_object (json_builder);

    success = gst_parser_module_set_output (json_builder, output);
    if (!success) {
      GST_ERROR ("Failed to set module output for video mode!");
      goto cleanup;
    }
  }

cleanup:
  g_free (timestamp);
  g_object_unref (json_builder);

  return success ? TRUE : FALSE;
}
