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
G_DEFINE_TYPE (GstMlMetaExtractor, gst_mlmeta_extractor, GST_TYPE_ELEMENT);

#define OBJECT_DETECTION_NAME     "ObjectDetection"
#define IMAGE_CLASSIFICATION_NAME "ImageClassification"
#define POSE_ESTIMATION_NAME      "VideoLandmarks"

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

enum
{
  PROP_0,
};

enum
{
  PROCESS_TYPE_DETECTION,
  PROCESS_TYPE_POSE,
  PROCESS_TYPE_CLASSIFICATION,
};

static GstStaticPadTemplate gst_mlmeta_extractor_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate gst_mlmeta_extractor_video_src_template =
    GST_STATIC_PAD_TEMPLATE("video",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate gst_mlmeta_extractor_meta_src_template =
    GST_STATIC_PAD_TEMPLATE("meta",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("text/x-raw, format = (string) utf8")
    );

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_mlmeta_extractor_src_pad_push_event (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (element);
  GstEvent *event = GST_EVENT (userdata);

  // On EOS wait until all queued buffers have been pushed before propagating it.
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    GST_MLMETA_EXTRACTOR_PAD_WAIT_IDLE (GST_MLMETA_EXTRACTOR_SRCPAD_CAST (pad));

  GST_TRACE_OBJECT (extractor, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_ref (event));
}

static GstCaps *
gst_mlmeta_extractor_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = NULL, *intersect = NULL;

  if (!(caps = gst_pad_get_current_caps (pad)))
    caps = gst_pad_get_pad_template_caps (pad);

  GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);
    intersect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersect;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_mlmeta_extractor_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *tmplcaps = NULL;
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success &= gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with template!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_mlmeta_extractor_sink_setcaps (GstMlMetaExtractor * extractor, GstPad * pad, GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  GST_MLMETA_EXTRACTOR_LOCK (extractor);

  // Get the negotiated caps between the video srcpad and its peer.
  srccaps = gst_pad_get_allowed_caps (GST_PAD (extractor->vpad));
  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  intersect = gst_caps_intersect (srccaps, caps);
  GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

  gst_caps_unref (srccaps);
  srccaps = intersect;

  if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
    GST_ELEMENT_ERROR (extractor, CORE, NEGOTIATION, (NULL),
        ("Source %s and sink caps do not intersect!", GST_PAD_NAME (extractor->vpad)));

    if (intersect != NULL)
      gst_caps_unref (intersect);

    GST_MLMETA_EXTRACTOR_UNLOCK (extractor);
    return FALSE;
  }

  if (!gst_pad_set_caps (GST_PAD (extractor->vpad), srccaps)) {
    GST_ELEMENT_ERROR (GST_ELEMENT (extractor), CORE, NEGOTIATION, (NULL),
        ("Failed to set caps to %s!", GST_PAD_NAME (extractor->vpad)));
    GST_MLMETA_EXTRACTOR_UNLOCK (extractor);
    return FALSE;
  }

  GST_DEBUG_OBJECT (pad, "Negotiated caps at source pad %s: %" GST_PTR_FORMAT,
      GST_PAD_NAME (extractor->vpad), srccaps);

  // Extract video information from caps.
  if (!gst_video_info_from_caps (&extractor->vinfo, caps)) {
    GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  GST_MLMETA_EXTRACTOR_UNLOCK (extractor);

  return TRUE;
}

static void
gst_mlmeta_extractor_src_pad_worker_task (gpointer userdata)
{
  GstMlMetaExtractorSrcPad *srcpad = GST_MLMETA_EXTRACTOR_SRCPAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting %" GST_PTR_FORMAT, buffer);
    gst_pad_push (GST_PAD (srcpad), buffer);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

static gint
gst_mlmeta_extractor_group_buffer_metas (GstMlMetaExtractor * extractor, GstBuffer * buffer)
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
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (userdata);
  gint parent_id = GPOINTER_TO_INT (key);
  GList *metalist = (GList *) data;

  if (metalist != NULL)
    g_list_free (metalist);

  GST_TRACE_OBJECT (extractor, "Freed GList %p; parent_id %d", metalist, parent_id);
}

static gboolean
gst_mlmeta_extractor_add_class_structs_to_list (GstMlMetaExtractorParams * extractor_params)
{
  GstMlMetaExtractor *extractor = extractor_params->extractor;
  GValue *output_list = extractor_params->output_list;
  GstBuffer *inbuffer = extractor_params->inbuffer;
  GstStructure *structure = NULL;
  GList *cmeta_list = extractor_params->metalist, *list = NULL;
  GValue labels = G_VALUE_INIT, value = G_VALUE_INIT;
  gint current_idx = *(extractor_params->seq_index);
  gint n_entries = extractor_params->n_entries;
  gint parent_id = extractor_params->parent_id;

  (*(extractor_params->seq_index))++;

  GST_DEBUG_OBJECT (extractor, "Going to process %d class metas with parent_id %d",
      g_list_length (cmeta_list), parent_id);

  g_value_init (&labels, GST_TYPE_ARRAY);

  for (list = g_list_last (cmeta_list); list != NULL; list = list->prev) {
    GstVideoClassificationMeta *cmeta =
        GST_VIDEO_CLASSIFICATION_META_CAST (list->data);
    guint index = 0;

    if (cmeta->labels == NULL)
      continue;

    GST_DEBUG_OBJECT (extractor,
        "Processing Classification meta with ID: [0x%X], parent_id: [0x%X] "
        "from buffer: %p", cmeta->id, parent_id, inbuffer);

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
      "timestamp", G_TYPE_UINT64, GST_BUFFER_PTS (inbuffer),
      "sequence-index", G_TYPE_UINT, current_idx,
      "sequence-num-entries", G_TYPE_UINT, n_entries,
      "parent-id", G_TYPE_INT, parent_id,
      NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);

  if (structure)
    g_value_take_boxed (&value, structure);

  gst_value_list_append_value (output_list, &value);
  g_value_unset (&value);

  return TRUE;
}

static gboolean
gst_mlmeta_extractor_add_pose_structs_to_list (GstMlMetaExtractorParams * extractor_params)
{
  GstMlMetaExtractor *extractor = extractor_params->extractor;
  GstVideoRegionOfInterestMeta * parent_meta = NULL;
  GValue *output_list = extractor_params->output_list;
  GstBuffer *inbuffer = extractor_params->inbuffer;
  GstStructure *structure = NULL;
  GList *pmeta_list = extractor_params->metalist, *list = NULL;
  GValue poses = G_VALUE_INIT, value = G_VALUE_INIT;
  gint current_idx = *(extractor_params->seq_index);
  gint n_entries = extractor_params->n_entries;
  gint parent_id = extractor_params->parent_id;

  if (parent_id != -1)
    parent_meta = gst_mlmeta_extractor_seek_parent_meta (extractor->roimetas, parent_id);

  (*(extractor_params->seq_index))++;

  GST_DEBUG_OBJECT (extractor, "Going to process %d pose metas with parent_id %d",
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
        "Processing Pose meta with ID: [0x%X], parent_id: [0x%X] from buffer: %p",
        pmeta->id, parent_id, inbuffer);

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
      "timestamp", G_TYPE_UINT64, GST_BUFFER_PTS (inbuffer),
      "sequence-index", G_TYPE_UINT, current_idx,
      "sequence-num-entries", G_TYPE_UINT, n_entries,
      "parent-id", G_TYPE_INT, parent_id,
      NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);

  if (structure)
    g_value_take_boxed (&value, structure);

  gst_value_list_append_value (output_list, &value);
  g_value_unset (&value);

  return TRUE;
}

static gboolean
gst_mlmeta_extractor_add_detection_structs_to_list (GstMlMetaExtractorParams * extractor_params)
{
  GstMlMetaExtractor *extractor = extractor_params->extractor;
  GstVideoRegionOfInterestMeta *parent_meta = NULL;
  GValue *output_list = extractor_params->output_list;
  GstBuffer *inbuffer = extractor_params->inbuffer;
  GstStructure *structure = NULL;
  GList *roimeta_list = extractor_params->metalist, *list = NULL;
  GValue bboxes = G_VALUE_INIT, value = G_VALUE_INIT;
  gint current_idx = *(extractor_params->seq_index);
  gint n_entries = extractor_params->n_entries;
  gint parent_id = extractor_params->parent_id;

  if (parent_id != -1)
    parent_meta = gst_mlmeta_extractor_seek_parent_meta (extractor->roimetas, parent_id);

  (*(extractor_params->seq_index))++;

  GST_DEBUG_OBJECT (extractor, "Going to process %d roi metas with parent_id %d",
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
        "Processing Detection meta with ID: [0x%X], parent_id: [0x%X] from"
        "buffer: %p", roimeta->id, parent_id, inbuffer);

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
        GQuark landmark_name = 0;
        guint x = 0, y = 0;

        GstStructure *str_landmark = g_array_index (
            incoming_landmarks, GstStructure*, index);

        gst_structure_get_uint (str_landmark, "name", &landmark_name);
        gst_structure_get_uint (str_landmark, "x", &x);
        gst_structure_get_uint (str_landmark, "y", &y);

        // Replace empty spaces otherwise subsequent stream parse call will fail.
        name = g_strdup (g_quark_to_string (landmark_name));
        name = g_strdelimit (name, " ", '.');

        landmark = gst_structure_new (name,
            "x", G_TYPE_UINT, x,
            "y", G_TYPE_UINT, y,
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
      "timestamp", G_TYPE_UINT64, GST_BUFFER_PTS (inbuffer),
      "sequence-index", G_TYPE_UINT, current_idx,
      "sequence-num-entries", G_TYPE_UINT, n_entries,
      "parent-id", G_TYPE_INT, parent_id,
      NULL);

  g_value_init (&value, GST_TYPE_STRUCTURE);

  if (structure)
    g_value_take_boxed (&value, structure);

  gst_value_list_append_value (output_list, &value);
  g_value_unset (&value);

  return TRUE;
}

static void
gst_mlmeta_extractor_process_meta_entry (gpointer key, gpointer data, gpointer params)
{
  GstMlMetaExtractorParams *extractor_params = (GstMlMetaExtractorParams *) params;
  extractor_params->parent_id = GPOINTER_TO_INT (key);
  extractor_params->metalist = (GList *) data;

  if (extractor_params->metalist == NULL)
    return;

  switch (extractor_params->process_type) {
    case PROCESS_TYPE_DETECTION:
      gst_mlmeta_extractor_add_detection_structs_to_list (extractor_params);
      break;

    case PROCESS_TYPE_POSE:
      gst_mlmeta_extractor_add_pose_structs_to_list (extractor_params);
      break;

    case PROCESS_TYPE_CLASSIFICATION:
      gst_mlmeta_extractor_add_class_structs_to_list (extractor_params);
      break;

    default:
      break;
  }
}

static GstFlowReturn
gst_mlmeta_extractor_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuffer)
{
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (parent);
  GstBuffer *outbuffer = NULL;
  GstDataQueueItem *item = NULL;
  GstMemory *mem = NULL;
  GValue output_list = G_VALUE_INIT;
  GstMlMetaExtractorParams extractor_params;
  gchar *output_string = NULL;
  gint string_len = 0;
  gint n_entries = 0, seq_index = 1;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, inbuffer);

  GST_MLMETA_EXTRACTOR_LOCK (extractor);

  // Adjust the source pad segment position.
  extractor->segment.position = GST_BUFFER_TIMESTAMP (inbuffer) +
      GST_BUFFER_DURATION (inbuffer);

  // Create a new buffer wrapper to hold a reference to input buffer.
  outbuffer = gst_buffer_new ();

  // If input is a GAP buffer set the GAP flag for the output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_GAP);

  gst_buffer_copy_into (outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  n_entries = gst_mlmeta_extractor_group_buffer_metas (extractor, inbuffer);

  g_value_init (&output_list, GST_TYPE_LIST);

  extractor_params.extractor = extractor;
  extractor_params.output_list = &output_list;
  extractor_params.inbuffer = inbuffer;
  extractor_params.metalist = NULL;
  extractor_params.process_type = PROCESS_TYPE_DETECTION;
  extractor_params.n_entries = n_entries;
  extractor_params.seq_index = &seq_index;
  extractor_params.parent_id = -1;

  g_hash_table_foreach (extractor->roimetas, gst_mlmeta_extractor_process_meta_entry,
      &extractor_params);

  extractor_params.process_type = PROCESS_TYPE_POSE;
  g_hash_table_foreach (extractor->ldmrkmetas, gst_mlmeta_extractor_process_meta_entry,
      &extractor_params);

  extractor_params.process_type = PROCESS_TYPE_CLASSIFICATION;
  g_hash_table_foreach (extractor->classmetas, gst_mlmeta_extractor_process_meta_entry,
      &extractor_params);

  g_hash_table_foreach (extractor->roimetas, g_hash_table_free_glists, extractor);
  g_hash_table_foreach (extractor->ldmrkmetas, g_hash_table_free_glists, extractor);
  g_hash_table_foreach (extractor->classmetas, g_hash_table_free_glists, extractor);

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
    GST_ERROR_OBJECT (pad, "Failed to serialize detection structure!");
    return GST_FLOW_ERROR;
  }

  string_len = strlen (output_string) + 1;
  output_string[string_len - 1] = '\n';

  GST_TRACE_OBJECT (pad, "Serialized output string: %s", output_string);

  mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_ZERO_PADDED,
      output_string, string_len, 0, string_len, output_string, g_free);
  gst_buffer_append_memory (outbuffer, mem);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (outbuffer);
  item->size = gst_buffer_get_size (outbuffer);
  item->duration = GST_BUFFER_DURATION (outbuffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the input buffer into the queue or free it on failure.
  if (!gst_data_queue_push (extractor->metapad->buffers, item))
    item->destroy (item);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (inbuffer);
  item->size = gst_buffer_get_size (inbuffer);
  item->duration = GST_BUFFER_DURATION (inbuffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (extractor->vpad->buffers, item))
    item->destroy (item);

  GST_MLMETA_EXTRACTOR_UNLOCK (extractor);

  GST_DEBUG_OBJECT (pad, "Finishing");

  return GST_FLOW_OK;
}

static gboolean
gst_mlmeta_extractor_sink_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (parent);

  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL, *intersect = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_mlmeta_extractor_sink_getcaps (pad, filter);

      // Get the negotiated caps between the video srcpad and its peer.
      filter = gst_pad_get_allowed_caps (GST_PAD (extractor->vpad));
      GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, filter);

      if (filter != NULL) {
        intersect = gst_caps_intersect_full (filter, caps,
            GST_CAPS_INTERSECT_FIRST);

        gst_caps_unref (caps);
        caps = intersect;
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_mlmeta_extractor_sink_acceptcaps (pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    case GST_QUERY_ALLOCATION:
    {
      return gst_pad_peer_query (GST_PAD (extractor->vpad), query);
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_mlmeta_extractor_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (parent);
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_mlmeta_extractor_sink_setcaps (extractor, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      gst_event_copy_segment (event, &segment);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(extractor->segment), GST_FORMAT_TIME);
        extractor->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(extractor->segment));
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(extractor->segment));
        gst_segment_copy_into (&segment, &(extractor->segment));
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }

      // Initialize and send the source segments for synchronization.
      event = gst_event_new_segment (&(extractor->segment));

      success = gst_element_foreach_src_pad (GST_ELEMENT (extractor),
          gst_mlmeta_extractor_src_pad_push_event, event);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_STREAM_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (extractor),
          gst_mlmeta_extractor_src_pad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (extractor),
          gst_mlmeta_extractor_src_pad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&(extractor->segment), GST_FORMAT_UNDEFINED);

      success = gst_element_foreach_src_pad (GST_ELEMENT (extractor),
          gst_mlmeta_extractor_src_pad_push_event, event);
      return success;
    case GST_EVENT_EOS:
      success = gst_element_foreach_src_pad (GST_ELEMENT (extractor),
          gst_mlmeta_extractor_src_pad_push_event, event);
      return success;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_mlmeta_extractor_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMlMetaExtractorSrcPad *srcpad = GST_MLMETA_EXTRACTOR_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_mlmeta_extractor_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (parent);
  GstMlMetaExtractorSrcPad *srcpad = GST_MLMETA_EXTRACTOR_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (srcpad, "Current caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (srcpad, "Filter caps: %" GST_PTR_FORMAT, filter);

      if (filter != NULL) {
        GstCaps *intersection  =
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (caps);
        caps = intersection;
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &(extractor->segment);
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (srcpad, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_SEGMENT:
    {
      GstSegment *segment = &(extractor->segment);
      gint64 start = 0, stop = 0;

      start = gst_segment_to_stream_time (segment, segment->format,
          segment->start);

      stop = (segment->stop == GST_CLOCK_TIME_NONE) ? segment->duration :
          gst_segment_to_stream_time (segment, segment->format, segment->stop);

      gst_query_set_segment (query, segment->rate, segment->format, start, stop);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_mlmeta_extractor_src_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  GST_INFO_OBJECT (pad, "%s worker task", active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (GST_MLMETA_EXTRACTOR_SRCPAD (pad)->buffers, FALSE);
        gst_data_queue_flush (GST_MLMETA_EXTRACTOR_SRCPAD (pad)->buffers);

        success = gst_pad_start_task (pad, gst_mlmeta_extractor_src_pad_worker_task,
            pad, NULL);
      } else {
        gst_data_queue_set_flushing (GST_MLMETA_EXTRACTOR_SRCPAD (pad)->buffers, TRUE);
        gst_data_queue_flush (GST_MLMETA_EXTRACTOR_SRCPAD (pad)->buffers);
        // TODO wait for all requests.
        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to %s worker task!",
        active ? "activate" : "deactivate");
    return FALSE;
  }

  GST_INFO_OBJECT (pad, "Worker task %s", active ? "activated" : "deactivated");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static void
gst_mlmeta_extractor_finalize (GObject * object)
{
  GstMlMetaExtractor *extractor = GST_MLMETA_EXTRACTOR (object);

  g_hash_table_destroy (extractor->classmetas);
  g_hash_table_destroy (extractor->ldmrkmetas);
  g_hash_table_destroy (extractor->roimetas);

  g_mutex_clear (&(extractor)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (extractor));
}

static void
gst_mlmeta_extractor_class_init (GstMlMetaExtractorClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->finalize     = GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_mlmeta_extractor_sink_template, GST_TYPE_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_mlmeta_extractor_video_src_template, GST_TYPE_MLMETA_EXTRACTOR_SRCPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_mlmeta_extractor_meta_src_template, GST_TYPE_MLMETA_EXTRACTOR_SRCPAD);

  gst_element_class_set_static_metadata (element,
      "Video mlmeta extractor", "Video/Demuxer/Converter",
      "Extract and copy mlmeta from video buffers into text buffers", "QTI"
  );

  // Initializes a new ML extractor GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_mlmeta_extractor_debug, "qtimlmetaextractor", 0,
      "QTI ML Meta Extractor");
}

static void
gst_mlmeta_extractor_init (GstMlMetaExtractor * extractor)
{
  GstPadTemplate *template = NULL;
  GstPad *sinkpad = NULL;

  g_mutex_init (&(extractor)->lock);

  extractor->roimetas = g_hash_table_new_full (NULL, NULL, NULL, NULL);
  extractor->ldmrkmetas = g_hash_table_new_full (NULL, NULL, NULL, NULL);
  extractor->classmetas = g_hash_table_new_full (NULL, NULL, NULL, NULL);

  template = gst_static_pad_template_get (&gst_mlmeta_extractor_sink_template);
  sinkpad = g_object_new (GST_TYPE_PAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  template = gst_static_pad_template_get (&gst_mlmeta_extractor_video_src_template);
  extractor->vpad = g_object_new (GST_TYPE_MLMETA_EXTRACTOR_SRCPAD, "name", "video",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  template = gst_static_pad_template_get (&gst_mlmeta_extractor_meta_src_template);
  extractor->metapad = g_object_new (GST_TYPE_MLMETA_EXTRACTOR_SRCPAD, "name", "meta",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_sink_chain));
  gst_pad_set_query_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_sink_pad_query));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_sink_pad_event));

  gst_pad_set_query_function (GST_PAD (extractor->vpad),
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_src_pad_query));
  gst_pad_set_event_function (GST_PAD (extractor->vpad),
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_src_pad_event));
  gst_pad_set_activatemode_function (GST_PAD (extractor->vpad),
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_src_pad_activate_mode));

  gst_pad_set_query_function (GST_PAD (extractor->metapad),
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_src_pad_query));
  gst_pad_set_event_function (GST_PAD (extractor->metapad),
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_src_pad_event));
  gst_pad_set_activatemode_function (GST_PAD (extractor->metapad),
      GST_DEBUG_FUNCPTR (gst_mlmeta_extractor_src_pad_activate_mode));

  gst_element_add_pad (GST_ELEMENT (extractor), sinkpad);
  gst_element_add_pad (GST_ELEMENT (extractor), GST_PAD (extractor->vpad));
  gst_element_add_pad (GST_ELEMENT (extractor), GST_PAD (extractor->metapad));
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
