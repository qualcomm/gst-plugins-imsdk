/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>

// Set the default debug category.
#define GST_CAT_DEFAULT gst_meta_module_debug
GST_DEBUG_CATEGORY_STATIC (gst_meta_module_debug);

#define GST_META_SUB_MODULE_CAST(obj) ((GstMetaSubModule*)(obj))

#define DEFAULT_MAX_RECORDS 10

typedef struct _GstMetaSubModule GstMetaSubModule;

struct _GstMetaSubModule {
  // Mapping between ROI meta type(name) and its recorded values over time.
  GHashTable *roi_label_records;

  /// Properties.
  guint      maxrecords;
};

static void
gst_region_label_records_cleanup (GArray * records)
{
  g_array_free (records, TRUE);
}

static GstClassLabel *
gst_region_label_records_majority_vote (GArray * records)
{
  GstClassLabel *label = NULL, *current_label = NULL, *next_label = NULL;
  guint idx = 0, num = 0, n_entries = 0, n_max_entries = 0;

  if ((records == NULL) || (records->len == 0))
    return NULL;

  for (idx = 0; idx < records->len; idx++, n_entries = 0) {
    current_label = &(g_array_index (records, GstClassLabel, idx));

    for (num = (idx + 1); num < records->len; num++) {
      next_label = &(g_array_index (records, GstClassLabel, num));

      if (current_label->name != next_label->name)
        continue;

      n_entries++;
    }

    if (n_entries < n_max_entries)
      continue;

    label = current_label;
  }

  return label;
}

gpointer
gst_meta_module_open (GstStructure * settings)
{
  GstMetaSubModule *submodule = NULL;

  GST_DEBUG_CATEGORY_GET (gst_meta_module_debug, "meta-transform-module");

  submodule = g_slice_new0 (GstMetaSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  submodule->roi_label_records = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gst_region_label_records_cleanup);
  submodule->maxrecords = DEFAULT_MAX_RECORDS;

  // No module settings, nothing further to do.
  if (settings == NULL)
    return (gpointer) submodule;

  if (gst_structure_has_field (settings, "max-records"))
    gst_structure_get_uint (settings, "max-records", &(submodule->maxrecords));

  return (gpointer) submodule;
}

void
gst_meta_module_close (gpointer instance)
{
  GstMetaSubModule *submodule = GST_META_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  if (submodule->roi_label_records != NULL)
    g_hash_table_destroy (submodule->roi_label_records);

  g_slice_free (GstMetaSubModule, submodule);
}

gboolean
gst_meta_module_process (gpointer instance, GstBuffer * buffer)
{
  GstMetaSubModule *submodule = GST_META_SUB_MODULE_CAST (instance);
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL;

  // Iterate over the metas available in the buffer and process them.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    GstVideoClassificationMeta *classmeta = NULL;
    GstStructure *objparam = NULL;
    GstClassLabel *label = NULL, *toplabel = NULL;
    GArray *records = NULL,  *labels = NULL;
    GList *metalist = NULL;
    gpointer key = NULL;
    guint tracking_id = 0;

    // Skip if ROI is a ImageRegion with actual data (populated by vsplit).
    // This is primary used for blitting only pixels with actual data.
    if (roimeta->roi_type == g_quark_from_static_string ("ImageRegion"))
      continue;

    objparam = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");

    if (!gst_structure_has_field (objparam, "tracking-id"))
      continue;

    gst_structure_get_uint (objparam, "tracking-id", &tracking_id);

    // Fetch the temporal records for this ROI meta. // here?
    key = GUINT_TO_POINTER (tracking_id);
    records = g_hash_table_lookup (submodule->roi_label_records, key);

    // No records for this ROI meta, create a records queue.
    if (records == NULL) {
      records = g_array_new (FALSE, TRUE, sizeof (GstClassLabel));
      g_hash_table_insert (submodule->roi_label_records, key, records);
    }

    GST_TRACE ("Received root ROI meta %s and ID [0x%X], tracking-id: %u",
        g_quark_to_string (roimeta->roi_type), roimeta->id, tracking_id);

    metalist =
        gst_buffer_get_video_classification_metas_parent_id (buffer, roimeta->id);

    // Expecting at the most a single classification meta.
    if (metalist != NULL) {
      classmeta = GST_VIDEO_CLASSIFICATION_META_CAST (metalist->data);
      label = &(g_array_index (classmeta->labels, GstClassLabel, 0));

      GST_TRACE ("Current label %s, confidence %.2f, color %X",
          g_quark_to_string (label->name), label->confidence, label->color);

      records = g_array_append_vals (records, label, 1);

      // Records exceed maximum depth, discard old entires.
      while (records->len > submodule->maxrecords)
        records = g_array_remove_index (records, 0);
    } else {
      labels = g_array_sized_new (FALSE, TRUE, sizeof (GstClassLabel), 1);
      g_array_set_size (labels, 1);

      label = &(g_array_index (labels, GstClassLabel, 0));
    }

    g_list_free (metalist);

    // Find the what is the ROI type according to the accumulated records.
    toplabel = gst_region_label_records_majority_vote (records);

    if (toplabel != NULL) {
      label->name = toplabel->name;
      label->confidence = toplabel->confidence;
      label->color = toplabel->color;
    } else {
      label->name = g_quark_from_static_string ("UNKNOWN");
      label->confidence = 0.0;
      label->color = 0xFF0000FF;
    }

    GST_TRACE ("Top label %s, confidence %.2f, color %X",
        g_quark_to_string (label->name), label->confidence, label->color);

    // Update the color of the root ROI meta.
    gst_structure_set (objparam, "color", G_TYPE_UINT, label->color, NULL);

    // ImageClassification doesn't exist, add new metadata into the buffer.
    if (labels != NULL) {
      classmeta = gst_buffer_add_video_classification_meta (buffer, labels);
      classmeta->parent_id = roimeta->id;
    }
  }

  return TRUE;
}
