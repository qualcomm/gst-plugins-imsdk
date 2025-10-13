/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlmetaextractor.h"

#include <stdio.h>

#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

#define GST_CAT_DEFAULT gst_mlmeta_extractor_debug
GST_DEBUG_CATEGORY (gst_mlmeta_extractor_debug);

#define gst_mlmeta_extractor_parent_class parent_class
G_DEFINE_TYPE (GstMLMetaExtractor, gst_mlmeta_extractor, GST_TYPE_BASE_TRANSFORM);

#define OBJECT_DETECTION_NAME     "ObjectDetection"
#define IMAGE_CLASSIFICATION_NAME "ImageClassification"
#define POSE_ESTIMATION_NAME      "VideoLandmarks"

#define GST_META_IS_OBJECT_DETECTION(meta) \
    ((meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) && \
     (GST_VIDEO_ROI_META_CAST (meta)->roi_type != \
          g_quark_from_static_string ("ImageRegion")))

#define GST_META_IS_IMAGE_CLASSIFICATION(meta) \
    (meta->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE)

#define GST_META_IS_POSE_ESTIMATION(meta) \
    (meta->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE)

#define GST_MLMETA_EXTRACTOR_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_MLMETA_EXTRACTOR_SRC_CAPS \
    "text/x-raw, format = (string) utf8"

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_mlmeta_extractor_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_MLMETA_EXTRACTOR_SINK_CAPS)
    );

static GstStaticPadTemplate gst_mlmeta_extractor_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_MLMETA_EXTRACTOR_SRC_CAPS)
    );

static GstCaps *
gst_mlmeta_extractor_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (base);
  GstCaps *result = NULL;

  GST_DEBUG_OBJECT (extractor, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (extractor, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (extractor, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static gboolean
gst_mlmeta_extractor_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (base);

  GST_MLMETA_EXTRACTOR_LOCK (extractor);

  // Extract video information from caps.
  if (!gst_video_info_from_caps (&extractor->vinfo, incaps)) {
    GST_ERROR_OBJECT (extractor, "Invalid caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  GST_MLMETA_EXTRACTOR_UNLOCK (extractor);

  GST_DEBUG_OBJECT (extractor, "Input caps: %" GST_PTR_FORMAT, incaps);
  return TRUE;
}

static GstFlowReturn
gst_mlmeta_extractor_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (base);

  // Create a new buffer wrapper to hold a reference to input buffer.
  *outbuffer = gst_buffer_new ();

  if (*outbuffer == NULL) {
    GST_ERROR_OBJECT (extractor, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }

  // If input is a GAP buffer set the GAP flag for the output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static gint
gst_mlmeta_extractor_group_buffer_metas (GstMLMetaExtractor * extractor,
    GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;
  guint n_entries = 0;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    GHashTable *metatable = NULL;
    GList *metalist = NULL;
    gint parent_id = -1;

    if (GST_META_IS_OBJECT_DETECTION (meta)) {
      parent_id = GST_VIDEO_ROI_META_CAST (meta)->parent_id;
      metatable = extractor->roimetas;
    } else if (GST_META_IS_POSE_ESTIMATION (meta)) {
      parent_id = GST_VIDEO_LANDMARKS_META_CAST (meta)->parent_id;
      metatable = extractor->ldmrkmetas;
    } else if (GST_META_IS_IMAGE_CLASSIFICATION (meta)) {
      parent_id = GST_VIDEO_CLASSIFICATION_META_CAST (meta)->parent_id;
      metatable = extractor->classmetas;
    }

    // If meta is not supported skip handling it.
    if (metatable == NULL)
      continue;

    metalist = g_hash_table_lookup (metatable, GINT_TO_POINTER (parent_id));
    metalist = g_list_prepend (metalist, meta);

    g_hash_table_insert (metatable, GINT_TO_POINTER (parent_id), metalist);
  }

  n_entries = g_hash_table_size (extractor->roimetas) +
      g_hash_table_size (extractor->classmetas) +
      g_hash_table_size (extractor->ldmrkmetas);

  return n_entries;
}

static GstVideoRegionOfInterestMeta *
gst_mlmeta_extractor_seek_parent_meta (GHashTable * roimetas, gint parent_id)
{
  GPtrArray *parent_ids = NULL;
  GList *list = NULL;
  guint index = 0;

  parent_ids = g_hash_table_get_keys_as_ptr_array (roimetas);
  for (index = 0; index < parent_ids->len; index++) {
    gpointer key = g_ptr_array_index (parent_ids, index);
    GList *roimeta_list = g_hash_table_lookup (roimetas, key);

    for (list = g_list_last (roimeta_list); list != NULL; list = list->prev) {
      GstVideoRegionOfInterestMeta * roimeta = GST_VIDEO_ROI_META_CAST (list->data);

      if (roimeta->id == parent_id) {
        g_ptr_array_unref (parent_ids);
        return roimeta;
      }
    }
  }

  g_ptr_array_unref (parent_ids);
  return NULL;
}

static void
g_hash_table_free_glists (gpointer key, gpointer data, gpointer userdata)
{
  GstMLMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (userdata);
  gint parent_id = GPOINTER_TO_INT (key);
  GList *metalist = (GList *) data;

  if (metalist != NULL)
    g_list_free (metalist);

  GST_TRACE_OBJECT (extractor, "Freed GList %p; parent_id %d", metalist,
      parent_id);
}

static guint
gst_mlmeta_extractor_add_class_structs_to_list (GstMLMetaExtractor * extractor,
    GList * cmeta_list, gint parent_id, guint current_idx, guint n_entries,
    guint timestamp, GValue * output_list)
{
  GstStructure *structure = NULL;
  GList *list = NULL;
  GValue labels = G_VALUE_INIT, value = G_VALUE_INIT;

  GST_DEBUG_OBJECT (extractor, "Received %d class metas with parent_id %d",
      g_list_length (cmeta_list), parent_id);

  g_value_init (&labels, GST_TYPE_ARRAY);

  for (list = g_list_last (cmeta_list); list != NULL; list = list->prev) {
    GstVideoClassificationMeta *cmeta =
        GST_VIDEO_CLASSIFICATION_META_CAST (list->data);
    guint index = 0;

    if (cmeta->labels == NULL)
      continue;

    GST_DEBUG_OBJECT (extractor,
        "Processing Classification meta with ID: [0x%X], parent_id: [0x%X]",
        cmeta->id, parent_id);

    for (index = 0; index < cmeta->labels->len; index++) {
      GstClassLabel clabel = g_array_index (cmeta->labels, GstClassLabel, index);
      GstStructure *label = NULL;
      gchar *name = NULL;

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (g_quark_to_string (clabel.name));
      name = g_strdelimit (name, " ", '.');

      label = gst_structure_new (name,
          "id", G_TYPE_UINT, cmeta->id,
          "confidence", G_TYPE_DOUBLE, clabel.confidence,
          "color", G_TYPE_UINT, clabel.color,
          NULL);

      g_free (name);

      if (clabel.xtraparams != NULL) {
        g_value_init (&value, GST_TYPE_STRUCTURE);

        g_value_set_boxed (&value, clabel.xtraparams);
        gst_structure_set_value (structure, "xtraparams", &value);
        g_value_unset (&value);
      }

      g_value_init (&value, GST_TYPE_STRUCTURE);

      g_value_take_boxed (&value, label);
      gst_value_array_append_value (&labels, &value);
      g_value_unset (&value);
    }
  }

  structure = gst_structure_new_empty ("ImageClassification");

  gst_structure_set_value (structure, "labels", &labels);
  g_value_unset (&labels);

  gst_structure_set (structure,
      "timestamp", G_TYPE_UINT64, timestamp,
      "sequence-index", G_TYPE_UINT, current_idx++,
      "sequence-num-entries", G_TYPE_UINT, n_entries,
      "parent-id", G_TYPE_INT, parent_id,
      NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);

  if (structure)
    g_value_take_boxed (&value, structure);

  gst_value_list_append_value (output_list, &value);
  g_value_unset (&value);

  return current_idx;
}

static guint
gst_mlmeta_extractor_add_pose_structs_to_list (GstMLMetaExtractor * extractor,
    GList * pmeta_list, gint parent_id, guint current_idx, guint n_entries,
    guint timestamp, GValue * output_list)
{
  GstVideoRegionOfInterestMeta * parent_meta = NULL;
  GstStructure *structure = NULL;
  GList *list = NULL;
  GValue poses = G_VALUE_INIT, value = G_VALUE_INIT;

  if (parent_id != -1)
    parent_meta = gst_mlmeta_extractor_seek_parent_meta (extractor->roimetas,
        parent_id);

  GST_DEBUG_OBJECT (extractor, "Received %d pose metas with parent_id %d",
      g_list_length (pmeta_list), parent_id);

  g_value_init (&poses, GST_TYPE_ARRAY);

  for (list = g_list_last (pmeta_list); list != NULL; list = list->prev) {
    GstVideoLandmarksMeta *pmeta = GST_VIDEO_LANDMARKS_META_CAST (list->data);
    GstStructure *pose = NULL;
    GValue array = G_VALUE_INIT;
    gint parent_w = 0, parent_h = 0, parent_x = 0, parent_y = 0;
    guint index = 0;

    if (pmeta->keypoints == NULL)
      continue;

    GST_DEBUG_OBJECT (extractor,
        "Processing Pose meta with ID: [0x%X], parent_id: [0x%X]", pmeta->id,
        parent_id);

    if (parent_meta != NULL) {
      parent_w = parent_meta->w;
      parent_h = parent_meta->h;
      parent_x = parent_meta->x;
      parent_y = parent_meta->y;
    } else {
      parent_w = GST_VIDEO_INFO_WIDTH (&(extractor->vinfo));
      parent_h = GST_VIDEO_INFO_HEIGHT (&(extractor->vinfo));
      parent_x = 0;
      parent_y = 0;
    }

    g_value_init (&array, GST_TYPE_ARRAY);

    pose = gst_structure_new ("pose",
        "id", G_TYPE_UINT, pmeta->id,
        "confidence", G_TYPE_DOUBLE, pmeta->confidence,
        NULL);

    for (index = 0; index < pmeta->keypoints->len; index++) {
      GstVideoKeypoint vkeypoint = g_array_index (
          pmeta->keypoints, GstVideoKeypoint, index);
      GstStructure *keypoint = NULL;
      gchar *name = NULL;

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (g_quark_to_string (vkeypoint.name));
      name = g_strdelimit (name, " ", '.');

      keypoint = gst_structure_new (name,
          "confidence", G_TYPE_DOUBLE, vkeypoint.confidence,
          "x", G_TYPE_DOUBLE, ((gdouble) (vkeypoint.x - parent_x) / parent_w),
          "y", G_TYPE_DOUBLE, ((gdouble) (vkeypoint.y - parent_y) / parent_h),
          "color", G_TYPE_UINT, vkeypoint.color,
          NULL);

      g_free (name);

      g_value_init (&value, GST_TYPE_STRUCTURE);

      g_value_take_boxed (&value, keypoint);
      gst_value_array_append_value (&array, &value);
      g_value_unset (&value);
    }

    gst_structure_set_value (pose, "keypoints", &array);
    g_value_reset (&array);

    if (pmeta->links != NULL) {
      for (index = 0; index < pmeta->links->len; index++) {
        GstVideoKeypointLink vkplink = g_array_index (
            pmeta->links, GstVideoKeypointLink, index);
        GstVideoKeypoint vkeypoint;
        GValue link = G_VALUE_INIT;

        g_value_init (&link, GST_TYPE_ARRAY);
        g_value_init (&value, G_TYPE_STRING);

        vkeypoint = g_array_index (pmeta->keypoints, GstVideoKeypoint,
            vkplink.s_kp_idx);

        g_value_set_string (&value, g_quark_to_string (vkeypoint.name));
        gst_value_array_append_value (&link, &value);
        g_value_reset (&value);

        vkeypoint = g_array_index (pmeta->keypoints, GstVideoKeypoint,
            vkplink.d_kp_idx);

        g_value_set_string (&value, g_quark_to_string(vkeypoint.name));
        gst_value_array_append_value (&link, &value);
        g_value_unset (&value);

        gst_value_array_append_value (&array, &link);
        g_value_unset (&link);
      }

      gst_structure_set_value (pose, "connections", &array);
      g_value_reset (&array);
    }

    if (pmeta->xtraparams != NULL) {
      g_value_init (&value, GST_TYPE_STRUCTURE);

      g_value_set_boxed (&value, pmeta->xtraparams);
      gst_structure_set_value (pose, "xtraparams", &value);
      g_value_unset (&value);
    }

    g_value_init (&value, GST_TYPE_STRUCTURE);

    g_value_take_boxed (&value, pose);
    gst_value_array_append_value (&poses, &value);
    g_value_unset (&value);
  }

  structure = gst_structure_new_empty ("PoseEstimation");

  gst_structure_set_value (structure, "poses", &poses);
  g_value_unset (&poses);

  gst_structure_set (structure,
      "timestamp", G_TYPE_UINT64, timestamp,
      "sequence-index", G_TYPE_UINT, current_idx++,
      "sequence-num-entries", G_TYPE_UINT, n_entries,
      "parent-id", G_TYPE_INT, parent_id,
      NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);

  if (structure)
    g_value_take_boxed (&value, structure);

  gst_value_list_append_value (output_list, &value);
  g_value_unset (&value);

  return current_idx;
}

static guint
gst_mlmeta_extractor_add_detection_structs_to_list (GstMLMetaExtractor *extractor,
    GList * roimeta_list, gint parent_id, guint current_idx, guint n_entries,
    guint timestamp, GValue * output_list)
{
  GstVideoRegionOfInterestMeta *parent_meta = NULL;
  GstStructure *structure = NULL;
  GList *list = NULL;
  GValue bboxes = G_VALUE_INIT, value = G_VALUE_INIT;

  if (parent_id != -1)
    parent_meta = gst_mlmeta_extractor_seek_parent_meta (extractor->roimetas,
        parent_id);

  GST_DEBUG_OBJECT (extractor, "Received %d roi metas with parent_id %d",
      g_list_length (roimeta_list), parent_id);

  g_value_init (&bboxes, GST_TYPE_ARRAY);

  for (list = g_list_last (roimeta_list); list != NULL; list = list->prev) {
    GstVideoRegionOfInterestMeta *roimeta = GST_VIDEO_ROI_META_CAST (list->data);
    GstStructure *params = NULL, *bbox = NULL;
    const GValue *temp_val = NULL;
    GValue array = G_VALUE_INIT;
    gchar *name = NULL;
    gdouble confidence = 0.0;
    guint color = 0;
    gint parent_w = 0, parent_h = 0;

    if ((params = gst_video_region_of_interest_meta_get_param (roimeta,
        OBJECT_DETECTION_NAME)) == NULL)
      continue;

    GST_DEBUG_OBJECT (extractor,
        "Processing Detection meta with ID: [0x%X], parent_id: [0x%X]",
        roimeta->id, parent_id);

    if (parent_meta != NULL) {
      parent_w = parent_meta->w;
      parent_h = parent_meta->h;
    } else {
      parent_w = GST_VIDEO_INFO_WIDTH (&(extractor->vinfo));
      parent_h = GST_VIDEO_INFO_HEIGHT (&(extractor->vinfo));
    }

    g_value_init (&array, GST_TYPE_ARRAY);

    gst_structure_get_double (params, "confidence", &confidence);
    gst_structure_get_uint (params, "color", &color);

    // Replace empty spaces otherwise subsequent stream parse call will fail.
    name = g_strdup (g_quark_to_string (roimeta->roi_type));
    name = g_strdelimit (name, " ", '.');

    bbox = gst_structure_new (name,
        "id", G_TYPE_UINT, roimeta->id,
        "confidence", G_TYPE_DOUBLE, confidence,
        "color", G_TYPE_UINT, color,
        NULL);

    g_free (name);

    g_value_init (&value, G_TYPE_FLOAT);

    g_value_set_float (&value, ((gdouble) roimeta->x / parent_w));
    gst_value_array_append_value (&array, &value);

    g_value_set_float (&value, ((gdouble) roimeta->y / parent_h));
    gst_value_array_append_value (&array, &value);

    g_value_set_float (&value, ((gdouble) roimeta->w / parent_w));
    gst_value_array_append_value (&array, &value);

    g_value_set_float (&value, ((gdouble) roimeta->h / parent_h));
    gst_value_array_append_value (&array, &value);

    gst_structure_set_value (bbox, "rectangle", &array);
    g_value_reset (&array);

    g_value_unset (&value);

    if ((temp_val = gst_structure_get_value (params, "landmarks")) != NULL) {
      GArray *incoming_landmarks = g_value_get_boxed (temp_val);
      guint index = 0;

      for (index = 0; index < incoming_landmarks->len; index++) {
        GstStructure *landmark = NULL;
        GstVideoKeypoint *kp = NULL;

        kp = &(g_array_index (
            incoming_landmarks, GstVideoKeypoint, index));

        // Replace empty spaces otherwise subsequent stream parse call will fail.
        name = g_strdup (g_quark_to_string (kp->name));
        name = g_strdelimit (name, " ", '.');

        landmark = gst_structure_new (name,
            "x", G_TYPE_UINT, kp->x,
            "y", G_TYPE_UINT, kp->y,
            NULL);

        g_free (name);

        g_value_init (&value, GST_TYPE_STRUCTURE);

        g_value_take_boxed (&value, landmark);
        gst_value_array_append_value (&array, &value);
        g_value_unset (&value);
      }

      gst_structure_set_value (bbox, "landmarks", &array);
      g_value_reset (&array);
    }

    if (gst_structure_has_field (params, "xtraparams")) {
      GstStructure *xtraparams = GST_STRUCTURE (
          g_value_get_boxed (gst_structure_get_value (params, "xtraparams")));

      g_value_init (&value, GST_TYPE_STRUCTURE);

      g_value_set_boxed (&value, xtraparams);
      gst_structure_set_value (bbox, "xtraparams", &value);
      g_value_unset (&value);
    }

    g_value_init (&value, GST_TYPE_STRUCTURE);

    g_value_take_boxed (&value, bbox);
    gst_value_array_append_value (&bboxes, &value);
    g_value_unset (&value);
  }

  structure = gst_structure_new_empty ("ObjectDetection");

  gst_structure_set_value (structure, "bounding-boxes", &bboxes);
  g_value_unset (&bboxes);

  gst_structure_set (structure,
      "timestamp", G_TYPE_UINT64, timestamp,
      "sequence-index", G_TYPE_UINT, current_idx++,
      "sequence-num-entries", G_TYPE_UINT, n_entries,
      "parent-id", G_TYPE_INT, parent_id,
      NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);

  if (structure)
    g_value_take_boxed (&value, structure);

  gst_value_list_append_value (output_list, &value);
  g_value_unset (&value);

  return current_idx;
}

static guint
gst_mlmeta_extractor_process_metas (GstMLMetaExtractor * extractor,
    GHashTable * metatable, guint seqidx, guint n_entries,
    GstClockTime timestamp, GValue * outlist)
{
  gpointer key = NULL, value = NULL;
  GList *metalist = NULL;
  GHashTableIter iter;
  gint parent_id = -1;

  g_hash_table_iter_init (&iter, metatable);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    parent_id = GPOINTER_TO_INT (key);
    metalist = (GList *) value;

    if (metalist == NULL)
      return seqidx;

    GstMeta *meta = GST_META_CAST ((g_list_first (metalist))->data);

    if (GST_META_IS_OBJECT_DETECTION (meta)) {
      seqidx = gst_mlmeta_extractor_add_detection_structs_to_list (extractor,
          metalist, parent_id, seqidx, n_entries, timestamp, outlist);
    } else if (GST_META_IS_POSE_ESTIMATION (meta)) {
      seqidx = gst_mlmeta_extractor_add_pose_structs_to_list (extractor,
          metalist, parent_id, seqidx, n_entries, timestamp, outlist);
    } else if (GST_META_IS_IMAGE_CLASSIFICATION (meta)) {
      seqidx = gst_mlmeta_extractor_add_class_structs_to_list (extractor,
          metalist, parent_id, seqidx, n_entries, timestamp, outlist);
    } else {
      GST_WARNING_OBJECT (extractor, "Unsupported meta detected in metalist!");
    }
  }

  return seqidx;
}

static GstFlowReturn
gst_mlmeta_extractor_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (base);
  GstMemory *mem = NULL;
  GValue output_list = G_VALUE_INIT;
  gchar *output_string = NULL;
  gint string_len = 0;
  gint n_entries = 0, seq_index = 1;
  guint timestamp = GST_BUFFER_PTS (inbuffer);

  GST_TRACE_OBJECT (extractor, "Received %" GST_PTR_FORMAT, inbuffer);

  GST_MLMETA_EXTRACTOR_LOCK (extractor);

  n_entries = gst_mlmeta_extractor_group_buffer_metas (extractor, inbuffer);

  g_value_init (&output_list, GST_TYPE_LIST);

  seq_index = gst_mlmeta_extractor_process_metas (extractor,
      extractor->roimetas, seq_index, n_entries, timestamp, &output_list);
  seq_index = gst_mlmeta_extractor_process_metas (extractor,
      extractor->ldmrkmetas, seq_index, n_entries, timestamp, &output_list);
  seq_index = gst_mlmeta_extractor_process_metas (extractor,
      extractor->classmetas, seq_index, n_entries, timestamp, &output_list);

  g_hash_table_foreach (extractor->roimetas, g_hash_table_free_glists,
      extractor);
  g_hash_table_foreach (extractor->ldmrkmetas, g_hash_table_free_glists,
      extractor);
  g_hash_table_foreach (extractor->classmetas, g_hash_table_free_glists,
      extractor);

  g_hash_table_remove_all (extractor->roimetas);
  g_hash_table_remove_all (extractor->ldmrkmetas);
  g_hash_table_remove_all (extractor->classmetas);

  if (gst_value_list_get_size (&output_list) == 0) {
    GstStructure *structure = gst_structure_new_empty ("ObjectDetection");
    GValue bboxes = G_VALUE_INIT, value = G_VALUE_INIT;

    g_value_init (&bboxes, GST_TYPE_ARRAY);

    gst_structure_set_value (structure, "bounding-boxes", &bboxes);
    g_value_unset (&bboxes);

    gst_structure_set (structure,
        "timestamp", G_TYPE_UINT64, GST_BUFFER_PTS (inbuffer),
        "sequence-index", G_TYPE_UINT, 1,
        "sequence-num-entries", G_TYPE_UINT, 1,
        NULL);

    g_value_init (&value, GST_TYPE_STRUCTURE);

    if (structure)
      g_value_take_boxed (&value, structure);

    gst_value_list_append_value (&output_list, &value);
    g_value_unset (&value);
  }

  output_string = gst_value_serialize (&output_list);
  g_value_unset (&output_list);

  if (output_string == NULL) {
    GST_ERROR_OBJECT (extractor, "Failed to serialize detection structure!");
    return GST_FLOW_ERROR;
  }

  string_len = strlen (output_string) + 1;
  output_string[string_len - 1] = '\n';

  mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_ZERO_PADDED,
      output_string, string_len, 0, string_len, output_string, g_free);
  gst_buffer_append_memory (outbuffer, mem);

  GST_MLMETA_EXTRACTOR_UNLOCK (extractor);

  return GST_FLOW_OK;
}

static void
gst_mlmeta_extractor_finalize (GObject * object)
{
  GstMLMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (object);

  g_hash_table_destroy (extractor->classmetas);
  g_hash_table_destroy (extractor->ldmrkmetas);
  g_hash_table_destroy (extractor->roimetas);

  g_mutex_clear (&(extractor)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (extractor));
}

static void
gst_mlmeta_extractor_class_init (GstMLMetaExtractorClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  object->finalize = GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_finalize);

  gst_element_class_set_static_metadata (element,
      "Video mlmeta extractor", "Filter/Demuxer/Converter",
      "Extract mlmeta from video buffers into text buffers", "QTI"
  );

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_mlmeta_extractor_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_mlmeta_extractor_src_template));

  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_prepare_output_buffer);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_transform_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_transform);
}

static void
gst_mlmeta_extractor_init (GstMLMetaExtractor * extractor)
{
  g_mutex_init (&(extractor)->lock);

  extractor->roimetas = g_hash_table_new_full (NULL, NULL, NULL, NULL);
  extractor->ldmrkmetas = g_hash_table_new_full (NULL, NULL, NULL, NULL);
  extractor->classmetas = g_hash_table_new_full (NULL, NULL, NULL, NULL);

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (extractor), TRUE);

  // Initializes a new ML extractor GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_mlmeta_extractor_debug, "qtimlmetaextractor", 0,
      "QTI ML Meta Extractor");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlmetaextractor", GST_RANK_NONE,
      GST_TYPE_MLMETA_EXTRACTOR);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlmetaextractor,
    "QTI ML Meta Extractor",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
