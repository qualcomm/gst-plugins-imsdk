/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <math.h>

#include <gst/ml/gstmlmodule.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

#include <json-glib/json-glib.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define OBJECT_DETECTION_NAME "ObjectDetection"
#define IMAGE_CLASSIFICATION_NAME "ImageClassification"
#define POSE_ESTIMATION_NAME "PoseEstimation"
#define PARAMETERS_NAME "Parameters"


#define GST_REDIS_SUB_MODULE_CAST(obj) ((GstRedisSubModule*)(obj))

#define GST_VIDEO_ROI_META_CAST(obj)    ((GstVideoRegionOfInterestMeta *)(obj))

#define GST_META_IS_OBJECT_DETECTION(meta) \
    (meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)

#define GST_META_IS_IMAGE_CLASSIFICATION(meta) \
    (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE)

#define GST_META_IS_POSE_ESTIMATION(meta) \
    (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE)

typedef struct _GstRedisSubModule GstRedisSubModule;
typedef struct _BuilderInfo BuilderInfo;

typedef enum {
  GST_DATA_TYPE_NONE,
  GST_DATA_TYPE_VIDEO,
  GST_DATA_TYPE_TEXT,
} GstDataType;

struct _GstRedisSubModule {
  GstDataType data_type;
};

struct _BuilderInfo {
  JsonBuilder *builder;
  guint w_coef;
  guint h_coef;
};

static gboolean
gst_structure_to_json_append (GstStructure * structure, BuilderInfo * binfo,
    gboolean is_name_flag);

static gboolean
gst_array_to_json_append (const GValue * value, const gchar * name,
    BuilderInfo * binfo)
{
  guint idx;
  guint size = gst_value_array_get_size (value);
  JsonBuilder *builder = binfo->builder;

  if (name != NULL) json_builder_set_member_name (builder, name);

  json_builder_begin_array (builder);

  for (idx = 0; idx < size; idx++) {
    const GValue * val = gst_value_array_get_value (value, idx);

    if (G_VALUE_TYPE (val) == G_TYPE_STRING) {
      json_builder_add_string_value (builder, g_value_get_string (val));
    } else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE) {
      GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (val));
      gst_structure_to_json_append (structure, binfo, FALSE);
    } else if (G_VALUE_TYPE (val) == GST_TYPE_ARRAY) {
      gst_array_to_json_append (val, NULL, binfo);
    } else if (G_VALUE_TYPE (val) == G_TYPE_INT) {
      json_builder_add_int_value (builder, g_value_get_int (val));
    } else if (G_VALUE_TYPE (val) == G_TYPE_UINT) {
      json_builder_add_int_value (builder, g_value_get_uint (val));
    } else if (G_VALUE_TYPE (val) == G_TYPE_DOUBLE) {
      json_builder_add_double_value (builder, g_value_get_double (val));
    } else if (G_VALUE_TYPE (val) == G_TYPE_FLOAT) {
      json_builder_add_double_value (builder, g_value_get_float (val));
    } else {
      json_builder_add_string_value (builder, gst_value_serialize (value));
    }
  }

  json_builder_end_array (builder);
  return TRUE;
}

static gboolean
gst_structure_json_serialize (GQuark field, const GValue * value,
    gpointer userdata)
{
  BuilderInfo *binfo = (BuilderInfo *) userdata;
  JsonBuilder *builder = binfo->builder;
  gint num = 0;
  const gchar *name = g_quark_to_string (field);

  g_return_val_if_fail (builder != NULL, FALSE);

  if (G_VALUE_TYPE (value)  == GST_TYPE_ARRAY) {
    gst_array_to_json_append (value, name, binfo);
  } else if (G_VALUE_TYPE (value)  == G_TYPE_STRING) {
    json_builder_set_member_name (builder, g_quark_to_string (field));
    json_builder_add_string_value (builder, g_value_get_string (value));
  } else if (G_VALUE_TYPE (value) == G_TYPE_ARRAY && !strcmp (name, "keypoints")) {
    GArray *keypoints = g_value_get_boxed (value);
    gint length = (keypoints != NULL) ? keypoints->len : 0;

    json_builder_set_member_name (builder, name);
    json_builder_begin_array (builder);

    for (num = 0; num < length; num++) {
      GstVideoKeypoint *kp =
        &(g_array_index (keypoints, GstVideoKeypoint, num));
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "keypoint");
      json_builder_add_string_value (builder, g_quark_to_string (kp->name));
      json_builder_set_member_name (builder, "x");
      json_builder_add_double_value (builder, (double) kp->x / binfo->w_coef);
      json_builder_set_member_name (builder, "y");
      json_builder_add_double_value (builder, (double) kp->y / binfo->h_coef);
      json_builder_set_member_name (builder, "confidence");
      json_builder_add_double_value (builder, kp->confidence);
      json_builder_set_member_name (builder, "color");
      json_builder_add_int_value (builder, kp->color);
      json_builder_end_object (builder);
    }
    json_builder_end_array (builder);
  } else if (G_VALUE_TYPE (value) == G_TYPE_ARRAY && !strcmp (name, "links")) {
    GArray *links = g_value_get_boxed (value);
    gint length = (links != NULL) ? links->len : 0;

    json_builder_set_member_name (builder, name);
    json_builder_begin_array (builder);

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
  } else if (G_VALUE_TYPE (value) == G_TYPE_ARRAY && !strcmp (name, "labels")) {
    GArray *labels = g_value_get_boxed (value);
    gint length = (labels != NULL) ? labels->len : 0;

    json_builder_set_member_name (builder, name);
    json_builder_begin_array (builder);

    for (num = 0; num < length; num++) {
      GstClassLabel *label = &(g_array_index (labels, GstClassLabel, num));

      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "label");
      json_builder_add_string_value (builder, g_quark_to_string (label->name));
      json_builder_set_member_name (builder, "confidence");
      json_builder_add_double_value (builder, label->confidence);
      json_builder_set_member_name (builder, "color");
      json_builder_add_int_value (builder, label->color);
      json_builder_end_object (builder);
    }

    json_builder_end_array (builder);
  } else {
    json_builder_set_member_name (builder, g_quark_to_string (field));
    json_builder_add_string_value (builder, gst_value_serialize (value));
  }
  return TRUE;
}

static gboolean
gst_structure_to_json_append (GstStructure * structure, BuilderInfo * binfo,
    gboolean is_name_flag)
{
  JsonBuilder *builder = binfo->builder;
  const gchar *name = NULL;

  g_return_val_if_fail (structure != NULL, FALSE);

  name = gst_structure_get_name (structure);

  if (is_name_flag) {
    json_builder_set_member_name (builder, name);
    json_builder_begin_object (builder);
  } else {
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "name");
    json_builder_add_string_value (builder, name);
  }

  gst_structure_foreach (structure, gst_structure_json_serialize, binfo);

  json_builder_end_object (builder);

  return TRUE;
}

static gboolean
gst_list_to_json_append (GValue * list, BuilderInfo * binfo)
{
  GstStructure *structure = NULL;
  JsonBuilder *builder = binfo->builder;
  const gchar *name = NULL;
  guint idx;

  g_return_val_if_fail (list != NULL, FALSE);

  if (gst_value_list_get_size (list) == 0) return TRUE;

  structure = GST_STRUCTURE (
      g_value_get_boxed (gst_value_list_get_value (list, 0)));

  name = gst_structure_get_name (structure);

  json_builder_set_member_name (builder, name);
  json_builder_begin_array (builder);

  for (idx = 0; idx < gst_value_list_get_size (list); idx++) {
      structure = GST_STRUCTURE (
          g_value_get_boxed (gst_value_list_get_value (list, idx)));

      if (structure == NULL) {
        GST_WARNING ("Structure is NULL!");
        continue;
      }

      gst_structure_to_json_append (structure, binfo, FALSE);
  }

  json_builder_end_array (builder);

  return TRUE;
}

static gboolean
gst_module_detection_meta_to_json_append (GstBuffer * buffer,
    GstVideoMeta * vmeta, GstVideoRegionOfInterestMeta * roimeta,
    JsonBuilder * builder)
{
  GstStructure *structure = NULL;
  GstStructure *param = NULL;
  GstMeta *meta = NULL;
  GstVideoRegionOfInterestMeta *rmeta;
  gpointer state = NULL;
  gdouble confidence = 0.0;
  guint color = 0x000000FF;
  GList *list = NULL;
  gboolean nested_detection = FALSE;
  BuilderInfo binfo;

  binfo.builder = builder;
  binfo.w_coef = vmeta->width;
  binfo.h_coef = vmeta->height;

  GValue value = G_VALUE_INIT;
  GValue landmark = G_VALUE_INIT;
  GValue classification = G_VALUE_INIT;

  g_value_init (&landmark, GST_TYPE_LIST);
  g_value_init (&classification, GST_TYPE_LIST);
  g_value_init (&value, GST_TYPE_STRUCTURE);

  structure = gst_video_region_of_interest_meta_get_param (roimeta,
      OBJECT_DETECTION_NAME);
  gst_structure_get_double (structure, "confidence", &confidence);
  gst_structure_get_uint (structure, "color", &color);

  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "label");
  json_builder_add_string_value (builder, g_quark_to_string (roimeta->roi_type));
  json_builder_set_member_name (builder, "confidence");
  json_builder_add_double_value (builder, confidence);
  json_builder_set_member_name (builder, "color");
  json_builder_add_int_value (builder, color);
  json_builder_set_member_name (builder, "rectangle");
  json_builder_begin_array (builder);
  json_builder_add_double_value (builder, (double) roimeta->x / vmeta->width);
  json_builder_add_double_value (builder, (double) roimeta->y / vmeta->height);
  json_builder_add_double_value (builder, (double) roimeta->w / vmeta->width);
  json_builder_add_double_value (builder, (double) roimeta->h / vmeta->height);
  json_builder_end_array (builder);

  for (list = roimeta->params; list != NULL; list = g_list_next (list)) {
    GQuark id = 0;

    param = GST_STRUCTURE_CAST (list->data);
    id = gst_structure_get_name_id (param);

    const gchar *name = gst_structure_get_name (param);

    GST_LOG ("param name = %s", name);

    if (id == g_quark_from_static_string ("VideoLandmarks")) {
      GArray *keypoints = NULL, *links = NULL;
      gint length = 0;
      GstStructure *structure = NULL;

      keypoints = g_value_get_boxed (gst_structure_get_value (param, "keypoints"));
      links = g_value_get_boxed (gst_structure_get_value (param, "links"));
      gst_structure_get_double (param, "confidence", &confidence);

      length = (keypoints != NULL) ? keypoints->len : 0;

      if (length == 0) continue;

      GST_LOG ("keypoints length = %d", length);

      structure = gst_structure_new ("VideoLandmarks",
          "keypoints", G_TYPE_ARRAY, keypoints,
          "links", G_TYPE_ARRAY, links,
          "confidence", G_TYPE_DOUBLE, confidence, NULL);

      g_value_take_boxed (&value, structure);
      gst_value_list_append_value (&landmark, &value);
      g_value_reset (&value);

    } else if (id == g_quark_from_static_string (IMAGE_CLASSIFICATION_NAME)) {
      GArray *labels = NULL;
      gint length = 0;

      labels = g_value_get_boxed (gst_structure_get_value (param, "labels"));

      length = (labels != NULL) ? labels->len : 0;

      if (length == 0) continue;

      structure = gst_structure_new (IMAGE_CLASSIFICATION_NAME,
        "labels", G_TYPE_ARRAY, labels, NULL);

      g_value_take_boxed (&value, structure);
      gst_value_list_append_value (&classification, &value);
      g_value_reset (&value);
    }
  }

  if (gst_value_list_get_size (&landmark) > 0)
      gst_list_to_json_append (&landmark, &binfo);

  if (gst_value_list_get_size (&classification) > 0)
      gst_list_to_json_append (&classification, &binfo);

  while ((meta = gst_buffer_iterate_meta (buffer, &state)) != NULL) {
    if (GST_META_IS_OBJECT_DETECTION (meta)) {
      rmeta = GST_VIDEO_ROI_META_CAST(meta);

      if (roimeta->id == rmeta->parent_id) {
        if (nested_detection == FALSE) {
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, OBJECT_DETECTION_NAME);
          json_builder_begin_array (builder);
          nested_detection = TRUE;
        }

        gst_module_detection_meta_to_json_append (buffer, vmeta,
            GST_VIDEO_ROI_META_CAST (meta), builder);
      }
    }
  }

  if (nested_detection == TRUE) {
    json_builder_end_array (builder);
    json_builder_end_object (builder);
  }

  json_builder_end_object (builder);

  return TRUE;
}

static gboolean
gst_module_image_classification_meta_to_json_append (
    GstVideoClassificationMeta * meta, JsonBuilder * builder)
{
  gint num = 0, length = 0;

  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (builder != NULL, FALSE);

  length = (meta->labels != NULL) ? (meta->labels)->len : 0;

  json_builder_begin_object (builder);

  json_builder_begin_array (builder);
  for (num = 0; num < length; num++) {
    GstClassLabel *label = &(g_array_index (meta->labels, GstClassLabel, num));
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "label");
    json_builder_add_string_value (builder, g_quark_to_string (label->name));
    json_builder_set_member_name (builder, "confidence");
    json_builder_add_double_value (builder, label->confidence);
    json_builder_set_member_name (builder, "color");
    json_builder_add_int_value (builder, label->color);
    json_builder_end_object (builder);
  }
  json_builder_end_array (builder);

  json_builder_end_object (builder);
  return TRUE;
}

static gboolean
gst_module_pose_estimation_meta_to_json_append (
    GstVideoMeta * vmeta, GstVideoLandmarksMeta * meta, JsonBuilder * builder)
{
  gint num = 0, length = 0;
  GArray *keypoints = GST_VIDEO_LANDMARKS_META_CAST (meta)->keypoints;
  GArray *links = GST_VIDEO_LANDMARKS_META_CAST (meta)->links;

  g_return_val_if_fail (vmeta != NULL, FALSE);
  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (builder != NULL, FALSE);

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

  json_builder_begin_array (builder);

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
  json_builder_end_object (builder);

  return TRUE;
}

static gboolean
gst_module_set_output (gchar * name, JsonBuilder * builder,
    GstStructure * output)
{
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (builder != NULL, FALSE);
  g_return_val_if_fail (output != NULL, FALSE);

  JsonNode *root = NULL;
  JsonGenerator *generator = NULL;
  gchar *string = NULL;

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  string = json_generator_to_data (generator, NULL);

  gst_structure_set (output, name, G_TYPE_STRING, string, NULL);

  g_object_unref (generator);
  json_node_free (root);
  g_free (string);
  return TRUE;
}

gpointer
gst_ml_module_open (void)
{
  GstRedisSubModule *submodule = NULL;

  submodule = g_slice_new0 (GstRedisSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  return (gpointer) submodule;
}

void
gst_ml_module_close (gpointer instance)
{
  GstRedisSubModule *submodule = GST_REDIS_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  g_slice_free (GstRedisSubModule, submodule);
}

GstCaps *
gst_ml_module_caps (void)
{
  return NULL;
}

gboolean
gst_ml_module_configure (gpointer instance, GstStructure * settings)
{
  GstRedisSubModule *submodule = GST_REDIS_SUB_MODULE_CAST (instance);
  gboolean success = FALSE;
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  const gchar *caps_name = NULL;

  g_return_val_if_fail (settings != NULL, FALSE);
  g_return_val_if_fail (submodule != NULL, FALSE);

  if (!(success = gst_structure_has_field (settings, GST_ML_MODULE_OPT_CAPS))) {
    GST_ERROR ("Settings stucture does not contain configuration caps!");
    goto cleanup;
  }

  gst_structure_get (settings, GST_ML_MODULE_OPT_CAPS, GST_TYPE_CAPS, &caps, NULL);

  structure = gst_caps_get_structure (caps, 0);

  caps_name = gst_structure_get_name (structure);

  GST_LOG ("Caps: %s", caps_name);

  if (gst_structure_has_name (structure, "text/x-raw")) {
    submodule->data_type = GST_DATA_TYPE_TEXT;
  } else if (gst_structure_has_name (structure, "video/x-raw")) {
    submodule->data_type = GST_DATA_TYPE_VIDEO;
  } else {
    submodule->data_type = GST_DATA_TYPE_NONE;
  }

cleanup:

  return TRUE;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe, gpointer output)
{
  GstMapInfo buffer_info;
  GstMeta *meta = NULL;
  GstVideoRegionOfInterestMeta *rmeta;
  gpointer state = NULL;
  guint idx = 0;
  GValue object_detection = G_VALUE_INIT;
  GValue image_classification = G_VALUE_INIT;
  GValue pose_estimation = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  GValue list = G_VALUE_INIT;
  BuilderInfo binfo;
  GstVideoMeta *vmeta = NULL;
  gchar *timestamp = NULL;
  GstStructure *structure = NULL;

  JsonBuilder *object_detection_builder = json_builder_new ();
  JsonBuilder *image_classification_builder = json_builder_new ();
  JsonBuilder *pose_estimation_builder = json_builder_new ();
  JsonBuilder *parameters_builder = json_builder_new ();

  GstRedisSubModule *submodule = GST_REDIS_SUB_MODULE_CAST (instance);

  g_value_init (&object_detection, GST_TYPE_LIST);
  g_value_init (&image_classification, GST_TYPE_LIST);
  g_value_init (&pose_estimation, GST_TYPE_LIST);
  g_value_init (&value, GST_TYPE_STRUCTURE);
  g_value_init (&list, GST_TYPE_LIST);

  if (!gst_buffer_map (mlframe->buffer, &buffer_info, GST_MAP_READ)) {
    GST_ERROR ("Unable to map buffer!");
    return GST_FLOW_ERROR;
  }

  timestamp = g_strdup_printf("%lu", GST_BUFFER_PTS (mlframe->buffer));

  if (submodule->data_type == GST_DATA_TYPE_TEXT) {
    if (buffer_info.data == NULL) {
      GST_DEBUG ("Null data");
      return FALSE;
    }

    if (!gst_value_deserialize (&list, (gpointer) buffer_info.data)) {
      GST_WARNING ("Failed to deserialize");
      return FALSE;
    }

    binfo.w_coef = 1;
    binfo.h_coef = 1;

    for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
      const gchar *name;
      structure = GST_STRUCTURE (
          g_value_get_boxed (gst_value_list_get_value (&list, idx)));
      if (structure == NULL) {
        GST_WARNING ("Structure is NULL!");
        continue;
      }

      gst_structure_remove_field (structure, "sequence-index");
      gst_structure_remove_field (structure, "sequence-num-entries");
      gst_structure_remove_field (structure, "batch-index");
      g_value_take_boxed (&value, structure);

      name = gst_structure_get_name (structure);
      if (!strcmp(name, OBJECT_DETECTION_NAME)) {
        gst_value_list_append_value (&object_detection, &value);
      } else if (!strcmp (name, IMAGE_CLASSIFICATION_NAME)) {
        gst_value_list_append_value (&image_classification, &value);
      } else if (!strcmp (name, POSE_ESTIMATION_NAME)) {
        gst_value_list_append_value (&pose_estimation, &value);
      }
      g_value_reset (&value);
    }

    structure = gst_structure_new (PARAMETERS_NAME,
        "timestamp", G_TYPE_STRING, timestamp, NULL);
    g_value_take_boxed (&value, structure);
    gst_value_list_append_value (&object_detection, &value);
    gst_value_list_append_value (&image_classification, &value);
    gst_value_list_append_value (&pose_estimation, &value);
    g_value_reset (&value);

    binfo.builder = object_detection_builder;
    json_builder_begin_object (object_detection_builder);
    gst_list_to_json_append (&object_detection, &binfo);
    json_builder_end_object (object_detection_builder);
    gst_module_set_output (OBJECT_DETECTION_NAME, object_detection_builder, output);

    binfo.builder = image_classification_builder;
    json_builder_begin_object (image_classification_builder);
    gst_list_to_json_append (&image_classification, &binfo);
    json_builder_end_object (image_classification_builder);
    gst_module_set_output (IMAGE_CLASSIFICATION_NAME, image_classification_builder, output);

    binfo.builder = pose_estimation_builder;
    json_builder_begin_object (pose_estimation_builder);
    gst_list_to_json_append (&pose_estimation, &binfo);
    json_builder_end_object (pose_estimation_builder);
    gst_module_set_output (POSE_ESTIMATION_NAME, pose_estimation_builder, output);

    GST_DEBUG ("%s",  buffer_info.data);

  } else if (submodule->data_type == GST_DATA_TYPE_VIDEO) {
    vmeta = gst_buffer_get_video_meta (mlframe->buffer);
    g_return_val_if_fail (vmeta != NULL, FALSE);

    json_builder_begin_object (object_detection_builder);
    json_builder_set_member_name (object_detection_builder, OBJECT_DETECTION_NAME);
    json_builder_begin_array (object_detection_builder);

    json_builder_begin_object (image_classification_builder);
    json_builder_set_member_name (image_classification_builder, IMAGE_CLASSIFICATION_NAME);
    json_builder_begin_array (image_classification_builder);

    json_builder_begin_object (pose_estimation_builder);
    json_builder_set_member_name (pose_estimation_builder, POSE_ESTIMATION_NAME);
    json_builder_begin_array (pose_estimation_builder);

    while ((meta = gst_buffer_iterate_meta (mlframe->buffer, &state)) != NULL) {
      if (GST_META_IS_OBJECT_DETECTION (meta)) {
        rmeta = GST_VIDEO_ROI_META_CAST(meta);
        if (rmeta->parent_id == -1)
          gst_module_detection_meta_to_json_append (mlframe->buffer, vmeta,
              GST_VIDEO_ROI_META_CAST (meta), object_detection_builder);
      } else if (GST_META_IS_IMAGE_CLASSIFICATION (meta)) {
        gst_module_image_classification_meta_to_json_append (
            GST_VIDEO_CLASSIFICATION_META_CAST (meta),
            image_classification_builder);
      } else if (GST_META_IS_POSE_ESTIMATION (meta)) {
        gst_module_pose_estimation_meta_to_json_append (vmeta,
            GST_VIDEO_LANDMARKS_META_CAST (meta), pose_estimation_builder);
      }
    }

    json_builder_end_array (object_detection_builder);
    json_builder_end_array (image_classification_builder);
    json_builder_end_array (pose_estimation_builder);

    json_builder_set_member_name (object_detection_builder, PARAMETERS_NAME);
    json_builder_begin_object (object_detection_builder);
    json_builder_set_member_name (object_detection_builder, "timestamp");
    json_builder_add_string_value  (object_detection_builder, timestamp);
    json_builder_end_object (object_detection_builder);

    json_builder_set_member_name (image_classification_builder, PARAMETERS_NAME);
    json_builder_begin_object (image_classification_builder);
    json_builder_set_member_name (image_classification_builder, "timestamp");
    json_builder_add_string_value  (image_classification_builder, timestamp);
    json_builder_end_object (image_classification_builder);

    json_builder_set_member_name (pose_estimation_builder, PARAMETERS_NAME);
    json_builder_begin_object (pose_estimation_builder);
    json_builder_set_member_name (pose_estimation_builder, "timestamp");
    json_builder_add_string_value  (pose_estimation_builder, timestamp);
    json_builder_end_object (pose_estimation_builder);

    g_free (timestamp);

    json_builder_end_object (object_detection_builder);
    json_builder_end_object (image_classification_builder);
    json_builder_end_object (pose_estimation_builder);

    gst_module_set_output (OBJECT_DETECTION_NAME, object_detection_builder, output);
    gst_module_set_output (IMAGE_CLASSIFICATION_NAME, image_classification_builder, output);
    gst_module_set_output (POSE_ESTIMATION_NAME, pose_estimation_builder, output);
  }

  g_object_unref (object_detection_builder);
  g_object_unref (image_classification_builder);
  g_object_unref (pose_estimation_builder);
  g_object_unref (parameters_builder);

  return TRUE;
}