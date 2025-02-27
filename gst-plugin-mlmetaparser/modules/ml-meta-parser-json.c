/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "parsermodule.h"

#include <math.h>

#include <json-glib/json-glib.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_parser_module_debug
GST_DEBUG_CATEGORY (gst_parser_module_debug);

#define GST_PARSER_SUB_MODULE_CAST(obj) ((GstParserSubModule*)(obj))

#define GST_JSON_BEGIN_META_ARRAY(builder, metalist, name) \
    if (metalist != NULL) {                                \
      json_builder_set_member_name (builder, name);        \
      json_builder_begin_array (builder);                  \
    }

#define GST_JSON_END_META_ARRAY(builder, metalist)         \
    if (metalist != NULL) {                                \
      json_builder_end_array (builder);         \
      g_clear_pointer (&metalist, g_list_free);            \
    }

typedef struct _GstParserSubModule GstParserSubModule;

struct _GstParserSubModule {
  JsonBuilder *builder;

  // The type of the incoming buffers.
  GstDataType datatype;
};

static void
gst_parser_module_process_structure (GstParserSubModule * submodule,
    const gchar * name, GstStructure * structure);

static gboolean
gst_parser_module_process_gvalue (GstParserSubModule *submodule,
    const gchar * name, const GValue * value)
{
  if (name != NULL)
    json_builder_set_member_name (submodule->builder, name);

  if (G_VALUE_HOLDS (value, G_TYPE_STRING)) {
    json_builder_add_string_value (submodule->builder, g_value_get_string (value));
  } else if (G_VALUE_HOLDS (value, GST_TYPE_STRUCTURE)) {
    GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (value));
    gst_parser_module_process_structure (submodule, name, structure);
  } else if (G_VALUE_HOLDS (value, GST_TYPE_ARRAY)) {
    guint idx = 0, length = 0;

    length = gst_value_array_get_size (value);
    json_builder_begin_array (submodule->builder);

    for (idx = 0; idx < length; idx++) {
      const GValue *val = gst_value_array_get_value (value, idx);
      gst_parser_module_process_gvalue (submodule, NULL, val);
    }

    json_builder_end_array (submodule->builder);
  } else if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
    json_builder_add_int_value (submodule->builder, g_value_get_int (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_UINT)) {
    json_builder_add_int_value (submodule->builder, g_value_get_uint (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_DOUBLE)) {
    json_builder_add_double_value (submodule->builder, g_value_get_double (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_FLOAT)) {
    json_builder_add_double_value (submodule->builder, g_value_get_float (value));
  } else {
    GST_ERROR ("Field %s has unknown value type!", name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_structure_process_field_value (GQuark field, const GValue * value,
    gpointer userdata)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (userdata);

  return gst_parser_module_process_gvalue (submodule,
      g_quark_to_string (field), value);
}

static GList *
gst_value_list_get_meta_structs (GValue * valist, GQuark name_id, gint parent_id)
{
  GList *metalist = NULL;
  GstStructure *structure = NULL;
  const GValue *value = NULL;
  guint idx = 0, length = 0;
  gint parent_meta_id = -1;

  length = gst_value_list_get_size (valist);

  for (idx = 0; idx < length; idx++) {
    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (valist, idx)));

    if (gst_structure_get_name_id (structure) != name_id)
      continue;

    value = gst_structure_get_value (structure, "parent-id");
    parent_meta_id = (value != NULL) ? g_value_get_int (value) : (-1);

    if (parent_id == parent_meta_id)
      metalist = g_list_prepend (metalist, structure);
  }
  return metalist;
}

static void
gst_parser_module_process_structure (GstParserSubModule * submodule,
    const gchar * name, GstStructure * structure)
{
  // Prefer name set from outside, otherwise use the structure name.
  if (name == NULL)
    name = gst_structure_get_name (structure);

  json_builder_set_member_name (submodule->builder, name);
  json_builder_begin_object (submodule->builder);

  gst_structure_foreach (structure, gst_structure_process_field_value, submodule);

  json_builder_end_object (submodule->builder);
}

static void
gst_parser_module_process_classification_structure (
    GstParserSubModule * submodule, GstStructure * structure)
{
  const GValue *labels = NULL, *value = NULL;
  GstStructure *params = NULL;
  gchar *name = NULL;
  guint idx = 0, length = 0, color = 0x000000FF;
  gdouble confidence = 0.0;

  labels = gst_structure_get_value (structure, "labels");
  length = gst_value_array_get_size (labels);

  for (idx = 0; idx < length; idx++) {
    value = gst_value_array_get_value (labels, idx);
    params = GST_STRUCTURE (g_value_get_boxed (value));

    name = g_strdup (gst_structure_get_name (params));
    name = g_strdelimit (name, ".", ' ');

    gst_structure_get_double (params, "confidence", &confidence);
    gst_structure_get_uint (params, "color", &color);

    json_builder_begin_object (submodule->builder);

    json_builder_set_member_name (submodule->builder, "label");
    json_builder_add_string_value (submodule->builder, name);
    json_builder_set_member_name (submodule->builder, "confidence");
    json_builder_add_double_value (submodule->builder, confidence);
    json_builder_set_member_name (submodule->builder, "color");
    json_builder_add_int_value (submodule->builder, color);

    if (gst_structure_has_field (params, "xtraparams")) {
      GstStructure *xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (params, "xtraparams")));

      gst_parser_module_process_structure (submodule, "xtraparams", xtraparams);
    }

    json_builder_end_object (submodule->builder);
    g_free (name);
  }
}

static void
gst_parser_module_process_pose_structure (GstParserSubModule * submodule,
    GstStructure * structure)
{
  GstStructure *params = NULL, *subparams = NULL;
  const GValue *poses = NULL, *keypoints = NULL, *links = NULL, *value = NULL;
  GHashTable *kp_names_table = NULL;
  gchar *name = NULL;
  guint idx = 0, length = 0, num = 0, size = 0, color = 0x000000FF;
  gdouble confidence = 0.0, x = 0.0, y = 0.0;

  poses = gst_structure_get_value (structure, "poses");
  length = gst_value_array_get_size (poses);

  for (idx = 0; idx < length; idx++) {
    value = gst_value_array_get_value (poses, idx);
    params = GST_STRUCTURE (g_value_get_boxed (value));

    gst_structure_get_double (params, "confidence", &confidence);

    keypoints = gst_structure_get_value (params, "keypoints");
    size = gst_value_array_get_size (keypoints);

    // Create a mapping between keypoint name and its index in the JSON array.
    kp_names_table = g_hash_table_new (NULL, NULL);

    json_builder_begin_object (submodule->builder);

    json_builder_set_member_name (submodule->builder, "confidence");
    json_builder_add_double_value (submodule->builder, confidence);

    json_builder_set_member_name (submodule->builder, "keypoints");
    json_builder_begin_array (submodule->builder);

    // Iterate over the keypoints GValue and create JSON entries.
    for (num = 0; num < size; num++) {
      value = gst_value_array_get_value (keypoints, num);
      subparams = GST_STRUCTURE (g_value_get_boxed (value));

      name = g_strdup (gst_structure_get_name (subparams));
      name = g_strdelimit (name, ".", ' ');

      // Add the keypoint name and index to the mapping for the links later.
      g_hash_table_insert (kp_names_table,
          GUINT_TO_POINTER (g_quark_from_string (name)), GUINT_TO_POINTER (num));

      gst_structure_get_double (subparams, "x", &x);
      gst_structure_get_double (subparams, "y", &y);
      gst_structure_get_uint (subparams, "color", &color);

      json_builder_begin_object (submodule->builder);

      json_builder_set_member_name (submodule->builder, "keypoint");
      json_builder_add_string_value (submodule->builder, name);
      json_builder_set_member_name (submodule->builder, "x");
      json_builder_add_double_value (submodule->builder, x);
      json_builder_set_member_name (submodule->builder, "y");
      json_builder_add_double_value (submodule->builder, y);
      json_builder_set_member_name (submodule->builder, "color");
      json_builder_add_int_value (submodule->builder, color);

      json_builder_end_object (submodule->builder);
      g_free (name);
    }

    json_builder_end_array (submodule->builder);

    if ((links = gst_structure_get_value (params, "connections")) != NULL) {
      size = gst_value_array_get_size (links);

      json_builder_set_member_name (submodule->builder, "links");
      json_builder_begin_array (submodule->builder);

      // Iterate over the keypoints GValue and create JSON entries.
      for (num = 0; num < size; num++) {
        const gchar *string = NULL;
        guint start = 0, end = 0;

        value = gst_value_array_get_value (links, num);

        // Extract the keypoint name and get the corresponding index from the map.
        string = g_value_get_string (gst_value_array_get_value (value, 0));
        start = GPOINTER_TO_UINT (g_hash_table_lookup (kp_names_table,
            GUINT_TO_POINTER (g_quark_from_string (string))));

        string = g_value_get_string (gst_value_array_get_value (value, 1));
        end = GPOINTER_TO_UINT (g_hash_table_lookup (kp_names_table,
            GUINT_TO_POINTER (g_quark_from_string (string))));

        json_builder_begin_object (submodule->builder);

        json_builder_set_member_name (submodule->builder, "start");
        json_builder_add_int_value (submodule->builder, start);
        json_builder_set_member_name (submodule->builder, "end");
        json_builder_add_int_value (submodule->builder, end);

        json_builder_end_object (submodule->builder);
      }

      json_builder_end_array (submodule->builder);
    }

    if (gst_structure_has_field (params, "xtraparams")) {
      GstStructure *xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (params, "xtraparams")));

      gst_parser_module_process_structure (submodule, "xtraparams", xtraparams);
    }

    json_builder_end_object (submodule->builder);
    g_hash_table_destroy (kp_names_table);
  }
}

static void
gst_parser_module_process_detection_structure (GstParserSubModule * submodule,
    GValue * valist, GstStructure * structure)
{
  GstStructure *params = NULL, *subparams = NULL;
  const GValue *bboxes = NULL, *landmarks = NULL, *value = NULL;
  GList *metalist = NULL, *list = NULL;
  gchar *name = NULL;
  guint id = 0, idx = 0, length = 0, num = 0, size = 0, color = 0x000000FF;
  guint tracking_id = 0;
  gdouble confidence = 0.0, x = 0.0, y = 0.0, width = 0.0, height = 0.0;
  gdouble lx = 0.0, ly = 0.0;

  bboxes = gst_structure_get_value (structure, "bounding-boxes");
  length = gst_value_array_get_size (bboxes);

  for (idx = 0; idx < length; idx++) {
    value = gst_value_array_get_value (bboxes, idx);
    params = GST_STRUCTURE (g_value_get_boxed (value));

    value = gst_structure_get_value (params, "rectangle");

    x = g_value_get_float (gst_value_array_get_value (value, 0));
    y = g_value_get_float (gst_value_array_get_value (value, 1));
    width = g_value_get_float (gst_value_array_get_value (value, 2));
    height = g_value_get_float (gst_value_array_get_value (value, 3));

    gst_structure_get_double (params, "confidence", &confidence);
    gst_structure_get_uint (params, "color", &color);

    name = g_strdup (gst_structure_get_name (params));
    name = g_strdelimit (name, ".", ' ');

    json_builder_begin_object (submodule->builder);

    if (gst_structure_has_field (params, "tracking-id")) {
      gst_structure_get_uint (params, "tracking-id", &tracking_id);
      json_builder_set_member_name (submodule->builder, "tracking_id");
      json_builder_add_int_value (submodule->builder, tracking_id);
    }

    json_builder_set_member_name (submodule->builder, "label");
    json_builder_add_string_value (submodule->builder, name);
    json_builder_set_member_name (submodule->builder, "confidence");
    json_builder_add_double_value (submodule->builder, confidence);
    json_builder_set_member_name (submodule->builder, "color");
    json_builder_add_int_value (submodule->builder, color);

    json_builder_set_member_name (submodule->builder, "rectangle");
    json_builder_begin_object (submodule->builder);

    json_builder_set_member_name (submodule->builder, "x");
    json_builder_add_double_value (submodule->builder, x);
    json_builder_set_member_name (submodule->builder, "y");
    json_builder_add_double_value (submodule->builder, y);
    json_builder_set_member_name (submodule->builder, "width");
    json_builder_add_double_value (submodule->builder, width);
    json_builder_set_member_name (submodule->builder, "height");
    json_builder_add_double_value (submodule->builder, height);

    json_builder_end_object (submodule->builder);
    g_free (name);

    if ((landmarks = gst_structure_get_value (params, "landmarks")) != NULL) {
      size = gst_value_array_get_size (landmarks);

      json_builder_set_member_name (submodule->builder, "landmarks");
      json_builder_begin_object (submodule->builder);

      // Iterate over the landmarks GValue and create JSON entries.
      for (num = 0; num < size; num++) {
        value = gst_value_array_get_value (landmarks, num);
        subparams = GST_STRUCTURE (g_value_get_boxed (value));

        name = g_strdup (gst_structure_get_name (subparams));
        name = g_strdelimit (name, ".", ' ');

        gst_structure_get_double (subparams, "x", &lx);
        gst_structure_get_double (subparams, "y", &ly);

        lx = x + lx * width;
        ly = y + ly * height;

        json_builder_set_member_name (submodule->builder, name);
        json_builder_begin_object (submodule->builder);

        json_builder_set_member_name (submodule->builder, "x");
        json_builder_add_double_value (submodule->builder, lx);
        json_builder_set_member_name (submodule->builder, "y");
        json_builder_add_double_value (submodule->builder, ly);

        json_builder_end_object (submodule->builder);
        g_free (name);
      }

      json_builder_end_object (submodule->builder);
    }

    if (gst_structure_has_field (params, "xtraparams")) {
      GstStructure *xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (params, "xtraparams")));

      gst_parser_module_process_structure (submodule, "xtraparams", xtraparams);
    }

    // Get the ID of the meta struct for which to search child meta structs.
    gst_structure_get_uint (params, "id", &id);

    // Parse derived detection structs and add section if there are any available.
    metalist = gst_value_list_get_meta_structs (valist,
        g_quark_from_static_string ("ObjectDetection"), id);

    GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "object_detection");

    for (list = g_list_last (metalist); list != NULL; list = list->prev) {
      structure = GST_STRUCTURE (list->data);
      gst_parser_module_process_detection_structure (submodule, valist, structure);
    }

    GST_JSON_END_META_ARRAY (submodule->builder, metalist);

    // Parse derived pose structs and add section if there are any available.
    metalist = gst_value_list_get_meta_structs (valist,
        g_quark_from_static_string ("PoseEstimation"), id);

    GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "video_landmarks");

    for (list = g_list_last (metalist); list != NULL; list = list->prev) {
      structure = GST_STRUCTURE (list->data);
      gst_parser_module_process_pose_structure (submodule, structure);
    }

    GST_JSON_END_META_ARRAY (submodule->builder, metalist);

    // Parse derived class structs and add section if there are any available.
    metalist = gst_value_list_get_meta_structs (valist,
        g_quark_from_static_string ("ImageClassification"), id);

    GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "image_classification");

    for (list = g_list_last (metalist); list != NULL; list = list->prev) {
      structure = GST_STRUCTURE (list->data);
      gst_parser_module_process_classification_structure (submodule, structure);
    }

    GST_JSON_END_META_ARRAY (submodule->builder, metalist);

    json_builder_end_object (submodule->builder);
  }
}

static void
gst_parser_module_process_classification_meta (GstParserSubModule * submodule,
    GstVideoClassificationMeta * classmeta)
{
  GstClassLabel *label = NULL;
  guint idx = 0;

  for (idx = 0; idx < classmeta->labels->len; idx++) {
    label = &(g_array_index (classmeta->labels, GstClassLabel, idx));

    json_builder_begin_object (submodule->builder);

    json_builder_set_member_name (submodule->builder, "label");
    json_builder_add_string_value (submodule->builder,
        g_quark_to_string (label->name));

    json_builder_set_member_name (submodule->builder, "confidence");
    json_builder_add_double_value (submodule->builder, label->confidence);

    json_builder_set_member_name (submodule->builder, "color");
    json_builder_add_int_value (submodule->builder, label->color);

    if (label->xtraparams != NULL) {
      gst_parser_module_process_structure (submodule, "xtraparams",
          label->xtraparams);
    }

    json_builder_end_object (submodule->builder);
  }
}

static void
gst_parser_module_process_landmarks_meta (GstParserSubModule * submodule,
    GstVideoMeta * vmeta, GstVideoLandmarksMeta * lmkmeta)
{
  GstVideoKeypoint *kp = NULL;
  GstVideoKeypointLink *link = NULL;
  gdouble x = 0.0, y = 0.0;
  guint idx = 0;

  json_builder_begin_object (submodule->builder);

  json_builder_set_member_name (submodule->builder, "keypoints");
  json_builder_begin_array (submodule->builder);

  for (idx = 0; idx < lmkmeta->keypoints->len ; idx++) {
    kp = &(g_array_index (lmkmeta->keypoints, GstVideoKeypoint, idx));

    x = ((gdouble) kp->x) / vmeta->width;
    y = ((gdouble) kp->y) / vmeta->height;

    json_builder_begin_object (submodule->builder);

    json_builder_set_member_name (submodule->builder, "keypoint");
    json_builder_add_string_value (submodule->builder,
        g_quark_to_string (kp->name));

    json_builder_set_member_name (submodule->builder, "x");
    json_builder_add_double_value (submodule->builder, x);

    json_builder_set_member_name (submodule->builder, "y");
    json_builder_add_double_value (submodule->builder, y);

    json_builder_set_member_name (submodule->builder, "confidence");
    json_builder_add_double_value (submodule->builder, kp->confidence);

    json_builder_set_member_name (submodule->builder, "color");
    json_builder_add_int_value (submodule->builder, kp->color);

    json_builder_end_object (submodule->builder);
  }

  json_builder_end_array (submodule->builder);

  if (lmkmeta->links != NULL) {
    json_builder_set_member_name (submodule->builder, "links");
    json_builder_begin_array (submodule->builder);

    for (idx = 0; idx < lmkmeta->links->len; idx++) {
      link = &(g_array_index (lmkmeta->links, GstVideoKeypointLink, idx));

      json_builder_begin_object (submodule->builder);

      json_builder_set_member_name (submodule->builder, "start");
      json_builder_add_int_value (submodule->builder, link->s_kp_idx);

      json_builder_set_member_name (submodule->builder, "end");
      json_builder_add_int_value (submodule->builder, link->d_kp_idx);

      json_builder_end_object (submodule->builder);
    }

    json_builder_end_array (submodule->builder);
  }

  if (lmkmeta->xtraparams != NULL) {
    gst_parser_module_process_structure (submodule, "xtraparams",
        lmkmeta->xtraparams);
  }

  json_builder_end_object (submodule->builder);
}

static void
gst_parser_module_process_roi_meta (GstParserSubModule * submodule,
    GstBuffer * buffer, GstVideoMeta * vmeta, GstVideoRegionOfInterestMeta * roimeta)
{
  GstStructure *objparam = NULL;
  GList *metalist = NULL, *list = NULL;
  gdouble confidence = 0.0, x = 0.0, y = 0.0, width = 0.0, height = 0.0;
  guint idx = 0, color = 0, tracking_id = 0;

  objparam = gst_video_region_of_interest_meta_get_param (roimeta,
      "ObjectDetection");

  gst_structure_get_double (objparam, "confidence", &confidence);
  gst_structure_get_uint (objparam, "color", &color);

  x = ((gdouble) roimeta->x) / vmeta->width;
  y = ((gdouble) roimeta->y) / vmeta->height;
  width = ((gdouble) roimeta->w) / vmeta->width;
  height = ((gdouble) roimeta->h) / vmeta->height;

  json_builder_begin_object (submodule->builder);

  if (gst_structure_has_field (objparam, "tracking-id")) {
    gst_structure_get_uint (objparam, "tracking-id", &tracking_id);
    json_builder_set_member_name (submodule->builder, "tracking_id");
    json_builder_add_int_value (submodule->builder, tracking_id);
  }

  json_builder_set_member_name (submodule->builder, "label");
  json_builder_add_string_value (submodule->builder,
      g_quark_to_string (roimeta->roi_type));

  json_builder_set_member_name (submodule->builder, "confidence");
  json_builder_add_double_value (submodule->builder, confidence);

  json_builder_set_member_name (submodule->builder, "color");
  json_builder_add_int_value (submodule->builder, color);

  json_builder_set_member_name (submodule->builder, "rectangle");
  json_builder_begin_object (submodule->builder);

  json_builder_set_member_name (submodule->builder, "x");
  json_builder_add_double_value (submodule->builder, x);
  json_builder_set_member_name (submodule->builder, "y");
  json_builder_add_double_value (submodule->builder, y);
  json_builder_set_member_name (submodule->builder, "width");
  json_builder_add_double_value (submodule->builder, width);
  json_builder_set_member_name (submodule->builder, "height");
  json_builder_add_double_value (submodule->builder, height);

  json_builder_end_object (submodule->builder);

  if (gst_structure_has_field (objparam, "landmarks")) {
    GArray *landmarks = NULL;
    GstVideoKeypoint *kp = NULL;

    landmarks = g_value_get_boxed (
        gst_structure_get_value (objparam, "landmarks"));

    json_builder_set_member_name (submodule->builder, "landmarks");
    json_builder_begin_object (submodule->builder);

    for (idx = 0; idx < landmarks->len; idx++) {
      kp = &(g_array_index (landmarks, GstVideoKeypoint, idx));

      x = ((gdouble) kp->x) / vmeta->width;
      y = ((gdouble) kp->y) / vmeta->height;

      json_builder_set_member_name (submodule->builder,
          g_quark_to_string (kp->name));
      json_builder_begin_object (submodule->builder);

      json_builder_set_member_name (submodule->builder, "x");
      json_builder_add_double_value (submodule->builder, x);
      json_builder_set_member_name (submodule->builder, "y");
      json_builder_add_double_value (submodule->builder, y);

      json_builder_end_object (submodule->builder);
    }

    json_builder_end_object (submodule->builder);
  }

  if (gst_structure_has_field (objparam, "xtraparams")) {
    GstStructure *xtraparams = GST_STRUCTURE (
        g_value_get_boxed (gst_structure_get_value (objparam, "xtraparams")));

    gst_parser_module_process_structure (submodule, "xtraparams", xtraparams);
  }

  // Add all derived ROI metas if there are any.
  metalist =
      gst_buffer_get_video_region_of_interest_metas_parent_id (buffer, roimeta->id);
  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "object_detection");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    GstVideoRegionOfInterestMeta *rmeta = GST_VIDEO_ROI_META_CAST (list->data);
    gst_parser_module_process_roi_meta (submodule, buffer, vmeta, rmeta);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

  // Add all derived pose metas if there are any.
  metalist = gst_buffer_get_video_landmarks_metas_parent_id (buffer, roimeta->id);
  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "video_landmarks");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    GstVideoLandmarksMeta *lmkmeta = GST_VIDEO_LANDMARKS_META_CAST (list->data);
    gst_parser_module_process_landmarks_meta (submodule, vmeta, lmkmeta);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

  metalist =
      gst_buffer_get_video_classification_metas_parent_id (buffer, roimeta->id);
  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "image_classification");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    GstVideoClassificationMeta *classmeta =
        GST_VIDEO_CLASSIFICATION_META_CAST (list->data);

    gst_parser_module_process_classification_meta (submodule, classmeta);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);
  json_builder_end_object (submodule->builder);
}

static gboolean
gst_parser_module_process_text_buffer (GstParserSubModule * submodule,
    GstBuffer * buffer)
{
  GstStructure *structure = NULL;
  GList *metalist = NULL, *list = NULL;
  gchar *string = NULL;
  GValue valist = G_VALUE_INIT;
  GstMapInfo memmap = { 0, };
  gboolean success = TRUE;

  if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
    GST_ERROR ("Failed to map %" GST_PTR_FORMAT "!", buffer);
    return FALSE;
  }

  // Copy of the buffer's data is needed because gst_value_deserialize()
  // modifies the given data by placing null character at the end of the string.
  // This causes data loss when two plugins are modifying the same buffer data.
  string = g_strndup ((gchar *) memmap.data, memmap.size);
  gst_buffer_unmap (buffer, &memmap);

  g_value_init (&valist, GST_TYPE_LIST);
  success = gst_value_deserialize (&valist, string);
  g_free (string);

  if (!success) {
    GST_ERROR ("Failed to deserialize input buffer!");
    goto cleanup;
  }

  // Parse root detection structs and add array section if there are any available.
  metalist = gst_value_list_get_meta_structs (&valist,
      g_quark_from_static_string ("ObjectDetection"), -1);

  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "object_detection");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    structure = GST_STRUCTURE (list->data);
    gst_parser_module_process_detection_structure (submodule, &valist, structure);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

  // Parse root pose structs and add array section if there are any available.
  metalist = gst_value_list_get_meta_structs (&valist,
      g_quark_from_static_string ("PoseEstimation"), -1);

  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "video_landmarks");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    structure = GST_STRUCTURE (list->data);
    gst_parser_module_process_pose_structure (submodule, structure);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

  // Parse root class structs and add array section if there are any available.
  metalist = gst_value_list_get_meta_structs (&valist,
      g_quark_from_static_string ("ImageClassification"), -1);

  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "image_classification");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    structure = GST_STRUCTURE (list->data);
    gst_parser_module_process_classification_structure (submodule, structure);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

cleanup:
  g_value_unset (&valist);
  return success;
}

static gboolean
gst_parser_module_process_video_buffer (GstParserSubModule * submodule,
    GstBuffer * buffer)
{
  GstVideoMeta *vmeta = NULL;
  GList *metalist = NULL, *list = NULL;

  // Extract the video meta, used for conversion to relative coordinates.
  if ((vmeta = gst_buffer_get_video_meta (buffer)) == NULL) {
    GST_ERROR ("Failed to get video meta from %" GST_PTR_FORMAT "!", buffer);
    return FALSE;
  }

  // Parse root ROI metas and add array section if there are any available.
  metalist = gst_buffer_get_video_region_of_interest_metas_parent_id (buffer, -1);
  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "object_detection");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    GstVideoRegionOfInterestMeta *roimeta = GST_VIDEO_ROI_META_CAST (list->data);
    gst_parser_module_process_roi_meta (submodule, buffer, vmeta, roimeta);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

  // Parse root pose metas and add array section if there are any available.
  metalist = gst_buffer_get_video_landmarks_metas_parent_id (buffer, -1);
  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "video_landmarks");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    GstVideoLandmarksMeta *lmkmeta = GST_VIDEO_LANDMARKS_META_CAST (list->data);
    gst_parser_module_process_landmarks_meta (submodule, vmeta, lmkmeta);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);

  // Parse root class metas and add array section if there are any available.
  metalist = gst_buffer_get_video_classification_metas_parent_id (buffer, -1);
  GST_JSON_BEGIN_META_ARRAY (submodule->builder, metalist, "image_classification");

  for (list = g_list_last (metalist); list != NULL; list = list->prev) {
    GstVideoClassificationMeta *classmeta =
        GST_VIDEO_CLASSIFICATION_META_CAST (list->data);

    gst_parser_module_process_classification_meta (submodule, classmeta);
  }

  GST_JSON_END_META_ARRAY (submodule->builder, metalist);
  return TRUE;
}

static void
gst_parser_module_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_parser_module_debug,
        "ml-metaparser-module", 0, "QTI ML meta parser module");
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

  if ((submodule->builder = json_builder_new ()) == NULL) {
    GST_ERROR ("Failed to allocate JSON builder!");
    g_slice_free (GstParserSubModule, submodule);
    return NULL;
  }

  return submodule;
}

void
gst_parser_module_close (gpointer instance)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (instance);

  g_object_unref (submodule->builder);
  g_slice_free (GstParserSubModule, submodule);
}

gboolean
gst_parser_module_configure (gpointer instance, GstStructure * settings)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (instance);

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  return gst_structure_get (settings, GST_PARSER_MODULE_OPT_DATA_TYPE,
      G_TYPE_ENUM, &(submodule->datatype), NULL);
}

gboolean
gst_parser_module_process (gpointer instance, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstParserSubModule *submodule = GST_PARSER_SUB_MODULE_CAST (instance);
  JsonNode *root = NULL;
  JsonGenerator *generator = NULL;
  gchar *string = NULL, *timestamp = NULL;
  guint size = 0;
  gboolean success = FALSE;

  json_builder_begin_object (submodule->builder);

  if (submodule->datatype == GST_DATA_TYPE_VIDEO)
    success = gst_parser_module_process_video_buffer (submodule, inbuffer);
  else if (submodule->datatype == GST_DATA_TYPE_TEXT)
    success = gst_parser_module_process_text_buffer (submodule, inbuffer);
  else
    GST_ERROR ("Unsupported data type!");

  // Add timestamp as string becuase JSON doesn't support 64 bit integer values.
  timestamp = g_strdup_printf("%" G_GINT64_FORMAT, GST_BUFFER_PTS (inbuffer));

  json_builder_set_member_name (submodule->builder, "parameters");
  json_builder_begin_object (submodule->builder);

  json_builder_set_member_name (submodule->builder, "timestamp");
  json_builder_add_string_value (submodule->builder, timestamp);

  json_builder_end_object (submodule->builder);
  g_free (timestamp);

  json_builder_end_object (submodule->builder);

  root = json_builder_get_root (submodule->builder);
  generator = json_generator_new ();

  json_generator_set_root (generator, root);
  string = json_generator_to_data (generator, NULL);

  size = strlen (string) + 1;
  gst_buffer_append_memory (outbuffer,
      gst_memory_new_wrapped (0, string, size, 0, size, string, g_free));

  GST_TRACE ("Size: %u, Output: '%s'", size, string);

  g_object_unref (generator);
  json_node_free (root);
  json_builder_reset (submodule->builder);

  return success ? TRUE : FALSE;
}
